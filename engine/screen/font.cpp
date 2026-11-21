#include "screen/font.h"
#include <algorithm>

namespace nestbox {

namespace {
constexpr unsigned char FIRST_GLYPH = ' ', LAST_GLYPH = '~';
constexpr unsigned char UTF8_LEAD_MIN = 0xC0;
}  // namespace

const Glyph* glyph_of(const FontSize& f, char c) {
    if (c == '~') c = ' ';                               // a non-breaking space (see screen/layout.h)
    unsigned char u = static_cast<unsigned char>(c);
    if (u >= UTF8_LEAD_MIN) return &f.glyphs['?' - FIRST_GLYPH];   // one "?" per non-ASCII character
    return u >= FIRST_GLYPH && u <= LAST_GLYPH ? &f.glyphs[u - FIRST_GLYPH] : nullptr;   // continuation bytes: none
}

bool drawable(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        unsigned char u = static_cast<unsigned char>(c);
        return u >= FIRST_GLYPH && u <= LAST_GLYPH;
    });
}

int text_px(const std::string& text, int size) {
    const FontSize& f = font_size(size);
    int w = 0;
    for (char c : text)
        if (const Glyph* g = glyph_of(f, c)) w += g->advance;
    return w;
}

}  // namespace nestbox
