#include "sim_device.h"
#include <fstream>
#include <random>
#include <sstream>
#include <thread>
#include "bonjour.h"
#include "files.h"
#include "host/eggs.h"
#include "host/protocol.h"
#include "image.h"
#include "keys.h"
#include "log.h"

namespace nestbox {

namespace {
constexpr uint32_t NAMED_LOOK_MS = 4000;    // how long the S key plays a reaction

// The Bonjour TXT id: sim-<this Mac>, plus the --name of any instance other than the default one, so a test's
// simulator never matches the id a companion paired with.
std::string sim_id(const std::string& name) {
    return "sim-" + short_hostname() + (name == DEFAULT_NAME ? "" : "-" + name);
}

Settings load_settings(const std::string& name, const Display& display) {
    Settings s;
    s.screen = screen_defaults(display);
    std::ifstream in(settings_file(name));
    std::stringstream text;
    text << in.rdbuf();
    Fields f;
    if (!parse_json(text.str(), f)) return s;
    s.key = f.str("key");
    s.locked = f.flag("locked");
    s.text = f.integer("text", 2);
    ScreenSettings& sc = s.screen;
    sc.bright = f.integer("bright", sc.bright);
    sc.dim = f.integer("dim", sc.dim);
    sc.dim_after = f.integer("dim_after", sc.dim_after);
    sc.off_after = f.integer("off_after", sc.off_after);
    sc.night_from = f.integer("night_from", sc.night_from);
    sc.night_to = f.integer("night_to", sc.night_to);
    if (f.has("care")) sc.care = f.flag("care");
    s.relay.mailbox = f.str("relay_mailbox");
    s.relay.secret = f.str("relay_secret");
    if (f.has("relay_on")) s.relay.on = f.flag("relay_on");
    s.ignore = ignore_from_text(f.str("ignore"));
    return s;
}

void store_settings(const std::string& name, const Settings& s) {
    std::filesystem::create_directories(settings_file(name).parent_path());
    const ScreenSettings& sc = s.screen;
    std::ofstream(settings_file(name)) << JsonWriter().begin_object().field("key", s.key).field("locked", s.locked)
                                              .field("text", s.text).field("bright", sc.bright).field("dim", sc.dim)
                                              .field("dim_after", sc.dim_after).field("off_after", sc.off_after)
                                              .field("night_from", sc.night_from).field("night_to", sc.night_to).field("care", sc.care)
                                              .field("relay_mailbox", s.relay.mailbox)
                                              .field("relay_secret", s.relay.secret).field("relay_on", s.relay.on)
                                              .field("ignore", ignore_text(s.ignore))
                                              .end_object().text() << "\n";
}

std::string saved_choice() {
    std::string id;
    std::getline(std::ifstream(chosen_pet_file()), id);
    return id;
}

// Breaks or mends the pretend router: {"router": "ok" | "password" | "gone"}. From this Mac only.
constexpr const char* ROUTER_PATH = "/sim/router";
// Plays one of the pet's own pastimes by name: {"pastime": "a nap in the sun"}. From this Mac only (recordings).
constexpr const char* PASTIME_PATH = "/sim/pastime";
// Where the pet's head was last drawn, in screen pixels: {"x": 140, "y": 96}. From this Mac only (recordings).
constexpr const char* HEAD_PATH = "/sim/head";

// Requests the phone makes while it is on the setup page or checking for a captive portal.
bool from_setup_phone(const std::string& path) {
    return path == "/" || path.rfind("/setup/", 0) == 0 || path == "/hotspot-detect.html" || path == "/generate_204";
}
}  // namespace

void FramePacer::wait() {
    next_ += std::chrono::microseconds(FRAME_US);
    const auto now = std::chrono::steady_clock::now();
    if (next_ < now) next_ = now;
    std::this_thread::sleep_until(next_);
}

SimDevice::SimDevice(const Options& o, Shelf& shelf, std::vector<std::filesystem::path> egg_dirs)
    : display_(*o.display),
      shelf_(shelf),
      egg_dirs_(std::move(egg_dirs)),
      clock_(o.speed),
      screen_(display_.w, display_.h),
      dev_{brain_, pet_, shelf_, screen_,
           o.name,                               // device_name
           "",                                   // base_url, until set_base_url()
           sim_id(o.name),                       // id
           &setup_},
      radio_([this] { return clock_.now(); }, dev_.id, short_hostname()) {
    dev_.settings = load_settings(o.name, display_);
    dev_.save = [name = o.name](const Settings& s) { store_settings(name, s); };
    dev_.random = [] {
        static std::random_device rd;
        return uint32_t(rd());
    };
    brain_.text_size(dev_.settings.text);
    put_out_first(dev_, display_, saved_choice(), 0);
    if (o.setup) {
        setup_.begin(radio_, shelf_.current_egg().name, clock_.now());
    } else {
        radio_.start_on_home();
        setup_.watch(radio_, shelf_.current_egg().name, radio_.saved());
    }
    shelf_.rescan = [this] { load_eggs(); };
    shelf_.on_change = [this](const std::string& id) {
        save_choice(id);
        const std::string& name = shelf_.current_egg().name;
        if (pet_changed_) pet_changed_(name);
        note("pet: %s\n", name.c_str());
    };
    relay_ = std::make_unique<SimRelay>(SimRelay::Hooks{
        [this] { return clock_.now(); },
        [this]() -> std::optional<RelaySettings> {
            std::lock_guard<std::mutex> lock(mu_);
            if (!dev_.relay_changed && !dev_.relay_state.empty()) return std::nullopt;
            dev_.relay_changed = false;
            return dev_.settings.relay;
        },
        [this](const char* word) {
            std::lock_guard<std::mutex> lock(mu_);
            dev_.relay_state = word;
        },
        [this](const std::string& frame) {
            std::lock_guard<std::mutex> lock(mu_);
            return relay_deliver(dev_, frame, clock_.now());
        }});
}

void SimDevice::load_eggs() {
    for (const auto& dir : egg_dirs_) load_egg_folder(dir, shelf_);
}

void SimDevice::save_choice(const std::string& id) {
    std::filesystem::create_directories(chosen_pet_file().parent_path());
    std::ofstream(chosen_pet_file()) << id << "\n";
}

std::string SimDevice::pet_name() {
    std::lock_guard<std::mutex> lock(mu_);
    return shelf_.current_egg().name;
}

void SimDevice::set_base_url(const std::string& url) {
    std::lock_guard<std::mutex> lock(mu_);
    dev_.base_url = url;
}

Response SimDevice::serve(const HttpRequest& q) {
    std::lock_guard<std::mutex> lock(mu_);
    if (q.path == "/screen.bmp") {
        Response r;
        r.type = "image/bmp";
        r.body = bmp_image(screen_);
        return r;
    }
    if (q.path == ROUTER_PATH && q.method == "POST" && q.loopback) {
        Fields f;
        const std::string want = parse_json(q.body, f) ? f.str("router") : "";
        Response r;
        if (want == "ok") radio_.set_router(SimRadio::Router::Ok);
        else if (want == "password") radio_.set_router(SimRadio::Router::NewPassword);
        else if (want == "gone") radio_.set_router(SimRadio::Router::Gone);
        else set_error(r, 400, "router is \"ok\", \"password\" or \"gone\"");
        return r;
    }
    if (q.path == PASTIME_PATH && q.method == "POST" && q.loopback) {
        Fields f;
        Response r;
        if (!parse_json(q.body, f) || !brain_.play_pastime(f.str("pastime"), clock_.now()))
            set_error(r, 400, "pastime is the name of one of the pet's own pastimes");
        return r;
    }
    if (q.path == HEAD_PATH && q.method == "GET" && q.loopback) {
        const Point head = pet_.drawn_head();
        Response r;
        r.body = "{\"x\": " + std::to_string(int(head.x)) + ", \"y\": " + std::to_string(int(head.y)) + "}";
        return r;
    }
    if (setup_.ap_open() && from_setup_phone(path_only(q.path))) radio_.saw_phone();
    return handle_request(dev_, q.method, q.path, q.body, clock_.now(), q.host, q.origin, q.loopback, q.auth, q.peer);
}

void SimDevice::step() {
    std::lock_guard<std::mutex> lock(mu_);
    const uint32_t now = clock_.now();
    store_pet_change(now);
    const Look s = step_frame(dev_, now);
    if (radio_.take_reset()) start_as_new(now);
    const std::string line = log_.line(s, now, brain_.why());
    if (!line.empty()) note("%s", line.c_str());
}

Shift SimDevice::shift() {
    std::lock_guard<std::mutex> lock(mu_);
    return dev_.shift;
}

float SimDevice::screen_level() {
    std::lock_guard<std::mutex> lock(mu_);
    const Egg* pet = shelf_.pet_out();
    const int awake = backlight_percent(dev_.settings.screen, ScreenState::Awake, pet && !pet->screen_palette.empty());
    return awake > 0 ? float(backlight_now(dev_)) / float(awake) : 0.0f;
}

// BOOT held to the full reset: what a device would come back as after its restart. The pets stay.
void SimDevice::start_as_new(uint32_t now) {
    dev_.settings = Settings{};
    dev_.settings.screen = screen_defaults(display_);
    dev_.relay_changed = true;
    dev_.save(dev_.settings);
    brain_.text_size(dev_.settings.text);
    setup_.begin(radio_, shelf_.current_egg().name, now);
}

// A pet added over the API is written to ~/.nestbox/pets; one taken off is deleted from there. The pets that come
// with the pet window (its own folder) stay as they are.
void SimDevice::store_pet_change(uint32_t now) {
    const Device::PetChange* c = pet_change_to_store(dev_, now);
    if (!c) return;
    namespace fs = std::filesystem;
    const fs::path own = egg_dirs_.back() / (c->id + EGG_EXTENSION);
    for (size_t i = 0; i + 1 < egg_dirs_.size(); i++)
        if (fs::exists(egg_dirs_[i] / (c->id + EGG_EXTENSION))) {
            finish_pet_change(dev_, "it comes with the pet window", now);
            return;
        }
    std::error_code err;
    std::string error;
    if (c->kind == Device::PetChange::Kind::Remove) {
        fs::remove(own, err);
        if (err) error = "can't delete " + own.string() + ": " + err.message();
    } else {
        fs::create_directories(own.parent_path(), err);
        const fs::path part = fs::path(own).concat(PART_SUFFIX);
        std::ofstream out(part, std::ios::binary);
        out.write(reinterpret_cast<const char*>(c->egg.bytes.data()), std::streamsize(c->egg.bytes.size()));
        out.close();
        if (out) fs::rename(part, own, err);
        if (!out || err) {
            error = "can't write " + own.string() + (err ? ": " + err.message() : "");
            fs::remove(part, err);
        }
    }
    finish_pet_change(dev_, error, now);
    note("pets: %s\n", dev_.pet_change_done.c_str());
}

void SimDevice::press(const std::string& kind) {
    std::lock_guard<std::mutex> lock(mu_);
    handle_request(dev_, "POST", INPUT_PATH, button_input(kind), clock_.now(), "", "", true);
}

void SimDevice::boot(bool down) {
    std::lock_guard<std::mutex> lock(mu_);
    handle_request(dev_, "POST", INPUT_PATH, down ? R"({"boot":"down"})" : R"({"boot":"up"})", clock_.now(), "", "",
                   true);
}

bool SimDevice::debug_key(int key, bool shift) {
    std::lock_guard<std::mutex> lock(mu_);
    const uint32_t now = clock_.now();
    if (key >= '0' && key <= '9') {
        const Look s = DIGIT_KEY_LOOKS[key - '0'];
        if (is_reaction(s)) brain_.show_reaction(s, now);
        else brain_.hold(s);
    } else if (key == 'a') {
        brain_.automatic();
    } else if (key == 'u') {
        setup_.begin(radio_, shelf_.current_egg().name, now);
    } else if (key == 'd') {
        cycle_daypart();
    } else if (key == 'r') {
        cycle_router();
    } else if (key == 'w') {
        brain_.hold(Look::Watch);
    } else if (key == 's') {
        step_named_look(shift);
    } else if (key == 'm') {
        toggle_music(now);
    } else if (key == 'p') {
        brain_.next_pastime(now);
    } else if (key == '\t') {
        load_eggs();
        size_t i;
        if (shelf_.find(shift ? "previous" : "next", i)) shelf_.put_out(i, pet_, screen_, now);
    } else {
        return false;
    }
    return true;
}

// Morning, afternoon, evening, night, then the clock's own time of day again.
void SimDevice::cycle_daypart() {
    if (!held_daypart_) held_daypart_ = Daypart::Morning;
    else if (*held_daypart_ == Daypart::Night) held_daypart_.reset();
    else held_daypart_ = Daypart(int(*held_daypart_) + 1);
    brain_.hold_daypart(held_daypart_);
}

// The pretend router: works, new password, gone, works again.
void SimDevice::cycle_router() {
    using R = SimRadio::Router;
    const R r = radio_.router();
    radio_.set_router(r == R::Ok ? R::NewPassword : r == R::NewPassword ? R::Gone : R::Ok);
}

// Every look in turn: reactions play for a while, the others are held.
void SimDevice::step_named_look(bool back) {
    const int count = int(Look::Act);
    named_look_ = Look((int(named_look_) + (back ? count - 1 : 1)) % count);
    if (is_reaction(named_look_)) brain_.show_reaction(named_look_, clock_.now(), NAMED_LOOK_MS);
    else brain_.hold(named_look_);
}

// Music from the window's M key: a note from "simulator", on until the key is pressed again.
void SimDevice::toggle_music(uint32_t now) {
    Fields f;
    f.set("from", "simulator");
    f.set("id", "music");
    if (music_) {
        f.set("state", "over");
    } else {
        f.set("state", "on");
        f.set("do", "dance");
        f.set("effect", "notes");
        f.set("for", "10m");
    }
    music_ = !music_;
    brain_.tell(f, now);
}

std::pair<std::string, std::string> SimDevice::label() {
    std::lock_guard<std::mutex> lock(mu_);
    return {shelf_.current_egg().name + "  " + look_label(pet_.look()),
            brain_.held() ? "HELD - A FOR AUTO" : brain_.why()};
}

}  // namespace nestbox
