// Walks Nibble around its farm for an hour of simulated time: its feet never stand in the pond, its body stays clear
// of the water, and it reaches every part of its floor, including the strip above the pond, the shore and the
// meadow's bottom-left corner.
// Usage: pond_test NIBBLE_EGG WATER_FILE
// WATER_FILE lists the scene's water as "x y" per pixel, in screen pixels (tests/fixtures/nibble_water.txt: the pond's
// blue pixels in pets/retro/nibble/meadow.png connected to the splash point (246, 196)).
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <utility>
#include "body/pet.h"
#include "check.h"

using namespace nestbox;
using test::check;

namespace {

constexpr uint32_t HOUR_MS = 3600u * 1000u;
constexpr uint32_t FRAME_MS = 16;
constexpr int BODY_HALF_WIDTH = 26;  // px either side of the feet: the body's lowest rows reach 22, and a margin
constexpr int FEET_HALF_WIDTH = 2;

std::set<std::pair<int, int>> read_water(const char* path) {
    std::set<std::pair<int, int>> water;
    std::ifstream in(path);
    for (int x, y; in >> x >> y;) water.insert({x, y});
    return water;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: pond_test NIBBLE_EGG WATER_FILE\n");
        return 2;
    }
    Egg egg;
    if (!test::load_egg_file(argv[1], egg)) return 1;
    const auto water = read_water(argv[2]);
    if (!check(!water.empty(), std::string("water pixels in ") + argv[2])) return test::summary();

    Pet pet;
    pet.set_egg(&egg);
    pet.want(Look::Wander, 0);
    int wet_feet = 0, wet = 0, steps = 0, above = 0, corner = 0;
    float minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (uint32_t t = 0; t < HOUR_MS; t += FRAME_MS) {
        pet.update(t);
        const int x = int(pet.x()), y = int(pet.y());
        steps++;
        if (y < 140 && x > 200) above++;
        if (y > 212 && x < 80) corner++;
        minx = std::min<float>(minx, x);
        maxx = std::max<float>(maxx, x);
        miny = std::min<float>(miny, y);
        maxy = std::max<float>(maxy, y);
        for (int dx = -FEET_HALF_WIDTH; dx <= FEET_HALF_WIDTH; dx++)
            if (water.count({x + dx, y})) {
                wet_feet++;
                break;
            }
        for (int dx = -BODY_HALF_WIDTH; dx <= BODY_HALF_WIDTH; dx += 3)
            if (water.count({x + dx, y})) {
                wet++;
                break;
            }
    }
    std::printf("%d steps, feet ranged x %.0f-%.0f, y %.0f-%.0f, %zu water pixels\n", steps, minx, maxx, miny, maxy,
                water.size());
    std::printf("time above the pond %.1f%%, in the bottom-left corner %.1f%%\n", 100.0 * above / steps,
                100.0 * corner / steps);
    check(wet_feet == 0, "its feet never stand in the water (" + std::to_string(wet_feet) + " frames did)");
    check(wet == 0, "its body stays clear of the water (" + std::to_string(wet) + " frames did not)");
    check(above >= steps / 50, "it spends time on the strip above the pond");
    check(corner >= steps / 50, "it spends time in the meadow's bottom-left corner");
    const Stage& stage = pet.stage();
    check(stage.can_stand(240, 125) && stage.reachable(240, 125), "it can walk along the strip above the pond");
    check(stage.can_stand(180, 186) && stage.reachable(180, 186), "it can walk along the shore");
    return test::summary();
}
