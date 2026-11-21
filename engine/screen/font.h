// The pet's text font: DejaVu Sans Bold as sharp pixels, at three sizes (capitals 7, 10 and 15 px tall),
// printable ASCII. Proportional: measure with text_px().
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
    const Glyph* glyphs;         // ' ' to '~'
    const uint8_t* bits;
};

const FontSize& font_size(int size);
// The glyph for one byte of UTF-8 text: printable ASCII as itself, a non-ASCII character as one "?" (on its lead
// byte); nullptr for continuation bytes and control characters.
const Glyph* glyph_of(const FontSize& f, char c);
// True if every character has a glyph of its own (printable ASCII).
bool drawable(const std::string& text);
// Width in pixels; "~" is a non-breaking space.
int text_px(const std::string& text, int size);
// The height of `lines` lines: from the top of the first line's capitals to the bottom of the last one.
inline int text_block(int lines, int size) {
    const FontSize& f = font_size(size);
    return lines > 0 ? (lines - 1) * f.pitch + f.cap + f.below : 0;
}

}  // namespace nestbox
