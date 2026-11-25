#include "service.h"
#include <Arduino.h>
#include <functional>
#include <LittleFS.h>
#include <WiFi.h>
#include "console.h"
#include "console_reply.h"
#include "host/eggs.h"
#include "host/protocol.h"
#include "log.h"
#include "net.h"
#include "relay.h"
#include "settings.h"
#include "usb_port.h"
#include "wall_clock.h"

namespace {
constexpr uint32_t LOOP_MS = 20;
constexpr size_t FLASH_SPARE = 64 * 1024;          // kept free in the pets' flash, for littlefs's own blocks

// The name and address, for a while after joining and after a press of BOOT. Being off Wi-Fi is setup's line.
std::string caption_for(bool online, bool naming, const std::string& host, const std::string& ip) {
    if (!naming) return "";
    return online ? host + ".local  " + ip : host + ".local - NO WI-FI";
}
}  // namespace

void service_task(void* arg) {
    App& app = *static_cast<App*>(arg);
    Console commands(app);
    Upkeep upkeep(app);
    for (;;) {
        commands.poll();
        const std::string log = drain_log();
        if (!log.empty()) console.print(log.c_str());
        const uint32_t now = millis();
        app.radio.tick(now);
        upkeep.tick(now);
        upkeep.load_coming_pet();
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}

void Upkeep::tick(uint32_t now) {
    if (now - last_ < INTERVAL_MS) return;
    last_ = now;
    const bool online = follow_wifi(now);
    sync_clock(now);
    follow_host();
    report_usb_sleep(now);
    update_caption(now, online);
    store_pet_change(now);
    save_changed_pet();
    check_health(now, online);
    restart_if_asked(now);
    follow_relay(&app_);
}

// Keeps the pets' flash for GET /v1/pets: total and used bytes, read before taking the lock (LittleFS walks its
// blocks to count them).
static void keep_flash(nestbox::Health& h, size_t total, size_t used) {
    h.flash_total = total;
    h.flash_used = used;
}

// The chip's temperature, the Wi-Fi signal and the pets' flash, for GET /v1 and /v1/pets; a hot chip slows the frames
// until it cools.
void Upkeep::check_health(uint32_t now, bool online) {
    if (health_at_ && now - health_at_ < HEALTH_MS) return;
    health_at_ = now;
    const float c = temperatureRead();
    if (!app_.hot && c >= HOT_C) {
        app_.hot = true;
        console.printf("health: %.0f C, slowing down until it cools\n", c);
    } else if (app_.hot && c <= COOL_C) {
        app_.hot = false;
        console.printf("health: %.0f C, back to full speed\n", c);
    }
    const int dbm = online ? int(WiFi.RSSI()) : 0;
    const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    app_.with_lock([&](PetCore& core) {
        keep_flash(core.dev.health, total, used);
        core.dev.health.temperature_c = c;
        if (online) core.dev.health.wifi_dbm = dbm;
        else core.dev.health.wifi_dbm.reset();
    });
}

void Upkeep::restart_if_asked(uint32_t now) {
    const uint32_t at = app_.with_lock([](PetCore& c) { return c.dev.restart_at; });
    if (!at || int32_t(now - at) < 0) return;
    console.println("restarting, as asked over the API");
    console.flush();
    esp_restart();
}

bool Upkeep::follow_wifi(uint32_t now) {
    net::tick(now);
    const bool online = net::online();
    if (online && !was_online_) {
        online_since_ = now;
        console.printf("%s%s\n", console_reply::WIFI, net::status().c_str());
    }
    was_online_ = online;
    if (online) wall_clock::start();
    return online;
}

void Upkeep::sync_clock(uint32_t now) {
    if (wall_clock::just_synced() || (wall_clock::synced() && now - clock_at_ >= CLOCK_REFRESH_MS)) {
        clock_at_ = now;
        app_.update_clock();
    }
}

// The name changes with `name` on the console and when mDNS has to pick another; the API's pages follow it.
void Upkeep::follow_host() {
    const std::string host = net::host();
    if (host == shown_host_) return;
    shown_host_ = host;
    app_.with_lock([&](PetCore& c) {
        c.dev.device_name = host;
        c.dev.base_url = "http://" + host + ".local";
    });
}

// A computer suspends its USB ports when it goes to sleep.
void Upkeep::report_usb_sleep(uint32_t now) {
    const bool asleep = usb::host_asleep(now);
    if (asleep == told_asleep_ || !app_.has_pets()) return;
    told_asleep_ = asleep;
    app_.post_input(nestbox::computer_input(asleep));
}

void Upkeep::update_caption(uint32_t now, bool online) {
    const bool naming = (online && now - online_since_ < SHOW_NAME_MS) || int32_t(app_.show_name_until - now) > 0;
    const std::string ip = online ? net::ip() : "";
    app_.with_lock([&](PetCore& c) { c.caption = caption_for(online, naming, shown_host_, ip); });
}

// A pet added over the API goes into the pets' flash here, outside the lock (the change it comes from holds still
// until it is finished), then onto the shelf. One taken off comes off the shelf first, then its file is deleted: if
// the pet out can't make way (the next pet can't be loaded), its file stays.
void Upkeep::store_pet_change(uint32_t now) {
    using Kind = nestbox::Device::PetChange::Kind;
    Kind kind = Kind::None;
    std::string id, done;
    const uint8_t* bytes = nullptr;
    size_t size = 0;
    bool removed = false;
    app_.with_lock([&](PetCore& c) {
        const nestbox::Device::PetChange* p = nestbox::pet_change_to_store(c.dev, now);
        if (!p) return;
        kind = p->kind;
        id = p->id;
        bytes = p->egg.bytes.data();
        size = p->egg.bytes.size();
        if (kind != Kind::Remove) return;
        nestbox::finish_pet_change(c.dev, "", millis());
        removed = !c.shelf.has(id);
        done = c.dev.pet_change_done;
    });
    if (kind == Kind::None) return;
    const std::string path = "/" + id + nestbox::EGG_EXTENSION, part = path + nestbox::PART_SUFFIX;
    if (kind == Kind::Remove) {
        if (removed && LittleFS.exists(path.c_str()) && !LittleFS.remove(path.c_str()))
            done += " (its file can't be deleted: it is back after a restart)";
        const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
        app_.with_lock([&](PetCore& c) { keep_flash(c.dev.health, total, used); });
        console.printf("pets: %s\n", done.c_str());
        return;
    }
    std::string error;
    const size_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (size + FLASH_SPARE > free) {
        error = "no room left: " + std::to_string(size / 1024) + " KB needed, " + std::to_string(free / 1024) +
                " KB free; take another pet off first";
    } else {
        File f = LittleFS.open(part.c_str(), "w");
        const size_t written = f ? f.write(bytes, size) : 0;
        if (f) f.close();
        if (written != size || !LittleFS.rename(part.c_str(), path.c_str())) {
            LittleFS.remove(part.c_str());
            error = "its file can't be written";
        }
    }
    const size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
    app_.with_lock([&](PetCore& c) {
        keep_flash(c.dev.health, total, used);
        nestbox::finish_pet_change(c.dev, error, millis());
        done = c.dev.pet_change_done;
    });
    console.printf("pets: %s\n", done.c_str());
}

void Upkeep::load_coming_pet() {
    std::string id;
    std::function<bool(const std::string&, nestbox::Egg&)> load;
    std::function<void(const std::function<void()>&)> in_memory;
    app_.with_lock([&](PetCore& c) {
        id = nestbox::pet_to_load(c.dev);
        load = c.shelf.load;
        in_memory = c.shelf.egg_memory;
    });
    if (id.empty() || !load) return;
    nestbox::Egg egg;
    bool read = false;
    const auto step = [&] { read = load(id, egg); };
    if (in_memory) in_memory(step);
    else step();
    app_.with_lock([&](PetCore& c) { nestbox::finish_pet_load(c.dev, id, read ? &egg : nullptr, millis()); });
}

void Upkeep::save_changed_pet() {
    std::string id, name, device;
    app_.with_lock([&](PetCore& c) {
        id.swap(c.changed_pet);
        if (id.empty()) return;
        if (c.shelf.size()) name = c.shelf.current_egg().name;
        device = setup_name(c);                        // the name given in setup, else the pet's
        c.setup.rename(device);
    });
    if (id.empty()) return;
    settings::put(settings::CHOSEN_PET, id);
    net::announce(device);
    console.printf("pet: %s\n", name.c_str());
}
