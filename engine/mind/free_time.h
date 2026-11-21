// The pet's own life when nothing is going on: it stands about, now and then strolls round the whole floor, plays
// with its toy if it has one it plays with, and goes to bed by its own clock when no computer is talking to it.
#pragma once
#include <cstdint>
#include "body/looks.h"

namespace nestbox {

class FreeTime {
public:
    // The first stroll comes after 2-4 min of quiet and lasts 40-90 s; the next follow every 3-6 min.
    static constexpr uint32_t STROLL_FIRST_MIN = 120000, STROLL_FIRST_MAX = 240000;
    static constexpr uint32_t STROLL_MIN = 40000, STROLL_MAX = 90000;
    static constexpr uint32_t STROLL_GAP_MIN = 180000, STROLL_GAP_MAX = 360000;
    // A game: the first after 1-2.5 min of quiet, the next 3-6 min after one ends; one never runs past PLAY_MAX.
    static constexpr uint32_t PLAY_FIRST_MIN = 60000, PLAY_FIRST_MAX = 150000;
    static constexpr uint32_t PLAY_GAP_MIN = 180000, PLAY_GAP_MAX = 360000;
    static constexpr uint32_t PLAY_MAX = 90000;

    struct Choice {
        Look look;
        bool play = false;               // a game: the brain hands the body its throws (look is Look::Act)
    };
    // `companion`: a computer has reported activity; `bedtime`: its own clock says night; `can_play`: it has a toy
    // it plays with.
    Choice choose(uint32_t now_ms, bool companion, bool bedtime, bool can_play = false);
    // The game ended: the next one comes after a gap.
    void played(uint32_t now_ms);
    // A pastime now, whatever the time (the simulator's P key).
    void play_now(uint32_t now_ms);
    uint32_t random_between(uint32_t lo, uint32_t hi);

private:
    bool active_ = false;
    uint32_t stroll_until_ = 0, next_stroll_ = 0;
    uint32_t play_until_ = 0, next_play_ = 0;
    uint32_t rng_ = 2463534242u;         // xorshift32 state (Marsaglia's example seed)
};

}  // namespace nestbox
