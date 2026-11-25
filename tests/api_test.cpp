// The HTTP API (engine/door/api.cpp) on a device with no pet out: which hosts and origins it answers, the key, Lock
// and pairing, the size and rate limits, the replies and warnings it gives, the device's own input, changing the
// device, and what it serves for reading.
// Usage: api_test
#include "door/api.h"
#include "door/setup.h"
#include <string>
#include "check.h"
#include "util/json.h"

using namespace nestbox;
using test::check;

namespace {

constexpr uint32_t S = 1000;  // one second, in ms
constexpr const char* HOST = "nestbox-test.local";

// A device answering to nestbox-test.local, with an empty shelf.
struct Door {
    Brain brain;
    Pet pet;
    Shelf shelf;
    Canvas screen{280, 240};
    Device device{brain, pet, shelf, screen, "nestbox-test", "http://nestbox-test.local", "test"};
    Settings saved;
    int saves = 0;

    Door() {
        device.random = [n = 0u]() mutable { return n += 2654435761u; };
        device.save = [this](const Settings& s) {
            saved = s;
            saves++;
        };
    }
    Response call(const std::string& method, const std::string& path, const std::string& body, uint32_t now,
                  const std::string& host = HOST, const std::string& origin = "", bool local = false,
                  const std::string& auth = "", const std::string& peer = "10.0.0.5") {
        return handle_request(device, method, path, body, now, host, origin, local, auth, peer);
    }
    Response tell(const std::string& body, uint32_t now, const std::string& auth = "") {
        return call("POST", "/v1/tell", body, now, HOST, "", false, auth);
    }
    Response input(const std::string& body, uint32_t now) { return call("POST", "/v1/input", body, now, "", "", true); }
    Response get(const std::string& path, uint32_t now, const std::string& auth = "") {
        return call("GET", path, "", now, HOST, "", false, auth);
    }
    // Pairs as the Mac app does; returns the key.
    std::string pair(uint32_t now) {
        call("POST", "/v1/pair", R"({"name":"Studio Mac"})", now);
        input(R"({"press":"single"})", now + S);
        Fields f;
        parse_json(call("POST", "/v1/pair", R"({"name":"Studio Mac"})", now + 2 * S).body, f);
        return f.str("key");
    }
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

Fields json(const std::string& body) {
    Fields f;
    if (!parse_json(body, f)) test::fail("a reply that is JSON: " + body);
    return f;
}

void hosts_and_origins() {
    Door d;
    const std::string done = R"({"state":"done"})";
    check(d.tell(done, 5000).status == 200, "its own name: ok");
    check(d.call("POST", "/v1/tell", done, 5000, "nestbox-test.local:80").status == 200, "with a port: ok");
    check(d.call("POST", "/v1/tell", done, 5000, "192.168.1.40").status == 200, "its address: ok");
    check(d.call("POST", "/v1/tell", done, 5000, "").status == 200, "no Host: ok");
    check(d.call("POST", "/v1/tell", done, 5000, "evil.example.com").status == 403, "a foreign name: refused");
    check(d.call("GET", "/v1", "", 5000, "evil.example.com").status == 403, "a foreign name can't read either");
    check(d.call("POST", "/v1/tell", done, 5000, HOST, "https://evil.example.com").status == 403,
          "another site's page: refused");
    check(d.call("POST", "/v1/tell", done, 5000, HOST, "null").status == 403, "a sandboxed page: refused");
    check(d.call("POST", "/v1/tell", done, 5000, HOST, "http://nestbox-test.local").status == 200, "its own page: ok");
}

void the_device_is_its_own() {
    Door d;
    for (const char* body : {R"({"press":"long"})", R"({"press":"double"})", R"({"computer":"asleep"})"})
        check(d.call("POST", "/v1/input", body, 5000).status == 403,
              std::string("input from the network: refused ") + body);
    check(d.tell(R"({"from":"device","say":"hi"})", 5000).status == 403, "the device's name from the network: refused");
    check(d.tell(R"({"from":"Device","say":"hi"})", 5000).status == 403, "in capitals too");
    check(json(d.input(R"({"press":"single"})", 6000).body).str("input") == "petted", "its own button pats the pet");
    check(json(d.input(R"({"press":"long"})", 7000).body).str("input") == "quiet on", "a long press: quiet");
    check(json(d.input(R"({"gesture":"shake"})", 8000).body).str("input") == "dizzy", "a shake: dizzy");
}

void tell_replies() {
    Door d;
    Fields f = json(d.tell(R"({"from":"ci","state":"failed","say":"build broke"})", 5000).body);
    check(f.str("ok") == "true" && f.str("note.from") == "ci" && f.str("note.state") == "failed",
          "the reply names the note");
    check(f.str("note.showing.0") == "do" && f.str("note.ends_in") == "8", "what of it shows, and when it ends");
    check(f.str("now.doing") == "sulk" && f.str("now.why") == "ci failed" && f.str("now.say") == "build broke",
          "what the pet shows now and why");
    check(f.has("warnings"), "warnings are always there, maybe empty");
    f = json(d.tell(R"({"from":"ci","state":"over"})", 6000).body);
    check(f.str("ended") == "1", "over says how many notes ended");
    f = json(d.tell(R"({"from":"x","state":"running","colour":"red"})", 7000).body);
    check(
        f.str("ok") == "true" && contains(f.str("warnings"), "unknown state") && contains(f.str("warnings"), "colour"),
        "bad input: ok, with warnings");
    Response r = d.tell(R"({"mood":"great"})", 8000);
    check(r.status == 400 && contains(r.body, "\"help\""), "nothing usable: 400, with the help's address");
    check(d.tell("not json", 9000).status == 400, "not JSON: 400");
    check(d.tell("", 9500).status == 200, "an empty body is an empty tell");
    check(d.call("GET", "/v1/tell", "", 10000).status == 405, "the wrong method: 405");
    check(d.get("/v1/nothing", 10000).status == 404, "no such route: 404");
}

void limits() {
    Door d;
    check(d.tell(std::string(R"({"say":")") + std::string(1100, 'a') + "\"}", 5000).status == 413,
          "a body over 1024 bytes: 413");
    int refused = 0;
    for (int i = 0; i < 25; i++)
        if (d.tell(R"({"say":"hi"})", 60 * S).status == 429) refused++;
    check(refused == 5, "20 requests a second from the network; more are refused");
    check(d.tell(R"({"say":"hi"})", 61 * S).status == 200, "the next second, fine again");
    check(d.input(R"({"gesture":"shake"})", 61 * S).status == 200, "the device's own input is never limited");
}

void the_key_and_pairing() {
    Door d;
    Response r = d.call("POST", "/v1/user", R"({"keys":3})", 5000);
    check(r.status == 401 && contains(r.body, "pair first"), "activity needs the key; none yet: pair first");
    check(d.call("POST", "/v1", R"({"pet":"next"})", 5000).status == 401, "changing the device needs the key");
    Fields f = json(d.call("POST", "/v1/pair", R"({"name":"Studio Mac"})", 6000).body);
    check(f.str("pairing") == "waiting", "pairing waits for the button");
    check(contains(d.get("/v1", 6000).body, "Pair with Studio Mac"), "the pet asks the user");
    check(d.call("POST", "/v1/pair", R"({"name":"Other"})", 6500, HOST, "", false, "", "10.0.0.9").status == 409,
          "another address can't pair meanwhile");
    f = json(d.call("POST", "/v1/pair", R"({"name":"Studio Mac"})", 7000).body);
    check(f.str("pairing") == "waiting" && !f.has("key"), "no key without the button");
    check(json(d.input(R"({"press":"single"})", 8000).body).str("input") == "paired", "the button: consent");
    check(d.call("POST", "/v1/pair", R"({"name":"Studio Mac"})", 8500, HOST, "", false, "", "10.0.0.9").status == 409,
          "only the address that asked can collect it");
    f = json(d.call("POST", "/v1/pair", R"({"name":"Studio Mac"})", 9000).body);
    const std::string key = f.str("key");
    check(f.str("pairing") == "done" && key.rfind("nb_", 0) == 0 && key.size() == 27 && f.str("id") == "test",
          "then the same request gets the key and the device's id");
    check(d.saves == 1 && d.saved.key == key, "the key is kept");
    const std::string bearer = "Bearer " + key;
    check(d.call("POST", "/v1/user", R"({"keys":3})", 10000, HOST, "", false, bearer).status == 200,
          "with the key, activity is heard");
    check(d.call("POST", "/v1/user", R"({"keys":3})", 10000, HOST, "", false, "Bearer nb_wrong").status == 401,
          "a wrong key: 401");
    check(d.call("POST", "/v1/user", R"({"keys":3})", 10000, "", "", true).status == 200,
          "the device itself needs no key");
    check(d.tell(R"({"say":"hi"})", 11000).status == 200, "telling stays open");
    check(json(d.get("/v1", 11000).body).str("device.paired") == "true", "GET /v1 says it is paired");
}

void lock() {
    Door d;
    const std::string key = d.pair(5000);
    Fields f = json(d.call("POST", "/v1", R"({"lock":true})", 10000, HOST, "", false, "Bearer " + key).body);
    check(contains(f.str("warnings"), "on the device itself"), "Lock is not set over the network");
    d.call("POST", "/v1", R"({"lock":true})", 11000, "", "", true);
    check(d.device.settings.locked, "the device sets Lock");
    check(d.tell(R"({"say":"hi"})", 12000).status == 401, "locked: telling needs the key");
    check(d.get("/v1", 12000).status == 401, "and so does reading");
    check(d.tell(R"({"say":"hi"})", 12000, "Bearer " + key).status == 200, "with the key: fine");
    check(d.get("/", 12000).status == 200 && d.get("/api", 12000).status == 200, "its page and the help stay open");
    check(json(d.get("/v1", 12000, "Bearer " + key).body).str("device.tell") == "key", "GET /v1 says it is locked");
    f = json(d.call("POST", "/v1", R"({"key":"new"})", 13000, "", "", true).body);
    check(!f.str("key").empty() && f.str("key") != key, "the device makes a new key");
    check(d.tell(R"({"say":"hi"})", 14000, "Bearer " + key).status == 401, "the old one stops working at once");
}

void changing_the_device() {
    Door d;
    const std::string bearer = "Bearer " + d.pair(5000);
    Fields f = json(d.call("POST", "/v1", R"({"time":"22:30"})", 10000, HOST, "", false, bearer).body);
    check(f.str("time") == "22:30", "forcing the time of day");
    d.call("POST", "/v1", R"({"time":"auto"})", 11000, HOST, "", false, bearer);
    f = json(d.call("POST", "/v1", R"({"text":"large"})", 12000, HOST, "", false, bearer).body);
    check(d.device.settings.text == 3 && d.brain.text_size() == 3 && d.saved.text == 3, "text size, kept");
    f = json(d.call("POST", "/v1", R"({"text":"huge","volume":3})", 13000, HOST, "", false, bearer).body);
    check(contains(f.str("warnings"), "\\\"text\\\" is") && contains(f.str("warnings"), "volume"),
          "bad values: warnings");
    check(d.call("POST", "/v1", R"({})", 14000, HOST, "", false, bearer).status == 400, "nothing to change: 400");
}

// POST /v1 {"name"}: only where the platform can take it, only a valid name, and "-" for its own.
void renaming() {
    Door d;
    const std::string bearer = "Bearer " + d.pair(5000);
    auto rename = [&](const std::string& body, uint32_t now) {
        return json(d.call("POST", "/v1", body, now, HOST, "", false, bearer).body);
    };
    check(contains(rename(R"({"name":"desk-goose"})", 10000).str("warnings"), "can't be changed") &&
              d.device.rename_to.empty(),
          "a device that can't take a new name says so");
    d.device.renames = true;
    check(rename(R"({"name":"Desk-Goose"})", 11000).str("warnings") == "[]" && d.device.rename_to == "desk-goose",
          "a new name, lower-cased, for the platform to take");
    d.device.rename_to.clear();
    for (const char* bad : {R"({"name":"-desk"})", R"({"name":"desk goose"})", R"({"name":""})", R"({"name":"gänse"})"})
        check(contains(rename(bad, 12000).str("warnings"), "lower-case letters") && d.device.rename_to.empty(),
              std::string("refused: ") + bad);
    check(rename(R"({"name":"-"})", 13000).str("warnings") == "[]" && d.device.rename_to == "-", "\"-\": its own name back");
    check(d.call("POST", "/v1", R"({"name":"x"})", 14000).status == 401, "renaming needs the key");
    check(valid_device_name(std::string(63, 'a')) && !valid_device_name(std::string(64, 'a')), "up to 63 characters");
}

void reading() {
    Door d;
    d.tell(R"({"from":"claude","id":"7f3a","state":"working","line":"editing app.ts"})", 5000);
    Fields f = json(d.get("/v1", 6000).body);
    check(
        f.str("notes.0.from") == "claude" && f.str("notes.0.id") == "7f3a" && f.str("notes.0.line") == "editing app.ts",
        "GET /v1 lists the notes");
    check(f.str("device.name") == "nestbox-test" && f.str("device.id") == "test" && f.str("device.api") == "1",
          "and the device");
    check(f.str("device.screen.width") == "280" && f.str("device.tell") == "open", "its screen and whether it is open");
    f = json(d.get("/v1/words", 6000).body);
    check(f.has("states.working") && f.has("behaviours.throw") && f.str("limits.request") == "1024" &&
              f.str("limits.steps") == "8" && f.str("signs.2") == "...",
          "GET /v1/words: every word and the limits");
    const std::string help = d.get("/api", 6000).body;
    check(contains(help, "POST /v1/tell") && contains(help, "curl nestbox-test.local/v1/tell"), "the help page");
    const Response page = d.get("/", 6000);
    check(page.type.rfind("text/html", 0) == 0 && contains(page.body, "<title>") && d.get("/welcome", 6000).body == page.body,
          "its address serves its page, the welcome page too");
    check(contains(d.get("/agents", 6000).body, "POST http://nestbox-test.local/v1/tell"), "the guide for agents");
    check(json(d.get("/v1/pets", 6000).body).str("ok") == "true", "an empty shelf still answers");
    check(d.get("/v1/pets/nobody", 6000).status == 404, "a pet that isn't there: 404");
    check(json(d.get("/v1/debug", 6000).body).has("x"), "the body's details for tests");
}

}  // namespace

// When the screen rests (door/rest.h): dimmed, then off, sooner when nobody is there or at night; what wakes it.
void resting() {
    constexpr uint32_t MIN = 60 * S;
    ScreenSettings s;
    check(rest_state(s, 59 * MIN, false, std::nullopt) == ScreenState::Awake &&
              rest_state(s, 60 * MIN, false, std::nullopt) == ScreenState::Dim &&
              rest_state(s, 180 * MIN, false, std::nullopt) == ScreenState::Off,
          "by default it dims after an hour and goes off after three");
    check(rest_state(s, 5 * MIN, true, std::nullopt) == ScreenState::Dim &&
              rest_state(s, 30 * MIN, true, std::nullopt) == ScreenState::Off,
          "with nobody at the computer, sooner");
    s.night_from = 23 * 60;
    s.night_to = 7 * 60;
    check(rest_state(s, 2 * MIN, false, 23 * 60 + 30) == ScreenState::Dim &&
              rest_state(s, 10 * MIN, false, 6 * 60) == ScreenState::Off &&
              rest_state(s, 10 * MIN, false, 7 * 60) == ScreenState::Awake,
          "night hours across midnight rest it sooner, and end on time");
    s = ScreenSettings{};
    s.dim_after = s.off_after = 0;
    check(rest_state(s, 24 * 60 * MIN, true, std::nullopt) == ScreenState::Awake, "\"never\" holds, even with nobody there");
    s = ScreenSettings{};
    check(backlight_percent(s, ScreenState::Awake, false) == 70 && backlight_percent(s, ScreenState::Dim, false) == 20 &&
              backlight_percent(s, ScreenState::Off, false) == 0 && backlight_percent(s, ScreenState::Awake, true) == 50,
          "backlight: 70 % awake, 20 % dimmed, off; 50 % for a handheld's few colours");

    const ScreenSettings oled = screen_defaults(*display_named("amoled-2.41"));
    const ScreenSettings lcd = screen_defaults(*display_named("lcd-1.69"));
    check(oled.care && oled.bright == 55 && oled.dim == 15 && oled.dim_after == 15 && oled.off_after == 60 &&
              !lcd.care && lcd.bright == 70 && lcd.dim == 20 && lcd.off_after == 180,
          "an OLED screen rests sooner and dimmer, with burn-in care; an LCD keeps the defaults");
    check(pixel_shift(lcd, 4 * SHIFT_MS).x == 0 && pixel_shift(oled, 0).x == 0 && pixel_shift(oled, SHIFT_MS).x == 1 &&
              pixel_shift(oled, 2 * SHIFT_MS).y == 1 && pixel_shift(oled, 8 * SHIFT_MS).x == 0,
          "burn-in care moves the picture a pixel round a small square, every few minutes");

    Door d;
    const std::string key = d.pair(0);
    Fields f = json(d.call("POST", "/v1", R"({"screen":{"bright":70,"dim_after":15,"night":"22:30-06:00"}})", 3 * S, HOST,
                           "", false, "Bearer " + key).body);
    check(f.str("ok") == "true" && d.saved.screen.bright == 70 && d.saved.screen.dim_after == 15 &&
              d.saved.screen.night_from == 22 * 60 + 30 && d.saved.screen.night_to == 6 * 60,
          "POST /v1 sets the screen's brightness, rests and night hours, and keeps them");
    f = json(d.call("POST", "/v1", R"({"screen":{"bright":0,"night":"late","glow":1}})", 4 * S, HOST, "", false,
                    "Bearer " + key).body);
    check(contains(f.str("warnings"), "screen.bright") && contains(f.str("warnings"), "screen.night") &&
              contains(f.str("warnings"), "screen.glow") && d.device.settings.screen.bright == 70,
          "bad screen values are refused one by one, with a warning each");
    check(d.call("POST", "/v1", R"({"screen":{"bright":20}})", 5 * S).status == 401, "changing the screen needs the key");
    f = json(d.get("/v1", 6 * S).body);
    check(f.str("device.screen.bright") == "70" && f.str("device.screen.night") == "22:30-06:00" &&
              f.str("device.screen.state") == "awake" && f.str("device.screen.backlight") == "70",
          "GET /v1 says how the screen rests and what it shows now");

    const uint32_t told = 10 * S;
    d.tell(R"({"say":"hi"})", told, "Bearer " + key);
    check(update_rest(d.device, told + 14 * MIN) == ScreenState::Awake &&
              update_rest(d.device, told + 15 * MIN) == ScreenState::Dim && backlight_now(d.device) == 20,
          "a tell starts the rest over: dimmed after 15 minutes");
    d.device.settings.screen.off_after = 20;
    check(update_rest(d.device, told + 20 * MIN) == ScreenState::Off && backlight_now(d.device) == 0, "then off");
    Response r = d.input(R"({"press":"single"})", told + 21 * MIN);
    check(contains(r.body, "woke the screen") && d.device.screen_state == ScreenState::Awake,
          "a press in the dark only wakes the screen");
    r = d.input(R"({"press":"single"})", told + 21 * MIN + S);
    check(!contains(r.body, "woke the screen"), "the next press is a pat again");
    d.device.settings.screen.dim_after = 1;
    d.input(R"({"computer":"asleep"})", told + 30 * MIN);
    check(update_rest(d.device, told + 30 * MIN + S) == ScreenState::Dim, "the computer going to sleep is no activity");
    f = json(d.call("POST", "/v1", R"({"screen":{"care":true}})", told + 30 * MIN + 2 * S, HOST, "", false,
                    "Bearer " + key).body);
    check(d.device.settings.screen.care && json(d.get("/v1", told + 30 * MIN + 3 * S).body).str("device.screen.care") == "true",
          "burn-in care can be turned on for any screen");
    check(json(d.call("POST", "/v1", R"({"restart":true})", told + 31 * MIN, HOST, "", false, "Bearer " + key).body)
                  .str("ok") == "true" && d.device.restart_at == told + 31 * MIN + S,
          "a restart is asked for, a second later so the reply gets out");
}

// A radio that finds no networks and joins nothing: enough for setup to open its access point.
struct QuietRadio : SetupRadio {
    void scan() override {}
    bool scanned(std::vector<WifiNet>&) override { return true; }
    void open_ap(const std::string&, const std::string&, int) override {}
    void close_ap() override {}
    int phones() override { return 0; }
    void join(const std::string&, const std::string&, int) override {}
    Join joined(std::string&) override { return Join::Trying; }
    void keep(const std::string&, const std::string&) override {}
    Link link() override { return Link::Unseen; }
    void hold_saved(bool) override {}
    void forget() override {}
    void reset_all() override {}
    void set_time(int64_t, const std::string&, int32_t) override {}
    void save(const std::string&, const std::string&) override {}
    uint32_t random() override { return 4; }
    std::string id() override { return "a1b2c3"; }
    std::string host() override { return "nestbox-test"; }
};

// Setup's pages need no key, so only a phone on the device's own access point gets them.
void setup_only_over_its_network() {
    Door d;
    QuietRadio radio;
    Setup setup;
    d.device.setup = &setup;
    setup.begin(radio, "Nibble", 0);
    uint32_t t = 0;
    while (!setup.ap_open() && t < 60 * S) setup.update(t += 100);
    check(setup.ap_open(), "setup opens its access point");
    const std::string ap = Setup::AP_IP;
    Response r = handle_request(d.device, "GET", "/setup/networks", "", t, ap, "", false, "", "192.168.4.2", true);
    check(r.status == 200 && contains(r.body, "networks"), "a phone on the access point gets setup's API");
    r = handle_request(d.device, "GET", "/setup/networks", "", t, "192.168.1.40", "", false, "", "192.168.1.7", false);
    check(r.status == 404, "from the home network it is not there");
    r = handle_request(d.device, "POST", "/setup/finish", R"({"name":"x"})", t, "192.168.1.40", "", false, "",
                       "192.168.1.7", false);
    check(r.status != 200, "nor can the home network finish setup");
    r = handle_request(d.device, "GET", "/", "", t, "192.168.1.40", "", false, "", "192.168.1.7", false);
    check(r.status == 200 && !contains(r.body, "/setup/"), "the home network gets the device's own page");
    r = handle_request(d.device, "GET", "/ping.png", "", t, "192.168.1.40", "", false, "", "192.168.1.7", false);
    check(r.status == 200 && r.type == "image/png", "the handover's ping answers on the home network");
}

int main() {
    setup_only_over_its_network();
    resting();
    hosts_and_origins();
    the_device_is_its_own();
    tell_replies();
    limits();
    the_key_and_pairing();
    lock();
    changing_the_device();
    renaming();
    reading();
    return test::summary();
}
