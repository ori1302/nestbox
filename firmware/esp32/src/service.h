// The service task (core 0): the USB console, the log, Wi-Fi setup's radio work, and the periodic upkeep below.
// Wi-Fi setup's and new pets' flash writes happen here, never on the frame task.
#pragma once
#include <cstdint>
#include <string>
#include "app.h"

// Runs forever; `app` is the App*.
void service_task(void* app);

// Every half second: Wi-Fi and mDNS upkeep, the clock, the pet's name and address, the USB host's sleep, the
// caption, writing a pet added over the API (or deleting one taken off), saving a newly chosen pet, the board's
// health (its temperature and Wi-Fi signal) and a restart asked for over the API.
class Upkeep {
public:
    static constexpr uint32_t INTERVAL_MS = 500;
    static constexpr uint32_t CLOCK_REFRESH_MS = 60000;
    static constexpr uint32_t SHOW_NAME_MS = 8000;   // after joining Wi-Fi, and after each press of BOOT
    static constexpr uint32_t HEALTH_MS = 5000;
    static constexpr float HOT_C = 70, COOL_C = 65;   // the chip's temperature: frames slow down, and recover

    explicit Upkeep(App& app) : app_(app) {}
    void tick(uint32_t now_ms);

private:
    bool follow_wifi(uint32_t now);
    void sync_clock(uint32_t now);
    void follow_host();
    void report_usb_sleep(uint32_t now);
    void update_caption(uint32_t now, bool online);
    void save_changed_pet();
    void store_pet_change(uint32_t now);
    void check_health(uint32_t now, bool online);
    void restart_if_asked(uint32_t now);

    App& app_;
    uint32_t last_ = 0, online_since_ = 0, clock_at_ = 0, health_at_ = 0;
    bool was_online_ = false, told_asleep_ = false;
    std::string shown_host_;
};
