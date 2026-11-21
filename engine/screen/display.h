// Screen profiles. Everything the engine draws takes its size, pixel scale and shape from a Display, and keeps
// text inside the visible area (on a round screen, the circle).
#pragma once
#include <string>
#include <vector>

namespace nestbox {

struct Box {
    int x = 0, y = 0, w = 0, h = 0;

    // The box from its first to its last pixel, both included.
    static Box spanning(int x0, int y0, int x1, int y1) { return {x0, y0, x1 - x0 + 1, y1 - y0 + 1}; }
    int last_x() const { return x + w - 1; }
    int last_y() const { return y + h - 1; }
    bool empty() const { return w <= 0 || h <= 0; }
    // A position between the first and the last pixel, both included.
    bool contains(float px, float py) const { return px >= x && px <= last_x() && py >= y && py <= last_y(); }
    bool overlaps(const Box& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
};

enum class Shape { Rect, Round };

struct Display {
    std::string id;              // "lcd-1.69", "lcd-2", "square-240", "round-240", "amoled-2.41"
    std::string name;            // for people
    int w = 0, h = 0;            // pixels, as the engine draws them
    int scale = 1;               // screen pixels per art pixel, for pets that don't set their own
    Shape shape = Shape::Rect;
    float width_mm = 0;          // physical width of the visible picture (simulator's real-size view)
    bool oled = false;           // each pixel lights itself (AMOLED): what stays still and bright for hours burns in

    bool round() const { return shape == Shape::Round; }
    // The visible columns [x0, x1) on every row from y0 to y1, `margin` pixels in from the edge.
    // False if no column is visible on all of those rows.
    bool span(int y0, int y1, int margin, int& x0, int& x1) const;
    // True if the point, or the whole box, is visible at least `margin` pixels in from the edge.
    bool visible(int x, int y, int margin = 0) const;
    bool visible(const Box& b, int margin = 0) const;
    // The largest centred rectangle that is fully visible (the whole screen, or a round screen's inner square).
    Box safe(int margin = 0) const;
};

// The known screens; the first is the default (the LCD-1.69, 280x240 used sideways).
const std::vector<Display>& displays();
// A screen by id; nullptr if unknown.
const Display* display_named(const std::string& id);
inline const Display& default_display() { return displays().front(); }

// A corner of the screen; the pet's scene says which one its gauges go in.
enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };
// "top-left", "top-right", "bottom-left", "bottom-right".
const char* corner_name(Corner c);
// The corner a name means; false for anything else.
bool corner_named(const std::string& name, Corner& out);

struct Canvas;
// Paints the area outside a round screen black so captures match the device; no-op for rectangular screens.
void mask_outside(Canvas& c, const Display& d);

}  // namespace nestbox
