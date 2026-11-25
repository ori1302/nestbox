// Pictures for the device's pages: a pet's standing frame (Egg::portrait) and words in the screen's own font.
#pragma once
#include <string>
#include "egg/egg.h"

namespace nestbox {

// The egg's portrait, centred and standing on the bottom of a square, as an SVG of crisp pixels.
std::string pet_svg(const Egg* egg);
// The egg's portrait as a PNG at its own pixels; "" without one.
std::string pet_png(const Egg* egg);

// `text` in the screen's font at `size` (1-3, see screen/font.h), drawn in currentColor; "" unless every character
// has a glyph (printable ASCII), so the page can fall back to its own type. `scale` sets its size on the page in whole
// pixels per pixel of the font.
std::string text_svg(const std::string& text, int size, int scale = 1);

}  // namespace nestbox
