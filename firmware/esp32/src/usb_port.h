// The USB-C port, through TinyUSB on the S3's USB-OTG controller: one composite device ("NestBox") with a serial
// console and a small read-only drive (NESTBOX: the Mac app and a README). It also watches the bus: a computer
// suspends its USB ports when it sleeps and resumes them when it wakes.
// (Not usb.h: on a case-insensitive file system that would shadow the framework's USB.h.)
#pragma once
#include <USBCDC.h>
#include <cstdint>

extern USBCDC console;                     // the USB serial console (pio device monitor)

namespace usb {

constexpr uint32_t ASLEEP_MS = 3000;       // suspended this long counts as asleep (enumeration has short gaps)

// Starts USB. Returns false if the drive image is missing (`pio run -t upload` writes it).
bool begin();
// The computer that enumerated the device has suspended the bus. Never true on a charger, which never enumerates.
bool host_asleep(uint32_t now_ms);
const char* status();                      // one line for the console

}  // namespace usb
