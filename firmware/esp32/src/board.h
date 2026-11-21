// Pins of the two boards NestBox runs on, both without touch: the Waveshare ESP32-S3-LCD-1.69 (the default) and the
// ESP32-S3-LCD-2 (NESTBOX_BOARD_LCD2). Each pin was matched against the board's schematics and demo code.
#pragma once
#include <cstddef>

namespace board {

#ifdef NESTBOX_BOARD_LCD2
// ST7789 on SPI: a 240x320 panel, the whole of the controller's RAM, used sideways as 320x240. The panel's reset
// line has its own RC and no pin; the firmware resets it by command. The SD card slot shares the bus (SD_CS).
constexpr int LCD_DC = 42, LCD_CS = 45, LCD_SCK = 39, LCD_MOSI = 38, LCD_RST = -1;
constexpr int LCD_BL = 1;             // drives a transistor: high = lit, so PWM works
constexpr int LCD_W = 320, LCD_H = 240;
constexpr int LCD_GAP = 0;
constexpr const char* SCREEN_PROFILE = "lcd-2";   // the engine's screen profile (engine/screen/display.cpp)
constexpr int SD_CS = 41;             // held high: the card stays off the screen's bus

// One I2C bus for the QMI8658 IMU (0x6B; also on the pin header as GPIO47/48).
constexpr int I2C_SDA = 48, I2C_SCL = 47;
constexpr int IMU_ADDR = 0x6B;

constexpr int BOOT = 0;               // the BOOT button, low while pressed; an ordinary input once running

// No PWR button and no power hold: on battery the board runs while the battery is connected. BOOT is the pet's
// button (main.cpp).
constexpr bool HAS_PWR = false;
constexpr int BUZZER = -1, SYS_EN = -1, SYS_OUT = -1;
#else
// ST7789V2 on SPI: a 240x280 panel in the controller's 240x320 RAM; the 280 side starts 20 lines in.
constexpr int LCD_DC = 4, LCD_CS = 5, LCD_SCK = 6, LCD_MOSI = 7, LCD_RST = 8;
constexpr int LCD_BL = 15;            // drives a transistor: high = lit, so PWM works
constexpr int LCD_W = 280, LCD_H = 240;
constexpr int LCD_GAP = 20;
constexpr const char* SCREEN_PROFILE = "lcd-1.69";   // the engine's screen profile (engine/screen/display.cpp)
constexpr int SD_CS = -1;

// One I2C bus for the QMI8658 IMU (0x6B) and the PCF85063 RTC (0x51, unused: the pet runs on millis).
constexpr int I2C_SDA = 11, I2C_SCL = 10;
constexpr int IMU_ADDR = 0x6B;

constexpr int BOOT = 0;               // the BOOT button, low while pressed; an ordinary input once running

#ifdef NESTBOX_BOARD_V1
// First revision: these sit on GPIO33/35/36, which the S3R8's in-package octal PSRAM uses. The firmware needs PSRAM,
// so it leaves them alone: no PWR button and no power hold on battery.
constexpr bool HAS_PWR = false;
constexpr int BUZZER = 33, SYS_EN = 35, SYS_OUT = 36;
#else
// V2 ("V2" on the QC sticker): PWR switches the battery on, then SYS_EN must stay high or the board switches off;
// the button reads on SYS_OUT (low while pressed).
constexpr bool HAS_PWR = true;
constexpr int BUZZER = 42, SYS_EN = 41, SYS_OUT = 40;
#endif
#endif

// PSRAM left free after loading eggs, at boot and when a pet is added, for the rest of the firmware.
constexpr size_t PSRAM_SPARE = 512 * 1024;

}  // namespace board
