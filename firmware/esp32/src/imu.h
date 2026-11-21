// The QMI8658 motion sensor: a tap on the case and a shake, read by polling the accelerometer.
#pragma once
#include <atomic>
#include <cstdint>

namespace imu {

bool begin();                       // false if no QMI8658 answers on I2C
// Call about 200 times a second. Returns "tap", "shake" or nullptr.
const char* poll(uint32_t now_ms);
extern std::atomic<bool> debug;     // log every bump and swing, to tune the thresholds

}  // namespace imu
