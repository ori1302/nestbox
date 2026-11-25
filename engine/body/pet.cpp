#include "body/pet.h"
#include <algorithm>
#include <cmath>
#include "body/parts.h"

namespace nestbox {

namespace {
// Standing still in Idle: after a rest it looks around (other rows), then nods slowly if it can.
constexpr uint32_t IDLE_LOOK_FROM_MS = 2500, IDLE_LOOK_STEP_MS = 900;
constexpr int IDLE_LOOKS[] = {ROW_FRONT_RIGHT, ROW_FRONT, ROW_FRONT_LEFT, ROW_FRONT};
constexpr uint32_t IDLE_NOD_FROM_MS = IDLE_LOOK_FROM_MS + 4 * IDLE_LOOK_STEP_MS;
constexpr float IDLE_NOD_SPEED = 0.5f;

constexpr uint32_t SHAKE_PERIOD_MS = 1200, SHAKE_MS = 450, SHAKE_FLIP_MS = 60;
constexpr uint32_t SWAY_MS = 500;            // music: one sway step
constexpr uint32_t NOTE_LIFE_MS = 2200, NOTE_SHOWN_MS = 1900;
constexpr int NOTE_LEFT_X = -9, NOTE_RIGHT_X = 7;       // where each note starts beside the head, art pixels
constexpr uint32_t NOTE_DRIFT_MS = 400, NOTE_RISE_MS = 120;   // ms per half pixel sideways, and per pixel up
constexpr int SIGN_REACH = 12;              // art px a sign over the head reaches, up and to the right

// How far a dashing strike has slid at frame `f`: 0 at rest, 1 at its hit.
float dash_amount(const Anim& a, int f) {
    if (!a.dash || a.rush < 0 || a.ret < 0) return 0;
    const int hit = a.hit >= 0 ? a.hit : a.ret;
    if (f < a.rush || f > a.ret) return 0;
    if (f < hit) return float(f - a.rush + 1) / float(hit - a.rush + 1);
    if (f == hit) return 1;
    return 1 - float(f - hit) / float(a.ret - hit + 1);
}

int row_for(Facing f) {
    switch (f) {
        case Facing::Up: return ROW_BACK;
        case Facing::UpRight: return ROW_BACK_RIGHT;
        case Facing::Right: return ROW_RIGHT;
        case Facing::Down:
        case Facing::Desk: break;
    }
    return ROW_FRONT;
}

// The middle of the topmost drawn row of frame `b`, its whole frame's top-left at (left, top); none if empty.
std::optional<Pixel> head_of(const FrameBox& b, int left, int top, int unit) {
    for (int sy = 0; sy < b.h; sy++) {
        int lo = -1, hi = -1;
        for (int sx = 0; sx < b.w; sx++)
            if (b.px[sy * b.w + sx]) {
                if (lo < 0) lo = sx;
                hi = sx;
            }
        if (lo >= 0) return Pixel{left + (2 * b.x + lo + hi) * unit / 2, top + (b.y + sy) * unit};
    }
    return std::nullopt;
}
}  // namespace

const char* Pet::phase_label() const {
    switch (phase_) {
        case Phase::Free: return "free";
        case Phase::Walking: return "walking";
        case Phase::Turning: return "turning";
        case Phase::Acting: break;
    }
    return "acting";
}

float Pet::rand01() {
    rng_ = rng_ * 1664525u + 1013904223u;   // a linear congruential generator (Numerical Recipes)
    return float(rng_ >> 8) / float(1 << 24);
}

void Pet::set_display(const Display& d) {
    stage_.set_display(d);
    if (egg_) set_egg(egg_);
}

void Pet::set_egg(const Egg* egg) {
    egg_ = egg;
    frame_cache_ = FrameCache();
    item_cache_ = FrameCache();
    part_cache_.clear();
    room_ = Room();
    desk_ = Desk();
    stage_.set_egg(egg);
    if (stage_.has_scene()) {
        pic_ = stage_.scene().picture(part_, dim_);
        fade_ = 1;
    }
    const Pixel home = stage_.home();
    x_ = float(home.x);
    y_ = float(home.y);
    row_ = ROW_FRONT;
    route_.clear();
    night_ = 0;
    want_ = shown_ = Look::Idle;
    phase_ = Phase::Free;
    pause_ms_ = 1500;
}

void Pet::daypart(Daypart p) {
    if (p == part_) return;
    part_ = p;
    if (!stage_.has_scene()) return;
    pic_from_ = pic_;
    dim_from_ = dim_;
    pic_ = stage_.scene().picture(p, dim_);
    if (pic_ != pic_from_ || dim_ != dim_from_) fade_ = 0;
}

uint32_t Pet::anim_ms(const std::string& anim) const {
    if (!egg_) return 0;
    const Anim* a = egg_->find(anim);
    if (!a) a = egg_->find("Idle");
    return a ? ticks_to_ms(a->total_ticks()) : 0;
}

Pet::Pose Pet::resolve(Look s, bool laps) const {
    if (s == Look::Act) return resolve_act(laps);
    Pose r;
    const LookInfo& rule = info(s);
    r.bubble = rule.bubble;
    for (const Play& p : rule.plays) {
        if (p.typing && !keys_) continue;
        const Anim* a = egg_->find(p.anim);
        if (!a || (p.lap && (!laps || a->rows < ROWS || stage_.lap().empty()))) continue;
        r.anim = a;
        r.lap = p.lap;
        r.speed = p.speed;
        r.prop = p.prop;
        r.shake = p.shake;
        r.row = row_for(p.facing);
        if (p.facing == Facing::Desk) {
            r.toward_desk = true;
            r.row = stage_.desk_row(x_, y_);
        }
        if (p.loop == PlayLoop::Strike && a->rush >= 0 && a->ret >= 0)
            for (int f = a->rush; f <= a->ret; f++) r.frames.push_back(f);
        return r;
    }
    r.anim = egg_->find("Idle");
    return r;
}

// A step's pose: a look at the step's speed, laps, a throw's resting pose (throw_at draws the throw itself), or an
// animation. A throw without a kit is a happy look.
Pet::Pose Pet::resolve_act(bool laps) const {
    Pose r;
    switch (act_.kind) {
        case Act::Kind::Look:
            r = resolve(act_.look, laps);
            r.speed *= act_.speed;
            if (act_.row >= 0) r.row = act_.row;                     // the step's face
            else if (act_.spot.kind != Spot::Kind::Here) r.row = row_;   // sent somewhere: the way it stands there
            return r;
        case Act::Kind::Throw:
            if (!can_throw()) {                                      // a pet that cannot throw skips it
                r.anim = egg_->find("Idle");
                r.row = row_;
                return r;
            }
            r.anim = egg_->find(egg_->throw_kit.anim);
            r.row = row_;
            return r;
        case Act::Kind::Laps: {
            r.anim = runs_laps() ? egg_->find("Zoomies") : egg_->find("Walk");
            r.lap = laps && r.anim && r.anim->rows >= ROWS && !stage_.lap().empty();
            if (!r.lap) r.anim = egg_->find("Idle");
            r.row = row_;
            return r;
        }
        case Act::Kind::Anim:
        case Act::Kind::Wander:
            break;
    }
    r.anim = act_.anim.empty() ? nullptr : egg_->find(act_.anim);
    if (!r.anim) r.anim = egg_->find("Idle");
    r.row = act_.row < 0 ? row_ : act_.row;
    r.speed = act_.speed;
    return r;
}

uint32_t Pet::run_ms() const {
    if (!egg_) return 0;
    if (throwing()) return ticks_to_ms(throw_cycle_ticks()) / act_speed();
    const Pose p = resolve(want_);
    if (!p.anim) return 0;
    int ticks = 0;
    if (p.frames.empty()) ticks = p.anim->total_ticks();
    for (int f : p.frames) ticks += p.anim->durations[f];
    return uint32_t(ticks_to_ms(ticks) / std::max(0.01f, p.speed));
}

Pet::Pose Pet::idle_now(uint32_t t) const {
    Pose r;
    r.anim = egg_->find("Idle");
    if (t >= IDLE_LOOK_FROM_MS && t < IDLE_NOD_FROM_MS) {
        r.row = IDLE_LOOKS[(t - IDLE_LOOK_FROM_MS) / IDLE_LOOK_STEP_MS];
    } else if (t >= IDLE_NOD_FROM_MS) {
        if (const Anim* nod = egg_->find("Nod")) {
            r.anim = nod;
            r.speed = IDLE_NOD_SPEED;
        }
    }
    return r;
}

void Pet::draw_pet(Canvas& c, const Pose& p, uint32_t t_ms, int feet_x, int feet_y, int dx) {
    const Anim& a = *p.anim;
    const int u = unit();
    std::vector<int> frames = p.frames;
    if (frames.empty())
        for (int i = 0; i < a.frames; i++) frames.push_back(i);
    std::vector<uint8_t> durations;
    for (int i : frames) durations.push_back(a.durations[i]);
    const int f = frames[frame_at(durations.data(), int(durations.size()), ms_to_ticks(t_ms, p.speed))];
    const int row = std::min(p.row, a.rows - 1);
    drawn_row_ = row;
    drawn_frame_ = f;
    const float lunge = dash_amount(a, f) * DASH_PX * (row == ROW_BACK ? -1 : 1);
    const int cx = (feet_x + dx) / u * u;
    const int cy = int(std::lround((feet_y - egg_->feet * u + lunge * u) / u)) * u;   // the frame's centre
    int left = cx - a.fw / 2 * u, top = cy - a.fh / 2 * u;
    const FrameBox& frame = pixels_of(a.box(row, f), frame_cache_);
    if (swimming() && !jumping_) {                           // in the water: sunk to it, only what is above it shows
        const float deep = stage_.water_deep(x_, y_);        // wading: less near the bank
        top += int(std::lround(egg_->scene.water_depth * deep)) * u;
        draw_swimmer(c, frame, left, top, feet_y, last_ms_, true);
    } else if (const std::optional<int> surface = jump_surface()) {   // rising out of the water, or diving into it
        draw_swimmer(c, frame, left, top, *surface, last_ms_, false);
    } else {
        c.sprite(frame, egg_->palette.data(), left, top, u);
    }
    const Pixel head = head_of(frame, left, top, u).value_or(Pixel{feet_x, feet_y - egg_->tall * u});
    top_x_ = head.x;
    top_y_ = head.y;
    reach_left_ = reach_right_ = cx;
    reach_top_ = head.y;
    for (int i : frames) {
        const FrameBox& b = a.box(row, i);
        if (!b.w || !b.h) continue;
        reach_left_ = std::min(reach_left_, left + b.x * u);
        reach_right_ = std::max(reach_right_, left + (b.x + b.w) * u);
        reach_top_ = std::min(reach_top_, top + b.y * u);
    }
    playing_ = a.name;
    if (!p.frames.empty())
        playing_ += " frames " + std::to_string(p.frames.front()) + "-" + std::to_string(p.frames.back());
}

const StatusBar::Lines& Pet::shown_status() const {
    static const StatusBar::Lines none;
    return hud_rest_ ? none : status_;
}

void Pet::rest_hud(uint32_t now) {
    std::string key;
    for (const auto& [text, colour] : status_) key += text + '\x1f' + std::to_string(colour) + '\x1e';
    for (const MeterRing& m : meters_) key += m.label + '\x1f' + std::to_string(int(m.fill * 100)) + '\x1e';
    if (key != hud_key_) {
        hud_key_ = key;
        hud_since_ = now;
    }
    hud_rest_ = care_ && now - hud_since_ >= HUD_REST_MS;
}

void Pet::render(Canvas& c, uint32_t now) {
    if (!egg_) {
        c.fill(0);
        return;
    }
    rest_hud(now);
    c.tint = egg_->screen_lut.empty() ? nullptr : egg_->screen_lut.data();   // its screen palette, if it has one
    draw_room(c);
    draw_splash(c, now);                                     // on the water, under it
    Pose r;
    uint32_t t = now;
    const bool acting = phase_ == Phase::Acting;
    std::optional<Throw> thrown;
    if (acting) {
        r = resolve(want_);
        t = now - act_ms_;                                   // every animation starts at its first frame
        if (want_ == Look::Act && act_.kind == Act::Kind::Anim && act_.plays > 0) {   // and a set number of plays
            const uint32_t run = run_ms();                   // ends on its last frame, never a wrapped first one
            if (run) t = std::min<uint32_t>(t, run * uint32_t(act_.plays) - 1);
        }
        if (r.lap && lapping_) {
            r.row = row_;                                    // the way it runs
            if (!runs_laps()) t = uint32_t(stride_ms_);      // on its walk: the legs follow the ground covered
        } else if (r.lap) {                                  // between laps: the look's next play, once turned
            r = resolve(want_, false);
            t = lap_end_ms_ ? now - lap_end_ms_ : t;
            if (row_ != r.row) {
                const Bubble bubble = r.bubble;
                r = Pose();
                r.anim = egg_->find("Idle");
                r.row = row_;
                r.bubble = bubble;
            }
        }
        if (throwing()) thrown = throw_at(r, t);
        if (thrown) {
            r = thrown->pose;
            t = 0;
        }
    } else {
        const bool moving = !route_.empty() && !setting_off_ &&
                            (phase_ == Phase::Walking || (phase_ == Phase::Free && pause_ms_ <= 0));
        if (moving && swimming()) {                         // the step's own swim, else its Swim, else its walk
            if (act_.move == Act::Move::Swim && !act_.move_anim.empty()) r.anim = egg_->find(act_.move_anim);
            if (!r.anim) r.anim = egg_->find("Swim");
        }
        const bool own_way = moving && want_ == Look::Act && act_.move == Act::Move::Walk;
        if (own_way && !r.anim && !act_.moving.empty()) r.anim = egg_->find(act_.moving);   // the step's own way
        if (moving && !r.anim) r.anim = egg_->find("Walk");
        if (moving && r.anim) t = uint32_t(stride_ms_);     // the legs follow the ground covered, not the clock
        if (moving && jumping_) {                           // its jump, played once over the way it flies
            const Anim* jump = act_.move == Act::Move::Jump && !act_.move_anim.empty() ? egg_->find(act_.move_anim)
                               : act_.move == Act::Move::Jump && !act_.anim.empty() ? egg_->find(act_.anim)
                                                                                     : nullptr;
            if (!jump) jump = egg_->find("Jump");
            if (jump) {
                r.anim = jump;
                const float run = float(ticks_to_ms(jump->total_ticks()));
                t = uint32_t(std::clamp(1 - route_left() / jump_len_, 0.0f, 0.999f) * run);
            }
        }
        if (!r.anim) {
            if (phase_ == Phase::Free && look_now() == Look::Idle && row_ == ROW_FRONT)
                r = idle_now(now - pause_start_);
            else r.anim = egg_->find("Idle");
        }
        if (moving || row_ != ROW_FRONT || phase_ != Phase::Free || look_now() != Look::Idle) r.row = row_;
        if (own_way && act_.spin > 0)                       // spinning round as it goes, from the way it set off
            r.row = (row_ + int(float(now - begun_ms_) * act_.spin * 8 / 1000)) % 8;
    }
    if (swimming() && r.anim && r.anim->name == "Idle") {   // still in the water (or turning to leap): treading it
        const Anim* swim =
            act_.move == Act::Move::Swim && !act_.move_anim.empty() ? egg_->find(act_.move_anim) : nullptr;
        if (!swim) swim = egg_->find("Swim");
        if (swim) {
            r = Pose();
            r.anim = swim;
            r.row = row_;
            t = now;
        }
    }
    const int u = unit();
    const int fx = int(x_), fy = int(y_ - jump_lift());
    const bool desk_on = acting && r.prop == Prop::Desk;
    int dx = acting && r.shake && (t % SHAKE_PERIOD_MS) < SHAKE_MS ? ((now / SHAKE_FLIP_MS) % 2 ? u : -u) : 0;
    const bool walking = !setting_off_ && (phase_ == Phase::Walking || lapping_ ||
                                           (phase_ == Phase::Free && pause_ms_ <= 0 && !route_.empty()));
    if (effect_ == Effect::Notes && !walking && !dx) dx = (now / SWAY_MS) % 2 ? u : 0;
    const bool typing = acting && look_now() == Look::Type && desk_on;
    if (fy < stage_.desk_base()) {                           // behind the work spot
        draw_pet(c, r, t, fx, fy, dx);
        draw_workspot(c, desk_on, now);
        if (typing) draw_typed_text(c);
    } else {
        draw_workspot(c, desk_on, now);
        if (typing) draw_typed_text(c);
        draw_pet(c, r, t, fx, fy, dx);
    }
    if (desk_on) playing_ += " + desk";
    if (phase_ == Phase::Walking) playing_ += " to its spot";
    if (phase_ == Phase::Turning) playing_ += " turning";
    if (acting && lapping_) playing_ += " laps";
    draw_overlays(c, r, acting, thrown, now);
    // While a time of day or scene fades in, the blend makes colours of its own: then the whole frame is snapped.
    if (c.tint && (fade_ < 1 || (night_ > 0 && night_ < 1))) c.recolour(egg_->screen_lut);
    c.tint = nullptr;
}

void Pet::draw_overlays(Canvas& c, const Pose& p, bool acting, const std::optional<Throw>& thrown, uint32_t now) {
    const int u = unit();
    const int fx = int(x_), fy = int(y_);
    if (thrown && thrown->item_frame >= 0) {                    // centred on its point
        const Anim* item = egg_->find(egg_->throw_kit.item);
        const FrameBox& frame = pixels_of(item->box(0, thrown->item_frame), item_cache_);
        c.sprite(frame, egg_->palette.data(), int(thrown->item.x) - item->fw / 2 * u,
                 int(thrown->item.y) - item->fh / 2 * u, u);
    }
    if (thrown && thrown->effect) draw_effect(c, *thrown);      // over the item: a net's front hides it going through
    // Signs, notes and sparkles keep the screen's own pixel size whatever the pet's, so they read the same on every
    // pet.
    const int su = sign_unit();
    if (acting && p.bubble != Bubble::None) draw_bubble(c, p.bubble, top_x_, top_y_, su, now);
    else if (mark_ != Bubble::None) draw_bubble(c, mark_, top_x_, top_y_, su, now);
    const bool signed_head = (acting && p.bubble != Bubble::None) || mark_ != Bubble::None;
    // A letter waits for a reaction (the jump it comes with), turning to it included, and stands beside any sign.
    const bool letter = effect_ == Effect::Letter && !(is_reaction(look_now()) && phase_ != Phase::Free);
    if (effect_ == Effect::Notes && !(acting && p.bubble == Bubble::Note))   // two notes drifting up, half a life apart
        for (int k = 0; k < 2; k++) {
            const uint32_t life = (now + k * NOTE_LIFE_MS / 2) % NOTE_LIFE_MS;
            const int nx = fx + (k ? NOTE_LEFT_X : NOTE_RIGHT_X) * u + int(life / NOTE_DRIFT_MS) * (k ? -1 : 1) * u / 2;
            const int ny = fy - egg_->tall * u - int(life / NOTE_RISE_MS);
            if (life < NOTE_SHOWN_MS) draw_note(c, nx, ny, su);
        }
    if (effect_ == Effect::Sparkles) draw_sparkles(c, fx, fy - egg_->tall * u / 2, egg_->tall * u, su, now);
    if (effect_ != Effect::Letter) letter_since_.reset();
    if (letter) {
        if (!letter_since_ || *letter_since_ > now) letter_since_ = now;
        draw_letter(c, top_x_, top_y_, su, signed_head, now - *letter_since_);
    }
    const Display& d = stage_.display();
    if (effect_ == Effect::Confetti) draw_confetti(c, d, now);
    const StatusBar::Lines& status = shown_status();
    const int below_bar = StatusBar::bottom(d, status);
    const bool top_caption = speech_.at == SpeechAt::Top && !speech_.text.empty();
    bar_.draw(c, d, status, now);
    const bool bottom_caption = !caption_text_.empty() || (speech_.at == SpeechAt::Bottom && !speech_.text.empty());
    if (!hud_rest_) draw_meters(c, d, meters_, top_caption ? CaptionBar::bottom(d, below_bar) : below_bar,
                stage_.has_scene() ? stage_.scene().gauges : Corner::TopRight,
                bottom_caption ? CaptionBar::top(d, SpeechAt::Bottom, below_bar) - 1 : -1);
    if (speech_.at == SpeechAt::Pet) {
        // The head's signs go up and to the right of the top of the frame; the bubble keeps clear of them too.
        SpeechSpot at;
        at.head_x = fx;
        at.head_y = fy - egg_->tall * u;
        at.left = reach_left_;
        at.right = std::max(reach_right_, top_x_ + SIGN_REACH * su);
        at.top = std::min(reach_top_, top_y_ - SIGN_REACH * su);
        if (letter) {
            const LetterBox box = letter_box(top_x_, top_y_, su, signed_head);
            at.left = std::min(at.left, box.left);
            at.top = std::min(at.top, box.top);
        }
        at.feet = fy;
        const int row = drawn_row_;
        at.facing = row >= ROW_FRONT_RIGHT && row <= ROW_BACK_RIGHT ? 1 : row > ROW_BACK ? -1 : 0;
        draw_speech(c, d, speech_.text, speech_.color, speech_.size, at, below_bar, speech_place_);
    }
    if (!caption_text_.empty()) caption_.draw(c, d, caption_text_, caption_color_, SpeechAt::Bottom, below_bar, now);
    else caption_.draw(c, d, speech_.text, speech_.color, speech_.at, below_bar, now);
    mask_outside(c, d);
}

}  // namespace nestbox
