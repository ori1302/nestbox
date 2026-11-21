#include "door/rest.h"
#include <algorithm>

namespace nestbox {

namespace {
constexpr uint32_t MINUTE_MS = 60 * 1000;

// The user's rest, shortened to `quick` at most; "never" (0) stays never.
int sooner(int a, int quick) { return a == 0 ? 0 : std::min(a, quick); }
}  // namespace

bool in_night(const ScreenSettings& s, int minute) {
    if (s.night_from < 0 || s.night_to < 0 || s.night_from == s.night_to) return false;
    if (s.night_from < s.night_to) return minute >= s.night_from && minute < s.night_to;
    return minute >= s.night_from || minute < s.night_to;
}

ScreenState rest_state(const ScreenSettings& s, uint32_t idle_ms, bool away, std::optional<int> minute) {
    int dim_after = s.dim_after, off_after = s.off_after;
    if (away) {
        dim_after = sooner(dim_after, AWAY_DIM_AFTER);
        off_after = sooner(off_after, AWAY_OFF_AFTER);
    }
    if (minute && in_night(s, *minute)) {
        dim_after = sooner(dim_after, NIGHT_DIM_AFTER);
        off_after = sooner(off_after, NIGHT_OFF_AFTER);
    }
    if (off_after > 0 && idle_ms >= uint32_t(off_after) * MINUTE_MS) return ScreenState::Off;
    if (dim_after > 0 && idle_ms >= uint32_t(dim_after) * MINUTE_MS) return ScreenState::Dim;
    return ScreenState::Awake;
}

int backlight_percent(const ScreenSettings& s, ScreenState state, bool handheld) {
    if (state == ScreenState::Off) return 0;
    int bright = std::clamp(s.bright, 1, 100);
    if (handheld) bright = std::clamp(bright * ScreenSettings::HANDHELD_BRIGHT / ScreenSettings::BRIGHT, 1, 100);
    return state == ScreenState::Dim ? std::min(bright, std::clamp(s.dim, 1, 100)) : bright;
}

ScreenSettings screen_defaults(const Display& d) {
    ScreenSettings s;
    if (d.oled) {
        s.bright = OLED_BRIGHT;
        s.dim = OLED_DIM;
        s.dim_after = OLED_DIM_AFTER;
        s.off_after = OLED_OFF_AFTER;
        s.care = true;
    }
    return s;
}

Shift pixel_shift(const ScreenSettings& s, uint32_t now) {
    static const Shift ROUND[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}};
    if (!s.care) return {};
    return ROUND[now / SHIFT_MS % (sizeof ROUND / sizeof ROUND[0])];
}

const char* screen_state_key(ScreenState s) {
    switch (s) {
        case ScreenState::Awake: return "awake";
        case ScreenState::Dim: return "dim";
        case ScreenState::Off: return "off";
    }
    return "awake";
}

}  // namespace nestbox
