// Coded frames: run codes decoded into a cache when a frame is read.
#include "screen/frames.h"
#include <cstring>

namespace nestbox {

size_t decode_runs(const uint8_t* code, const uint8_t* end, uint8_t* out, size_t count, const uint8_t* whole, int fw,
                   int bx, int by, int bw, int colours) {
    const uint8_t* p = code;
    size_t i = 0;
    while (i < count) {
        if (end && p >= end) return 0;
        const uint8_t c = *p++;
        const size_t n = (c & 0x3F) + (c >= 0x80 && c < 0xC0 ? 2 : 1);
        if (i + n > count) return 0;
        if (c < 0x40) {                                   // clear
            std::memset(out + i, 0, n);
        } else if (c < 0x80) {                            // as the row's first frame
            if (!whole) return 0;
            for (size_t j = i; j < i + n; j++)
                out[j] = whole[size_t(by + int(j) / bw) * fw + bx + int(j) % bw];
        } else if (c < 0xC0) {                            // one colour, repeated
            if ((end && p >= end) || *p >= colours) return 0;
            std::memset(out + i, *p++, n);
        } else {                                          // as they are
            if (end && size_t(end - p) < n) return 0;
            for (size_t j = 0; j < n; j++)
                if (p[j] >= colours) return 0;
            std::memcpy(out + i, p, n);
            p += n;
        }
        i += n;
    }
    return size_t(p - code);
}

namespace {
constexpr int ANY_COLOUR = 256;          // checked when the egg was read

void decode(const FrameBox& b, const uint8_t* whole, uint8_t* out) {
    decode_runs(b.code, nullptr, out, size_t(b.w) * b.h, whole, b.fw, b.x, b.y, b.w, ANY_COLOUR);
}
}  // namespace

const FrameBox& pixels_of(const FrameBox& b, FrameCache& cache) {
    if (!b.code) return b;
    if (cache.frame == &b && cache.code == b.code) return cache.box;
    if (b.first && (cache.first != b.first || cache.first_code != b.first->code)) {   // the row's first frame, whole
        const FrameBox& f = *b.first;
        cache.whole.assign(size_t(b.fw) * b.fh, 0);
        cache.px.resize(size_t(f.w) * f.h);
        decode(f, nullptr, cache.px.data());
        for (int y = 0; y < f.h; y++)
            std::memcpy(&cache.whole[size_t(f.y + y) * b.fw + f.x], &cache.px[size_t(y) * f.w], size_t(f.w));
        cache.first = b.first;
        cache.first_code = f.code;
    }
    cache.px.resize(size_t(b.w) * b.h);
    decode(b, b.first ? cache.whole.data() : nullptr, cache.px.data());
    cache.box = b;
    cache.box.px = cache.px.data();
    cache.box.code = nullptr;
    cache.frame = &b;
    cache.code = b.code;
    return cache.box;
}

}  // namespace nestbox
