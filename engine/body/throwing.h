// Throwing. A pet with a throw kit (egg.h) plays its throw, lets go of the item on the animation's hit frame, and
// the device flies the item one of three ways (Flight), then plays the catch if the item comes back.
#pragma once
#include <optional>
#include "body/stage.h"
#include "egg/egg.h"

namespace nestbox {

// Flights in ticks (1/TICKS_PER_S s). A hoop: an arc to the target, a drop through it, two bounces back. A splash:
// the arc, then ripples where it landed. A toss: up and back down.
constexpr int FLY_TICKS = 40, DROP_TICKS = 10, BOUNCE1_TICKS = 16, BOUNCE2_TICKS = 10;
constexpr int HOOP_AIR_TICKS = FLY_TICKS + DROP_TICKS + BOUNCE1_TICKS + BOUNCE2_TICKS;
constexpr int TOSS_TICKS = 36;
constexpr int RIPPLE_TICKS = 60;

// Ticks the item is in the air.
int air_ticks(Flight f);
// Ticks the target's moment lasts once the item reaches it (a hoop's net, a splash's ripples); 0 for a toss.
int effect_ticks(Flight f);

// Where the item is `tick` ticks after leaving the thrower at `from`, aimed at `aim` and coming back to `catch_at`;
// none before it leaves and once it is back or gone. `unit`: the screen's own art scale, so a flight is the same
// whatever the thrower's pixel size.
std::optional<Point> item_at(Flight f, int tick, Point from, Point aim, Point catch_at, int unit);

}  // namespace nestbox
