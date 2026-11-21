// The companion loop: samples the Mac's activity and reports it to the pet's POST /v1/user, with the key from
// pairing. It reports at once when typing starts or stops, four times a second while it goes on, and otherwise
// every few seconds, promising the next report within NEXT_S.
//
// Fields (each optional one is left out when its signal is switched off):
//   from    this Mac's short name
//   keys    key presses since the last report, a count only
//   idle    seconds since the last keyboard or mouse input
//   state   "awake", or "locked" while the screen is locked
//   time    the local time, "14:32", so the pet's scene follows the time of day
//   next    the next report comes within this many seconds
// The stream doubles as a heartbeat: when the Mac sleeps it stops, and after twice `next` the pet sleeps too.
//
// With --machine it also prints, for the Mac app (companion/mac/app/HelperEvent.swift), one JSON object per line
// after MACHINE_TAG: {"event": "found" | "paired" | "connected" | "lost" | "sent" | "needs_key", ...}.
#pragma once
#include <string>

namespace companion {

struct CompanionOptions {
    std::string device;    // a fixed URL; no Bonjour
    std::string name;      // a Bonjour service name
    std::string id;        // the paired pet's TXT id
    bool id_given = false;
    bool print = false;    // echo each body with the pet's status and reply
    bool machine = false;  // also print tagged JSON lines
    bool keys = true, idle = true, lock = true;
};

constexpr const char* MACHINE_TAG = "@nestbox ";

[[noreturn]] void run_companion(CompanionOptions options);

}  // namespace companion
