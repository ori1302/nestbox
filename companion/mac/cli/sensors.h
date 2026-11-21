// What the Mac is doing, read from system counters that need no permission.
#pragma once
#include <cstdint>
#include <string>

namespace companion {

// The system-wide count of key presses so far. It reveals no keys.
uint32_t key_presses();

// Seconds since the last keyboard or mouse input.
double idle_seconds();

bool screen_locked();

// This Mac's host name up to the first dot, at most 24 characters ("mac" if it has none): how it signs its reports.
std::string short_host();

}  // namespace companion
