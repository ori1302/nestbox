// Log lines from any task. Only the service task writes them to USB (drain_log), so a slow or absent host does not
// stall a frame.
#pragma once
#include <string>

void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Takes every line logged since the last call.
std::string drain_log();
