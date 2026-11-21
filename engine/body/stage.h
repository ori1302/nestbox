// Where the pet lives on one screen: its egg's scene fitted to the display (or the plain room), the work spot
// and its screen, the ground it may stand on, and paths across it. All coordinates are screen pixels.
#pragma once
#include <optional>
#include <vector>
#include "egg/egg.h"
#include "screen/display.h"

namespace nestbox {

struct Point {
    float x = 0, y = 0;
};

struct Pixel {
    int x = 0, y = 0;
};

// A continuous area of the screen, from (x0, y0) to (x1, y1).
struct Area {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// A polygon's edges by the row they cross, so a point is tested against those few instead of every edge: the same
// test as Scene::inside (an edge can only cross the ray of a point in a row it spans), so the same answer.
struct EdgeRows {
    int top = 0;                                   // the first row any edge spans
    std::vector<std::vector<uint16_t>> rows;       // per row from `top`: the edges (by their end index) spanning it
    void build(const std::vector<std::pair<int, int>>& poly);
    bool inside(const std::vector<std::pair<int, int>>& poly, float x, float y) const;
};

class Stage {
public:
    Stage() { set_display(default_display()); }

    // Set the display before the egg; the egg's scene is fitted to it.
    void set_display(const Display& d);
    void set_egg(const Egg* egg);

    const Display& display() const { return *display_; }
    int width() const { return width_; }
    int height() const { return height_; }
    // Screen pixels per art pixel: the egg's own, else the display's.
    int unit() const { return unit_; }

    // The egg's scene with every position moved to screen pixels; empty without one.
    bool has_scene() const { return has_scene_; }
    const Scene& scene() const { return scene_; }
    // Screen pixels per scene picture pixel, and the picture pixel at the screen's top-left corner.
    int scene_scale() const { return scene_scale_; }
    int scene_x() const { return scene_x_; }
    int scene_y() const { return scene_y_; }

    // Points in percent of the pet's scene (of the screen without a scene) and back: [0, 0] is the scene's top left,
    // [100, 100] its bottom right. A point means the same spot in the pet's world on every screen.
    Point from_scene(float px, float py) const;
    Point to_scene(Point screen) const;
    // The part of the scene this screen shows, in percent: left, top, right, bottom.
    Area view() const;

    // Where the pet stands to work and sleep: the scene's home (or the plain room's), moved onto standable ground.
    Pixel home() const;
    // The home as the scene or the plain room gives it, before that move.
    Pixel plain_home() const;
    // The scene's own spot to watch its screen from, if it names one.
    std::optional<Pixel> watch_spot() const;
    bool can_stand(float x, float y) const;
    // In its scene's water, where a pastime may swim.
    bool in_water(float x, float y) const { return has_scene_ && scene_.in_water(x, y); }
    // In water too deep to stand in: it swims there.
    bool deep_water(float x, float y) const { return in_water(x, y) && !can_stand(x, y); }
    // How deep the water is at (x, y), 0 to 1 of its swimming depth: shallow near its floor when the scene has
    // shallows.
    float water_deep(float x, float y) const;
    // Standable and joined to its home over the floor on this screen: a round screen or a blocked part can cut
    // the floor into pieces, and the pet only picks places it can walk to. Worked out the first time it is asked.
    bool reachable(float x, float y) const;
    // The reachable spot nearest to `p`, or `p` itself if none is found.
    Point nearest_standable(Point p) const;
    // True if the work spot would hide the pet's middle with its feet at (x, y); it may walk there, not stop.
    bool hidden(float x, float y) const;
    // A walkable path from (x0, y0) to (x1, y1), excluding the start; empty if there is none.
    std::vector<Point> path(float x0, float y0, float x1, float y1) const;
    bool line_clear(float x0, float y0, float x1, float y1) const;
    // Clear with room to spare: the line and two lines `margin` pixels either side of it, so a walk along it never
    // grazes the edge of the floor or a blocked part between the points it was checked at.
    bool roomy(float x0, float y0, float x1, float y1, float margin = ROOM_PX) const;
    static constexpr float ROOM_PX = 2;
    // The area free roaming picks its destinations from.
    const Area& roaming_area() const { return roaming_; }
    // A closed loop round the open floor for laps, its points in order (the last joins the first): the widest
    // flattened ring that fits, nearest the home among those. Empty if the floor has no room for one. Worked out
    // the first time it is asked for.
    const std::vector<Point>& lap() const;

    // The point the pet looks at when it faces its screen, and the row facing it with the feet at (x, y).
    Point desk_point() const;
    int desk_row(float x, float y) const;
    // The work spot's screen, where its "on" picture differs from "off"; empty without a scene work spot.
    const Box& work_screen() const { return screen_; }
    // The row of the work spot's foot: a pet whose feet are above it stands behind the work spot.
    int desk_base() const { return desk_base_; }
    // The scene's target (a hoop's rim, a spot in a pond), if it has one and it is on this screen.
    std::optional<Pixel> target() const;

    // The plain room: its floor's top row (screen pixels) and its desk's tabletop (art pixels from the feet).
    int plain_floor_y() const;
    int plain_desk_top() const;

private:
    struct Cover {                       // the scene work spot's whole picture
        int top = -1, x0 = 0, x1 = -1;   // top < 0: none
    };
    void fit_scene();
    void measure_pet();
    void measure_desk();
    void measure_scene_desk();
    void measure_plain_desk();
    void measure_roaming();
    void tighten(Point start, std::vector<Point>& legs) const;

    const Display* display_ = nullptr;
    const Egg* egg_ = nullptr;
    int width_ = 0, height_ = 0, unit_ = 1;
    Scene scene_;
    bool has_scene_ = false;
    EdgeRows floor_rows_;                // scene_.floor and scene_.blocked, indexed for can_stand()
    std::vector<EdgeRows> blocked_rows_;
    bool walkable(float x, float y) const;   // as Scene::walkable, through those
    int scene_scale_ = 1, scene_x_ = 0, scene_y_ = 0;
    Box desk_;                           // where the pet may not stand: the work spot's foot, with a margin
    int desk_base_ = 0;
    Cover cover_;
    Box screen_;
    int half_width_ = 0;                 // the widest reach of Idle either side of the feet, screen pixels
    Area roaming_;
    mutable std::vector<Point> lap_;
    mutable bool lap_measured_ = false;
    mutable std::vector<char> reach_;    // path cells joined to the home cell (CELL-pixel grid)
    mutable bool reach_measured_ = false;
};

}  // namespace nestbox
