// Test eggs with a throw kit, built in memory from a real egg: the first two frames of its Idle as the throw
// (letting go at the end of the second, so the pose then holds still until the catch), a small magenta item that
// is easy to find on a rendered frame, and a one-row Idle as the catch.
#pragma once
#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <vector>
#include "check.h"
#include "egg/egg.h"
#include "body/pet.h"
#include "body/throwing.h"
#include "screen/canvas.h"

namespace nestbox::test {

constexpr uint16_t ITEM_COLOUR = rgb(0xff, 0x00, 0xff);
constexpr int ITEM_SIZE = 3;            // the item's drawn square, art pixels
constexpr int ITEM_FRAME = 5;           // its whole frame, art pixels

struct KitOptions {
    Flight flight = Flight::Hoop;
    bool with_catch = true;
    std::optional<std::pair<int, int>> target;   // in the scene's own pixels; none keeps the egg's own
};

// The egg with a throw kit named Throw / Item / Catch. Frames point into storage that lives as long as the test.
inline Egg with_throw(const Egg& e, const KitOptions& o) {
    static std::deque<std::vector<uint8_t>> pixels;
    Egg z = reloaded(e);
    const Anim idle = *z.find("Idle");
    Anim thrower = idle;
    thrower.name = "Throw";
    thrower.frames = std::min(2, idle.frames);
    thrower.durations.resize(size_t(thrower.frames));
    thrower.boxes.clear();
    for (int r = 0; r < idle.rows; r++)
        for (int f = 0; f < thrower.frames; f++) thrower.boxes.push_back(idle.box(r, f));
    thrower.hit = thrower.frames - 1;
    thrower.rush = thrower.ret = -1;
    Anim item;
    item.name = "Item";
    item.fw = item.fh = ITEM_FRAME;
    item.frames = 2;
    item.durations = {4, 4};
    if (z.palette.size() < 256) z.palette.push_back(ITEM_COLOUR);
    else z.palette.back() = ITEM_COLOUR;
    pixels.emplace_back(size_t(ITEM_SIZE * ITEM_SIZE), uint8_t(z.palette.size() - 1));
    const int edge = (ITEM_FRAME - ITEM_SIZE) / 2;
    for (int f = 0; f < item.frames; f++)
        item.boxes.push_back({edge, edge, ITEM_SIZE, ITEM_SIZE, pixels.back().data()});
    z.anims.push_back(thrower);
    z.anims.push_back(item);
    z.has_throw = true;
    z.throw_kit.anim = "Throw";
    z.throw_kit.item = "Item";
    z.throw_kit.flight = o.flight;
    z.throw_kit.typing_fast = true;
    if (o.with_catch) {
        Anim catcher = idle;
        catcher.name = "Catch";
        catcher.boxes.resize(size_t(catcher.frames));
        catcher.rows = 1;
        z.anims.push_back(catcher);
        z.throw_kit.catch_anim = "Catch";
    }
    if (o.target) {
        z.scene.target_x = o.target->first;
        z.scene.target_y = o.target->second;
    }
    return z;
}

// The egg without its own throw kit or scene target, for checks that need a pet that can't throw.
inline Egg without_throw(const Egg& e) {
    Egg z = reloaded(e);
    z.has_throw = false;
    z.throw_kit = ThrowKit{};
    z.scene.target_x = z.scene.target_y = -1;
    return z;
}

// The scene pixel that `e`'s scene shows at the screen point `p` on `d`: where to put a target to see it there.
inline std::pair<int, int> scene_point(const Egg& e, const Display& d, Point p) {
    Pet probe;
    probe.set_display(d);
    probe.set_egg(&e);
    const Stage& st = probe.stage();
    const int s = st.scene_scale();
    return {(int(p.x) + st.scene_x()) / s, (int(p.y) + st.scene_y()) / s};
}

// Ticks from the start of the throw to the item leaving the hand: up to the end of its hit frame.
inline int release_ticks(const Anim& a) {
    int t = 0;
    for (int f = 0; f <= a.hit; f++) t += a.durations[f];
    return t;
}

// One throw of `kit`, ticks: the throw, the item in the air (and a splash's ripples), the catch. As Pet plays it.
inline int cycle_ticks(const Egg& e) {
    const Anim& a = *e.find(e.throw_kit.anim);
    const Anim* c = e.find(e.throw_kit.catch_anim);
    const Flight f = e.throw_kit.flight;
    const int after = f == Flight::Splash ? FLY_TICKS + RIPPLE_TICKS : f == Flight::Toss ? TOSS_TICKS : HOOP_AIR_TICKS;
    return std::max(a.total_ticks(), release_ticks(a) + after) + (c ? c->total_ticks() : 0);
}

// Pixels of the item's colour on a frame.
inline int item_pixels(const Canvas& c) { return int(std::count(c.px.begin(), c.px.end(), ITEM_COLOUR)); }

}  // namespace nestbox::test
