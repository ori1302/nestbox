// An RGB565 framebuffer, the same format as the device's LCD, and the few drawing operations the pet needs.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "screen/frames.h"

namespace nestbox {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

// Colours shared by the signs, the lines and gauges senders colour, and the screen's ink.
// An RGB565 colour's index among the 4096 RGB444 colours (its top four bits of red, green and blue).
constexpr size_t rgb444(uint16_t c) {
    return size_t((c >> 12) & 15) << 8 | size_t((c >> 7) & 15) << 4 | ((c >> 1) & 15);
}
constexpr uint16_t RED = rgb(0xe5, 0x48, 0x4d), YELLOW = rgb(0xf2, 0xc2, 0x30), GREEN = rgb(0x46, 0xa7, 0x58);
constexpr uint16_t BLUE = rgb(0x3e, 0x9b, 0xf0), PURPLE = rgb(0x9b, 0x6c, 0xe0), CREAM = rgb(0xf4, 0xf1, 0xd0);
// RGB565 widened to 8 bits per channel (low bits zero), packed as 0xRRGGBB.
constexpr uint32_t rgb888(uint16_t c) {
    return uint32_t((c >> 11) << 3) << 16 | uint32_t(((c >> 5) & 63) << 2) << 8 | uint32_t((c & 31) << 3);
}
// RGB565 blend in 1/32 steps, k = 0 (a) .. 32 (b).
inline uint16_t blend(uint16_t a, uint16_t b, int k) {
    const int j = 32 - k;
    const int r = (((a >> 11) & 31) * j + ((b >> 11) & 31) * k) >> 5;
    const int g = (((a >> 5) & 63) * j + ((b >> 5) & 63) * k) >> 5;
    const int bl = ((a & 31) * j + (b & 31) * k) >> 5;
    return uint16_t((r << 11) | (g << 5) | bl);
}
// RGB565 blend, t = 0 (a) .. 1 (b), rounded to 1/32 steps.
inline uint16_t mix(uint16_t a, uint16_t b, float t) { return blend(a, b, int(t * 32 + 0.5f)); }
// Half brightness.
inline uint16_t dim(uint16_t p) { return uint16_t((p >> 1) & 0x7BEF); }

struct Canvas {
    int w, h;
    std::vector<uint16_t> px;
    // What px held when it was last sent, as fingerprints of its slices (h * SLICES, screen/slices.h), filled by a
    // device that fingerprints the frames it sends; empty when nobody does. Stale once anything draws on px.
    std::vector<uint32_t> held;
    // A pet's screen palette (Egg::screen_lut) while it is drawn: rect, sprite, glyph and text snap every colour they
    // write to it, so the engine's own overlays come out in the pet's few colours. Null: colours as given.
    const uint16_t* tint = nullptr;
    Canvas(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * h_, 0) {}
    uint16_t tinted(uint16_t c) const { return tint ? tint[rgb444(c)] : c; }

    void fill(uint16_t c) { std::fill(px.begin(), px.end(), c); }
    void rect(int x, int y, int rw, int rh, uint16_t c);
    // One frame scaled by `scale`, the top-left of its whole frame at (x, y).
    void sprite(const FrameBox& b, const uint16_t* palette, int x, int y, int scale);
    // A small bitmap given as rows of '1' and '.', with a one-pixel outline.
    void glyph(const char* const* rows, int nrows, int x, int y, int scale, uint16_t color, uint16_t outline);
    // Text in the pet's font (screen/font.h) at size 1, 2 or 3, the top of its capitals at y.
    void text(const char* s, int x, int y, int size, uint16_t color);
    void text(const std::string& s, int x, int y, int size, uint16_t color) { text(s.c_str(), x, y, size, color); }
    // Every pixel through a 4096-entry RGB444 table (Egg::screen_lut): a pet's few screen colours, say.
    void recolour(const std::vector<uint16_t>& lut);
};

}  // namespace nestbox
