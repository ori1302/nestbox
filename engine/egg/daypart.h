// The time of day, for scenes that look different through the day.
#pragma once

namespace nestbox {

enum class Daypart { Morning, Afternoon, Evening, Night };
constexpr Daypart DAYPARTS[] = {Daypart::Morning, Daypart::Afternoon, Daypart::Evening, Daypart::Night};

// "morning", "afternoon", "evening" or "night".
inline const char* daypart_key(Daypart p) {
    switch (p) {
        case Daypart::Morning: return "morning";
        case Daypart::Afternoon: return "afternoon";
        case Daypart::Evening: return "evening";
        case Daypart::Night: break;
    }
    return "night";
}

// How dark evening is, from 0 (day) to 1 (night).
constexpr float EVENING_DARKNESS = 0.4f;

// The part of the day at `minute` after local midnight: morning from 6:00, afternoon from 12:00, evening from 18:00,
// night from 20:30.
inline Daypart daypart_at(int minute) {
    constexpr int MORNING_FROM = 6 * 60, AFTERNOON_FROM = 12 * 60, EVENING_FROM = 18 * 60, NIGHT_FROM = 20 * 60 + 30;
    if (minute >= NIGHT_FROM || minute < MORNING_FROM) return Daypart::Night;
    if (minute >= EVENING_FROM) return Daypart::Evening;
    if (minute >= AFTERNOON_FROM) return Daypart::Afternoon;
    return Daypart::Morning;
}

}  // namespace nestbox
