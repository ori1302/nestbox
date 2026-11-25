// The heads-up rows and rings on every screen profile. Captions are status-bar rows: drawn exactly like a status
// line, stacked under the bar or mirrored at the bottom, scrolling when long. Progress rings sit side by side in
// the corner the scene names: under the bar and a top caption, or above a bottom caption, every pixel 5 px inside
// the visible area (on a round screen, the circle); on real frames they are drawn in the sender's colour, change
// nothing outside their own boxes, and leave no trace once removed. With burn-in care they rest while unchanged.
// Usage: hud_test EGG...
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "body/shelf.h"
#include "check.h"
#include "door/api.h"
#include "mind/brain.h"
#include "screen/font.h"
#include "screen/hud.h"

using namespace nestbox;
using test::check;

namespace {

constexpr int EDGE_MARGIN = 5;  // px from the visible edge
constexpr int GAP = 3;          // px between rings, and below the status bar
constexpr uint32_t FRAME_MS = 16;
constexpr int SETTLE_FRAMES = 120;
const char* const HOST = "nestbox-test.local";

std::string box_text(const Box& b) {
    return std::to_string(b.x) + "," + std::to_string(b.y) + " " + std::to_string(b.w) + "x" + std::to_string(b.h);
}

// Every pixel of the ring's disc, outline included, is visible.
bool disc_visible(const Display& d, const Box& b) {
    const int r = b.w / 2, cx = b.x + r, cy = b.y + r;
    for (int y = b.y; y <= b.last_y(); y++)
        for (int x = b.x; x <= b.last_x(); x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r && !d.visible(x, y, EDGE_MARGIN)) return false;
    return true;
}

void check_ring_layout(const Display& d, const std::vector<MeterRing>& rings, int below, const std::string& name,
                       Corner corner = Corner::TopRight, int above = -1) {
    const std::vector<Box> boxes = meter_boxes(d, rings, below, corner, above);
    const bool left = corner == Corner::TopLeft || corner == Corner::BottomLeft;
    const bool low = corner == Corner::BottomLeft || corner == Corner::BottomRight;
    const int floor = above < 0 ? d.h : above;
    for (size_t i = 0; i < boxes.size(); i++) {
        const Box& b = boxes[i];
        const std::string what = name + " ring " + std::to_string(i) + " at " + box_text(b);
        if (!check(!b.empty(), name + ": ring " + std::to_string(i) + " finds room")) continue;
        check(disc_visible(d, b), what + ": inside the visible area");
        check(b.y >= below + GAP, what + ": below the status bar");
        check(b.last_y() + GAP < floor, what + ": above the bottom caption");
        if (i + 1 == boxes.size()) check(left ? b.x < d.w / 2 : b.last_x() >= d.w / 2, what + ": on its corner's side");
        check(low ? b.y >= d.h / 2 : b.last_y() < d.h / 2, what + ": at its corner's edge");
        if (i + 1 < boxes.size()) {
            const Box& n = boxes[i + 1];
            check(left ? n.last_x() + GAP < b.x : b.last_x() + GAP < n.x,
                  what + ": the next is nearer the corner, apart");
        }
        if (i > 0) check(low ? b.last_y() == boxes[i - 1].last_y() : b.y == boxes[i - 1].y, what + ": edges level");
    }
}

void ring_layouts() {
    const std::vector<std::vector<int>> size_sets = {{1}, {2}, {3}, {2, 2}, {1, 3}, {3, 1, 2}, {1, 1, 1}, {2, 2, 2}};
    for (auto& d : displays())
        for (bool bar : {false, true})
            for (auto& sizes : size_sets) {
                std::vector<MeterRing> rings;
                for (int size : sizes) rings.push_back({0.5f, "100", 0xffff, size});
                const StatusBar::Lines lines = {{"standup in 5 min", 0xffff}};
                std::string name = d.id + (bar ? " with the bar," : ",") + " sizes";
                for (int size : sizes) name += " " + std::to_string(size);
                const int below = StatusBar::bottom(d, bar ? lines : StatusBar::Lines{});
                for (Corner corner : {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft, Corner::BottomRight})
                    for (bool caption : {false, true})
                        check_ring_layout(d, rings, below,
                                          name + ", " + corner_name(corner) + (caption ? ", bottom caption" : ""),
                                          corner, caption ? CaptionBar::top(d, SpeechAt::Bottom, below) - 1 : -1);
            }
    const std::vector<Box> large = meter_boxes(default_display(), {{1, "", 0, 3}, {1, "", 0, 3}, {1, "", 0, 3}}, 200);
    check(large[0].empty() && large[1].empty() && large[2].empty(), "rings with no room below a low bar are left out");
}

const char* const SHORT_CAPTION = "lunch";
const char* const LONG_CAPTION = "the deploy finished and every check passed, time for lunch";

Canvas blank(const Display& d) {
    Canvas c(d.w, d.h);
    c.fill(rgb(0x40, 0x90, 0x40));
    return c;
}

// With no status bar, a top caption is drawn exactly as a status bar showing that one line, scroll included.
void captions_look_like_status_lines() {
    for (auto& d : displays())
        for (const char* text : {SHORT_CAPTION, LONG_CAPTION}) {
            StatusBar bar;
            CaptionBar caption;
            bool same = true;
            for (uint32_t t = 0; t < 30000; t += 250) {
                Canvas a = blank(d), b = blank(d);
                bar.draw(a, d, {{text, 0xffe0}}, t);
                caption.draw(b, d, text, 0xffe0, SpeechAt::Top, 0, t);
                same = same && a.px == b.px;
            }
            check(same, d.id + ": a top caption is a status line, drawn the same (" + text + ")");
        }
}

// Rows of `c` in [y0, y1) that differ from the blank background anywhere.
int changed_rows(const Canvas& c, const Display& d, int y0, int y1) {
    const Canvas bg = blank(d);
    int n = 0;
    for (int y = std::max(0, y0); y < std::min(d.h, y1); y++)
        for (int x = 0; x < d.w; x++)
            if (c.px[size_t(y) * d.w + x] != bg.px[size_t(y) * d.w + x]) {
                n++;
                break;
            }
    return n;
}

void caption_rows() {
    const StatusBar::Lines lines = {{"standup in 5 min", 0x07ff}};
    const int h = StatusBar::height();
    for (auto& d : displays()) {
        const int below = StatusBar::bottom(d, lines);
        const int top = CaptionBar::top(d, SpeechAt::Top, below);
        check(top == below + 1 && CaptionBar::top(d, SpeechAt::Top, 0) == StatusBar::top(d),
              d.id + ": a top caption stacks right under the bar's line, or takes the bar's place");
        const int bottom = CaptionBar::top(d, SpeechAt::Bottom, below);
        check(bottom + h == d.h - StatusBar::top(d), d.id + ": a bottom caption mirrors the bar at the bottom");
        for (int row : {top, bottom}) {
            int x0, x1;
            check(row > 0 && row + h <= d.h && d.span(row, row + h, 0, x0, x1) && x1 - x0 > d.w / 2,
                  d.id + ": the caption row at " + std::to_string(row) + " is on the screen, at least half its width");
        }
        CaptionBar caption;
        StatusBar bar;
        Canvas c = blank(d);
        bar.draw(c, d, lines, 1000);
        caption.draw(c, d, SHORT_CAPTION, 0xffe0, SpeechAt::Top, below, 1000);
        check(changed_rows(c, d, below + 1, top) == 0 && changed_rows(c, d, top, top + h + 1) == h + 1 &&
                  changed_rows(c, d, top + h + 1, d.h) == 0,
              d.id + ": the caption is one band of the bar's height with its line under it, nothing else");
        c = blank(d);
        caption.draw(c, d, SHORT_CAPTION, 0xffe0, SpeechAt::Bottom, below, 1000);
        check(changed_rows(c, d, 0, bottom - 1) == 0 && changed_rows(c, d, bottom - 1, bottom + h) == h + 1,
              d.id + ": a bottom caption has its line above it");
        for (const char* text : {SHORT_CAPTION, LONG_CAPTION}) {
            CaptionBar fresh;
            Canvas a = blank(d), b = blank(d);
            fresh.draw(a, d, text, 0xffe0, SpeechAt::Top, below, 1000);
            fresh.draw(b, d, text, 0xffe0, SpeechAt::Top, below, 3000);
            check((a.px != b.px) == (std::string(text) == LONG_CAPTION),
                  d.id + ": only a caption too wide scrolls (" + text + ")");
        }
        const std::vector<Box> rings = meter_boxes(d, {{0.5f, "62", 0, 3}}, CaptionBar::bottom(d, below));
        check(!rings[0].empty() && rings[0].y > top + h, d.id + ": a ring goes below a top caption");
    }
}

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
        for (int i = 0; i < SETTLE_FRAMES; i++) frame();
    }
    void frame() {
        const Look s = brain.decide(t);
        brain.show(pet, t);
        pet.want(s, t);
        pet.update(t);
        pet.render(screen, t);
        t += FRAME_MS;
    }
    void post(const std::string& json) { handle_request(dev, "POST", "/v1/tell", json, t, HOST, "", false); }
};

// Two worlds run the same frames; one shows rings, then removes them. The scene puts them in `corner`.
void rings_on_frames(const Display& d, const Egg& base, Corner corner) {
    Egg egg = test::reloaded(base);
    egg.scene.gauges = corner;
    if (!egg.has_scene) corner = Corner::TopRight;     // the plain room keeps them top right
    const std::string name = d.id + " " + egg.name + " " + corner_name(corner);
    World plain(d, egg), ringed(d, egg);
    ringed.post(R"({"from":"spotify","state":"on","gauge":40,"color":"green"})");
    ringed.post(R"({"from":"usage","state":"on","gauge":62,"color":"orange"})");
    plain.frame();
    ringed.frame();
    const std::vector<Box> boxes =
        meter_boxes(d, {{0.4f, "40", 0, 2}, {0.62f, "62", 0, 2}}, StatusBar::bottom(d, StatusBar::Lines{}), corner);
    auto in_box = [&](const Box& b, int x, int y) { return b.contains(float(x), float(y)); };
    int green = 0, orange = 0, outside = 0;
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++) {
            const uint16_t p = ringed.screen.px[size_t(y) * d.w + x];
            if (in_box(boxes[0], x, y) && p == color_value("green")) green++;
            if (in_box(boxes[1], x, y) && p == color_value("orange")) orange++;
            if (!in_box(boxes[0], x, y) && !in_box(boxes[1], x, y) && p != plain.screen.px[size_t(y) * d.w + x])
                outside++;
        }
    check(green > 50 && orange > 50, name + ": both rings drawn in their colours (" + std::to_string(green) + ", " +
                                         std::to_string(orange) + " px)");
    const bool left = corner == Corner::TopLeft || corner == Corner::BottomLeft;
    const bool low = corner == Corner::BottomLeft || corner == Corner::BottomRight;
    check((left ? boxes[1].x < boxes[0].x && boxes[1].x < d.w / 2
                : boxes[1].x > boxes[0].x && boxes[1].last_x() > d.w / 2) &&
              (low ? boxes[1].y > d.h * 2 / 3 : boxes[1].y < d.h / 3),
          name + ": the latest ring is nearest the corner");
    check(outside == 0, name + ": nothing changes outside the rings (" + std::to_string(outside) + " px)");
    ringed.post(R"({"from":"spotify","state":"over"})");
    ringed.post(R"({"from":"usage","state":"over"})");
    plain.frame();
    ringed.frame();
    check(ringed.screen.px == plain.screen.px, name + ": removed rings leave the frame as it was");
}

// On a real frame with a status line, the ring is drawn below it.
void ring_under_status_line(const Display& d, const Egg& egg) {
    World w(d, egg);
    w.post(R"({"from":"cal","state":"on","line":"standup in 5 min"})");
    w.post(R"({"from":"usage","state":"on","gauge":62,"color":"purple"})");
    w.frame();
    const int bar_bottom = StatusBar::bottom(d, {{"x", 0}});
    int first = d.h;
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++)
            if (w.screen.px[size_t(y) * d.w + x] == color_value("purple")) first = std::min(first, y);
    check(first < d.h && first > bar_bottom, d.id + " " + egg.name + ": the ring is below the status line (row " +
                                                 std::to_string(first) + ")");
}

// Burn-in care (door/rest.h): a status line and a gauge that stay the same leave the screen after HUD_REST_MS, and
// come back when they change. Without care they stay.
void hud_rests(const Display& d, const Egg& egg) {
    World plain(d, egg), cared(d, egg), uncared(d, egg);
    for (World* w : {&cared, &uncared}) {
        w->post(R"({"from":"cal","state":"on","line":"standup at 10"})");
        w->post(R"({"from":"usage","state":"on","gauge":62,"color":"purple"})");
    }
    cared.pet.care(true);
    for (World* w : {&plain, &cared, &uncared}) w->frame();   // shown from here
    for (World* w : {&plain, &cared, &uncared}) {
        w->t += Pet::HUD_REST_MS;
        w->frame();
    }
    const std::string name = d.id + " " + egg.name;
    check(uncared.screen.px != plain.screen.px, name + ": without care the line and gauge stay");
    check(cared.screen.px == plain.screen.px, name + ": with care, unchanged for 5 minutes, they leave no trace");
    cared.post(R"({"from":"usage","state":"on","gauge":70,"color":"purple"})");
    plain.frame();
    cared.frame();
    check(cared.screen.px != plain.screen.px, name + ": a change brings them back");
}

// With burn-in care, a dimmed OLED screen shows only the pet on black, drifting; an LCD dims as it is.
void saver(const Display& d, const Egg& egg) {
    World w(d, egg);
    w.dev.settings.screen.care = true;
    w.dev.settings.screen.dim_after = 15;
    w.dev.active_at = w.t;
    const std::string name = d.id + " " + egg.name;
    auto lit = [&](int& x0, int& y0, int& x1, int& y1) {
        int n = 0;
        x0 = w.screen.w, y0 = w.screen.h, x1 = -1, y1 = -1;
        for (int y = 0; y < w.screen.h; y++)
            for (int x = 0; x < w.screen.w; x++)
                if (w.screen.px[size_t(y) * w.screen.w + x]) {
                    n++;
                    x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                }
        return n;
    };
    int x0, y0, x1, y1, a0, b0, a1, b1;
    update_rest(w.dev, w.t);
    w.frame();
    check(lit(x0, y0, x1, y1) > w.screen.w * w.screen.h / 2, name + ": awake, the scene fills the screen");
    w.t += 16 * 60 * 1000;
    check(update_rest(w.dev, w.t) == ScreenState::Dim, name + ": dimmed after a quarter of an hour");
    w.frame();
    const int pet = lit(x0, y0, x1, y1);
    if (!d.oled) {
        check(pet > w.screen.w * w.screen.h / 2, name + ": an LCD dims with its scene");
        return;
    }
    check(pet > 0 && (x1 - x0 + 1) * (y1 - y0 + 1) < w.screen.w * w.screen.h / 6 && x0 >= 0 && y1 < w.screen.h,
          name + ": dimmed, only the pet shows, whole, on black");
    w.t += Pet::SAVER_ACROSS_MS / 4;
    update_rest(w.dev, w.t);
    w.frame();
    check(lit(a0, b0, a1, b1) > 0 && (a0 != x0 || b0 != y0), name + ": and it drifts");
    w.dev.settings.screen.care = false;
    update_rest(w.dev, w.t);
    w.frame();
    check(lit(a0, b0, a1, b1) > w.screen.w * w.screen.h / 2, name + ": without care, dimmed shows the scene");
}

// A bubble stays put while its words stay and the pet stands where it stood, however much room its animation leaves
// beside the head or which way it turns; it is placed anew when the pet walks off or the words change.
void bubble_stays_put() {
    for (auto& d : displays()) {
        const SpeechSpot calm{140, 120, 128, 152, 112, 160, 0};
        SpeechSpot waving = calm;      // a wing out to the right, and turned to face it
        waving.right = 196;
        waving.top = 100;
        waving.facing = 1;
        SpeechPlace place;
        Canvas a = blank(d), b = blank(d), c = blank(d);
        draw_speech(a, d, "Claude has a question", 0xffff, 2, calm, 0, place);
        draw_speech(b, d, "Claude has a question", 0xffff, 2, waving, 0, place);
        check(a.px == b.px, d.id + ": a wave or a turn does not move the bubble");
        SpeechSpot walked = calm;
        walked.head_x += 40;
        walked.left += 40;
        walked.right += 40;
        draw_speech(c, d, "Claude has a question", 0xffff, 2, walked, 0, place);
        check(a.px != c.px, d.id + ": walking off takes it along");
        draw_speech(c, d, "Allow the edit to app.ts?", 0xffff, 2, walked, 0, place);
        check(place.text == "Allow the edit to app.ts?" && place.lines.front() != "Claude has a question" &&
                  place.head_x == walked.head_x,
              d.id + ": new words are placed anew");
    }
}

// On a real frame with a caption along the bottom, a ring in a bottom corner stays above it.
void ring_over_bottom_caption(const Display& d, const Egg& base) {
    Egg egg = test::reloaded(base);
    if (!egg.has_scene) return;
    egg.scene.gauges = Corner::BottomRight;
    World w(d, egg);
    w.post(R"({"from":"cal","state":"on","caption":"standup in 5 min"})");
    w.post(R"({"from":"usage","state":"on","gauge":62,"color":"purple"})");
    w.frame();
    const int caption_top = CaptionBar::top(d, SpeechAt::Bottom, 0);
    int last = -1;
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++)
            if (w.screen.px[size_t(y) * d.w + x] == color_value("purple")) last = std::max(last, y);
    check(last >= 0 && last < caption_top - 1 && last > d.h / 2,
          d.id + " " + egg.name + ": a bottom ring stays above the caption (row " + std::to_string(last) + ")");
}

}  // namespace

// The font draws printable ASCII and its extra characters (Latin-1, punctuation, a few symbols) as themselves, any
// other character as one "?".
void font_characters() {
    for (int size = 1; size <= 3; size++) {
        const FontSize& f = font_size(size);
        const char* note = "\u266a";
        const char* ask = "?";
        const Glyph* g = next_glyph(f, note);
        check(g && g != next_glyph(f, ask) && g->w > 0 && *note == 0,
              "the music note has a glyph of its own, size " + std::to_string(size));
        check(text_px("a\u266ab", size) == text_px("a", size) + text_px("\u266a", size) + text_px("b", size),
              "a note between letters takes its own width only, size " + std::to_string(size));
        check(text_px("\u65e5", size) == text_px("?", size) && text_px("\xe2", size) == text_px("?", size),
              "a character the font lacks, or a broken one, is one \"?\", size " + std::to_string(size));
        const char* star = "\u2605";
        const Glyph* s = next_glyph(f, star);
        check(s && s != next_glyph(f, ask) && *star == 0, "the star has a glyph of its own, size " + std::to_string(size));
    }
    check(drawable("\u2605 128 stars") && drawable("Don\u2019t Stop \u2013 Beyonc\u00e9 \u2713") && drawable("Song \u266a")
              && !drawable("\u65e5\u672c"),
          "stars, curly quotes, dashes, accented letters and ticks are drawable; characters the font lacks are not");
}

int main(int argc, char** argv) {
    font_characters();
    ring_layouts();
    captions_look_like_status_lines();
    caption_rows();
    bubble_stays_put();
    for (int i = 1; i < argc; i++) {
        Egg egg;
        if (!test::load_egg_file(argv[i], egg)) {
            test::fail(std::string("loads ") + argv[i]);
            continue;
        }
        for (auto& d : displays()) {
            for (Corner corner : {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft, Corner::BottomRight})
                rings_on_frames(d, egg, corner);
            ring_under_status_line(d, egg);
            hud_rests(d, egg);
            saver(d, egg);
            ring_over_bottom_caption(d, egg);
        }
    }
    return test::summary();
}
