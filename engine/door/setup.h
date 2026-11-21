// Wi-Fi setup with a phone. The pet opens its own access point next to its station interface, on the same channel;
// the phone joins it from a QR code, is led to the setup page (captive portal), picks a network and stays connected
// while the pet tries it. Credentials are saved only after a successful join; then the page and the screen hand the
// phone over to the pet's address on the home network. The radio is the platform's (SetupRadio); every call
// returns at once and results are polled.
//
// Once on Wi-Fi, setup watches the saved network. A long outage (the password changed, the router replaced, a move)
// puts a line on the screen, and a tap opens setup again. Holding BOOT forgets Wi-Fi and starts over; holding it much
// longer resets everything but the pets.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include "door/http.h"
#include "egg/egg.h"
#include "screen/layout.h"

namespace nestbox {

struct WifiNet {
    std::string ssid;
    int rssi = -100;             // dBm
    int channel = 0;
    bool secure = true;          // needs a password
    bool enterprise = false;     // WPA-Enterprise (user name and password); unsupported
};

enum class Join { Trying, Ok, WrongPassword, NotFound, NoAddress, NeedsLogin, Failed };

// The saved network: joined, refused (the password), not seen, or lost for another reason (a router restarting).
enum class Link { Online, Refused, Unseen, Lost };

// What SetupRadio::save() keeps: the name given to the pet, and that it runs without Wi-Fi ("1").
constexpr const char* SAVE_NAME = "name";
constexpr const char* SAVE_OFFLINE = "offline";

// The platform's radio, clock and storage.
struct SetupRadio {
    virtual ~SetupRadio() = default;
    virtual void scan() = 0;
    virtual bool scanned(std::vector<WifiNet>& out) = 0;              // true once per finished scan
    virtual void open_ap(const std::string& ssid, const std::string& password, int channel) = 0;
    virtual void close_ap() = 0;
    virtual int phones() = 0;                                         // stations connected to the pet's access point
    // Tries a network while the access point stays up; `channel` (0: unknown) speeds up finding it.
    virtual void join(const std::string& ssid, const std::string& password, int channel) = 0;
    virtual Join joined(std::string& ip) = 0;                         // Trying until it knows
    virtual void keep(const std::string& ssid, const std::string& password) = 0;   // persist working credentials
    virtual Link link() = 0;                                          // how the saved network is doing
    // Pauses (true) or resumes rejoining the saved network, so the phone on the pet's own network keeps its channel.
    virtual void hold_saved(bool hold) = 0;
    virtual void forget() = 0;                                        // the saved network and offline mode
    // Everything kept but the pets (Wi-Fi, the name, pairing, Lock, time zone, screen settings), then a restart.
    virtual void reset_all() = 0;
    virtual void set_time(int64_t unix_s, const std::string& posix_tz, int32_t utc_offset_s) = 0;
    virtual void save(const std::string& key, const std::string& value) = 0;      // SAVE_NAME, SAVE_OFFLINE
    virtual uint32_t random() = 0;
    virtual std::string id() = 0;                                     // permanent; seeds the network name's suffix
    virtual std::string host() = 0; // its mDNS name without ".local", e.g. nestbox-3f2a
};

class Setup {
public:
    const std::string& name() const { return pet_; }   // the name given in setup, else the pet's
    enum class Phase { Off, Hello, ShowCode, Joined, Page, Checking, Failed, AllSet };
    static constexpr const char* AP_IP = "192.168.4.1";
    static constexpr uint32_t HELLO_MS = 3000, HINT_MS = 10000, CHECK_MS = 20000;
    static constexpr uint32_t HANDOVER_MS = 10000, HANDOVER_MAX_MS = 180000, ALL_SET_MS = 300000, CLOSE_SOON_MS = 3000;
    // Off the saved network: the screen says so after QUIET_MS, and offers setup after REFUSED_MS of a refused
    // password or UNSEEN_MS of anything else. Setup opened that way closes again after RESCUE_IDLE_MS without a phone.
    static constexpr uint32_t QUIET_MS = 30000, REFUSED_MS = 120000, UNSEEN_MS = 600000, RESCUE_IDLE_MS = 300000;
    // BOOT held: a countdown shows after HOLD_SHOW_MS (unless set_hold_show() moved it). Let go after START_OVER_MS
    // and Wi-Fi is forgotten; held to RESET_ALL_MS, everything but the pets is reset.
    static constexpr uint32_t HOLD_SHOW_MS = 1000, START_OVER_MS = 5000, RESET_ALL_MS = 20000;

    // Starts setup (no Wi-Fi saved): greet, scan, then open the access point. `pet` names the network (NIBBLE-7K3Q).
    void begin(SetupRadio& radio, const std::string& pet, uint32_t now);
    // Runs without Wi-Fi ("Just be a pet" was chosen earlier), showing a small corner mark.
    void offline(SetupRadio& radio) { radio_ = &radio; offline_ = true; }
    // Watches the saved network `ssid` (Wi-Fi was set up earlier, or another way). An empty `ssid` only keeps the
    // radio, for starting over.
    void watch(SetupRadio& radio, const std::string& pet, const std::string& ssid);
    // The device's name changed (another pet came out, and no name was given in setup).
    void rename(const std::string& pet) {
        if (!pet.empty()) pet_ = pet;
    }
    // Forgets Wi-Fi and starts setup again, after `delay_ms` (time for a reply to leave first).
    void start_over(uint32_t now, uint32_t delay_ms = 0);
    // BOOT went down or up.
    void boot(bool down, uint32_t now);
    // Shows the countdown after `ms` of holding BOOT instead (below START_OVER_MS): for a device whose BOOT is also the
    // pet's button, where a shorter hold means something else.
    void set_hold_show(uint32_t ms) { hold_show_ms_ = std::min(ms, START_OVER_MS - 1); }
    void update(uint32_t now);
    // The screen the cards are laid out for; default_display() unless set.
    void set_display(const Display& d) { d_ = &d; }
    // Wi-Fi was configured another way (the developer console): close the access point and stop.
    void end();

    bool active() const { return phase_ != Phase::Off; }     // setup owns the screen
    // render() will draw the whole screen at `now` (setup, or the BOOT countdown): nothing under it shows.
    bool covers(uint32_t now) const { return active() || (holding_ && now - hold_since_ >= hold_show_ms_); }
    bool ap_open() const { return ap_open_; }
    Phase phase() const { return phase_; }

    // Draws the whole screen and returns true while active or while BOOT counts down; otherwise draws the offline
    // mark (if any) and returns false. Records what it drew into `l`.
    bool render(Canvas& c, const Egg* pet, uint32_t now, Layout* l = nullptr) const;
    // The line along the bottom while the pet is off its Wi-Fi; "" when there is nothing to say.
    std::string caption(uint32_t now) const;
    // A tap on the pet closes "All set", or opens setup when the caption offers it; true if it did either.
    bool tap(uint32_t now);
    // Answers the setup page and its API, captive-portal probes and the handover page; true if it answered.
    // `host` is the request's Host header ("" if unknown); `pet` is the pet out (or nullptr), for the page's picture.
    // `setup_network`: the request came over the access point; only those get the setup page and its API.
    bool handle(const std::string& method, const std::string& path_q, const std::string& body,
                const std::string& host, uint32_t now, const Egg* pet, Response& r, bool setup_network = true);

    // For screen tests: shows a phase with made-up facts, without a radio.
    // `again`: setup runs again after the Wi-Fi stopped working.
    void preview(Phase p, const std::string& pet, const std::string& ssid, const std::string& ap_pass,
                 Join why = Join::Failed, const std::string& ip = "", const std::string& host = "", bool again = false);
    // For screen tests: BOOT held for `held_ms` (drawn at `now`).
    void preview_hold(uint32_t held_ms, uint32_t now) { hold_since_ = now - held_ms; holding_ = true; }

private:
    enum class Audience { Screen, Page };
    // Off the saved network: nothing to do, setup offered on the screen, or setup opened from that offer.
    enum class Rescue { None, Offered, Open };

    void open_ap();
    void close_ap();
    void enter(Phase p, uint32_t now) { phase_ = p; since_ = now; }
    void poll_scan();
    void step(uint32_t now, int phones, bool page_alive);
    void follow_link(uint32_t now);
    void follow_rescue(uint32_t now);
    void follow_hold(uint32_t now);
    void finish_check(Join result, const std::string& ip, uint32_t now);
    void close_ap_when_done(uint32_t now, int phones);
    std::string status_json(uint32_t now) const;
    std::string reason(Audience a) const;     // why the last check failed
    const WifiNet* seen(const std::string& ssid) const;
    std::string code(int n);                  // letters and digits without look-alikes
    std::string welcome_url() const;          // the handover page on the home network
    std::string mdns_name() const;            // "name.local", or "" if unknown
    void clock_from(const Fields& f);

    void welcome_visit(const std::string& path_q);
    bool redirect_probe(const std::string& path, const std::string& host, Response& r) const;
    void serve_setup_api(const std::string& method, const std::string& path, const std::string& body, uint32_t now,
                         const Egg* pet, Response& r);
    std::string networks_json() const;
    void connect(const Fields& f, uint32_t now, Response& r);
    void finish(const Fields& f, uint32_t now, Response& r);
    void go_offline(const Fields& f, uint32_t now, Response& r);

    SetupRadio* radio_ = nullptr;
    Phase phase_ = Phase::Off;
    uint32_t since_ = 0;                      // when this phase began
    std::string pet_ = "Nibble";
    std::string ap_ssid_, ap_pass_;
    bool ap_open_ = false, offline_ = false;
    uint32_t close_at_ = 0;                   // 0: no planned closing
    uint32_t phone_gone_ = 0;                 // when the last phone left the access point
    uint32_t page_seen_ = 0;                  // last request from the setup page
    std::vector<WifiNet> nets_;
    bool scanning_ = false;
    std::string want_ssid_, want_pass_;       // the network being checked; the password is cleared once decided
    Join result_ = Join::Trying;
    std::string ip_, token_, tz_, host_;
    uint32_t ok_at_ = 0;
    std::string saved_;                       // the saved network watched while setup is off; "" for none
    Rescue rescue_ = Rescue::None;
    bool again_ = false;                      // setup runs again: the old Wi-Fi stopped working or was forgotten
    bool off_ = false, refused_ = false;      // off the saved network, refused by it
    uint32_t off_since_ = 0, refused_since_ = 0;
    bool holding_ = false, held_saved_ = false;   // BOOT is down; the radio was asked to hold the saved network
    uint32_t hold_since_ = 0, hold_show_ms_ = HOLD_SHOW_MS;
    bool forget_due_ = false;                 // a start-over is due at forget_at_
    uint32_t forget_at_ = 0;
    const Display* d_ = &default_display();
};

const char* setup_page();                     // the phone's setup page (door/setup_page.cpp)

}  // namespace nestbox
