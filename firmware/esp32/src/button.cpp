#include "button.h"

const char* Button::poll(bool down, uint32_t now) {
    if (down != raw_) { raw_ = down; raw_since_ = now; }
    if (now - raw_since_ < DEBOUNCE_MS) return nullptr;
    if (!armed_) { armed_ = !raw_; return nullptr; }

    if (raw_ != held_) {
        held_ = raw_;
        if (held_) { press_ms_ = now; long_sent_ = false; }
        else if (!long_sent_) { clicks_++; release_ms_ = now; }
        if (!held_ && clicks_ >= 2) { clicks_ = 0; return "double"; }
    }
    if (held_ && !long_sent_ && now - press_ms_ >= LONG_MS) {   // fires while still held
        long_sent_ = true;
        clicks_ = 0;
        return "long";
    }
    if (!held_ && clicks_ == 1 && now - release_ms_ >= DOUBLE_MS) { clicks_ = 0; return "single"; }
    return nullptr;
}
