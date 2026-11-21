#include "options.h"
#include <mach-o/dyld.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "util/json.h"

namespace nestbox {

namespace {
constexpr double MIN_SPEED = 0.1;
constexpr int MAX_PORT = 65535;

// A TCP port number (0: any free one), or -1 if `text` is not one.
int parse_port(const std::string& text) {
    char* end = nullptr;
    const long port = std::strtol(text.c_str(), &end, 10);
    return !text.empty() && *end == '\0' && port >= 0 && port <= MAX_PORT ? int(port) : -1;
}

// Removes the first "`flag` VALUE" pair from anywhere in `args`; false if there is none.
bool take_pair(std::vector<std::string>& args, const std::string& flag, std::string& value) {
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == flag) {
            value = args[i + 1];
            args.erase(args.begin() + long(i), args.begin() + long(i) + 2);
            return true;
        }
    return false;
}

// pets/ next to the executable, symbolic links resolved.
std::filesystem::path default_pets_dir() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) return "pets";
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::canonical(path.c_str(), ec);
    return (ec ? std::filesystem::path(".") : exe.parent_path()) / "pets";
}
}  // namespace

int parse_options(int argc, char** argv, Options& o) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string value;
    if (std::find(args.begin(), args.end(), "--displays") != args.end()) {
        JsonWriter json;
        json.begin_array(JsonWriter::Items::OnePerLine);
        for (auto& d : displays())
            json.begin_object().field("id", d.id).field("name", d.name).field("w", d.w).field("h", d.h).end_object();
        json.end_array();
        std::printf("%s\n", json.text().c_str());
        return 0;
    }
    if (take_pair(args, "--display", value)) {
        o.display = display_named(value);
        if (!o.display) {
            std::fprintf(stderr, "unknown display %s; known:", value.c_str());
            for (auto& d : displays()) std::fprintf(stderr, " %s", d.id.c_str());
            std::fprintf(stderr, "\n");
            return 2;
        }
    }
    o.pets_dir = take_pair(args, "--pets", value) ? std::filesystem::path(value) : default_pets_dir();
    if (!args.empty() && args[0] == "--shot") {
        o.shot = args;
        return -1;
    }
    for (size_t i = 0; i < args.size(); i++) {
        auto next = [&] { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (args[i] == "--port") o.port = parse_port(next());
        else if (args[i] == "--name") o.name = next();
        else if (args[i] == "--headless") o.headless = true;
        else if (args[i] == "--setup") o.setup = true;
        else if (args[i] == "--machine") o.machine = true;
        else if (args[i] == "--speed") o.speed = std::max(MIN_SPEED, std::atof(next().c_str()));
        else {
            std::fprintf(stderr, "unknown option %s\n", args[i].c_str());
            return 2;
        }
    }
    if (o.port < 0) {
        std::fprintf(stderr, "--port takes a port number, 0 (any free one) to %d\n", MAX_PORT);
        return 2;
    }
    return -1;
}

}  // namespace nestbox
