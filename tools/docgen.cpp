// Prints what the engine serves, for the packer and the docs at nestbox.pet:
//   docgen api    the API help text (GET /), without its first line, which names the device
//   docgen spec   the pet format's contract: the signs, the required animations and the look table, as JSON
#include <cstdio>
#include <cstring>
#include <string>
#include "door/api.h"
#include "door/spec.h"

using namespace nestbox;

namespace {

std::string get(const char* path) {
    Brain brain;
    Pet pet;
    Shelf shelf;
    Canvas screen(1, 1);
    Egg egg;
    egg.name = "Nibble";
    shelf.add(std::move(egg), "nibble");
    Device device{brain, pet, shelf, screen, "nestbox", "http://nestbox.local", ""};
    return handle_request(device, "GET", path, "", 0, "nestbox.local").body;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "api") == 0) {
        const std::string help = get("/");
        const size_t body = help.find("\n\n");
        std::fputs(body == std::string::npos ? help.c_str() : help.c_str() + body + 2, stdout);
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "spec") == 0) {
        std::fputs(spec_json().c_str(), stdout);
        return 0;
    }
    std::fprintf(stderr, "usage: docgen api|spec\n");
    return 2;
}
