#include "mind/local_clock.h"

namespace nestbox {

namespace {
constexpr int64_t DAY_S = 86400;
}  // namespace

std::optional<int> LocalClock::minute_of_day(uint32_t now) const {
    if (local_s_ < 0) return std::nullopt;
    const int64_t t = local_s_ + int64_t(now - set_at_) / 1000;
    return int((t % DAY_S + DAY_S) % DAY_S / 60);
}

}  // namespace nestbox
