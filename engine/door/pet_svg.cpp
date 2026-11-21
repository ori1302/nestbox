#include "door/pet_svg.h"
#include <algorithm>
#include <cstdio>
#include "screen/canvas.h"
#include "screen/font.h"

namespace nestbox {

namespace {
const char* const EMPTY_SVG = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"/>";
}  // namespace

// One <rect> per horizontal run of a colour.
std::string pet_svg(const Egg* egg) {
    const Anim* a = egg ? egg->standing_anim() : nullptr;
    if (!a) return EMPTY_SVG;
    FrameCache cache;
    const FrameBox& b = pixels_of(a->box(0, 0), cache);
    auto at = [&](int x, int y) { return b.px[(y - b.y) * b.w + (x - b.x)]; };   // frame coordinates inside the box
    int x0 = a->fw, y0 = a->fh, x1 = -1, y1 = -1;
    for (int y = b.y; y < b.y + b.h; y++)
        for (int x = b.x; x < b.x + b.w; x++)
            if (at(x, y)) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 < 0) return EMPTY_SVG;
    const int w = x1 - x0 + 1, h = y1 - y0 + 1, side = std::max(w, h);
    const int ox = (side - w) / 2, oy = side - h;
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" shape-rendering=\"crispEdges\">",
                  side, side);
    std::string out = buf;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1;) {
            const uint8_t k = at(x, y);
            int run = 1;
            while (x + run <= x1 && at(x + run, y) == k) run++;
            if (k) {
                std::snprintf(buf, sizeof buf, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\" fill=\"#%06x\"/>",
                              x - x0 + ox, y - y0 + oy, run, unsigned(rgb888(egg->palette[k])));
                out += buf;
            }
            x += run;
        }
    return out + "</svg>";
}

std::string text_svg(const std::string& text, int size) {
    if (text.empty() || !drawable(text)) return "";
    const FontSize& f = font_size(size);
    const int w = text_px(text, size), h = f.cap + f.below;
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\" "
                  "shape-rendering=\"crispEdges\" fill=\"currentColor\" aria-hidden=\"true\">",
                  w, h, w, h);
    std::string out = buf;
    int pen = 0;
    for (char c : text) {
        const Glyph* g = glyph_of(f, c == '~' ? ' ' : c);
        if (!g) continue;
        const int row_bytes = (g->w + 7) / 8;
        for (int y = 0; y < g->h; y++) {
            const uint8_t* row = f.bits + g->bits + y * row_bytes;
            auto on = [&](int x) { return (row[x / 8] >> (x % 8)) & 1; };
            for (int x = 0; x < g->w;) {
                int run = 0;
                while (x + run < g->w && on(x + run)) run++;
                if (run) {
                    std::snprintf(buf, sizeof buf, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\"/>",
                                  pen + g->x + x, g->y + y, run);
                    out += buf;
                    x += run;
                } else {
                    x++;
                }
            }
        }
        pen += g->advance;
    }
    return out + "</svg>";
}

}  // namespace nestbox
