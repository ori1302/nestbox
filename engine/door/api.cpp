#include "door/api.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include "door/guide.h"
#include "door/page.h"
#include "door/pet_svg.h"
#include "door/setup.h"
#include "util/deadline.h"
#include "util/json.h"
#include "util/text.h"

namespace nestbox {

namespace {

// The numbers HELP states in words.
static_assert(MAX_BODY == 1024 && MAX_REQUESTS_PER_S == 20 && StepChain::MAX_STEPS == 8);
static_assert(Note::MAX_SAY == 72 && Note::MAX_CAPTION == 60 && Note::MAX_LINE == 60 && Note::MAX_LABEL == 8);
static_assert(Brain::WORKING_MS == 15 * Brain::MINUTE_MS && Brain::ASKING_MS == Brain::HOUR_MS);
static_assert(Brain::FAILING_MS == Brain::HOUR_MS && Brain::ON_MS == 15 * Brain::MINUTE_MS);
static_assert(Brain::QUIET_MS == Brain::HOUR_MS && Brain::MOMENT_MS == 8 * Brain::SECOND_MS);
static_assert(Brain::DAY_MS == 24 * Brain::HOUR_MS && Step::MAX_COUNT == 20 && User::NEXT_DEFAULT_S == 10);

// A pairing, or a pet being added or taken off, waits this long for the button.
constexpr uint32_t PAIR_MS = 60 * 1000;
constexpr size_t PET_ID_MAX = 32;
constexpr uint32_t RESTART_DELAY_MS = 1000;  // "restart" waits this long, so the reply gets out first
constexpr const char* SWITCH_NOTE = "switch";       // the device's note while a pet coming out is read
constexpr uint32_t SWITCH_NOTE_MS = 10000;            // at most; it ends when the pet comes out
constexpr uint32_t FORGET_DELAY_MS = 1000;  // "wifi": "forget" waits this long, so the reply leaves on that Wi-Fi
constexpr size_t KEY_CHARS = 24;
constexpr float AT_PX = 12;                 // this close to its home, it is "at" home (or its desk)

const char* const HELP = R"HELP(NESTBOX  a pet on your desk. Tell it what is going on.             API v1

  curl {HOST}/v1/tell -d '{"from":"ci","state":"failed"}'

The reply says what your request did, what the pet shows now and why,
and warns about anything it could not use.

ROUTES
  GET  /              its own page: pets, screen, settings
  GET  /api           this page
  GET  /agents        setup for coding agents; the same at /llms.txt
  GET  /v1            what the pet is doing now, and why; the device
  GET  /v1/pets       the pets on this device
  GET  /v1/pets/NAME  one pet: what it can do, its places, its scene
  GET  /v1/pets/NAME/picture  how it stands, as a PNG
  GET  /v1/words      every word on this page, and the limits, as JSON
  POST /v1/tell       tell the pet something
  POST /v1/user       a computer reports the user's activity      (key)
  POST /v1            change the pet out, time, text, screen      (key)
  POST /v1/pair       get the key; confirm with the pet's button
  PUT  /v1/pets/NAME  add a pet, or replace it: its .egg file as the body
  DELETE /v1/pets/NAME  take a pet off this device
                      (both: the key, or confirm with the pet's button)
  Key:  -H "Authorization: Bearer $NESTBOX_KEY"  Locked: all /v1 but pair.

TELL  a JSON object. Every key is optional. Each tell replaces what you
      told before about the same thing, so leave out what should go.
  from     who you are: "ci". You change and end only what is yours.
  id       which of your things: "build-12". "a/b" is a part of "a".
  state    what is going on:
             working  in progress           done    it ended well
             asking   it needs the user     failed  it ended badly
             failing  wrong, and going on   over    it ended, no reaction
             on       simply going on               (and so do its parts;
             quiet    do not disturb                 without id: all yours)
  do       what the body does: a behaviour, an animation of the pet's own
           (see /v1/pets/NAME), or a list of up to 8 steps. Pets differ:
           what one lacks falls back to what it has, with a warning.
             stand stroll run sleep type watch eat wave celebrate sulk
             dance surprised throw
  at       where: "here" "home" "desk" "target", or [x,y] in percent of
           the pet's scene from its top left
  aim      what a throw aims at, like "at". Default "target".
  face     front front-right right back-right back back-left left
           front-left, or a place or point to face
  times    how many times: 1-20        speed  "slow", "fast", or 1.5
  say      words in a bubble beside the pet         (72 characters)
  caption  words along the bottom                   (60)
  line     a line in the status bar; it scrolls     (60)
  gauge    0-100, or "timer" to count down "for".  label: text in it (8)
  sign     ! ? ... zzz heart star check cross note angry shock
  effect   notes sparkles confetti letter
  color    red orange yellow green blue purple pink white
  for      how long, in real time: "8s" "5m" "2h". A number is seconds.
  until    "seen": until the user taps the pet. "back": until they return.
  A step is a behaviour, or {do at aim face times for speed say sign}.
  A step with only "at" is walked through: a list of points is a path.
  On the way to "at": through [[x,y],...] one smooth curve through up to
  8 points; moving: an animation to play on the way; spin: turns a second.
  false switches off a part that a state brings:  "line":false

STATES  bring a look of their own. Any key above changes that part only.
  working  watches its desk, line "<from> working"           for 15m
  asking   waves, sign ?, line "<from> needs you"            for 1h
  failing  sulks, sign !, red line "<from> failing"          for 1h
  on       nothing but what you send                         for 15m
  quiet    stays put                                         for 1h
  done     celebrates, line "<from> done"                    for 8s
  failed   sulks, red line "<from> failed"                   for 8s
  no state: a passing moment, as long as "do" takes, at least 8s
  Time counts from your last tell, 24h at most. Unless you give "for",
  asking, done and failed do not count the time the user is away. While
  they are away, done and failed show their look once, then only their
  line waits; the look plays again when they are back.
  done and failed wait for parts still working.

WHAT SHOWS  each part of the screen shows the highest note that has it:
  the user's hand > asking > done, failed, no state (newest first)
  > working > failing > on > the pet's own life
  For the body, a "do" you give comes first for 8 s, then waits its
  state's turn. Senders share the body: one that keeps it with its
  own looks while another waits gives way after 8 s, for a while.
  Lines and gauges show several notes at once.
  Quiet: only asking gets the body and words. Lines and gauges stay,
  passing moments are skipped, done and failed wait until it ends.
  A tap on the pet: done, failed and "until" notes end; a question
  stops waving but keeps its line and sign. A double press hides
  everything. Seen or hidden lasts until the state or "say" changes.

USER  POST /v1/user (key). Numbers and fixed words, never what was typed.
  from   which computer             keys   key presses since last report
  idle   seconds without input      state  "awake" "locked" "asleep"
  time   local time "14:32"         next   seconds to the next report
  Report at once when typing starts or stops, every second while it
  goes on, and at least every "next" seconds (default 10).

DEVICE  POST /v1 (key)
  pet    bring out another pet: its name, "next" or "previous"
  time   "22:30" forces the time of day for an hour. "auto" lets go.
  text   the size of words in bubbles and gauges: "small" "medium" "large"
  screen {"bright":70, "dim":20, "dim_after":60, "off_after":180,
         "night":"23:00-07:00", "care":true}: brightness and dimmed
         brightness in percent; minutes with nothing going on before it
         dims or turns off (0 never); night hours or "off"; burn-in care,
         on for OLED. With nobody there, or at night, it rests sooner.
  restart true: it restarts after replying
  wifi   "forget": it forgets its Wi-Fi and asks for a new one
  relay  {"mailbox":..., "secret":..., "on":true}: webhooks through the
         NestBox relay (the Mac app sets them); "forget" clears them.
         GET /v1 shows device.relay.state, never the keys.

REPLY
  ok        false only if nothing could be used (then "error" and "help")
  note      yours: state, showing (its parts on screen now), why (when
            something is not showing), ends_in (seconds)
  now       pet, doing, at, why, and the words and line on screen
  warnings  one sentence for each thing ignored or changed
  200 done, maybe with warnings   400 not JSON          401 needs the key
  403 not from here               413 over 1024 bytes   429 too fast

PRIVACY  Send short descriptions: a file's name, the kind of command.
  Never prompts, paths, commands or code. Over 1024 bytes is refused.
)HELP";

struct ApiRequest {
    const std::string& method;
    const std::string& path;
    const std::string& body;
    uint32_t now;
    bool local;
    const std::string& auth;
    const std::string& peer;
};

Response json_reply(const std::string& body) {
    Response r;
    r.body = body + "\n";
    return r;
}

Response error_reply(const Device& d, int status, const std::string& message) {
    Response r;
    r.status = status;
    r.body = JsonWriter().begin_object().field("ok", false).field("error", message).field("help", d.base_url + "/api")
                 .end_object().text() + "\n";
    return r;
}

// One of the pet's own names or addresses. A literal address is always accepted: DNS rebinding needs a name.
bool own_host(const Device& d, const std::string& host) {
    const std::string h = bare_host(host);
    if (host.empty() || h.empty() || is_literal_host(h)) return true;
    return h == bare_host(d.device_name) || h == bare_host(d.base_url);
}

bool has_key(const Device& d, const ApiRequest& q) {
    if (q.local) return true;
    if (d.settings.key.empty()) return false;
    const std::string want = "bearer " + d.settings.key;
    return q.auth.size() == want.size() && ascii_lower(q.auth.substr(0, 7)) == "bearer " &&
           q.auth.substr(7) == d.settings.key;
}

Response needs_key(const Device& d) {
    return error_reply(d, 401, d.settings.key.empty()
                                   ? "this needs the key, and the pet has none yet: pair first with POST /v1/pair"
                                   : "this needs the key: -H \"Authorization: Bearer <key>\"");
}

// A new key from the platform's random source (handle_pair refuses to pair without one).
std::string new_key(Device& d) {
    static const char ALPHABET[] = "abcdefghijkmnpqrstuvwxyz23456789";
    std::string key = "nb_";
    for (size_t i = 0; i < KEY_CHARS; i++) key += ALPHABET[d.random() % (sizeof ALPHABET - 1)];
    return key;
}

void save(Device& d) {
    if (d.save) d.save(d.settings);
}

bool parse_post(const Device& d, const ApiRequest& q, Fields& f, Response& error) {
    if (q.method != "POST") {
        error = error_reply(d, 405, "use POST; the routes are listed at GET /api");
        return false;
    }
    if (q.body.size() > MAX_BODY) {
        error = error_reply(d, 413, "body over " + std::to_string(MAX_BODY) + " bytes");
        return false;
    }
    if (!parse_json(q.body.empty() ? "{}" : q.body, f)) {
        error = error_reply(d, 400, "body is not a JSON object");
        return false;
    }
    return true;
}

// Each warning once: a tell's own fields and its single step can find the same fault.
void write_warnings(JsonWriter& w, const std::vector<std::string>& warnings) {
    w.key("warnings").begin_array();
    for (size_t i = 0; i < warnings.size(); i++)
        if (std::find(warnings.begin(), warnings.begin() + std::ptrdiff_t(i), warnings[i]) ==
            warnings.begin() + std::ptrdiff_t(i))
            w.str(warnings[i]);
    w.end_array();
}

// "HH:MM" for a minute of the day.
std::string clock_text(int minute) {
    char text[8];
    std::snprintf(text, sizeof text, "%02d:%02d", minute / 60 % 24, minute % 60);
    return text;
}

// "HH:MM" to minutes after midnight; none if it isn't a time.
std::optional<int> parse_clock(const std::string& t) {
    int h = 0, m = 0;
    char extra = 0;
    if (std::sscanf(t.c_str(), "%d:%d%c", &h, &m, &extra) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return std::nullopt;
    return h * 60 + m;
}

// Someone is at the computer, as the reports tell (typing, pausing or just there).
bool user_there(const Device& d, uint32_t now) {
    const std::string u = d.brain.user_word(now);
    return u == "typing" || u == "typing-fast" || u == "paused" || u == "here";
}

// Nobody is at the computer: every source says away or asleep.
bool user_gone(const Device& d, uint32_t now) {
    const std::string u = d.brain.user_word(now);
    return u == "away" || u == "asleep";
}

// "23:00-07:00", or "off".
std::string night_text(const ScreenSettings& s) {
    return s.night_from < 0 || s.night_to < 0 ? "off" : clock_text(s.night_from) + "-" + clock_text(s.night_to);
}

// "good", "fair" or "weak", from the signal in dBm.
const char* wifi_word(int dbm) { return dbm >= -60 ? "good" : dbm >= -72 ? "fair" : "weak"; }

const char* direction_word(int row) {
    const std::vector<std::string>& all = directions();
    return row >= 0 && row < int(all.size()) ? all[size_t(row)].c_str() : "front";
}

// What the pet shows now: its name, what it does and where, why, and the words and line on screen.
void write_now(JsonWriter& w, Device& d, Look look, uint32_t now) {
    const Egg* pet = d.shelf.pet_out();
    const std::string doing = d.brain.doing(look);
    w.key("now").begin_object().field("pet", pet ? d.shelf.id(d.shelf.current()) : "").field("doing", doing);
    const Pixel home = d.pet.stage().home();
    const bool at_home = std::hypot(d.pet.x() - home.x, d.pet.y() - home.y) <= AT_PX;
    const bool desk_look = look == Look::Type || look == Look::TypeFast || look == Look::Watch || look == Look::Eat;
    w.key("at");
    if (at_home) w.str(desk_look && d.pet.stage().has_scene() ? "desk" : "home");
    else write_point(w, d.pet.stage().to_scene({d.pet.x(), d.pet.y()}));
    w.field("facing", direction_word(d.pet.drawn_row())).field("why", d.brain.why());
    const std::string say = d.brain.saying(now), caption = d.brain.captioning(now), line = d.brain.first_line(now);
    if (!say.empty()) w.field("say", say);
    if (!caption.empty()) w.field("caption", caption);
    if (!line.empty()) w.field("line", line);
    w.end_object();
}

// What the pet that is out cannot play from this tell: an animation it lacks, a throw, a place.
void pet_warnings(Device& d, const Fields& f, std::vector<std::string>& out) {
    const Egg* pet = d.shelf.pet_out();
    if (!pet) return;
    const std::string name = d.shelf.id(d.shelf.current());
    Note n;
    std::vector<std::string> ignore;
    parse_tell(f, n, ignore);
    if (!n.body.set()) return;
    for (const Step& s : n.body.value) {
        if (s.act.kind == Act::Kind::Anim && !s.act.anim.empty() && !pet->find(s.act.anim))
            out.push_back(name + " has no \"" + s.act.anim + "\"; it will stand. Its animations are at /v1/pets/" +
                          name);
        if (s.act.kind == Act::Kind::Throw && !d.pet.can_throw())
            out.push_back(name + " cannot throw; that step was skipped");
        for (const Spot* spot : {&s.act.spot, &s.act.aim, &s.act.face})
            if (spot->kind == Spot::Kind::Target && !d.pet.stage().target())
                out.push_back(name + " has no \"target\"; it stays where it is");
    }
}

std::string pet_names(const Shelf& s);

// `which`: a name, "next" or "previous". Rescans the pets once for a name it doesn't know. Returns what went wrong,
// or nothing.
std::optional<std::string> switch_pet(Device& d, const std::string& which, uint32_t now) {
    size_t i;
    if (!d.shelf.find(which, i)) {
        if (d.shelf.rescan) d.shelf.rescan();
        if (!d.shelf.find(which, i)) return "no pet called \"" + which + "\"; the pets are " + pet_names(d.shelf);
    }
    if (!d.shelf.bring_out(i, d.pet, d.screen, now))
        return d.shelf.id(i) + " can't be loaded; " + d.shelf.id(d.shelf.current()) + " stays out";
    if (d.shelf.coming().empty()) {
        d.brain.end_device(SWITCH_NOTE);
    } else {                                           // while its egg is read, the pet out says who is coming
        Note n;
        n.id = SWITCH_NOTE;
        n.state = State::On;
        const std::string& name = d.shelf.egg(i).name;
        n.caption = {Part<std::string>::Kind::Set, utf8_cut((name.empty() ? d.shelf.id(i) : name) + " is coming",
                                                            Note::MAX_CAPTION)};
        n.for_ms = SWITCH_NOTE_MS;
        d.brain.tell_device(n, now);
    }
    return std::nullopt;
}

std::string pet_names(const Shelf& s) {
    std::vector<std::string> ids;
    for (size_t i = 0; i < s.size(); i++) ids.push_back(s.id(i));
    return joined(ids);
}

// ---- Routes --------------------------------------------------------------------------------------------------

Response handle_tell(Device& d, const ApiRequest& q) {
    Fields f;
    Response error;
    if (!parse_post(d, q, f, error)) return error;
    if (!q.local && ascii_lower(f.str("from")) == DEVICE_SENDER)
        return error_reply(d, 403, "\"device\" is the pet's own name; choose another \"from\"");
    Brain::Told t = d.brain.tell(f, q.now);
    if (!t.used) {
        JsonWriter w;
        w.begin_object().field("ok", false).field("error", "nothing in this request could be used")
            .field("help", d.base_url + "/api");
        write_warnings(w, t.warnings);
        Response r = json_reply(w.end_object().text());
        r.status = 400;
        return r;
    }
    wake(d, q.now);
    pet_warnings(d, f, t.warnings);
    const Look look = d.brain.decide(q.now);
    JsonWriter w;
    w.begin_object().field("ok", true).key("note").begin_object().field("from", t.from);
    if (!t.id.empty()) w.field("id", t.id);
    if (!t.state.empty()) w.field("state", t.state);
    w.key("showing").begin_array();
    for (auto& part : t.showing) w.str(part);
    w.end_array();
    if (!t.why.empty()) w.field("why", t.why);
    w.field("ends_in", static_cast<long long>(t.ends_in)).end_object();
    if (t.ended >= 0) w.field("ended", t.ended);
    write_now(w, d, look, q.now);
    write_warnings(w, t.warnings);
    return json_reply(w.end_object().text());
}

Response handle_user(Device& d, const ApiRequest& q) {
    Fields f;
    Response error;
    if (!parse_post(d, q, f, error)) return error;
    if (!has_key(d, q)) return needs_key(d);
    const std::vector<std::string> warnings = d.brain.user(f, q.now);
    if (user_there(d, q.now)) wake(d, q.now);
    const Look look = d.brain.decide(q.now);
    JsonWriter w;
    w.begin_object().field("ok", true).field("user", d.brain.user_word(q.now));
    write_now(w, d, look, q.now);
    write_warnings(w, warnings);
    return json_reply(w.end_object().text());
}

void write_device(JsonWriter& w, const Device& d) {
    const Display& disp = d.pet.display();
    static const char* const SIZES[] = {"small", "medium", "large"};
    w.key("device").begin_object()
        .field("name", d.device_name)
        .field("id", d.id)
        .field("api", 1)
        .field("firmware", d.firmware)
        .field("tell", d.settings.locked ? "key" : "open")
        .field("paired", !d.settings.key.empty())
        .key("screen").begin_object()
        .field("width", disp.w)
        .field("height", disp.h)
        .field("shape", disp.round() ? "round" : "rectangle")
        .field("lines", 1)
        .field("gauges", static_cast<long long>(Brain::MAX_GAUGES))
        .field("state", screen_state_key(d.screen_state))
        .field("backlight", backlight_now(d))
        .field("bright", d.settings.screen.bright)
        .field("dim", d.settings.screen.dim)
        .field("dim_after", d.settings.screen.dim_after)
        .field("off_after", d.settings.screen.off_after)
        .field("night", night_text(d.settings.screen))
        .field("care", d.settings.screen.care)
        .field("oled", disp.oled)
        .end_object()
        .field("text", SIZES[std::clamp(d.settings.text, 1, 3) - 1])
        .key("inputs").begin_array().str("button").str("motion").end_array();
    if (!d.relay_state.empty())
        w.key("relay").begin_object()
            .field("state", d.relay_state)
            .field("set", !d.settings.relay.mailbox.empty())
            .field("on", d.settings.relay.on)
            .end_object();
    if (d.health.temperature_c) w.key("temperature_c").num(double(*d.health.temperature_c), 0);
    if (d.health.wifi_dbm) w.field("wifi", wifi_word(*d.health.wifi_dbm)).field("wifi_dbm", *d.health.wifi_dbm);
    w
        .field("help", d.base_url + "/api")
        .field("agents", d.base_url + "/agents")
        .end_object();
}

Response serve_now(Device& d, const ApiRequest& q) {
    const Look look = d.brain.decide(q.now);
    JsonWriter w;
    w.begin_object().field("ok", true);
    write_now(w, d, look, q.now);
    w.field("user", d.brain.user_word(q.now));
    if (const std::optional<int> m = d.brain.minute_of_day(q.now)) w.field("time", clock_text(*m));
    w.field("uptime_s", static_cast<long long>(q.now / 1000));
    w.field("quiet", d.brain.quiet(q.now));
    w.key("notes").begin_array(JsonWriter::Items::OnePerLine);
    for (const Brain::NoteView& n : d.brain.notes(q.now)) {
        w.begin_object().field("from", n.from);
        if (!n.id.empty()) w.field("id", n.id);
        if (!n.state.empty()) w.field("state", n.state);
        if (!n.line.empty()) w.field("line", n.line);
        if (!n.color.empty()) w.field("color", n.color);
        w.key("showing").begin_array();
        for (auto& part : n.showing) w.str(part);
        w.end_array();
        if (!n.why.empty()) w.field("why", n.why);
        w.field("ends_in", static_cast<long long>(n.ends_in)).end_object();
    }
    w.end_array();
    write_device(w, d);
    return json_reply(w.end_object().text());
}

// A relay key: 16 to 128 characters of base64url (the relay's own keys carry a short prefix, also in that set).
bool relay_key(const std::string& k) {
    return k.size() >= 16 && k.size() <= 128 && std::all_of(k.begin(), k.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}

// "relay": the keys for webhooks through the relay, "on", or "forget".
void set_relay(Device& d, const Fields& f, std::vector<std::string>& warnings) {
    RelaySettings& r = d.settings.relay;
    if (f.has("relay")) {
        if (ascii_lower(f.str("relay")) == "forget") r = RelaySettings{};
        else warnings.push_back("\"relay\" is an object with mailbox, secret and on, or \"forget\"");
    } else {
        const Fields rf = f.under("relay");
        for (auto [k, into] : {std::pair{"mailbox", &r.mailbox}, {"secret", &r.secret}}) {
            if (!rf.has(k)) continue;
            if (relay_key(rf.str(k))) *into = rf.str(k);
            else warnings.push_back(std::string("\"relay.") + k + "\" is 16 to 128 letters, digits, - and _");
        }
        if (rf.has("on")) {
            const std::string on = ascii_lower(rf.str("on"));
            if (on == "true" || on == "false") r.on = on == "true";
            else warnings.push_back("\"relay.on\" is true or false");
        }
        for (const auto& [k, v] : rf.values)
            if (k != "mailbox" && k != "secret" && k != "on")
                warnings.push_back("unknown key \"relay." + k + "\"; it takes mailbox, secret and on");
    }
    d.relay_changed = true;
    save(d);
}

Response handle_device(Device& d, const ApiRequest& q) {
    Fields f;
    Response error;
    if (!parse_post(d, q, f, error)) return error;
    if (!has_key(d, q)) return needs_key(d);
    wake(d, q.now);
    std::vector<std::string> warnings;
    bool any = false;
    if (f.has("pet")) {
        any = true;
        const std::string which = ascii_lower(f.str("pet"));
        const size_t before = d.shelf.current();
        if (const std::optional<std::string> problem = switch_pet(d, which, q.now))
            warnings.push_back(*problem);
        else if ((which == "next" || which == "previous") && d.shelf.current() == before && d.shelf.coming().empty())
            warnings.push_back(d.shelf.id(before) + " is the only pet on this device");
    }
    if (f.has("time")) {
        any = true;
        const std::string t = ascii_lower(f.str("time"));
        int h = 0, m = 0;
        char extra = 0;
        if (t == "auto") d.brain.force_time(std::nullopt, q.now);
        else if (std::sscanf(t.c_str(), "%d:%d%c", &h, &m, &extra) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60)
            d.brain.force_time(h * 60 + m, q.now);
        else warnings.push_back("\"time\" is a local time such as \"22:30\", or \"auto\"");
    }
    if (f.has("text")) {
        any = true;
        const std::string t = ascii_lower(f.str("text"));
        const int size = t == "small" ? 1 : t == "medium" ? 2 : t == "large" ? 3 : 0;
        if (size) {
            d.settings.text = size;
            d.brain.text_size(size);
            save(d);
        } else {
            warnings.push_back("\"text\" is \"small\", \"medium\" or \"large\"");
        }
    }
    if (f.has_object("screen")) {
        any = true;
        const Fields sf = f.under("screen");
        ScreenSettings& sc = d.settings.screen;
        auto percent = [&](const char* k, int& into) {
            if (!sf.has(k)) return;
            const double v = sf.num(k, NAN);
            if (v >= 1 && v <= 100) into = int(v);
            else warnings.push_back(std::string("\"screen.") + k + "\" is a percent, 1 to 100");
        };
        auto minutes = [&](const char* k, int& into) {
            if (!sf.has(k)) return;
            const double v = sf.num(k, NAN);
            if (v >= 0 && v <= 24 * 60) into = int(v);
            else warnings.push_back(std::string("\"screen.") + k + "\" is minutes, 0 (never) to 1440");
        };
        percent("bright", sc.bright);
        percent("dim", sc.dim);
        minutes("dim_after", sc.dim_after);
        minutes("off_after", sc.off_after);
        if (sf.has("night")) {
            const std::string n = ascii_lower(sf.str("night"));
            const size_t dash = n.find('-');
            const std::optional<int> from = dash == std::string::npos ? std::nullopt : parse_clock(n.substr(0, dash));
            const std::optional<int> to = dash == std::string::npos ? std::nullopt : parse_clock(n.substr(dash + 1));
            if (n == "off") sc.night_from = sc.night_to = -1;
            else if (from && to && *from != *to) { sc.night_from = *from; sc.night_to = *to; }
            else warnings.push_back("\"screen.night\" is hours such as \"23:00-07:00\", or \"off\"");
        }
        if (sf.has("care")) {
            const std::string c = ascii_lower(sf.str("care"));
            if (c == "true" || c == "false") sc.care = c == "true";
            else warnings.push_back("\"screen.care\" is true or false");
        }
        for (const auto& [k, v] : sf.values)
            if (k != "bright" && k != "dim" && k != "dim_after" && k != "off_after" && k != "night" && k != "care")
                warnings.push_back("unknown key \"screen." + k +
                                   "\"; it takes bright, dim, dim_after, off_after, night and care");
        save(d);
    }
    if (f.has("restart")) {
        any = true;
        if (!f.flag("restart")) warnings.push_back("\"restart\" takes only true");
        else d.restart_at = deadline(q.now, RESTART_DELAY_MS);
    }
    if (f.has("lock") || f.has("key")) {
        any = true;
        if (!q.local) {
            warnings.push_back("\"lock\" and a new \"key\" are set on the device itself, not over the network");
        } else {
            if (f.has("lock")) d.settings.locked = f.flag("lock");
            if (ascii_lower(f.str("key")) == "new" && d.random) d.settings.key = new_key(d);
            save(d);
        }
    }
    if (f.has("relay") || f.has_object("relay")) {
        any = true;
        set_relay(d, f, warnings);
    }
    if (f.has("wifi")) {
        any = true;
        if (ascii_lower(f.str("wifi")) != "forget") warnings.push_back("\"wifi\" takes only \"forget\"");
        else if (!d.setup) warnings.push_back("this device has no Wi-Fi setup");
        else d.setup->start_over(q.now, FORGET_DELAY_MS);
    }
    for (const auto& [k, v] : f.values)
        if (k.find('.') == std::string::npos && k != "pet" && k != "time" && k != "text" && k != "lock" && k != "key" &&
            k != "wifi" && k != "restart" && k != "relay")
            warnings.push_back("unknown key \"" + k +
                               "\"; this route takes pet, time, text, screen, wifi, restart and relay");
    if (!any)
        return error_reply(d, 400, "needs \"pet\", \"time\", \"text\", \"screen\", \"wifi\", \"restart\" or \"relay\"");
    const Look look = d.brain.decide(q.now);
    JsonWriter w;
    w.begin_object().field("ok", true);
    write_now(w, d, look, q.now);
    if (const std::optional<int> m = d.brain.minute_of_day(q.now)) w.field("time", clock_text(*m));
    if (q.local && !d.settings.key.empty() && ascii_lower(f.str("key")) == "new") w.field("key", d.settings.key);
    write_warnings(w, warnings);
    return json_reply(w.end_object().text());
}

// Pairing: the first request asks the user on the pet; after a press of its button, the same request from the same
// address gets the key.
Response handle_pair(Device& d, const ApiRequest& q) {
    Fields f;
    Response error;
    if (!parse_post(d, q, f, error)) return error;
    if (!d.random) return error_reply(d, 503, "this device has no random source to make a key with");
    wake(d, q.now);
    const std::string name = utf8_cut(f.str("name", "a computer"), 32);
    Device::Pairing& p = d.pairing;
    const bool pending = live(p.until, q.now);
    const bool same = p.peer.empty() || q.peer.empty() || p.peer == q.peer;
    if (pending && p.confirmed && same) {
        if (d.settings.key.empty()) {
            d.settings.key = new_key(d);
            save(d);
        }
        p = Device::Pairing{};
        return json_reply(JsonWriter().begin_object().field("ok", true).field("pairing", "done")
                              .field("key", d.settings.key).field("id", d.id).end_object().text());
    }
    if (pending && !same) return error_reply(d, 409, "another pairing is waiting for the button; try in a minute");
    if (!pending && d.pet_change.kind != Device::PetChange::Kind::None)
        return error_reply(d, 409, "a pet is being added or taken off; try in a minute");
    if (!pending) {
        p = Device::Pairing{name, q.peer, deadline(q.now, PAIR_MS), false};
        Note ask;
        ask.id = "pair";
        ask.state = State::On;
        ask.caption = {Part<std::string>::Kind::Set,
                       utf8_cut("Pair with " + name + "? Press the button", Note::MAX_CAPTION)};
        ask.sign = {Part<Bubble>::Kind::Set, Bubble::Question};
        ask.for_ms = PAIR_MS;
        d.brain.tell_device(ask, q.now);
    }
    const long long ends_in = (p.until - q.now + 999) / 1000;
    return json_reply(
        JsonWriter().begin_object().field("ok", true).field("pairing", "waiting").field("ends_in", ends_in).end_object()
            .text());
}

// The device's own input: its button ("press"), motion sensor ("gesture") and USB port ("computer").
Response handle_input(Device& d, const ApiRequest& q) {
    Fields f;
    Response error;
    if (!parse_post(d, q, f, error)) return error;
    if (!q.local) return error_reply(d, 403, "the button and sensors are the device's own");
    auto ok = [](const std::string& what) {
        return json_reply(JsonWriter().begin_object().field("ok", true).field("input", what).end_object().text());
    };
    const std::string press = f.str("press"), gesture = f.str("gesture");
    if (f.has("computer")) {
        const bool asleep = f.str("computer") == "asleep";
        if (!asleep) wake(d, q.now);
        return ok(d.brain.computer(asleep, q.now));
    }
    if (f.has("boot") && f.str("boot") == "down") wake(d, q.now);
    if (!press.empty() || !gesture.empty()) {
        const bool was_off = d.screen_state == ScreenState::Off;
        wake(d, q.now);
        if (was_off && (press == "single" || gesture == "tap")) return ok("woke the screen");   // not a pat in the dark
    }
    if (f.has("boot")) {
        const std::string state = f.str("boot");
        if (state != "down" && state != "up") return error_reply(d, 400, "\"boot\" is \"down\" or \"up\"");
        if (d.setup) d.setup->boot(state == "down", q.now);
        return ok("boot " + state);
    }
    if (press == "single" || gesture == "tap") {
        if (d.setup && d.setup->tap(q.now)) return ok(d.setup->active() ? "setup opened" : "setup closed");
        if (live(d.pairing.until, q.now) && !d.pairing.confirmed) {
            d.pairing.confirmed = true;
            d.brain.end_device();
            d.brain.show_reaction(Look::Nod, q.now);
            return ok("paired");
        }
        Device::PetChange& c = d.pet_change;
        if (c.kind != Device::PetChange::Kind::None && !c.go && live(c.until, q.now)) {
            c.go = true;
            d.brain.end_device();
            d.brain.show_reaction(Look::Nod, q.now);
            return ok(c.kind == Device::PetChange::Kind::Add ? "adding " + c.id : "taking off " + c.id);
        }
        return ok(d.brain.tap(q.now));
    }
    if (gesture == "shake") return ok(d.brain.shake(q.now));
    if (press == "long") return ok(d.brain.quiet_press(q.now));
    if (press == "double") {
        const std::string did = d.brain.dismiss(q.now);
        if (!did.empty()) return ok(did);
        if (const std::optional<std::string> problem = switch_pet(d, "next", q.now))
            return error_reply(d, 400, "nothing to dismiss; " + *problem);
        return ok("pet " + (d.shelf.coming().empty() ? d.shelf.id(d.shelf.current()) : d.shelf.coming()));
    }
    return error_reply(d, 400, "needs press (single, double, long), gesture (tap, shake), boot or computer");
}

Response serve_help(Device& d, const ApiRequest&) {
    Response r;
    r.type = "text/plain; charset=utf-8";
    const Egg* pet = d.shelf.pet_out();
    std::string help = HELP;
    std::string host = d.base_url;
    if (host.rfind("http://", 0) == 0) host = host.substr(7);
    replace_all(help, "{HOST}", host);
    r.body = "NestBox " + d.device_name + " at " + d.base_url + (pet ? " (" + pet->name + " is out)" : "") + "\n\n" +
             help;
    return r;
}

Response serve_page(Device& d, const ApiRequest&) {
    Response r;
    r.type = "text/html; charset=utf-8";
    const Egg* pet = d.shelf.pet_out();
    const std::string name = d.setup ? d.setup->name() : pet ? pet->name : d.device_name;
    r.body = device_page(name, text_svg(name, 3));
    return r;
}

Response serve_guide(Device& d, const ApiRequest&) {
    Response r;
    r.type = "text/markdown; charset=utf-8";
    r.body = guide(d.base_url, d.shelf, d.pet.display(), d.settings.locked);
    return r;
}

Response serve_pets(Device& d, const ApiRequest& q) {
    if (d.shelf.rescan) d.shelf.rescan();
    pet_change_to_store(d, q.now);                        // drops one that waited too long
    const Device::PetChange& c = d.pet_change;
    return json_reply(pets_json(d.shelf, [&](JsonWriter& w) {
        if (c.kind != Device::PetChange::Kind::None) {
            w.key("change").begin_object().field("pet", c.id)
                .field("doing", c.kind == Device::PetChange::Kind::Add ? "adding" : "taking off")
                .field("waiting", c.go ? "saving" : "the button");
            if (!c.go) w.field("ends_in", static_cast<long long>((c.until - q.now + 999) / 1000));
            w.end_object();
        }
        if (!d.pet_change_done.empty()) w.field("last_change", d.pet_change_done);
        if (d.health.flash_total)
            w.key("storage").begin_object().field("total", static_cast<long long>(d.health.flash_total))
                .field("used", static_cast<long long>(d.health.flash_used)).end_object();
    }));
}

// A pet's id: what its file is called. Lower-case letters, digits and hyphens, starting with a letter or digit.
bool valid_pet_id(const std::string& id) {
    if (id.empty() || id.size() > PET_ID_MAX || id == "next" || id == "previous" || id == "prev" || id[0] == '-')
        return false;
    return std::all_of(id.begin(), id.end(), [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
    });
}

// Starts a change: with the key it goes ahead at once, else the pet asks for its button.
Response start_pet_change(Device& d, const ApiRequest& q, Device::PetChange&& c, const std::string& question) {
    wake(d, q.now);                                    // the question is on the screen
    c.go = has_key(d, q);
    if (!c.go) {
        c.until = deadline(q.now, PAIR_MS);
        Note ask;
        ask.id = "pet";
        ask.state = State::On;
        ask.caption = {Part<std::string>::Kind::Set, utf8_cut(question + "? Press the button", Note::MAX_CAPTION)};
        ask.sign = {Part<Bubble>::Kind::Set, Bubble::Question};
        ask.for_ms = PAIR_MS;
        d.brain.tell_device(ask, q.now);
    }
    const bool add = c.kind == Device::PetChange::Kind::Add;
    const std::string id = c.id;
    d.pet_change = std::move(c);
    JsonWriter w;
    w.begin_object().field("ok", true).field("pet", id);
    w.field(add ? "adding" : "taking_off", d.pet_change.go ? "saving" : "waiting for the button");
    if (!d.pet_change.go) w.field("ends_in", static_cast<long long>(PAIR_MS / 1000));
    Response r = json_reply(w.end_object().text());
    r.status = 202;
    return r;
}

// PUT /v1/pets/NAME: the body is the pet's egg. It is checked here, then kept until the platform has written it.
Response handle_pet_put(Device& d, const ApiRequest& q, const std::string& id) {
    if (!valid_pet_id(id))
        return error_reply(d, 400, "a pet's name here is its file name: lower-case letters, digits and hyphens");
    pet_change_to_store(d, q.now);
    if (d.pet_change.kind != Device::PetChange::Kind::None || live(d.pairing.until, q.now))
        return error_reply(d, 409, "another change is waiting for the pet's button or being saved; try in a minute");
    if (q.body.empty()) return error_reply(d, 400, "send the pet's .egg file as the body");
    if (q.body.size() > MAX_EGG) return error_reply(d, 413, "an egg over " + std::to_string(MAX_EGG / 1024) + " KB");
    Device::PetChange c;
    c.kind = Device::PetChange::Kind::Add;
    c.id = id;
    std::string error;
    if (!load_egg(std::vector<uint8_t>(q.body.begin(), q.body.end()), c.egg, error))
        return error_reply(d, 400, "not a pet's egg: " + error);
    c.name = c.egg.name.empty() ? id : c.egg.name;
    const std::string question = (d.shelf.has(id) ? "Replace " : "Add ") + c.name;
    return start_pet_change(d, q, std::move(c), question);
}

// DELETE /v1/pets/NAME.
Response handle_pet_delete(Device& d, const ApiRequest& q, const std::string& id) {
    const std::optional<size_t> i = d.shelf.index_of(id);
    if (!i) return error_reply(d, 404, "no pet called \"" + id + "\"; the pets are " + pet_names(d.shelf));
    if (d.shelf.size() < 2) return error_reply(d, 409, "the last pet stays");
    pet_change_to_store(d, q.now);
    if (d.pet_change.kind != Device::PetChange::Kind::None || live(d.pairing.until, q.now))
        return error_reply(d, 409, "another change is waiting for the pet's button or being saved; try in a minute");
    Device::PetChange c;
    c.kind = Device::PetChange::Kind::Remove;
    c.id = id;
    c.name = d.shelf.egg(*i).name.empty() ? id : d.shelf.egg(*i).name;
    return start_pet_change(d, q, std::move(c), "Take " + c.name + " off");
}

Response serve_pet(Device& d, const ApiRequest& q) {
    std::string name = q.path.substr(std::string(PETS_PATH).size() + 1);
    const std::string picture = "/picture";
    const bool wants_picture = name.size() > picture.size() &&
                               name.compare(name.size() - picture.size(), picture.size(), picture) == 0;
    if (wants_picture) name.resize(name.size() - picture.size());
    size_t i;
    if (!d.shelf.find(name, i) || name == "next" || name == "previous" || name == "prev") {
        if (d.shelf.rescan) d.shelf.rescan();
        if (!d.shelf.find(name, i) || name == "next" || name == "previous" || name == "prev")
            return error_reply(d, 404, "no pet called \"" + name + "\"; the pets are " + pet_names(d.shelf));
    }
    if (wants_picture) {
        Response r;
        r.type = "image/png";
        r.body = pet_png(&d.shelf.egg(i));
        if (r.body.empty()) return error_reply(d, 404, "\"" + name + "\" has no picture");
        return r;
    }
    return json_reply(pet_json(d.shelf, i, d.pet.display()));
}

Response serve_words(Device&, const ApiRequest&) {
    JsonWriter w;
    w.begin_object().field("ok", true).field("api", 1);
    const std::pair<const char*, const char*> keys[] = {
        {"from", "who is speaking"}, {"id", "which of the sender's things; \"a/b\" is a part of \"a\""},
        {"state", "what is going on"}, {"do", "what the body does: a behaviour, an animation, or a list of steps"},
        {"at", "where: a place or a point [x, y] in percent of the scene"}, {"aim", "what a throw aims at"},
        {"face", "which way it faces: a direction, a place or a point"}, {"times", "how many times"},
        {"speed", "how fast: \"slow\", \"fast\" or a factor"},
        {"through", "on the way to \"at\": up to 8 points [x, y] it curves through"},
        {"moving", "on the way to \"at\": an animation it plays as it goes"},
        {"spin", "on the way to \"at\": turns a second it spins round as it goes"},
        {"say", "words in a bubble beside the pet"},
        {"caption", "words along the bottom of the screen"}, {"line", "a line in the status bar"},
        {"gauge", "a progress gauge: 0 to 100, or \"timer\""}, {"label", "text inside the gauge"},
        {"sign", "a sign over the pet's head"}, {"effect", "an effect around the pet"},
        {"color", "the colour of the note's line and gauge"}, {"for", "how long it lasts, in real time"},
        {"until", "what it waits for: \"seen\" or \"back\""}};
    w.key("tell").begin_object();
    for (auto [k, v] : keys) w.field(k, v);
    w.end_object();
    w.key("states").begin_object();
    for (State s : states()) w.field(state_key(s), state_means(s));
    w.end_object();
    w.key("behaviours").begin_object();
    for (const Behaviour& b : behaviours()) w.field(b.word, b.means);
    w.end_object();
    w.key("directions").begin_array();
    for (auto& dir : directions()) w.str(dir);
    w.end_array();
    w.key("places").begin_array().str("here").str("home").str("desk").str("target").end_array();
    w.key("signs").begin_array();
    for (Bubble b : signs()) w.str(sign_key(b));
    w.end_array();
    w.key("effects").begin_array();
    for (Effect e : effects()) w.str(effect_key(e));
    w.end_array();
    w.key("colors").begin_array();
    for (auto& [name, value] : color_names()) w.str(name);
    w.end_array();
    w.key("until").begin_array().str("seen").str("back").end_array();
    w.key("user").begin_object()
        .field("from", "which source")
        .field("keys", "key presses since the last report")
        .field("idle", "seconds since any input")
        .field("state", "awake, locked or asleep")
        .field("time", "local time, \"14:32\"")
        .field("next", "the next report comes within this many seconds")
        .end_object();
    w.key("device").begin_object()
        .field("pet", "bring out another pet: its name, \"next\" or \"previous\"")
        .field("time", "force the time of day for an hour, or \"auto\"")
        .field("text", "small, medium or large")
        .field("screen", "bright and dim (percents), dim_after and off_after (minutes, 0 never), night (\"23:00-07:00\" or \"off\"), care (true or false)")
        .field("restart", "true: restart the device")
        .field("wifi", "\"forget\": forget the Wi-Fi and ask for a new one")
        .field("relay", "mailbox and secret (the relay's keys) and on (true or false), or \"forget\"")
        .end_object();
    w.key("limits").begin_object()
        .field("request", static_cast<long long>(MAX_BODY))
        .field("from", static_cast<long long>(Note::MAX_FROM))
        .field("id", static_cast<long long>(Note::MAX_ID))
        .field("say", static_cast<long long>(Note::MAX_SAY))
        .field("caption", static_cast<long long>(Note::MAX_CAPTION))
        .field("line", static_cast<long long>(Note::MAX_LINE))
        .field("label", static_cast<long long>(Note::MAX_LABEL))
        .field("steps", static_cast<long long>(StepChain::MAX_STEPS))
        .field("times", Step::MAX_COUNT)
        .field("for", "24h")
        .field("notes", static_cast<long long>(Brain::MAX_NOTES))
        .field("notes_per_sender", static_cast<long long>(Brain::MAX_PER_SENDER))
        .field("requests_per_second", MAX_REQUESTS_PER_S)
        .end_object();
    return json_reply(w.end_object().text());
}

// The body's details for tests and recordings: where it stands in screen pixels, what it plays, which phase.
Response serve_debug(Device& d, const ApiRequest& q) {
    const Look look = d.brain.decide(q.now);
    const Pixel home = d.pet.stage().home();
    JsonWriter w;
    w.begin_object()
        .field("ok", true)
        .field("look", look_key(look))
        .field("showing", look_key(d.pet.look()))
        .field("going_to", look_key(d.pet.wanted()))
        .field("phase", d.pet.phase_label())
        .field("playing", d.pet.playing())
        .field("daypart", daypart_key(d.pet.daypart()))
        .field("display", d.pet.display().id)
        .field("why", d.brain.why())
        .field("steps_left", static_cast<long long>(d.brain.steps_left()))
        .field("held", d.brain.held())
        .key("x").num(double(d.pet.x()), 1)
        .key("y").num(double(d.pet.y()), 1)
        .field("row", d.pet.drawn_row())
        .field("home_x", home.x)
        .field("home_y", home.y)
        .key("kps").num(double(d.brain.kps(q.now)), 1)
        .key("uptime_ms").num(static_cast<long long>(q.now))
        .field("wifi_line", d.setup ? d.setup->caption(q.now) : "");
    return json_reply(w.end_object().text());
}

struct Route {
    const char* method;          // nullptr: any method (the handler checks)
    const char* path;
    Response (*handle)(Device&, const ApiRequest&);
};

const Route ROUTES[] = {
    {"GET", "/", serve_page},
    {"GET", "/welcome", serve_page},             // where setup hands over to the home network
    {"GET", "/api", serve_help},
    {"GET", "/agents", serve_guide},
    {"GET", "/llms.txt", serve_guide},
    {"GET", API_PATH, serve_now},
    {"POST", API_PATH, handle_device},
    {"GET", PETS_PATH, serve_pets},
    {"GET", WORDS_PATH, serve_words},
    {"GET", DEBUG_PATH, serve_debug},
    {nullptr, TELL_PATH, handle_tell},
    {nullptr, USER_PATH, handle_user},
    {nullptr, PAIR_PATH, handle_pair},
    {nullptr, INPUT_PATH, handle_input},
};

}  // namespace

size_t body_limit(const std::string& method, const std::string& path) {
    return method == "PUT" && path.rfind(std::string(PETS_PATH) + "/", 0) == 0 ? MAX_EGG : MAX_BODY;
}

const Device::PetChange* pet_change_to_store(Device& d, uint32_t now) {
    Device::PetChange& c = d.pet_change;
    if (c.kind == Device::PetChange::Kind::None) return nullptr;
    if (c.go) return &c;
    if (!live(c.until, now)) {
        d.pet_change_done = c.id + ": the button wasn't pressed";
        c = Device::PetChange{};
        d.brain.end_device();
    }
    return nullptr;
}

std::string pet_to_load(const Device& d) { return d.shelf.coming(); }

void finish_pet_load(Device& d, const std::string& id, Egg* egg, uint32_t now) {
    if (id != d.shelf.coming()) return;                // another was asked for meanwhile; its turn comes next
    d.brain.end_device(SWITCH_NOTE);
    if (egg && d.shelf.arrive(id, std::move(*egg), d.pet, d.screen, now)) return;
    d.shelf.give_up(id);
    d.pet_change_done = id + ": it can't be loaded; " + d.shelf.id(d.shelf.current()) + " stays out";
}

void finish_pet_change(Device& d, const std::string& error, uint32_t now) {
    Device::PetChange& c = d.pet_change;
    if (c.kind == Device::PetChange::Kind::None) return;
    if (!error.empty()) {
        d.pet_change_done = c.id + ": " + error;
    } else if (c.kind == Device::PetChange::Kind::Add) {
        d.shelf.put(std::move(c.egg), c.id, d.pet, d.screen, now);
        d.brain.forget_pet();
        d.pet_change_done = "added " + c.id;
    } else {
        const std::optional<size_t> i = d.shelf.index_of(c.id);
        d.brain.forget_pet();
        d.pet_change_done = i && d.shelf.remove(*i, d.pet, d.screen, now) ? "took off " + c.id : c.id + ": it stays";
    }
    c = Device::PetChange{};
}

Response handle_request(Device& d, const std::string& method, const std::string& path_q, const std::string& body,
                        uint32_t now, const std::string& host, const std::string& origin, bool local,
                        const std::string& auth, const std::string& peer, bool setup_network) {
    Response r;
    if (d.setup && d.setup->handle(method, path_q, body, host, now, d.shelf.pet_out(), r, setup_network)) return r;
    if (!own_host(d, host)) return error_reply(d, 403, "this pet answers only to its own name or address");
    const bool write = method != "GET" && method != "HEAD";
    if (write && !origin.empty() && bare_host(origin) != bare_host(host))
        return error_reply(d, 403, "not from another site's page");
    if (!local) {
        const uint32_t second = now / 1000;
        if (second != d.rate_second) {
            d.rate_second = second;
            d.rate_count = 0;
        }
        if (++d.rate_count > MAX_REQUESTS_PER_S) return error_reply(d, 429, "too many requests; slow down");
    }
    std::string path = path_only(path_q);
    if (path.size() > 1 && path.back() == '/') path.pop_back();
    const ApiRequest q{method, path, body, now, local, auth, peer};
    const bool api = path == API_PATH || path.rfind(std::string(API_PATH) + "/", 0) == 0;
    if (api && d.settings.locked && path != PAIR_PATH && !has_key(d, q)) return needs_key(d);
    if (path.rfind(std::string(PETS_PATH) + "/", 0) == 0) {
        const std::string id = path.substr(std::string(PETS_PATH).size() + 1);
        if (method == "GET") return serve_pet(d, q);
        if (method == "PUT") return handle_pet_put(d, q, id);
        if (method == "DELETE") return handle_pet_delete(d, q, id);
        return error_reply(d, 405, "wrong method for " + path + "; GET, PUT or DELETE");
    }
    bool known = false;
    for (const Route& route : ROUTES) {
        if (path != route.path) continue;
        known = true;
        if (!route.method || method == route.method) return route.handle(d, q);
    }
    if (known) return error_reply(d, 405, "wrong method for " + path + "; the routes are listed at GET /api");
    return error_reply(d, 404, "no such route; the routes are listed at GET /api");
}

void wake(Device& d, uint32_t now) {
    d.active_at = now;
    d.screen_state = ScreenState::Awake;
}

ScreenState update_rest(Device& d, uint32_t now) {
    d.screen_state = rest_state(d.settings.screen, now - d.active_at, user_gone(d, now), d.brain.minute_of_day(now));
    d.shift = pixel_shift(d.settings.screen, now);
    d.pet.care(d.settings.screen.care);
    return d.screen_state;
}

int backlight_now(const Device& d) {
    const Egg* pet = d.shelf.pet_out();
    return backlight_percent(d.settings.screen, d.screen_state, pet && !pet->screen_palette.empty());
}

}  // namespace nestbox
