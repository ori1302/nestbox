// A push button (PWR or BOOT), debounced: single, double and long presses from its level.
#pragma once
#include <cstdint>

class Button {
public:
    static constexpr uint32_t DEBOUNCE_MS = 20, DOUBLE_MS = 350, LONG_MS = 1000;

    // Call often (every few ms) with whether it is held down now. Returns "single", "double", "long" or nullptr.
    const char* poll(bool down, uint32_t now_ms);
    // Down now, after debouncing (and after the first release since start).
    bool held() const { return armed_ && held_; }

private:
    bool armed_ = false;        // seen released once: the press that switched the board on doesn't count
    bool raw_ = false, held_ = false, long_sent_ = false;
    uint32_t raw_since_ = 0, press_ms_ = 0, release_ms_ = 0;
    int clicks_ = 0;
};
