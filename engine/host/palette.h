// Colours the hosts draw with outside the pet's own screens.
#pragma once
#include <cstdint>
#include "screen/canvas.h"

namespace nestbox {

constexpr uint16_t HOST_DARK = rgb(0x17, 0x19, 0x1c);     // the simulator's bezel, the firmware's message screen
constexpr uint16_t HOST_TEXT = rgb(0xe8, 0xee, 0xe0);
constexpr uint16_t HOST_ACCENT = rgb(0xd9, 0x77, 0x57);

}  // namespace nestbox
