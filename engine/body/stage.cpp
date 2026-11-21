#include "body/stage.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include "body/parts.h"

namespace nestbox {

namespace {
constexpr double PLAIN_HOME_Y = 0.7;         // the plain room's home, as a fraction of the screen's height
constexpr double PLAIN_FLOOR_TOP = 0.36;     // the plain room's floor starts this far down the screen
constexpr int PLAIN_EDGE_MARGIN = 36;        // the plain room: screen pixels kept clear at the sides
constexpr int SPIRAL_STEP = 3;               // nearest-spot search: ring spacing, screen pixels
constexpr int SPIRAL_DIRECTIONS = 16;
constexpr int HOME_SEARCH_PX = 200;          // how far it looks for a home it can stand on
constexpr int STANDABLE_SEARCH_PX = 300;     // how far it looks for a spot it can reach
constexpr int TIGHTEN_ROUNDS = 4, TIGHTEN_STEPS = 8;   // pulling a grid path tight into straight legs
constexpr float BEND_STEP = 1;               // the search round a bend that doubles back, screen pixels
constexpr int SCENE_FOOT_ROWS = 11;          // the bottom rows of a work spot that count as its foot, screen pixels
constexpr int LAP_POINTS = 24;               // a lap's corners
constexpr float LAP_FLATTEN = 0.55f;         // the floor is seen from above at an angle: a round lap looks this squat
constexpr int LAP_MIN_HALF = 14;             // the smallest lap's half width, art pixels
constexpr int LAP_RADIUS_STEP = 4;           // lap sizes tried, screen pixels apart
constexpr int LAP_CENTRE_STEP = 8;           // lap centres tried, screen pixels apart

// The offset of a `screen`-sized window into a picture `size` wide: centred on `focus` and kept inside a bigger
// picture, centred on a smaller one.
int fit(int size, int screen, int focus) {
    return size > screen ? std::clamp(focus - screen / 2, 0, size - screen) : (size - screen) / 2;
}

// The same for an area to keep in view, from `from` to `to`, inside the part of the window that is fully visible
// (`seen` long, starting `seen_from` in: on a round screen, its inner square): the window stays centred on the picture
// and moves only as far as the area needs; an area longer than that part is centred in the window.
int fit_keeping(int size, int screen, int from, int to, int seen_from, int seen) {
    if (size <= screen || to - from > seen) return fit(size, screen, (from + to) / 2);
    int x = (size - screen) / 2;
    if (from < x + seen_from) x = from - seen_from;
    if (to > x + seen_from + seen) x = to - seen_from - seen;
    return std::clamp(x, 0, size - screen);
}

// The largest 4-connected patch of `lit` cells in a w x h grid, as a bounding box; empty if there is none.
template <typename Lit>
Box largest_patch(int w, int h, Lit lit) {
    struct Corners {
        int x0, y0, x1, y1;
    };
    std::vector<int> seen(size_t(w) * h, 0), stack;
    Corners best{w, h, -1, -1};
    int best_n = 0;
    for (int start = 0; start < w * h; start++) {
        if (seen[start] || !lit(start)) continue;
        Corners r{w, h, -1, -1};
        int n = 0;
        stack.assign(1, start);
        seen[start] = 1;
        while (!stack.empty()) {
            int i = stack.back();
            stack.pop_back();
            n++;
            int x = i % w, y = i / w;
            r = {std::min(r.x0, x), std::min(r.y0, y), std::max(r.x1, x), std::max(r.y1, y)};
            const int neighbours[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& d : neighbours) {
                int nx = x + d[0], ny = y + d[1];
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                int j = ny * w + nx;
                if (!seen[j] && lit(j)) { seen[j] = 1; stack.push_back(j); }
            }
        }
        if (n > best_n) { best_n = n; best = r; }
    }
    return best_n ? Box::spanning(best.x0, best.y0, best.x1, best.y1) : Box{};
}

// Calls `at(dx, dy)` for points on circles widening round a spot, SPIRAL_STEP apart out to `max_r`, SPIRAL_DIRECTIONS
// on each, until it returns true.
template <class F>
void spiral(int max_r, F&& at) {
    for (int r = SPIRAL_STEP; r < max_r; r += SPIRAL_STEP)
        for (int a = 0; a < SPIRAL_DIRECTIONS; a++) {
            const double t = FULL_TURN * a / SPIRAL_DIRECTIONS;
            if (at(r * std::cos(t), r * std::sin(t))) return;
        }
}
}  // namespace

void Stage::set_display(const Display& d) {
    display_ = &d;
    width_ = d.w;
    height_ = d.h;
    unit_ = d.scale;
    if (egg_) set_egg(egg_);
    else measure_roaming();
}

void Stage::set_egg(const Egg* egg) {
    egg_ = egg;
    lap_.clear();
    lap_measured_ = false;
    reach_.clear();
    reach_measured_ = false;
    unit_ = egg_ && egg_->scale ? egg_->scale : display_->scale;
    fit_scene();
    measure_pet();
    measure_desk();
    measure_roaming();
}

// Scenes are drawn for one size. On another screen the picture is cropped: centred, moved only as far as the area
// its author wants in view needs (else around the home), or centred with its edges carried on when it is smaller;
// everything placed on it moves with it.
void Stage::fit_scene() {
    has_scene_ = egg_ && egg_->has_scene && egg_->scene.day;
    if (!has_scene_) {
        scene_ = Scene();
        scene_x_ = scene_y_ = 0;
        floor_rows_ = EdgeRows();
        blocked_rows_.clear();
        return;
    }
    scene_ = egg_->scene;
    const int s = scene_scale_ = scene_.scale;
    const bool keep = scene_.keep[0] >= 0 && scene_.keep[2] > scene_.keep[0];
    if (keep) {
        const Box seen = display().safe();
        scene_x_ = fit_keeping(scene_.w * s, width_, scene_.keep[0] * s, scene_.keep[2] * s, seen.x, seen.w);
        scene_y_ = fit_keeping(scene_.h * s, height_, scene_.keep[1] * s, scene_.keep[3] * s, seen.y, seen.h);
    } else {
        scene_x_ = fit(scene_.w * s, width_, (scene_.home_x >= 0 ? scene_.home_x : scene_.w / 2) * s);
        scene_y_ = fit(scene_.h * s, height_, (scene_.home_y >= 0 ? scene_.home_y : scene_.h / 2) * s);
    }
    auto to_screen = [&](int& x, int& y) { x = x * s - scene_x_; y = y * s - scene_y_; };
    for (auto& pt : scene_.floor) to_screen(pt.first, pt.second);
    for (auto& pt : scene_.water) to_screen(pt.first, pt.second);
    for (auto& hole : scene_.blocked)
        for (auto& pt : hole) to_screen(pt.first, pt.second);
    if (scene_.home_x >= 0) to_screen(scene_.home_x, scene_.home_y);
    if (scene_.target_x >= 0) to_screen(scene_.target_x, scene_.target_y);
    if (scene_.watch_x >= 0) to_screen(scene_.watch_x, scene_.watch_y);
    to_screen(scene_.swish_x, scene_.swish_y);
    to_screen(scene_.work_x, scene_.work_y);
    floor_rows_.build(scene_.floor);
    blocked_rows_.assign(scene_.blocked.size(), EdgeRows());
    for (size_t i = 0; i < scene_.blocked.size(); i++) blocked_rows_[i].build(scene_.blocked[i]);
}

void EdgeRows::build(const std::vector<std::pair<int, int>>& poly) {
    rows.clear();
    top = 0;
    if (poly.size() < 3) return;
    int lo = poly[0].second, hi = poly[0].second;
    for (const auto& p : poly) {
        lo = std::min(lo, p.second);
        hi = std::max(hi, p.second);
    }
    top = lo;
    rows.assign(size_t(hi - lo), {});
    // With whole-pixel corners, an edge crosses the ray of a point at y only when min <= y < max, so in the rows
    // floor(y) from min to max - 1; level edges cross none.
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const int a = std::min(poly[i].second, poly[j].second), b = std::max(poly[i].second, poly[j].second);
        for (int r = a; r < b; r++) rows[size_t(r - top)].push_back(uint16_t(i));
    }
}

bool EdgeRows::inside(const std::vector<std::pair<int, int>>& poly, float x, float y) const {
    const float r = std::floor(y) - float(top);
    if (!(r >= 0 && r < float(rows.size()))) return false;   // no edge spans this row: outside (and NaN too)
    bool in = false;
    for (const uint16_t i : rows[size_t(r)])
        if (Scene::crosses(poly, i, i ? i - 1 : poly.size() - 1, x, y)) in = !in;
    return in;
}

bool Stage::walkable(float x, float y) const {
    if (scene_.floor.size() >= 3 && !floor_rows_.inside(scene_.floor, x, y)) return false;
    for (size_t i = 0; i < scene_.blocked.size(); i++)
        if (scene_.blocked[i].size() >= 3 && blocked_rows_[i].inside(scene_.blocked[i], x, y)) return false;
    return true;
}

void Stage::measure_pet() {
    half_width_ = 0;
    const Anim* a = egg_ ? egg_->find("Idle") : nullptr;
    if (!a) return;
    FrameCache cache;
    for (int r = 0; r < a->rows; r++)
        for (int f = 0; f < a->frames; f++) {
            const FrameBox& b = pixels_of(a->box(r, f), cache);
            for (int i = 0; i < b.w * b.h; i++)
                if (b.px[i]) half_width_ = std::max(half_width_, std::abs(2 * (b.x + i % b.w) + 1 - a->fw) * unit_ / 2);
        }
}

void Stage::measure_desk() {
    cover_ = Cover();
    screen_ = Box();
    if (has_scene_ && scene_.work_on) measure_scene_desk();
    else measure_plain_desk();
}

void Stage::measure_scene_desk() {
    const Scene& sc = scene_;
    const int s = scene_scale_;
    int top = -1, bottom = -1, left = sc.work_w, right = -1;     // the opaque pixels' rows and columns
    for (int y = 0; y < sc.work_h; y++)
        for (int x = 0; x < sc.work_w; x++)
            if (sc.work_on[y * sc.work_w + x]) {
                if (top < 0) top = y;
                bottom = y;
                left = std::min(left, x);
                right = std::max(right, x);
            }
    if (bottom < 0) {
        desk_ = Box();
        desk_base_ = 0;
        return;
    }
    cover_ = {sc.work_y + top * s, sc.work_x + left * s, sc.work_x + (right + 1) * s};
    if (sc.work_off) {   // the screen: the biggest patch where "on" and "off" differ, less a one-pixel margin
        const Box lit = largest_patch(sc.work_w, sc.work_h, [&](int i) { return sc.work_on[i] != sc.work_off[i]; });
        if (lit.w > 3 && lit.h > 3)
            screen_ = Box::spanning(sc.work_x + (lit.x + 1) * s, sc.work_y + (lit.y + 1) * s,
                                    sc.work_x + lit.last_x() * s, sc.work_y + lit.last_y() * s);
    }
    desk_base_ = sc.work_y + (bottom + 1) * s;
    int lo = sc.work_w, hi = -1;
    for (int y = std::max(0, bottom - SCENE_FOOT_ROWS / s); y <= bottom; y++)
        for (int x = 0; x < sc.work_w; x++)
            if (sc.work_on[y * sc.work_w + x]) { lo = std::min(lo, x); hi = std::max(hi, x); }
    desk_ = Box::spanning(sc.work_x + lo * s - 2, desk_base_ - 12, sc.work_x + (hi + 1) * s + 1, desk_base_ + 4);
}

void Stage::measure_plain_desk() {
    const Pixel h = plain_home();
    desk_base_ = h.y + DESK_LEGS_END * unit_;
    desk_ = Box::spanning(h.x - PLAIN_DESK_HALF * unit_ - 6, desk_base_ - 14, h.x + PLAIN_DESK_HALF * unit_ + 6,
                          desk_base_ + 6);
}

void Stage::measure_roaming() {
    roaming_ = {8, height_ * float(PLAIN_FLOOR_TOP), float(width_ - 8), float(height_ - 6)};
    if (!has_scene_ || scene_.floor.empty()) return;
    Area b{float(width_), float(height_), 0, 0};
    for (auto& p : scene_.floor) {
        b.x0 = std::min<float>(b.x0, p.first);
        b.x1 = std::max<float>(b.x1, p.first);
        b.y0 = std::min<float>(b.y0, p.second);
        b.y1 = std::max<float>(b.y1, p.second);
    }
    roaming_ = {std::max(b.x0, 0.0f), std::max(b.y0, 0.0f), std::min<float>(b.x1, width_),
                std::min<float>(b.y1, height_)};
}

const std::vector<Point>& Stage::lap() const {
    if (lap_measured_) return lap_;
    lap_measured_ = true;
    if (!egg_) return lap_;
    const Pixel h = home();
    // The corners' directions, worked out once: a small CPU has no double-precision hardware, and the search below
    // tries hundreds of rings.
    static const struct Turns {
        float cos[LAP_POINTS], sin[LAP_POINTS];
        Turns() {
            for (int i = 0; i < LAP_POINTS; i++) {
                const double a = FULL_TURN * i / LAP_POINTS;
                cos[i] = float(std::cos(a));
                sin[i] = float(std::sin(a));
            }
        }
    } turns;
    std::vector<Point> ring, best;
    auto fits = [&](float cx, float cy, float rx) {
        ring.clear();
        for (int i = 0; i < LAP_POINTS; i++) {
            const Point p{cx + rx * turns.cos[i], cy + rx * LAP_FLATTEN * turns.sin[i]};
            if (!reachable(p.x, p.y) || hidden(p.x, p.y)) return false;
            ring.push_back(p);
        }
        for (int i = 0; i < LAP_POINTS; i++) {
            const Point& p = ring[i];
            const Point& q = ring[(i + 1) % LAP_POINTS];
            if (!line_clear(p.x, p.y, q.x, q.y)) return false;
        }
        return true;
    };
    const Area& a = roaming_;
    for (float rx = std::floor((a.x1 - a.x0) / 2); rx >= LAP_MIN_HALF * unit_; rx -= LAP_RADIUS_STEP) {
        float nearest = -1;
        for (float cy = a.y0; cy <= a.y1; cy += LAP_CENTRE_STEP)
            for (float cx = a.x0 + rx; cx <= a.x1 - rx; cx += LAP_CENTRE_STEP) {
                const float d = std::hypot(cx - h.x, cy - h.y);
                if ((nearest >= 0 && d >= nearest) || !fits(cx, cy, rx)) continue;
                nearest = d;
                best = ring;
            }
        if (nearest >= 0) {
            lap_ = best;
            break;
        }
    }
    return lap_;
}

Pixel Stage::plain_home() const {
    if (egg_ && has_scene_ && scene_.home_x >= 0) return {scene_.home_x, scene_.home_y};
    return {width_ / 2, int(height_ * PLAIN_HOME_Y)};
}

Pixel Stage::home() const {
    const Pixel h = plain_home();
    if (!egg_ || can_stand(h.x, h.y)) return h;
    // the scene's home is off the ground on this screen (a round screen's edge): the nearest spot it may stand on
    // that the work spot doesn't hide, else the nearest it may stand on at all
    std::optional<Pixel> any, open;
    spiral(HOME_SEARCH_PX, [&](double dx, double dy) {
        const int x = h.x + int(dx), y = h.y + int(dy);
        if (!can_stand(x, y)) return false;
        if (!hidden(x, y)) open = Pixel{x, y};
        else if (!any) any = Pixel{x, y};
        return open.has_value();
    });
    return open ? *open : any.value_or(h);
}

Point Stage::nearest_standable(Point p) const {
    if (!egg_ || reachable(p.x, p.y)) return p;
    Point found = p;
    spiral(STANDABLE_SEARCH_PX, [&](double dx, double dy) {
        const Point q{p.x + float(dx), p.y + float(dy)};
        if (!reachable(q.x, q.y)) return false;
        found = q;
        return true;
    });
    return found;
}

// Flood the path grid from the home cell with the same rule path() walks by: a cell is open if the pet can stand at
// its centre, and it never cuts a corner between two closed cells.
bool Stage::reachable(float x, float y) const {
    if (!can_stand(x, y)) return false;
    const int gw = width_ / CELL, gh = height_ / CELL;
    if (!reach_measured_) {
        reach_measured_ = true;
        reach_.assign(size_t(gw * gh), 0);
        const Pixel h = home();
        const int hx = std::clamp(h.x / CELL, 0, gw - 1), hy = std::clamp(h.y / CELL, 0, gh - 1);
        auto open = [&](int cx, int cy) {
            if (cx < 0 || cy < 0 || cx >= gw || cy >= gh) return false;
            return (cx == hx && cy == hy) || can_stand(cx * CELL + CELL / 2.0f, cy * CELL + CELL / 2.0f);
        };
        std::deque<int> queue{hy * gw + hx};
        reach_[size_t(hy * gw + hx)] = 1;
        while (!queue.empty()) {
            const int c = queue.front();
            queue.pop_front();
            const int cx = c % gw, cy = c / gw;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    const int nx = cx + dx, ny = cy + dy;
                    if (!open(nx, ny) || (dx && dy && (!open(cx + dx, cy) || !open(cx, cy + dy)))) continue;
                    char& seen = reach_[size_t(ny * gw + nx)];
                    if (seen) continue;
                    seen = 1;
                    queue.push_back(ny * gw + nx);
                }
        }
    }
    // A point at the floor's very edge may sit in a cell whose centre is not standable: a joined cell next to it
    // will do, as path() lets a walk start or end in such a cell.
    const int cx = std::clamp(int(x) / CELL, 0, gw - 1), cy = std::clamp(int(y) / CELL, 0, gh - 1);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            const int nx = cx + dx, ny = cy + dy;
            if (nx >= 0 && ny >= 0 && nx < gw && ny < gh && reach_[size_t(ny * gw + nx)]) return true;
        }
    return false;
}

int Stage::plain_floor_y() const { return int(std::lround(height_ * PLAIN_FLOOR_TOP / unit_)) * unit_; }

int Stage::plain_desk_top() const {
    const int tall = egg_ ? egg_->tall : 0;
    return DESK_LEGS_END - std::max(8, int(tall * 0.55f)) - 5;
}

float Stage::water_deep(float x, float y) const {
    if (!has_scene_ || scene_.water_shallow <= 0 || scene_.floor.size() < 3) return 1;
    float near = 1e9f;                    // the distance to its floor's edge
    const auto& f = scene_.floor;
    for (size_t i = 0; i < f.size(); i++) {
        const float ax = float(f[i].first), ay = float(f[i].second);
        const float bx = float(f[(i + 1) % f.size()].first), by = float(f[(i + 1) % f.size()].second);
        const float dx = bx - ax, dy = by - ay, len = dx * dx + dy * dy;
        const float t = len > 0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / len, 0.0f, 1.0f) : 0;
        near = std::min(near, std::hypot(x - ax - t * dx, y - ay - t * dy));
    }
    return std::clamp(near / float(scene_.water_shallow * scene_scale()), 0.0f, 1.0f);
}

bool Stage::can_stand(float x, float y) const {
    if (!egg_) return false;
    if (has_scene_ && !scene_.floor.empty()) {
        if (!walkable(x, y)) return false;
    } else {
        const int floor_y = plain_floor_y();
        const bool off_floor = y < floor_y + 6 * unit_ || y > height_ - 4 * unit_;
        if (off_floor || x < PLAIN_EDGE_MARGIN || x > width_ - PLAIN_EDGE_MARGIN) return false;
        if (has_scene_ && !walkable(x, y)) return false;                // its blocked parts, if any
    }
    const Display& d = *display_;
    if (!d.visible(int(x), int(y), d.round() ? 8 * unit_ : 4)) return false;
    if (!d.visible(int(x), int(y) - egg_->tall * unit_, d.round() ? 2 : 0)) return false;   // its head too
    // Most of the widest frame stays on screen; a tail or wing tip may be cut off.
    const int side = half_width_ * 3 / 4;
    if (side > unit_ && (!d.visible(int(x) - side, int(y) - unit_) || !d.visible(int(x) + side, int(y) - unit_)))
        return false;
    return !desk_.contains(x, y);
}

// Its edges or a little of its feet behind the work spot only read as depth.
bool Stage::hidden(float x, float y) const {
    return cover_.top >= 0 && y < desk_base_ && y - cover_.top > egg_->tall * unit_ / 4 &&
           x + half_width_ / 2 > cover_.x0 && x - half_width_ / 2 < cover_.x1;
}

bool Stage::line_clear(float x0, float y0, float x1, float y1) const {
    float d = std::hypot(x1 - x0, y1 - y0);
    int n = std::max(1, int(d));
    for (int k = 1; k <= n; k++)
        if (!can_stand(x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n)) return false;
    return true;
}

bool Stage::roomy(float x0, float y0, float x1, float y1, float margin) const {
    const float d = std::hypot(x1 - x0, y1 - y0);
    if (d < 0.01f) return can_stand(x0, y0);
    const float nx = -(y1 - y0) / d * margin, ny = (x1 - x0) / d * margin;
    return line_clear(x0, y0, x1, y1) && line_clear(x0 + nx, y0 + ny, x1 + nx, y1 + ny) &&
           line_clear(x0 - nx, y0 - ny, x1 - nx, y1 - ny);
}

// Breadth-first search on a grid of CELL-pixel cells, then pulled tight into straight legs.
std::vector<Point> Stage::path(float x0, float y0, float x1, float y1) const {
    if (line_clear(x0, y0, x1, y1)) return {{x1, y1}};
    const int gw = width_ / CELL, gh = height_ / CELL;
    auto cell_x = [&](float v) { return std::clamp(int(v) / CELL, 0, gw - 1); };
    auto cell_y = [&](float v) { return std::clamp(int(v) / CELL, 0, gh - 1); };
    const int start = cell_y(y0) * gw + cell_x(x0), goal = cell_y(y1) * gw + cell_x(x1);
    auto centre = [&](int c) { return Point{c % gw * CELL + CELL / 2.0f, c / gw * CELL + CELL / 2.0f}; };
    // A cell is open if the pet can stand at its centre. The start and goal cells are open too: the points
    // themselves may be standable where their cell's centre is not, at the very edge of the floor.
    auto open = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= gw || y >= gh) return false;
        const int c = y * gw + x;
        return c == goal || c == start || can_stand(centre(c).x, centre(c).y);
    };
    std::vector<int> from(gw * gh, -1);
    std::deque<int> queue{start};
    from[start] = start;
    while (!queue.empty() && from[goal] < 0) {
        const int c = queue.front();
        queue.pop_front();
        const int x = c % gw, y = c / gw;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (!dx && !dy) continue;
                const int nx = x + dx, ny = y + dy;
                if (!open(nx, ny) || (dx && dy && (!open(x + dx, y) || !open(x, y + dy)))) continue;
                const int n = ny * gw + nx;
                if (from[n] >= 0) continue;
                from[n] = c;
                queue.push_back(n);
            }
    }
    if (from[goal] < 0) return {};
    std::vector<Point> cells;
    for (int c = goal; c != start; c = from[c]) cells.push_back(centre(c));
    std::reverse(cells.begin(), cells.end());
    if (cells.empty()) return {{x1, y1}};
    cells.back() = {x1, y1};
    std::vector<Point> legs;
    Point at{x0, y0};
    for (size_t i = 0; i < cells.size();) {
        size_t j = cells.size() - 1;
        while (j > i && !line_clear(at.x, at.y, cells[j].x, cells[j].y)) j--;
        if (j == i && !line_clear(at.x, at.y, cells[i].x, cells[i].y)) {
            // a start at the floor's very edge: no straight leg to the next cell stays on the ground, so step
            // round the corner, along one axis and then the other
            for (const Point corner : {Point{cells[i].x, at.y}, Point{at.x, cells[i].y}})
                if (line_clear(at.x, at.y, corner.x, corner.y) &&
                    line_clear(corner.x, corner.y, cells[i].x, cells[i].y)) {
                    legs.push_back(corner);
                    break;
                }
        }
        legs.push_back(cells[j]);
        at = cells[j];
        i = j + 1;
    }
    tighten({x0, y0}, legs);
    return legs;
}

// The grid's steps can leave a bend that swings out and back (a V round a corner). Each bend is slid toward the
// straight line between its neighbours as far as the floor allows, and dropped when they see each other, so a
// path never doubles back on itself.
void Stage::tighten(Point start, std::vector<Point>& legs) const {
    for (int round = 0; round < TIGHTEN_ROUNDS; round++) {
        bool changed = false;
        for (size_t i = 0; i + 1 < legs.size(); i++) {
            const Point a = i ? legs[i - 1] : start, b = legs[i], c = legs[i + 1];
            if (roomy(a.x, a.y, c.x, c.y)) {
                legs.erase(legs.begin() + long(i));
                changed = true;
                i = i ? i - 1 : 0;
                if (legs.size() < 2) break;
                continue;
            }
            if ((b.x - a.x) * (c.x - b.x) + (b.y - a.y) * (c.y - b.y) < 0) {
                // it doubles back here: look round the bend for the shortest pair of clear legs
                const float current = std::hypot(b.x - a.x, b.y - a.y) + std::hypot(c.x - b.x, c.y - b.y);
                float best = current - 0.5f;
                Point pick = b;
                const float x0 = std::min({a.x, b.x, c.x}) - CELL, x1 = std::max({a.x, b.x, c.x}) + CELL;
                const float y0 = std::min({a.y, b.y, c.y}) - CELL, y1 = std::max({a.y, b.y, c.y}) + CELL;
                for (float y = y0; y <= y1; y += BEND_STEP)
                    for (float x = x0; x <= x1; x += BEND_STEP) {
                        const float l = std::hypot(x - a.x, y - a.y) + std::hypot(c.x - x, c.y - y);
                        if (l < best && can_stand(x, y) && roomy(a.x, a.y, x, y) && roomy(x, y, c.x, c.y)) {
                            best = l;
                            pick = {x, y};
                        }
                    }
                if (pick.x != b.x || pick.y != b.y) {
                    legs[i] = pick;
                    changed = true;
                }
                continue;
            }
            const float ux = c.x - a.x, uy = c.y - a.y, len2 = ux * ux + uy * uy;
            if (len2 < 0.01f) continue;
            const float k = std::clamp(((b.x - a.x) * ux + (b.y - a.y) * uy) / len2, 0.0f, 1.0f);
            const Point m{a.x + ux * k, a.y + uy * k};
            float lo = 0, hi = 1;                    // how far toward the line it can go, both legs still clear
            for (int s = 0; s < TIGHTEN_STEPS; s++) {
                const float t = (lo + hi) / 2;
                const Point p{b.x + (m.x - b.x) * t, b.y + (m.y - b.y) * t};
                if (can_stand(p.x, p.y) && roomy(a.x, a.y, p.x, p.y) && roomy(p.x, p.y, c.x, c.y)) lo = t;
                else hi = t;
            }
            if (lo > 0.01f && std::hypot((m.x - b.x) * lo, (m.y - b.y) * lo) > 0.5f) {
                legs[i] = {b.x + (m.x - b.x) * lo, b.y + (m.y - b.y) * lo};
                changed = true;
            }
        }
        if (!changed) break;
    }
}

Point Stage::desk_point() const {
    if (has_scene_ && scene_.work_on) {   // the upper part of the scene's work spot
        const Scene& sc = scene_;
        return {sc.work_x + sc.work_w * scene_scale_ / 2.0f, sc.work_y + sc.work_h * scene_scale_ * 0.35f};
    }
    const Pixel h = plain_home();      // the middle of the plain desk's monitor
    return {float(h.x), h.y + (plain_desk_top() - 6.5f) * unit_};
}

int Stage::desk_row(float x, float y) const {
    if (!egg_) return 0;
    const Point d = desk_point();
    return row_toward(d.x - x, d.y - (y - egg_->tall * unit_ * EYE_HEIGHT));
}

std::optional<Pixel> Stage::watch_spot() const {
    if (!has_scene_ || egg_->scene.watch_x < 0) return std::nullopt;
    return Pixel{scene_.watch_x, scene_.watch_y};
}

std::optional<Pixel> Stage::target() const {
    if (!has_scene_ || egg_->scene.target_x < 0) return std::nullopt;   // the egg's own: -1 means none
    const Pixel p{scene_.target_x, scene_.target_y};
    if (!display().visible(p.x, p.y)) return std::nullopt;   // cropped off this screen, or outside a round one
    return p;
}

Point Stage::from_scene(float px, float py) const {
    if (!has_scene_) return {px / 100 * width_, py / 100 * height_};
    const float s = float(scene_scale_);
    return {px / 100 * scene_.w * s - scene_x_, py / 100 * scene_.h * s - scene_y_};
}

Point Stage::to_scene(Point p) const {
    if (!has_scene_) return {p.x * 100 / width_, p.y * 100 / height_};
    const float s = float(scene_scale_);
    return {(p.x + scene_x_) * 100 / (scene_.w * s), (p.y + scene_y_) * 100 / (scene_.h * s)};
}

Area Stage::view() const {
    const Point a = to_scene({0, 0}), b = to_scene({float(width_), float(height_)});
    auto clip = [](float v) { return std::clamp(v, 0.0f, 100.0f); };
    return {clip(a.x), clip(a.y), clip(b.x), clip(b.y)};
}

}  // namespace nestbox
