// A pet egg (.egg): one pet in one file. See https://nestbox.pet/docs/egg-format/.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "egg/daypart.h"
#include "screen/display.h"
#include "screen/frames.h"
#include "util/json.h"

namespace nestbox {

// An animation has one row of frames (facing the viewer) or one per facing, clockwise from the front.
constexpr int FACING_ROWS = 8;

struct Anim {
    std::string name;
    int fw = 0, fh = 0;                  // frame size in pixels
    int rows = 1;                        // 1, or FACING_ROWS clockwise from the front: down, down-right, right...
    int frames = 0;
    int rush = -1, hit = -1, ret = -1;   // a strike's frame markers (lunge, impact, back), -1 when absent
    bool dash = false;                   // the strike also slides the pet forward between rush and return
    std::vector<uint8_t> durations;      // ticks (1/TICKS_PER_S s) per frame
    std::vector<FrameBox> boxes;         // rows * frames, row by row

    const FrameBox& box(int row, int f) const { return boxes[size_t(row) * frames + f]; }
    int total_ticks() const { int t = 0; for (auto d : durations) t += d; return t; }
    // Ticks from the start to the end of frame `last` (a throw's release: through its HitFrame).
    int ticks_through(int last) const {
        int t = 0;
        for (int f = 0; f <= last && f < int(durations.size()); f++) t += durations[size_t(f)];
        return t;
    }
};

// A small part of a scene that moves on its own (falling water, a flickering lamp): frames of changes to
// the scene picture, one set per scene picture it belongs to. A frame is coded like an animation's (FrameBox), its
// pixels the palette indices it puts in the scene; index 0 leaves the scene as it is. It plays only while the scene
// shows a picture it has frames for, so it always matches what is around it.
struct Ambient {
    // Always: it moves all the time. Loop, Once: it shows only while a pastime's step shows it, from that step's start
    // (steps in a row that show it keep it going from the first), over and over, or once and then holding its last
    // frame.
    enum class Cue { Always, Loop, Once };
    std::string name;                    // what a pastime's step calls it ("show")
    Cue cue = Cue::Always;
    std::string during;                  // a look whose playing shows it too (a cued part), e.g. "type_fast"
    uint8_t fill = 0;                    // a palette colour painted over its whole box under each frame that draws
                                         // anything (a screen's dark), so frames hold only what is on it; 0: none
    int w = 0, h = 0, x = 0, y = 0;      // its size and top-left corner, in scene picture pixels
    int frames = 0;
    std::vector<uint8_t> durations;      // ticks (1/TICKS_PER_S s) per frame
    std::vector<FrameBox> day;           // one per frame, inside its w x h box; empty: none for this picture
    std::vector<FrameBox> part[4];       // for the scene's morning, afternoon, evening and night pictures

    // The frame showing `tick` ticks into its loop, and one loop's length.
    int frame_at(uint64_t tick) const { return nestbox::frame_at(durations.data(), frames, int64_t(tick)); }
    int total_ticks() const { int t = 0; for (auto d : durations) t += d; return t; }
};

// A pet's own scene, but for its moving parts: full-screen pictures and the area it may walk on. Coordinates are the
// pictures' pixels.
struct SceneLayout {
    int w = 0, h = 0;                    // the pictures' size in their own pixels
    int scale = 1;                       // screen pixels per picture pixel (2: drawn at 2x pixels)
    const uint8_t* day = nullptr;        // the main picture, w * h palette indices
    const uint8_t* part[4] = {};         // optional pictures for the morning, afternoon, evening and night
    bool has_part[4] = {};               // which of them it has (kept when the egg is slim)
    std::vector<std::pair<int, int>> floor;   // polygon; empty = the whole floor
    std::vector<std::vector<std::pair<int, int>>> blocked;   // optional polygons inside the floor it may not stand on
    std::vector<std::pair<int, int>> water;   // optional polygon a pastime may swim in (its steps "swim" and "jump")
    int water_depth = 0;                 // how far it sinks swimming, in the pet's own pixels
    int water_shallow = 0;               // picture pixels from its floor over which it sinks to that depth; 0: at once
    int home_x = -1, home_y = -1;             // where the pet stands to work and sleep; -1 = the middle
    int target_x = -1, target_y = -1;         // optional: a point the pet can aim at (a hoop's rim); -1 = none
    int watch_x = -1, watch_y = -1;           // optional: where it watches its screen from; -1 = a step back from home
    int keep[4] = {-1, -1, -1, -1};           // x0, y0, x1, y1 to keep in view on a smaller screen; -1 = around home
    Corner gauges = Corner::TopRight;         // the corner its gauges go in, where they hide the least of it
    // Optional work spot, always in the scene (the pet walks around it and goes to it to work):
    // `on` while it works, `off` the rest of the time, `night` while it sleeps. Index 0 = transparent.
    int work_w = 0, work_h = 0, work_x = 0, work_y = 0;
    const uint8_t* work_on = nullptr;         // work_w * work_h palette indices
    const uint8_t* work_off = nullptr;        // optional; else `on`
    const uint8_t* work_night = nullptr;      // optional; else `off`, dimmed
    // Optional: the target's moment (a hoop's net swishing), drawn over the scene while the item drops through or
    // lands.
    int swish_w = 0, swish_h = 0, swish_x = 0, swish_y = 0;
    const uint8_t* swish_day = nullptr;       // swish_w * swish_h palette indices, 0 = transparent
    const uint8_t* swish_night = nullptr;     // optional; else `day`, dimmed

    // The picture for a time of day: its own, else the nearest it has (morning and afternoon: the main
    // picture; evening: the night picture; night: the main picture dimmed). `dim` says how much to dim it.
    const uint8_t* picture(Daypart p, float& dim) const;
    // An ambient part's frames that belong with the scene picture `pic`; null if it has none for it.
    const std::vector<FrameBox>* frames_for(const Ambient& a, const uint8_t* pic) const {
        const std::vector<FrameBox>* f = nullptr;
        if (pic && pic == day) f = &a.day;
        for (int i = 0; i < 4 && !f; i++)
            if (pic && pic == part[i]) f = &a.part[i];
        return f && !f->empty() ? f : nullptr;
    }
    // In the scene's water.
    bool in_water(float x, float y) const { return water.size() >= 3 && inside(water, x, y); }
    // On the floor and in none of its blocked parts.
    bool walkable(float x, float y) const {
        if (floor.size() >= 3 && !inside(floor, x, y)) return false;
        for (const auto& hole : blocked)
            if (hole.size() >= 3 && inside(hole, x, y)) return false;
        return true;
    }
    static bool inside(const std::vector<std::pair<int, int>>& poly, float x, float y) {   // even-odd rule
        bool in = false;
        for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            if (crosses(poly, i, j, x, y)) in = !in;
        return in;
    }
    // Whether the edge from poly[j] to poly[i] crosses the ray from (x, y) to the right: one test of inside().
    static bool crosses(const std::vector<std::pair<int, int>>& poly, size_t i, size_t j, float x, float y) {
        const float xi = float(poly[i].first), yi = float(poly[i].second), xj = float(poly[j].first),
                    yj = float(poly[j].second);
        return (yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi;
    }
};

// The scene with its moving parts. A stage keeps a copy of the layout only: the parts' frame tables are most of a
// scene's memory, and the pet draws them from the egg's own.
struct Scene : SceneLayout {
    std::vector<Ambient> ambient;             // optional; positions stay in picture pixels
};

// How the item flies once thrown: through the target and back to be caught (a hoop), into the target and gone
// (a pond), or straight up and back down (no target needed).
enum class Flight { Hoop, Splash, Toss };

// A pet's throw: `anim` lets go of the item on its hit frame, `item` (one row) is drawn in the air, and the
// optional `catch_anim` (one row) plays when it comes back.
struct ThrowKit {
    std::string anim, item, catch_anim;
    Flight flight = Flight::Hoop;
    bool typing_fast = true;             // it throws while the user types fast (else only when a step asks)
    float catch_height = 0.25f;          // where a caught item comes back to, as a part of its height (0: its feet)
    int release_x = -1, release_y = -1;  // where the item leaves, in the throw's frame (its own pixels from the
                                         // frame's top-left); -1: the top of its head as drawn
    bool plays = false;                  // it plays with it in its free time: throws from here and there
};

// Something it does now and then in its free time: steps written as a sender writes them ("do", "at", "face",
// "for", "times", "say"...), kept as read and played by the brain; and the hours it may happen in (-1: any).
struct Pastime {
    static constexpr size_t MAX = 16, MAX_STEPS = 16;
    std::string name;
    int from_hour = -1, to_hour = -1;    // from from_hour:00 up to to_hour:00, local time
    std::vector<Fields> steps;
};

// Move-only: its scene and frames point into `bytes`, so a copy would point into the original's buffer.
struct Egg {
    Egg() = default;
    Egg(Egg&&) noexcept = default;
    Egg& operator=(Egg&&) noexcept = default;
    Egg(const Egg&) = delete;
    Egg& operator=(const Egg&) = delete;

    std::string name;
    // Its author's words about it, for people and agents (each may be empty).
    std::string theme;               // where it lives and what it is, e.g. "a white farm goose in a mountain meadow"
    std::string personality;         // e.g. "curious, a bit proud, famously grumpy"
    std::string style;               // how it is drawn
    std::string about;               // anything else worth knowing
    std::string color;               // its own colour, "#rrggbb" (or empty)
    std::string credit, license;     // who made the art, and how it may be used
    std::vector<std::pair<std::string, std::string>> notes;   // animation name -> what it looks like
    std::string note(const std::string& n) const { for (auto& [k, v] : notes) if (k == n) return v; return ""; }
    std::vector<uint16_t> palette;   // RGB565; index 0 unused (transparent)
    // pet.json "screen_palette": the only colours its screen may show (a handheld's few greens, say). Everything the
    // engine draws over the pet's own pictures (bubbles, signs, effects, the status bar, gauges) is snapped to the
    // nearest of them by brightness. screen_lut maps every RGB444 colour to one of them; both empty when it has none.
    std::vector<uint16_t> screen_palette, screen_lut;
    int scale = 0;                   // screen pixels per art pixel it was drawn for (0: the screen's own)
    float pace = 0;                  // walking speed, screen pixels per second (0: the engine's own)
    int feet = 0;                    // pixels from a frame's centre down to the feet
    int tall = 0;                    // visible height of Idle
    bool has_scene = false;
    Scene scene;
    bool has_throw = false;
    ThrowKit throw_kit;
    std::vector<Pastime> pastimes;           // its own free-time doings, from the pet folder's "free_time"
    std::vector<Anim> anims;
    std::vector<uint8_t> bytes;      // the whole file; pictures and frames point into it
    // Kept without its pictures and frames (slim_egg) while it is not out: everything else stays, so it can be listed
    // and described, but it can't be drawn.
    bool slim = false;
    int idle_reach = -1;             // idle_reach(), measured when it is read (-1: not yet); a slim egg keeps it

    const Anim* find(const std::string& n) const {
        for (auto& a : anims) if (a.name == n) return &a;
        return nullptr;
    }
    // Idle, else the first animation; nullptr if there is none.
    const Anim* standing_anim() const {
        const Anim* a = find("Idle");
        return a || anims.empty() ? a : &anims[0];
    }
};

// How far Idle's farthest pixel is from the middle of its frame, in half pixels of the pet's own art; 0 without Idle.
int idle_reach(const Egg& egg);

// Drops what only drawing the pet needs (its scene pictures, moving parts, swish and frames) and keeps the rest,
// including its work spot (the desk's place is measured on it): a shelf keeps the pets that are not out this way.
void slim_egg(Egg& egg);

// Reads an egg from its bytes. Returns false and fills `error` if the data is not a valid egg.
bool load_egg(std::vector<uint8_t> bytes, Egg& out, std::string& error);
// Gives the egg a screen palette (see Egg::screen_palette) and its snap table; an empty list takes it away.
void set_screen_palette(Egg& egg, const std::vector<uint16_t>& colours);

}  // namespace nestbox
