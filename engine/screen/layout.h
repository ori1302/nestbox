// Layout for full-screen cards: text that wraps and shrinks to fit its box, and QR codes. Everything drawn can be
// recorded in a Layout so tests can check that nothing overlaps, leaves the visible area or gets cut.
#pragma once
#include <string>
#include <vector>
#include "screen/canvas.h"
#include "screen/display.h"
#include "screen/font.h"
#include "util/qr.h"

namespace nestbox {

struct Placed {
    std::string what;            // e.g. "text: Phone joined!", "qr", "pet"
    Box box;                     // the ink actually drawn
    bool fits = true;            // false: the text had to be cut or broken mid-word
};

struct Layout {
    std::vector<Placed> items;
    void add(const std::string& what, Box b, bool fits = true) { items.push_back({what, b, fits}); }
};

enum class Align { Left, Center };

// Lines of `text` no wider than `width` pixels at `size` (screen/font.h), words whole where they can be.
// `fits` (if given) says whether every word did fit whole; `even` makes the lines about the same length.
std::vector<std::string> wrap_lines(const std::string& text, int width, int size, bool* fits = nullptr,
                                    bool even = false);

// Draws `text` inside `area` at the largest size from `max_size` down to `min_size` where every word fits
// whole (breaking a long one after "-" or before ".local" if it must), top-aligned; centred lines come out
// even. A "~" is a space that never breaks ("2.4~GHz"). Returns the box it used (h = 0 if empty).
Box draw_text(Canvas& c, Layout* l, const Box& area, const std::string& text, uint16_t color,
              int max_size = 3, int min_size = 2, Align align = Align::Left);
// True if draw_text would fit it whole: no word broken mid-word, nothing cut.
bool text_fits(const Box& area, const std::string& text, int max_size = 3, int min_size = 2);
// How tall draw_text would make it, without drawing.
int text_height(const Box& area, const std::string& text, int max_size = 3, int min_size = 2);

// A QR code: dark modules on white with a `quiet`-module border, `module` pixels each. Returns its box.
Box draw_qr(Canvas& c, Layout* l, const Qr& q, int x, int y, int module, int quiet = 2);
inline int qr_box(const Qr& q, int module, int quiet = 2) { return (q.size + 2 * quiet) * module; }

}  // namespace nestbox
