// The pet's looks in one table: each look's API name, label, kind, reaction length, sign, purpose and the animations
// it plays, best first. A look is only what the body shows; what lasts and why (an agent working, a broken build)
// is the brain's. The brain, the body, the pet descriptions and the docs all read this table.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "screen/hud.h"

namespace nestbox {

// Moods are held until something changes them; reactions play once and return. Act plays a step sent with "do"
// (one of the pet's own animations, a walk, laps, a throw).
enum class Look {
    Wander, Idle, Type, TypeFast, Happy, Alert, Sad, Dizzy, Wake, Sleep, Watch,
    Busy, Party, Dance, Fight, Eat, Sit, Slump,                                   // moods
    Celebrate, Nod, Surprised, Petted, Grumpy, Confused, Yawn, Oops,              // reactions
    Act, Count
};

enum class LookKind { Mood, Reaction };
enum class PlayLoop { All, Strike };      // Strike: only the frames from rush to return, on a loop
enum class Facing { Down, Up, UpRight, Right, Desk };   // front, back, back-right, side-on, toward its screen
enum class Prop { None, Desk };            // things drawn around the pet

// One way a look can be played. The first one whose animation the egg has wins.
struct Play {
    const char* anim = "";
    float speed = 1;
    PlayLoop loop = PlayLoop::All;
    Facing facing = Facing::Down;
    Prop prop = Prop::None;
    bool shake = false;                    // the sprite jiggles
    bool typing = false;                   // only while keys are being pressed
    bool lap = false;                      // runs laps round the open floor instead (an 8-row run cycle)
};

struct LookInfo {
    Look look = Look::Wander;
    const char* key = "";                  // its name in the API, e.g. "type_fast"
    const char* label = "";                // for people, e.g. "TYPE FAST"
    LookKind kind = LookKind::Mood;
    uint32_t length_ms = 0;                // how long a reaction plays unless told; 0 for moods
    Bubble bubble = Bubble::None;          // the sign it wears
    const char* means = "";                // what it is for, in a few words
    std::vector<Play> plays;               // best first
};

// Every look, indexed by Look (Wander first, Act last).
const std::vector<LookInfo>& looks();
// `l` must be a look, not Look::Count.
const LookInfo& info(Look l);
inline const char* look_key(Look l) { return info(l).key; }
inline const char* look_label(Look l) { return info(l).label; }
inline bool is_reaction(Look l) { return info(l).kind == LookKind::Reaction; }
// The look named `key` ("type_fast" -> Look::TypeFast), Act excluded. False if there is none.
bool look_named(const std::string& key, Look& out);

// Signs over the pet by name: ! ? ... zzz heart star check cross note angry shock ("" for Bubble::None). "dots" is
// also read as "...".
const char* sign_key(Bubble b);
bool sign_named(const std::string& key, Bubble& out);
const std::vector<Bubble>& signs();

// Effects by name: notes, sparkles, confetti, letter ("" for Effect::None).
const char* effect_key(Effect e);
bool effect_named(const std::string& key, Effect& out);
const std::vector<Effect>& effects();

}  // namespace nestbox
