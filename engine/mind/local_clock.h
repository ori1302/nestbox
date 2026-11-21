// The user's local wall-clock time, from the device's own clock (Wi-Fi time, or setup from a phone).
#pragma once
#include <cstdint>
#include <optional>

namespace nestbox {

class LocalClock {
public:
    // Local time in seconds since 1970 as of `now_ms`.
    void set(int64_t local_seconds, uint32_t now_ms) {
        local_s_ = local_seconds;
        set_at_ = now_ms;
    }
    // Minutes after local midnight; none until the time is known.
    std::optional<int> minute_of_day(uint32_t now_ms) const;

private:
    int64_t local_s_ = -1;               // -1: unknown
    uint32_t set_at_ = 0;
};

}  // namespace nestbox
