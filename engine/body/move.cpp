// Getting from one look to the next: where a look happens, walking there, turning, then acting; free roaming.
#include <algorithm>
#include <cmath>
#include "body/parts.h"
#include "body/pet.h"

namespace nestbox {

namespace {
constexpr int WATCH_STEP_X = 14, WATCH_STEP_Y = 6;   // watching: a step back from home, away from the desk
constexpr float ARRIVED_PX = 2;
constexpr float CURVE_STEP_PX = 4;                   // a curve through points: one point this far apart, art pixels
constexpr float HURRY = 2.0f;                        // walking speed to a look's spot, times its own pace
constexpr float LAP_PACE = 3.0f;                     // running laps with a run of its own, times its own pace
constexpr float WALK_LAP_PACE = 1.8f;                // running laps on its walk: as fast as its legs still look right
constexpr int MAX_LAPS = 6;                          // a reaction's laps, fitted to its length
constexpr int MAX_ASKED_LAPS = 20;                   // laps a step may ask for
constexpr float LAP_SPARE_MS = 600;                  // a reaction's laps leave this much of it unused
constexpr float LAP_REST_MS = 2500, LAP_REST_EXTRA_MS = 2500;   // a mood: standing about between laps
constexpr uint32_t MAX_FRAME_MS = 100;               // longer gaps between updates are treated as this long
constexpr uint32_t MAX_TURN_STEP_MS = 250;           // turning on the spot is never slower than this a facing
constexpr float LOOK_AHEAD = 5;                      // a rounded corner's reach either side, art pixels
constexpr int ARC_POINTS = 4;                        // points along a rounded corner
constexpr float SCENE_FADE_MS = 1500;                // the time of day's cross-fade and the night's dimming

// Free roaming.
constexpr float FIRST_PAUSE_MS = 1200, RETRY_PAUSE_MS = 500;
constexpr int ROAM_TRIES = 40;
constexpr float STROLL_MIN_PX = 25, STROLL_EXTRA_PX = 45, STROLL_FLATTEN = 0.6f;   // idle: an ellipse round the pet
constexpr float WANDER_PACE = 1.0f, IDLE_PACE = 0.8f;
constexpr float SWIM_PACE = 0.42f;                   // swimming, times its own pace
constexpr float WANDER_REST_MS = 700, WANDER_REST_EXTRA_MS = 1800;   // after each leg
constexpr float IDLE_REST_MS = 4000, IDLE_REST_EXTRA_MS = 6000;

// Typing on its screen: characters per second, following the user's keys per second.
constexpr float TYPED_UNKNOWN_KPS = 4, TYPED_MIN = 2, TYPED_MAX = 12;

// The length of the way from `from` through every point of `route`.
float route_length(Point from, const std::vector<Point>& route) {
    float d = 0;
    for (const Point& p : route) {
        d += std::hypot(p.x - from.x, p.y - from.y);
        from = p;
    }
    return d;
}
}  // namespace

Pet::Goal Pet::watch_goal(Pixel home) const {
    Goal g;
    g.spot = true;
    g.x = home.x;
    g.y = home.y;
    if (const std::optional<Pixel> w = stage_.watch_spot(); w && stage_.reachable(w->x, w->y)) {   // the scene's own
        g.x = w->x;
        g.y = w->y;
        return g;
    }
    const Point desk = stage_.desk_point();
    const int away = home.x >= desk.x ? WATCH_STEP_X : -WATCH_STEP_X;
    if (stage_.reachable(home.x + away, home.y + WATCH_STEP_Y)) {
        g.x = home.x + away;
        g.y = home.y + WATCH_STEP_Y;
    } else if (stage_.reachable(home.x - away, home.y + WATCH_STEP_Y)) {
        g.x = home.x - away;
        g.y = home.y + WATCH_STEP_Y;
    }
    return g;
}

Pet::Goal Pet::goal_for(Look s) const {
    if (s == Look::Act) return act_goal();
    const Pixel home = stage_.home();
    const Pose pose = resolve(s);
    Goal g;
    switch (s) {
        case Look::Type: case Look::TypeFast: case Look::Sleep: case Look::Busy: case Look::Sit:
        case Look::Eat:
            g.spot = true;
            g.x = home.x;
            g.y = home.y;
            break;
        case Look::Watch: g = watch_goal(home); break;
        case Look::Wake: g.turn = false; break;                 // it wakes where it slept
        default: break;
    }
    g.row = pose.row;
    const float x = g.spot ? g.x : x_, y = g.spot ? g.y : y_;
    if (pose.toward_desk) g.row = stage_.desk_row(x, y);     // its screen, from where it will stand
    if (s == Look::TypeFast && can_throw() && egg_->throw_kit.typing_fast) g.row = row_to_aim(x, y);
    return g;
}

// Facing what it throws at from (x, y); the front for a toss.
int Pet::row_to_aim(float x, float y) const {
    if (egg_->throw_kit.flight == Flight::Toss) return ROW_FRONT;
    const Point aim = aim_point();
    return row_toward(aim.x - x, aim.y - (y - egg_->tall * unit() * EYE_HEIGHT));
}

// A step's place and facing. A look keeps its own place and facing unless the step names a spot; there, and for
// everything else sent somewhere, it plays facing the way it arrived.
Pet::Goal Pet::act_goal() const {
    const bool look = act_.kind == Act::Kind::Look;
    Goal g;
    if (look) {
        g = goal_for(act_.look);
    } else {
        g.row = ROW_FRONT;
    }
    if (act_.spot.kind != Spot::Kind::Here) {
        const Pixel home = stage_.home();
        Point p{float(home.x), float(home.y)};
        if (act_.spot.kind == Spot::Kind::Point || act_.spot.kind == Spot::Kind::Scene) {
            const Point at = *spot_point(act_.spot);
            p = act_.move == Act::Move::Walk ? stage_.nearest_standable(at) : at;   // swimming or jumping: right there
        } else if (act_.spot.kind == Spot::Kind::Target) {
            const std::optional<Pixel> target = stage_.target();
            p = target ? stage_.nearest_standable({float(target->x), float(target->y)}) : Point{x_, y_};
        }
        if (act_.move == Act::Move::Walk && !stage_.reachable(p.x, p.y)) p = {x_, y_};   // nowhere to stand near it
        g.spot = true;
        g.x = p.x;
        g.y = p.y;
        const bool toward_desk = look && resolve(act_.look).toward_desk;
        g.turn = toward_desk || act_.spot.kind == Spot::Kind::Desk;
        if (g.turn) g.row = stage_.desk_row(g.x, g.y);
    }
    const float x = g.spot ? g.x : x_, y = g.spot ? g.y : y_;
    if (act_.kind == Act::Kind::Throw && can_throw()) {
        g.turn = true;
        g.row = row_to_aim(x, y);
    }
    if (act_.row >= 0) {
        g.turn = true;
        g.row = act_.row;
    } else if (const std::optional<Point> at = spot_point(act_.face)) {
        g.turn = true;
        g.row = row_toward(at->x - x, at->y - (y - egg_->tall * unit() * EYE_HEIGHT));
    }
    return g;
}

std::optional<Point> Pet::spot_point(const Spot& s) const {
    switch (s.kind) {
        case Spot::Kind::Point: return Point{s.x, s.y};
        case Spot::Kind::Scene: return stage_.from_scene(s.x, s.y);
        case Spot::Kind::Home: {
            const Pixel h = stage_.home();
            return Point{float(h.x), float(h.y)};
        }
        case Spot::Kind::Desk: return stage_.desk_point();
        case Spot::Kind::Target: {
            const std::optional<Pixel> t = stage_.target();
            if (!t) return std::nullopt;
            return Point{float(t->x), float(t->y)};
        }
        case Spot::Kind::Here: break;
    }
    return std::nullopt;
}

void Pet::act(const Act& a, bool again) {
    Act next = a;
    next.row = a.row < 0 ? -1 : std::clamp(a.row, 0, ROWS - 1);
    next.speed = std::clamp(a.speed, MIN_ACT_SPEED, MAX_ACT_SPEED);
    next.count = std::max(1, a.count);
    if (next == act_ && !again) return;
    act_ = next;
    if (want_ == Look::Act) begin(last_ms_);                // to its place (a look's own, or the step's), then play
}

bool Pet::act_done(uint32_t now) const {
    if (want_ != Look::Act || phase_ != Phase::Acting) return false;
    if (act_.kind == Act::Kind::Laps) return !lapping_;     // its laps are run, or it could not get onto them
    if (act_.kind != Act::Kind::Throw) return false;
    if (!can_throw()) return true;                          // nothing to throw: the step is skipped
    return ms_to_ticks(now - act_ms_, act_.speed) >= int64_t(throw_cycle_ticks()) * act_.count;
}

// Roaming about in its free time: wandering far, or idling with short strolls.
bool Pet::roams() const {
    if (want_ == Look::Wander || want_ == Look::Idle) return true;
    if (want_ != Look::Act) return false;
    if (act_.kind == Act::Kind::Wander) return true;
    return act_.kind == Act::Kind::Look && act_.spot.kind == Spot::Kind::Here &&
           (act_.look == Look::Wander || act_.look == Look::Idle);
}

bool Pet::wanders() const {
    return want_ == Look::Wander ||
           (want_ == Look::Act && (act_.kind == Act::Kind::Wander ||
                                    (act_.kind == Act::Kind::Look && act_.look == Look::Wander)));
}

// The look whose rules apply now: a step's look, or the one it wants.
Look Pet::look_now() const {
    return want_ == Look::Act && act_.kind == Act::Kind::Look ? act_.look : want_;
}

float Pet::act_speed() const { return want_ == Look::Act ? act_.speed : 1.0f; }

void Pet::want(Look s, uint32_t now) {
    if (!egg_) return;
    if (s == want_) {
        next_.reset();
        return;
    }
    if (!next_ && !is_reaction(s)) next_at_ = loop_end(now);
    if (next_at_ && !is_reaction(s) && now < next_at_) {
        next_ = s;
        return;
    }
    next_.reset();
    next_at_ = 0;
    shown_ = look();
    want_ = s;
    begin(now);
}

uint32_t Pet::loop_end(uint32_t now) const {
    if (phase_ != Phase::Acting || want_ == Look::Act || !is_reaction(want_) || lapping_ || throwing()) return 0;
    Pose p = resolve(want_);
    uint32_t from = act_ms_;
    if (p.lap) {                                             // between laps: its play there, from the lap's end
        p = resolve(want_, false);
        if (lap_end_ms_) from = lap_end_ms_;
    }
    const uint32_t run = pose_ms(p), into = now - from;
    if (!run || run > FINISH_MAX_MS || into % run == 0) return 0;
    return from + (into / run + 1) * run;
}

void Pet::begin(uint32_t now) {
    goal_ = goal_for(want_);
    route_.clear();
    lapping_ = false;
    lap_end_ms_ = 0;
    turn_ms_ = begun_ms_ = now;
    const bool moves_itself = want_ == Look::Act && act_.move != Act::Move::Walk;
    jumping_ = jump_out_ = false;
    if (!moves_itself && stage_.in_water(x_, y_) && !stage_.can_stand(x_, y_)) {   // swimming: out onto the bank first
        start_jump(stage_.nearest_standable({x_, y_}), true, now);
        return;
    }
    if (roams()) {                                           // stand a moment, turn to the front, then roam
        walking_ = setting_off_ = turning_off_ = false;
        phase_ = Phase::Free;
        pause_ms_ = FIRST_PAUSE_MS;
        pause_start_ = now;
        return;
    }
    if (goal_.spot && std::hypot(goal_.x - x_, goal_.y - y_) > ARRIVED_PX && moves_itself) {
        if (act_.move == Act::Move::Jump) {
            start_jump({goal_.x, goal_.y}, false, now);
        } else {                                             // straight through the water
            route_ = {{goal_.x, goal_.y}};
            phase_ = Phase::Walking;
            speed_ = SWIM_PACE * act_.speed;
        }
        return;
    }
    if (goal_.spot && want_ == Look::Act && !act_.through.empty()) {   // one smooth curve through its points
        route_ = curve_through();
        if (!route_.empty()) {
            phase_ = Phase::Walking;
            speed_ = act_.speed;
            return;
        }
    }
    if (goal_.spot && std::hypot(goal_.x - x_, goal_.y - y_) > ARRIVED_PX) {
        route_ = stage_.path(x_, y_, goal_.x, goal_.y);
        if (!route_.empty()) {
            phase_ = Phase::Walking;
            speed_ = want_ == Look::Act ? act_.speed : HURRY;
            return;
        }
    }
    walking_ = setting_off_ = turning_off_ = false;
    phase_ = Phase::Turning;
}

// A Catmull-Rom curve from where it stands through act_.through to the goal, a point every CURVE_STEP_PX: it carves
// it at any speed without stopping at a point. Empty (it walks its floor the usual way) if any of it leaves the
// floor or cuts through something in the way.
std::vector<Point> Pet::curve_through() const {
    std::vector<Point> pts{{x_, y_}};
    for (const Spot& s : act_.through)
        if (const std::optional<Point> p = spot_point(s)) pts.push_back(stage_.nearest_standable(*p));
    pts.push_back({goal_.x, goal_.y});
    std::vector<Point> out;
    Point from = pts.front();
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        const Point p0 = pts[i ? i - 1 : 0], p1 = pts[i], p2 = pts[i + 1], p3 = pts[std::min(i + 2, pts.size() - 1)];
        const int n = std::max(2, int(std::hypot(p2.x - p1.x, p2.y - p1.y) / (CURVE_STEP_PX * unit())));
        for (int k = 1; k <= n; k++) {
            const float t = float(k) / float(n), t2 = t * t, t3 = t2 * t;
            auto at = [&](float a, float b, float c, float d) {
                return 0.5f * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (3 * b - a - 3 * c + d) * t3);
            };
            const Point q{at(p0.x, p1.x, p2.x, p3.x), at(p0.y, p1.y, p2.y, p3.y)};
            if (!stage_.roomy(from.x, from.y, q.x, q.y)) return {};
            out.push_back(q);
            from = q;
        }
    }
    return out;
}

float Pet::pace() const { return egg_ && egg_->pace > 0 ? egg_->pace : WALK_PX_S; }

// Laps on a run of its own (Zoomies) go at LAP_PACE; on its walk, slower, with the walk keeping time with the ground.
float Pet::lap_pace() const { return runs_laps() ? LAP_PACE : WALK_LAP_PACE; }

bool Pet::runs_laps() const {
    const Anim* zoomies = egg_ ? egg_->find("Zoomies") : nullptr;
    return zoomies && zoomies->rows >= ROWS;
}

uint32_t Pet::turn_step_ms() const {
    const Anim* walk = egg_ ? egg_->find("Walk") : nullptr;
    if (!walk) return TURN_STEP_MS;
    return std::clamp<uint32_t>(ticks_to_ms(walk->total_ticks()) / 4, TURN_STEP_MS, MAX_TURN_STEP_MS);
}

void Pet::turn_toward(int goal, uint32_t now) {
    if (row_ != goal && now - turn_ms_ >= turn_step_ms()) {
        row_ = turn_step(row_, goal);
        turn_ms_ = now;
    }
}

float Pet::route_left() const { return phase_ == Phase::Walking ? route_length({x_, y_}, route_) : 0; }

namespace {
Point bezier(Point a, Point c, Point b, float t) {
    const float u = 1 - t;
    return {u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y};
}
}  // namespace

// A new way is rounded once before it walks it: each sharp corner becomes a short arc, and a turn back at the start
// (it was heading the other way) becomes a U carried a little forward first. An arc is only used where all of it
// has room on the floor; else that corner stays sharp.
void Pet::round_corners(bool heading_on) {
    const float r_max = LOOK_AHEAD * unit();
    std::vector<Point> out;
    Point a{x_, y_};
    auto fits = [&](const std::vector<Point>& pts, Point from) {
        for (const Point& p : pts) {
            if (!stage_.roomy(from.x, from.y, p.x, p.y)) return false;
            from = p;
        }
        return true;
    };
    if (heading_on && !route_.empty()) {                   // already walking: ease from its heading into the new way
        const Point c = route_.front();
        const float lc = std::hypot(c.x - a.x, c.y - a.y);
        const float hx = last_dx_, hy = last_dy_;
        const float along = lc > 0.01f ? ((c.x - a.x) * hx + (c.y - a.y) * hy) / lc : 1;
        if (lc > 0.01f && along < -0.5f) {                 // turning back: a half circle out to one side
            const float widest = std::min(r_max, lc) / 2;
            bool turned = false;
            for (const float rad : {widest, widest * 0.7f, widest * 0.45f}) {   // a tighter one where that has no room
                for (const float side : {1.0f, -1.0f}) {
                    const float nx = -hy * side, ny = hx * side;
                    std::vector<Point> arc;
                    for (int k = 1; k <= 2 * ARC_POINTS; k++) {
                        const double t = PI * k / (2 * ARC_POINTS);
                        arc.push_back({a.x + nx * rad * float(1 - std::cos(t)) + hx * rad * float(std::sin(t)),
                                       a.y + ny * rad * float(1 - std::cos(t)) + hy * rad * float(std::sin(t))});
                    }
                    if (fits(arc, a) && stage_.line_clear(arc.back().x, arc.back().y, c.x, c.y)) {
                        out.insert(out.end(), arc.begin(), arc.end());
                        turned = true;
                        break;
                    }
                }
                if (turned) break;
            }
        } else if (lc > 0.01f && along < 0.7f) {           // a sharp turn: ease from its heading into the new way
            const float r = std::min(r_max, lc / 2);
            const Point ctrl{a.x + hx * r, a.y + hy * r};
            const Point end{a.x + (c.x - a.x) / lc * r, a.y + (c.y - a.y) / lc * r};
            std::vector<Point> arc;
            for (int k = 1; k <= ARC_POINTS; k++) arc.push_back(bezier(a, ctrl, end, float(k) / ARC_POINTS));
            if (fits(arc, a)) out.insert(out.end(), arc.begin(), arc.end());
        }
    }
    for (size_t i = 0; i < route_.size(); i++) {
        const Point b = route_[i];
        if (i + 1 >= route_.size()) {
            out.push_back(b);
            break;
        }
        const Point from = out.empty() ? a : out.back(), c = route_[i + 1];
        const float lab = std::hypot(b.x - from.x, b.y - from.y), lbc = std::hypot(c.x - b.x, c.y - b.y);
        if (lab < 0.01f || lbc < 0.01f) {
            out.push_back(b);
            continue;
        }
        const float ux = (b.x - from.x) / lab, uy = (b.y - from.y) / lab, vx = (c.x - b.x) / lbc,
                    vy = (c.y - b.y) / lbc;
        if (ux * vx + uy * vy > 0.9f) {                    // nearly straight on: no arc needed
            out.push_back(b);
            continue;
        }
        const float r = std::min({r_max, lab / 2, lbc / 2});
        const Point s{b.x - ux * r, b.y - uy * r}, e{b.x + vx * r, b.y + vy * r};
        std::vector<Point> arc{s};
        for (int k = 1; k < ARC_POINTS; k++) arc.push_back(bezier(s, b, e, float(k) / ARC_POINTS));
        arc.push_back(e);
        if (fits(arc, from) && stage_.line_clear(e.x, e.y, c.x, c.y)) out.insert(out.end(), arc.begin(), arc.end());
        else out.push_back(b);
    }
    route_ = out;
}

// It walks its way at an even pace, carrying what is left of a step round each corner; its facing follows the way
// it heads one facing at a time, so a turn passes through the facings in between. Running laps (`follow`), it faces
// the way it heads at once: round a lap the way turns faster than a walk's turning would keep up.
bool Pet::walk_along(float dt, float speed, bool follow) {
    if (route_.empty()) {
        walking_ = setting_off_ = turning_off_ = false;
        return true;
    }
    const bool fresh = route_.back().x != route_end_.x || route_.back().y != route_end_.y ||
                       route_.size() > route_size_ || (!walking_ && !setting_off_);
    if (fresh) {                                           // a new way: round its corners once
        round_corners(walking_);
        route_end_ = route_.back();
        route_size_ = route_.size();
    }
    if (!walking_) {                                       // standing: it faces its way before it sets off
        const Point n = route_.front();
        if (std::hypot(n.x - x_, n.y - y_) > 0.01f) heading_ = row_toward(n.x - x_, n.y - y_);
        if (!setting_off_) {
            setting_off_ = true;
            walk_turn_ms_ = 0;
            turning_off_ = turns_apart(row_, heading_) > 1;   // more than 45 degrees: round on the spot first
            // A step's own way of going (spinning, or its own animation) sets off at once, whichever way it faces.
            if (want_ == Look::Act && act_.move == Act::Move::Walk && (act_.spin > 0 || !act_.moving.empty()))
                turning_off_ = false;
        }
        if (turning_off_ && row_ != heading_) {            // each facing on the way held a step of its walk
            walk_turn_ms_ += dt;
            if (walk_turn_ms_ >= turn_step_ms()) {
                row_ = turn_step(row_, heading_);
                walk_turn_ms_ = 0;
            }
            return false;
        }
        if (turning_off_ && walk_turn_ms_ < turn_step_ms()) {   // facing its way, a moment before the first step
            walk_turn_ms_ += dt;
            return false;
        }
        row_ = heading_;
        setting_off_ = turning_off_ = false;
        walking_ = true;
        walk_turn_ms_ = 0;
    }
    float step = pace() * speed * dt / 1000;
    stride_ms_ += speed * dt;                              // its legs keep time with the ground it covers
    walk_turn_ms_ += dt;
    while (step > 0 && !route_.empty()) {
        const Point next = route_.front();
        const float vx = next.x - x_, vy = next.y - y_, d = std::hypot(vx, vy);
        if (d > 0.01f) {
            heading_ = row_toward(vx, vy);
            last_dx_ = vx / d;
            last_dy_ = vy / d;
        }
        if (d <= step) {
            x_ = next.x;
            y_ = next.y;
            step -= d;
            route_.erase(route_.begin());
        } else {
            x_ += vx / d * step;
            y_ += vy / d * step;
            step = 0;
        }
    }
    if (follow) {
        row_ = heading_;
    } else if (row_ != heading_ && walk_turn_ms_ >= turn_step_ms()) {
        row_ = turn_step(row_, heading_);
        walk_turn_ms_ = 0;
    }
    if (!route_.empty()) {
        route_end_ = route_.back();
        route_size_ = route_.size();
    } else {
        walking_ = false;
    }
    return route_.empty();
}

// The run: the shortest way onto the lap, round it (either way, chosen at random), and the same way back, so it
// ends where it began. A reaction runs as many laps as fit in what is left of it, none if one does not fit; a mood
// runs one at a time.
void Pet::start_laps(uint32_t now) {
    lapping_ = false;
    route_.clear();
    const std::vector<Point>& ring = stage_.lap();
    if (ring.empty()) return;
    const Point start{x_, y_};
    size_t entry = 0;
    for (size_t i = 1; i < ring.size(); i++)
        if (std::hypot(ring[i].x - x_, ring[i].y - y_) < std::hypot(ring[entry].x - x_, ring[entry].y - y_)) entry = i;
    const Point on = ring[entry];
    std::vector<Point> in, out;
    if (std::hypot(on.x - x_, on.y - y_) > ARRIVED_PX) {
        in = stage_.path(x_, y_, on.x, on.y);
        if (in.empty()) return;                              // it can't get onto the lap from here
        out = stage_.path(on.x, on.y, start.x, start.y);
        if (out.empty()) return;
    }
    const bool backwards = rand01() < 0.5f;
    std::vector<Point> loop;
    const size_t n = ring.size();
    for (size_t k = 1; k <= n; k++) loop.push_back(ring[(backwards ? entry + n - k : entry + k) % n]);
    int laps = 1;
    if (want_ == Look::Act && act_.kind == Act::Kind::Laps) {
        laps = std::min(act_.count, MAX_ASKED_LAPS);
    } else if (is_reaction(look_now())) {
        const float px_ms = pace() * lap_pace() * act_speed() / 1000;
        const float there_and_back = (route_length(start, in) + route_length(on, out)) / px_ms;
        const float one = route_length(on, loop) / px_ms;
        const Point first = in.empty() ? loop.front() : in.front();
        const int apart = turns_apart(row_, row_toward(first.x - x_, first.y - y_));
        const float setting_off = apart > 1 ? float(apart * turn_step_ms()) : 0;   // as walk_along turns round
        const float left = info(look_now()).length_ms / act_speed() - float(now - begun_ms_);
        const int fit = int(std::floor((left - LAP_SPARE_MS - setting_off - there_and_back) / one));
        if (fit < 1) return;                                 // it plays in place
        laps = std::min(fit, MAX_LAPS);
    }
    route_ = in;
    for (int l = 0; l < laps; l++) route_.insert(route_.end(), loop.begin(), loop.end());
    route_.insert(route_.end(), out.begin(), out.end());
    lapping_ = true;
    turn_ms_ = now;
}

void Pet::lap_update(float dt, uint32_t now) {
    if (lapping_) {
        if (walk_along(dt, lap_pace() * act_speed(), true)) {
            lapping_ = false;
            lap_end_ms_ = now;
            turn_ms_ = now;
            lap_rest_ms_ = LAP_REST_MS + rand01() * LAP_REST_EXTRA_MS;
        }
        return;
    }
    if (!lap_end_ms_) return;                                // it never got onto the lap: it plays in place
    turn_toward(resolve(want_, false).row, now);             // then back round to face the way it plays
    if (!is_reaction(look_now()) && want_ != Look::Act && now - lap_end_ms_ >= lap_rest_ms_) start_laps(now);
}

// Wandering picks anywhere standable and walks the real path there (round the desk, through gaps), so every corner
// gets visited; idling strolls a short straight line nearby.
bool Pet::pick_roam_target(bool wander) {
    const Area& b = stage_.roaming_area();
    for (int i = 0; i < ROAM_TRIES && route_.empty(); i++) {
        float x, y;
        if (wander) {
            x = b.x0 + rand01() * (b.x1 - b.x0);
            y = b.y0 + rand01() * (b.y1 - b.y0);
        } else {
            float a = rand01() * float(FULL_TURN), r = STROLL_MIN_PX + rand01() * STROLL_EXTRA_PX;
            x = x_ + r * std::cos(a);
            y = y_ + r * STROLL_FLATTEN * std::sin(a);
        }
        if (!stage_.reachable(x, y) || stage_.hidden(x, y)) continue;
        if (wander) route_ = stage_.path(x_, y_, x, y);
        else if (stage_.line_clear(x_, y_, x, y)) route_.push_back({x, y});
    }
    return !route_.empty();
}

void Pet::free_update(float dt, uint32_t now) {
    if (pause_ms_ > 0) {
        pause_ms_ -= dt;
        turn_toward(ROW_FRONT, now);
        return;
    }
    const bool wander = wanders();
    if (route_.empty() && !pick_roam_target(wander)) {
        pause_ms_ = RETRY_PAUSE_MS;
        pause_start_ = now;
        return;
    }
    if (walk_along(dt, (wander ? WANDER_PACE : IDLE_PACE) * act_speed())) {   // idle strolls, then stays a while
        pause_ms_ = wander ? WANDER_REST_MS + rand01() * WANDER_REST_EXTRA_MS
                           : IDLE_REST_MS + rand01() * IDLE_REST_EXTRA_MS;
        pause_start_ = now;
        turn_ms_ = now;
    }
}

void Pet::update(uint32_t now) {
    const float dt = last_ms_ ? float(std::min<uint32_t>(MAX_FRAME_MS, now - last_ms_)) : 0;
    last_ms_ = now;
    if (!egg_) return;
    if (next_ && now >= next_at_) want(*next_, now);       // the reaction's loop is done
    const float night_goal = part_ == Daypart::Night ? 1.0f : part_ == Daypart::Evening ? EVENING_DARKNESS : 0.0f;
    fade_ = std::min(1.0f, fade_ + dt / SCENE_FADE_MS);
    night_ += std::clamp(night_goal - night_, -dt / SCENE_FADE_MS, dt / SCENE_FADE_MS);
    if (phase_ == Phase::Acting && look_now() == Look::Type) {
        if (keys_) typed_ += dt / 1000 * std::clamp(kps_ < 0 ? TYPED_UNKNOWN_KPS : kps_, TYPED_MIN, TYPED_MAX);
    } else {
        typed_ = 0;
    }
    switch (phase_) {
        case Phase::Free: free_update(dt, now); break;
        case Phase::Walking:
            if (walk_along(dt, speed_)) {
                phase_ = Phase::Turning;
                turn_ms_ = now;
                if (jumping_) land(now);
            }
            break;
        case Phase::Turning:
            if (!goal_.turn || row_ == goal_.row) {
                phase_ = Phase::Acting;
                act_ms_ = now;
                shown_ = want_;
                if (resolve(want_).lap) start_laps(now);
            } else {
                turn_toward(goal_.row, now);
            }
            break;
        case Phase::Acting:
            if (resolve(want_).lap) lap_update(dt, now);
            break;
    }
    if (phase_ == Phase::Acting && !goal_.turn) row_ = resolve(want_).row;   // it plays facing the way it stands
}

}  // namespace nestbox
