// The pet's body: which animation each look plays (with fallbacks), how it gets from one look to the next, and
// drawing the whole screen. Portable C++; the platform supplies the time and a display.
//
// Changes are animated: the pet walks to where the new look happens (its desk, its bed) around obstacles, turns a
// step at a time to face the right way, and only then plays it. Reactions play where it stands, after turning to the
// front.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "body/looks.h"
#include "body/stage.h"
#include "egg/egg.h"
#include "screen/canvas.h"
#include "screen/display.h"
#include "screen/hud.h"

namespace nestbox {

// A place in the scene: where the pet stands, a point in screen pixels, a point in percent of the pet's scene, its
// home, its home facing its desk, or the scene's target.
struct Spot {
    enum class Kind { Here, Point, Scene, Home, Desk, Target };
    Kind kind = Kind::Here;
    float x = 0, y = 0;
    bool operator==(const Spot& o) const { return kind == o.kind && x == o.x && y == o.y; }
    bool operator!=(const Spot& o) const { return !(*this == o); }
};

// What the body is asked to do while it shows Look::Act: one step sent with "do".
struct Act {
    enum class Kind { Anim, Look, Wander, Laps, Throw };
    Kind kind = Kind::Anim;
    std::string anim;                    // Kind::Anim: one of its animations (Idle if it lacks it; empty: Idle)
    Look look = Look::Idle;              // Kind::Look
    Spot spot;                           // where it walks first; Here: where it stands, or a look's own place
    std::vector<Spot> through;           // walking to `spot`: points it carves one smooth curve through on the way
    std::string moving;                  // walking to `spot`: the animation it plays on the way (empty: Walk)
    float spin = 0;                      // walking to `spot`: turns a second its facing goes round on the way
    Spot aim;                            // Kind::Throw: what it throws at (Here: the scene's target)
    int row = -1;                        // the facing it plays in (0-7); -1: the act's own
    Spot face;                           // a place or point to face instead of `row` (Here: none)
    float speed = 1;                     // times its walking pace and its animation's speed
    int count = 1;                       // Kind::Laps: laps; Kind::Throw: throws
    int plays = 0;                       // Kind::Anim: played this many times, then held on its last frame (0: loops)
    // How it gets to `spot`: walking its floor, or (a pastime's steps) swimming straight through its scene's water,
    // or jumping in an arc, into the water or out of it.
    enum class Move { Walk, Swim, Jump };
    Move move = Move::Walk;
    std::string move_anim;               // Move::Swim or Jump: the animation it swims or jumps with (empty: its own)
    bool operator==(const Act& o) const {
        return kind == o.kind && anim == o.anim && look == o.look && spot == o.spot && through == o.through &&
               moving == o.moving && spin == o.spin && aim == o.aim && row == o.row &&
               face == o.face && speed == o.speed && count == o.count && plays == o.plays && move == o.move &&
               move_anim == o.move_anim;
    }
    bool operator!=(const Act& o) const { return !(*this == o); }
};

class Pet {
public:
    enum class Phase { Free, Walking, Turning, Acting };
    static constexpr int SPEECH_SIZE = 2;    // a bubble's font size when none is given

    // Set the display before the egg (default: default_display()).
    void set_display(const Display& d);
    void set_egg(const Egg* egg);
    bool has_egg() const { return egg_ != nullptr; }
    bool jumping() const { return jumping_; }
    // In its scene's water and off its floor: swimming (drawn sunk to the waterline).
    bool swimming() const;
    // The top of its head as last drawn, screen pixels (above the screen when its scene runs past the top).
    Point drawn_head() const { return {float(top_x_), float(top_y_)}; }
    const Egg* egg() const { return egg_; }
    const Display& display() const { return stage_.display(); }
    const Stage& stage() const { return stage_; }

    // Where the pet should end up; it walks, turns, then acts on its own.
    void want(Look s, uint32_t now_ms);
    Look wanted() const { return want_; }
    // What it visibly does: the wanted look once it has arrived and turned, else the one it is leaving.
    Look look() const { return phase_ == Phase::Acting || phase_ == Phase::Free ? want_ : shown_; }
    Phase phase() const { return phase_; }
    uint32_t acting_since() const { return act_ms_; }   // when it began acting (its animation's first frame)
    const char* phase_label() const;
    // What the last frame showed, e.g. "Attack frames 2-7 + desk".
    const std::string& playing() const { return playing_; }
    float x() const { return x_; }
    float y() const { return y_; }
    int row() const { return row_; }
    // The facing the last frame drew (a one-row animation always faces you): what replies report.
    int drawn_row() const { return drawn_row_; }
    int drawn_frame() const { return drawn_frame_; }   // the frame of its animation the last frame drew

    // What Look::Act does. A new spot sends it walking there (to the nearest point it may stand on); anything else
    // new starts over where it stands. `again` starts it over even when nothing changed (the next of two equal steps).
    void act(const Act& a, bool again = false);
    static constexpr float MIN_ACT_SPEED = 0.25f, MAX_ACT_SPEED = 4;
    static constexpr size_t MAX_THROUGH = 8;  // points a step's curve may pass through
    static constexpr float MAX_SPIN = 4;      // turns a second
    const Act& current_act() const { return act_; }
    // The act has run its course: its laps run, its throws thrown. Only laps and throws end by themselves.
    bool act_done(uint32_t now_ms) const;
    // How far it still has to walk on its current way, screen pixels (0 when it is not walking).
    float route_left() const;
    // Its walk's clock, ms: it runs with the ground covered (twice as fast when it hurries), so the Walk frames
    // keep step with its feet and carry on through turns and waypoints.
    float stride_ms() const { return stride_ms_; }
    // One run of an animation (its Idle if the egg lacks it), ms; 0 without an egg.
    uint32_t anim_ms(const std::string& anim) const;
    // One run of what it plays now, ms at the act's speed; 0 without an egg.
    uint32_t run_ms() const;
    // The egg has a throw kit it can use on this screen (a hoop or splash flight needs the scene's target).
    bool can_throw() const;
    // It can throw, and its kit says it plays with it in its free time.
    bool plays() const;
    // A cued part of its scene to show from `since_ms` (empty: none), named as its ambient part is.
    void show_part(const std::string& name, uint32_t since_ms) {
        shown_part_ = name;
        shown_since_ = since_ms;
    }
    const std::string& shown_part() const { return shown_part_; }
    uint32_t shown_since() const { return shown_since_; }
    // Where a thrown item leaves: while throwing, the kit's release point in the throw's frame where it stands; else
    // the top of its head as last drawn (a beak, a turret).
    Point item_from() const;
    // A spot in screen pixels: a point as it is, a point of the scene, a place; none for Here (or a missing target).
    std::optional<Point> spot_point(const Spot& s) const;

    // A speech bubble or caption ("" for none) in this text colour; `size` is the bubble's font size, 1-3 (a caption
    // always uses the status bar's).
    void say(const std::string& text, uint16_t color, int size = SPEECH_SIZE, SpeechAt at = SpeechAt::Pet) {
        speech_ = {text, color, size, at};
    }
    // An effect around the pet, whatever it does: music notes (and swaying), sparkles, confetti, a letter.
    void effect(Effect e) { effect_ = e; }
    // Words along the bottom of the screen, besides any bubble ("" for none).
    void caption(const std::string& text, uint16_t color) {
        caption_text_ = text;
        caption_color_ = color;
    }
    // Typing pace in keys per second (below 0: unknown); while it works, text fills its screen at that pace.
    // keys false: a pause in the typing; the text stays and the cursor blinks.
    void typing(float kps, bool keys = true) { kps_ = kps; keys_ = keys; }
    // A sign over the pet sent with "mark" (Bubble::None: none). A look's own sign comes first.
    void mark(Bubble b) { mark_ = b; }
    // The scene shows its picture for this time of day (or the nearest one), cross-fading.
    void daypart(Daypart p);
    Daypart daypart() const { return part_; }
    // The status bar along the top, newest line last; empty = no bar.
    void status(StatusBar::Lines lines) { status_ = std::move(lines); }
    // Progress rings in the scene's gauges corner (top right without a scene); the last one nearest the corner.
    void meters(std::vector<MeterRing> rings) { meters_ = std::move(rings); }
    // Burn-in care (door/rest.h): the status bar and gauges leave the screen once they have stayed the same for
    // HUD_REST_MS, and come back as soon as they change.
    static constexpr uint32_t HUD_REST_MS = 5 * 60 * 1000;
    void care(bool on) { care_ = on; }
    // The screen saver, for an OLED screen with burn-in care while it is dimmed: only the pet, standing or asleep, drifting
    // slowly over black, so nothing stays lit in one place.
    void saver(bool on) { saver_ = on; }
    static constexpr uint32_t SAVER_ACROSS_MS = 7 * 60 * 1000, SAVER_DOWN_MS = 11 * 60 * 1000;   // one sway each way

    void update(uint32_t now_ms);
    void render(Canvas& c, uint32_t now_ms);
    void render_saver(Canvas& c, uint32_t now_ms);

private:
    // An animation chosen for a look, ready to draw.
    struct Pose {
        const Anim* anim = nullptr;
        float speed = 1;
        std::vector<int> frames;         // empty = all frames
        int row = 0;
        Prop prop = Prop::None;
        bool shake = false;
        Bubble bubble = Bubble::None;
        bool toward_desk = false;        // the row was worked out from where it stands (Facing::Desk)
        bool lap = false;                // it runs laps (Play::lap) with this animation
    };
    // A throw at one moment: the pet's pose and the item.
    struct Throw {
        Pose pose;
        int item_frame = -1;             // -1: the pet holds the item, or it is gone
        Point item;
        bool effect = false;             // the target's moment: a net swishing, ripples spreading
        Point effect_at;
        int effect_tick = 0;             // ticks into the moment
    };
    struct Goal {
        bool spot = false;               // false: where it stands
        float x = 0, y = 0;
        int row = 0;
        bool turn = true;
    };
    struct Speech {
        std::string text;
        uint16_t color = 0;
        int size = SPEECH_SIZE;
        SpeechAt at = SpeechAt::Pet;
    };

    int unit() const { return stage_.unit(); }
    // The pixel size of signs over the head, notes and sparkles: the screen's own, the same for every pet.
    int sign_unit() const { return stage_.display().scale; }
    // Its walking speed in screen pixels per second: the egg's own pace, else the engine's.
    float pace() const;
    // Turning on the spot: one facing per this many ms, about one step of its walk.
    uint32_t turn_step_ms() const;
    // One turning step toward `goal` when its time has come.
    void turn_toward(int goal, uint32_t now_ms);
    float rand01();

    // What it plays (pet.cpp). `laps` false skips the plays that run laps: what it does between them.
    Pose resolve(Look s, bool laps = true) const;
    Pose resolve_act(bool laps) const;
    Pose idle_now(uint32_t t_ms) const;
    void draw_pet(Canvas& c, const Pose& p, uint32_t t_ms, int feet_x, int feet_y, int dx);
    void draw_overlays(Canvas& c, const Pose& p, bool acting, const std::optional<Throw>& thrown, uint32_t now_ms);

    // Getting there (move.cpp).
    Goal goal_for(Look s) const;
    Goal act_goal() const;
    std::vector<Point> curve_through() const;
    int row_to_aim(float x, float y) const;
    bool roams() const;
    bool wanders() const;
    Look look_now() const;
    float act_speed() const;
    Goal watch_goal(Pixel home) const;
    void begin(uint32_t now_ms);
    void free_update(float dt, uint32_t now_ms);
    bool pick_roam_target(bool wander);
    bool walk_along(float dt, float speed, bool follow = false);
    float lap_pace() const;
    bool runs_laps() const;
    void round_corners(bool heading_on);
    // Laps: from where it stands to the floor's lap, round it, and back to where it started.
    void start_laps(uint32_t now_ms);
    void lap_update(float dt, uint32_t now_ms);

    // The room around it (room.cpp).
    void draw_room(Canvas& c) const;
    void paint_room(uint16_t* px, const std::vector<uint16_t>& colours,
                    const std::vector<uint16_t>& from_colours) const;
    void draw_plain_room(Canvas& c) const;
    void draw_ambient(Canvas& c, const std::vector<uint16_t>& colours, const std::vector<uint16_t>& from_colours) const;
    void start_jump(Point to, bool out, uint32_t now);
    void land(uint32_t now);
    float jump_lift() const;
    void draw_swimmer(Canvas& c, const FrameBox& b, int left, int top, int waterline, uint32_t now, bool afloat) const;
    std::optional<int> jump_surface() const;
    void draw_splash(Canvas& c, uint32_t now) const;
    void draw_workspot(Canvas& c, bool on, uint32_t now_ms) const;
    void draw_plain_desk(Canvas& c, bool on, uint32_t now_ms) const;
    void draw_typed_text(Canvas& c) const;
    void draw_effect(Canvas& c, const Throw& thrown) const;

    // Throwing (throwing.cpp).
    bool throwing() const;
    uint32_t pose_ms(const Pose& p) const;      // one play of a pose's frames
    uint32_t loop_end(uint32_t now_ms) const;   // when the reaction playing ends its loop; 0: now
    Point aim_point() const;
    int throw_cycle_ticks() const;
    std::optional<Throw> throw_at(const Pose& base, uint32_t t_ms) const;

    Stage stage_;
    const Egg* egg_ = nullptr;

    // Where it is going and what it shows.
    Look want_ = Look::Idle, shown_ = Look::Idle;
    // A reaction whose time ran out mid-loop plays to the end of that loop (up to FINISH_MAX_MS), so it never stops
    // mid-jump; then the look it was asked for comes.
    static constexpr uint32_t FINISH_MAX_MS = 2000;
    std::optional<Look> next_;
    uint32_t next_at_ = 0;
    Phase phase_ = Phase::Free;
    Goal goal_;
    uint32_t act_ms_ = 0, turn_ms_ = 0;
    uint32_t begun_ms_ = 0;              // when it set out for the look it wants
    Act act_;

    // The body.
    float x_ = 0, y_ = 0;
    int row_ = 0;
    std::vector<Point> route_;
    float speed_ = 1;
    float stride_ms_ = 0;                // its walk's own clock: distance walked, as time at its own pace
    bool walking_ = false;               // it walked on the last update
    bool setting_off_ = false;           // about to walk: facing its way first
    bool turning_off_ = false;           // and turning round on the spot first (its way over 45 degrees off)
    int drawn_row_ = 0;                  // the facing the last frame drew
    int drawn_frame_ = 0;
    int heading_ = 0;                    // the facing of the way it heads; its row follows a step at a time
    float walk_turn_ms_ = 0;             // since its facing last changed while walking
    float last_dx_ = 0, last_dy_ = 1;    // the way it last moved, a unit vector
    Point route_end_;                    // the way it is walking: its end and length, to notice a new one
    size_t route_size_ = 0;
    float pause_ms_ = 600;               // free roaming: standing still for this much longer
    uint32_t pause_start_ = 0;
    uint32_t last_ms_ = 0;
    mutable FrameCache frame_cache_;      // the frame drawn last, its codes decoded
    mutable FrameCache item_cache_;       // and the thrown item's
    mutable std::vector<FrameCache> part_cache_;   // two per scene part: its frame now, and the one it fades from
    std::string shown_part_;             // the cued scene part a step shows, and since when
    uint32_t shown_since_ = 0;
    // Swimming and jumping (a pastime's steps): a jump flies its straight way in an arc; out of the water after an
    // interruption it starts over where it was going.
    bool jumping_ = false, jump_out_ = false;
    Point jump_from_;
    float jump_len_ = 0;
    uint32_t jump_ms_ = 0;               // when the jump began
    uint32_t splash_ms_ = 0;             // the last landing in the water, and where
    Point splash_at_;
    uint32_t rng_ = 12345;
    bool lapping_ = false;               // on its laps now
    uint32_t lap_end_ms_ = 0;            // when the last laps ended (0: none yet in this look)
    float lap_rest_ms_ = 0;              // a mood's rest between laps

    // What the last frame drew.
    std::string playing_;
    int top_x_ = 0, top_y_ = 0;          // the top of the frame drawn, where bubbles go
    int reach_left_ = 0, reach_right_ = 0, reach_top_ = 0;   // what the playing animation reaches, all its frames
    SpeechPlace speech_place_;           // where the speech bubble went, so it stays put

    // Told from outside every frame.
    Speech speech_;
    std::string caption_text_;
    uint16_t caption_color_ = 0;
    Effect effect_ = Effect::None;
    // When the letter first showed (it waits for a reaction's sign to end), so it pops in once and then bobs.
    std::optional<uint32_t> letter_since_;
    bool keys_ = true;
    float kps_ = -1;
    Bubble mark_ = Bubble::None;
    StatusBar::Lines status_;
    StatusBar bar_;
    CaptionBar caption_;
    std::vector<MeterRing> meters_;
    bool saver_ = false;
    bool care_ = false, hud_rest_ = false;      // burn-in care; the status bar and gauges resting this frame
    uint32_t hud_since_ = 0;                    // since when they have looked the same
    std::string hud_key_;                       // what they showed, to notice a change
    const StatusBar::Lines& shown_status() const;
    void rest_hud(uint32_t now);

    // The scene's look.
    Daypart part_ = Daypart::Afternoon;
    const uint8_t* pic_ = nullptr;       // the picture shown, and the one it is fading from
    const uint8_t* pic_from_ = nullptr;
    float dim_ = 0, dim_from_ = 0;
    float fade_ = 1;                     // 0 just changed .. 1 done
    float night_ = 0;                    // 0 day .. 1 night
    float typed_ = 0;                    // characters typed on its screen

    // The picture as the screen shows it, kept between frames: it changes only with the time of day, the dimming,
    // the view and the pet, and copying it costs a small CPU far less than looking up each pixel's colour.
    struct Room {
        std::vector<uint16_t> px, colours;
        const uint8_t* pic = nullptr;
        int x = 0, y = 0, w = 0, h = 0;
        std::vector<uint32_t> slices;    // px's slice fingerprints (screen/slices.h)
        uint32_t frames = 0;             // frames drawn from px, for restore_room's rounds
    };
    mutable Room room_;
    void restore_room(Canvas& c, const Box (&covered)[2]) const;
    // The work spot's pixels as drawn (screen index, colour), kept the same way: they change only when its screen
    // goes on or off, with the night, and with the pet.
    struct Desk {
        std::vector<std::pair<uint32_t, uint16_t>> px;
        const uint8_t* picture = nullptr;
        bool on = false;
        float night = -1;
        int w = 0, h = 0, s = 0;
    };
    mutable Desk desk_;
};

}  // namespace nestbox
