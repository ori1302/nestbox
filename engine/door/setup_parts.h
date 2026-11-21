// Helpers shared by the setup's source files.
#pragma once
#include <string>
#include "screen/font.h"

namespace nestbox {

constexpr int WEAK_RSSI = -80;           // dBm; below this a failed join is blamed on the signal

// Signal strength as 1-4 bars for the setup page.
inline int signal_bars(int rssi) { return rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1; }

// A network name the screen can show: long or non-ASCII names become "your Wi-Fi" (the phone shows them).
inline std::string screen_name(const std::string& ssid) {
    return drawable(ssid) && !ssid.empty() && ssid.size() <= 20 ? ssid : "your Wi-Fi";
}

}  // namespace nestbox
