// Times and deadlines on a millisecond clock that wraps every 49.7 days (millis() on the device). Values are
// compared by their difference, so they keep working across the wrap. 0 means "not set".
#pragma once
#include <cstdint>

namespace nestbox {

inline bool is_before(uint32_t a, uint32_t b) { return int32_t(a - b) < 0; }
inline bool is_after(uint32_t a, uint32_t b) { return int32_t(a - b) > 0; }

// True while the deadline `until` is set and still ahead of `now`.
inline bool live(uint32_t until, uint32_t now) { return until != 0 && is_after(until, now); }

// `now` as a timestamp that is never 0 (0 means "not set").
inline uint32_t stamp(uint32_t now) { return now ? now : 1; }

// The deadline `ms` after `now`.
inline uint32_t deadline(uint32_t now, uint32_t ms) { return stamp(now + ms); }

}  // namespace nestbox
