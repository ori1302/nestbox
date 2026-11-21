#include "door/setup.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include "door/setup_parts.h"
#include "util/deadline.h"
#include "util/json.h"
#include "util/tzdata.h"

namespace nestbox {

namespace {

const char ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // 32 symbols, no 0/O or 1/I
constexpr size_t NETWORK_NAME_MAX = 10;     // letters of the pet's name in the network name (keeps the QR small)
constexpr int AP_PASSWORD_LEN = 8, TOKEN_LEN = 4, SUFFIX_LEN = 4;
constexpr uint32_t PAGE_ALIVE_MS = 15000;   // the setup page polls about once a second
constexpr uint32_t HELLO_MAX_MS = 3 * Setup::HELLO_MS;   // open the access point even if the scan hangs
constexpr int32_t MAX_UTC_OFFSET_S = 24 * 3600;           // a phone's reported UTC offset is cut to a day either way

const char* phase_key(Setup::Phase p) {
    switch (p) {
        case Setup::Phase::Hello: return "hello";
        case Setup::Phase::ShowCode: return "join";
        case Setup::Phase::Joined: return "joined";
        case Setup::Phase::Page: return "page";
        case Setup::Phase::Checking: return "checking";
        case Setup::Phase::Failed: return "failed";
        case Setup::Phase::AllSet: return "allset";
        default: return "off";
    }
}

uint32_t fnv1a(const std::string& s) {
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ uint8_t(c)) * 16777619u;
    return h;
}

// The access point's name: the pet's name in plain capitals and a suffix derived from the device id.
std::string network_name(const std::string& pet, const std::string& id) {
    std::string n;
    for (char c : pet)
        if (std::isalnum(static_cast<unsigned char>(c)) && n.size() < NETWORK_NAME_MAX)
            n += char(std::toupper(static_cast<unsigned char>(c)));
    if (n.empty()) n = "NESTBOX";
    std::string suffix;
    uint32_t h = fnv1a(id);
    for (int i = 0; i < SUFFIX_LEN; i++, h >>= 5) suffix += ALPHABET[h & 31];
    return n + "-" + suffix;
}

// A POSIX TZ rule for a fixed offset (seconds east of UTC). POSIX counts the other way: UTC+2 is "<+0200>-2:00".
std::string fixed_offset_tz(int32_t off) {
    char buf[24];
    int a = off < 0 ? -off : off;
    std::snprintf(buf, sizeof buf, "<%c%02d%02d>%c%d:%02d", off < 0 ? '-' : '+', a / 3600, a / 60 % 60,
                  off < 0 ? '+' : '-', a / 3600, a / 60 % 60);
    return buf;
}

}  // namespace

std::string Setup::code(int n) {
    std::string s;
    for (int i = 0; i < n; i++) s += ALPHABET[radio_->random() & 31];   // only called with a radio
    return s;
}

void Setup::begin(SetupRadio& radio, const std::string& pet, uint32_t now) {
    radio_ = &radio;
    pet_ = pet.empty() ? "NestBox" : pet;
    ap_ssid_ = network_name(pet_, radio.id());
    offline_ = false;
    enter(Phase::Hello, now);
    nets_.clear();
    radio.scan();
    scanning_ = true;
    want_ssid_.clear(); want_pass_.clear(); ip_.clear(); token_.clear(); host_.clear();
    result_ = Join::Trying;
    ok_at_ = page_seen_ = phone_gone_ = close_at_ = 0;
    rescue_ = Rescue::None;
    again_ = false;
}

void Setup::watch(SetupRadio& radio, const std::string& pet, const std::string& ssid) {
    radio_ = &radio;
    if (!pet.empty()) pet_ = pet;
    saved_ = ssid;
    rescue_ = Rescue::None;
    off_ = refused_ = false;
}

void Setup::end() {
    close_ap();
    phase_ = Phase::Off;
    offline_ = false;
    close_at_ = ok_at_ = 0;
    rescue_ = Rescue::None;
    off_ = refused_ = false;
}

void Setup::start_over(uint32_t now, uint32_t delay_ms) {
    forget_due_ = true;
    forget_at_ = deadline(now, delay_ms);
}

void Setup::boot(bool down, uint32_t now) {
    if (down && !holding_) hold_since_ = now;
    if (!down && holding_ && now - hold_since_ >= START_OVER_MS) start_over(now);
    holding_ = down;
}

void Setup::open_ap() {
    ap_pass_ = code(AP_PASSWORD_LEN);
    // The radio has one channel for both interfaces; the strongest network is most likely the one to join.
    int channel = nets_.empty() ? 1 : nets_[0].channel;
    radio_->open_ap(ap_ssid_, ap_pass_, channel > 0 ? channel : 1);
    ap_open_ = true;
    close_at_ = phone_gone_ = 0;
}

void Setup::close_ap() {
    if (radio_ && ap_open_) radio_->close_ap();
    ap_open_ = false;
    close_at_ = 0;
}

const WifiNet* Setup::seen(const std::string& ssid) const {
    auto it = std::find_if(nets_.begin(), nets_.end(), [&](const WifiNet& n) { return n.ssid == ssid; });
    return it == nets_.end() ? nullptr : &*it;
}

std::string Setup::welcome_url() const { return "http://" + ip_ + "/welcome?t=" + token_; }

std::string Setup::mdns_name() const { return host_.empty() ? "" : host_ + ".local"; }

// One entry per network name, strongest first.
void Setup::poll_scan() {
    std::vector<WifiNet> got;
    if (!scanning_ || !radio_->scanned(got)) return;
    scanning_ = false;
    std::sort(got.begin(), got.end(), [](const WifiNet& a, const WifiNet& b) { return a.rssi > b.rssi; });
    nets_.clear();
    for (auto& n : got)
        if (!n.ssid.empty() && !seen(n.ssid)) nets_.push_back(n);
}

void Setup::update(uint32_t now) {
    if (!radio_) return;
    follow_hold(now);
    if (forget_due_ && !is_before(now, forget_at_)) {
        forget_due_ = false;
        radio_->forget();
        saved_.clear();
        begin(*radio_, pet_, now);
        again_ = true;
    }
    poll_scan();
    const bool page_alive = page_seen_ && now - page_seen_ < PAGE_ALIVE_MS;
    const int phones = ap_open_ ? radio_->phones() : 0;
    step(now, phones, page_alive);
    follow_rescue(now);
    follow_link(now);
    close_ap_when_done(now, phones);
    // While setup looks for a network, rejoining the saved one would move the radio off the phone's channel.
    const bool hold = active() && phase_ != Phase::AllSet;
    if (hold != held_saved_) {
        held_saved_ = hold;
        radio_->hold_saved(hold);
    }
}

// BOOT held to RESET_ALL_MS resets everything; letting go before START_OVER_MS cancels, and between the two starts
// over (boot()).
void Setup::follow_hold(uint32_t now) {
    if (!holding_ || now - hold_since_ < RESET_ALL_MS) return;
    holding_ = false;
    radio_->reset_all();
}

// Setup opened from the offer goes back to the offer when no phone comes.
void Setup::follow_rescue(uint32_t now) {
    if (rescue_ != Rescue::Open) return;
    if (phase_ == Phase::Off) rescue_ = off_ ? Rescue::Offered : Rescue::None;
    else if (phase_ == Phase::ShowCode && now - since_ >= RESCUE_IDLE_MS) {
        close_ap();
        phase_ = Phase::Off;
        rescue_ = Rescue::Offered;
    }
}

// How long the pet has been off the saved network, and whether to offer setup.
void Setup::follow_link(uint32_t now) {
    if (phase_ != Phase::Off || saved_.empty() || offline_) return;
    const Link k = radio_->link();
    if (k == Link::Online) {
        off_ = refused_ = false;
        rescue_ = Rescue::None;
        return;
    }
    if (!off_) {
        off_ = true;
        off_since_ = now;
    }
    if (k != Link::Refused) refused_ = false;
    else if (!refused_) {
        refused_ = true;
        refused_since_ = now;
    }
    if (rescue_ == Rescue::None && ((refused_ && now - refused_since_ >= REFUSED_MS) || now - off_since_ >= UNSEEN_MS))
        rescue_ = Rescue::Offered;
}

std::string Setup::caption(uint32_t now) const {
    if (active() || offline_ || saved_.empty() || !off_) return "";
    if (rescue_ == Rescue::Offered)
        return refused_ ? "Wi-Fi password changed? Tap me" : "No Wi-Fi. Tap me to set it up";
    return now - off_since_ >= QUIET_MS ? "No Wi-Fi" : "";
}

void Setup::step(uint32_t now, int phones, bool page_alive) {
    switch (phase_) {
        case Phase::Hello:
            if ((!scanning_ && now - since_ >= HELLO_MS) || now - since_ >= HELLO_MAX_MS) {
                open_ap();
                enter(Phase::ShowCode, now);
            }
            break;
        case Phase::ShowCode:
            if (page_alive) enter(Phase::Page, now);
            else if (phones > 0) enter(Phase::Joined, now);
            break;
        case Phase::Joined:
            if (page_alive) enter(Phase::Page, now);
            else if (phones == 0) enter(Phase::ShowCode, now);
            break;
        case Phase::Page:
        case Phase::Failed:
            if (phones == 0 && !page_alive) enter(Phase::ShowCode, now);
            break;
        case Phase::Checking: {
            std::string ip;
            Join j = radio_->joined(ip);
            if (j == Join::Trying && now - since_ >= CHECK_MS) j = seen(want_ssid_) ? Join::Failed : Join::NotFound;
            if (j != Join::Trying) finish_check(j, ip, now);
            break;
        }
        case Phase::AllSet:
            if (now - since_ >= ALL_SET_MS) phase_ = Phase::Off;
            break;
        case Phase::Off:
            break;
    }
}

void Setup::finish_check(Join result, const std::string& ip, uint32_t now) {
    result_ = result;
    if (result == Join::Ok) {
        ip_ = ip;
        radio_->keep(want_ssid_, want_pass_);
        saved_ = want_ssid_;
        rescue_ = Rescue::None;
        off_ = refused_ = false;
        token_ = code(TOKEN_LEN);
        host_ = radio_->host();
        ok_at_ = now;
        enter(Phase::AllSet, now);
    } else {
        enter(Phase::Failed, now);
    }
    want_pass_.clear();
}

// After a successful join, the access point closes once the phone has left it (or after HANDOVER_MAX_MS).
void Setup::close_ap_when_done(uint32_t now, int phones) {
    if (!ap_open_) return;
    if (close_at_ && !is_before(now, close_at_)) {
        close_ap();
    } else if (ok_at_) {
        if (phones > 0) phone_gone_ = 0;
        else if (!phone_gone_) phone_gone_ = now;
        if ((phone_gone_ && now - phone_gone_ >= HANDOVER_MS) || now - ok_at_ >= HANDOVER_MAX_MS) close_ap();
    }
}

bool Setup::tap(uint32_t now) {
    if (phase_ == Phase::AllSet) {
        phase_ = Phase::Off;
        return true;
    }
    if (phase_ != Phase::Off || rescue_ != Rescue::Offered || !radio_) return false;
    begin(*radio_, pet_, now);
    rescue_ = Rescue::Open;
    again_ = true;
    return true;
}

std::string Setup::reason(Audience a) const {
    const bool screen = a == Audience::Screen;
    const std::string net = screen ? screen_name(want_ssid_) : want_ssid_;
    const WifiNet* n = seen(want_ssid_);
    const bool weak = n && n->rssi < WEAK_RSSI;
    switch (result_) {
        case Join::WrongPassword:
            if (weak) break;
            return screen ? "That password didn't work for " + net : "That password didn't work for " + net + ".";
        case Join::NotFound:
            return screen ? "I can't see " + net + ". I need 2.4~GHz."
                          : "I can't see " + net + ". I need a 2.4 GHz network; many routers offer one under the same "
                            "name, or under a name without \"5G\".";
        case Join::NoAddress:
            return screen ? net + " let me in but gave me no address"
                          : net + " let me in but didn't give me an address. Try again, or restart the router.";
        case Join::NeedsLogin:
            return screen ? "This Wi-Fi needs a login I can't do"
                          : net + " needs a login I can't do. A phone's hotspot works (on iPhone, turn on "
                                  "Maximize Compatibility).";
        default:
            break;
    }
    if (weak)
        return screen ? "I can barely hear " + net + ". Move me closer."
                      : "I can barely hear " + net + ". Move me closer to the router, or check the password.";
    return screen ? "I couldn't join " + net : "I couldn't join " + net + ". Check the password and try again.";
}

std::string Setup::status_json(uint32_t now) const {
    JsonWriter w;
    w.begin_object()
        .field("phase", phase_key(phase_))
        .field("pet", pet_)
        .field("ssid", want_ssid_)
        .key("elapsed_ms").num(static_cast<long long>(phase_ == Phase::Checking ? now - since_ : 0))
        .key("limit_ms").num(static_cast<long long>(CHECK_MS))
        .field("ok", ok_at_ != 0)
        .field("reason", phase_ == Phase::Failed ? reason(Audience::Page) : "")
        .field("ip", ip_)
        .field("url", ip_.empty() ? "" : welcome_url())
        .field("host", mdns_name())
        .field("tz", tz_)
        .field("offline", offline_)
        .field("again", again_);
    return w.end_object().text();
}

// The phone's clock and time zone, sent with its requests: "time" in ms, "tz" an IANA name, "utc_offset" seconds.
void Setup::clock_from(const Fields& f) {
    if (!f.has("time") || !radio_) return;
    const int32_t off = std::clamp(f.integer<int32_t>("utc_offset", 0), -MAX_UTC_OFFSET_S, MAX_UTC_OFFSET_S);
    const char* rule = posix_tz(f.str("tz"));
    tz_ = rule ? rule : fixed_offset_tz(off);
    radio_->set_time(f.integer<int64_t>("time", 0) / 1000, tz_, off);
}

void Setup::preview(Phase p, const std::string& pet, const std::string& ssid, const std::string& ap_pass, Join why,
                    const std::string& ip, const std::string& host, bool again) {
    radio_ = nullptr;
    again_ = again;
    holding_ = false;
    phase_ = p;
    pet_ = pet;
    ap_ssid_ = network_name(pet, "preview");
    ap_pass_ = ap_pass;
    want_ssid_ = ssid;
    result_ = why;
    ip_ = ip;
    host_ = host;
    token_ = "K3Q8";
    since_ = page_seen_ = 0;
    nets_.clear();
    if (why == Join::Failed && p == Phase::Failed) nets_.push_back({ssid, -86, 6, true, false});   // a weak signal
}

}  // namespace nestbox
