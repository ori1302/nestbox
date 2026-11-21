#include "files.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "host/eggs.h"

namespace nestbox {

namespace fs = std::filesystem;

fs::path state_dir() {
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".nestbox";
}

void load_egg_folder(const fs::path& dir, Shelf& shelf) {
    std::vector<fs::path> files;
    if (fs::is_directory(dir))
        for (auto& e : fs::directory_iterator(dir))
            if (e.path().extension() == EGG_EXTENSION && !shelf.has(e.path().stem().string()))
                files.push_back(e.path());
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return egg_file_before(a.filename().string(), b.filename().string());
    });
    for (auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Egg egg;
        std::string err;
        if (load_egg(std::move(bytes), egg, err)) shelf.add(std::move(egg), f.stem().string());
        else std::fprintf(stderr, "skipping %s: %s\n", f.c_str(), err.c_str());
    }
}

}  // namespace nestbox
