// The simulator's window on macOS: the device's screen in a bezel, always on top, draggable anywhere, at a
// multiple of the device's physical size on whichever display it is on. Mouse clicks are the device's button.
#pragma once
#include <SDL.h>
#include <ApplicationServices/ApplicationServices.h>
#include <array>
#include <functional>
#include "screen/canvas.h"
#include "sim_device.h"

namespace nestbox {

class SimWindow {
public:
    explicit SimWindow(SimDevice& device) : device_(device), display_(device.display()) {}
    ~SimWindow();
    SimWindow(const SimWindow&) = delete;
    SimWindow& operator=(const SimWindow&) = delete;

    // Opens the window; false (after printing why) if SDL cannot.
    bool open();
    // Runs the device and the window until it is closed, Q or Esc is pressed, or `stop` returns true.
    void run(const std::function<bool()>& stop);

private:
    static constexpr std::array<float, 5> SIZES = {1.0f, 1.5f, 2.0f, 2.5f, 3.0f};   // times the physical size
    static constexpr float SNAP = 0.05f;          // a size this close to whole display pixels per pixel snaps to them

    void fit_display(CGDirectDisplayID display);
    void resize();
    float zoom() const;
    bool sharp() const;
    void set_smoothing();
    bool handle(const SDL_Event& e);
    bool handle_key(const SDL_Keysym& key);
    void draw();
    void draw_label();
    int width() const;
    int height() const;

    SimDevice& device_;
    const Display& display_;
    bool sdl_up_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* screen_tex_ = nullptr;
    SDL_Texture* label_tex_ = nullptr;
    Canvas label_{0, 0};
    CGDirectDisplayID on_ = 0;                    // the display the window is on
    bool builtin_ = true;                         // ...and whether it is the Mac's own
    float points_per_pixel_ = 1;                  // at the device's physical size, on that display
    int backing_ = 1;                             // display pixels per point there (2 on a Retina display)
    int size_ = 0;                                // index into SIZES
    bool show_label_ = true;
};

}  // namespace nestbox
