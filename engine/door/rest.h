// When the screen rests: awake at its brightness, dimmed after a while with nothing going on, then off. The idle time
// runs from the last activity (a tell, a report of the user at the computer, a press, a tap, a device change); the pet
// keeps living while the screen rests, and any activity wakes it. Backlights age with hours lit, and an AMOLED's
// pixels with hours shown, so the defaults rest the screen once nobody has been around for a while.
#pragma once
#include <cstdint>
#include <optional>
#include "screen/display.h"

namespace nestbox {

enum class ScreenState { Awake, Dim, Off };

struct ScreenSettings {
    static constexpr int BRIGHT = 70;          // percent of the backlight while awake
    static constexpr int HANDHELD_BRIGHT = 50; // with a pet drawn in a handheld's few colours, which glare when bright
    static constexpr int DIM = 20;             // percent of the backlight while dimmed
    static constexpr int DIM_AFTER = 60;       // minutes
    static constexpr int OFF_AFTER = 180;      // minutes

    int bright = BRIGHT;
    int dim = DIM;
    int dim_after = DIM_AFTER;                 // minutes of nothing going on before dimming; 0: never
    int off_after = OFF_AFTER;                 // minutes before the screen goes off; 0: never
    int night_from = -1, night_to = -1;        // night hours, minutes after midnight; -1: none
    // Burn-in care, for screens whose pixels light themselves: the picture moves a pixel now and then, and the
    // status bar and gauges leave the screen while they stay the same (see pixel_shift() and Pet::care()).
    bool care = false;
};

// An OLED screen rests sooner and less bright, with burn-in care on; an LCD keeps the ScreenSettings defaults.
constexpr int OLED_BRIGHT = 55, OLED_DIM = 15, OLED_DIM_AFTER = 15, OLED_OFF_AFTER = 60;
ScreenSettings screen_defaults(const Display& d);

// Burn-in care moves the whole picture by a screen pixel, one step round a small square every SHIFT_MS.
constexpr uint32_t SHIFT_MS = 3 * 60 * 1000;
struct Shift {
    int x = 0, y = 0;
};
// Where the platform shows the picture now, in the engine's pixels from where it would be ({0, 0} without care).
Shift pixel_shift(const ScreenSettings& s, uint32_t now_ms);

// Quicker rests while nobody is there (every source of activity says the user is away or asleep) and in the night
// hours: whichever is sooner wins. A rest set to never stays never.
constexpr int AWAY_DIM_AFTER = 5, AWAY_OFF_AFTER = 30;     // minutes
constexpr int NIGHT_DIM_AFTER = 1, NIGHT_OFF_AFTER = 10;   // minutes

// Whether `minute` (after midnight) falls in the night hours; a span may cross midnight.
bool in_night(const ScreenSettings& s, int minute);

// The state after `idle_ms` with nothing going on. `away`: nobody at the computer; `minute`: the local time, if known.
ScreenState rest_state(const ScreenSettings& s, uint32_t idle_ms, bool away, std::optional<int> minute);

// The backlight for a state, in percent: `handheld` when the pet out draws in a handheld's few colours (its
// brightness is HANDHELD_BRIGHT scaled by how far the user moved theirs from BRIGHT).
int backlight_percent(const ScreenSettings& s, ScreenState state, bool handheld);

// "awake", "dim" or "off".
const char* screen_state_key(ScreenState s);

}  // namespace nestbox
