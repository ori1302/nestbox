// The screen as an image file, for tests and screenshots.
#pragma once
#include <string>
#include "screen/canvas.h"

namespace nestbox {

// A 24-bit BMP (GET /screen.bmp).
std::string bmp_image(const Canvas& c);
// Writes a binary PPM (P6); false if the file cannot be written.
bool write_ppm(const Canvas& c, const std::string& path);

}  // namespace nestbox
