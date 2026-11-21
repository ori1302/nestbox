#include "image.h"
#include <cstdio>

namespace nestbox {

namespace {
constexpr int BMP_FILE_HEADER = 14, BMP_INFO_HEADER = 40, BMP_HEADER = BMP_FILE_HEADER + BMP_INFO_HEADER;
constexpr int BITS_PER_PIXEL = 24;

uint8_t red(uint32_t rgb) { return uint8_t(rgb >> 16); }
uint8_t green(uint32_t rgb) { return uint8_t(rgb >> 8); }
uint8_t blue(uint32_t rgb) { return uint8_t(rgb); }
}  // namespace

std::string bmp_image(const Canvas& c) {
    const int row = (c.w * 3 + 3) & ~3;          // rows are padded to 4 bytes
    const int size = BMP_HEADER + row * c.h;
    std::string b(size_t(size), '\0');
    auto put = [&](int at, uint32_t v, int bytes) {
        for (int i = 0; i < bytes; i++) b[size_t(at + i)] = char((v >> (8 * i)) & 255);
    };
    b[0] = 'B';
    b[1] = 'M';
    put(2, uint32_t(size), 4);
    put(10, BMP_HEADER, 4);
    put(14, BMP_INFO_HEADER, 4);
    put(18, uint32_t(c.w), 4);
    put(22, uint32_t(c.h), 4);
    put(26, 1, 2);                               // colour planes
    put(28, BITS_PER_PIXEL, 2);
    put(34, uint32_t(row * c.h), 4);
    for (int y = 0; y < c.h; y++)                // bottom row first, blue-green-red
        for (int x = 0; x < c.w; x++) {
            const uint32_t p = rgb888(c.px[size_t((c.h - 1 - y) * c.w + x)]);
            const size_t at = size_t(BMP_HEADER + y * row + x * 3);
            b[at] = char(blue(p));
            b[at + 1] = char(green(p));
            b[at + 2] = char(red(p));
        }
    return b;
}

bool write_ppm(const Canvas& c, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", c.w, c.h);
    for (uint16_t px : c.px) {
        const uint32_t p = rgb888(px);
        const uint8_t bytes[3] = {red(p), green(p), blue(p)};
        std::fwrite(bytes, 1, 3, f);
    }
    std::fclose(f);
    return true;
}

}  // namespace nestbox
