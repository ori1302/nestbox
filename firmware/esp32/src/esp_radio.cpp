#include "esp_radio.h"
#include <DNSServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <mbedtls/platform_util.h>
#include <atomic>
#include <cstdio>
#include <optional>
#include "log.h"
#include "net.h"
#include "settings.h"
#include "wall_clock.h"

using nestbox::Join;
using nestbox::Link;
using nestbox::WifiNet;

namespace {
const IPAddress AP_IP(192, 168, 4, 1);   // nestbox::Setup::AP_IP
const IPAddress AP_NETMASK(255, 255, 255, 0);
constexpr int AP_MAX_PHONES = 4;
constexpr uint16_t DNS_PORT = 53;
constexpr int MAX_SCAN_TRIES = 3;            // a scan fails while the station is busy (mid-join)
constexpr int JOIN_ATTEMPTS = 2;
constexpr uint32_t RETRY_DELAY_MS = 500;
constexpr uint32_t NO_ADDRESS_MS = 10000;    // associated this long without a DHCP lease
constexpr uint32_t JOIN_GIVE_UP_MS = 22000;  // the driver stops at 20 s; stop it hopping channels after that

DNSServer dns;                               // answers every name with the pet, so any address opens the setup page

// Set by the Wi-Fi event task, read by tick().
std::atomic<uint32_t> disconnects{0}, associated_at{0};
std::atomic<uint8_t> disconnect_reason{0};

// Why a try failed, from the station's disconnect reason.
Join classify(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return Join::WrongPassword;
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return Join::NotFound;
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:   // found, but with security the station can't use
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        default:
            return Join::Failed;
    }
}

// How the saved network is doing, from the station's state and its last disconnect reason.
Link saved_link() {
    if (net::online()) return Link::Online;
    switch (classify(disconnect_reason)) {
        case Join::WrongPassword: return Link::Refused;
        case Join::NotFound: return Link::Unseen;
        default: return Link::Lost;
    }
}

bool enterprise(wifi_auth_mode_t m) {
    return m == WIFI_AUTH_WPA2_ENTERPRISE || m == WIFI_AUTH_WPA3_ENTERPRISE || m == WIFI_AUTH_WPA2_WPA3_ENTERPRISE ||
           m == WIFI_AUTH_WPA3_ENT_192 || m == WIFI_AUTH_WPA_ENTERPRISE;
}

// A fixed POSIX rule for an offset from UTC (+5:30 is "<+0530>-5:30"), as Setup builds for zones it doesn't know.
std::string fixed_rule(int32_t off) {
    char buf[24];
    const int a = off < 0 ? -off : off;
    snprintf(buf, sizeof buf, "<%c%02d%02d>%c%d:%02d", off < 0 ? '-' : '+', a / 3600, a / 60 % 60, off < 0 ? '+' : '-',
             a / 3600, a / 60 % 60);
    return buf;
}

const char* describe(Join j, const std::string& ip) {
    switch (j) {
        case Join::Ok: return ip.c_str();
        case Join::WrongPassword: return "wrong password";
        case Join::NotFound: return "not found";
        case Join::NoAddress: return "no address";
        default: return "failed";
    }
}

void begin_station(const std::string& ssid, const std::string& pass, int channel) {
    WiFi.begin(ssid.c_str(), pass.empty() ? nullptr : pass.c_str(), channel > 0 ? channel : 0);
}
}  // namespace

void EspRadio::wipe(std::string& s) {
    if (!s.empty()) mbedtls_platform_zeroize(&s[0], s.size());
    s.clear();
}

void EspRadio::wipe(Request& r) {
    if (auto* ap = std::get_if<OpenAp>(&r)) wipe(ap->password);
    if (auto* join = std::get_if<StartJoin>(&r)) wipe(join->password);
    if (auto* keep = std::get_if<Keep>(&r)) wipe(keep->password);
}

void EspRadio::begin() {
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        disconnect_reason = info.wifi_sta_disconnected.reason;
        disconnects++;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    // `| 1`: zero means "not associated".
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { associated_at = millis() | 1; },
                 ARDUINO_EVENT_WIFI_STA_CONNECTED);
}

// SetupRadio calls, from the frame loop or the HTTP task under the pet's lock: queue or read, never wait.

void EspRadio::push(Request r) {
    std::lock_guard<std::mutex> lock(m_);
    todo_.push_back(std::move(r));
}

void EspRadio::scan() {
    {
        std::lock_guard<std::mutex> lock(m_);
        scan_done_ = false;
    }
    push(Scan{});
}

bool EspRadio::scanned(std::vector<WifiNet>& out) {
    std::lock_guard<std::mutex> lock(m_);
    if (!scan_done_) return false;
    scan_done_ = false;
    out = std::move(nets_);
    nets_.clear();
    return true;
}

void EspRadio::open_ap(const std::string& ssid, const std::string& password, int channel) {
    push(OpenAp{ssid, password, channel});
}

void EspRadio::close_ap() { push(CloseAp{}); }

int EspRadio::phones() {
    std::lock_guard<std::mutex> lock(m_);
    return phones_;
}

void EspRadio::join(const std::string& ssid, const std::string& password, int channel) {
    {
        std::lock_guard<std::mutex> lock(m_);
        join_ = Join::Trying;
        ip_.clear();
    }
    push(StartJoin{ssid, password, channel});
}

Join EspRadio::joined(std::string& ip) {
    std::lock_guard<std::mutex> lock(m_);
    if (join_ == Join::Ok) ip = ip_;
    return join_;
}

void EspRadio::keep(const std::string& ssid, const std::string& password) {
    push(Keep{ssid, password});
}

Link EspRadio::link() {
    std::lock_guard<std::mutex> lock(m_);
    return link_;
}

void EspRadio::hold_saved(bool hold) { push(Hold{hold}); }
void EspRadio::forget() { push(Forget{}); }

void EspRadio::reset_all() { push(ResetAll{}); }

void EspRadio::set_time(int64_t unix_s, const std::string& posix_tz, int32_t utc_offset_s) {
    push(SetTime{unix_s, posix_tz, utc_offset_s});
}

void EspRadio::save(const std::string& key, const std::string& value) { push(Save{key, value}); }
uint32_t EspRadio::random() { return esp_random(); }
std::string EspRadio::id() { return net::device_id(); }

std::string EspRadio::host() {
    std::lock_guard<std::mutex> lock(m_);
    return host_;
}

// The work, on the service task.

void EspRadio::tick(uint32_t now) {
    for (;;) {
        std::optional<Request> r;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (todo_.empty()) break;
            r = std::move(todo_.front());
            todo_.pop_front();
        }
        std::visit([&](const auto& request) { run(request, now); }, *r);
        wipe(*r);
    }
    poll_scan();
    poll_join(now);
    const std::string host = net::host();
    const int phones = ap_ ? WiFi.softAPgetStationNum() : 0;
    const Link link = saved_link();
    std::lock_guard<std::mutex> lock(m_);
    phones_ = phones;
    host_ = host;
    link_ = link;
}

void EspRadio::run(const Scan&, uint32_t) {
    scan_tries_ = 0;
    scanning_ = WiFi.scanNetworks(true) != WIFI_SCAN_FAILED;   // async; poll_scan() collects it
    if (!scanning_) scan_tries_ = 1;
}

void EspRadio::run(const OpenAp& r, uint32_t) {
    WiFi.mode(WIFI_AP_STA);                  // the pet's own network next to its station
    WiFi.softAPConfig(AP_IP, AP_IP, AP_NETMASK);
    ap_ = WiFi.softAP(r.ssid.c_str(), r.password.c_str(), r.channel, 0, AP_MAX_PHONES);
    dns.start(DNS_PORT, "*", AP_IP);
    note("setup: own network %s %s on channel %d\n", r.ssid.c_str(), ap_ ? "open" : "FAILED to open", r.channel);
}

void EspRadio::run(const CloseAp&, uint32_t) {
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    ap_ = false;
    note("setup: own network closed\n");
}

void EspRadio::run(const StartJoin& r, uint32_t now) {
    ssid_ = r.ssid;
    pass_ = r.password;
    channel_ = r.channel;
    attempts_ = 1;
    joining_ = true;
    retry_pending_ = false;
    assoc_at_ = 0;
    join_start_ = now;
    associated_at = 0;
    disconnects = 0;
    WiFi.setAutoReconnect(false);            // this try decides; a wrong password must not be retried forever
    begin_station(ssid_, pass_, channel_);
    note("setup: trying %s\n", ssid_.c_str());
}

void EspRadio::run(const Keep& r, uint32_t) {
    settings::put(settings::SSID, r.ssid);
    settings::put(settings::PASSWORD, r.password);
    settings::remove(settings::OFFLINE);
    net::adopt(r.ssid, r.password);
    note("setup: saved %s\n", r.ssid.c_str());
}

// Sets the clock and the phone's zone; a rule newlib can't parse is replaced by the phone's fixed offset.
void EspRadio::run(const SetTime& r, uint32_t) {
    wall_clock::set_clock(r.unix_s);
    std::string rule = r.posix_tz;
    wall_clock::set_tz(rule);
    int64_t local;
    if (!wall_clock::local(local) || int32_t(local - r.unix_s) != r.utc_offset_s) {
        rule = fixed_rule(r.utc_offset_s);
        wall_clock::set_tz(rule);
    }
    settings::put(settings::TIME_ZONE, rule);
    note("setup: time set, zone %s\n", rule.c_str());
}

void EspRadio::run(const Save& r, uint32_t) {
    if (r.key == nestbox::SAVE_NAME) {
        settings::put(settings::PET_NAME, r.value);
        net::announce(r.value);                            // the device's name from now on, on the network too
    }
    else if (r.key == nestbox::SAVE_OFFLINE) settings::put(settings::OFFLINE, r.value);
    note("setup: %s saved\n", r.key.c_str());
}

void EspRadio::run(const Hold& r, uint32_t now) { net::hold(r.hold, now); }

void EspRadio::run(const Forget&, uint32_t) {
    settings::remove(settings::SSID);
    settings::remove(settings::PASSWORD);
    settings::remove(settings::OFFLINE);
    net::join("", "");
    note("setup: Wi-Fi forgotten\n");
}

// Every setting but the pets and which one was out, the screen's rest included; the device then starts as new.
void EspRadio::run(const ResetAll&, uint32_t) {
    for (const char* key : {settings::SSID, settings::PASSWORD, settings::OFFLINE, settings::PET_NAME,
                            settings::TIME_ZONE, settings::HOST, settings::FLIP, settings::BRIGHTNESS,
                            settings::API_KEY, settings::LOCKED, settings::TEXT_SIZE, settings::DIM,
                            settings::DIM_AFTER, settings::OFF_AFTER, settings::NIGHT_FROM, settings::NIGHT_TO,
                            settings::CARE})
        settings::remove(key);
    note("setup: everything reset but the pets, restarting\n");
    delay(100);
    ESP.restart();
}

void EspRadio::poll_scan() {
    const bool retrying = scan_tries_ > 0 && scan_tries_ < MAX_SCAN_TRIES;
    if (!scanning_ && !retrying) return;
    int n = scanning_ ? WiFi.scanComplete() : WIFI_SCAN_FAILED;
    if (n == WIFI_SCAN_RUNNING) return;
    if (n == WIFI_SCAN_FAILED) {
        if (retrying) {
            scan_tries_++;
            scanning_ = WiFi.scanNetworks(true) != WIFI_SCAN_FAILED;
            return;
        }
        n = 0;                               // give up with an empty list
    }
    std::vector<WifiNet> got;
    for (int i = 0; i < n; i++) {
        WifiNet w;
        w.ssid = WiFi.SSID(i).c_str();
        w.rssi = WiFi.RSSI(i);
        w.channel = WiFi.channel(i);
        const wifi_auth_mode_t m = WiFi.encryptionType(i);
        w.secure = m != WIFI_AUTH_OPEN;
        w.enterprise = enterprise(m);
        got.push_back(w);
    }
    WiFi.scanDelete();
    scanning_ = false;
    scan_tries_ = 0;
    std::lock_guard<std::mutex> lock(m_);
    nets_ = std::move(got);
    scan_done_ = true;
}

void EspRadio::finish(Join j, const std::string& ip) {
    joining_ = false;
    retry_pending_ = false;
    if (j != Join::Ok) WiFi.disconnect(false);   // the station only; the pet's own network stays up
    wipe(pass_);
    note("setup: %s -> %s\n", ssid_.c_str(), describe(j, ip));
    std::lock_guard<std::mutex> lock(m_);
    join_ = j;
    ip_ = ip;
}

void EspRadio::poll_join(uint32_t now) {
    if (!joining_) return;
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        finish(Join::Ok, WiFi.localIP().toString().c_str());
        return;
    }
    const uint32_t assoc = associated_at;
    if (assoc && !assoc_at_) assoc_at_ = assoc;
    if (assoc_at_ && now - assoc_at_ > NO_ADDRESS_MS) {
        finish(Join::NoAddress);
        return;
    }
    if (now - join_start_ > JOIN_GIVE_UP_MS) {
        finish(Join::Failed);
        return;
    }
    if (retry_pending_ && int32_t(now - retry_at_) >= 0) {
        retry_pending_ = false;
        attempts_++;
        assoc_at_ = 0;
        associated_at = 0;
        begin_station(ssid_, pass_, channel_);
        return;
    }
    if (!disconnects) return;
    disconnects = 0;
    const uint8_t reason = disconnect_reason;
    if (reason == WIFI_REASON_ASSOC_LEAVE || retry_pending_) return;   // the station's own disconnect before a retry
    const Join why = classify(reason);
    if (attempts_ < JOIN_ATTEMPTS) {         // once more, on every channel if the network wasn't found
        if (why == Join::NotFound) channel_ = 0;
        retry_at_ = now + RETRY_DELAY_MS;
        retry_pending_ = true;
        assoc_at_ = 0;
        return;
    }
    finish(why);
}
