// A pretend Wi-Fi radio for setup in the simulator (nestbox-sim --setup): a few networks, each with a fixed outcome,
// joined with realistic delays. The phone counts as connected while the setup page keeps making requests.
//
//   Home (strongest)                  password sunflower1 joins as 192.168.1.37; any other: wrong password
//   Home Guest                        password welcome123 joins but gets no address
//   Neighbours 2.4 (faint)            never joins
//   Office                            WPA-Enterprise (needs a login)
//   Cafe Free WiFi                    open, joins as 10.0.0.23
//   TheVeryLongNetworkNameForTesting  password longpassword joins as 192.168.100.200
//   הבית שלנו (Hebrew)                password shalom123 joins as 192.168.1.44
//   any other name                    never answers (not found)
//
// The pet starts on Home, saved earlier (or with nothing saved, under --setup). The router can be broken to test
// the rescue: Home's password changes to sunflower2, or Home disappears; "ok" puts it back as it was.
#pragma once
#include <cstdint>
#include <functional>
#include <random>
#include <utility>
#include <string>
#include <vector>
#include "door/setup.h"
#include "log.h"

namespace nestbox {

class SimRadio : public SetupRadio {
public:
    enum class Router { Ok, NewPassword, Gone };

    SimRadio(std::function<uint32_t()> clock, std::string id, std::string host)
        : now_(std::move(clock)), id_(std::move(id)), host_(std::move(host)) {}

    // Saves Home as if set up earlier.
    void start_on_home() {
        saved_ = HOME;
        saved_pass_ = HOME_PASSWORD;
    }
    const std::string& saved() const { return saved_; }

    void set_router(Router r) {
        router_ = r;
        note("setup: pretend router %s\n", r == Router::Ok ? "works again" : r == Router::Gone ? "is gone"
                                                                                               : "has a new password");
    }
    Router router() const { return router_; }

    // A request from the setup page: the phone is there.
    void saw_phone() {
        last_phone_ = now_();
        seen_phone_ = true;
    }

    void scan() override { scan_done_ = now_() + SCAN_MS; }
    bool scanned(std::vector<WifiNet>& out) override {
        if (!scan_done_ || now_() < scan_done_) return false;
        scan_done_ = 0;
        out = {{"Home Guest", -63, 6, true, false},
               {HEBREW, -66, 6, true, false},
               {"Cafe Free WiFi", -71, 1, false, false},
               {LONG_NAME, -75, 6, true, false},
               {"Office", -58, 11, true, true},
               {"Neighbours 2.4", -84, 1, true, false},
               {HOME, -80, 11, true, false}};    // a second access point of the same network
        if (router_ != Router::Gone) out.insert(out.begin(), {HOME, -48, 6, true, false});
        else out.pop_back();
        return true;
    }
    void open_ap(const std::string& ssid, const std::string&, int channel) override {
        ap_ = true;
        seen_phone_ = false;
        note("setup: own network %s open on channel %d\n", ssid.c_str(), channel);
    }
    void close_ap() override {
        ap_ = false;
        note("setup: own network closed\n");
    }
    int phones() override { return ap_ && seen_phone_ && now_() - last_phone_ < PHONE_GONE_MS ? 1 : 0; }

    void join(const std::string& ssid, const std::string& pass, int) override {
        const uint32_t t = now_();
        auto then = [&](uint32_t ms, Join j, const char* ip = "") {
            done_at_ = t + ms;
            outcome_ = j;
            ip_ = ip;
        };
        auto with_password = [&](const char* right, uint32_t ms, Join j, const char* ip = "") {
            if (pass == right) then(ms, j, ip);
            else then(WRONG_PASSWORD_MS, Join::WrongPassword);
        };
        if (ssid == HOME && router_ == Router::Gone) then(8000, Join::NotFound);
        else if (ssid == HOME) with_password(home_password(), 3000, Join::Ok, "192.168.1.37");
        else if (ssid == "Home Guest") with_password("welcome123", 5000, Join::NoAddress);
        else if (ssid == "Neighbours 2.4") then(8000, Join::Failed);
        else if (ssid == "Cafe Free WiFi") then(2500, Join::Ok, "10.0.0.23");
        else if (ssid == LONG_NAME) with_password("longpassword", 3000, Join::Ok, "192.168.100.200");
        else if (ssid == HEBREW) with_password("shalom123", 3000, Join::Ok, "192.168.1.44");
        else then(UINT32_MAX / 2, Join::Trying);
        note("setup: trying %s\n", ssid.c_str());
    }
    Join joined(std::string& ip) override {
        if (now_() < done_at_) return Join::Trying;
        ip = ip_;
        return outcome_;
    }
    void keep(const std::string& ssid, const std::string& pass) override {
        saved_ = ssid;
        saved_pass_ = pass;
        note("setup: saved %s\n", ssid.c_str());
    }
    Link link() override {
        if (saved_.empty() || held_) return Link::Lost;
        if (saved_ != HOME) return Link::Online;            // the other networks never break
        if (router_ == Router::Gone) return Link::Unseen;
        return saved_pass_ == home_password() ? Link::Online : Link::Refused;
    }
    void hold_saved(bool hold) override {
        held_ = hold;
        note("setup: %s %s\n", hold ? "stops rejoining" : "rejoins", saved_.empty() ? "(none saved)" : saved_.c_str());
    }
    void forget() override {
        note("setup: forgot %s\n", saved_.empty() ? "nothing" : saved_.c_str());
        saved_.clear();
        saved_pass_.clear();
    }
    void reset_all() override {
        note("setup: reset everything but the pets\n");
        saved_.clear();
        saved_pass_.clear();
        reset_ = true;
    }
    // True once after reset_all(): the device then starts again as new.
    bool take_reset() { return std::exchange(reset_, false); }
    void set_time(int64_t unix_s, const std::string& posix_tz, int32_t utc_offset_s) override {
        note("setup: time %lld, zone %s (%+d s)\n", static_cast<long long>(unix_s), posix_tz.c_str(), utc_offset_s);
    }
    void save(const std::string& key, const std::string& value) override {
        note("setup: %s = %s\n", key.c_str(), value.c_str());
    }
    uint32_t random() override { return rng_(); }
    std::string id() override { return id_; }
    std::string host() override { return host_; }

private:
    static constexpr const char* HOME = "Home";
    static constexpr const char* HOME_PASSWORD = "sunflower1";
    static constexpr const char* LONG_NAME = "TheVeryLongNetworkNameForTesting";
    static constexpr const char* HEBREW = "\xd7\x94\xd7\x91\xd7\x99\xd7\xaa \xd7\xa9\xd7\x9c\xd7\xa0\xd7\x95";
    static constexpr uint32_t SCAN_MS = 1500, WRONG_PASSWORD_MS = 4000, PHONE_GONE_MS = 5000;

    std::function<uint32_t()> now_;
    std::string id_, host_;
    std::random_device rng_;
    uint32_t scan_done_ = 0, last_phone_ = 0, done_at_ = 0;
    bool ap_ = false, seen_phone_ = false;
    Join outcome_ = Join::Trying;
    std::string ip_;
    Router router_ = Router::Ok;
    std::string saved_, saved_pass_;
    bool held_ = false, reset_ = false;

    const char* home_password() const { return router_ == Router::NewPassword ? "sunflower2" : HOME_PASSWORD; }
};

}  // namespace nestbox
