// Animation frames and their timing. Durations are counted in ticks of 1/TICKS_PER_S s.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nestbox {

constexpr int TICKS_PER_S = 60;

// One frame's picture: only the box that has pixels in it (eggs store frames cropped), placed at (x, y) in its
// animation's whole frame. An empty frame has w = h = 0. Its pixels are stored as run codes (code); pixels_of()
// decodes them into px.
struct FrameBox {
    int x = 0, y = 0, w = 0, h = 0;
    const uint8_t* px = nullptr;         // w * h palette indices, 0 = transparent
    const uint8_t* code = nullptr;       // or its run codes (https://nestbox.pet/docs/egg-format/)
    const FrameBox* first = nullptr;     // coded: its row's first frame, which its "as the first" runs copy
    int fw = 0, fh = 0;                  // coded: the whole frame's size
};

// The frame last read as pixels, and its row's first frame laid out whole.
struct FrameCache {
    const FrameBox* frame = nullptr;
    const uint8_t* code = nullptr;       // its codes: another egg at the same address is told apart
    const FrameBox* first = nullptr;
    const uint8_t* first_code = nullptr;
    FrameBox box;
    std::vector<uint8_t> px, whole;
};

// Frame `b` with its pixels: as stored, or its codes decoded into `cache` (kept until another frame is read).
const FrameBox& pixels_of(const FrameBox& b, FrameCache& cache);

// Decodes `count` pixels of run codes from `code` (not past `end`, unless it is null) into `out`, the "as the first
// frame" runs from `whole` (fw wide, the box at bx, by, bw wide; none: such a run is wrong). Colours must be below
// `colours`.
// Returns the bytes of codes read, or 0 if they are wrong.
size_t decode_runs(const uint8_t* code, const uint8_t* end, uint8_t* out, size_t count, const uint8_t* whole, int fw,
                   int bx, int by, int bw, int colours);

// Ticks elapsed after `ms` milliseconds played at `speed` times the normal pace.
inline int64_t ms_to_ticks(uint32_t ms, double speed = 1) {
    return int64_t(ms * double(TICKS_PER_S) / 1000.0 * speed);
}

inline uint32_t ticks_to_ms(int ticks) { return uint32_t(ticks) * 1000 / TICKS_PER_S; }

enum class Playback { Loop, Hold };

// The index of the frame showing `tick` ticks in, from each frame's length in ticks; after the last frame the
// animation starts again (Loop) or stays on it (Hold).
inline int frame_at(const uint8_t* durations, int count, int64_t tick, Playback playback = Playback::Loop) {
    if (count <= 0) return 0;
    if (playback == Playback::Loop) {
        int64_t total = 0;
        for (int i = 0; i < count; i++) total += durations[i];
        if (total > 0) tick %= total;
    }
    int f = 0;
    while (f < count - 1 && tick >= durations[f]) tick -= durations[f++];
    return f;
}

}  // namespace nestbox
