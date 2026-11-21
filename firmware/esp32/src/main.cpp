// NestBox on the Waveshare ESP32-S3-LCD-1.69 or LCD-2 (board.h): the engine's frame on core 1 at 30 fps, and on core 0 the network,
// the sensors, USB and the service task (service.h). Every task reaches the pet through App::with_lock().
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <algorithm>
#include <string>
#include <vector>
#include "app.h"
#include "board.h"
#include "button.h"
#include "console.h"
#include "host/eggs.h"
#include "host/frame.h"
#include "host/palette.h"
#include "host/protocol.h"
#include "screen/slices.h"
#include "imu.h"
#include "lcd.h"
#include "log.h"
#include "net.h"
#include "service.h"
#include "settings.h"
#include "usb_port.h"
#include "util/text.h"
#include "wall_clock.h"

using namespace nestbox;

namespace {
constexpr uint32_t SENSE_MS = 5;
constexpr uint32_t BOOT_COUNTDOWN_MS = 2000;   // without PWR: the forget-Wi-Fi countdown shows after this hold
constexpr uint32_t CONSOLE_TX_TIMEOUT_MS = 100;
constexpr int BACKLIGHT_FRAME = 2;           // the first frame is fully on the panel once the second is sent
constexpr int FADE_STEP = 2;                 // backlight percent a frame while dimming or brightening: about 1 s
constexpr int HOT_BACKLIGHT = 50;            // the most the backlight shows while the chip is hot (App::hot)

struct TaskSpec {
    const char* name;
    uint32_t stack;
    UBaseType_t priority;
    BaseType_t core;
};
constexpr TaskSpec SERVICE_TASK = {"service", 8192, 1, 0};
constexpr TaskSpec SENSE_TASK = {"sense", 8192, 2, 0};
constexpr TaskSpec FRAME_TASK = {"frame", 16384, 3, 1};

bool start_task(TaskFunction_t run, const TaskSpec& t, App* app) {
    if (xTaskCreatePinnedToCore(run, t.name, t.stack, app, t.priority, nullptr, t.core) == pdPASS) return true;
    note("%s: cannot start the task (%u bytes of stack)\n", t.name, unsigned(t.stack));
    return false;
}

// On battery, PWR only switches the board on; it stays on while SYS_EN is held high.
void hold_power() {
    if (!board::HAS_PWR) return;
    pinMode(board::SYS_EN, OUTPUT);
    digitalWrite(board::SYS_EN, HIGH);
    pinMode(board::SYS_OUT, INPUT_PULLUP);
    pinMode(board::BUZZER, OUTPUT);          // a passive buzzer left high heats the board (Waveshare FAQ)
    digitalWrite(board::BUZZER, LOW);
}

void load_egg_files(Shelf& shelf) {
    std::vector<std::string> files, unfinished;
    File dir = LittleFS.open("/");
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (is_egg_file(f.name())) files.push_back(f.name());
        else if (ends_with(f.name(), PART_SUFFIX)) unfinished.push_back(f.name());
    }
    for (const std::string& name : unfinished) LittleFS.remove(("/" + name).c_str());   // power lost mid-write
    std::sort(files.begin(), files.end(), egg_file_before);
    for (const std::string& name : files) {
        File f = LittleFS.open(("/" + name).c_str());
        const size_t size = f.size(), room = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        if (size + board::PSRAM_SPARE > room) {
            console.printf("pets: skipping %s (%u KB): only %u KB of PSRAM left\n", name.c_str(), unsigned(size / 1024),
                           unsigned(room / 1024));
            continue;
        }
        std::vector<uint8_t> bytes(size);
        if (f.read(bytes.data(), size) != size) {
            console.printf("pets: cannot read %s\n", name.c_str());
            continue;
        }
        Egg egg;
        std::string err;
        if (!load_egg(std::move(bytes), egg, err)) {
            console.printf("pets: skipping %s: %s\n", name.c_str(), err.c_str());
            continue;
        }
        console.printf("pets: %s (%u KB)\n", egg.name.c_str(), unsigned(size / 1024));
        shelf.add(std::move(egg), egg_id(name));
    }
}

// Every egg in the data image, in shelf order, each read whole into PSRAM. An egg is also many small tables (each
// animation's frames, the scene's parts), and malloc puts anything under CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL bytes in
// internal RAM, which the tasks' stacks and Wi-Fi need; with a full shelf no task could start. So while the eggs
// load, every allocation goes to PSRAM.
void load_eggs(Shelf& shelf) {
    heap_caps_malloc_extmem_enable(1);
    load_egg_files(shelf);
    heap_caps_malloc_extmem_enable(CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL);
}

// Without eggs there is no pet: the screen says how to flash them, and the console keeps working.
void show_no_pets(PetCore& core, int brightness) {
    struct Line {
        const char* text;
        int y;
        uint16_t color;
    };
    constexpr int X = 14, SIZE = 2;
    const Line lines[] = {{"No pets on this device.", 90, HOST_TEXT},
                          {"Flash the data image:", 116, HOST_TEXT},
                          {"pio run -t uploadfs", 142, HOST_ACCENT}};
    core.screen.fill(HOST_DARK);
    for (const Line& l : lines) core.screen.text(l.text, X, l.y, SIZE, l.color);
    lcd::push(core.screen.px.data());
    lcd::wait_idle();
    lcd::backlight(brightness);
}

// The key, Lock and text size kept in flash; changes are written back as they happen.
void load_api_settings(PetCore& core) {
    nestbox::Settings& s = core.dev.settings;
    s.key = settings::get(settings::API_KEY);
    s.locked = settings::get_flag(settings::LOCKED, false);
    s.text = settings::get_int(settings::TEXT_SIZE, 2);
    nestbox::ScreenSettings& sc = s.screen;
    sc = nestbox::screen_defaults(core.display);
    sc.bright = settings::get_int(settings::BRIGHTNESS, sc.bright);
    sc.dim = settings::get_int(settings::DIM, sc.dim);
    sc.dim_after = settings::get_int(settings::DIM_AFTER, sc.dim_after);
    sc.off_after = settings::get_int(settings::OFF_AFTER, sc.off_after);
    sc.night_from = settings::get_int(settings::NIGHT_FROM, sc.night_from);
    sc.night_to = settings::get_int(settings::NIGHT_TO, sc.night_to);
    sc.care = settings::get_flag(settings::CARE, sc.care);   // the LCD panels show the status bar's rest, not the shift
    core.brain.text_size(s.text);
    core.dev.save = [](const nestbox::Settings& kept) {
        settings::put(settings::API_KEY, kept.key);
        settings::put_flag(settings::LOCKED, kept.locked);
        settings::put_int(settings::TEXT_SIZE, kept.text);
        const nestbox::ScreenSettings& k = kept.screen;
        settings::put_int(settings::BRIGHTNESS, k.bright);
        settings::put_int(settings::DIM, k.dim);
        settings::put_int(settings::DIM_AFTER, k.dim_after);
        settings::put_int(settings::OFF_AFTER, k.off_after);
        settings::put_int(settings::NIGHT_FROM, k.night_from);
        settings::put_int(settings::NIGHT_TO, k.night_to);
        settings::put_flag(settings::CARE, k.care);
    };
}

void show_saved_pet(PetCore& core) {
    core.dev.random = [] { return uint32_t(esp_random()); };
    load_api_settings(core);
    put_out_first(core.dev, core.display, settings::get(settings::CHOSEN_PET), millis());
    // Pets added over the API go straight onto the shelf, and the data image only changes with a reflash: no rescan.
    core.shelf.on_change = [&core](const std::string& id) { core.changed_pet = id; };
}

// Without saved Wi-Fi, setup over the air starts (named as in an earlier setup, if any), unless offline mode was
// chosen. With it, setup watches that network.
void start_setup(App& app, const std::string& ssid) {
    const bool offline = settings::get(settings::OFFLINE) == "1";
    app.with_lock([&](PetCore& c) {
        if (offline) c.setup.offline(app.radio);
        else if (ssid.empty()) c.setup.begin(app.radio, setup_name(c), millis());
        else c.setup.watch(app.radio, setup_name(c), ssid);
    });
}

void frame_task(void* arg) {
    App& app = *static_cast<App*>(arg);
    LookLog log;
    int frame = 0;
    bool flipped = app.flip;
    int64_t next = esp_timer_get_time();
    // Two frames: one is drawn while the other goes to the panel.
    std::vector<uint16_t> sending;
    std::vector<uint32_t> sending_held;             // what `sending` holds, as the sender fingerprinted it
    app.with_lock([&](PetCore& c) { sending.resize(c.screen.px.size()); });
    int shown = 0;                                   // the backlight now, percent
    bool asleep = false;                             // the screen rests off: nothing is sent and the panel sleeps
    for (;;) {
        if (app.flip != flipped) {
            flipped = app.flip;
            lcd::set_flip(flipped);          // waits for the bus itself
        }
        const int64_t start = esp_timer_get_time();
        int64_t locked = start, drawn = start, sent = start;
        int target = 0;
        bool off = false;
        app.with_lock([&](PetCore& c) {
            locked = esp_timer_get_time();
            const uint32_t now = millis();   // read under the lock, so time never steps back for the brain
            const Look s = step_frame(c.dev, now, Caption{c.caption, HOST_TEXT});
            const std::string line = log.line(s, now, c.brain.why());
            if (!line.empty()) note("%s", line.c_str());
            target = nestbox::backlight_now(c.dev);
            off = c.dev.screen_state == nestbox::ScreenState::Off;
            drawn = esp_timer_get_time();
            if (off) return;                 // nothing new was drawn
            lcd::wait_idle();                // the frame before is on the panel: its buffer is free again
            sent = esp_timer_get_time();
            c.screen.px.swap(sending);       // the screen keeps the frame before, whole, for anyone reading it
            c.screen.held.swap(sending_held);
        });
        if (app.hot) target = std::min(target, HOT_BACKLIGHT);
        if (off) {
            // Fade out, then sleep the panel: a dark screen that stops sending also runs the board cooler.
            shown = std::max(0, shown - FADE_STEP);
            lcd::backlight(shown);
            if (!shown && !asleep) {
                lcd::sleep(true);
                asleep = true;
                note("screen: off\n");
            }
        } else {
            if (asleep) {
                lcd::sleep(false);           // this frame goes whole; the backlight comes up once it is in
                asleep = false;
                frame = 0;
                note("screen: awake\n");
            }
            sending_held.resize(size_t(lcd::H) * nestbox::SLICES);   // filled by the sender before it comes back
            lcd::push(sending.data(), sending_held.data());
            if (frame < BACKLIGHT_FRAME) {
                if (++frame == BACKLIGHT_FRAME) lcd::backlight(shown = target);   // waking shows at once
            } else if (shown != target) {
                shown += std::clamp(target - shown, -FADE_STEP, FADE_STEP);
                lcd::backlight(shown);
            }
        }
        const int64_t end = esp_timer_get_time();
        app.stats.add(uint32_t(end - start), uint32_t(locked - start), uint32_t(sent - drawn));
        // A late frame moves the schedule rather than rushing to catch up.
        next += app.hot ? 2 * FRAME_US : FRAME_US;   // hot: half the frames
        const int64_t left_us = std::max<int64_t>(0, next - esp_timer_get_time());
        if (!left_us) next = esp_timer_get_time();
        vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(left_us / 1000)));
    }
}

// The motion sensor, the PWR button and the BOOT button (a press shows the address; held 5 s and let go, it starts
// setup over; held 20 s, it resets everything but the pets).
// Without PWR, BOOT is the pet's button too: press, double press, and a hold let go before the countdown shows at
// BOOT_COUNTDOWN_MS is the long press (quiet). Held on, the countdown runs; letting go then shows the address.
void sense_task(void* arg) {
    App& app = *static_cast<App*>(arg);
    const bool imu_ok = imu::begin();
    note("imu: %s\n", imu_ok ? "QMI8658 ready" : "no QMI8658 found, taps and shakes are off");
    Button power, boot;
    bool boot_was_held = false, boot_long = false;
    uint32_t boot_down_ms = 0;
    pinMode(board::BOOT, INPUT_PULLUP);
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = millis();
        if (imu_ok)
            if (const char* gesture = imu::poll(now)) app.post_input(sensor_input(gesture));
        if (board::HAS_PWR)
            if (const char* press = power.poll(digitalRead(board::SYS_OUT) == LOW, now))
                app.post_input(button_input(press));
        const bool boot_down = digitalRead(board::BOOT) == LOW;
        if (const char* press = boot.poll(boot_down, now)) {
            if (board::HAS_PWR) app.show_name_until = now + Upkeep::SHOW_NAME_MS;
            else if (std::string(press) == "long") boot_long = true;   // decided when it is let go
            else app.post_input(button_input(press));
        }
        if (boot.held() != boot_was_held) {   // the hold's countdown and what it does are in door/setup.h
            boot_was_held = boot.held();
            if (boot_was_held) boot_down_ms = now;
            else if (boot_long) {
                boot_long = false;
                if (now - boot_down_ms < BOOT_COUNTDOWN_MS) app.post_input(button_input("long"));
                else app.show_name_until = now + Upkeep::SHOW_NAME_MS;
            }
            app.post_input(boot_was_held ? R"({"boot":"down"})" : R"({"boot":"up"})");
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSE_MS));
    }
}
}  // namespace

void setup() {
    hold_power();
    const bool drive = usb::begin();         // the computer sees the device from here on
    console.setTxTimeoutMs(CONSOLE_TX_TIMEOUT_MS);   // only the service task waits on USB
    settings::begin();
    wall_clock::set_tz(settings::get(settings::TIME_ZONE));
    const bool flip = settings::get_flag(settings::FLIP, false);

    if (!drive) console.println("usb: no drive image (pio run -t upload writes it)");
    // Before App: the LCD's DMA buffer needs internal RAM, which App's canvases would otherwise not leave.
    const bool lcd_ok = lcd::begin(flip);
    if (!lcd_ok) console.println("lcd: setup failed; running without the screen");
    App* app = new App;
    app->flip = flip;
    const bool data_image = LittleFS.begin(false);
    if (!data_image) console.println("pets: no data image (pio run -t uploadfs)");

    const std::string id = net::device_id();
    const bool fits = app->with_lock([&](PetCore& c) {
        c.dev.id = id;
        if (!board::HAS_PWR) c.setup.set_hold_show(BOOT_COUNTDOWN_MS);
        if (data_image) load_eggs(c.shelf);
        const bool same_size = c.screen.w == lcd::W && c.screen.h == lcd::H;
        if (!same_size) console.printf("lcd: screen profile %s is not %dx%d\n", board::SCREEN_PROFILE, lcd::W, lcd::H);
        if (c.shelf.size() > 0) show_saved_pet(c);
        else if (same_size && lcd_ok)
            show_no_pets(c, settings::get_int(settings::BRIGHTNESS, settings::DEFAULT_BRIGHTNESS));
        return same_size;
    });
    print_memory();
    const bool has_pets = app->has_pets();
    if (has_pets) net::announce(app->with_lock([](PetCore& c) { return setup_name(c); }));

    std::string host = settings::get(settings::HOST);   // renamed with `name`, else nestbox-xxxx
    if (!net::valid_host(host)) host = net::default_host();
    const std::string ssid = settings::get(settings::SSID);
    net::begin(host, ssid, settings::get(settings::PASSWORD),
               [app](const std::string& method, const std::string& path, const std::string& body,
                     const std::string& host_header, const std::string& origin, const std::string& auth,
                     const std::string& peer, bool setup_network) {
                   return app->request(method, path, body, false, host_header, origin, auth, peer, setup_network);
               });
    app->radio.begin();
    if (has_pets) start_setup(*app, ssid);

    // From here on only the service task writes to USB; without it, its log goes out once, here.
    console.printf("nestbox %s ready; type help\n", NESTBOX_VERSION);
    if (!start_task(service_task, SERVICE_TASK, app)) {
        console.print(drain_log().c_str());
        return;
    }
    if (has_pets) start_task(sense_task, SENSE_TASK, app);
    if (has_pets && fits && lcd_ok) start_task(frame_task, FRAME_TASK, app);
}

// The tasks above do the work. This one is suspended, not deleted: setup() used the network here, and deleting the
// task freed its lwIP thread semaphore from the idle task, which crashed about one start in three (core dumps in
// prvIdleTask, sys_thread_sem_free). Its 8 KB stack stays.
void loop() { vTaskSuspend(nullptr); }
