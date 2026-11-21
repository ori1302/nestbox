// Moving the pet from the API, frame by frame with a real egg: it walks to a spot, faces any of its 8 ways, plays
// there for a while or once, chains steps and comes back; an unreachable spot does not hang it; a reaction or a
// calling agent comes first and the chain carries on; only its own sender can end it; mistakes get a warning. On
// the body alone: where each kind of act plays and which way it faces, speed, acting again, throws and their end,
// and marks.
// Usage: move_test EGG...
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include "body/parts.h"
#include "body/plays.h"
#include "body/shelf.h"
#include "check.h"
#include "door/api.h"
#include "mind/brain.h"
#include "throw_kit.h"

using namespace nestbox;
using test::check;

namespace {

constexpr uint32_t FRAME_MS = 16;
constexpr int SETTLE_FRAMES = 200;
const char* const HOST = "nestbox-test.local";

// The stage of the world under test: points sent to the pet are in percent of its scene.
const Stage* g_stage = nullptr;

struct World {
    Brain brain;
    Pet pet;
    Shelf shelf;
    Canvas screen;
    Device dev;
    uint32_t t = 1000;

    World(const Display& d, const Egg& e)
        : screen(d.w, d.h), dev{brain, pet, shelf, screen, "nestbox-test", "http://nestbox-test.local", "test"} {
        pet.set_display(d);
        shelf.add(test::reloaded(e), "pet");
        shelf.put_out(0, pet, screen, 0);
        g_stage = &pet.stage();
        for (int i = 0; i < SETTLE_FRAMES; i++) frame();
    }
    void frame() {
        const Look s = brain.decide(t);
        brain.show(pet, t);
        pet.want(s, t);
        pet.update(t);
        t += FRAME_MS;
    }
    Response post(const std::string& json) { return handle_request(dev, "POST", "/v1/tell", json, t, HOST, "", false); }
    // Runs frames until `until` returns true or `max_ms` passes, calling `each` after every frame.
    void run(uint32_t max_ms, const std::function<bool()>& until, const std::function<void()>& each = [] {}) {
        for (uint32_t end = t + max_ms; t < end;) {
            frame();
            each();
            if (until()) return;
        }
    }
    void run_steps(uint32_t max_ms, const std::function<void()>& each = [] {}) {
        run(max_ms, [&] { return brain.steps_left() == 0; }, each);
    }
    bool acting_step() const { return pet.wanted() == Look::Act && pet.phase() == Pet::Phase::Acting; }
    float distance(float x, float y) const { return std::hypot(pet.x() - x, pet.y() - y); }
};

// Home, and two spots it can walk to, far from home and from each other.
struct Spots {
    float hx, hy, ax = -1, ay = -1, bx = -1, by = -1;
};

Spots find_spots(const Stage& stage, const Display& d) {
    Spots s{float(stage.home().x), float(stage.home().y)};
    auto open = [&](int x, int y) { return stage.reachable(x, y) && !stage.hidden(x, y); };
    float best = 0;
    for (int y = 2; y < d.h; y += 4)
        for (int x = 2; x < d.w; x += 4)
            if (open(x, y) && std::hypot(x - s.hx, y - s.hy) > best) {
                best = std::hypot(x - s.hx, y - s.hy);
                s.ax = x;
                s.ay = y;
            }
    best = 0;
    for (int y = 2; y < d.h; y += 4)
        for (int x = 2; x < d.w; x += 4) {
            if (!open(x, y)) continue;
            const float m = std::min<float>(std::hypot(x - s.hx, y - s.hy), std::hypot(x - s.ax, y - s.ay));
            if (m > best) {
                best = m;
                s.bx = x;
                s.by = y;
            }
        }
    return s;
}

// A screen point as the API takes it: [x, y] in percent of the pet's scene.
std::string pt(float x, float y) {
    const Point p = g_stage ? g_stage->to_scene({x, y}) : Point{x, y};
    char buf[48];
    std::snprintf(buf, sizeof buf, "[%.3f, %.3f]", p.x, p.y);
    return buf;
}

bool has(const Response& r, const std::string& part) { return r.body.find(part) != std::string::npos; }

void one_step(World& w, const Spots& s) {
    auto r = w.post(R"({"from":"t","do":{"at":)" + pt(s.ax, s.ay) + R"(,"do":"Eat","face":"left","for":2}})");
    check(r.status == 200 && has(r, "\"showing\": [\"do\""), "a step with a spot is accepted (" + r.body + ")");
    bool walked = false, at_spot = true, faced = true;
    uint32_t acting = 0;
    w.run_steps(60000, [&] {
        if (w.pet.wanted() == Look::Act && w.pet.phase() == Pet::Phase::Walking) walked = true;
        if (!w.acting_step()) return;
        acting += FRAME_MS;
        if (w.distance(s.ax, s.ay) > 3) at_spot = false;
        if (w.pet.row() != 6) faced = false;
    });
    check(walked && at_spot,
          "it walks to the spot and plays there (" + pt(w.pet.x(), w.pet.y()) + " vs " + pt(s.ax, s.ay) + ")");
    check(faced, "facing left while it plays");
    check(acting >= 1900 && acting <= 2300, "it plays for 2 s after arriving (" + std::to_string(acting) + " ms)");
    w.run(500, [] { return false; });
    check(w.brain.decide(w.t) != Look::Act, "then it goes back to its life (" + w.brain.why() + ")");
}

// Hop once at A facing back-right, eat at B saying "yum", then home facing the front.
void chain_of_steps(World& w, const Spots& s, const Egg& egg) {
    auto r = w.post(R"({"from":"t","do":[{"at":)" + pt(s.ax, s.ay) +
                    R"(,"face":"back-right","do":"Hop","times":1},)"
                    R"({"at":)" +
                    pt(s.bx, s.by) + R"(,"do":"Eat","for":1,"say":"yum"},{"at":"home","face":"front"}]})");
    check(has(r, "\"showing\": [\"do\""), "a chain of 3 is accepted (" + r.body + ")");
    const Anim* hop_anim = egg.find("Hop");
    const bool hop_turns = hop_anim && hop_anim->rows == 8;
    std::vector<int> order;  // where it acted, in order: 0 = A, 1 = B, 2 = home
    uint32_t hop = 0;
    bool yum = false, hop_row = true;
    w.run_steps(120000, [&] {
        if (!w.acting_step()) return;
        const int where = w.distance(s.ax, s.ay) < 4   ? 0
                          : w.distance(s.bx, s.by) < 4 ? 1
                          : w.distance(s.hx, s.hy) < 4 ? 2
                                                       : -1;
        if (order.empty() || order.back() != where) order.push_back(where);
        if (where == 0) {
            hop += FRAME_MS;
            if (hop_turns && w.pet.row() != 3) hop_row = false;
        }
        if (where == 1 && w.brain.saying(w.t) == "yum") yum = true;
    });
    std::string ord;
    for (int o : order) ord += std::to_string(o) + " ";
    check(order == std::vector<int>({0, 1, 2}), "it goes to A, then B, then home (" + ord + ")");
    const uint32_t one = w.pet.anim_ms("Hop");
    check(hop + 32 >= one && hop <= one + 64,
          "\"once\" plays one run of Hop (" + std::to_string(hop) + " of " + std::to_string(one) + " ms)");
    check(hop_row, "facing back-right while it hops");
    check(yum, "a step's text is said during that step");
    check(w.distance(s.hx, s.hy) < 4 && w.pet.row() == 0, "it ends at home, facing you");
}

// Three spots in a row with nothing to do at the first two: it walks straight through them, never standing, turning
// on the spot or slowing down, and its walk's clock keeps running through every change of facing.
void walks_through_waypoints(World& w, const Spots& s) {
    w.run(3000, [] { return false; });
    auto r = w.post(R"({"from":"t","do":[{"at":)" + pt(s.ax, s.ay) + R"(},{"at":)" + pt(s.bx, s.by) +
                    R"(},{"at":"home","do":"Idle","for":1}]})");
    check(has(r, "\"showing\": [\"do\""), "a chain of two waypoints and a stop is accepted (" + r.body + ")");
    bool started = false, arrived = false, stopped_on_the_way = false, odd_speed = false, clock_jumped = false;
    int facings = 0, last_row = w.pet.row();
    float px = w.pet.x(), py = w.pet.y(), clock = w.pet.stride_ms(), expected = -1, left = 0;
    size_t steps = w.brain.steps_left();
    std::string not_walking;
    w.run(60000, [&] { return arrived; }, [&] {
        w.pet.render(w.screen, w.t);
        const float moved = std::hypot(w.pet.x() - px, w.pet.y() - py);
        // it may turn on the spot to face its way before it sets off; from its first step on it keeps walking
        const bool walking = w.pet.phase() == Pet::Phase::Walking && (started || moved > 0.01f);
        if (walking) {
            started = true;
            if (w.pet.row() != last_row) facings++;
            if (std::abs((w.pet.stride_ms() - clock) - FRAME_MS) > 0.5f) clock_jumped = true;
        } else if (started && !arrived) {
            if (w.distance(s.hx, s.hy) < 4) arrived = true;
            else {
                stopped_on_the_way = true;
                not_walking = w.pet.phase_label();
            }
        }
        if (walking && started && w.pet.playing().rfind("Walk", 0) != 0) {
            stopped_on_the_way = true;
            not_walking = w.pet.playing();
        }
        // progress along its way (a straight line between two frames would be shorter at a corner); not across the
        // frames where it moves on to the next step and its way is planned afresh, nor the last few pixels
        const float travelled = left - w.pet.route_left();
        if (walking && moved > 0.01f && steps == w.brain.steps_left() && w.pet.route_left() > 6 && left > 0 &&
            travelled > 0) {
            if (expected < 0) expected = travelled;
            if (std::abs(travelled - expected) > expected * 0.1f) odd_speed = true;
        }
        left = w.pet.route_left();
        steps = w.brain.steps_left();
        last_row = w.pet.row();
        px = w.pet.x();
        py = w.pet.y();
        clock = w.pet.stride_ms();
    });
    check(started && arrived, "it walks the chain and arrives home");
    check(!stopped_on_the_way, "no standing, turning or Idle frame at the waypoints (" + not_walking + ")");
    check(!odd_speed, "the same speed all the way, frame by frame, within 10%");
    check(!clock_jumped && facings > 0,
          "its walk's clock runs on through " + std::to_string(facings) + " changes of facing");
    w.run_steps(5000);
}

// A step with "through" carves one smooth curve through its points to "at", at full speed, never stopping at a point.
void curves_through_points(World& w, const Spots& s) {
    const Stage& st = w.pet.stage();
    float tx = -1, ty = -1, best = 0;                      // a point off the straight way, with room on both legs
    for (int y = 2; y < st.height(); y += 4)
        for (int x = 2; x < st.width(); x += 4) {
            if (!st.reachable(x, y) || !st.roomy(s.hx, s.hy, x, y) || !st.roomy(x, y, s.ax, s.ay)) continue;
            const float off = std::abs((s.ax - s.hx) * (s.hy - y) - (s.hx - x) * (s.ay - s.hy)) /
                              std::max(1.0f, std::hypot(s.ax - s.hx, s.ay - s.hy));
            if (off > best && off < 40) {
                best = off;
                tx = x;
                ty = y;
            }
        }
    if (tx < 0) return;                                    // no such point in this scene
    w.run(3000, [] { return false; });
    w.run(20000, [&] { return w.distance(s.hx, s.hy) < 2 && w.pet.phase() != Pet::Phase::Walking; });
    auto r = w.post(R"({"from":"t","do":[{"at":)" + pt(s.ax, s.ay) + R"(,"through":[)" + pt(tx, ty) +
                    R"(],"speed":4},{"do":"Idle","for":1}]})");
    check(!has(r, "\"warnings\": [\""), "a step through a point is taken without a warning (" + r.body + ")");
    bool started = false, arrived = false, stopped = false;
    float nearest = 1e9f;
    w.run(20000, [&] { return arrived; }, [&] {
        const bool walking = w.pet.phase() == Pet::Phase::Walking;
        if (walking) started = true;
        if (started) nearest = std::min(nearest, w.distance(tx, ty));
        if (started && !walking) {
            if (w.distance(s.ax, s.ay) < 4) arrived = true;
            else stopped = true;
        }
    });
    check(started && arrived, "it carves the curve and arrives");
    check(nearest < 4, "passing its point (" + std::to_string(int(nearest)) + " px off)");
    check(!stopped, "at four times its pace, without a stop on the way");
    w.run_steps(5000);

    // "moving": its own animation on the way; "spin": its facing goes round as it goes.
    r = w.post(R"({"from":"t","do":[{"at":)" + pt(s.hx, s.hy) + R"(,"through":[)" + pt(tx, ty) +
               R"(],"speed":2,"moving":"Idle","spin":2},{"do":"Idle","for":1}]})");
    check(!has(r, "\"warnings\": [\""), "a spinning step is taken without a warning (" + r.body + ")");
    int rows = 0, last = -1;
    bool own = true, walked = false;
    w.run(20000, [&] { return walked && w.pet.phase() != Pet::Phase::Walking; }, [&] {
        if (w.pet.phase() != Pet::Phase::Walking) return;
        walked = true;
        w.pet.render(w.screen, w.t);
        if (w.pet.playing().rfind("Idle", 0) != 0) own = false;
        if (w.pet.drawn_row() != last) rows++;
        last = w.pet.drawn_row();
    });
    check(walked && own, "on the way it plays the step's own animation");
    check(rows >= 8, "and spins round as it goes (" + std::to_string(rows) + " facings)");
    w.run_steps(5000);
}

// Its walk's clock runs with the ground it covers: twice as fast when it hurries to its desk.
void walk_clock_follows_the_ground(World& w) {
    w.post(R"({"from":"t","state":"over"})");
    w.brain.hold(Look::Wander);
    w.run(1500, [] { return false; });
    w.brain.hold(Look::Type);
    float walked = 0, clock = 0, last = w.pet.stride_ms();
    w.run(20000, [&] { return w.pet.phase() == Pet::Phase::Acting && w.pet.wanted() == Look::Type; }, [&] {
        if (w.pet.phase() == Pet::Phase::Walking && w.pet.wanted() == Look::Type) {
            walked += FRAME_MS;
            clock += w.pet.stride_ms() - last;
        }
        last = w.pet.stride_ms();
    });
    if (walked > 3 * FRAME_MS)
        check(std::abs(clock - 2 * walked) <= 2 * FRAME_MS,
              "hurrying, its walk's clock runs twice as fast (" + std::to_string(int(clock)) + " ms in " +
                  std::to_string(int(walked)) + " ms)");
    w.brain.automatic();
    w.run(2000, [] { return false; });
}

// Turning on the spot goes one facing at a time, at about one step of its walk each.
void turns_at_walking_pace(World& w, const Egg& egg) {
    w.run(1500, [] { return false; });
    w.post(R"({"from":"t","do":{"do":"Idle","face":"front","for":1}})");
    w.run_steps(5000);
    w.post(R"({"from":"t","do":{"do":"Idle","face":"back","for":1}})");
    std::vector<uint32_t> when;
    std::vector<int> rows{w.pet.row()};
    w.run_steps(8000, [&] {
        if (w.pet.row() != rows.back()) {
            rows.push_back(w.pet.row());
            when.push_back(w.t);
        }
    });
    const Anim* walk = egg.find("Walk");
    const uint32_t step_ms = walk ? std::clamp<uint32_t>(ticks_to_ms(walk->total_ticks()) / 4, 90, 250) : 90;
    bool adjacent = true, paced = true;
    for (size_t i = 1; i < rows.size(); i++) {
        const int d = (rows[i] - rows[i - 1] + 8) % 8;
        adjacent = adjacent && (d == 1 || d == 7);
    }
    for (size_t i = 1; i < when.size(); i++) paced = paced && when[i] - when[i - 1] + FRAME_MS >= step_ms;
    check(rows.size() == 5 && rows.back() == 4,
          "it turns from the front to its back (" + std::to_string(rows.size() - 1) + " steps)");
    check(adjacent, "one facing at a time");
    check(paced, "about one walking step per facing (" + std::to_string(step_ms) + " ms)");

    // Setting off from standing toward its back: it turns through the facings first, then walks; a way within
    // one facing of where it looks it just takes.
    w.post(R"({"from":"t","do":{"at":"home","do":"Idle","face":"front","for":1}})");
    w.run_steps(30000);
    const Stage& st = w.pet.stage();
    const float hx = w.pet.x(), hy = w.pet.y();
    Point behind{-1, -1};
    for (float dy = 24; dy <= 60 && behind.x < 0; dy += 4)
        if (st.reachable(hx, hy - dy) && st.line_clear(hx, hy, hx, hy - dy)) behind = {hx, hy - dy};
    if (!check(behind.x >= 0, "there is room behind home to walk to")) return;
    // what is drawn, frame by frame, from the moment it is asked until its first step
    auto set_off = [&](const std::string& why, const std::function<void()>& ask, int toward) {
        const float sx = w.pet.x(), sy = w.pet.y();
        w.pet.render(w.screen, w.t);
        std::vector<int> drawn{w.pet.drawn_row()};
        std::vector<uint32_t> held{0};
        int first_row = -1;
        ask();
        w.run(8000, [&] { return first_row >= 0; }, [&] {
            w.pet.render(w.screen, w.t);
            if (w.pet.x() != sx || w.pet.y() != sy) {
                first_row = w.pet.drawn_row();
                return;
            }
            if (w.pet.drawn_row() != drawn.back()) {
                drawn.push_back(w.pet.drawn_row());
                held.push_back(0);
            }
            held.back() += FRAME_MS;
        });
        bool one = true, slow = true;
        for (size_t i = 1; i < drawn.size(); i++) {
            const int d = (drawn[i] - drawn[i - 1] + 8) % 8;
            one = one && (d == 1 || d == 7);
        }
        for (size_t i = 1; i < held.size(); i++) slow = slow && held[i] + FRAME_MS >= step_ms;  // each facing turned to
        std::string seq;
        for (int r : drawn) seq += std::to_string(r) + " ";
        const int off = first_row < 0 ? 8 : std::min((first_row - toward + 8) % 8, (toward - first_row + 8) % 8);
        check(off <= 1,
              why + ": the first step is drawn facing its way (row " + std::to_string(first_row) + ")");
        check(drawn.size() >= 3 && one && slow,
              why + ": before it, every facing in between is drawn, one at a time, a walking step each (" + seq + ")");
    };
    set_off("setting off behind it", [&] {
        w.post(R"({"from":"t","do":{"at":)" + pt(behind.x, behind.y) + R"(,"do":"Idle","face":"back","for":1}})");
    }, 4);
    w.run_steps(20000);
    set_off("walking back to work, facing away", [&] { w.brain.hold(Look::Type); }, 0);
    w.run(8000, [] { return false; });
    w.brain.automatic();
    w.post(R"({"from":"t","state":"over"})");
    w.run(2000, [] { return false; });
}

// Out to a spot and straight back again: it doesn't flip round on the spot. Its facing passes through every facing
// in between, one at a time; its way curves round (no frame reverses the way it moves); its speed stays even.
void turns_back_smoothly(World& w, const Spots& s) {
    w.run(2000, [] { return false; });
    // a turning point on the way to A with open floor all round it, so a curve has room; on a floor too narrow for
    // that along the way, the farthest such point in a straight line from home
    const Stage& st = w.pet.stage();
    auto open = [&](Point p) {
        bool all = std::hypot(p.x - s.hx, p.y - s.hy) > 30;
        for (int k = 0; k < 16 && all; k++)
            all = st.can_stand(p.x + 16 * std::cos(k * 0.3927f), p.y + 16 * std::sin(k * 0.3927f));
        return all;
    };
    Point out{-1, -1};
    for (float f = 0.3f; f <= 0.9f && out.x < 0; f += 0.05f) {
        const Point p{s.hx + (s.ax - s.hx) * f, s.hy + (s.ay - s.hy) * f};
        if (open(p)) out = p;
    }
    const bool along = out.x >= 0;
    float farthest = 0;
    for (int y = 2; y < st.height() && !along; y += 4)
        for (int x = 2; x < st.width(); x += 4) {
            const Point p{float(x), float(y)};
            const float far = std::hypot(p.x - s.hx, p.y - s.hy);
            if (far > farthest && open(p) && st.reachable(p.x, p.y) && st.line_clear(s.hx, s.hy, p.x, p.y)) {
                farthest = far;
                out = p;
            }
        }
    if (!check(out.x >= 0, "there is open floor to turn back on")) return;
    w.post(R"({"from":"t","do":[{"at":"home","do":"Idle","for":0.5}]})");
    w.run_steps(30000);
    w.post(R"({"from":"t","do":[{"at":)" + pt(out.x, out.y) + R"(},{"at":"home","do":"Idle","for":0.5}]})");
    std::vector<Point> track;
    std::vector<int> rows;
    std::vector<float> progress;                         // along its way, on frames that keep the same way
    float left = 0;
    size_t steps = w.brain.steps_left();
    w.run_steps(60000, [&] {
        if (w.pet.phase() != Pet::Phase::Walking) return;
        const float travelled = left - w.pet.route_left();
        if (!track.empty() && steps == w.brain.steps_left() && travelled > 0 && w.pet.route_left() > 6)
            progress.push_back(travelled);
        left = w.pet.route_left();
        steps = w.brain.steps_left();
        track.push_back({w.pet.x(), w.pet.y()});
        rows.push_back(w.pet.row());
    });
    bool one_facing = true, flipped = false, even = !progress.empty();
    for (size_t i = 1; i < track.size(); i++) {
        const int d = (rows[i] - rows[i - 1] + 8) % 8;
        one_facing = one_facing && (d == 0 || d == 1 || d == 7);
        const float vx = track[i].x - track[i - 1].x, vy = track[i].y - track[i - 1].y;
        if (i >= 2) {
            const float ux = track[i - 1].x - track[i - 2].x, uy = track[i - 1].y - track[i - 2].y;
            if (ux * vx + uy * vy < 0) flipped = true;
        }
    }
    for (float p : progress) even = even && std::abs(p - progress.front()) <= progress.front() * 0.1f;
    std::set<int> seen(rows.begin(), rows.end());
    check(track.size() > 20, "it walks out and back (" + std::to_string(track.size()) + " frames)");
    check(one_facing && seen.size() >= 4,
          "turning back, its facing passes one at a time through " + std::to_string(seen.size()) + " facings");
    check(!flipped, "its way curves round: no frame reverses the way it moves");
    check(even, "its speed stays even through the turn");
}

void unreachable_spot(World& w) {
    w.post(R"({"from":"t","do":{"at":[-200,-200],"do":"Pose","for":1}})");
    const uint32_t start = w.t;
    w.run_steps(70000);
    check(w.brain.steps_left() == 0 && w.t - start < 45000,
          "a spot off the floor: it goes as near as it can, then carries on");
    check(w.pet.stage().can_stand(w.pet.x(), w.pet.y()), "and stands where it may stand");
}

void eight_facings(World& w) {
    const char* const faces[] = {"front",  "front-right", "right", "back-right",
                                 "back", "back-left",   "left",  "front-left"};
    bool all = true;
    std::string got;
    for (int k = 0; k < 8; k++) {
        w.post(std::string(R"({"from":"t","do":{"do":"Walk","face":")") + faces[k] + R"(","for":0.5}})");
        int row = -1;
        w.run_steps(10000, [&] {
            if (w.acting_step()) row = w.pet.row();
        });
        if (row != k) all = false;
        got += std::to_string(row) + " ";
    }
    check(all, "the 8 faces turn it the 8 ways (" + got + ")");
}

// A point far off the screen: it stays on screen. A look after another look goes to its own place.
void off_screen_and_look_after_look(World& w, const Spots& s, const Display& d) {
    w.post(R"({"from":"t","do":{"at":[50,1000],"for":1}})");
    bool left = false;
    w.run_steps(60000, [&] {
        if (w.pet.y() > d.h + 10 || w.pet.y() < -10 || w.pet.x() < -10 || w.pet.x() > d.w + 10) left = true;
    });
    check(!left, "a point far off the screen: it stays on screen");
    w.post(R"({"from":"t","do":{"at":)" + pt(s.ax, s.ay) + R"(}})");
    w.run_steps(60000);
    w.post(R"({"from":"t","do":[{"do":"celebrate","for":1},{"do":"type","for":4}]})");
    bool typed_at_home = false;
    w.run_steps(60000, [&] {
        if (w.acting_step() && w.pet.current_act().look == Look::Type && w.distance(s.hx, s.hy) < 3)
            typed_at_home = true;
    });
    check(typed_at_home, "a look after a look goes to its own place (type: home)");
}

void anim_where_it_stands(World& w) {
    const float x0 = w.pet.x(), y0 = w.pet.y();
    auto r = w.post(R"({"from":"t","do":{"do":"Hop"}})");
    uint32_t acting = 0;
    bool moved = false;
    w.run_steps(20000, [&] {
        if (w.distance(x0, y0) > 1) moved = true;
        if (w.acting_step()) acting += FRAME_MS;
    });
    const uint32_t once = w.pet.anim_ms("Hop");
    check(has(r, "\"showing\": [\"do\"") && !moved && acting + 3 * FRAME_MS >= once && acting <= once + 6 * FRAME_MS,
          "{\"do\": \"Hop\"}, one of its own animations, plays once where it stands (" + std::to_string(acting) +
              " of " + std::to_string(once) + " ms)");
}

void reactions_and_agents_come_first(World& w, const Spots& s) {
    w.post(R"({"from":"t","do":{"at":)" + pt(s.bx, s.by) + R"(,"do":"Eat","for":3}})");
    w.run(60000, [&] { return w.acting_step(); });
    w.run(1000, [] { return false; });
    w.post(R"({"from":"ci","state":"done"})");
    w.frame();
    check(w.pet.wanted() != Look::Act || w.brain.decide(w.t) != Look::Act, "a reaction plays first");
    uint32_t acting = 0;
    w.run_steps(60000, [&] {
        if (w.acting_step()) acting += FRAME_MS;
    });
    check(acting >= 1800 && acting <= 2300,
          "then the step plays the rest of its time, not all of it again (" + std::to_string(acting) + " ms)");
    w.post(R"({"from":"t","do":{"do":"Eat","for":30}})");
    w.post(R"({"from":"claude","id":"s1","state":"asking","say":"ok?"})");
    check(w.brain.decide(w.t) == Look::Alert, "an agent calling comes before a step");
    w.post(R"({"from":"claude","id":"s1","state":"over"})");
    check(w.brain.decide(w.t) == Look::Act, "and the step carries on after");
}

void only_its_sender_stops_it(World& w) {
    w.post(R"({"from":"other","state":"over"})");
    w.post(R"({"from":"other","state":"over"})");
    check(w.brain.steps_left() == 1, "another sender's end leaves it alone");
    w.post(R"({"from":"t","state":"over"})");
    check(w.brain.steps_left() == 0, "its own sender's end ends it");
}

void mistakes_get_warnings(World& w) {
    auto r = w.post(R"({"from":"t","do":{"do":"Idle","face":"sideways"}})");
    check(has(r, "unknown direction"), "an unknown direction gets a warning");
    r = w.post(R"({"from":"t","do":[{"do":"Idle"},{"do":"Moonwalk"}]})");
    check(has(r, "has no \\\"Moonwalk\\\""), "an animation it lacks, in a step, gets a warning (" + r.body + ")");
    w.post(R"({"from":"t","state":"over"})");
}

// Throws, laps and wandering sent with "do" end on their own: a throw without a kit after a happy look (with a
// warning), laps once they are run, wandering after its seconds.
void steps_that_end_themselves(World& w) {
    w.run(500, [] { return false; });
    auto r = w.post(R"({"from":"t","do":{"do":"throw","speed":2}})");
    check(has(r, "\"showing\": [\"do\"") && has(r, "cannot throw"),
          "a throw without a kit is accepted with a warning (" + r.body + ")");
    uint32_t acting = 0;
    w.run_steps(20000, [&] {
        if (w.acting_step()) acting += FRAME_MS;
    });
    check(w.brain.steps_left() == 0 && acting <= 3 * FRAME_MS,
          "the step is skipped at once (" + std::to_string(acting) + " ms)");
    r = w.post(R"({"from":"t","do":{"do":"run","times":2}})");
    const uint32_t start = w.t;
    check(has(r, "\"showing\": [\"do\""), "laps are accepted (" + r.body + ")");
    w.run_steps(60000);
    check(w.brain.steps_left() == 0 && w.t - start < 60000, "the laps step ends once they are run");
    r = w.post(R"({"from":"t","do":{"do":"stroll","for":2}})");
    bool free = false;
    uint32_t wandering = 0;
    w.run_steps(10000, [&] {
        if (w.pet.wanted() == Look::Act && w.pet.phase() == Pet::Phase::Free) {
            free = true;
            wandering += FRAME_MS;
        }
    });
    check(has(r, "\"showing\": [\"do\"") && free && wandering >= 1900 && wandering <= 2300,
          "wandering roams in its free time for its seconds (" + std::to_string(wandering) + " ms)");
}

void pets_json_describes_anims(World& w) {
    auto p = handle_request(w.dev, "GET", "/v1/pets/pet", "", w.t, HOST);
    check(has(p, "{\"name\": \"Walk\", \"directions\": 8, \"ms\": "),
          "GET /v1/pets/NAME: directions and length per animation");
}

// The body on its own, without a brain: what each kind of Act does, frame by frame.
struct Bare {
    Pet pet;
    Canvas screen;
    uint32_t t = 1000;
    int arrival_row = -1;                // the facing on its last walking frame
    Bare(const Display& d, const Egg& e) : screen(d.w, d.h) {
        pet.set_display(d);
        pet.set_egg(&e);
        pet.want(Look::Idle, t);
        for (int i = 0; i < SETTLE_FRAMES; i++) frame();
    }
    void frame() {
        pet.update(t);
        if (pet.phase() == Pet::Phase::Walking) arrival_row = pet.row();
        t += FRAME_MS;
    }
    // Runs until `until` or `max_ms`; true if `until` came.
    bool run(uint32_t max_ms, const std::function<bool()>& until, const std::function<void()>& each = [] {}) {
        for (uint32_t end = t + max_ms; t < end;) {
            frame();
            each();
            if (until()) return true;
        }
        return false;
    }
    bool acting() const { return pet.phase() == Pet::Phase::Acting; }
    // Asks for `a`, then runs until it acts; true if it does.
    bool act(const Act& a, uint32_t max_ms = 60000) {
        arrival_row = -1;
        pet.act(a);
        pet.want(Look::Act, t);
        return run(max_ms, [&] { return acting(); });
    }
    // The row the last rendered frame drew.
    int drawn() {
        pet.render(screen, t);
        return pet.drawn_row();
    }
    float distance(Point p) const { return std::hypot(pet.x() - p.x, pet.y() - p.y); }
};

Act anim_act(const std::string& anim, Spot spot = {}) {
    Act a;
    a.anim = anim;
    a.spot = spot;
    return a;
}

Act look_act(Look l, Spot spot = {}) {
    Act a;
    a.kind = Act::Kind::Look;
    a.look = l;
    a.spot = spot;
    return a;
}

Spot point(float x, float y) { return {Spot::Kind::Point, x, y}; }

int rows_of(const Egg& e, const std::string& anim) { return e.find(anim) ? e.find(anim)->rows : 0; }

// An animation sent somewhere plays facing the way it arrived; sent nowhere, it turns to the front. A look plays at
// its own place (typing: home, facing its desk) facing its own way, or at a spot facing the way it arrived; a face
// given turns it that way in both. The desk spot is home facing the desk.
void acts_face_and_place(const Display& d, const Egg& egg, const Spots& s, const std::string& where) {
    Bare b(d, egg);
    const Stage& st = b.pet.stage();
    const Point a = st.nearest_standable({s.ax, s.ay}), home{float(st.home().x), float(st.home().y)};
    check(b.act(anim_act("Eat", point(s.ax, s.ay))) && b.distance(a) < 3,
          where + ": an animation is played at its spot");
    const int arrived = b.arrival_row;
    bool kept = true;
    b.run(1000, [] { return false; }, [&] { kept = kept && b.pet.row() == arrived; });
    check(arrived >= 0 && kept && (rows_of(egg, "Eat") < 8 || b.drawn() == arrived),
          where + ": facing the way it arrived (" + std::to_string(arrived) + ", now " +
              std::to_string(b.pet.row()) + ")");
    check(b.act(anim_act("Eat")) && b.distance(a) < 3 && b.pet.row() == 0 && b.drawn() == 0,
          where + ": the same animation here turns to the front where it stands");

    check(b.act(look_act(Look::Type)) && b.distance(home) < 3, where + ": a look goes to its own place");
    const int desk = st.desk_row(b.pet.x(), b.pet.y());
    check(b.pet.row() == desk, where + ": typing at home, facing its desk (row " + std::to_string(b.pet.row()) +
                                   ", desk " + std::to_string(desk) + ")");
    const Point bp = st.nearest_standable({s.bx, s.by});
    check(b.act(look_act(Look::Type, point(s.bx, s.by))) && b.distance(bp) < 3 &&
              b.pet.row() == st.desk_row(b.pet.x(), b.pet.y()),
          where + ": typing sent to a spot goes there and faces its desk from there");
    // a strike borrowed for fighting faces right where it stands, the look's other plays the front
    for (Look l : {Look::Eat, Look::Fight}) {
        // eating walks from B to A, fighting from A to B: two different ways of arriving
        const bool eat = l == Look::Eat;
        const Point from = eat ? Point{s.bx, s.by} : Point{s.ax, s.ay},
                    to = eat ? Point{s.ax, s.ay} : Point{s.bx, s.by};
        const Point there = st.nearest_standable(to);
        const Play own_play = play_for(egg, info(l)).play;
        const std::string anim = own_play.anim, what = where + ": " + look_key(l);
        const bool turns = rows_of(egg, anim) >= 8;
        b.act(anim_act("Idle", point(from.x, from.y)));
        check(b.act(look_act(l, point(to.x, to.y))) && b.distance(there) < 3, what + " sent to a spot goes there");
        const int came = b.arrival_row;
        b.run(500, [] { return false; });
        check(came >= 0 && b.pet.row() == came && (!turns || b.drawn() == came),
              what + " plays there facing the way it arrived (" + std::to_string(came) + "; row " +
                  std::to_string(b.pet.row()) + ", drawn " + std::to_string(b.drawn()) + ")");
        Act faced = look_act(l, point(to.x, to.y));
        faced.row = 6;
        check(b.act(faced) && b.pet.row() == 6 && (!turns || b.drawn() == 6),
              what + " given a face plays facing it (row " + std::to_string(b.pet.row()) + ", drawn " +
                  std::to_string(b.drawn()) + ")");
        Act own = look_act(l);
        const int own_row = own_play.facing == Facing::Right ? ROW_RIGHT : ROW_FRONT;
        check(b.act(own) && b.pet.row() == own_row && (!turns || b.drawn() == own_row),
              what + " sent nowhere turns its own way where it stands (row " + std::to_string(b.pet.row()) + ")");
    }

    check(b.act(anim_act("Idle", {Spot::Kind::Desk})) && b.distance(home) < 3 &&
              b.pet.row() == st.desk_row(b.pet.x(), b.pet.y()),
          where + ": the desk spot is home, facing its desk");
    const Point here{b.pet.x(), b.pet.y()};
    check(b.act(anim_act("Idle", {Spot::Kind::Target})) && b.distance(here) < 1,
          where + ": without a target in its scene, the target spot is where it stands");
}

// The target spot is the nearest standable point to the scene's target.
void act_at_target(const Display& d, const Egg& egg, const std::string& where) {
    if (!egg.has_scene) return;
    test::KitOptions o;
    o.flight = Flight::Splash;
    o.target = test::scene_point(egg, d, {float(d.w / 4), float(d.h * 3 / 4)});
    const Egg kit = test::with_throw(egg, o);
    Bare b(d, kit);
    const Pixel target = *b.pet.stage().target();
    const Point near = b.pet.stage().nearest_standable({float(target.x), float(target.y)});
    check(b.act(anim_act("Idle", {Spot::Kind::Target})) && b.distance(near) < 3,
          where + ": the target spot walks to the standable point nearest the target");
}

// The fastest step while walking to a spot (an Act at `speed`), or while wandering at `speed`.
float act_step(const Display& d, const Egg& e, const Spots& sp, float speed, bool wander) {
    Bare b(d, e);
    Act a = anim_act("Idle", point(sp.ax, sp.ay));
    if (wander) {
        a = Act();
        a.kind = Act::Kind::Wander;
    }
    a.speed = speed;
    b.pet.act(a);
    b.pet.want(Look::Act, b.t);
    float most = 0;
    bool free = true;
    for (int i = 0; i < 1500; i++) {
        const float x = b.pet.x(), y = b.pet.y();
        b.frame();
        free = free && (!wander || b.pet.phase() == Pet::Phase::Free);
        if (b.pet.phase() == Pet::Phase::Walking || wander)
            most = std::max(most, std::hypot(b.pet.x() - x, b.pet.y() - y));
    }
    return free ? most : -1;
}

// Speed scales the walk to a spot, wandering and the animation, from 0.25 to 4 times.
void act_speed(const Display& d, const Egg& egg, const Spots& sp, const std::string& where) {
    const float pace = (egg.pace > 0 ? egg.pace : WALK_PX_S) * FRAME_MS / 1000;
    auto near = [](float got, float want) { return std::abs(got - want) <= 0.05f; };
    const std::pair<float, float> speeds[] = {{2, 2}, {9, Pet::MAX_ACT_SPEED}, {0.1f, Pet::MIN_ACT_SPEED}};
    for (auto [asked, used] : speeds) {
        const float walk = act_step(d, egg, sp, asked, false);
        check(near(walk, pace * used), where + ": speed " + std::to_string(asked) + " walks at " +
                                           std::to_string(used) + " times its pace (" + std::to_string(walk) + ")");
    }
    const float wander = act_step(d, egg, sp, 2, true);
    check(near(wander, pace * 2), where + ": wandering at speed 2 roams in its free time at twice its pace (" +
                                      std::to_string(wander) + ")");
    Bare one(d, egg), two(d, egg);
    Act a = anim_act("Hop");
    one.act(a);
    a.speed = 2;
    two.act(a);
    check(two.pet.current_act().speed == 2 && one.pet.run_ms() == one.pet.anim_ms("Hop") &&
              std::abs(int(two.pet.run_ms()) - int(one.pet.anim_ms("Hop") / 2)) <= 1,
          where + ": at speed 2 one run of the animation takes half as long (" + std::to_string(two.pet.run_ms()) +
              " ms)");
    bool same = one.t == two.t;
    const uint32_t start = one.t - FRAME_MS;   // when both started acting
    for (uint32_t k = 0; k < 600 && same; k += 40) {
        one.pet.render(one.screen, start + 2 * k);
        two.pet.render(two.screen, start + k);
        same = one.screen.px == two.screen.px;
    }
    check(same, where + ": and its frames come twice as fast");
    Act wild = a;
    wild.speed = 100;
    wild.row = 12;
    wild.count = -3;
    two.pet.act(wild);
    check(two.pet.current_act().speed == Pet::MAX_ACT_SPEED && two.pet.current_act().row == 7 &&
              two.pet.current_act().count == 1,
          where + ": speed, facing and count are kept in range");
}

// An equal act asked again changes nothing unless `again`, which plays it from the start; asked again on its way
// to a spot, it still gets there.
void act_again(const Display& d, const Egg& egg, const Spots& sp, const std::string& where) {
    Bare b(d, egg);
    const Point a = b.pet.stage().nearest_standable({sp.ax, sp.ay});
    const Act go = anim_act("Eat", point(sp.ax, sp.ay));
    b.pet.act(go);
    b.pet.want(Look::Act, b.t);
    b.run(5000, [&] { return b.pet.phase() == Pet::Phase::Walking && b.pet.route_left() > 20; });
    b.pet.act(go, true);
    check(b.run(60000, [&] { return b.acting(); }) && b.distance(a) < 3,
          where + ": asked again on the way, it still walks to its spot");
    test::KitOptions o;
    o.flight = Flight::Toss;
    const Egg kit = test::with_throw(egg, o);
    Bare k(d, kit);
    Act toss;
    toss.kind = Act::Kind::Throw;
    check(k.act(toss), where + ": a toss starts");
    const uint32_t cycle = ticks_to_ms(test::cycle_ticks(kit));
    k.run(cycle + 100, [] { return false; });
    check(k.pet.act_done(k.t), where + ": one toss is done after one throw");
    k.pet.act(toss);
    k.frame();
    check(k.pet.act_done(k.t), where + ": the same act asked again changes nothing");
    k.pet.act(toss, true);
    k.run(2000, [&] { return k.acting(); });
    const uint32_t restart = k.t;
    check(!k.pet.act_done(k.t) && k.run(cycle + 100, [&] { return k.pet.act_done(k.t); }) &&
              k.t - restart + 2 * FRAME_MS >= cycle,
          where + ": asked again with `again`, it throws once more from the start");
}

// A throw ends after its count of throws at its speed; without a kit it is skipped at once.
void act_throws(const Display& d, const Egg& egg, const std::string& where) {
    for (Flight f : {Flight::Toss, Flight::Hoop}) {
        test::KitOptions o;
        o.flight = f;
        if (f == Flight::Hoop) {
            if (!egg.has_scene) continue;
            o.target = test::scene_point(egg, d, {float(d.w * 3 / 4), float(d.h / 3)});
        }
        const Egg kit = test::with_throw(egg, o);
        Bare b(d, kit);
        if (!check(b.pet.can_throw(), where + ": a test kit it can throw")) continue;
        Act a;
        a.kind = Act::Kind::Throw;
        a.count = 3;
        a.speed = 2;
        check(b.act(a), where + ": it gets ready to throw");
        const uint32_t start = b.t - FRAME_MS, cycle = ticks_to_ms(test::cycle_ticks(kit));
        check(std::abs(int(b.pet.run_ms()) - int(cycle / 2)) <= 1,
              where + ": one throw at speed 2 takes half its cycle (" + std::to_string(b.pet.run_ms()) + " ms)");
        bool early = false;
        b.run(
            20000, [&] { return b.pet.act_done(b.t); },
            [&] { early = early || (b.pet.act_done(b.t) && b.t - start < 3 * cycle / 2 - 2 * FRAME_MS); });
        const uint32_t took = b.t - start;
        check(!early && took + 2 * FRAME_MS >= 3 * cycle / 2 && took <= 3 * cycle / 2 + 2 * FRAME_MS,
              where + ": three throws at speed 2 end after 1.5 cycles (" + std::to_string(took) + " of " +
                  std::to_string(3 * cycle / 2) + " ms)");
    }
    Bare b(d, egg);
    check(!b.pet.can_throw(), where + ": an egg without a kit cannot throw");
    Act a;
    a.kind = Act::Kind::Throw;
    a.speed = 2;
    b.act(a);
    const uint32_t start = b.t - FRAME_MS;
    b.run(20000, [&] { return b.pet.act_done(b.t); });
    check(b.t - start <= 3 * FRAME_MS, where + ": a throw without a kit is done at once (" +
                                            std::to_string(b.t - start) + " ms)");
}

// Only laps and throws end by themselves.
void acts_without_end(const Display& d, const Egg& egg, const std::string& where) {
    Bare b(d, egg);
    Act w;
    w.kind = Act::Kind::Wander;
    b.pet.act(w);
    b.pet.want(Look::Act, b.t);
    bool done = false;
    b.run(5000, [] { return false; }, [&] { done = done || b.pet.act_done(b.t); });
    check(!done, where + ": wandering never ends by itself");
    b.act(anim_act("Hop"));
    b.run(5000, [] { return false; }, [&] { done = done || b.pet.act_done(b.t); });
    check(!done, where + ": nor does an animation");
}

// A sign sent with "mark" is drawn when the look wears none of its own.
void marks(const Display& d, const Egg& egg, const std::string& where) {
    Bare plain(d, egg), marked(d, egg);
    marked.pet.mark(Bubble::Heart);
    plain.pet.render(plain.screen, plain.t);
    marked.pet.render(marked.screen, marked.t);
    check(plain.screen.px != marked.screen.px, where + ": a mark is drawn over the pet");
    for (Bare* b : {&plain, &marked}) b->act(look_act(Look::Happy));
    marked.pet.mark(Bubble::Cross);
    plain.pet.render(plain.screen, plain.t);
    marked.pet.render(marked.screen, marked.t);
    check(info(Look::Happy).bubble != Bubble::None && plain.screen.px == marked.screen.px,
          where + ": a look's own sign comes first");
}

// Walking speed, frame by frame on a bare pet: a walk to a spot goes at its pace (the egg's own, else 66 px/s) and
// hurrying to a look's spot at twice that.
float fastest_step(const Display& d, const Egg& e, Look s, const Spots& sp) {
    Pet pet;
    pet.set_display(d);
    pet.set_egg(&e);
    uint32_t t = 1000;
    pet.want(Look::Idle, t);
    for (int i = 0; i < SETTLE_FRAMES; i++, t += FRAME_MS) pet.update(t);
    if (s == Look::Act) {
        Act a;
        a.anim = "Idle";
        a.spot = {Spot::Kind::Point, sp.ax, sp.ay};
        pet.act(a);
    }
    pet.want(s, t);
    float most = 0;
    for (int i = 0; i < 2000; i++, t += FRAME_MS) {
        const float x = pet.x(), y = pet.y();
        pet.update(t);
        if (pet.phase() == Pet::Phase::Walking) most = std::max(most, std::hypot(pet.x() - x, pet.y() - y));
    }
    return most;
}

void walks_at_its_pace(const Display& d, const Egg& egg, const Spots& sp) {
    constexpr float HURRY = 2, PACED = 40;     // body/move.cpp
    Egg own = test::reloaded(egg), slow = test::reloaded(egg);
    own.pace = 0;
    slow.pace = PACED;
    auto near = [](float got, float want) { return std::abs(got - want) <= 0.05f; };
    const float walk = fastest_step(d, own, Look::Act, sp), paced = fastest_step(d, slow, Look::Act, sp);
    check(near(walk, WALK_PX_S * FRAME_MS / 1000), "without a pace it walks at 66 px/s (" + std::to_string(walk) + ")");
    check(near(paced, PACED * FRAME_MS / 1000),
          "with a pace of 40 it walks at 40 px/s (" + std::to_string(paced) + ")");
    const float hurry = fastest_step(d, slow, Look::Type, sp);
    check(near(hurry, HURRY * PACED * FRAME_MS / 1000),
          "it hurries to its desk at twice its own pace (" + std::to_string(hurry) + ")");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: move_test EGG...\n");
        return 2;
    }
    for (int a = 1; a < argc; a++) {
        Egg egg;
        if (!test::load_egg_file(argv[a], egg)) return 1;
        egg = test::without_throw(egg);   // the throw is checked with test kits; these checks want one that can't
        for (auto& d : displays()) {
            std::printf("%s on %s\n", egg.name.c_str(), d.id.c_str());
            World w(d, egg);
            const Spots s = find_spots(w.pet.stage(), d);
            one_step(w, s);
            chain_of_steps(w, s, egg);
            unreachable_spot(w);
            eight_facings(w);
            anim_where_it_stands(w);
            off_screen_and_look_after_look(w, s, d);
            reactions_and_agents_come_first(w, s);
            only_its_sender_stops_it(w);
            mistakes_get_warnings(w);
            pets_json_describes_anims(w);
            steps_that_end_themselves(w);
            walks_at_its_pace(d, egg, s);
            walks_through_waypoints(w, s);
            walk_clock_follows_the_ground(w);
            turns_at_walking_pace(w, egg);
            turns_back_smoothly(w, s);
            curves_through_points(w, s);
            const std::string where = egg.name + " on " + d.id;
            acts_face_and_place(d, egg, s, where);
            act_at_target(d, egg, where);
            act_speed(d, egg, s, where);
            act_again(d, egg, s, where);
            act_throws(d, egg, where);
            acts_without_end(d, egg, where);
            marks(d, egg, where);
        }
    }
    return test::summary();
}
