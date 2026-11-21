// The ESP32's radio, clock and storage behind Wi-Fi setup (engine/door/setup.h). Every SetupRadio call only queues a
// request or reads a cached result, so the frame loop and the HTTP server, which call it under the pet's lock, never
// wait on Wi-Fi or flash; tick() does the work on the service task.
//
// Passwords are not logged. The radio zeroes its own copies once a request has run; a kept network's password is
// saved in NVS.
#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <variant>
#include <vector>
#include "door/setup.h"

class EspRadio : public nestbox::SetupRadio {
public:
    // Registers for the station's connect and disconnect events; call once Wi-Fi is started.
    void begin();
    // Runs queued requests and polls scans and joins; call every few tens of milliseconds.
    void tick(uint32_t now_ms);

    void scan() override;
    bool scanned(std::vector<nestbox::WifiNet>& out) override;
    void open_ap(const std::string& ssid, const std::string& password, int channel) override;
    void close_ap() override;
    int phones() override;
    void join(const std::string& ssid, const std::string& password, int channel) override;
    nestbox::Join joined(std::string& ip) override;
    void keep(const std::string& ssid, const std::string& password) override;
    nestbox::Link link() override;
    void hold_saved(bool hold) override;
    void forget() override;
    void reset_all() override;
    void set_time(int64_t unix_s, const std::string& posix_tz, int32_t utc_offset_s) override;
    void save(const std::string& key, const std::string& value) override;
    uint32_t random() override;
    std::string id() override;
    std::string host() override;

private:
    struct Scan {};
    struct OpenAp {
        std::string ssid, password;
        int channel;
    };
    struct CloseAp {};
    struct StartJoin {
        std::string ssid, password;
        int channel;
    };
    struct Keep {
        std::string ssid, password;
    };
    struct SetTime {
        int64_t unix_s;
        std::string posix_tz;
        int32_t utc_offset_s;
    };
    struct Save {
        std::string key, value;
    };
    struct Hold {
        bool hold;
    };
    struct Forget {};
    struct ResetAll {};
    using Request = std::variant<Scan, OpenAp, CloseAp, StartJoin, Keep, SetTime, Save, Hold, Forget, ResetAll>;

    void push(Request r);
    void run(const Scan&, uint32_t now);
    void run(const OpenAp& r, uint32_t now);
    void run(const CloseAp&, uint32_t now);
    void run(const StartJoin& r, uint32_t now);
    void run(const Keep& r, uint32_t now);
    void run(const SetTime& r, uint32_t now);
    void run(const Save& r, uint32_t now);
    void run(const Hold& r, uint32_t now);
    void run(const Forget&, uint32_t now);
    void run(const ResetAll&, uint32_t now);
    void poll_scan();
    void poll_join(uint32_t now);
    void finish(nestbox::Join j, const std::string& ip = "");
    static void wipe(std::string& s);
    static void wipe(Request& r);

    std::mutex m_;                       // guards the queue and the results below
    std::deque<Request> todo_;
    bool scan_done_ = false;
    std::vector<nestbox::WifiNet> nets_;
    int phones_ = 0;
    nestbox::Join join_ = nestbox::Join::Trying;
    nestbox::Link link_ = nestbox::Link::Lost;
    std::string ip_, host_;

    // Only tick() uses these.
    bool scanning_ = false, ap_ = false, joining_ = false, retry_pending_ = false;
    int scan_tries_ = 0, attempts_ = 0;
    std::string ssid_, pass_;            // the network being tried
    int channel_ = 0;
    uint32_t retry_at_ = 0, assoc_at_ = 0, join_start_ = 0;
};
