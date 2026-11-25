// Egg files on a device: which files are eggs, the order they go on the shelf, and the pet that comes out first.
#pragma once
#include <cstdint>
#include <string>
#include "door/api.h"
#include "screen/display.h"

namespace nestbox {

constexpr const char* EGG_EXTENSION = ".egg";
constexpr const char* PART_SUFFIX = ".part";      // after an egg's file name while it is being written
constexpr const char* DEFAULT_PET = "nibble";

bool is_egg_file(const std::string& file_name);
// The shelf id of an egg file: its name without the extension ("nibble.egg" -> "nibble").
std::string egg_id(const std::string& file_name);
// Shelf order for egg file names: the default pet first, then by name.
bool egg_file_before(const std::string& a, const std::string& b);

// Shows the pet last chosen (`saved_id`; the first on the shelf if it is gone, and the next one if it can't be
// loaded) on display `display`, fading in from what d.screen holds. The shelf must not be empty.
void put_out_first(Device& d, const Display& display, const std::string& saved_id, uint32_t now_ms);

}  // namespace nestbox
