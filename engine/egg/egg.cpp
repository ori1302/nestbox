// An egg file: "NEGG", the length of its description (4 bytes, little-endian), the description (JSON), then the
// pictures and frames as palette indices, found by their offsets from the start of the file
// (https://nestbox.pet/docs/egg-format/).
#include "egg/egg.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "screen/canvas.h"
#include "util/json.h"

namespace nestbox {

const uint8_t* SceneLayout::picture(Daypart p, float& dim) const {
    dim = 0;
    const uint8_t* own = part[int(p)];
    if (own) return own;
    if (p == Daypart::Evening && part[int(Daypart::Night)]) return part[int(Daypart::Night)];
    if (p == Daypart::Night || p == Daypart::Evening) dim = p == Daypart::Night ? 1.0f : EVENING_DARKNESS;
    return day;
}

namespace {

constexpr char MAGIC[4] = {'N', 'E', 'G', 'G'};
constexpr size_t HEADER_BYTES = 8;           // MAGIC + description length
constexpr int MAX_SCALE = 4;
constexpr int MAX_COLOURS = 256;
constexpr int MAX_FEET = 512;                // also the limit for `tall`
constexpr double MIN_PACE = 20, MAX_PACE = 120;   // a pet's own walking speed, screen pixels per second
constexpr int MAX_SCENE_PX = 1024;           // scene and work spot pictures, per side
constexpr int MAX_SWISH_PX = 256;
constexpr int MAX_AMBIENT = 12;              // moving parts of a scene
constexpr int MAX_AMBIENT_PX = 256;
constexpr int MAX_AMBIENT_FRAMES = 64;
constexpr int MAX_FRAME_PX = 512;
constexpr int MAX_FRAMES = 256;
constexpr int MAX_FLOOR_POINTS = 256;
constexpr int MAX_BLOCKED = 16;                // polygons the pet may not stand on
constexpr size_t BOX_BYTES = 8;              // x, y, w, h as little-endian uint16

bool fail(std::string& error, const std::string& why) { error = why; return false; }

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
int le16(const uint8_t* p) { return int(p[0] | (p[1] << 8)); }

// Reads the parts of an egg its description points at, checking every offset and pixel against the file.
class EggReader {
public:
    EggReader(const std::vector<uint8_t>& bytes, uint32_t description_len, const Fields& m, std::string& error)
        : b_(bytes), len_(description_len), m_(m), error_(error) {}

    bool palette(Egg& out);
    bool scene(Scene& sc);
    bool anim(int index, Anim& a);

private:
    // A scene position, kept within a picture's reach so that scaling it to the screen cannot overflow.
    int position(const std::string& key, int def = 0) const {
        return std::clamp(m_.integer(key, def), -MAX_SCENE_PX, MAX_SCENE_PX);
    }
    // A block of `size` bytes at the offset stored under `key`; false (with an error) unless it lies in the file.
    bool block(const std::string& key, size_t size, bool required, const uint8_t*& at);
    // Rejects pixels past the palette, so drawing needs no bounds checks.
    bool indices(const uint8_t* at, size_t size, const std::string& what);
    bool picture(const std::string& key, size_t size, bool required, const uint8_t*& at, const std::string& what);
    bool workspot(Scene& sc);
    bool swish(Scene& sc);
    bool ambient(int index, Ambient& a);
    bool frame_boxes(const std::string& key, const std::string& what, int fw, int fh, int rows, int frames,
                     std::vector<FrameBox>& boxes);
    bool coded_frames(const std::string& key, const std::string& what, int fw, int fh, int rows, int frames,
                      std::vector<FrameBox>& boxes, const uint8_t* table);

    const std::vector<uint8_t>& b_;
    uint32_t len_;
    const Fields& m_;
    std::string& error_;
    int colours_ = 0;
};

bool EggReader::block(const std::string& key, size_t size, bool required, const uint8_t*& at) {
    at = nullptr;
    if (!m_.has(key)) return !required || fail(error_, key + " is missing");
    double off = m_.num(key, -1);                         // compared as a double so huge numbers fail too
    if (!(off >= double(HEADER_BYTES) + len_ && off + double(size) <= double(b_.size())))
        return fail(error_, key + " runs past the end of the file");
    at = b_.data() + size_t(off);
    return true;
}

bool EggReader::indices(const uint8_t* at, size_t size, const std::string& what) {
    for (size_t i = 0; at && i < size; i++)
        if (at[i] >= colours_) return fail(error_, what + " uses a colour the palette doesn't have");
    return true;
}

bool EggReader::picture(const std::string& key, size_t size, bool required, const uint8_t*& at,
                        const std::string& what) {
    return block(key, size, required, at) && indices(at, size, what);
}

bool EggReader::palette(Egg& out) {
    colours_ = m_.integer("palette.count");
    if (colours_ < 1 || colours_ > MAX_COLOURS) return fail(error_, "bad palette");
    const uint8_t* pal;
    if (!block("palette.at", size_t(colours_) * 2, true, pal)) return false;
    for (int i = 0; i < colours_; i++) out.palette.push_back(uint16_t(le16(pal + 2 * i)));
    return true;
}

bool EggReader::scene(Scene& sc) {
    sc.w = m_.integer("scene.w"); sc.h = m_.integer("scene.h");
    sc.scale = m_.integer("scene.scale", 1);
    if (sc.scale < 1 || sc.scale > MAX_SCALE) return fail(error_, "the scene's scale is not 1 to 4");
    if (sc.w <= 0 || sc.h <= 0 || sc.w > MAX_SCENE_PX || sc.h > MAX_SCENE_PX)
        return fail(error_, "the scene's size is wrong");
    const size_t size = size_t(sc.w) * sc.h;
    if (!picture("scene.pictures.day", size, true, sc.day, "the scene")) return false;
    for (Daypart p : DAYPARTS) {
        if (!picture(std::string("scene.pictures.") + daypart_key(p), size, false, sc.part[int(p)],
                     std::string("the scene's ") + daypart_key(p)))
            return false;
        sc.has_part[int(p)] = sc.part[int(p)] != nullptr;
    }
    for (int i = 0; i < MAX_FLOOR_POINTS; i++) {
        const std::string point = "scene.floor." + std::to_string(i) + ".";
        if (!m_.has(point + "0")) break;
        sc.floor.push_back({position(point + "0"), position(point + "1")});
    }
    for (int i = 0; i < MAX_FLOOR_POINTS; i++) {
        const std::string point = "scene.water." + std::to_string(i) + ".";
        if (!m_.has(point + "0")) break;
        sc.water.push_back({position(point + "0"), position(point + "1")});
    }
    if (!sc.water.empty() && sc.water.size() < 3) return fail(error_, "the scene's water needs at least 3 points");
    sc.water_depth = std::clamp(m_.integer("scene.water_depth", 0), 0, MAX_FEET);
    sc.water_shallow = std::clamp(m_.integer("scene.water_shallow", 0), 0, MAX_FEET);
    for (int k = 0; k < MAX_BLOCKED; k++) {
        const std::string poly = "scene.blocked." + std::to_string(k) + ".";
        if (!m_.has(poly + "0.0")) break;
        std::vector<std::pair<int, int>> hole;
        for (int i = 0; i < MAX_FLOOR_POINTS; i++) {
            const std::string point = poly + std::to_string(i) + ".";
            if (!m_.has(point + "0")) break;
            hole.push_back({position(point + "0"), position(point + "1")});
        }
        if (hole.size() < 3) return fail(error_, "a blocked part of the scene needs at least 3 points");
        sc.blocked.push_back(std::move(hole));
    }
    sc.home_x = position("scene.home.0", -1); sc.home_y = position("scene.home.1", -1);
    sc.target_x = position("scene.target.0", -1); sc.target_y = position("scene.target.1", -1);
    sc.watch_x = position("scene.watch.0", -1); sc.watch_y = position("scene.watch.1", -1);
    for (int i = 0; i < 4; i++) sc.keep[i] = position("scene.keep." + std::to_string(i), -1);
    if (m_.has("scene.gauges") && !corner_named(m_.str("scene.gauges"), sc.gauges))
        return fail(error_, "scene.gauges is not a corner such as \"top-right\"");
    if (m_.has_object("scene.workspot") && !workspot(sc)) return false;
    if (m_.has_object("scene.swish") && !swish(sc)) return false;
    for (int i = 0; m_.has_object("scene.ambient." + std::to_string(i)); i++) {
        if (i == MAX_AMBIENT)
            return fail(error_, "the scene has more than " + std::to_string(MAX_AMBIENT) + " ambient parts");
        Ambient a;
        if (!ambient(i, a)) return false;
        sc.ambient.push_back(std::move(a));
    }
    return true;
}

bool EggReader::ambient(int index, Ambient& a) {
    const std::string k = "scene.ambient." + std::to_string(index) + ".";
    a.name = m_.str(k + "name");
    a.during = m_.str(k + "during");
    const std::string cue = m_.str(k + "cue");
    if (cue == "loop") a.cue = Ambient::Cue::Loop;
    else if (cue == "once") a.cue = Ambient::Cue::Once;
    else if (!cue.empty()) return fail(error_, "an ambient part's cue is not loop or once");
    if (a.cue != Ambient::Cue::Always && a.name.empty()) return fail(error_, "a cued ambient part has no name");
    const int fill = m_.integer(k + "fill", 0);
    if (fill < 0 || fill >= colours_) return fail(error_, "an ambient part's fill is not one of the egg's colours");
    a.fill = uint8_t(fill);
    a.w = m_.integer(k + "w"); a.h = m_.integer(k + "h");
    a.x = position(k + "x"); a.y = position(k + "y");
    a.frames = m_.integer(k + "frames");
    if (a.w <= 0 || a.h <= 0 || a.w > MAX_AMBIENT_PX || a.h > MAX_AMBIENT_PX || a.frames <= 0 ||
        a.frames > MAX_AMBIENT_FRAMES)
        return fail(error_, "an ambient part of the scene has a wrong size");
    for (int f = 0; f < a.frames; f++)
        a.durations.push_back(uint8_t(std::clamp(m_.integer(k + "durations." + std::to_string(f), 1), 1, 255)));
    const std::string what = "an ambient part of the scene";
    auto pictures = [&](const std::string& key, std::vector<FrameBox>& boxes) {
        if (!m_.has(key) || frame_boxes(key, what, a.w, a.h, 1, a.frames, boxes)) return true;
        error_ += " (eggs packed before NestBox 0.5.0 store moving parts another way: pack it again)";
        return false;
    };
    if (!pictures(k + "pictures.day", a.day)) return false;
    bool any = !a.day.empty();
    for (Daypart p : DAYPARTS) {
        if (!pictures(k + "pictures." + daypart_key(p), a.part[int(p)])) return false;
        any = any || !a.part[int(p)].empty();
    }
    return any || fail(error_, "an ambient part of the scene has no pictures");
}

bool EggReader::workspot(Scene& sc) {
    sc.work_w = m_.integer("scene.workspot.w"); sc.work_h = m_.integer("scene.workspot.h");
    sc.work_x = position("scene.workspot.x"); sc.work_y = position("scene.workspot.y");
    if (sc.work_w <= 0 || sc.work_h <= 0 || sc.work_w > MAX_SCENE_PX || sc.work_h > MAX_SCENE_PX)
        return fail(error_, "the work spot's size is wrong");
    const size_t size = size_t(sc.work_w) * sc.work_h;
    if (!block("scene.workspot.pictures.on", size, true, sc.work_on) ||
        !block("scene.workspot.pictures.off", size, false, sc.work_off) ||
        !block("scene.workspot.pictures.night", size, false, sc.work_night))
        return false;
    return indices(sc.work_on, size, "the work spot") && indices(sc.work_off, size, "the work spot") &&
           indices(sc.work_night, size, "the work spot");
}

bool EggReader::swish(Scene& sc) {
    sc.swish_w = m_.integer("scene.swish.w"); sc.swish_h = m_.integer("scene.swish.h");
    sc.swish_x = position("scene.swish.x"); sc.swish_y = position("scene.swish.y");
    if (sc.swish_w <= 0 || sc.swish_h <= 0 || sc.swish_w > MAX_SWISH_PX || sc.swish_h > MAX_SWISH_PX)
        return fail(error_, "the swish picture's size is wrong");
    const size_t size = size_t(sc.swish_w) * sc.swish_h;
    return block("scene.swish.pictures.day", size, true, sc.swish_day) &&
           block("scene.swish.pictures.night", size, false, sc.swish_night) &&
           indices(sc.swish_day, size, "the swish picture") && indices(sc.swish_night, size, "the swish picture");
}

bool EggReader::anim(int index, Anim& a) {
    const std::string k = "anims." + std::to_string(index) + ".";
    a.name = m_.str(k + "name");
    a.fw = m_.integer(k + "w"); a.fh = m_.integer(k + "h");
    a.rows = m_.integer(k + "rows", 1); a.frames = m_.integer(k + "frames");
    a.rush = m_.integer(k + "rush", -1); a.hit = m_.integer(k + "hit", -1); a.ret = m_.integer(k + "return", -1);
    a.dash = m_.num(k + "dash", 0) != 0;
    if (a.fw <= 0 || a.fh <= 0 || a.fw > MAX_FRAME_PX || a.fh > MAX_FRAME_PX || a.frames <= 0 ||
        a.frames > MAX_FRAMES || (a.rows != 1 && a.rows != FACING_ROWS))
        return fail(error_, "animation " + a.name + " has a wrong size");
    auto marker = [&](int v) { return v == -1 || (v >= 0 && v < a.frames); };
    if (!marker(a.rush) || !marker(a.hit) || !marker(a.ret) || (a.rush >= 0 && a.ret >= 0 && a.rush > a.ret))
        return fail(error_, "animation " + a.name + " marks frames it doesn't have");
    for (int f = 0; f < a.frames; f++)                    // a frame of zero ticks would never show
        a.durations.push_back(uint8_t(std::clamp(m_.integer(k + "durations." + std::to_string(f), 1), 1, 255)));
    return frame_boxes(k + "at", "animation " + a.name, a.fw, a.fh, a.rows, a.frames, a.boxes);
}

// A table of boxes (BOX_BYTES each), one per frame row by row, then the frames' run codes (coded_frames).
bool EggReader::frame_boxes(const std::string& key, const std::string& what, int fw, int fh, int rows, int frames,
                            std::vector<FrameBox>& boxes) {
    const size_t n = size_t(rows) * frames, table_bytes = n * BOX_BYTES;
    boxes.assign(n, FrameBox());
    const uint8_t* table;
    if (!block(key, table_bytes, true, table)) return false;
    for (size_t i = 0; i < n; i++) {
        const uint8_t* e = table + i * BOX_BYTES;
        FrameBox& bx = boxes[i];
        bx.x = le16(e); bx.y = le16(e + 2); bx.w = le16(e + 4); bx.h = le16(e + 6);
        if (bx.x + bx.w > fw || bx.y + bx.h > fh) return fail(error_, what + " has a frame outside its size");
    }
    return coded_frames(key, what, fw, fh, rows, frames, boxes, table);
}

// After the boxes, each frame's offset in the block (u32), then the run codes; every frame is decoded once here, so
// drawing can trust them.
bool EggReader::coded_frames(const std::string& key, const std::string& what, int fw, int fh, int rows, int frames,
                             std::vector<FrameBox>& boxes, const uint8_t* table) {
    const size_t n = boxes.size(), head = n * (BOX_BYTES + 4);
    const uint8_t* all;
    if (!block(key, head, true, all)) return false;
    const uint8_t* end = b_.data() + b_.size();
    std::vector<uint8_t> px, whole(size_t(fw) * fh);
    for (int r = 0; r < rows; r++)
        for (int f = 0; f < frames; f++) {
            const size_t i = size_t(r) * frames + f;
            FrameBox& bx = boxes[i];
            bx.fw = fw;
            bx.fh = fh;
            if (f) bx.first = &boxes[size_t(r) * frames];
            else std::fill(whole.begin(), whole.end(), 0);
            if (!bx.w || !bx.h) continue;
            const size_t off = le32(table + n * BOX_BYTES + i * 4);
            if (off < head || off >= size_t(end - all)) return fail(error_, what + " has a frame past the file");
            bx.code = all + off;
            px.resize(size_t(bx.w) * bx.h);
            if (!decode_runs(bx.code, end, px.data(), px.size(), f ? whole.data() : nullptr, fw, bx.x, bx.y, bx.w,
                             colours_))
                return fail(error_, what + " has a frame whose codes are wrong");
            if (f) continue;
            for (int y = 0; y < bx.h; y++)
                std::copy_n(&px[size_t(y) * bx.w], bx.w, &whole[size_t(bx.y + y) * fw + bx.x]);
        }
    return true;
}

bool read_screen_palette(const Fields& m, Egg& out, std::string& error) {
    std::vector<uint16_t> colours;
    for (int i = 0; m.has("screen_palette." + std::to_string(i)); i++) {
        const std::string s = m.str("screen_palette." + std::to_string(i));
        unsigned r, g, b;
        if (s.size() != 7 || s[0] != '#' || std::sscanf(s.c_str() + 1, "%2x%2x%2x", &r, &g, &b) != 3)
            return fail(error, "screen_palette entry \"" + s + "\" is not a #rrggbb colour");
        colours.push_back(rgb(uint8_t(r), uint8_t(g), uint8_t(b)));
    }
    if (!colours.empty() && colours.size() < 2) return fail(error, "screen_palette needs at least 2 colours");
    set_screen_palette(out, colours);
    return true;
}

void read_words(const Fields& m, Egg& out) {
    out.name = m.str("name");
    out.theme = m.str("theme");
    out.personality = m.str("personality");
    out.style = m.str("style");
    out.about = m.str("about");
    out.color = m.str("color");
    out.credit = m.str("credit");
    out.license = m.str("license");
    for (auto& [k, v] : m.under("notes").values) out.notes.push_back({k, v});
}

bool read_throw(const Fields& m, Egg& out, std::string& error) {
    if (!m.has_object("throw")) return true;
    ThrowKit& kit = out.throw_kit;
    kit.anim = m.str("throw.anim");
    kit.item = m.str("throw.item");
    kit.catch_anim = m.str("throw.catch");
    kit.typing_fast = m.str("throw.typing_fast", "true") != "false";
    kit.plays = m.str("throw.plays", "false") == "true";
    kit.catch_height = float(m.num_in("throw.catch_height", 0, 1, 0.25));
    if (m.has("throw.release.0")) {
        kit.release_x = m.integer("throw.release.0", -1);
        kit.release_y = m.integer("throw.release.1", -1);
        const Anim* anim = out.find(kit.anim);
        if (!anim || anim->rows != 1 || kit.release_x < 0 || kit.release_y < 0 || kit.release_x >= anim->fw ||
            kit.release_y >= anim->fh)
            return fail(error, "the throw's release is not a point in the frame of a 1-row throw");
    }
    const std::string flight = m.str("throw.flight", "hoop");
    if (flight == "hoop") kit.flight = Flight::Hoop;
    else if (flight == "splash") kit.flight = Flight::Splash;
    else if (flight == "toss") kit.flight = Flight::Toss;
    else return fail(error, "the throw's flight is not hoop, splash or toss");
    out.has_throw = true;
    return true;
}

// "free_time": [{"name", "hours": [from, to], "steps": [step, ...]}, ...]; a step that is a bare word is {"do": word}.
bool read_pastimes(const Fields& m, Egg& out, std::string& error) {
    for (size_t i = 0; m.has_object("free_time." + std::to_string(i)); i++) {
        if (i == Pastime::MAX)
            return fail(error, "free_time has more than " + std::to_string(Pastime::MAX) + " pastimes");
        const std::string k = "free_time." + std::to_string(i);
        Pastime p;
        p.name = m.str(k + ".name");
        if (m.has(k + ".hours.0")) {
            p.from_hour = m.integer(k + ".hours.0", -1);
            p.to_hour = m.integer(k + ".hours.1", -1);
            if (p.from_hour < 0 || p.from_hour > 24 || p.to_hour < 0 || p.to_hour > 24)
                return fail(error, "a pastime's hours are not 0 to 24");
        }
        for (size_t j = 0;; j++) {
            const std::string s = k + ".steps." + std::to_string(j);
            if (m.has_object(s)) p.steps.push_back(m.under(s));
            else if (m.has(s)) {
                Fields word;
                word.set("do", m.str(s));
                p.steps.push_back(word);
            } else {
                break;
            }
            if (p.steps.size() > Pastime::MAX_STEPS)
                return fail(error, "a pastime has more than " + std::to_string(Pastime::MAX_STEPS) + " steps");
        }
        if (p.steps.empty()) return fail(error, "a pastime has no steps");
        out.pastimes.push_back(std::move(p));
    }
    return true;
}

}  // namespace

void set_screen_palette(Egg& egg, const std::vector<uint16_t>& colours) {
    egg.screen_palette = colours;
    egg.screen_lut.clear();
    if (colours.empty()) return;
    auto lum = [](uint32_t c) { return 299 * ((c >> 16) & 255) + 587 * ((c >> 8) & 255) + 114 * (c & 255); };
    egg.screen_lut.resize(4096);
    for (int i = 0; i < 4096; i++) {
        const uint32_t c = (uint32_t((i >> 8) * 17) << 16) | (uint32_t(((i >> 4) & 15) * 17) << 8) | ((i & 15) * 17);
        uint16_t best = colours[0];
        int64_t best_d = -1;
        for (uint16_t p : colours) {
            const int64_t d = std::abs(int64_t(lum(rgb888(p))) - int64_t(lum(c)));
            if (best_d < 0 || d < best_d) best = p, best_d = d;
        }
        egg.screen_lut[size_t(i)] = best;
    }
    for (uint16_t p : colours) egg.screen_lut[rgb444(p)] = p;     // its own colours stay exactly as they are
    // Every picture of the egg indexes its one colour table: snapped once here, its art is in the palette too.
    for (size_t i = 1; i < egg.palette.size(); i++) egg.palette[i] = egg.screen_lut[rgb444(egg.palette[i])];
}

namespace {

Portrait portrait_of(const Egg& egg) {
    Portrait p;
    const Anim* a = egg.standing_anim();
    if (!a || a->boxes.empty()) return p;
    FrameCache cache;
    const FrameBox& b = pixels_of(a->box(0, 0), cache);
    int x0 = b.w, y0 = b.h, x1 = -1, y1 = -1;
    for (int y = 0; y < b.h; y++)
        for (int x = 0; x < b.w; x++)
            if (b.px[y * b.w + x]) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 < 0) return p;
    p.w = x1 - x0 + 1;
    p.h = y1 - y0 + 1;
    p.px.reserve(size_t(p.w) * p.h);
    for (int y = y0; y <= y1; y++) p.px.insert(p.px.end(), b.px + y * b.w + x0, b.px + y * b.w + x1 + 1);
    return p;
}

}  // namespace

int idle_reach(const Egg& egg) {
    if (egg.idle_reach >= 0 || egg.slim) return std::max(egg.idle_reach, 0);
    const Anim* a = egg.find("Idle");
    if (!a) return 0;
    int reach = 0;
    FrameCache cache;
    for (int r = 0; r < a->rows; r++)
        for (int f = 0; f < a->frames; f++) {
            const FrameBox& b = pixels_of(a->box(r, f), cache);
            for (int i = 0; i < b.w * b.h; i++)
                if (b.px[i]) reach = std::max(reach, std::abs(2 * (b.x + i % b.w) + 1 - a->fw));
        }
    return reach;
}

void slim_egg(Egg& egg) {
    if (egg.slim) return;
    egg.idle_reach = idle_reach(egg);         // measured from its frames, which are going
    Scene& sc = egg.scene;
    std::vector<uint8_t> kept;
    const size_t work = size_t(sc.work_w) * sc.work_h;
    const uint8_t** spot[] = {&sc.work_on, &sc.work_off, &sc.work_night};
    size_t at[3] = {};
    for (int i = 0; i < 3; i++)
        if (*spot[i]) {
            at[i] = kept.size();
            kept.insert(kept.end(), *spot[i], *spot[i] + work);
        }
    egg.bytes = std::move(kept);     // the file's buffer is freed here
    for (int i = 0; i < 3; i++)
        if (*spot[i]) *spot[i] = egg.bytes.data() + at[i];
    sc.day = nullptr;
    for (const uint8_t*& p : sc.part) p = nullptr;
    sc.swish_day = sc.swish_night = nullptr;
    for (Ambient& a : sc.ambient) {
        std::vector<FrameBox>().swap(a.day);
        for (auto& part : a.part) std::vector<FrameBox>().swap(part);
    }
    for (Anim& a : egg.anims)
        for (FrameBox& b : a.boxes) {
            b.px = b.code = nullptr;
            b.w = b.h = 0;
        }
    egg.slim = true;
}

bool load_egg(std::vector<uint8_t> bytes, Egg& out, std::string& error) {
    out = Egg{};
    out.bytes = std::move(bytes);
    out.file_bytes = out.bytes.size();
    const std::vector<uint8_t>& b = out.bytes;
    if (b.size() < HEADER_BYTES ||
        std::memcmp(b.data(), MAGIC, sizeof MAGIC) != 0)
        return fail(error, "not a NestBox egg (no NEGG header)");
    const uint32_t len = le32(b.data() + 4);
    if (HEADER_BYTES + size_t(len) > b.size())
        return fail(error, "the egg's description runs past the end of the file");
    Fields m;
    if (!parse_json(std::string(b.begin() + HEADER_BYTES, b.begin() + HEADER_BYTES + len), m))
        return fail(error, "the egg's description is not valid JSON");

    read_words(m, out);
    out.scale = m.integer("scale", 0);
    if (out.scale < 0 || out.scale > MAX_SCALE) return fail(error, "the egg's scale is not 0 (the screen's own) to 4");
    out.pace = m.num("pace", 0) > 0 ? float(m.num_in("pace", MIN_PACE, MAX_PACE, 0)) : 0;
    out.feet = m.integer("feet");
    out.tall = m.integer("tall");

    EggReader r(b, len, m, error);
    if (!r.palette(out)) return false;
    if (!read_screen_palette(m, out, error)) return false;   // after the colour table, which it snaps
    if (out.feet < 0 || out.feet > MAX_FEET || out.tall < 0 || out.tall > MAX_FEET)
        return fail(error, "the egg's feet or height is wrong");
    if (m.has_object("scene")) {
        if (!r.scene(out.scene)) return false;
        out.has_scene = true;
    }
    for (int i = 0; m.has("anims." + std::to_string(i) + ".name"); i++) {
        Anim a;
        if (!r.anim(i, a)) return false;
        out.anims.push_back(std::move(a));
    }
    if (!out.find("Idle")) return fail(error, "the egg has no Idle animation");
    out.idle_reach = idle_reach(out);          // measured once, here, so bringing the pet out costs nothing more
    out.portrait = portrait_of(out);
    return read_throw(m, out, error) && read_pastimes(m, out, error);
}

}  // namespace nestbox
