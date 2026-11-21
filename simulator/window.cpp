#include "window.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include "files.h"
#include "host/palette.h"

namespace nestbox {

namespace {
constexpr int BEZEL = 10;                    // dark frame around the screen, in screen pixels
constexpr int LABEL_H = 44;                  // the label under the screen: two lines of size-3 text
constexpr int LABEL_GAP = 4;                 // between the screen and the label
constexpr int LABEL_TEXT_SIZE = 3;
constexpr int LABEL_LINE_Y[2] = {2, 24};
constexpr uint16_t LABEL_MUTED = rgb(0x9a, 0xab, 0x9e);
constexpr SDL_Color BEZEL_COLOR = {0x17, 0x19, 0x1c, 0xff};   // HOST_DARK at full depth
constexpr float UNKNOWN_DISPLAY_ZOOM = 0.583f;  // points per pixel on a 14" MacBook Pro, for displays without a size
constexpr int BYTES_PER_PIXEL = 2;           // RGB565

SDL_HitTestResult drag_anywhere(SDL_Window*, const SDL_Point*, void*) { return SDL_HITTEST_DRAGGABLE; }

// The display the window is on (the main one before there is a window).
CGDirectDisplayID display_of(SDL_Window* win) {
    SDL_Rect r;
    CGDirectDisplayID id = CGMainDisplayID();
    uint32_t n = 0;
    if (win && SDL_GetDisplayBounds(SDL_GetWindowDisplayIndex(win), &r) == 0)
        CGGetDisplaysWithPoint(CGPointMake(r.x + r.w / 2.0, r.y + r.h / 2.0), 1, &id, &n);
    return id;
}

// macOS points per screen pixel that make the picture as wide on `display` as on the device.
float physical_zoom(CGDirectDisplayID display, const Display& device) {
    const CGSize mm = CGDisplayScreenSize(display);
    const CGRect points = CGDisplayBounds(display);
    if (mm.width <= 0) return UNKNOWN_DISPLAY_ZOOM;
    const float points_per_mm = float(points.size.width / mm.width);
    return device.width_mm * points_per_mm / device.w;
}

// Display pixels per point: 2 on a Retina display, 1 on most external monitors.
int backing_scale(CGDirectDisplayID display) {
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    const CGRect points = CGDisplayBounds(display);
    int scale = 1;
    if (mode && points.size.width > 0)
        scale = std::max(1, int(std::lround(double(CGDisplayModeGetPixelWidth(mode)) / points.size.width)));
    CGDisplayModeRelease(mode);
    return scale;
}

const char* size_key(bool builtin) { return builtin ? "builtin=" : "external="; }

// The size chosen for the Mac's own display and for external ones, kept apart: an external monitor is usually
// further away. Until one is chosen: physical size built in, twice that outside.
int saved_size(bool builtin, int count) {
    std::ifstream in(window_size_file());
    const std::string key = size_key(builtin);
    for (std::string line; std::getline(in, line);)
        if (line.compare(0, key.size(), key) == 0)
            return std::clamp(std::atoi(line.c_str() + key.size()), 0, count - 1);
    return builtin ? 0 : 2;
}

void save_size(bool builtin, int size, int count) {
    const int other = saved_size(!builtin, count);
    std::filesystem::create_directories(window_size_file().parent_path());
    std::ofstream(window_size_file()) << size_key(builtin) << size << "\n" << size_key(!builtin) << other << "\n";
}
}  // namespace

SimWindow::~SimWindow() {
    if (label_tex_) SDL_DestroyTexture(label_tex_);
    if (screen_tex_) SDL_DestroyTexture(screen_tex_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    if (sdl_up_) SDL_Quit();
}

bool SimWindow::open() {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return false;
    }
    sdl_up_ = true;
    fit_display(display_of(nullptr));
    window_ = SDL_CreateWindow("NestBox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               int(std::lround(width() * zoom())), int(std::lround(height() * zoom())),
                               SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (renderer_) {
        screen_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, display_.w,
                                        display_.h);
        label_tex_ =
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, display_.w, LABEL_H);
    }
    if (!screen_tex_ || !label_tex_) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowHitTest(window_, drag_anywhere, nullptr);
    SDL_RenderSetLogicalSize(renderer_, width(), height());
    set_smoothing();
    label_ = Canvas(display_.w, LABEL_H);
    return true;
}

void SimWindow::run(const std::function<bool()>& stop) {
    FramePacer pacer;
    bool running = true;
    while (running && !stop()) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (!handle(e)) running = false;
        device_.step();
        draw();
        pacer.wait();
    }
}

void SimWindow::fit_display(CGDirectDisplayID display) {
    on_ = display;
    builtin_ = CGDisplayIsBuiltin(display);
    points_per_pixel_ = physical_zoom(display, display_);
    backing_ = backing_scale(display);
    size_ = saved_size(builtin_, int(SIZES.size()));
}

// Points per device pixel at the chosen size; snapped to whole display pixels per pixel when that is close.
float SimWindow::zoom() const {
    const float z = points_per_pixel_ * SIZES[size_];
    const float whole = std::round(z * backing_);
    return whole >= 1 && std::abs(z * backing_ - whole) <= SNAP * whole ? whole / backing_ : z;
}

// Each device pixel is a whole number of display pixels.
bool SimWindow::sharp() const {
    const float px = zoom() * backing_;
    return std::abs(px - std::round(px)) < 1e-3f;
}

// Whole display pixels per pixel are drawn sharp; any other size is smoothed, so no row or column of pixels is
// dropped or doubled.
void SimWindow::set_smoothing() {
    const SDL_ScaleMode mode = sharp() ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
    SDL_SetTextureScaleMode(screen_tex_, mode);
    SDL_SetTextureScaleMode(label_tex_, mode);
}

void SimWindow::resize() {
    SDL_SetWindowSize(window_, int(std::lround(width() * zoom())), int(std::lround(height() * zoom())));
    set_smoothing();
}

int SimWindow::width() const { return display_.w + 2 * BEZEL; }
int SimWindow::height() const { return display_.h + 2 * BEZEL + (show_label_ ? LABEL_H : 0); }

// False once the window should close.
bool SimWindow::handle(const SDL_Event& e) {
    switch (e.type) {
        case SDL_QUIT:
            return false;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_MOVED && display_of(window_) != on_) {
                fit_display(display_of(window_));
                resize();
            }
            return true;
        case SDL_MOUSEBUTTONDOWN:
            device_.press(e.button.button == SDL_BUTTON_RIGHT ? "long" : e.button.clicks >= 2 ? "double" : "single");
            return true;
        case SDL_KEYDOWN:
            if (e.key.keysym.sym == SDLK_b) {            // BOOT, held as long as the key
                if (!e.key.repeat) device_.boot(true);
                return true;
            }
            return handle_key(e.key.keysym);
        case SDL_KEYUP:
            if (e.key.keysym.sym == SDLK_b) device_.boot(false);
            return true;
        default:
            return true;
    }
}

// Z: next window size (Shift: previous); L: label on or off; Q or Esc: quit; anything else goes to the device.
bool SimWindow::handle_key(const SDL_Keysym& key) {
    const bool shift = key.mod & KMOD_SHIFT;
    switch (key.sym) {
        case SDLK_z: {
            const int count = int(SIZES.size());
            size_ = (size_ + (shift ? count - 1 : 1)) % count;
            save_size(builtin_, size_, count);
            break;
        }
        case SDLK_l:
            show_label_ = !show_label_;
            break;
        case SDLK_q:
        case SDLK_ESCAPE:
            return false;
        default:
            device_.debug_key(key.sym, shift);
            return true;
    }
    resize();
    SDL_RenderSetLogicalSize(renderer_, width(), height());
    return true;
}

void SimWindow::draw() {
    if (show_label_) draw_label();
    SDL_UpdateTexture(screen_tex_, nullptr, device_.screen().px.data(), display_.w * BYTES_PER_PIXEL);
    const Uint8 level = Uint8(std::lround(255 * std::clamp(device_.screen_level(), 0.0f, 1.0f)));
    SDL_SetTextureColorMod(screen_tex_, level, level, level);   // a resting screen, dimmed or dark
    SDL_SetRenderDrawColor(renderer_, BEZEL_COLOR.r, BEZEL_COLOR.g, BEZEL_COLOR.b, BEZEL_COLOR.a);
    SDL_RenderClear(renderer_);
    const Shift moved = device_.shift();   // burn-in care moves the picture a pixel into the bezel
    const SDL_Rect screen{BEZEL + moved.x, BEZEL + moved.y, display_.w, display_.h};
    SDL_RenderCopy(renderer_, screen_tex_, nullptr, &screen);
    if (show_label_) {
        const SDL_Rect from{0, 0, display_.w, LABEL_H - LABEL_GAP};
        const SDL_Rect to{BEZEL, BEZEL + display_.h + LABEL_GAP, display_.w, LABEL_H - LABEL_GAP};
        SDL_RenderCopy(renderer_, label_tex_, &from, &to);
    }
    SDL_RenderPresent(renderer_);
}

void SimWindow::draw_label() {
    const auto [what, why] = device_.label();
    label_.fill(HOST_DARK);
    label_.text(what.c_str(), 0, LABEL_LINE_Y[0], LABEL_TEXT_SIZE, HOST_TEXT);
    label_.text(why.c_str(), 0, LABEL_LINE_Y[1], LABEL_TEXT_SIZE, LABEL_MUTED);
    SDL_UpdateTexture(label_tex_, nullptr, label_.px.data(), display_.w * BYTES_PER_PIXEL);
}

}  // namespace nestbox
