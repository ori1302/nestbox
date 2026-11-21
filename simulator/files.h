// The simulator's files: egg folders, and what it remembers between runs in ~/.nestbox.
#pragma once
#include <filesystem>
#include <string>
#include "body/shelf.h"

namespace nestbox {

std::filesystem::path state_dir();                                   // ~/.nestbox
inline std::filesystem::path chosen_pet_file() { return state_dir() / "pet"; }
inline std::filesystem::path window_size_file() { return state_dir() / "window-size"; }
inline std::filesystem::path own_eggs_dir() { return state_dir() / "pets"; }
// The device's settings (key, Lock, text size) for the simulator called `name`.
inline std::filesystem::path settings_file(const std::string& name) {
    return state_dir() / ("settings-" + name + ".json");
}

// Adds every egg in `dir` that is not on the shelf yet, in shelf order; unreadable eggs are reported and skipped.
void load_egg_folder(const std::filesystem::path& dir, Shelf& shelf);

}  // namespace nestbox
