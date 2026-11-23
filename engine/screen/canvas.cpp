#include "screen/canvas.h"
#include "screen/font.h"

namespace nestbox {

void Canvas::recolour(const std::vector<uint16_t>& lut) {
    // Runs of one colour are the rule (a scene's flat areas), so the last answer is kept and the table is looked up
    // only when the colour changes; most pixels are already the pet's colours and are not written.
    const uint16_t* table = lut.data();
    uint16_t last_in = px.empty() ? 0 : uint16_t(~px[0]), last_out = 0;
    for (uint16_t& p : px) {
        if (p != last_in) {
            last_in = p;
            last_out = table[rgb444(p)];
        }
        if (last_out != p) p = last_out;
    }
}

void Canvas::rect(int x, int y, int rw, int rh, uint16_t c) {
    c = tinted(c);
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + rw > w ? w : x + rw, y1 = y + rh > h ? h : y + rh;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t* row = &px[size_t(yy) * w];
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

void Canvas::sprite(const FrameBox& b, const uint16_t* palette, int x, int y, int scale) {
    if (!b.px) return;                   // coded: read it with pixels_of() first
    for (int sy = 0; sy < b.h; sy++) {
        int dy = y + (b.y + sy) * scale;
        if (dy >= h || dy + scale <= 0) continue;
        for (int sx = 0; sx < b.w; sx++) {
            uint8_t k = b.px[sy * b.w + sx];
            if (!k) continue;
            rect(x + (b.x + sx) * scale, dy, scale, scale, palette[k]);
        }
    }
}

void Canvas::glyph(const char* const* rows, int nrows, int x, int y, int scale, uint16_t color, uint16_t outline) {
    color = tinted(color);
    outline = tinted(outline);
    for (int pass = 0; pass < 2; pass++)
        for (int j = 0; j < nrows; j++)
            for (int i = 0; rows[j][i]; i++)
                if (rows[j][i] == '1') {
                    if (pass == 0) rect(x + i * scale - 1, y + j * scale - 1, scale + 2, scale + 2, outline);
                    else rect(x + i * scale, y + j * scale, scale, scale, color);
                }
}

void Canvas::text(const char* s, int x, int y, int size, uint16_t color) {
    color = tinted(color);
    const FontSize& f = font_size(size);
    while (*s) {
        const Glyph* g = next_glyph(f, s);
        if (!g) continue;
        if (x + g->x + g->w <= 0 || x + g->x >= w) {   // wholly off the screen (a scrolling line's far end)
            x += g->advance;
            continue;
        }
        const int bytes = (g->w + 7) / 8;
        for (int r = 0; r < g->h; r++) {
            int py = y + g->y + r;
            if (py < 0 || py >= h) continue;
            const uint8_t* row = f.bits + g->bits + r * bytes;
            for (int c = 0; c < g->w; c++) {
                int dx = x + g->x + c;
                if (dx >= 0 && dx < w && (row[c / 8] >> (c % 8)) & 1) px[size_t(py) * w + dx] = color;
            }
        }
        x += g->advance;
    }
}

}  // namespace nestbox
