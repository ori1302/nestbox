// Swimming and jumping, for a pastime's steps: a straight swim through the scene's water with the pet sunk to it,
// ripples round it and a wake behind it, and a jump in an arc into the water or out of it, with a splash.
#include <algorithm>
#include <cmath>
#include "body/parts.h"
#include "body/pet.h"
#include "util/deadline.h"

namespace nestbox {

namespace {
constexpr uint32_t LAP_MS = 260;          // the water lapping at its sides: one step of the flicker
constexpr int SIDE_RIPPLE = 3;            // art pixels of ripple either side of it
constexpr int WAKE_DOTS = 4;              // its wake while it swims: dots behind it, spreading
constexpr uint32_t SPLASH_MS = 1100;      // a splash's rings, growing and fading
constexpr int SPLASH_RINGS = 2;
constexpr uint32_t SPLASH_RING_GAP_MS = 260;
constexpr float SPLASH_FROM = 2, SPLASH_GROWTH = 10, SPLASH_FLATTEN = 0.35f;
constexpr float SPLASH_BREAKS_UP = 0.6f;       // past this much of its life a ring breaks up
constexpr uint32_t DROPS_MS = 300;        // drops thrown up at the start of a splash
}  // namespace

bool Pet::swimming() const { return stage_.deep_water(x_, y_); }

void Pet::start_jump(Point to, bool out, uint32_t now) {
    if (swimming()) {                     // jumping out: a splash where it leaves the water
        splash_ms_ = stamp(now);
        splash_at_ = {x_, y_};
    }
    route_ = {to};
    phase_ = Phase::Walking;
    walking_ = setting_off_ = turning_off_ = false;
    jumping_ = true;
    jump_out_ = out;
    jump_from_ = {x_, y_};
    jump_len_ = std::max(1.0f, std::hypot(to.x - x_, to.y - y_));
    jump_ms_ = now;
    speed_ = jump_len_ / (JUMP_MS / 1000) / pace();
}

void Pet::land(uint32_t now) {
    jumping_ = false;
    if (swimming()) {
        splash_ms_ = stamp(now);
        splash_at_ = {x_, y_};
    }
    if (jump_out_) {                      // out of the water after something came first: on to where it was going
        jump_out_ = false;
        begin(now);
    }
}

// A jump from the water or into it: the water's surface where it left or will land, so what is below it stays hidden,
// while it is over the water (over a bank or a jetty nothing hides it). A leap from the water back into it: the surface
// under it.
std::optional<int> Pet::jump_surface() const {
    if (!jumping_ || route_.empty() || !swimming()) return std::nullopt;
    const Point to = route_.back();
    const bool from_water = stage_.deep_water(jump_from_.x, jump_from_.y), to_water = stage_.deep_water(to.x, to.y);
    if (from_water && to_water) return int(y_);
    if (from_water) return int(jump_from_.y);
    if (to_water) return int(to.y);
    return std::nullopt;
}

// How high its jump has carried it above the straight way, screen pixels.
// Into the water it sinks toward its swimming depth as it lands; out of it, it rises from that depth; from the water
// back into it, it leaps up from that depth, clear of the water at the top, and dives back down to it.
float Pet::jump_lift() const {
    if (!jumping_ || phase_ != Phase::Walking || route_.empty()) return 0;
    const float s = walking_ ? std::clamp(1 - route_left() / jump_len_, 0.0f, 1.0f) : 0;
    const float arc = float(JUMP_HEIGHT * unit() * std::sin(PI * s));
    const float depth = float(egg_->scene.water_depth * unit());
    const Point to = route_.back();
    const bool from_water = stage_.deep_water(jump_from_.x, jump_from_.y), to_water = stage_.deep_water(to.x, to.y);
    if (from_water && to_water) return (float(JUMP_HEIGHT * unit()) + depth) * float(std::sin(PI * s)) - depth;
    if (from_water) return arc - depth * (1 - s);
    if (to_water) return arc - depth * s;
    return arc;
}

// Its frame sunk into the water: only what is above the waterline drawn, the water lapping at its sides and, while it
// swims, a wake spreading behind it.
void Pet::draw_swimmer(Canvas& c, const FrameBox& b, int left, int top, int waterline, uint32_t now,
                       bool afloat) const {
    const int u = unit();
    int lo = c.w, hi = -1, last_row = -1;
    for (int j = 0; j < b.h; j++) {
        const int y = top + (b.y + j) * u;
        if (y + u > waterline) break;
        for (int i = 0; i < b.w; i++) {
            const uint8_t k = b.px[j * b.w + i];
            if (!k) continue;
            const int x = left + (b.x + i) * u;
            c.rect(x, y, u, u, egg_->palette[k]);
            if (j > last_row) lo = c.w, hi = -1, last_row = j;
            lo = std::min(lo, x);
            hi = std::max(hi, x + u - 1);
        }
    }
    if (hi < 0 || !afloat) return;
    const uint16_t light = mix(WATER_LIGHT, WATER_LIGHT_NIGHT, night_);
    const int y = waterline / u * u;
    auto dot = [&](int x, int yy) {
        if (x >= 0 && x < c.w && yy >= 0 && yy < c.h) c.rect(x, yy, u, u, light);
    };
    const int phase = int(now / LAP_MS) % 2;
    for (int k = 1; k <= SIDE_RIPPLE; k++) {          // either side, every other pixel, shifting as the water laps
        if ((k + phase) % 2) continue;
        dot(lo - k * u, y);
        dot(hi + 1 + (k - 1) * u, y);
    }
    // The water's surface across it: the water just below, lightened, with a glint every third pixel.
    const int below = std::min(y + u, c.h - 1);
    for (int x = std::max(lo, 0); x <= std::min(hi, c.w - 1); x += u) {
        if (y < 0 || y >= c.h) break;
        const uint16_t water = c.px[size_t(below) * c.w + x];
        c.rect(x, y, u, u, (x / u + phase) % 3 ? mix(water, light, 0.55f) : light);
    }
    if (walking_ && !jumping_) {
        const int back = last_dx_ >= 0 ? lo : hi;     // behind it, the way it came
        const int dir = last_dx_ >= 0 ? -1 : 1;
        for (int k = 1; k <= WAKE_DOTS; k++) {
            if ((k + int(now / LAP_MS)) % 3 == 0) continue;
            const int x = back + dir * k * 2 * u;
            dot(x, y - (k / 2) * u);
            dot(x, y + (k / 2 + 1) * u);
        }
    }
}

// Rings spreading on the water where it went in or came out, and drops thrown up at the start.
void Pet::draw_splash(Canvas& c, uint32_t now) const {
    if (!splash_ms_ || now - splash_ms_ >= SPLASH_MS) return;
    const int u = unit();
    const uint16_t light = mix(WATER_LIGHT, WATER_LIGHT_NIGHT, night_);
    const uint32_t since = now - splash_ms_;
    for (int k = 0; k < SPLASH_RINGS; k++) {
        if (since < k * SPLASH_RING_GAP_MS) continue;
        const float grown = float(since - k * SPLASH_RING_GAP_MS) / SPLASH_MS;
        const float rx = (SPLASH_FROM + grown * SPLASH_GROWTH) * u, ry = std::max(float(u), rx * SPLASH_FLATTEN);
        draw_ring(c, splash_at_.x, splash_at_.y, rx, ry, u, light, grown > SPLASH_BREAKS_UP);
    }
    if (since < DROPS_MS) {
        const float up = float(since) / DROPS_MS;
        const int h = int((4 + 6 * std::sin(PI * up)) * u);
        for (int d : {-3, -1, 2, 4}) {
            const int x = int(splash_at_.x) + d * u, y = int(splash_at_.y) - h + std::abs(d) * u;
            if (x >= 0 && x < c.w && y >= 0 && y < c.h) c.rect(x, y, u, u, light);
        }
    }
}

}  // namespace nestbox
