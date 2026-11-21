#include "wall_clock.h"
#include <Arduino.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <atomic>
#include <cstdio>
#include <ctime>

namespace wall_clock {

namespace {
const char* const SERVERS[] = {"pool.ntp.org", "time.google.com", "time.apple.com"};
constexpr time_t EARLIEST_VALID = 1700000000;    // 2023-11-14: an earlier clock has not been set
constexpr int64_t DAY_S = 86400;

std::string tz_;
bool started = false;
std::atomic<bool> fresh{false};

void on_sync(struct timeval*) { fresh = true; }

void apply() {
    setenv("TZ", tz_.empty() ? "UTC0" : tz_.c_str(), 1);
    tzset();
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's days_from_civil).
int64_t days(int y, int m, int d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = int(y - era * 400), doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

std::string zone_text() { return tz_.empty() ? "no time zone: set one with tz" : "zone " + tz_; }
}  // namespace

void start() {
    if (started) return;
    started = true;
    esp_sntp_set_time_sync_notification_cb(on_sync);
    configTime(0, 0, SERVERS[0], SERVERS[1], SERVERS[2]);
    apply();                                // configTime sets its own TZ
}

void set_tz(const std::string& posix_tz) {
    tz_ = posix_tz;
    apply();
}

void set_clock(int64_t unix_s) {
    timeval tv{time_t(unix_s), 0};
    settimeofday(&tv, nullptr);
    fresh = true;
}

bool synced() { return time(nullptr) >= EARLIEST_VALID; }
bool just_synced() { return fresh.exchange(false); }

bool local(int64_t& out) {
    const time_t now = time(nullptr);
    if (now < EARLIEST_VALID || tz_.empty()) return false;
    struct tm t;
    localtime_r(&now, &t);
    out = days(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * DAY_S + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    return true;
}

std::string status() {
    if (!synced()) return started ? "time: waiting for internet time" : "time: no Wi-Fi yet";
    const std::string zone = zone_text();
    int64_t l;
    if (!local(l)) return "time: synced; " + zone;
    const int s = int(((l % DAY_S) + DAY_S) % DAY_S);
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    return "time: " + std::string(buf) + " local; " + zone;
}

}  // namespace wall_clock
