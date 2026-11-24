// The heads-up layer drawn over the scene on every screen: marks over the pet's head, the status bar along the
// top, speech bubbles and caption bars. Drawing only; the pet decides what to show and where its head is.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "screen/canvas.h"
#include "screen/display.h"

namespace nestbox {

enum class Bubble { None, Heart, Bang, Zzz, Dots, Note, Question, Angry, Shock, Cross, Star, Check };

// Effects in the scene around the pet, on top of whatever it does.
enum class Effect { None, Notes, Sparkles, Confetti, Letter };

// A look's sign over the head; (hx, hy) is the top of the frame being shown, `unit` the sign's pixel size.
void draw_bubble(Canvas& c, Bubble b, int hx, int hy, int unit, uint32_t now_ms);
void draw_note(Canvas& c, int x, int y, int unit);
// Sparkles twinkling around the pet, whose middle is (cx, cy) and half-size `r`; confetti falling over the screen.
void draw_sparkles(Canvas& c, int cx, int cy, int r, int unit, uint32_t now_ms);
void draw_confetti(Canvas& c, const Display& d, uint32_t now_ms);
// A letter with an unread dot over the head, (hx, hy) the top of the frame: centred, or `beside` a sign (to its
// left, clear of it). It pops in with a bounce `shown_ms` after it first showed, then bobs. letter_box is where it
// reaches, so a speech bubble keeps clear of it.
struct LetterBox {
    int left, top;
};
LetterBox letter_box(int hx, int hy, int unit, bool beside);
void draw_letter(Canvas& c, int hx, int hy, int unit, bool beside, uint32_t shown_ms);

// Where a sender's words go: a speech bubble over the pet, or a caption row across the top or bottom.
enum class SpeechAt { Pet, Top, Bottom };

// The status bar: one line at a time in the large font, readable at arm's length on a 1.69" screen. Several lines
// take turns (dots on the right show which), and a line too wide for the screen scrolls.
class StatusBar {
public:
    using Lines = std::vector<std::pair<std::string, uint16_t>>;   // text, colour; newest last
    void draw(Canvas& c, const Display& d, const Lines& lines, uint32_t now_ms);
    // The bar's top row: 0, or lower on a round screen where the circle is wide enough.
    static int top(const Display& d);
    // The first row below the bar (0 when there are no lines).
    static int bottom(const Display& d, const Lines& lines);
    static int height();
    // What draw() paints over completely when it shows `lines`: the band and its rim line. Empty without lines.
    static Box covers(const Display& d, const Lines& lines);
private:
    size_t turn_ = 0;                    // the line being shown
    uint32_t turn_since_ = 0;
};

// A caption: one more row in the status bar's style, under the status bar (in its place without one) or mirrored
// at the bottom edge. A caption too wide for the screen scrolls like a status line, from when it appeared.
class CaptionBar {
public:
    // Draws nothing for empty text or SpeechAt::Pet. `below` is StatusBar::bottom().
    void draw(Canvas& c, const Display& d, const std::string& text, uint16_t color, SpeechAt at, int below,
              uint32_t now_ms);
    // The caption row's top row, for a status bar ending at `below` (0 when there is none).
    static int top(const Display& d, SpeechAt at, int below);
    // The first row below a top caption, as StatusBar::bottom() is for the bar.
    static int bottom(const Display& d, int below) { return top(d, SpeechAt::Top, below) + StatusBar::height(); }
    // What draw() paints over completely for `text` at `at`: the band and its rim line. Empty when it draws nothing.
    static Box covers(const Display& d, const std::string& text, SpeechAt at, int below);

private:
    std::string text_;                   // the caption being shown, and since when
    uint32_t since_ = 0;
};

// A progress ring in a corner of the screen: filled clockwise from 12 o'clock, a short label inside.
struct MeterRing {
    float fill = 0;                      // 0..1
    std::string label;                   // cut to what fits inside the ring
    uint16_t color = 0;                  // the filled part, RGB565
    int size = 2;                        // 1 small, 2 medium, 3 large
};

// Draws the rings side by side in a corner, the last one nearest it, inside the visible area: in a top corner from
// row `below` down (under the status bar), in a bottom one above row `above` (a bottom caption; -1: the screen's
// bottom edge), tops or bottoms level. A ring that finds no room is left out.
void draw_meters(Canvas& c, const Display& d, const std::vector<MeterRing>& rings, int below,
                 Corner corner = Corner::TopRight, int above = -1);
// The box each ring covers, outline included, in the same order as `rings`; an empty box for one left out.
std::vector<Box> meter_boxes(const Display& d, const std::vector<MeterRing>& rings, int below,
                             Corner corner = Corner::TopRight, int above = -1);

// Where a speech bubble goes: beside the pet's head, so what shows above the head (a sign, a hop, a throw) stays in
// sight; above all of it when neither side has room, and under its feet when that doesn't fit either.
struct SpeechSpot {
    int head_x = 0, head_y = 0;          // the top of the pet's head
    int left = 0, right = 0, top = 0;    // what the playing animation and its signs reach: outer columns, top row
    int feet = 0;                        // the row the pet stands on
    int facing = 0;                      // -1 left, 1 right, 0 front or back
};

// Where a bubble went. It stays there, the same lines in the same place, while its words are the same and the pet
// stands where it stood: a wave, a hop or a turn does not move it, only walking off does. Kept between frames.
struct SpeechPlace {
    std::string text;
    int size = 0;
    int head_x = 0, feet_y = 0;          // where the pet stood when the bubble was placed
    int side = 0;                        // -1 left of the pet, 1 right, 0 above or under it
    bool under = false;
    int wrap = 0;                        // the width its words were wrapped to
    int x = 0, y = 0, w = 0, h = 0;
    int tail = 0;                        // the tail's row beside the pet, or its column above or under
    std::vector<std::string> lines;
};

// A speech bubble whose tail points at the pet's head. `size` 1-3 is the font size; `below` is the first row it may
// use (under the status bar). Placed anew, it goes on the side it went last, else behind the pet (the side it isn't
// facing), else on the roomier side, wrapped as before when that still fits; then `place` holds it still.
void draw_speech(Canvas& c, const Display& d, const std::string& text, uint16_t color, int size, const SpeechSpot& at,
                 int below, SpeechPlace& place);

}  // namespace nestbox
