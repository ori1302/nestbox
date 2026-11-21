// The simulator's command line.
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "screen/display.h"

namespace nestbox {

constexpr const char* DEFAULT_NAME = "nestbox-simulator";

struct Options {
    const Display* display = &default_display();
    std::filesystem::path pets_dir;          // default: pets/ next to the binary
    std::vector<std::string> shot;           // --shot and its arguments, unparsed
    int port = 8080;
    std::string name = DEFAULT_NAME;
    bool headless = false;
    bool setup = false;                      // start as a pet fresh out of the box
    bool machine = false;                    // also print tagged JSON lines for the Mac app (log.h)
    double speed = 1;                        // clock speed-up, for tests
};

// Fills `o` from the command line. Returns -1 to go on, or the exit code after printing the problem.
int parse_options(int argc, char** argv, Options& o);

}  // namespace nestbox
