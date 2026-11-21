#include "screen/hud.h"
#include <algorithm>
#include <cstdlib>
#include <climits>
#include <cmath>
#include "screen/font.h"
#include "screen/layout.h"
#include "util/math.h"
#include "util/text.h"

namespace nestbox {

namespace {
constexpr uint16_t INK = rgb(0x1d, 0x24, 0x33);      // bubble and bar background, glyph outlines
constexpr uint16_t RIM = rgb(0x4a, 0x55, 0x6a);
constexpr uint16_t PINK = rgb(0xe8, 0x50, 0x6a);

const char* const Z_GLYPH[] = {"11111", "...1.", "..1..", ".1...", "11111"};
const char* const HEART[] = {".11.11.", "1111111", "1111111", ".11111.", "..111..", "...1..."};
const char* const BANG[] = {"11", "11", "11", "11", "..", "11"};
const char* const DOT[] = {"1"};
const char* const NOTE[] = {"..11", "..1.", "..1.", "111.", "11.."};
const char* const QUESTION[] = {"111", "..1", ".11", "...", ".1."};
const char* const ANGRY[] = {"1.1.1", ".1.1.", "11.11", ".1.1.", "1.1.1"};   // the popping-vein mark
const char* const SHOCK[] = {"1...1", ".1.1.", ".....", ".1.1.", "1...1"};
const char* const CROSS[] = {"11..11", "111111", ".1111.", ".1111.", "111111", "11..11"};
const char* const STAR[] = {"...1...", "..111..", "1111111", ".11111.", ".11.11.", "11...11"};
const char* const CHECK[] = {".....11", "....111", "11.111.", "11111..", ".111...", "..1...."};
const char* const SPARKLE[] = {".1.", "111", ".1."};

// Status bar: lines that fit stay TURN_MS; a long one pauses, scrolls MS_PER_PX per pixel, then pauses again.
constexpr uint32_t TURN_MS = 4000, SCROLL_PAUSE_MS = 700, SCROLL_END_MS = 400, MS_PER_PX = 12;
constexpr int SCROLL_GAP = 40;             // between a scrolling line and its repeat
constexpr int BAR_PAD = 6, TEXT_LEFT = 12, TEXT_TOP = 4, DOT_PITCH = 5;

// Meter rings, by size 1-3: outer radius and thickness in px, and the largest font size for the label.
struct RingStyle {
    int radius;
    float thick;
    int font;
};
constexpr RingStyle RING_STYLES[] = {{15, 4.2f, 1}, {20, 5.0f, 2}, {26, 6.0f, 3}};
constexpr float RING_OUTLINE_PX = 1.2f;    // a dark outline around a ring, so it reads on any scene
constexpr int RING_REACH = 1;              // whole px the outline adds to the radius
constexpr int RING_MARGIN = 5;             // px from the visible edge and from the status bar
constexpr int RING_GAP = 6;                // px between rings
constexpr uint16_t RING_TRACK = rgb(0x3a, 0x44, 0x57);
constexpr uint16_t RING_OUTLINE = rgb(0x12, 0x16, 0x20);

// Speech: at most this many lines, and bubbles over the pet no wider than these (size 1, sizes 2-3).
constexpr size_t SPEECH_LINES = 3;
constexpr int BUBBLE_MAX_W_SMALL = 150, BUBBLE_MAX_W = 192;
constexpr int BUBBLE_GAP = 6;        // px between a bubble and what the pet reaches: room for the tail
constexpr int BUBBLE_DROP = 8;       // px a bubble beside the head reaches below the top of the head
constexpr int BUBBLE_MIN_W = 56;     // px of room a bubble needs beside the head, else it goes above
constexpr int BUBBLE_HOLD_PX = 4;    // a bubble stays put until the pet's feet move further than this
}  // namespace

void draw_bubble(Canvas& c, Bubble b, int hx, int hy, int unit, uint32_t now_ms) {
    const int u = unit, x = hx + 2 * u, y = hy - 4 * u;
    auto bob = [&](uint32_t period_ms) { return int((now_ms / period_ms) % 2) * u; };
    switch (b) {
        case Bubble::Zzz: {
            int n = std::min(int((now_ms / 700) % 4), 3);
            for (int k = 0; k < n; k++) c.glyph(Z_GLYPH, 5, x + k * 5 * u, y - k * 6 * u, u, CREAM, INK);
            break;
        }
        case Bubble::Heart:
            if ((now_ms / 500) % 2 == 0) c.glyph(HEART, 6, x, y - 2 * u, u, PINK, INK);
            break;
        case Bubble::Dots: {
            int n = int((now_ms / 400) % 4);
            for (int k = 0; k < n; k++) c.glyph(DOT, 1, x + k * 3 * u, y - 2 * u, u, CREAM, INK);
            break;
        }
        case Bubble::Note:
            if ((now_ms / 600) % 2 == 0) c.glyph(NOTE, 5, x, y - 2 * u, u, CREAM, INK);
            else c.glyph(NOTE, 5, x + 4 * u, y - 5 * u, u, CREAM, INK);
            break;
        case Bubble::Question: c.glyph(QUESTION, 5, x, y - 3 * u - bob(400), u, CREAM, INK); break;
        case Bubble::Angry:
            if ((now_ms / 300) % 3) c.glyph(ANGRY, 5, x, y - 2 * u, u, RED, INK);
            break;
        case Bubble::Shock: c.glyph(SHOCK, 5, x - u, y - 3 * u - bob(150), u, YELLOW, INK); break;
        case Bubble::Cross:
            if ((now_ms / 400) % 3) c.glyph(CROSS, 6, x, y - 3 * u, u, RED, INK);
            break;
        case Bubble::Bang: c.glyph(BANG, 6, x, y - 4 * u - bob(250), u, YELLOW, INK); break;
        case Bubble::Star: c.glyph(STAR, 6, x, y - 3 * u - bob(500), u, YELLOW, INK); break;
        case Bubble::Check: c.glyph(CHECK, 6, x, y - 3 * u, u, GREEN, INK); break;
        case Bubble::None: break;
    }
}

// Six sparkles on fixed spots around the pet, each twinkling on its own beat.
void draw_sparkles(Canvas& c, int cx, int cy, int r, int unit, uint32_t now_ms) {
    static constexpr int SPOTS[][2] = {{-9, -7}, {8, -9}, {11, 2}, {-11, 4}, {-3, -12}, {5, 9}};
    for (int k = 0; k < 6; k++) {
        if ((now_ms / 180 + k * 3) % 7 > 3) continue;
        const int x = cx + SPOTS[k][0] * r / 10, y = cy + SPOTS[k][1] * r / 10;
        c.glyph(SPARKLE, 3, x - unit, y - unit, unit, k % 2 ? CREAM : YELLOW, INK);
    }
}

// Confetti: bits of colour falling and drifting across the whole screen, on a loop.
void draw_confetti(Canvas& c, const Display& d, uint32_t now_ms) {
    static constexpr uint16_t COLOURS[] = {PINK, YELLOW, GREEN, BLUE, PURPLE, CREAM};
    for (int k = 0; k < 28; k++) {
        const uint32_t seed = uint32_t(k) * 2654435761u;
        const int x0 = int(seed % uint32_t(d.w));
        const uint32_t fall_ms = 2200 + (seed >> 8) % 1600;
        const uint32_t t = (now_ms + (seed >> 4) % fall_ms) % fall_ms;
        const int y = int(int64_t(t) * (d.h + 8) / fall_ms) - 4;
        const int x = x0 + int(((t / 250 + uint32_t(k)) % 4)) - 2;
        const int w = 2 + int((seed >> 12) % 2), h = 2 + int((seed >> 13) % 2);
        c.rect(x, y, w, h, COLOURS[k % 6]);
    }
}

void draw_note(Canvas& c, int x, int y, int unit) { c.glyph(NOTE, 5, x, y, unit, CREAM, INK); }

namespace {

// A status-bar row: where its band starts and the visible columns it spans.
struct Band {
    int top, left, right;
};

Band band_at(const Display& d, int top) {
    Band b{top, 0, d.w};
    if (d.round()) d.span(top, top + StatusBar::height(), 0, b.left, b.right);
    return b;
}

// The width a row's text may use when `reserve` px on the right are kept free.
int text_room(const Band& b, int reserve) { return b.right - (b.left + TEXT_LEFT) - 4 - reserve; }

// How long a line `width` px wide is shown: TURN_MS if it fits in `room`, else a pause, its scroll and a pause.
uint32_t line_ms(int width, int room) {
    return width <= room ? TURN_MS : SCROLL_PAUSE_MS + uint32_t(width + SCROLL_GAP) * MS_PER_PX + SCROLL_END_MS;
}

enum class RimSide { Below, Above };

// One row: the band with a RIM line on the scene's side, the sender's square and the text in `color`, `shown_ms`
// into its scroll. `reserve` px on the right stay clear.
void draw_row(Canvas& c, const Display& d, const Band& b, const std::string& text, uint16_t color, uint32_t shown_ms,
              int reserve, RimSide rim) {
    const int top = b.top, left = b.left, right = b.right, h = StatusBar::height();
    const int x0 = left + TEXT_LEFT, width = text_px(text, 3), ty = top + TEXT_TOP;
    c.rect(left, top, right - left, h, INK);
    c.rect(left, rim == RimSide::Below ? top + h : top - 1, right - left, 1, RIM);
    if (width <= text_room(b, reserve)) {
        c.text(text, x0, ty, 3, color);
    } else {
        int off = shown_ms < SCROLL_PAUSE_MS
                      ? 0
                      : std::min(width + SCROLL_GAP, int(shown_ms - SCROLL_PAUSE_MS) / int(MS_PER_PX));
        c.text(text, x0 - off, ty, 3, color);
        c.text(text, x0 - off + width + SCROLL_GAP, ty, 3, color);
        c.rect(right - 4 - reserve, top, 4 + reserve, h, INK);    // clear the reserved corner
        if (left > 0) c.rect(0, top, left, h, 0);                 // and outside a round screen's edge
        if (right < d.w) c.rect(right, top, d.w - right, h, 0);
    }
    c.rect(left, top, x0 - 2 - left, h, INK);
    c.rect(left + 4, top + h / 2 - 1, 3, 3, color);               // the sender's colour
}

}  // namespace

int StatusBar::top(const Display& d) { return d.round() ? std::max(0, d.safe().y - 6) : 0; }
int StatusBar::height() { return font_size(3).cap + font_size(3).below + BAR_PAD; }
Box StatusBar::covers(const Display& d, const Lines& lines) {
    if (lines.empty()) return Box{};
    const Band b = band_at(d, top(d));
    return Box{b.left, b.top, b.right - b.left, height() + 1};   // draw_row: the band, then the rim line under it
}
int StatusBar::bottom(const Display& d, const Lines& lines) { return lines.empty() ? 0 : top(d) + height(); }

void StatusBar::draw(Canvas& c, const Display& d, const Lines& lines, uint32_t now) {
    if (lines.empty()) return;
    const int n = int(lines.size());
    const int dots = n > 1 ? n * DOT_PITCH + 4 : 0;
    const Band band = band_at(d, top(d));
    const int room = text_room(band, dots);
    // A line keeps its turn until it has been read (or scrolled through), however the lines change meanwhile.
    auto turn_ms = [&](size_t k) { return line_ms(text_px(lines[k].first, 3), room); };
    if (turn_ >= lines.size()) { turn_ = 0; turn_since_ = now; }
    if (now - turn_since_ >= turn_ms(turn_)) { turn_ = (turn_ + 1) % lines.size(); turn_since_ = now; }
    draw_row(c, d, band, lines[turn_].first, lines[turn_].second, now - turn_since_, dots, RimSide::Below);
    const int dot_y = band.top + height() / 2 - 1;
    for (int k = 0; k < n && n > 1; k++)
        c.rect(band.right - dots + k * DOT_PITCH, dot_y, 3, 3, size_t(k) == turn_ ? CREAM : RIM);
}

Box CaptionBar::covers(const Display& d, const std::string& text, SpeechAt at, int below) {
    if (text.empty() || at == SpeechAt::Pet) return Box{};
    const Band b = band_at(d, top(d, at, below));
    const int h = StatusBar::height();
    // draw_row: the band, and its rim line under it (a top caption) or over it (at the bottom)
    return at == SpeechAt::Top ? Box{b.left, b.top, b.right - b.left, h + 1} : Box{b.left, b.top - 1, b.right - b.left, h + 1};
}

// A top caption starts under the bar's RIM line, one row past StatusBar::bottom().
int CaptionBar::top(const Display& d, SpeechAt at, int below) {
    if (at == SpeechAt::Bottom) return d.h - StatusBar::top(d) - StatusBar::height();
    return below > 0 ? below + 1 : StatusBar::top(d);
}

void CaptionBar::draw(Canvas& c, const Display& d, const std::string& text, uint16_t color, SpeechAt at, int below,
                      uint32_t now) {
    if (text.empty() || at == SpeechAt::Pet) {
        text_.clear();
        return;
    }
    const Band band = band_at(d, top(d, at, below));
    const uint32_t turn_ms = line_ms(text_px(text, 3), text_room(band, 0));
    if (text != text_ || now - since_ >= turn_ms) {            // a new caption, or the next turn of the same one
        text_ = text;
        since_ = now;
    }
    draw_row(c, d, band, text, color, now - since_, 0, at == SpeechAt::Top ? RimSide::Below : RimSide::Above);
}

namespace {

const RingStyle& ring_style(int size) { return RING_STYLES[std::clamp(size, 1, 3) - 1]; }
int ring_reach(const MeterRing& m) { return ring_style(m.size).radius + RING_REACH; }

struct Pen {
    int x, y;
};

struct RingSpot {
    int x = 0, y = 0;                    // the centre pixel
    bool placed = false;
};

// From the last ring outwards from the corner, tops (or bottoms) level, each as near the corner's side as it stays
// inside the visible area.
std::vector<RingSpot> place_rings(const Display& d, const std::vector<MeterRing>& rings, int below, Corner corner,
                                  int above) {
    std::vector<RingSpot> spots(rings.size());
    const bool left = corner == Corner::TopLeft || corner == Corner::BottomLeft;
    const bool low = corner == Corner::BottomLeft || corner == Corner::BottomRight;
    const int top = std::max(below, StatusBar::top(d)) + RING_MARGIN;
    // The first row no ring may reach: the bottom caption's, else the status bar's place mirrored (a round screen's
    // bottom is as narrow as its top).
    const int floor = (above < 0 ? d.h - StatusBar::top(d) : above) - RING_MARGIN;
    const int step = left ? 1 : -1;                               // inwards, away from the corner's side
    int side = left ? RING_MARGIN : d.w - 1 - RING_MARGIN;       // the outermost column the next ring may use
    for (size_t i = rings.size(); i-- > 0;) {
        const int reach = ring_reach(rings[i]), y = low ? floor - 1 - reach : top + reach;
        if (y - reach < top || y + reach >= floor) break;
        int x = side + step * reach;
        auto inside = [&] { return x - reach >= RING_MARGIN && x + reach < d.w - RING_MARGIN; };
        while (inside() && !d.visible(x, y, RING_MARGIN + reach)) x += step;
        if (!inside()) break;
        spots[i] = {x, y, true};
        side = x + step * (reach + 1 + RING_GAP);
    }
    return spots;
}

// Calls f(dx, dy, d2, band) for each pixel a ring of `size` draws, row by row; `band` for its coloured band.
template <typename F>
void each_ring_pixel(int size, F&& f) {
    const RingStyle& style = ring_style(size);
    const float outer = float(style.radius), inner = outer - style.thick, edge = outer + RING_OUTLINE_PX;
    const int reach = style.radius + RING_REACH;
    for (int dy = -reach; dy <= reach; dy++)
        for (int dx = -reach; dx <= reach; dx++) {
            const float d2 = float(dx * dx + dy * dy);
            if (d2 > edge * edge) continue;
            f(dx, dy, d2, !(d2 < inner * inner) && d2 <= outer * outer);
        }
}

// Where each pixel of a ring's band sits round the ring, as a part of a turn clockwise from 12 o'clock, in the
// order each_ring_pixel() visits them. Worked out once a size: an atan2 a pixel costs a small CPU more than the
// drawing. Under 4 KB, so it stays in fast internal memory. A plain static, as every cache here: on the device a
// thread_local is copied onto the top of every task's stack, the idle task's 1 KB too, and overflows it.
const std::vector<float>& band_turns(int size) {
    static std::vector<float> turns[3];
    std::vector<float>& t = turns[std::clamp(size, 1, 3) - 1];
    if (t.empty())
        each_ring_pixel(size, [&](int dx, int dy, float, bool band) {
            if (!band) return;
            float angle = std::atan2(float(dx), float(-dy));
            if (angle < 0) angle += float(FULL_TURN);
            t.push_back(angle / float(FULL_TURN));
        });
    return t;
}

void draw_ring(Canvas& c, const RingSpot& at, const MeterRing& m) {
    const RingStyle& style = ring_style(m.size);
    const float inner = float(style.radius) - style.thick;
    const std::vector<float>& turns = band_turns(m.size);
    size_t k = 0;
    each_ring_pixel(m.size, [&](int dx, int dy, float d2, bool band) {
        const float turn = band ? turns[k++] : 0;
        const int x = at.x + dx, y = at.y + dy;
        if (x < 0 || y < 0 || x >= c.w || y >= c.h) return;
        uint16_t color = RING_OUTLINE;
        if (d2 < inner * inner) color = INK;
        else if (band) color = turn < m.fill ? m.color : RING_TRACK;
        c.px[size_t(y) * c.w + x] = c.tinted(color);
    });
}

// The pixels `text` inks at `size`, relative to the pen at (0, 0) on the top of its capitals.
Box ink_box(const std::string& text, int size) {
    const FontSize& f = font_size(size);
    int pen = 0, x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (char ch : text) {
        const Glyph* g = glyph_of(f, ch);
        if (!g) continue;
        if (g->w && g->h) {
            x0 = std::min(x0, pen + g->x);
            y0 = std::min(y0, int(g->y));
            x1 = std::max(x1, pen + g->x + g->w - 1);
            y1 = std::max(y1, g->y + g->h - 1);
        }
        pen += g->advance;
    }
    return x1 < x0 ? Box{} : Box::spanning(x0, y0, x1, y1);
}

// The label centred in the ring at the largest font size (up to the ring's own) at which it stays on the dark
// disc, cut if it fits at none.
void draw_ring_label(Canvas& c, const RingSpot& at, const MeterRing& m) {
    const RingStyle& style = ring_style(m.size);
    const float inner = style.radius - style.thick;
    auto inside = [&](int dx, int dy) { return float(dx * dx + dy * dy) < inner * inner; };
    auto pen_for = [&](const Box& ink, int font) {
        return Pen{at.x - ink.x - (ink.w - 1) / 2, at.y - font_size(font).cap / 2};
    };
    auto fits = [&](const std::string& label, int font) {
        const Box ink = ink_box(label, font);
        const Pen pen = pen_for(ink, font);
        const int left = pen.x + ink.x - at.x, top = pen.y + ink.y - at.y;
        return inside(left, top) && inside(left + ink.w - 1, top) && inside(left, top + ink.h - 1) &&
               inside(left + ink.w - 1, top + ink.h - 1);
    };
    int font = style.font;
    while (font > 1 && !fits(m.label, font)) font--;
    std::string label = m.label;
    while (!label.empty() && !fits(label, font)) label = utf8_cut(label, label.size() - 1);
    const Pen pen = pen_for(ink_box(label, font), font);
    c.text(label, pen.x, pen.y, font, CREAM);
}

}  // namespace

void draw_meters(Canvas& c, const Display& d, const std::vector<MeterRing>& rings, int below, Corner corner,
                 int above) {
    const std::vector<RingSpot> spots = place_rings(d, rings, below, corner, above);
    for (size_t i = 0; i < rings.size(); i++) {
        if (!spots[i].placed) continue;
        draw_ring(c, spots[i], rings[i]);
        draw_ring_label(c, spots[i], rings[i]);
    }
}

std::vector<Box> meter_boxes(const Display& d, const std::vector<MeterRing>& rings, int below, Corner corner,
                             int above) {
    const std::vector<RingSpot> spots = place_rings(d, rings, below, corner, above);
    std::vector<Box> boxes(rings.size());
    for (size_t i = 0; i < rings.size(); i++) {
        const int reach = ring_reach(rings[i]);
        if (spots[i].placed)
            boxes[i] = Box::spanning(spots[i].x - reach, spots[i].y - reach, spots[i].x + reach, spots[i].y + reach);
    }
    return boxes;
}

namespace {

// Lays a bubble out afresh for the pet as it is now (see draw_speech) and keeps it in `place`.
void place_speech(const Display& d, const std::string& text, int s, const SpeechSpot& at, int below, bool same_words,
                  SpeechPlace& place) {
    const Box safe = d.safe(2);
    const int widest = std::min(safe.w - 14, s == 1 ? BUBBLE_MAX_W_SMALL : BUBBLE_MAX_W);
    const int wrap_was = same_words ? place.wrap : widest;      // the words keep their line breaks while they fit
    const int first_row = std::max(safe.y, 2 + below);
    // The text wrapped to `max_w`, and the bubble's size; false if it needs more than SPEECH_LINES.
    std::vector<std::string> lines;
    int w = 0, h = 0, wrap = 0;
    auto layout = [&](int max_w, bool cut) {
        lines = wrap_lines(text, max_w, s, nullptr, true);
        const bool whole = lines.size() <= SPEECH_LINES;
        if (!whole && !cut) return false;
        if (!whole) {
            lines.resize(SPEECH_LINES);
            std::string& last = lines.back();
            while (last.size() > 1 && text_px(last + "..", s) > max_w) last.pop_back();
            last += "..";
        }
        int longest = 0;
        for (auto& l : lines) longest = std::max(longest, text_px(l, s));
        w = longest + 12;
        h = text_block(int(lines.size()), s) + 9;
        wrap = max_w;
        return true;
    };

    // Beside the head, wrapped to the room there: its bottom a little under the top of the head, clear of the pet
    // by the tail's length.
    int x = 0, y = 0, left = 2, right = d.w - 2;
    d.span(at.head_y - BUBBLE_GAP, at.head_y + BUBBLE_DROP, 2, left, right);
    auto try_side = [&](int to) {
        const int room = (to > 0 ? right - at.right : at.left - left) - BUBBLE_GAP;
        if (room < BUBBLE_MIN_W || !layout(std::min({widest, wrap_was, room}), false)) return false;
        y = std::clamp(at.head_y - h + BUBBLE_DROP, first_row, std::max(first_row, d.h - 2 - h));
        x = to > 0 ? at.right + BUBBLE_GAP : at.left - BUBBLE_GAP - w;
        int l = 2, r = d.w - 2;
        return d.span(y, y + h, 2, l, r) && x >= l && x + w <= r;
    };
    const int first = place.side ? place.side : at.facing ? -at.facing : (at.left - left > right - at.right ? -1 : 1);
    int side = try_side(first) ? first : try_side(-first) ? -first : 0;
    bool under = false;
    if (!side) {   // above what the pet reaches, centred on the head; under its feet if that's off the top
        layout(widest, true);
        y = std::min(at.top, at.head_y) - h - BUBBLE_GAP;
        if (y < first_row && at.feet + BUBBLE_GAP + h <= d.h - 2 && d.visible(at.head_x, at.feet + BUBBLE_GAP + h, 2)) {
            y = at.feet + BUBBLE_GAP;
            under = true;
        }
        y = std::max(first_row, y);
        left = 2, right = d.w - 2;
        d.span(y, y + h, 2, left, right);
        x = std::clamp(at.head_x - w / 2, left, std::max(left, right - w));
    }
    place.text = text;
    place.size = s;
    place.head_x = at.head_x;
    place.feet_y = at.feet;
    place.side = side;
    place.under = under;
    place.wrap = wrap;
    place.x = x, place.y = y, place.w = w, place.h = h;
    place.tail =
        side ? std::clamp(at.head_y + BUBBLE_DROP / 2, y + 5, y + h - 6) : std::clamp(at.head_x, x + 6, x + w - 8);
    place.lines = std::move(lines);
}

}  // namespace

void draw_speech(Canvas& c, const Display& d, const std::string& text, uint16_t color, int size, const SpeechSpot& at,
                 int below, SpeechPlace& place) {
    if (text.empty()) return;
    const int s = std::clamp(size, 1, 3);
    const FontSize& f = font_size(s);
    const bool same_words = place.text == text && place.size == s && !place.lines.empty();
    if (!same_words || std::abs(at.head_x - place.head_x) > BUBBLE_HOLD_PX ||
        std::abs(at.feet - place.feet_y) > BUBBLE_HOLD_PX)
        place_speech(d, text, s, at, below, same_words, place);
    const int x = place.x, y = place.y, w = place.w, h = place.h;
    c.rect(x + 1, y, w - 2, h, RIM);                              // rounded corners: two overlapping rects
    c.rect(x, y + 1, w, h - 2, RIM);
    c.rect(x + 2, y + 1, w - 4, h - 2, INK);
    c.rect(x + 1, y + 2, w - 2, h - 4, INK);
    if (place.side) {   // the tail, out of the side that faces the pet, level with the head
        const int edge = place.side > 0 ? x : x + w - 1, out = place.side > 0 ? -1 : 1, ty = place.tail;
        for (int k = 0; k < 4; k++) {
            c.rect(edge + out * k, ty - 3 + k, 1, 7 - 2 * k, RIM);
            if (k < 3) c.rect(edge + out * k, ty - 2 + k, 1, 5 - 2 * k, INK);
        }
    } else {   // the tail, down to the head, or up to the feet
        const int tx = place.tail, edge = place.under ? y : y + h - 1, out = place.under ? -1 : 1;
        for (int k = 0; k < 4; k++) {
            c.rect(tx - 3 + k, edge + out * k, 7 - 2 * k, 1, RIM);
            if (k < 3) c.rect(tx - 2 + k, edge + out * k, 5 - 2 * k, 1, INK);
        }
    }
    for (size_t i = 0; i < place.lines.size(); i++) c.text(place.lines[i], x + 6, y + 4 + int(i) * f.pitch, s, color);
}

}  // namespace nestbox
