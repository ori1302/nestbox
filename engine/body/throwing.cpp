#include "body/throwing.h"
#include <algorithm>
#include <cmath>
#include "body/parts.h"
#include "body/pet.h"
#include "body/plays.h"

namespace nestbox {

namespace {
constexpr float ARC_LIFT = 14;        // the arc's top above the higher end, in the screen's own art pixels
constexpr float ARC_TOP_MARGIN = 4;   // ...but kept this far below the top of the screen
constexpr int NET_DEPTH = 10;         // the item drops this far below a hoop's rim at least, screen art pixels
constexpr float FIRST_BOUNCE_AT = 0.6f;
constexpr int FIRST_BOUNCE_HEIGHT = 10, SECOND_BOUNCE_HEIGHT = 4;   // screen art pixels
constexpr int TOSS_HEIGHT = 26;       // a toss's rise over the thrower's head, screen art pixels (below the margin)
constexpr int ITEM_FRAME_TICKS = 4;
constexpr int SWISH_EXTRA_TICKS = 6;  // a net keeps moving a little after the item has gone through
constexpr int SWISH_LEAD_TICKS = 4;   // ...and starts a little before it, its front over the item coming down

float lerp(float a, float b, float s) { return a + (b - a) * s; }

// A parabola's height at `s` (0..1) with its top `peak` above the line.
float arc(float peak, float s) { return peak * 4 * s * (1 - s); }

// The frame showing `tick` ticks into one run of `a`, holding its last frame after the end.
int frame_held(const Anim& a, int tick) { return frame_at(a.durations.data(), a.frames, tick, Playback::Hold); }

// The arc from `from` to `to` at `s` (0..1): halfway it is ARC_LIFT above the higher end, and all of it stays below
// the top of the screen. With the ends at different heights its top comes before or after halfway, higher still:
// `highest` is the height over the straight line that puts that top at the margin.
Point arc_to(Point from, Point to, float s, int unit) {
    const float higher = std::min(from.y, to.y);
    const float margin = std::min(higher, ARC_TOP_MARGIN * unit);
    const float rise = std::sqrt(from.y - margin) + std::sqrt(to.y - margin), highest = rise * rise / 4;
    const float peak = std::min((from.y + to.y) / 2 - higher + ARC_LIFT * unit, highest);
    return Point{lerp(from.x, to.x, s), lerp(from.y, to.y, s) - arc(peak, s)};
}

std::optional<Point> hoop_at(int tick, Point from, Point rim, Point catch_at, int unit) {
    if (tick < FLY_TICKS) return arc_to(from, rim, float(tick) / FLY_TICKS, unit);
    const float floor_y = std::max(rim.y + NET_DEPTH * unit, catch_at.y);
    if ((tick -= FLY_TICKS) < DROP_TICKS) return Point{rim.x, lerp(rim.y, floor_y, float(tick) / DROP_TICKS)};
    const Point bounce{lerp(rim.x, catch_at.x, FIRST_BOUNCE_AT), lerp(floor_y, catch_at.y, FIRST_BOUNCE_AT)};
    if ((tick -= DROP_TICKS) < BOUNCE1_TICKS) {
        const float s = float(tick) / BOUNCE1_TICKS;
        const float y = lerp(floor_y, bounce.y, s) - arc(float(FIRST_BOUNCE_HEIGHT * unit), s);
        return Point{lerp(rim.x, bounce.x, s), y};
    }
    tick -= BOUNCE1_TICKS;
    const float s = float(tick) / (BOUNCE2_TICKS - 1);   // its last tick lands on the catch point
    const float y = lerp(bounce.y, catch_at.y, s) - arc(float(SECOND_BOUNCE_HEIGHT * unit), s);
    return Point{lerp(bounce.x, catch_at.x, s), y};
}
}  // namespace

int air_ticks(Flight f) {
    switch (f) {
        case Flight::Splash: return FLY_TICKS;
        case Flight::Toss: return TOSS_TICKS;
        case Flight::Hoop: break;
    }
    return HOOP_AIR_TICKS;
}

int effect_ticks(Flight f) {
    switch (f) {
        case Flight::Splash: return RIPPLE_TICKS;
        case Flight::Toss: return 0;
        case Flight::Hoop: break;
    }
    return DROP_TICKS + SWISH_EXTRA_TICKS;
}

std::optional<Point> item_at(Flight f, int tick, Point from, Point aim, Point catch_at, int unit) {
    if (tick < 0 || tick >= air_ticks(f)) return std::nullopt;
    switch (f) {
        case Flight::Hoop: return hoop_at(tick, from, aim, catch_at, unit);
        case Flight::Splash:
            return arc_to(from, aim, float(tick) / (FLY_TICKS - 1), unit);  // its last tick lands on the aim
        case Flight::Toss: {
            const float s = float(tick) / (TOSS_TICKS - 1);
            const float height = std::min(float(TOSS_HEIGHT * unit), std::max(0.0f, from.y - ARC_TOP_MARGIN * unit));
            return Point{lerp(from.x, catch_at.x, s), lerp(from.y, catch_at.y, s) - arc(height, s)};
        }
    }
    return std::nullopt;
}

bool Pet::can_throw() const {
    return egg_ && has_throw(*egg_) && (egg_->throw_kit.flight == Flight::Toss || stage_.target());
}

bool Pet::plays() const { return can_throw() && egg_->throw_kit.plays; }

// Typing fast with a kit, or a step asking for a throw.
bool Pet::throwing() const {
    if (!can_throw()) return false;
    if (want_ == Look::TypeFast) return egg_->throw_kit.typing_fast;
    return want_ == Look::Act && act_.kind == Act::Kind::Throw;
}

// Where the item leaves: while throwing, the kit's release point (its beak, its hand) in the throw's frame placed
// where the pet stands, as draw_pet() places it; else the top of its head as last drawn (a beak, a turret), else
// the top of the pet.
Point Pet::item_from() const {
    const ThrowKit& kit = egg_->throw_kit;
    const Anim* anim = kit.release_x >= 0 && throwing() ? egg_->find(kit.anim) : nullptr;
    if (anim) {
        const int u = unit();
        const int cx = int(x_) / u * u;
        const int cy = (int(y_) - egg_->feet * u) / u * u;
        const int left = cx - anim->fw / 2 * u, top = cy - anim->fh / 2 * u;
        return {float(left + kit.release_x * u + u / 2), float(top + kit.release_y * u + u / 2)};
    }
    if (top_y_ > 0) return {float(top_x_), float(top_y_)};
    return {x_, y_ - egg_->tall * unit()};
}

// What it throws at: the step's point, else the scene's target.
Point Pet::aim_point() const {
    if (want_ == Look::Act && act_.kind == Act::Kind::Throw)
        if (const std::optional<Point> at = spot_point(act_.aim)) return *at;
    const std::optional<Pixel> target = stage_.target();
    return target ? Point{float(target->x), float(target->y)} : Point{x_, y_};
}

// One throw: the throw itself, the item in the air (the throw's last frame held), the target's moment, the catch.
int Pet::throw_cycle_ticks() const {
    const ThrowKit& kit = egg_->throw_kit;
    const Anim* anim = egg_->find(kit.anim);
    const Anim* catch_anim = egg_->find(kit.catch_anim);
    const int release = anim->ticks_through(anim->hit);
    const int air = air_ticks(kit.flight);
    const int after = kit.flight == Flight::Splash ? air + effect_ticks(kit.flight) : air;
    return std::max(anim->total_ticks(), release + after) + (catch_anim ? catch_anim->total_ticks() : 0);
}

// A fixed cycle as a function of `t` (ms into the throwing) alone, so nothing can drift or pile up.
std::optional<Pet::Throw> Pet::throw_at(const Pose& base, uint32_t t) const {
    if (!throwing()) return std::nullopt;
    const ThrowKit& kit = egg_->throw_kit;
    const Anim* anim = egg_->find(kit.anim);
    const Anim* catch_anim = egg_->find(kit.catch_anim);
    const Anim* item = egg_->find(kit.item);
    const float speed = act_speed();
    const int release = anim->ticks_through(anim->hit);
    const int cycle = throw_cycle_ticks();
    const int catch_ticks = catch_anim ? catch_anim->total_ticks() : 0;
    const int caught_from = cycle - catch_ticks;
    const int tick = int(ms_to_ticks(t, speed) % cycle);
    Throw thrown;
    Pose& pose = thrown.pose;
    pose = base;
    pose.anim = anim;
    pose.row = row_;
    pose.frames.clear();
    pose.speed = 1;
    pose.bubble = Bubble::None;
    if (tick < anim->total_ticks()) {
        pose.frames = {frame_held(*anim, tick)};
    } else if (tick < caught_from || !catch_anim) {
        pose.frames = {anim->frames - 1};
    } else {
        pose.anim = catch_anim;
        pose.row = ROW_FRONT;
        pose.frames = {frame_held(*catch_anim, tick - caught_from)};
    }
    const int u = unit();
    const Point from = item_from();
    const Point aim = aim_point();
    const Point catch_at{x_, y_ - egg_->tall * u * kit.catch_height};
    // The flight's arc and bounces are in the screen's own pixels, the same whatever the pet's pixel size.
    if (const auto at = item_at(kit.flight, tick - release, from, aim, catch_at, sign_unit())) {
        thrown.item = *at;
        thrown.item_frame = (tick / ITEM_FRAME_TICKS) % item->frames;
    }
    const int since = tick - release - (kit.flight == Flight::Hoop ? FLY_TICKS : air_ticks(kit.flight));
    const int lead = kit.flight == Flight::Hoop ? SWISH_LEAD_TICKS : 0;
    thrown.effect = since >= -lead && since < effect_ticks(kit.flight);
    thrown.effect_at = aim;
    thrown.effect_tick = since;
    return thrown;
}

}  // namespace nestbox
