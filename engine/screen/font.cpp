#include "screen/font.h"

#include <algorithm>

namespace nestbox {

namespace {
constexpr uint32_t FIRST_GLYPH = ' ', LAST_GLYPH = '~';
constexpr uint32_t BROKEN = 0xFFFD;    // what a malformed UTF-8 byte stands for

// The code point of the UTF-8 character at `s`, and its length in bytes; a malformed byte is one BROKEN.
uint32_t decode(const char* s, int& len) {
    const unsigned char u = static_cast<unsigned char>(s[0]);
    len = 1;
    if (u < 0x80) return u;
    const int more = u >= 0xF0 && u < 0xF8 ? 3 : u >= 0xE0 ? 2 : u >= 0xC0 ? 1 : -1;
    if (more < 0) return BROKEN;
    uint32_t cp = u & (0x3F >> more);
    for (int i = 1; i <= more; i++) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) return BROKEN;
        cp = cp << 6 | (c & 0x3F);
    }
    len = 1 + more;
    return cp;
}

// The index of a code point's own glyph, or -1.
int own_glyph(uint32_t cp) {
    if (cp == '~') cp = ' ';                               // a non-breaking space (see screen/layout.h)
    if (cp >= FIRST_GLYPH && cp <= LAST_GLYPH) return int(cp - FIRST_GLYPH);
    const uint32_t* end = FONT_EXTRA + FONT_EXTRAS;
    const uint32_t* at = std::lower_bound(FONT_EXTRA, end, cp);   // FONT_EXTRA is sorted
    return at != end && *at == cp ? int(LAST_GLYPH - FIRST_GLYPH + 1) + int(at - FONT_EXTRA) : -1;
}
}  // namespace

const Glyph* next_glyph(const FontSize& f, const char*& s) {
    int len = 1;
    const uint32_t cp = decode(s, len);
    s += len;
    const int own = own_glyph(cp);
    if (own >= 0) return &f.glyphs[own];
    return cp < 0x80 ? nullptr : &f.glyphs['?' - FIRST_GLYPH];   // control characters draw nothing
}

bool drawable(const std::string& text) {
    for (const char* s = text.c_str(); *s;) {
        int len = 1;
        if (own_glyph(decode(s, len)) < 0) return false;
        s += len;
    }
    return true;
}

int text_px(const std::string& text, int size) {
    const FontSize& f = font_size(size);
    int w = 0;
    for (const char* s = text.c_str(); *s;)
        if (const Glyph* g = next_glyph(f, s)) w += g->advance;
    return w;
}

}  // namespace nestbox
