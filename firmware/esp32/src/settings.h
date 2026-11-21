// What the device keeps in flash (NVS) across restarts.
#pragma once
#include <string>
#include "door/rest.h"

namespace settings {

// Keys.
constexpr const char* SSID = "ssid";
constexpr const char* PASSWORD = "pass";
constexpr const char* OFFLINE = "offline";   // "1": setup chose to run without Wi-Fi
constexpr const char* PET_NAME = "name";     // the name given during setup
constexpr const char* TIME_ZONE = "tz";      // POSIX TZ rule
constexpr const char* HOST = "host";         // mDNS name, if renamed
constexpr const char* CHOSEN_PET = "pet";    // shelf id of the pet last out
constexpr const char* FLIP = "flip";
constexpr const char* BRIGHTNESS = "bright";
constexpr const char* API_KEY = "key";       // the key from pairing
constexpr const char* LOCKED = "lock";       // "1": every API request needs the key
constexpr const char* TEXT_SIZE = "text";    // 1-3
constexpr const char* DIM = "dim";           // the screen's rest (door/rest.h): percent while dimmed,
constexpr const char* DIM_AFTER = "dim_after";   // minutes to dim and to go off (0 never),
constexpr const char* OFF_AFTER = "off_after";
constexpr const char* NIGHT_FROM = "night_from";   // and the night hours, minutes after midnight (-1 none)
constexpr const char* NIGHT_TO = "night_to";
constexpr const char* CARE = "care";         // "1": burn-in care (on by default for an OLED screen)

constexpr int DEFAULT_BRIGHTNESS = nestbox::ScreenSettings::BRIGHT;

void begin();
std::string get(const char* key);            // "" if unset
void put(const char* key, const std::string& value);
bool get_flag(const char* key, bool fallback);
void put_flag(const char* key, bool value);
int get_int(const char* key, int fallback);
void put_int(const char* key, int value);
void remove(const char* key);

}  // namespace settings
