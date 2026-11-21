// The simulated device: the engine's brain, pet and Wi-Fi setup behind one lock, driven by the frame loop, the HTTP
// server and the window's debug controls.
#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "door/api.h"
#include "door/setup.h"
#include "host/frame.h"
#include "http_server.h"
#include "options.h"
#include "sim_radio.h"

namespace nestbox {

// Milliseconds since start, optionally sped up for tests.
class Clock {
public:
    explicit Clock(double speed) : speed_(speed) {}
    uint32_t now() const {
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start_;
        return uint32_t(elapsed.count() * speed_);
    }

private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    double speed_;
};

// Sleeps until the next frame is due; a late frame moves the schedule rather than rushing to catch up.
class FramePacer {
public:
    void wait();

private:
    std::chrono::steady_clock::time_point next_ = std::chrono::steady_clock::now();
};

class SimDevice {
public:
    SimDevice(const Options& o, Shelf& shelf, std::vector<std::filesystem::path> egg_dirs);

    const Display& display() const { return display_; }
    const std::string& id() const { return dev_.id; }
    std::string pet_name();
    // How others reach the API, once the server knows its port.
    void set_base_url(const std::string& url);
    // Called with the new pet's name after the current pet changes, under the device's lock.
    void on_pet_change(std::function<void(const std::string&)> f) { pet_changed_ = std::move(f); }

    Response serve(const HttpRequest& q);
    // Advances and draws one frame.
    void step();
    // A press of the device's button: "single", "double" or "long".
    void press(const std::string& kind);
    // BOOT went down or up.
    void boot(bool down);
    // A debug key (SDL keycode, which is ASCII for letters, digits and Tab); false if it is not one.
    bool debug_key(int key, bool shift);
    // The screen as of the last step(); read it on the thread that calls step().
    const Canvas& screen() const { return screen_; }
    // How bright the screen shows next to awake, 0 to 1: dimmed or off while it rests (door/rest.h).
    float screen_level();
    // Where the picture shows, moved by burn-in care (door/rest.h).
    Shift shift();
    // The window's label: the pet and its look, and why.
    std::pair<std::string, std::string> label();

private:
    void load_eggs();
    void start_as_new(uint32_t now);
    void store_pet_change(uint32_t now);
    void save_choice(const std::string& id);
    void step_named_look(bool back);
    void toggle_music(uint32_t now);
    void cycle_daypart();
    void cycle_router();

    const Display& display_;
    Shelf& shelf_;
    std::vector<std::filesystem::path> egg_dirs_;
    Clock clock_;
    std::mutex mu_;
    Brain brain_;
    Pet pet_;
    Canvas screen_;
    Setup setup_;
    Device dev_;
    SimRadio radio_;
    LookLog log_;
    std::function<void(const std::string&)> pet_changed_;
    std::optional<Daypart> held_daypart_;
    Look named_look_ = Look::Watch;           // the S key's place in the list of looks
    bool music_ = false;                      // the M key's music is on
};

}  // namespace nestbox
