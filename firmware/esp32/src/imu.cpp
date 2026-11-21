#include "imu.h"
#include <Arduino.h>
#include <Wire.h>
#include <algorithm>
#include <cmath>
#include "board.h"
#include "log.h"

namespace imu {

std::atomic<bool> debug{false};

namespace {
// Registers and values, from the QMI8658 datasheet as SensorLib (used by the board's demos) uses them.
constexpr uint8_t WHO_AM_I = 0x00, CTRL1 = 0x02, CTRL2 = 0x03, CTRL7 = 0x08, AX_L = 0x35, RESET = 0x60;
constexpr uint8_t CHIP_ID = 0x05, RESET_VALUE = 0xB0;
constexpr uint8_t CTRL1_AUTO_INCREMENT = 0x40;           // little-endian, register address auto-increment
constexpr uint8_t CTRL2_8G_500HZ = (2 << 4) | 4;
constexpr uint8_t CTRL7_ACCEL_ONLY = 0x01;
constexpr uint32_t I2C_HZ = 400000;
constexpr float LSB_PER_G = 4096;            // at +-8 g

// Thresholds for a board in a printed case; `imu` on the console logs each bump and swing to tune them by.
constexpr float QUIET_G = 0.12f;             // below this the board is still
constexpr float TAP_G = 0.45f;               // a tap is a jolt over this,
constexpr uint32_t TAP_MAX_MS = 80;          // over within this long,
constexpr uint32_t CALM_MS = 300;            // after the board was still for this long,
constexpr uint32_t TAP_WAIT_MS = 250;        // and not followed by a shake within this long
constexpr uint32_t BUMP_END_MS = 40;         // below TAP_G this long ends a jolt
constexpr float SHAKE_G = 1.0f;              // a shake is SHAKE_SWINGS swings over SHAKE_G within SHAKE_MS
constexpr int SHAKE_SWINGS = 4;
constexpr uint32_t SHAKE_MS = 1200, SWING_GAP_MS = 100;
constexpr int SHAKE_TAP_SWINGS = 2;          // a jolt with this many recent swings is part of a shake, not a tap
constexpr uint32_t COOLDOWN_MS = 800;        // one gesture, one event
constexpr float GRAVITY_FOLLOW = 0.02f;      // per poll: about 250 ms at 200 polls a second

bool write(uint8_t reg, uint8_t v) {
    Wire.beginTransmission(board::IMU_ADDR);
    Wire.write(reg);
    Wire.write(v);
    return Wire.endTransmission() == 0;
}

bool read(uint8_t reg, uint8_t* out, size_t n) {
    Wire.beginTransmission(board::IMU_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(uint8_t(board::IMU_ADDR), uint8_t(n)) != n) return false;
    for (size_t i = 0; i < n; i++) out[i] = uint8_t(Wire.read());
    return true;
}

bool ok = false, primed = false;
float gx = 0, gy = 0, gz = 0;                // gravity, followed slowly; the rest is motion
uint32_t calm_since = 0;                     // 0 = moving now
uint32_t bump_start = 0, bump_last = 0;      // the jolt in progress (0 = none)
bool bump_calm = false;
float bump_peak = 0;
uint32_t tap_at = 0;                         // a tap waiting to be sure it wasn't the start of a shake
uint32_t swings[SHAKE_SWINGS] = {};
int swing_n = 0;
uint32_t quiet_until = 0;

int recent_swings(uint32_t now) {
    int n = 0;
    for (int i = 0; i < SHAKE_SWINGS; i++)
        if (swings[i] && now - swings[i] < SHAKE_MS) n++;
    return n;
}
}  // namespace

bool begin() {
    Wire.begin(board::I2C_SDA, board::I2C_SCL, I2C_HZ);
    uint8_t id = 0;
    if (!read(WHO_AM_I, &id, 1) || id != CHIP_ID) return false;
    write(RESET, RESET_VALUE);
    delay(20);
    ok = write(CTRL1, CTRL1_AUTO_INCREMENT) && write(CTRL2, CTRL2_8G_500HZ) && write(CTRL7, CTRL7_ACCEL_ONLY);
    delay(10);
    return ok;
}

const char* poll(uint32_t now) {
    uint8_t b[6];
    if (!ok || !read(AX_L, b, 6)) return nullptr;
    float ax = int16_t(b[0] | b[1] << 8) / LSB_PER_G, ay = int16_t(b[2] | b[3] << 8) / LSB_PER_G,
          az = int16_t(b[4] | b[5] << 8) / LSB_PER_G;
    if (!primed) {
        gx = ax;
        gy = ay;
        gz = az;
        primed = true;
        calm_since = now;
    }
    gx += (ax - gx) * GRAVITY_FOLLOW;
    gy += (ay - gy) * GRAVITY_FOLLOW;
    gz += (az - gz) * GRAVITY_FOLLOW;
    float dx = ax - gx, dy = ay - gy, dz = az - gz;
    float m = std::sqrt(dx * dx + dy * dy + dz * dz);

    bool was_calm = calm_since && now - calm_since >= CALM_MS;   // before this sample
    if (m >= QUIET_G) calm_since = 0;
    else if (!calm_since) calm_since = now;

    if (m > SHAKE_G && (!swing_n || now - swings[(swing_n - 1) % SHAKE_SWINGS] >= SWING_GAP_MS)) {
        swings[swing_n++ % SHAKE_SWINGS] = now;
        if (debug) note("imu: swing %.2f g (%d in %u ms)\n", m, recent_swings(now), unsigned(SHAKE_MS));
        if (recent_swings(now) >= SHAKE_SWINGS && int32_t(now - quiet_until) >= 0) {
            for (auto& s : swings) s = 0;
            tap_at = 0;
            quiet_until = now + COOLDOWN_MS;
            return "shake";
        }
    }

    if (m > TAP_G) {
        if (!bump_start) {
            bump_start = now;
            bump_peak = 0;
            bump_calm = was_calm;
        }
        bump_last = now;
        bump_peak = std::max(bump_peak, m);
    } else if (bump_start && now - bump_last > BUMP_END_MS) {
        uint32_t len = bump_last - bump_start;
        if (debug) note("imu: bump %.2f g for %u ms%s\n", bump_peak, unsigned(len), bump_calm ? "" : " (was moving)");
        if (bump_calm && len <= TAP_MAX_MS) tap_at = now + TAP_WAIT_MS;
        bump_start = 0;
    }

    if (tap_at && int32_t(now - tap_at) >= 0) {
        tap_at = 0;
        if (recent_swings(now) < SHAKE_TAP_SWINGS && int32_t(now - quiet_until) >= 0) {
            quiet_until = now + COOLDOWN_MS;
            return "tap";
        }
    }
    return nullptr;
}

}  // namespace imu
