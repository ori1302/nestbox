#include "screen/display.h"
#include <algorithm>
#include <cmath>
#include "screen/canvas.h"

namespace nestbox {

const std::vector<Display>& displays() {
    static const std::vector<Display> all = {
        {"lcd-1.69", "Waveshare ESP32-S3-LCD-1.69, sideways (280x240)", 280, 240, 2, Shape::Rect, 32.6f},
        {"lcd-2", "Waveshare ESP32-S3-LCD-2, sideways (320x240)", 320, 240, 2, Shape::Rect, 40.8f},
        {"square-240", "A square 1.54-inch screen (240x240)", 240, 240, 2, Shape::Rect, 27.7f},
        {"round-240", "A round 1.28-inch screen (240x240)", 240, 240, 2, Shape::Round, 32.4f},
        // 600x450, each pixel drawn as 2x2 on the panel: 300x225, so a 320x240 scene loses 10 columns a side and
        // 7-8 rows top and bottom, and every pixel stays square and even (1.875x of the whole scene would not).
        {"amoled-2.41", "Waveshare ESP32-S3-Touch-AMOLED-2.41, sideways (600x450 at 2x: 300x225)", 300, 225, 2,
         Shape::Rect, 49.0f, true},
    };
    return all;
}

const Display* display_named(const std::string& id) {
    for (auto& d : displays()) if (d.id == id) return &d;
    return nullptr;
}

bool Display::span(int y0, int y1, int margin, int& x0, int& x1) const {
    if (y0 > y1) std::swap(y0, y1);
    if (!round()) {
        x0 = margin; x1 = w - margin;
        return y0 >= margin && y1 <= h - margin && x1 > x0;
    }
    // the circle's chord on the row of the band farthest from the centre
    float cx = w / 2.0f, cy = h / 2.0f, r = std::min(w, h) / 2.0f - margin;
    float dy = std::max(std::fabs(y0 - cy), std::fabs(y1 - cy));
    if (dy >= r) return false;
    float half = std::sqrt(r * r - dy * dy);
    x0 = int(std::ceil(cx - half)); x1 = int(std::floor(cx + half));
    return x1 > x0;
}

bool Display::visible(int x, int y, int margin) const {
    if (!round()) return x >= margin && y >= margin && x < w - margin && y < h - margin;
    float dx = x + 0.5f - w / 2.0f, dy = y + 0.5f - h / 2.0f, r = std::min(w, h) / 2.0f - margin;
    return dx * dx + dy * dy <= r * r;
}

bool Display::visible(const Box& b, int margin) const {
    return visible(b.x, b.y, margin) && visible(b.x + b.w - 1, b.y, margin) && visible(b.x, b.y + b.h - 1, margin) &&
           visible(b.x + b.w - 1, b.y + b.h - 1, margin);
}

Box Display::safe(int margin) const {
    if (!round()) return {margin, margin, w - 2 * margin, h - 2 * margin};
    int side = int((std::min(w, h) / 2.0f - margin) * std::sqrt(2.0f));
    return {(w - side) / 2, (h - side) / 2, side, side};
}

void mask_outside(Canvas& c, const Display& d) {
    if (!d.round()) return;
    for (int y = 0; y < c.h; y++) {
        int x0, x1;
        if (!d.span(y, y, 0, x0, x1)) { c.rect(0, y, c.w, 1, 0); continue; }
        c.rect(0, y, x0, 1, 0);
        c.rect(x1, y, c.w - x1, 1, 0);
    }
}

namespace {
const char* const CORNER_NAMES[] = {"top-left", "top-right", "bottom-left", "bottom-right"};
}

const char* corner_name(Corner c) { return CORNER_NAMES[int(c)]; }

bool corner_named(const std::string& name, Corner& out) {
    for (int i = 0; i < 4; i++)
        if (name == CORNER_NAMES[i]) {
            out = Corner(i);
            return true;
        }
    return false;
}

}  // namespace nestbox
