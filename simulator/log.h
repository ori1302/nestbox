// Log lines, to stdout, flushed at once (tests read the simulator's output as it runs).
#pragma once
#include <string>

void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// With --machine: one JSON object per line after this tag, for the Mac app (companion/mac/app/SimulatorEvent.swift).
constexpr const char* MACHINE_TAG = "@nestbox ";
void set_machine_lines(bool on);
// Prints MACHINE_TAG + json when machine lines are on.
void machine_line(const std::string& json);
