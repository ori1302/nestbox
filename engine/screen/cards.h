// Full-screen cards (the setup screens): a column of text, the pet and a progress bar, or a QR code with a few
// lines. Cards adapt to every screen profile: each part takes the width visible where it lands, text shrinks or
// wraps, and the pet steps down a pixel size before the text does.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "screen/canvas.h"
#include "screen/display.h"
#include "screen/layout.h"

namespace nestbox {
namespace cards {

constexpr uint16_t BG = rgb(0x14, 0x17, 0x1c), FG = rgb(0xf2, 0xf0, 0xe6), DIM = rgb(0x9a, 0xa3, 0xad);
constexpr uint16_t YES = rgb(0x7d, 0xd8, 0x8a), WARN = rgb(0xff, 0xb4, 0x6b), ACCENT = rgb(0xff, 0xd8, 0x6b);

// The pet as a card shows it: one animation's first row of frames, its palette and its own pixel size (0: the
// screen's). frames == 0: no pet.
struct PetPicture {
    const FrameBox* boxes = nullptr;     // `frames` of them
    const uint8_t* durations = nullptr;  // ticks per frame
    int frames = 0, fw = 0, fh = 0, scale = 0;
    const uint16_t* palette = nullptr;
};

// One piece of a column card.
struct Piece {
    enum Kind { Text, Pet, Bar } kind = Text;
    std::string text;
    uint16_t color = 0;
    int max_scale = 3, min_scale = 2, max_h = 60;
    int gap = 10;                        // space below it
};
Piece text_piece(const std::string& t, uint16_t color, int max_scale = 3, int gap = 10, int max_h = 60);
Piece pet_piece(int gap = 18);
// Draws the pieces as one centred column; `progress` (0..1) fills any Bar piece.
void stack(Canvas& c, Layout* l, const Display& d, const std::vector<Piece>& parts, const PetPicture& pet, uint32_t now,
           float progress);

// A line of text on a QR card.
struct Line {
    std::string text;
    uint16_t color = FG;
    int scale = 3;                       // largest size; it may shrink to 2
    int gap = 6;                         // space below it
    bool whole = false;                  // never broken across lines (an IP address)
    bool optional = false;               // dropped when nothing else fits
};
// A title, a QR code, and lines beside the code (then `below` under it) or all `stacked` under it.
struct Card {
    std::string title, short_title;
    uint16_t title_color = FG;
    int title_scale = 2, title_gap = 12;
    std::string qr;
    std::vector<Line> side, below, stacked;
};
// Draws the first arrangement where everything is visible, nothing touches and no line is cut; if none fits,
// draws the smallest and returns false.
bool qr_card(Canvas& c, Layout* l, const Display& d, const Card& card);

}  // namespace cards
}  // namespace nestbox
