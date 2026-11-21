#include "console.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "host/frame.h"
#include "host/protocol.h"
#include "imu.h"
#include "lcd.h"
#include "console_reply.h"
#include "net.h"
#include "usb_port.h"
#include "wall_clock.h"

namespace {
constexpr size_t MAX_LINE = 300;

const char* const HELP =
    "commands:\n"
    "  wifi <ssid> <password>   join a network (remembered); quote a name with spaces: wifi \"My Net\" secret\n"
    "  wifi                     how the connection is\n"
    "  forget                   forget the network and start setup again\n"
    "  tell <json>              as POST /v1/tell, e.g. tell {\"from\":\"me\",\"state\":\"done\"}\n"
    "  now | pets | words | api as GET /v1, GET /v1/pets, GET /v1/words, GET /api\n"
    "  key                      the key from pairing; key new makes a new one (the old one stops working)\n"
    "  lock on|off              whether every API request needs the key (remembered)\n"
    "  tz <POSIX TZ>            the time zone, e.g. tz CET-1CEST,M3.5.0,M10.5.0/3 (remembered); tz - clears\n"
    "  tz                       the time and zone in use\n"
    "  name <word>              rename it: <word>.local (lowercase letters, digits, hyphens; remembered);\n"
    "                           name - goes back to its own name; name shows the name in use and its id\n"
    "  usb                      what the USB port sees (a computer, asleep or awake)\n"
    "  rotate                   turn the picture upside down (remembered)\n"
    "  bright <1-100>           backlight while awake (remembered)\n"
    "  imu                      print every bump and swing, to tune tap/shake (again to stop)\n"
    "  perf                     frame times and free memory\n"
    "  reboot\n";

// `s` without leading and trailing white space (isspace).
std::string trim_space(const std::string& s) {
    size_t first = 0, last = s.size();
    while (first < last && std::isspace(static_cast<unsigned char>(s[first]))) first++;
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) last--;
    return s.substr(first, last - first);
}

bool starts_with(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

void print(const std::string& s) { console.println(s.c_str()); }

// A JSON reply on one line, so a program reading the console line by line gets it whole.
void print_json(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
    console.println(s.c_str());
}

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}
}  // namespace

void print_memory() {
    console.printf("memory: %u KB internal free, %u KB PSRAM free\n",
                   unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                   unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

const Console::Command Console::COMMANDS[] = {
    {"help", Arg::None, &Console::help},
    {"?", Arg::None, &Console::help},
    {"wifi", Arg::Optional, &Console::wifi},
    {"forget", Arg::None, &Console::forget},
    {"tz", Arg::Optional, &Console::time_zone},
    {"name", Arg::Optional, &Console::name},
    {"usb", Arg::None, &Console::usb},
    {"tell", Arg::Required, &Console::tell},
    {"now", Arg::None, &Console::now},
    {"pets", Arg::None, &Console::get_pets},
    {"words", Arg::None, &Console::words},
    {"api", Arg::None, &Console::get_api},
    {"key", Arg::Optional, &Console::key},
    {"lock", Arg::Optional, &Console::lock},
    {"rotate", Arg::None, &Console::rotate},
    {"bright", Arg::Number, &Console::bright},
    {"imu", Arg::None, &Console::imu},
    {"perf", Arg::None, &Console::perf},
    {"reboot", Arg::None, &Console::reboot},
};

Console::Console(App& app) : app_(app) {}

void Console::poll() {
    while (console.available()) {
        const char c = char(console.read());
        if (c == '\n' || c == '\r') {
            run(line_);
            line_.clear();
        } else if (line_.size() < MAX_LINE) {
            line_ += c;
        }
    }
}

// True if `line` invokes `c`; its argument goes to `arg`.
bool Console::matches(const Command& c, const std::string& line, std::string& arg) {
    const std::string name = c.name;
    const bool bare = line == name, spaced = starts_with(line, name + " ");
    switch (c.arg) {
        case Arg::None:
            return bare;
        case Arg::Optional:
        case Arg::Required:
            if (spaced) arg = line.substr(name.size() + 1);
            return spaced || (bare && c.arg == Arg::Optional);
        case Arg::Number:
            if (!starts_with(line, name)) return false;
            arg = trim_space(line.substr(name.size()));
            return all_digits(arg);
    }
    return false;
}

void Console::run(const std::string& raw) {
    const std::string line = trim_space(raw);
    if (line.empty()) return;
    for (const Command& c : COMMANDS) {
        std::string arg;
        if (matches(c, line, arg)) {
            (this->*c.run)(arg);
            return;
        }
    }
    console.println("unknown command; type help");
}

void Console::help(const std::string&) { console.print(HELP); }

// wifi <ssid> <password>, or wifi "<ssid with spaces>" <password>; no argument: the connection's status.
void Console::wifi(const std::string& arg) {
    const std::string rest = trim_space(arg);
    if (rest.empty()) {
        console.printf("%s%s\n", console_reply::WIFI, net::status().c_str());
        return;
    }
    std::string ssid, pass;
    if (rest[0] == '"') {
        const size_t end = rest.find('"', 1);
        if (end == std::string::npos) {
            console.println("wifi: missing the closing quote");
            return;
        }
        ssid = rest.substr(1, end - 1);
        pass = rest.substr(end + 1);
    } else {
        const size_t sp = rest.find(' ');
        ssid = rest.substr(0, sp);
        pass = sp == std::string::npos ? "" : rest.substr(sp + 1);
    }
    pass = trim_space(pass);
    settings::put(settings::SSID, ssid);
    settings::put(settings::PASSWORD, pass);
    settings::remove(settings::OFFLINE);         // a network given here also ends offline mode
    app_.with_lock([&](PetCore& c) {
        c.setup.end();
        c.setup.watch(app_.radio, setup_name(c), ssid);
    });
    net::join(ssid, pass);
    console.printf("%s%s%s\"%s (remembered)\n", console_reply::WIFI, console_reply::JOINING, ssid.c_str(),
                   pass.empty() ? " with no password" : "");
}

// Setup forgets the network (the radio's own request) and starts again.
void Console::forget(const std::string&) {
    app_.with_lock([](PetCore& c) { c.setup.start_over(millis()); });
    console.println("wifi: forgotten; setup starts again");
}

// tz <POSIX rule>, "tz -" to clear; no argument: the time and zone in use.
void Console::time_zone(const std::string& arg) {
    if (!arg.empty()) {
        std::string tz = trim_space(arg);
        if (tz == "-") tz = "";
        settings::put(settings::TIME_ZONE, tz);
        wall_clock::set_tz(tz);
        app_.update_clock();
    }
    print(wall_clock::status());
}

// name <host>, "name -" for the default; no argument: the name in use and the id.
void Console::name(const std::string& arg) {
    if (arg.empty()) {
        console.printf("name: %s.local (id %s, own name %s)\n", net::host().c_str(), net::device_id().c_str(),
                       net::default_host().c_str());
        return;
    }
    std::string n = trim_space(arg);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (n == "-") {
        settings::remove(settings::HOST);
        n = net::default_host();
    } else if (!net::valid_host(n)) {
        console.println("name: use lowercase letters, digits and hyphens (not at either end), up to 63");
        return;
    } else {
        settings::put(settings::HOST, n);
    }
    net::rename(n);
    console.printf("name: now %s.local\n", n.c_str());
}

void Console::usb(const std::string&) { console.println(usb::status()); }

// The console counts as the device's own: it needs no key, the cable is the proof.
void Console::tell(const std::string& json) { print_json(app_.request("POST", nestbox::TELL_PATH, json, true).body); }
void Console::now(const std::string&) { print_json(app_.request("GET", nestbox::API_PATH, "", true).body); }
void Console::get_pets(const std::string&) { print_json(app_.request("GET", nestbox::PETS_PATH, "", true).body); }
void Console::words(const std::string&) { print_json(app_.request("GET", nestbox::WORDS_PATH, "", true).body); }

// key: shows the key from pairing; "key new" makes a new one, and the old one stops working at once.
void Console::key(const std::string& arg) {
    if (trim_space(arg) == "new") {
        print_json(app_.request("POST", nestbox::API_PATH, R"({"key":"new"})", true).body);
        return;
    }
    const std::string k = app_.with_lock([](PetCore& c) { return c.dev.settings.key; });
    console.printf("key: %s\n", k.empty() ? "none yet (pair from the Mac app)" : k.c_str());
}

// lock on|off: whether every API request needs the key; no argument: the setting.
void Console::lock(const std::string& arg) {
    const std::string a = trim_space(arg);
    if (a == "on" || a == "off")
        app_.request("POST", nestbox::API_PATH, a == "on" ? R"({"lock":true})" : R"({"lock":false})", true);
    const bool locked = app_.with_lock([](PetCore& c) { return c.dev.settings.locked; });
    console.printf("lock: %s\n", locked ? "on (every request needs the key)" : "off (telling is open)");
}
void Console::get_api(const std::string&) { console.print(app_.request("GET", "/api", "", true).body.c_str()); }

void Console::rotate(const std::string&) {
    const bool flip = !app_.flip;
    settings::put_flag(settings::FLIP, flip);
    app_.flip = flip;                            // the frame task owns the SPI bus and applies it
}

void Console::bright(const std::string& percent) {
    const int b = int(std::clamp(std::strtol(percent.c_str(), nullptr, 10), 1L, 100L));
    app_.with_lock([&](PetCore& c) {     // the frame task fades to it
        c.dev.settings.screen.bright = b;
        if (c.dev.save) c.dev.save(c.dev.settings);
        nestbox::wake(c.dev, millis());
    });
}

void Console::imu(const std::string&) {
    imu::debug = !imu::debug;
    console.printf("imu: debug %s\n", imu::debug ? "on" : "off");
}

namespace {
// Why the chip last started, in words.
const char* reset_reason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "power on";
        case ESP_RST_SW: return "a restart";
        case ESP_RST_PANIC: return "a crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT: return "a watchdog";
        case ESP_RST_BROWNOUT: return "low power (brownout)";
        case ESP_RST_USB: return "USB";
        case ESP_RST_DEEPSLEEP: return "waking from sleep";
        case ESP_RST_EXT: return "the reset pin";
        default: return "something else";
    }
}
}  // namespace

void Console::perf(const std::string&) {
    const uint32_t frames = app_.stats.frames.exchange(0), max_us = app_.stats.max_us.exchange(0);
    const uint64_t total_us = app_.stats.total_us.exchange(0), wait_us = app_.stats.wait_us.exchange(0),
                   send_us = app_.stats.send_us.exchange(0);
    const uint32_t n = std::max<uint32_t>(1, frames);
    console.printf("frames: %u, %.1f ms average (lock %.1f, draw %.1f, panel %.1f), %.1f ms worst (budget %d ms)\n",
                   unsigned(frames), total_us / 1000.0 / n, wait_us / 1000.0 / n,
                   (total_us - wait_us - send_us) / 1000.0 / n, send_us / 1000.0 / n, max_us / 1000.0,
                   int(nestbox::FRAME_US / 1000));
    uint32_t sent_frames = 0;
    uint64_t sent_px = 0;
    lcd::take_send_stats(sent_frames, sent_px);
    if (sent_frames)
        console.printf("panel: %.1f%% of each frame's pixels sent, the rest unchanged\n",
                       100.0 * double(sent_px) / (double(sent_frames) * lcd::W * lcd::H));
    const uint32_t up_s = uint32_t(esp_timer_get_time() / 1000000);
    console.printf("up %uh %02um %02us, started by %s; chip %.0f C\n", unsigned(up_s / 3600), unsigned(up_s / 60 % 60),
                   unsigned(up_s % 60), reset_reason(), temperatureRead());
    print_memory();
}

void Console::reboot(const std::string&) { ESP.restart(); }
