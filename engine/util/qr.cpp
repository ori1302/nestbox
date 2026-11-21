#include "util/qr.h"
#include <algorithm>
#include <cstdlib>
#include <string>

namespace nestbox {

namespace {
constexpr int MAX_VERSION = 6;
// Error correction level M, versions 1-6 (index = version): codewords per block, number of blocks.
const int ECC_PER_BLOCK[7] = {0, 10, 16, 26, 18, 24, 16};
const int BLOCKS[7] = {0, 1, 1, 1, 2, 2, 4};
const int FORMAT_M = 0;          // the format bits for level M

int raw_modules(int ver) {      // data + error correction modules in a symbol of this version
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2) { int n = ver / 7 + 2; r -= (25 * n - 10) * n - 55; }
    return r;
}
int data_codewords(int ver) { return raw_modules(ver) / 8 - ECC_PER_BLOCK[ver] * BLOCKS[ver]; }

uint8_t gf_mul(uint8_t x, uint8_t y) {          // multiplication in GF(2^8) modulo x^8+x^4+x^3+x^2+1
    int z = 0;
    for (int i = 7; i >= 0; i--) { z = (z << 1) ^ ((z >> 7) * 0x11D); z ^= ((y >> i) & 1) * x; }
    return uint8_t(z);
}
std::vector<uint8_t> rs_divisor(int degree) {
    std::vector<uint8_t> r(size_t(degree), 0);
    r.back() = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree; j++) {
            r[size_t(j)] = gf_mul(r[size_t(j)], root);
            if (j + 1 < degree) r[size_t(j)] ^= r[size_t(j) + 1];
        }
        root = gf_mul(root, 0x02);
    }
    return r;
}
std::vector<uint8_t> rs_remainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& div) {
    std::vector<uint8_t> r(div.size(), 0);
    for (uint8_t b : data) {
        uint8_t f = b ^ r[0];
        r.erase(r.begin()); r.push_back(0);
        for (size_t i = 0; i < r.size(); i++) r[i] ^= gf_mul(div[i], f);
    }
    return r;
}

struct Grid {
    int n;
    std::vector<uint8_t> dark, fn;              // module colour; part of a fixed pattern
    explicit Grid(int size) : n(size), dark(size_t(size) * size, 0), fn(size_t(size) * size, 0) {}
    void set_fn(int x, int y, bool d) { dark[size_t(y) * n + x] = d; fn[size_t(y) * n + x] = 1; }
    bool is_fn(int x, int y) const { return fn[size_t(y) * n + x] != 0; }
    bool at(int x, int y) const { return dark[size_t(y) * n + x] != 0; }
};

void draw_format(Grid& g, int mask) {
    int data = FORMAT_M << 3 | mask, rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = (data << 10 | rem) ^ 0x5412;
    auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
    for (int i = 0; i <= 5; i++) g.set_fn(8, i, bit(i));
    g.set_fn(8, 7, bit(6)); g.set_fn(8, 8, bit(7)); g.set_fn(7, 8, bit(8));
    for (int i = 9; i < 15; i++) g.set_fn(14 - i, 8, bit(i));
    for (int i = 0; i < 8; i++) g.set_fn(g.n - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; i++) g.set_fn(8, g.n - 15 + i, bit(i));
    g.set_fn(8, g.n - 8, true);                 // the module that is always dark
}

void draw_function_patterns(Grid& g, int ver) {
    for (int i = 0; i < g.n; i++) { g.set_fn(6, i, i % 2 == 0); g.set_fn(i, 6, i % 2 == 0); }   // timing
    auto finder = [&](int cx, int cy) {
        for (int dy = -4; dy <= 4; dy++)
            for (int dx = -4; dx <= 4; dx++) {
                int x = cx + dx, y = cy + dy, d = std::max(std::abs(dx), std::abs(dy));
                if (x >= 0 && x < g.n && y >= 0 && y < g.n) g.set_fn(x, y, d != 2 && d != 4);
            }
    };
    finder(3, 3); finder(g.n - 4, 3); finder(3, g.n - 4);
    if (ver >= 2) {                             // one alignment pattern for versions 2-6
        int p = g.n - 7;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) g.set_fn(p + dx, p + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
    draw_format(g, 0);                          // reserve the format areas; the real bits come later
}

void draw_codewords(Grid& g, const std::vector<uint8_t>& data) {
    size_t i = 0;
    for (int right = g.n - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < g.n; vert++)
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? g.n - 1 - vert : vert;
                if (!g.is_fn(x, y) && i < data.size() * 8) {
                    g.dark[size_t(y) * g.n + x] = (data[i >> 3] >> (7 - (i & 7))) & 1;
                    i++;
                }
            }
    }
}

bool mask_bit(int mask, int x, int y) {
    switch (mask) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (x / 3 + y / 2) % 2 == 0;
        case 5: return x * y % 2 + x * y % 3 == 0;
        case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
        default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}
void apply_mask(Grid& g, int mask) {
    for (int y = 0; y < g.n; y++)
        for (int x = 0; x < g.n; x++)
            if (!g.is_fn(x, y) && mask_bit(mask, x, y)) g.dark[size_t(y) * g.n + x] ^= 1;
}

// The standard penalty (runs, 2x2 blocks, finder-like patterns, dark balance): lower reads better.
long penalty(const Grid& g) {
    long p = 0;
    auto line = [&](bool rows) {
        for (int a = 0; a < g.n; a++) {
            int run = 1;
            std::string s;
            for (int b = 0; b < g.n; b++) s += (rows ? g.at(b, a) : g.at(a, b)) ? '1' : '0';
            for (int b = 1; b < g.n; b++) {
                if (s[size_t(b)] == s[size_t(b) - 1]) { run++; if (run == 5) p += 3; else if (run > 5) p++; }
                else run = 1;
            }
            std::string padded = "0000" + s + "0000";
            for (size_t at = 0; (at = padded.find("10111010000", at)) != std::string::npos; at++) p += 40;
            for (size_t at = 0; (at = padded.find("00001011101", at)) != std::string::npos; at++) p += 40;
        }
    };
    line(true); line(false);
    for (int y = 0; y + 1 < g.n; y++)
        for (int x = 0; x + 1 < g.n; x++) {
            bool c = g.at(x, y);
            if (c == g.at(x + 1, y) && c == g.at(x, y + 1) && c == g.at(x + 1, y + 1)) p += 3;
        }
    long dark = 0, total = long(g.n) * g.n;
    for (auto d : g.dark) dark += d;
    long k = (std::labs(dark * 20 - total * 10) + total - 1) / total - 1;
    return p + k * 10;
}

// Byte mode: mode 0100, an 8-bit length, the bytes, a terminator, then the standard pad bytes up to `capacity`.
std::vector<uint8_t> data_codewords_for(const std::string& text, int capacity) {
    std::vector<bool> bits;
    auto put = [&](uint32_t v, int n) { for (int i = n - 1; i >= 0; i--) bits.push_back((v >> i) & 1); };
    put(0x4, 4);
    put(uint32_t(text.size()), 8);
    for (unsigned char c : text) put(c, 8);
    put(0, std::min(4, capacity * 8 - int(bits.size())));
    put(0, (8 - int(bits.size()) % 8) % 8);
    std::vector<uint8_t> data;
    for (size_t i = 0; i < bits.size(); i += 8) {
        uint8_t b = 0;
        for (int j = 0; j < 8; j++) b = uint8_t(b << 1 | bits[i + size_t(j)]);
        data.push_back(b);
    }
    for (uint8_t pad = 0xEC; int(data.size()) < capacity; pad ^= 0xEC ^ 0x11) data.push_back(pad);
    return data;
}

// Splits the data into blocks, appends each block's error correction, then interleaves the blocks.
std::vector<uint8_t> with_error_correction(const std::vector<uint8_t>& data, int ver) {
    const int nb = BLOCKS[ver], ecl = ECC_PER_BLOCK[ver], raw = raw_modules(ver) / 8;
    const int short_blocks = nb - raw % nb, short_len = raw / nb;
    const std::vector<uint8_t> div = rs_divisor(ecl);
    std::vector<std::vector<uint8_t>> blocks;
    for (int i = 0, k = 0; i < nb; i++) {
        int len = short_len - ecl + (i < short_blocks ? 0 : 1);
        std::vector<uint8_t> block(data.begin() + k, data.begin() + k + len);
        k += len;
        std::vector<uint8_t> ecc = rs_remainder(block, div);
        if (i < short_blocks) block.push_back(0);
        block.insert(block.end(), ecc.begin(), ecc.end());
        blocks.push_back(block);
    }
    std::vector<uint8_t> all;
    for (size_t i = 0; i < blocks[0].size(); i++)
        for (int j = 0; j < nb; j++)
            if (int(i) != short_len - ecl || j >= short_blocks) all.push_back(blocks[size_t(j)][i]);
    return all;
}
}  // namespace

bool qr_encode(const std::string& text, Qr& out, int force_mask) {
    if (force_mask < -1 || force_mask > 7) return false;
    int ver = 1;
    while (ver <= MAX_VERSION && 2 + text.size() > size_t(data_codewords(ver))) ver++;   // 4-bit mode + 8-bit count
    if (ver > MAX_VERSION) return false;

    Grid base(ver * 4 + 17);
    draw_function_patterns(base, ver);
    draw_codewords(base, with_error_correction(data_codewords_for(text, data_codewords(ver)), ver));
    long best = -1;
    for (int mask = 0; mask < 8; mask++) {
        if (force_mask >= 0 && mask != force_mask) continue;
        Grid g = base;
        apply_mask(g, mask);
        draw_format(g, mask);
        long p = penalty(g);
        if (best < 0 || p < best) { best = p; out.size = g.n; out.dark = g.dark; }
    }
    return true;
}

}  // namespace nestbox
