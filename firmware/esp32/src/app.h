// State shared by the device's tasks. Built in setup() once PSRAM is up, so the canvases (134 KB each) and the eggs
// land there.
#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include "board.h"
#include "door/api.h"
#include "door/setup.h"
#include "esp_radio.h"
#include "settings.h"

// Draw times: the frame task adds, the console reads and resets.
struct FrameStats {
    std::atomic<uint32_t> frames{0}, max_us{0};
    // wait_us: waiting for the lock; send_us: waiting for the panel to finish taking the frame before.
    std::atomic<uint64_t> total_us{0}, wait_us{0}, send_us{0};

    void add(uint32_t us, uint32_t waited_us, uint32_t sent_us) {
        frames++;
        total_us += us;
        wait_us += waited_us;
        send_us += sent_us;
        uint32_t worst = max_us;
        while (us > worst && !max_us.compare_exchange_weak(worst, us)) {
        }
    }
};

// The engine's objects and the values that go with them; reached only through App::with_lock().
struct PetCore {
    const nestbox::Display& display = *nestbox::display_named(board::SCREEN_PROFILE);
    nestbox::Brain brain;
    nestbox::Pet pet;
    nestbox::Shelf shelf;
    nestbox::Canvas screen{display.w, display.h};
    nestbox::Setup setup;
    nestbox::Device dev{brain, pet, shelf, screen,
                        "nestbox",                // device_name; the service task follows the real host name
                        "http://nestbox.local",   // base_url
                        "",                       // id
                        &setup};
    std::string caption;                          // the bottom line while the brain is silent (the address)
    std::string changed_pet;                      // id of a newly chosen pet, for the service task to save
};

// The device's name: the one given in setup (its Bonjour name, its own network's and its welcome page's), else the
// pet out.
inline std::string setup_name(PetCore& c) {
    const std::string name = settings::get(settings::PET_NAME);
    return name.empty() ? c.shelf.current_egg().name : name;
}

class App {
public:
    EspRadio radio;
    std::atomic<bool> hot{false};                 // the chip runs hot: the frame task shows fewer frames, dimmer
    std::atomic<bool> flip{false};                // picture upside down; the frame task applies it to the panel
    std::atomic<uint32_t> show_name_until{0};     // BOOT was pressed: show the address until then
    FrameStats stats;

    // Runs f(PetCore&) under the lock and returns its result.
    template <typename F>
    auto with_lock(F&& f) {
        std::lock_guard<std::mutex> lock(mu_);
        return f(core_);
    }

    // Calls the API under the lock. `local`: the device's own input (button, motion sensor, USB console).
    nestbox::Response request(const std::string& method, const std::string& path, const std::string& body,
                              bool local, const std::string& host = "", const std::string& origin = "",
                              const std::string& auth = "", const std::string& peer = "",
                              bool setup_network = false);
    // Posts input from the device's own hardware (POST /v1/input) and logs the reply.
    void post_input(const std::string& json);
    // Gives the brain the local time from the device's own clock, if it knows it.
    void update_clock();
    bool has_pets();

private:
    std::mutex mu_;
    PetCore core_;
};
