// The pet's standing frame, and words in the screen's own font, as SVGs of crisp pixels for the device's pages.
#pragma once
#include <string>
#include "egg/egg.h"

namespace nestbox {

// The first frame of the egg's standing animation, trimmed, centred and standing on the bottom of a square.
std::string pet_svg(const Egg* egg);

// `text` in the screen's font at `size` (1-3, see screen/font.h), drawn in currentColor; "" unless every character
// has a glyph (printable ASCII), so the page can fall back to its own type.
std::string text_svg(const std::string& text, int size);

}  // namespace nestbox
