// The pet's text font: DejaVu Sans Bold as sharp pixels, at three sizes (capitals 7, 10 and 15 px tall),
// printable ASCII and the characters in FONT_EXTRA. Proportional: measure with text_px().
#pragma once
#include <cstdint>
#include <string>

namespace nestbox {

struct Glyph {
    int8_t advance;              // how far the next letter starts
    int8_t x, y;                 // the bitmap's top-left, from the pen and from the top of the capitals
    uint8_t w, h;
    uint16_t bits;               // where its rows start: (w + 7) / 8 bytes per row, lowest bit leftmost
};

struct FontSize {
    int size;                    // 1, 2 or 3
    int cap;                     // capital height in pixels: text is placed by the top of its capitals
    int below;                   // how far letters like g and y reach under the line
    int pitch;                   // line to line
    const Glyph* glyphs;         // ' ' to '~', then FONT_EXTRA's
    const uint8_t* bits;
};

const FontSize& font_size(int size);
// The characters past ASCII that have glyphs of their own, as code points, sorted: Latin-1 letters and signs,
// typographic punctuation and a few symbols (tools/fontgen.py lists them).
extern const uint32_t FONT_EXTRA[];
extern const int FONT_EXTRAS;
// The glyph for the UTF-8 character at `s`, moving `s` past it: printable ASCII and FONT_EXTRA as themselves, any
// other character as one "?"; nullptr for a control character.
const Glyph* next_glyph(const FontSize& f, const char*& s);
// True if every character has a glyph of its own.
bool drawable(const std::string& text);
// Width in pixels; "~" is a non-breaking space.
int text_px(const std::string& text, int size);
// The height of `lines` lines: from the top of the first line's capitals to the bottom of the last one.
inline int text_block(int lines, int size) {
    const FontSize& f = font_size(size);
    return lines > 0 ? (lines - 1) * f.pitch + f.cap + f.below : 0;
}

}  // namespace nestbox
