#include "door/pet_svg.h"
#include <algorithm>
#include <cstdio>
#include "screen/canvas.h"
#include "screen/font.h"

namespace nestbox {

namespace {

const char* const EMPTY_SVG = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\"/>";

void be32(std::string& s, uint32_t v) { s += {char(v >> 24), char(v >> 16), char(v >> 8), char(v)}; }

uint32_t crc32(const std::string& data) {
    uint32_t c = 0xffffffffu;
    for (unsigned char byte : data) {
        c ^= byte;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xedb88320u : c >> 1;
    }
    return ~c;
}

void png_chunk(std::string& png, const char* type, const std::string& data) {
    be32(png, uint32_t(data.size()));
    const std::string typed = std::string(type, 4) + data;
    png += typed;
    be32(png, crc32(typed));
}

}  // namespace

// One <rect> per horizontal run of a colour.
std::string pet_svg(const Egg* egg) {
    if (!egg || !egg->portrait.w) return EMPTY_SVG;
    const Portrait& p = egg->portrait;
    const int side = std::max(p.w, p.h), ox = (side - p.w) / 2, oy = side - p.h;
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" shape-rendering=\"crispEdges\">",
                  side, side);
    std::string out = buf;
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w;) {
            const uint8_t k = p.px[size_t(y) * p.w + x];
            int run = 1;
            while (x + run < p.w && p.px[size_t(y) * p.w + x + run] == k) run++;
            if (k && k < egg->palette.size()) {
                std::snprintf(buf, sizeof buf, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\" fill=\"#%06x\"/>",
                              x + ox, y + oy, run, unsigned(rgb888(egg->palette[k])));
                out += buf;
            }
            x += run;
        }
    return out + "</svg>";
}

// An indexed PNG with the egg's palette, index 0 transparent, its pixels in stored (uncompressed) deflate blocks: a
// few KB for a pet, and nothing to compress on the device.
std::string pet_png(const Egg* egg) {
    if (!egg || !egg->portrait.w) return "";
    const Portrait& p = egg->portrait;
    size_t colours = 1;
    for (uint8_t k : p.px) colours = std::max(colours, size_t(k) + 1);
    std::string header, palette, raw, z = "\x78\x01";
    be32(header, uint32_t(p.w));
    be32(header, uint32_t(p.h));
    header += std::string("\x08\x03\x00\x00\x00", 5);   // 8 bits per index, indexed colour
    for (size_t k = 0; k < colours; k++) {
        const uint32_t c = k && k < egg->palette.size() ? rgb888(egg->palette[k]) : 0;
        palette += {char(c >> 16), char(c >> 8), char(c)};
    }
    for (int y = 0; y < p.h; y++) {
        raw += '\0';                                        // no filter
        raw.append(reinterpret_cast<const char*>(p.px.data()) + size_t(y) * p.w, size_t(p.w));
    }
    uint32_t a = 1, b = 0;                                  // Adler-32 of the raw rows
    for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    for (size_t at = 0; at < raw.size();) {
        const size_t len = std::min<size_t>(raw.size() - at, 65535);
        z += char(at + len == raw.size());
        z += {char(len), char(len >> 8), char(~len), char(~len >> 8)};
        z.append(raw, at, len);
        at += len;
    }
    be32(z, (b << 16) | a);
    std::string png("\x89PNG\r\n\x1a\n", 8);
    png_chunk(png, "IHDR", header);
    png_chunk(png, "PLTE", palette);
    png_chunk(png, "tRNS", std::string(1, '\0'));
    png_chunk(png, "IDAT", z);
    png_chunk(png, "IEND", "");
    return png;
}

std::string text_svg(const std::string& text, int size, int scale) {
    if (text.empty() || !drawable(text)) return "";
    const FontSize& f = font_size(size);
    const int w = text_px(text, size), h = f.cap + f.below;
    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" width=\"%d\" height=\"%d\" "
                  "shape-rendering=\"crispEdges\" fill=\"currentColor\" aria-hidden=\"true\">",
                  w, h, w * scale, h * scale);
    std::string out = buf;
    int pen = 0;
    for (const char* s = text.c_str(); *s;) {
        const Glyph* g = next_glyph(f, s);
        if (!g) continue;
        const int row_bytes = (g->w + 7) / 8;
        for (int y = 0; y < g->h; y++) {
            const uint8_t* row = f.bits + g->bits + y * row_bytes;
            auto on = [&](int x) { return (row[x / 8] >> (x % 8)) & 1; };
            for (int x = 0; x < g->w;) {
                int run = 0;
                while (x + run < g->w && on(x + run)) run++;
                if (run) {
                    std::snprintf(buf, sizeof buf, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"1\"/>",
                                  pen + g->x + x, g->y + y, run);
                    out += buf;
                    x += run;
                } else {
                    x++;
                }
            }
        }
        pen += g->advance;
    }
    return out + "</svg>";
}

}  // namespace nestbox
