// A small ST7789 driver for the board's panel, used sideways (280x240 on the LCD-1.69, 320x240 on the LCD-2): full
// frames of RGB565 over SPI DMA.
#pragma once
#include <cstdint>
#include "board.h"

namespace lcd {

constexpr int W = board::LCD_W, H = board::LCD_H;

// Resets and sets up the panel; false if the SPI bus or the frame buffer could not be set up. flip: turn the
// picture upside down (which way is up depends on the case).
bool begin(bool flip);
void set_flip(bool flip);
void backlight(int percent);   // 0..100
// Puts the panel to sleep (its display off, then sleep mode) or wakes it; the frame after waking is sent whole.
void sleep(bool asleep);

// Blocks until the frame sent last is all on the panel.
void wait_idle();
// Sends one W x H frame (native RGB565, as the engine's Canvas holds it): waits for the frame before to finish,
// then returns while a task on the other core sends what changed, a strip at a time with bytes swapped. `held`
// (H * SLICES, or null) gets px's slice fingerprints (Canvas::held). Leave both as they are until wait_idle()
// returns or the next push(). Does nothing before begin().
void push(const uint16_t* px, uint32_t* held = nullptr);

// Frames sent and how many of their pixels went to the panel since the last call (the rest were unchanged).
void take_send_stats(uint32_t& frames, uint64_t& pixels);

}  // namespace lcd
