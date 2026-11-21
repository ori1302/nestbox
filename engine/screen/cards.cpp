#include "screen/cards.h"
#include <algorithm>
#include <string>
#include "util/qr.h"

namespace nestbox {
namespace cards {

namespace {

constexpr int SHADOW_H = 4;              // under the pet's feet
constexpr uint16_t SHADOW = rgb(0x0c, 0x0e, 0x11);

// The pet's ink (every frame's opaque pixels together, so it never jumps) and the scale it is drawn at.
struct PetLook {
    int x0 = 0, y0 = 0, w = 0, h = 0, scale = 0;   // w, h in screen pixels, shadow included
};

Box ink_of(const PetPicture& pet) {
    int x0 = pet.fw, y0 = pet.fh, x1 = -1, y1 = -1;
    FrameCache cache;
    for (int f = 0; f < pet.frames; f++) {
        const FrameBox& b = pixels_of(pet.boxes[f], cache);
        for (int y = 0; y < b.h; y++)
            for (int x = 0; x < b.w; x++)
                if (b.px[y * b.w + x]) {
                    x0 = std::min(x0, b.x + x); y0 = std::min(y0, b.y + y);
                    x1 = std::max(x1, b.x + x); y1 = std::max(y1, b.y + y);
                }
    }
    return x1 < 0 ? Box{} : Box{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

// The largest scale up to the pet's own (or `prefer`), capped at `cap`, that fits max_w x max_h.
PetLook fit_pet(const PetPicture& pet, const Box& ink, int max_w, int max_h, int prefer, int cap) {
    PetLook p;
    if (ink.w == 0) return p;
    const int best = std::min(cap, pet.scale ? pet.scale : prefer);
    for (int s = best; s >= 1; s--)
        if (ink.w * s <= max_w && ink.h * s + SHADOW_H <= max_h) { p.scale = s; break; }
    p.x0 = ink.x; p.y0 = ink.y; p.w = ink.w * p.scale; p.h = p.scale ? ink.h * p.scale + SHADOW_H : 0;
    return p;
}

void draw_pet(Canvas& c, Layout* l, const PetPicture& pet, const PetLook& p, int cx, int top, uint32_t now) {
    if (!p.scale) return;
    int left = cx - p.w / 2, feet = top + p.h - SHADOW_H;
    int sw = p.w * 3 / 4;
    c.rect(cx - sw / 2 + 4, feet - 1, sw - 8, 5, SHADOW);
    c.rect(cx - sw / 2, feet, sw, 3, SHADOW);
    static FrameCache cache;                                 // kept: a card draws its pet every frame
    const FrameBox& frame = pixels_of(pet.boxes[frame_at(pet.durations, pet.frames, ms_to_ticks(now))], cache);
    c.sprite(frame, pet.palette, left - p.x0 * p.scale, top - p.y0 * p.scale, p.scale);
    if (l) l->add("pet", {left, top, p.w, p.h});
}

constexpr int COLUMN_MARGIN = 10;
constexpr int COLUMN_PAD_Y = 14;         // kept free above and below when sizing the pet
constexpr int PET_MAX_H = 110;
constexpr int NO_CAP = 99;
constexpr int BAR_H = 8, BAR_INSET = 40;
constexpr uint16_t BAR_TRACK = rgb(0x2c, 0x31, 0x38);
// Widths depend on where parts land (a round screen is narrower near its edges) and positions depend on heights,
// so the column is laid out a few times until it settles; then the pet shrinks if the text still does not fit.
constexpr int SETTLE_ROUNDS = 3, SHRINK_ATTEMPTS = 6;

struct Column {
    std::vector<int> xs, ws, hs;
    PetLook look;
    int top = 0;
};

int text_h(const Piece& p, int w) { return text_height({0, 0, w, p.max_h}, p.text, p.max_scale, p.min_scale); }

int gap_below(const Piece& p, int h) { return p.kind == Piece::Pet && !h ? 0 : p.gap; }

void settle(const Display& d, const std::vector<Piece>& parts, const PetPicture& pet, const Box& ink, int cap,
            Column& col) {
    const Box safe = d.safe(COLUMN_MARGIN);
    const size_t n = parts.size();
    for (int round = 0; round < SETTLE_ROUNDS; round++) {
        int rest = d.h - 2 * COLUMN_PAD_Y, total = 0;
        for (size_t i = 0; i < n; i++)
            if (parts[i].kind == Piece::Text) rest -= text_h(parts[i], col.ws[i]) + parts[i].gap;
            else if (parts[i].kind == Piece::Bar) rest -= BAR_H + parts[i].gap;
        for (size_t i = 0; i < n; i++) {
            const Piece& p = parts[i];
            if (p.kind == Piece::Pet) col.look = fit_pet(pet, ink, col.ws[i], std::min(rest, PET_MAX_H), d.scale, cap);
            col.hs[i] = p.kind == Piece::Text ? text_h(p, col.ws[i]) : p.kind == Piece::Bar ? BAR_H : col.look.h;
            total += col.hs[i] + gap_below(p, col.hs[i]);
        }
        if (n) total -= gap_below(parts[n - 1], col.hs[n - 1]);
        col.top = (d.h - total) / 2;
        int y = col.top;
        for (size_t i = 0; i < n; i++) {
            int x0, x1;
            if (d.span(y, y + std::max(col.hs[i], 1), COLUMN_MARGIN, x0, x1)) { col.xs[i] = x0; col.ws[i] = x1 - x0; }
            else { col.xs[i] = safe.x; col.ws[i] = safe.w; }
            y += col.hs[i] + gap_below(parts[i], col.hs[i]);
        }
    }
}

bool text_fits_where_placed(const std::vector<Piece>& parts, const Column& col) {
    for (size_t i = 0; i < parts.size(); i++)
        if (parts[i].kind == Piece::Text && !text_fits({0, 0, col.ws[i], std::max(col.hs[i], 1)}, parts[i].text,
                                                      parts[i].max_scale, parts[i].min_scale))
            return false;
    return true;
}

void draw_column(Canvas& c, Layout* l, const Display& d, const std::vector<Piece>& parts, const PetPicture& pet,
                 const Column& col, uint32_t now, float progress) {
    int y = col.top;
    for (size_t i = 0; i < parts.size(); i++) {
        const Piece& p = parts[i];
        if (p.kind == Piece::Text) {
            draw_text(c, l, {col.xs[i], y, col.ws[i], col.hs[i]}, p.text, p.color, p.max_scale, p.min_scale,
                      Align::Center);
        } else if (p.kind == Piece::Pet) {
            draw_pet(c, l, pet, col.look, d.w / 2, y, now);
        } else {
            Box bar{col.xs[i] + BAR_INSET, y, col.ws[i] - 2 * BAR_INSET, BAR_H};
            c.rect(bar.x, bar.y, bar.w, bar.h, BAR_TRACK);
            c.rect(bar.x, bar.y, int(bar.w * std::min(1.f, progress)), bar.h, ACCENT);
            if (l) l->add("progress", bar);
        }
        y += col.hs[i] + gap_below(p, col.hs[i]);
    }
}

struct Placing {
    Box area;
    std::string text;
    uint16_t color;
    int max_scale;
    Align align;
    bool whole;
};

enum class Arrangement { Side, Stacked };

constexpr int QR_MARGIN = 8;
constexpr int QR_GAP = 8;                // between the code and the lines beside or under it
constexpr int BELOW_GAP = 10;
constexpr int MIN_SIDE_W = 40;
constexpr int TITLE_MAX_H = 60, BELOW_MAX_H = 24, STACKED_MAX_H = 30, STACKED_PROBE_H = 15;
constexpr int CLEARANCE = 6;             // from the edge of the visible area
constexpr int SPACING = 3;               // between any two things drawn

class QrCardPlanner {
public:
    QrCardPlanner(const Display& d, const Qr& q, const Card& k) : d_(d), q_(q), k_(k), safe_(d.safe(QR_MARGIN)) {}

    // Lays the card out in `out` and `qr`; false if anything would be hidden, touching or cut.
    bool plan(Arrangement a, int module, const std::string& title, std::vector<Placing>& out, Box& qr) const {
        out.clear();
        const int box = qr_box(q_, module);
        const bool side = a == Arrangement::Side;
        int th = height(safe_.w, TITLE_MAX_H, title, k_.title_scale);
        const int qx = side ? QR_MARGIN : (d_.w - box) / 2;
        int y = (d_.h - total_height(side, th, box)) / 2, tx, tw;
        if (!span(y, th, tx, tw)) return false;
        th = height(tw, TITLE_MAX_H, title, k_.title_scale);
        out.push_back({{tx, y, tw, th}, title, k_.title_color, k_.title_scale, Align::Center, false});
        y += th + k_.title_gap;
        qr = {qx, y, box, box};
        if (side ? !place_side(qr, out) : !place_stacked(qr, out)) return false;
        return clear(out, qr);
    }

private:
    int height(int w, int h, const std::string& t, int max_scale) const {
        return text_height({0, 0, w, h}, t, max_scale, 2);
    }

    bool span(int y0, int h, int& x0, int& w) const {
        int a, b;
        if (!d_.span(y0, y0 + std::max(h, 1), QR_MARGIN, a, b)) return false;
        x0 = a; w = b - a;
        return true;
    }

    // The card's height as first estimated at full width, to centre it.
    int total_height(bool side, int title_h, int box) const {
        int lines = 0;
        if (side) {
            for (auto& b : k_.below) lines += height(safe_.w, BELOW_MAX_H, b.text, b.scale) + b.gap;
            return title_h + k_.title_gap + box + (k_.below.empty() ? 0 : BELOW_GAP + lines - k_.below.back().gap);
        }
        for (auto& s : k_.stacked) lines += height(safe_.w, STACKED_MAX_H, s.text, s.scale) + s.gap;
        return title_h + k_.title_gap + box + QR_GAP + lines - (k_.stacked.empty() ? 0 : k_.stacked.back().gap);
    }

    // A whole line's largest size that keeps it on one line `w` wide (never below 2); other lines keep theirs.
    static int line_scale(const Line& s, int w) {
        int scale = s.scale;
        while (s.whole && scale > 2 && text_px(s.text, scale) > w) scale--;
        return scale;
    }

    bool place_side(const Box& qr, std::vector<Placing>& out) const {
        const Box col{qr.x + qr.w + QR_GAP, qr.y, d_.w - QR_MARGIN - (qr.x + qr.w + QR_GAP), qr.h};
        if (col.w < MIN_SIDE_W) return false;
        int h = 0;
        for (auto& s : k_.side) h += height(col.w, col.h, s.text, line_scale(s, col.w)) + s.gap;
        if (!k_.side.empty()) h -= k_.side.back().gap;
        int cy = col.y + std::max(0, (col.h - h) / 2);
        for (auto& s : k_.side) {
            const int scale = line_scale(s, col.w);
            int sh = height(col.w, col.y + col.h - cy, s.text, scale);
            out.push_back({{col.x, cy, col.w, sh}, s.text, s.color, scale, Align::Left, s.whole});
            cy += sh + s.gap;
        }
        int y = qr.y + qr.h + BELOW_GAP;
        for (auto& b : k_.below) {
            int bx, bw;
            if (!span(y, BELOW_MAX_H, bx, bw)) return false;
            const int scale = line_scale(b, bw);
            int bh = height(bw, BELOW_MAX_H, b.text, scale);
            out.push_back({{bx, y, bw, bh}, b.text, b.color, scale, Align::Center, b.whole});
            y += bh + b.gap;
        }
        return true;
    }

    bool place_stacked(const Box& qr, std::vector<Placing>& out) const {
        int y = qr.y + qr.h + QR_GAP;
        for (auto& s : k_.stacked) {
            int sx, sw;
            if (!span(y, STACKED_PROBE_H, sx, sw)) return false;
            const int scale = line_scale(s, sw);
            int sh = height(sw, STACKED_MAX_H, s.text, scale);
            out.push_back({{sx, y, sw, sh}, s.text, s.color, scale, Align::Center, s.whole});
            y += sh + s.gap;
        }
        return true;
    }

    // Everything visible, nothing touching, no text cut.
    bool clear(const std::vector<Placing>& out, const Box& qr) const {
        if (!d_.visible(qr, CLEARANCE)) return false;
        for (size_t i = 0; i < out.size(); i++) {
            const Placing& a = out[i];
            if (!d_.visible(a.area, CLEARANCE) || !text_fits(a.area, a.text, a.max_scale, 2)) return false;
            if (a.whole && text_px(a.text, 2) > a.area.w) return false;
            const Box grown{a.area.x - SPACING, a.area.y - SPACING, a.area.w + 2 * SPACING, a.area.h + 2 * SPACING};
            if (grown.overlaps(qr)) return false;
            for (size_t j = i + 1; j < out.size(); j++)
                if (grown.overlaps(out[j].area)) return false;
        }
        return true;
    }

    const Display& d_;
    const Qr& q_;
    const Card& k_;
    const Box safe_;
};

void drop_optional(std::vector<Line>& lines) {
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const Line& x) { return x.optional; }), lines.end());
}

}  // namespace

Piece text_piece(const std::string& t, uint16_t color, int max_scale, int gap, int max_h) {
    Piece p;
    p.text = t; p.color = color; p.max_scale = max_scale; p.gap = gap; p.max_h = max_h;
    return p;
}

Piece pet_piece(int gap) {
    Piece p;
    p.kind = Piece::Pet; p.gap = gap;
    return p;
}

void stack(Canvas& c, Layout* l, const Display& d, const std::vector<Piece>& parts, const PetPicture& pet, uint32_t now,
           float progress) {
    const Box safe = d.safe(COLUMN_MARGIN), ink = ink_of(pet);
    const size_t n = parts.size();
    Column col{std::vector<int>(n, safe.x), std::vector<int>(n, safe.w), std::vector<int>(n, 0), {}, 0};
    for (int cap = NO_CAP, attempt = 0; attempt < SHRINK_ATTEMPTS; attempt++) {
        settle(d, parts, pet, ink, cap, col);
        if (text_fits_where_placed(parts, col) || !col.look.scale) break;
        cap = col.look.scale - 1;                          // 0: no room for the pet at all
        std::fill(col.xs.begin(), col.xs.end(), safe.x);
        std::fill(col.ws.begin(), col.ws.end(), safe.w);
    }
    draw_column(c, l, d, parts, pet, col, now, progress);
}

namespace {
// The code for `text`, encoded once: encoding takes most of a frame on the device, and a card shows the same code
// frame after frame. nullptr if it doesn't encode.
const Qr* encoded(const std::string& text) {
    static std::string last;
    static Qr q;
    static bool ok = false;
    if (q.size == 0 || text != last) {
        last = text;
        ok = qr_encode(text, q);
    }
    return ok ? &q : nullptr;
}

// Where a card's parts go on one screen. Finding it tries many arrangements, which costs more than drawing.
struct CardPlan {
    std::vector<Placing> placed;
    Box qr;
    int module = 3;
    bool ok = false;
};

// Everything the plan depends on, as one string.
std::string plan_key(const Display& d, const Card& k) {
    std::string key = d.id + '\x1f' + std::to_string(d.w) + 'x' + std::to_string(d.h);
    const auto add = [&key](const std::string& s) { key += '\x1f'; key += s; };
    const auto num = [&add](long v) { add(std::to_string(v)); };
    add(k.title); add(k.short_title); num(k.title_color); num(k.title_scale); num(k.title_gap); add(k.qr);
    for (const auto* lines : {&k.side, &k.below, &k.stacked}) {
        add("|");
        for (const Line& n : *lines) {
            add(n.text); num(n.color); num(n.scale); num(n.gap); num(n.whole); num(n.optional);
        }
    }
    return key;
}

CardPlan plan_card(const Display& d, const Qr& q, const Card& card) {
    Card k = card;
    const QrCardPlanner planner(d, q, k);
    // The arrangements that read best first: beside the code, then under it, with smaller modules last.
    const std::pair<Arrangement, int> order[] = {{Arrangement::Side, 5},    {Arrangement::Side, 4},
                                                 {Arrangement::Stacked, 5}, {Arrangement::Stacked, 4},
                                                 {Arrangement::Side, 3},    {Arrangement::Stacked, 3}};
    CardPlan p;
    for (bool drop : {false, true}) {
        if (drop) { drop_optional(k.side); drop_optional(k.below); drop_optional(k.stacked); }
        for (const std::string* title : {&k.title, &k.short_title}) {
            for (auto [arrangement, m] : order)
                if (!title->empty() && planner.plan(arrangement, m, *title, p.placed, p.qr)) {
                    p.module = m;
                    p.ok = true;
                    break;
                }
            if (p.ok) break;
        }
        if (p.ok) break;
    }
    if (!p.ok) planner.plan(Arrangement::Stacked, 3, k.short_title.empty() ? k.title : k.short_title, p.placed, p.qr);
    return p;
}

// The plan for this card on this screen, kept while the card shows.
const CardPlan& planned(const Display& d, const Qr& q, const Card& card) {
    static std::string last;
    static CardPlan p;
    std::string key = plan_key(d, card);
    if (key != last) {
        p = plan_card(d, q, card);
        last = std::move(key);
    }
    return p;
}
}  // namespace

bool qr_card(Canvas& c, Layout* l, const Display& d, const Card& card) {
    const Qr* code = encoded(card.qr);
    if (!code) return false;
    const CardPlan& p = planned(d, *code, card);
    if (!p.ok && l) l->add("qr card: no arrangement fits this screen", {d.w / 2, d.h / 2, 1, 1}, false);
    draw_qr(c, l, *code, p.qr.x, p.qr.y, p.module);
    for (auto& t : p.placed) draw_text(c, l, t.area, t.text, t.color, t.max_scale, 2, t.align);
    return p.ok;
}

}  // namespace cards
}  // namespace nestbox
