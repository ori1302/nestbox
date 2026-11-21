// The room around the pet: its scene by the time of day, the work spot (or the plain desk), the text it types.
#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>
#include "body/parts.h"
#include "body/pet.h"
#include "body/throwing.h"
#include "screen/slices.h"

namespace nestbox {

namespace {
struct DayNight {
    uint16_t day, night;
};

// The plain room, for eggs without a scene.
constexpr DayNight WALL = {rgb(0xd9, 0xe3, 0xc4), rgb(0x3b, 0x4a, 0x63)};
constexpr DayNight WALL_STRIPE = {rgb(0xcd, 0xd9, 0xb4), rgb(0x33, 0x41, 0x5a)};
constexpr DayNight FLOOR = {rgb(0xb9, 0x8a, 0x5a), rgb(0x4a, 0x3c, 0x33)};
constexpr DayNight FLOOR_SEAM = {rgb(0xa7, 0x7a, 0x4d), rgb(0x3e, 0x32, 0x2b)};
constexpr DayNight SKIRTING = {rgb(0x7b, 0x56, 0x34), rgb(0x2b, 0x22, 0x1d)};
constexpr int WALL_STRIPE_EVERY = 6;   // art pixels

// A splash's ripples: rings growing from RIPPLE_FROM to RIPPLE_FROM + RIPPLE_GROWTH art pixels across, flattened
// as water seen from above, one after another.
constexpr int RIPPLES = 2, RIPPLE_GAP_TICKS = 14;
constexpr float RIPPLE_FROM = 2, RIPPLE_GROWTH = 9, RIPPLE_FLATTEN = 0.4f;

// The plain desk.
constexpr uint16_t DESK_EDGE = rgb(0x5b, 0x3a, 0x24), DESK_TOP = rgb(0x8a, 0x5a, 0x35);
constexpr uint16_t MONITOR = rgb(0x2a, 0x2f, 0x3a), MONITOR_OFF = rgb(0x3a, 0x44, 0x52);
constexpr uint16_t MONITOR_ON = rgb(0x7f, 0xd1, 0xff), MONITOR_ON_BRIGHT = rgb(0xa9, 0xe2, 0xff);
constexpr uint32_t MONITOR_FLICKER_MS = 300;

constexpr float NIGHT_SCREEN_DIMMING = 0.2f;   // a lit screen at night dims only this much

// Typed text: one character per scene pixel, lines two pixels apart, four-character words.
constexpr int WORD_LENGTH = 4;
constexpr int PAGE_PAUSE = 6;                    // characters' worth of time a full page stays before a fresh one
constexpr uint16_t INK_DARK = rgb(0x20, 0x30, 0x48), INK_LIGHT = rgb(0xd8, 0xf0, 0xe0);
constexpr int LIGHT_SCREEN_LUMA = 1100;          // of 0..2550
constexpr uint32_t CURSOR_BLINK_MS = 300, CURSOR_BLINK_WAITING_MS = 500;

// A scene picture's pixel at night: its night picture's if it has one, else itself dimmed.
uint16_t at_night(const Egg& egg, uint16_t p, const uint8_t* night, int i) {
    return night && night[i] ? egg.palette[night[i]] : dim(p);
}

// The palette with every colour dimmed by `amount` (0: as it is, 1: half brightness).
std::vector<uint16_t> dimmed(const std::vector<uint16_t>& palette, float amount) {
    std::vector<uint16_t> out = palette;
    if (amount > 0)
        for (auto& p : out) p = mix(p, dim(p), amount);
    return out;
}

// Rough brightness of an RGB565 pixel, 0..2550.
int luma(uint16_t p) { return ((p >> 11) & 31) * 8 * 3 + ((p >> 5) & 63) * 4 * 6 + (p & 31) * 8; }
}  // namespace

void Pet::draw_room(Canvas& c) const {
    if (!stage_.has_scene()) {
        draw_plain_room(c);
        return;
    }
    const std::vector<uint16_t> colours = dimmed(egg_->palette, dim_);
    const std::vector<uint16_t> from_colours = fade_ < 1 ? dimmed(egg_->palette, dim_from_) : std::vector<uint16_t>();
    const int w = stage_.width(), h = stage_.height();
    if (fade_ >= 1 && c.px.size() == size_t(w) * size_t(h)) {
        Room& r = room_;
        if (r.pic != pic_ || r.colours != colours || r.x != stage_.scene_x() || r.y != stage_.scene_y() || r.w != w ||
            r.h != h) {
            r = Room{std::vector<uint16_t>(c.px.size()), colours, pic_, stage_.scene_x(), stage_.scene_y(), w, h, {}, 0};
            paint_room(r.px.data(), colours, from_colours);
            const int sw = w / SLICES;
            if (w % SLICES == 0 && sw % 2 == 0) {
                r.slices.resize(size_t(h) * SLICES);
                for (int y = 0; y < h; y++)
                    for (int k = 0; k < SLICES; k++)
                        r.slices[size_t(y) * SLICES + k] =
                            slice_hash(r.px.data() + size_t(y) * w + size_t(k) * sw, size_t(sw));
            }
        }
        // What the status bar and a caption will paint over this frame, as draw_overlays() draws them.
        const Display& d = stage_.display();
        const bool own = !caption_text_.empty();
        const StatusBar::Lines& status = shown_status();
        const Box covered[2] = {StatusBar::covers(d, status),
                                CaptionBar::covers(d, own ? caption_text_ : speech_.text,
                                                   own ? SpeechAt::Bottom : speech_.at, StatusBar::bottom(d, status))};
        restore_room(c, covered);
    } else {
        paint_room(c.px.data(), colours, from_colours);   // while the time of day fades, every frame is new
    }
    draw_ambient(c, colours, from_colours);
}

// The scene picture onto the canvas from the cache. Copying it whole is most of a small CPU's frame, so where the
// canvas says what it holds (Canvas::held), only the slices that differ from the picture are copied, and none that
// `covered` (the status bar and a caption, painted over them later this frame) hides. A band of BAND_ROWS rows is copied whole
// every BAND_EVERY frames, in turn down the screen, so a fingerprint that matched by chance (about one in four
// billion) shows for 5 s at most.
void Pet::restore_room(Canvas& c, const Box (&covered)[2]) const {
    constexpr int BAND_ROWS = 24, BAND_EVERY = 15;
    Room& r = room_;
    if (r.slices.empty() || c.held.size() != r.slices.size()) {
        std::memcpy(c.px.data(), r.px.data(), r.px.size() * sizeof(uint16_t));   // much faster than std::copy on PSRAM
        return;
    }
    const uint32_t round = r.frames++;
    const int bands = (c.h + BAND_ROWS - 1) / BAND_ROWS;
    const int band = round % BAND_EVERY ? -1 : int(round / BAND_EVERY % uint32_t(bands));
    const int sw = c.w / SLICES;
    for (int y = 0; y < c.h; y++) {
        const Box* under = nullptr;
        for (const Box& b : covered)
            if (y >= b.y && y < b.y + b.h) under = &b;
        for (int k = 0; k < SLICES; k++) {
            const size_t i = size_t(y) * SLICES + k;
            if (under && k * sw >= under->x && (k + 1) * sw <= under->x + under->w) continue;
            if (c.held[i] == r.slices[i] && y / BAND_ROWS != band) continue;
            const size_t at = size_t(y) * c.w + size_t(k) * sw;
            std::memcpy(c.px.data() + at, r.px.data() + at, size_t(sw) * sizeof(uint16_t));
        }
    }
}

// The scene picture on the screen, w x h pixels.
void Pet::paint_room(uint16_t* px, const std::vector<uint16_t>& colours,
                     const std::vector<uint16_t>& from_colours) const {
    const Scene& sc = stage_.scene();
    const int w = stage_.width(), h = stage_.height(), s = stage_.scene_scale();
    // Each screen column's picture column, worked out once: a division per pixel costs a small CPU most of its frame.
    std::vector<int> column(static_cast<size_t>(w));
    for (int x = 0; x < w; x++) column[size_t(x)] = std::clamp((x + stage_.scene_x()) / s, 0, sc.w - 1);
    for (int y = 0; y < h; y++) {
        const size_t row = size_t(std::clamp((y + stage_.scene_y()) / s, 0, sc.h - 1)) * sc.w;
        const uint8_t* to = pic_ + row;
        uint16_t* out = px + size_t(y) * w;
        if (fade_ < 1) {                                     // fading from the last time of day
            const uint8_t* from = pic_from_ + row;
            for (int x = 0; x < w; x++)
                out[x] = mix(from_colours[from[column[size_t(x)]]], colours[to[column[size_t(x)]]], fade_);
        } else {
            for (int x = 0; x < w; x++) out[x] = colours[to[column[size_t(x)]]];
        }
    }
}

// The scene's moving parts over its picture, with the same colours and fade, so they match what is around them.
void Pet::draw_ambient(Canvas& c, const std::vector<uint16_t>& colours,
                       const std::vector<uint16_t>& from_colours) const {
    const Scene& sc = stage_.scene();
    const int s = stage_.scene_scale();
    const uint64_t tick = uint64_t(last_ms_) * TICKS_PER_S / 1000;
    const bool fading = fade_ < 1;
    for (const Ambient& a : sc.ambient) {
        // A cued part only while a step shows it, or while the look it goes with plays (from when it began).
        const bool with_look = !a.during.empty() && want_ != Look::Act && phase_ == Phase::Acting &&
                               a.during == look_key(look_now());
        if (a.cue != Ambient::Cue::Always && a.name != shown_part_ && !with_look) continue;
        const std::vector<Changes>* to = sc.frames_for(a, pic_);
        const std::vector<Changes>* from = fading ? sc.frames_for(a, pic_from_) : nullptr;
        if (!to && !from) continue;
        int f = a.frame_at(tick);
        if (a.cue != Ambient::Cue::Always) {
            const uint32_t cue_from = a.name == shown_part_ ? shown_since_ : act_ms_;
            const uint64_t since = uint64_t(last_ms_ - cue_from) * TICKS_PER_S / 1000;
            f = a.cue == Ambient::Cue::Once && since >= uint64_t(a.total_ticks()) ? a.frames - 1 : a.frame_at(since);
        }
        // A changed pixel: the frame's index for each picture where it has one, else that picture's own.
        auto put = [&](int x, int y, uint8_t k, uint8_t k_from) {
            const int px = a.x + x, py = a.y + y;
            if (px < 0 || py < 0 || px >= sc.w || py >= sc.h) return;
            const size_t i = size_t(py) * sc.w + px;
            uint16_t p = colours[k ? k : pic_[i]];
            if (fading) p = mix(from_colours[k_from ? k_from : pic_from_[i]], p, fade_);
            c.rect(px * s - stage_.scene_x(), py * s - stage_.scene_y(), s, s, p);
        };
        if (a.fill && to && (*to)[f].count) {          // its box filled first (a screen's dark), then what is on it
            uint16_t p = colours[a.fill];
            if (fading) p = mix(from_colours[a.fill], p, fade_);
            for (int y = 0; y < a.h; y++)
                for (int x = 0; x < a.w; x++) {
                    const int px = a.x + x, py = a.y + y;
                    if (px >= 0 && py >= 0 && px < sc.w && py < sc.h)
                        c.rect(px * s - stage_.scene_x(), py * s - stage_.scene_y(), s, s, p);
                }
        }
        if (to)
            for (int n = 0; n < (*to)[f].count; n++) {
                const uint8_t* p = (*to)[f].px + n * AMBIENT_PIXEL_BYTES;
                put(p[0], p[1], p[2], from ? (*from)[f].at(p[0], p[1]) : 0);
            }
        if (from)
            for (int n = 0; n < (*from)[f].count; n++) {
                const uint8_t* p = (*from)[f].px + n * AMBIENT_PIXEL_BYTES;
                if (!to || !(*to)[f].at(p[0], p[1])) put(p[0], p[1], 0, p[2]);
            }
    }
}

void Pet::draw_plain_room(Canvas& c) const {
    const int w = stage_.width(), h = stage_.height(), u = unit();
    auto colour = [&](DayNight dn) { return night_ > 0 ? mix(dn.day, dn.night, night_) : dn.day; };
    const int floor_y = stage_.plain_floor_y();
    c.rect(0, 0, w, floor_y, colour(WALL));
    for (int y = 0; y < floor_y; y += WALL_STRIPE_EVERY * u) c.rect(0, y, w, u, colour(WALL_STRIPE));
    c.rect(0, floor_y, w, h - floor_y, colour(FLOOR));
    for (int y = floor_y + 3 * u, gap = 3; y < h; gap++, y += gap * u) c.rect(0, y, w, u, colour(FLOOR_SEAM));
    c.rect(0, floor_y, w, u, colour(SKIRTING));
}

// The target's moment: the scene's own picture for it (a net swishing) while it lasts, else, for a splash, ripples
// spreading where the item went in.
void Pet::draw_effect(Canvas& c, const Throw& thrown) const {
    const Scene& sc = stage_.scene();
    const int s = stage_.scene_scale(), w = stage_.width(), h = stage_.height();
    const std::optional<Pixel> target = stage_.target();
    const bool at_target =
        target && std::abs(thrown.effect_at.x - target->x) < 1 && std::abs(thrown.effect_at.y - target->y) < 1;
    if (stage_.has_scene() && sc.swish_day && at_target) {
        for (int y = 0; y < sc.swish_h; y++)
            for (int x = 0; x < sc.swish_w; x++) {
                const int i = y * sc.swish_w + x;
                const uint8_t k = sc.swish_day[i];
                const int sx = sc.swish_x + x * s, sy = sc.swish_y + y * s;
                if (!k || sx + s <= 0 || sx >= w || sy + s <= 0 || sy >= h) continue;
                uint16_t p = egg_->palette[k];
                if (night_ > 0) p = mix(p, at_night(*egg_, p, sc.swish_night, i), night_);
                c.rect(sx, sy, s, s, p);
            }
        return;
    }
    if (egg_->throw_kit.flight != Flight::Splash) return;
    const int u = sign_unit();                     // ripples, like signs, in the screen's own pixels
    const uint16_t ring = mix(WATER_LIGHT, WATER_LIGHT_NIGHT, night_);
    for (int k = 0; k < RIPPLES; k++) {
        const int tick = thrown.effect_tick - k * RIPPLE_GAP_TICKS;
        if (tick < 0) continue;
        const float grown = float(tick) / RIPPLE_TICKS;
        const float rx = (RIPPLE_FROM + grown * RIPPLE_GROWTH) * u;
        draw_ring(c, thrown.effect_at.x, thrown.effect_at.y, rx, rx * RIPPLE_FLATTEN, u, ring);
    }
}

void Pet::draw_workspot(Canvas& c, bool on, uint32_t now) const {
    const Scene& sc = stage_.scene();
    if (!stage_.has_scene() || !sc.work_on) {
        draw_plain_desk(c, on, now);
        return;
    }
    const uint8_t* picture = on ? sc.work_on : (sc.work_off ? sc.work_off : sc.work_on);
    const int s = stage_.scene_scale(), w = stage_.width(), h = stage_.height();
    Desk& d = desk_;
    if (d.picture != picture || d.on != on || d.night != night_ || d.w != w || d.h != h || d.s != s) {
        d = Desk{{}, picture, on, night_, w, h, s};
        const Box& screen = stage_.work_screen();
        for (int y = 0; y < sc.work_h; y++)
            for (int x = 0; x < sc.work_w; x++) {
                const int i = y * sc.work_w + x;
                const uint8_t k = picture[i];
                const int sx = sc.work_x + x * s, sy = sc.work_y + y * s;
                if (!k || sx + s <= 0 || sx >= w || sy + s <= 0 || sy >= h) continue;
                uint16_t p = egg_->palette[k];
                const bool glow = on && !screen.empty() && sx >= screen.x - s && sx <= screen.last_x() + s &&
                                  sy >= screen.y - s && sy <= screen.last_y() + s;
                if (night_ > 0 && glow) p = mix(p, dim(p), night_ * NIGHT_SCREEN_DIMMING);
                else if (night_ > 0) p = mix(p, at_night(*egg_, p, sc.work_night, i), night_);
                for (int yy = std::max(sy, 0); yy < std::min(sy + s, h); yy++)
                    for (int xx = std::max(sx, 0); xx < std::min(sx + s, w); xx++)
                        d.px.push_back({uint32_t(yy * w + xx), p});
            }
    }
    if (c.px.size() != size_t(w) * size_t(h)) return;
    for (const auto& [at, p] : d.px) c.px[at] = c.tinted(p);
}

// Sized to the pet: the tabletop at about half its height, the legs just behind its feet.
void Pet::draw_plain_desk(Canvas& c, bool on, uint32_t now) const {
    const Pixel home = stage_.plain_home();
    const int u = unit();
    auto block = [&](int x, int y, int w, int h, uint16_t colour) {
        c.rect(home.x + x * u, home.y + y * u, w * u, h * u, night_ > 0 ? mix(colour, dim(colour), night_) : colour);
    };
    const int top = stage_.plain_desk_top();
    const int legs = DESK_LEGS_END - (top + 5), half = PLAIN_DESK_HALF;
    block(-half, top - 1, 2 * half, 6, DESK_EDGE);
    block(1 - half, top, 2 * (half - 1), 4, DESK_TOP);
    block(2 - half, top + 5, 2, legs, DESK_EDGE);
    block(half - 4, top + 5, 2, legs, DESK_EDGE);
    block(-7, top - 12, 14, 11, MONITOR);
    block(-6, top - 11, 12, 8, !on ? MONITOR_OFF : (now / MONITOR_FLICKER_MS) % 2 ? MONITOR_ON : MONITOR_ON_BRIGHT);
    block(-1, top - 2, 2, 2, MONITOR);
}

// While the user types, text fills the work spot's screen: words in lines, a blinking cursor, then a fresh page.
// Dark ink on a light screen, light ink on a dark one.
void Pet::draw_typed_text(Canvas& c) const {
    const Box& screen = stage_.work_screen();
    if (screen.empty()) return;
    const int px = stage_.scene_scale(), w = stage_.width(), h = stage_.height();
    const int cols = std::max(1, (screen.w - 1) / px), rows = std::max(1, (screen.h - 1) / (2 * px));
    const int page = rows * cols, n = int(typed_) % (page + PAGE_PAUSE);
    const int sheet = int(typed_) / (page + PAGE_PAUSE);
    for (int i = 0; i < n && i < page; i++) {
        const int row = i / cols, col = i % cols;
        // Knuth's multiplicative hash of (page, line, word) decides which line ends are ragged.
        const uint32_t hash = uint32_t(sheet * 7919 + row * 977 + (col / WORD_LENGTH) * 131) * 2654435761u;
        const bool space = col % WORD_LENGTH == WORD_LENGTH - 1;
        const bool ragged_end = col > cols - 3 && (hash >> 7) % 2;
        if (space || ragged_end) continue;
        const int x = screen.x + col * px, y = screen.y + row * 2 * px;
        if (x < 0 || y < 0 || x >= w || y >= h) continue;
        const uint16_t under = c.px[size_t(y) * w + x];
        c.rect(x, y, px, px, mix(under, luma(under) > LIGHT_SCREEN_LUMA ? INK_DARK : INK_LIGHT, 0.6f));
    }
    const uint32_t blink = keys_ ? CURSOR_BLINK_MS : CURSOR_BLINK_WAITING_MS;
    if (n < page && (last_ms_ / blink) % 2) {
        const int x = screen.x + (n % cols) * px, y = screen.y + (n / cols) * 2 * px;
        if (x >= 0 && y >= 0 && x < w && y < h) c.rect(x, y, px, px, rgb(0xff, 0xff, 0xff));
    }
}

}  // namespace nestbox
