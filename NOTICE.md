# Notices

**The code** is MIT licensed (see `LICENSE`).

**The pets' art** in `pets/` (the Goose, Milo and Robot eggs; in `pets/retro/`, the Nibble and Slime eggs and the
folder Nibble is packed from) and the animation in `assets/` are the NestBox authors' own, licensed CC BY 4.0
(https://creativecommons.org/licenses/by/4.0/). Each egg also carries its own credit and licence (`credit` and
`license` in its description), and `GET /v1/pets` shows them.

**The text font** is DejaVu Sans Bold, rendered without anti-aliasing by `tools/fontgen.py`. Its licence (Bitstream
Vera and Arev terms) is in `engine/screen/FONT-LICENSE.txt` and must be distributed with any copy.

**The firmware** (`firmware/esp32/`) is built with PlatformIO and links, among others:

| Component | Licence |
|---|---|
| Arduino core for the ESP32 (arduino-esp32) | LGPL-2.1-or-later |
| ESP-IDF, including its mDNS, Wi-Fi and LittleFS support | Apache-2.0 (and the licences of its components) |
| TinyUSB (the USB stack) | MIT |

A distributed firmware image contains these libraries. The LGPL requires that recipients can relink it with a
modified Arduino core; this source tree with `platformio.ini` provides that. Distribute this notice and the licences
with the image.

**Time zones**: `engine/util/tzdata.cpp` is generated from the IANA time zone database (public domain) by
`tools/tzdata.py`.

**The panel set-up values** in `firmware/esp32/src/lcd.cpp` (power, porch and gamma registers) follow Waveshare's
examples for these boards.

**The simulator** links SDL2 (zlib licence). The Mac app uses only Apple's frameworks.

**Other pets**: an egg keeps the licence of the art it was made from. The firmware build leaves out eggs whose art
may not be shared unless told otherwise (https://nestbox.pet/docs/firmware/#which-pets-go-on-it).
