// A small QR code encoder for the pet's own screens: byte mode, error correction level M, versions 1-6 (up to
// 106 bytes), enough for a Wi-Fi joining code or a short address.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nestbox {

struct Qr {
    int size = 0;                          // modules per side (21 for version 1, +4 per version)
    std::vector<uint8_t> dark;             // size * size, row by row; 1 = dark module
    bool at(int x, int y) const { return dark[size_t(y) * size + x] != 0; }
};

// Encodes `text` in the smallest version that fits; false if it is longer than 106 bytes or `force_mask` is not -1
// (the lowest-penalty mask) or 0-7.
bool qr_encode(const std::string& text, Qr& out, int force_mask = -1);

}  // namespace nestbox
