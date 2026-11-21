#include "mind/free_time.h"
#include "util/deadline.h"

namespace nestbox {

uint32_t FreeTime::random_between(uint32_t lo, uint32_t hi) {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return lo + rng_ % (hi - lo + 1);
}

FreeTime::Choice FreeTime::choose(uint32_t now, bool companion, bool bedtime, bool can_play) {
    if (!companion && bedtime) {
        active_ = false;
        return {Look::Sleep};
    }
    if (!active_) {
        active_ = true;
        stroll_until_ = play_until_ = 0;
        next_stroll_ = deadline(now, random_between(STROLL_FIRST_MIN, STROLL_FIRST_MAX));
        next_play_ = deadline(now, random_between(PLAY_FIRST_MIN, PLAY_FIRST_MAX));
    }
    if (play_until_ && !live(play_until_, now)) played(now);   // a game cut off at PLAY_MAX
    if (can_play && !live(play_until_, now) && !live(next_play_, now) && !live(stroll_until_, now))
        play_until_ = deadline(now, PLAY_MAX);
    if (can_play && live(play_until_, now)) return {Look::Act, true};
    if (!live(stroll_until_, now) && !live(next_stroll_, now)) {
        stroll_until_ = deadline(now, random_between(STROLL_MIN, STROLL_MAX));
        next_stroll_ = deadline(stroll_until_, random_between(STROLL_GAP_MIN, STROLL_GAP_MAX));
    }
    if (live(stroll_until_, now)) return {Look::Wander};
    return {Look::Idle};
}

void FreeTime::play_now(uint32_t now) {
    active_ = true;
    stroll_until_ = 0;
    play_until_ = deadline(now, PLAY_MAX);
}

void FreeTime::played(uint32_t now) {
    play_until_ = 0;
    next_play_ = deadline(now, random_between(PLAY_GAP_MIN, PLAY_GAP_MAX));
}

}  // namespace nestbox
