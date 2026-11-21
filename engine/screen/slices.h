// The screen in slices, SLICES across each row, each with a fingerprint of its pixels. A device that fingerprints
// every frame as it sends it (the firmware's LCD sender) sends only the slices that changed, and hands the
// fingerprints back with the frame's buffer (Canvas::held), so the scene's picture is restored only where the
// buffer differs from it.
#pragma once
#include <cstddef>
#include <cstdint>

namespace nestbox {

constexpr int SLICES = 10;   // 32 px wide on a 320 screen, 28 on 280, 24 on 240: whole pixel pairs on every screen

// Fingerprints are built two pixels at a time, as the pair reads from memory (two RGB565 pixels in a 32-bit word),
// from SLICE_SEED. Equal pixels give equal fingerprints; different ones the same one about once in four billion.
constexpr uint32_t SLICE_SEED = 0x811C9DC5u;
inline uint32_t slice_step(uint32_t h, uint32_t pair) {
    h = (h ^ pair) * 0x9E3779B1u;
    return h ^ (h >> 15);
}

// The fingerprint of n pixels (n even, px 4-byte aligned).
inline uint32_t slice_hash(const uint16_t* px, size_t n) {
    typedef uint32_t __attribute__((may_alias)) Pair;
    const Pair* p = reinterpret_cast<const Pair*>(px);
    uint32_t h = SLICE_SEED;
    for (size_t i = 0; i < n / 2; i++) h = slice_step(h, p[i]);
    return h;
}

}  // namespace nestbox
