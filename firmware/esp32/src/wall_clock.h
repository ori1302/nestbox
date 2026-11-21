// The device's own clock: internet time (SNTP) once Wi-Fi is up, read in the configured time zone (a POSIX TZ rule).
#pragma once
#include <cstdint>
#include <string>

namespace wall_clock {

void start();                                // once Wi-Fi is up; later calls do nothing
void set_tz(const std::string& posix_tz);   // "": none
void set_clock(int64_t unix_s);             // time from the setup phone, until SNTP answers
bool synced();
bool just_synced();                         // true once after each sync or set_clock()
// Local time as seconds since 1970 (the wall-clock fields read as UTC); false while unknown or with no time zone.
bool local(int64_t& out);
std::string status();                        // one line for the console

}  // namespace wall_clock
