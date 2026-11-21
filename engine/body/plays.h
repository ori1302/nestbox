// What an egg plays for each look, judged from the egg alone (no running pet), for descriptions of the pet such as
// GET /agents and GET /v1/pets. Pet::resolve makes the same choice at run time.
#pragma once
#include "body/looks.h"
#include "egg/egg.h"

namespace nestbox {

struct PlayChoice {
    bool found = false;                  // false: the egg has none of the look's animations and no Idle
    Play play;
    bool own = false;                    // the look's first choice or an animation its author describes
};

// The egg's throw kit is complete: the throw has a release (hit) frame, the item and catch exist, and a hoop or
// splash flight has a target in its scene.
bool has_throw(const Egg& egg);

// The first of the look's plays the egg has an animation for (laps need all eight facings), else its Idle. With a
// throw kit that throws while typing fast, throwing replaces typing fast (see Pet::can_throw).
PlayChoice play_for(const Egg& egg, const LookInfo& look);

}  // namespace nestbox
