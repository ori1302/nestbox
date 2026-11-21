#include "activity.h"

#include <time.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <utility>

#include "bonjour.h"
#include "door/http.h"
#include "http.h"
#include "pairing_store.h"
#include "sensors.h"
#include "util/json.h"

namespace companion {
namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr milliseconds TICK{100};
constexpr milliseconds TYPING_INTERVAL{250};       // while typing, so the pet's speed follows closely
constexpr milliseconds STOPPED_AFTER{600};         // no key for this long: typing stopped, reported at once
constexpr milliseconds QUIET_INTERVAL{5000};       // otherwise
constexpr int NEXT_S = 10;                         // the promise: a report within this many seconds
constexpr std::chrono::seconds REDISCOVER_INTERVAL{10};
constexpr int DISCOVERY_BROWSE_MS = 1500;
constexpr int POST_TIMEOUT_MS = 800;
constexpr int HTTP_OK = 200, HTTP_UNAUTHORIZED = 401;

std::string user_json(const CompanionOptions& o, const std::string& from, uint32_t keys) {
    nestbox::JsonWriter json;
    json.begin_object().field("from", from);
    if (o.keys) json.field("keys", static_cast<long long>(keys));
    if (o.idle) json.key("idle").num(idle_seconds(), 0);
    json.field("state", o.lock && screen_locked() ? "locked" : "awake");
    time_t now = time(nullptr);
    tm local{};
    localtime_r(&now, &local);
    char hhmm[8];
    std::snprintf(hhmm, sizeof hhmm, "%02d:%02d", local.tm_hour, local.tm_min);
    json.field("time", hhmm).field("next", NEXT_S);
    return json.end_object().text();
}

void print_machine_line(const nestbox::JsonWriter& json) { std::printf("%s%s\n", MACHINE_TAG, json.text().c_str()); }

// With a paired id, only that pet. Otherwise --name if given, else devices before simulators. Without --name, the
// only device with an id on the network becomes the paired pet; several devices and none paired: the first,
// unpaired, until the menu bar app pairs one.
struct Discovery {
    NetworkPet pet;
    bool newly_paired = false;
};

Discovery discover(const std::string& name, const std::string& paired) {
    auto pets = pets_on_network(DISCOVERY_BROWSE_MS, name);
    if (!paired.empty()) {
        for (auto& p : pets)
            if (p.id == paired) return {p};
        return {};
    }
    if (pets.empty()) return {};
    size_t devices = 0;
    for (auto& p : pets) devices += !p.simulator() && !p.id.empty();
    const bool pair = name.empty() && devices == 1 && !pets[0].simulator() && !pets[0].id.empty();
    return {pets[0], pair};
}

// SIGUSR1 from the app: the Mac woke or changed network, so the pet may be at a new address.
std::atomic<bool> look_again{false};
void on_look_again(int) { look_again = true; }

class Companion {
public:
    explicit Companion(CompanionOptions options)
        : o_(std::move(options)), url_(o_.device), from_(short_host()), last_count_(o_.keys ? key_presses() : 0),
          sample_count_(last_count_) {}

    [[noreturn]] void run() {
        for (;;) {
            std::this_thread::sleep_for(TICK);
            if (look_again.exchange(false)) start_again();
            if (!due()) continue;
            if (url_.empty() && Clock::now() - last_discovery_ > REDISCOVER_INTERVAL) look_for_pet();
            Url target;
            if (url_.empty() || !parse_url(url_, target)) continue;
            send(target);
        }
    }

private:
    // Samples the keys; true when a report is due: at once when typing starts or stops (STOPPED_AFTER without a
    // key), every TYPING_INTERVAL while typing, else every QUIET_INTERVAL.
    bool due() {
        const Clock::time_point now = Clock::now();
        const uint32_t count = o_.keys ? key_presses() : 0;
        const uint32_t pressed = count - sample_count_;      // unsigned: correct across counter wrap-around
        sample_count_ = count;
        if (pressed) last_key_ = now;
        const bool typing = now - last_key_ < STOPPED_AFTER;
        const bool changed = typing != was_typing_;
        was_typing_ = typing;
        return changed || now - last_sent_ >= (typing ? TYPING_INTERVAL : QUIET_INTERVAL);
    }

    // Drops the connection and reports at once; without --device, finds the pet again first.
    void start_again() {
        std::printf("the Mac woke or changed network; reaching the pet again\n");
        pet_.close();
        const Clock::time_point long_ago = Clock::now() - std::chrono::hours(1);
        last_sent_ = long_ago;
        if (o_.device.empty()) {
            url_.clear();
            last_discovery_ = long_ago;
        }
    }

    void look_for_pet() {
        last_discovery_ = Clock::now();
        Discovery found = discover(o_.name, o_.id);
        if (found.newly_paired) {
            o_.id = found.pet.id;
            std::printf("paired with %s (%s)\n", o_.id.c_str(), found.pet.name.c_str());
            machine("paired").field("id", o_.id).field("name", found.pet.name);
            flush_machine();
        }
        url_ = found.pet.url;
        if (url_.empty()) return;
        const NetworkPet& pet = found.pet;
        std::printf("found the pet at %s%s%s\n", url_.c_str(), pet.id.empty() ? "" : " id=", pet.id.c_str());
        machine("found").field("url", url_).field("id", pet.id);
        flush_machine();
        // The file the hooks read follows this pet, unless another pet is paired (a run with --name, say).
        if (o_.id == pet.id || paired_id().empty()) remember_pet(url_, o_.id == pet.id ? o_.id : "");
    }

    // Starts a tagged line for `event`; flush_machine() prints it if --machine is on.
    nestbox::JsonWriter& machine(const char* event) {
        machine_ = nestbox::JsonWriter();
        return machine_.begin_object().field("event", event);
    }

    void flush_machine() {
        machine_.end_object();
        if (o_.machine) print_machine_line(machine_);
    }

    void send(const Url& target) {
        const uint32_t count = o_.keys ? key_presses() : 0;
        const std::string body = user_json(o_, from_, count - last_count_);
        last_count_ = count;
        std::string reply;
        const int status = pet_.post(target, nestbox::USER_PATH, body, POST_TIMEOUT_MS, &reply, paired_key());
        last_sent_ = Clock::now();
        if (o_.print) std::printf("%s -> %d %s\n", body.c_str(), status, reply.c_str());
        machine("sent").field("status", status);
        flush_machine();
        if (status == HTTP_UNAUTHORIZED && !needs_key_) {
            std::printf("the pet needs the key: run `nestbox pair` and press the pet's button\n");
            machine("needs_key").field("url", url_);
            flush_machine();
        }
        needs_key_ = status == HTTP_UNAUTHORIZED;
        const bool answered = status == HTTP_OK || status == HTTP_UNAUTHORIZED;
        if (answered && !reachable_) {
            std::printf("connected to %s\n", url_.c_str());
            machine("connected").field("url", url_);
            flush_machine();
            reachable_ = true;
        }
        if (!answered && reachable_) {
            std::printf("lost the pet at %s; looking again\n", url_.c_str());
            machine("lost").field("url", url_);
            flush_machine();
            reachable_ = false;
            pet_.close();
        }
        if (!answered && o_.device.empty()) url_.clear();    // it may have a new address
    }

    CompanionOptions o_;
    nestbox::JsonWriter machine_;
    std::string url_;
    Connection pet_;    // kept open between reports
    std::string from_;
    bool reachable_ = false, needs_key_ = false;
    uint32_t last_count_ = 0, sample_count_ = 0;
    bool was_typing_ = false;
    Clock::time_point last_key_ = Clock::now() - std::chrono::hours(1);
    Clock::time_point last_sent_ = Clock::now() - std::chrono::hours(1);
    Clock::time_point last_discovery_ = Clock::now() - std::chrono::hours(1);
};

}  // namespace

void run_companion(CompanionOptions options) {
    std::signal(SIGUSR1, on_look_again);
    if (!options.id_given && options.name.empty()) options.id = paired_id();
    Companion(std::move(options)).run();
}

}  // namespace companion
