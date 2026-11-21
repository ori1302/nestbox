// What the pet knows about the user, from the sources that report activity (POST /v1/user): a computer, a second
// computer, a sensor, the device's own USB port. Each report carries counts and fixed words only: key presses since
// the last report, seconds since any input, whether the screen is awake, locked or asleep, the local time, and when
// the next report will come. A source that goes silent for twice its promised interval is gone. The pet merges the
// sources: the user is typing if any source says so, and away only if every source says so or has gone.
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "util/json.h"

namespace nestbox {

// The device itself, as a sender of notes and as a source of the user's activity (its USB port).
constexpr const char* DEVICE_SENDER = "device";

class User {
public:
    static constexpr uint32_t NEXT_DEFAULT_S = 10;         // a source reports at least this often unless it says
    static constexpr uint32_t NEXT_MAX_S = 3600;
    static constexpr uint32_t TYPING_AFTER_MS = 1500;      // typing stops this long after the last report with keys
    static constexpr uint32_t PAUSE_MS = 8000;             // after typing, a pause this long keeps it at its desk
    // Typing speed is the keys of the reports from the last RATE_WINDOW_MS of the current stretch of typing, over
    // the time those reports cover, and never over less than RATE_MIN_MS (so a quick word alone is not fast
    // typing: words come in bursts).
    static constexpr uint32_t RATE_MIN_MS = 1500, RATE_WINDOW_MS = 3000;
    static constexpr float AWAY_S = 60;                    // no input this long: the user stepped away
    static constexpr float LONG_AWAY_S = 300;              // no input this long: the pet goes to sleep
    static constexpr size_t MAX_SOURCES = 8;
    static constexpr size_t MAX_NAME = 24;

    // What one report did: "" if fine, else a warning for each key it could not use.
    std::vector<std::string> report(const Fields& f, uint32_t now_ms);
    // The device's USB port: the computer it is plugged into slept or woke.
    void computer(bool asleep, uint32_t now_ms);

    // Any source has reported and not gone.
    bool heard(uint32_t now_ms) const;
    // Every source that ever reported has gone silent.
    bool all_gone(uint32_t now_ms) const;
    bool typing(uint32_t now_ms) const;
    // Typing fast: the speed held at FAST_KPS for FAST_HOLD_MS, until it stays under CALM_KPS for CALM_MS or typing
    // stops. Call once per frame with the current `typing`.
    bool typing_fast(bool typing, uint32_t now_ms);
    // A pause in the typing short enough to stay at the desk.
    bool pausing(uint32_t now_ms) const;
    // The user is at the computer, as far as the sources tell; true with no source at all.
    bool there(uint32_t now_ms) const;
    // The screen is locked or asleep everywhere, or no input for LONG_AWAY_S: the pet sleeps.
    bool asleep(uint32_t now_ms) const;
    bool locked(uint32_t now_ms) const;
    // Keys per second, as RATE_WINDOW_MS says, the fastest source's; 0 when unknown. A report's keys cover the time
    // since the one before, so the speed holds steady between reports and when one leaves the window.
    float kps(uint32_t now_ms) const;
    // One word for replies: typing, typing-fast, paused, here, away, asleep or unknown.
    std::string word(uint32_t now_ms) const;
    // The local time, in minutes after midnight, from the newest report that had one; none if none did.
    std::optional<int> minute_of_day(uint32_t now_ms) const;

private:
    struct Keys {
        uint32_t from = 0, when = 0;     // the report before, and this one: the keys were pressed between them
        uint32_t keys = 0;
    };
    struct Source {
        uint32_t last = 0;               // when it last reported
        uint32_t next_ms = NEXT_DEFAULT_S * 1000;
        uint32_t last_keys = 0;          // when a report last had keys in it; 0 once one had none
        uint32_t typed_at = 0;           // when a report last had keys in it, kept
        uint32_t run_from = 0;           // the first report with keys after one without
        std::deque<Keys> recent;         // the reports with keys in the rate's window
        bool keys_known = false;
        float idle_s = 0;
        bool idle_known = false;
        std::string state = "awake";     // awake, locked, asleep
        int minute = -1;                 // minutes after midnight at `last`; -1: none
        uint32_t minute_at = 0;
    };
    bool gone(const Source& s, uint32_t now_ms) const;
    bool source_away(const Source& s, uint32_t now_ms) const;

    std::map<std::string, Source> sources_;
    bool fast_ = false;
    uint32_t calm_since_ = 0, fast_since_ = 0;   // under CALM_KPS since; at FAST_KPS since (0: not)
};

}  // namespace nestbox
