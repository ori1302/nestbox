// --shot: renders one frame of a pet to a PPM image, without a window or a server.
#pragma once
#include <string>
#include <vector>
#include "body/shelf.h"
#include "screen/display.h"

namespace nestbox {

// --shot OUT.ppm PET_INDEX LOOK MS [TEXT [SIZE AT]]
struct ShotRequest {
    std::string out;
    int pet = 0;                 // index on the shelf, modulo its size
    std::string look;            // a look's key ("watch"), or a digit key 0-9 as in the window (keys.h)
    uint32_t ms = 0;             // how long the pet has been in that look
    std::string text;            // speech, in HOST_ACCENT
    int size = 2;                // its font size, 1-3
    int at = 0;                  // SpeechAt
};

// Reads the arguments after --shot (args[0] is "--shot"); false if any required one is missing.
bool parse_shot(const std::vector<std::string>& args, ShotRequest& r);

// Renders the shot and prints "Name  LOOK  phase  animation". Returns the process exit code.
int render_shot(const Shelf& shelf, const Display& display, const ShotRequest& r);

}  // namespace nestbox
