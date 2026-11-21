// The device's frame, shared by the firmware and the simulator: one call per frame advances setup, brain and pet
// and draws the result into the device's screen canvas.
#pragma once
#include <cstdint>
#include <string>
#include "door/api.h"

namespace nestbox {

constexpr int FPS = 30;
constexpr uint32_t FRAME_US = 1000000 / FPS;

// A line along the bottom of the screen, shown only while the brain has nothing to say.
struct Caption {
    std::string text;
    uint16_t color = 0;
};

// Advances `d` to now_ms and draws it into d.screen, unless the screen is resting off (d.screen_state; see
// door/rest.h): then the platform turns the panel off too. Returns the look the brain decided on.
Look step_frame(Device& d, uint32_t now_ms, const Caption& caption = {});

// Remembers the last decided look, to log only changes.
class LookLog {
public:
    // A log line ("    12.3s  type         the user is typing\n") when `s` differs from the last call, else "".
    std::string line(Look s, uint32_t now_ms, const std::string& why);

private:
    Look last_ = Look::Count;
};

}  // namespace nestbox
