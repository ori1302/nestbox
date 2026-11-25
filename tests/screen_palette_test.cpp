// A pet with a screen palette (pet.json "screen_palette": a handheld's few greens, say) shows only those colours:
// a busy scenario on every screen, with bubbles, signs, effects, the status bar, a caption, gauges and a night, and
// every pixel of every frame checked. Its own colours pass through unchanged.
// Usage: screen_palette_test EGG...
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <cstdlib>
#include <set>
#include <string>
#include "check.h"
#include "door/api.h"
#include "host/eggs.h"
#include "host/frame.h"
#include "screen/display.h"

using namespace nestbox;
using test::check;

int main(int argc, char** argv) {
    const std::vector<uint16_t> GREENS = {rgb(0x0b, 0x26, 0x0b), rgb(0x0f, 0x38, 0x0f), rgb(0x24, 0x52, 0x1f),
                                          rgb(0x30, 0x62, 0x30), rgb(0x5e, 0x8a, 0x1c), rgb(0x8b, 0xac, 0x0f),
                                          rgb(0x9b, 0xbc, 0x0f), rgb(0xb2, 0xc8, 0x4a)};
    const std::set<uint16_t> allowed(GREENS.begin(), GREENS.end());
    const char* const STEPS[] = {
        R"({"from":"claude","id":"s1","state":"working","line":"running a command in the terminal for a while","do":"type"})",
        R"({"from":"claude","id":"usage/5h","state":"on","gauge":37})",
        R"({"from":"t","do":"dance","effect":"confetti","say":"Hello from the test, a long bubble"})",
        R"({"from":"t","caption":"A caption along the bottom of the screen","gauge":"timer","for":"20s"})",
        R"({"from":"ci","state":"failed","line":"build 12 failed: 3 tests","sign":"cross"})",
        R"({"from":"t","do":"run","effect":"sparkles"})",
        R"({"from":"claude","id":"s1","state":"asking","say":"May I run the tests?"})",
        R"({"from":"t","do":"sleep","for":"10s"})",
        R"({"from":"t","sign":"heart","effect":"notes"})",
        R"({"from":"slack","id":"D1","say":"Shira wrote to you","effect":"letter","for":20})",
        R"({"from":"t","state":"over"})",
    };
    int eggs = 0;
    for (int a = 1; a < argc; a++) {
        Egg egg;
        if (!test::load_egg_file(argv[a], egg)) continue;
        eggs++;
        set_screen_palette(egg, GREENS);
        for (uint16_t g : GREENS) check(egg.screen_lut[rgb444(g)] == g, egg.name + ": its own colour changed");
        for (const Display& d : displays()) {
            Brain brain; Pet pet; Shelf shelf; Canvas screen(d.w, d.h);
            shelf.add(test::reloaded(egg), "pet");
            set_screen_palette(const_cast<Egg&>(shelf.egg(0)), GREENS);
            Device dev(brain, pet, shelf, screen, "t", "http://t.local", "t");
            dev.random = [n = 0u]() mutable { return n += 2654435761u; };
            put_out_first(dev, d, "", 0);
            uint32_t now = 0;
            int step = 0, bad_frames = 0;
            for (int f = 0; f < 1500; f++, now += 33) {
                if (f % 140 == 50)
                    handle_request(dev, "POST", "/v1/tell", STEPS[step++ % std::size(STEPS)], now, "", "", true);
                if (f == 900) handle_request(dev, "POST", "/v1", R"({"time":"22:30"})", now, "", "", true);
                step_frame(dev, now);
                int nbad = 0, bx0 = 9999, by0 = 9999, bx1 = -1, by1 = -1;
                uint16_t sample = 0;
                for (int y = 0; y < d.h; y++)
                    for (int x = 0; x < d.w; x++) {
                        const uint16_t p = screen.px[size_t(y) * d.w + x];
                        if (allowed.count(p)) continue;
                        nbad++, sample = p;
                        bx0 = std::min(bx0, x), by0 = std::min(by0, y), bx1 = std::max(bx1, x), by1 = std::max(by1, y);
                    }
                if (nbad) {
                    if (!bad_frames && std::getenv("SP_DEBUG"))
                        std::printf("%s %s frame %d: %d stray px in %d,%d..%d,%d, e.g. %06x; playing %s\n",
                                    egg.name.c_str(), d.id.c_str(), f, nbad, bx0, by0, bx1, by1, rgb888(sample),
                                    pet.playing().c_str());
                    bad_frames++;
                }
            }
            check(bad_frames == 0, egg.name + " on " + d.id + ": " + std::to_string(bad_frames) +
                                       " frames show a colour outside its screen palette");
        }
    }
    check(eggs > 0, "no eggs");
    std::printf("screen palette: %d eggs on %zu screens\n", eggs, displays().size());
    return test::summary();
}
