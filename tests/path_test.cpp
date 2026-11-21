// The pet can get wherever a look needs it, on every screen profile (a cropped or round screen leaves less ground):
// from every spot it may stand on there is a walkable path home; at its desk it faces its laptop; it never rests
// hidden behind the laptop; a thrown item (hoop, splash or toss) stays on screen and its ripples inside it; typing
// shows on the laptop's screen; a hole cut into the floor is walked round, never stood in; the floor's row index
// answers as the polygon test does.
// Usage: path_test EGG...
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>
#include "body/parts.h"
#include "body/pet.h"
#include "body/plays.h"
#include "body/throwing.h"
#include "check.h"
#include "mind/brain.h"
#include "throw_kit.h"

using namespace nestbox;
using test::check;

namespace {

constexpr int GRID = 3;  // px between start spots: every 4 px cell gets one, off its centre
constexpr uint32_t FRAME_MS = 16;
constexpr uint32_t ARRIVE_MS = 60000;  // longest a walk to the desk may take

std::string tag(const Egg& egg, const Display& d) { return egg.name + " on " + d.id; }

std::string at(int x, int y) { return "(" + std::to_string(x) + ", " + std::to_string(y) + ")"; }

// Runs `pet` until it acts (or ARRIVE_MS); returns the time it got there.
uint32_t arrive(Pet& pet) {
    uint32_t t = 0;
    for (; t < ARRIVE_MS && pet.phase() != Pet::Phase::Acting; t += FRAME_MS) pet.update(t);
    return t;
}

// The floor and its holes indexed by row (EdgeRows) answer exactly as the plain polygon test, on and between
// pixels, at their corners and edges, and off the screen.
void edge_rows_match(const Egg& egg, const Display& d) {
    Pet pet;
    pet.set_display(d);
    pet.set_egg(&egg);
    const Scene& sc = pet.stage().scene();
    std::vector<const std::vector<std::pair<int, int>>*> polys{&sc.floor};
    for (const auto& hole : sc.blocked) polys.push_back(&hole);
    int differ = 0, tested = 0;
    for (const auto* poly : polys) {
        if (poly->size() < 3) continue;
        EdgeRows rows;
        rows.build(*poly);
        for (float y = -8; y < d.h + 8; y += 0.25f)
            for (float x = -8; x < d.w + 8; x += 0.75f) {
                tested++;
                if (rows.inside(*poly, x, y) != Scene::inside(*poly, x, y)) differ++;
            }
    }
    check(differ == 0, tag(egg, d) + ": the row index differs from the polygon test at " + std::to_string(differ) +
                           " of " + std::to_string(tested) + " points");
}

void paths_home(const Egg& egg, const Display& d) {
    Pet pet;
    pet.set_display(d);
    pet.set_egg(&egg);
    const Stage& stage = pet.stage();
    const int hx = stage.home().x, hy = stage.home().y;
    int spots = 0, unreachable = 0, bad_legs = 0, ux = -1, uy = -1, cut_off = 0;
    for (int y = 1; y < pet.display().h; y += GRID)
        for (int x = 1; x < pet.display().w; x += GRID) {
            if (!stage.can_stand(x, y)) continue;
            if (!stage.reachable(x, y)) {      // cut off from home (a round screen's edge): it never goes there
                cut_off++;
                continue;
            }
            spots++;
            const auto route = stage.path(x, y, hx, hy);
            if (route.empty()) {
                if (!unreachable++) {
                    ux = x;
                    uy = y;
                }
                continue;
            }
            float px = x, py = y;
            for (auto& [qx, qy] : route) {
                const int n = std::max(1, int(std::hypot(qx - px, qy - py)));
                for (int k = 1; k <= n; k++)
                    if (!stage.can_stand(px + (qx - px) * k / n, py + (qy - py) * k / n)) {
                        bad_legs++;
                        break;
                    }
                px = qx;
                py = qy;
            }
        }
    std::printf("%-22s home %s: %d spots, %d cannot reach home, %d paths leave the ground, %d cut off\n",
                tag(egg, d).c_str(), at(hx, hy).c_str(), spots, unreachable, bad_legs, cut_off);
    check(spots > 0, tag(egg, d) + ": there is ground to stand on");
    check(!unreachable, tag(egg, d) + ": every spot it may go to reaches home (first that does not: " + at(ux, uy) +
                            ")");
    check(!bad_legs, tag(egg, d) + ": every path stays on the ground");
}

// Watching, it faces its screen; a pet's own Watch keeps its own pose, facing the front.
void watches_its_screen(const Egg& egg, const Display& d) {
    const bool toward_desk = play_for(egg, info(Look::Watch)).play.facing == Facing::Desk;
    Pet w;
    w.set_display(d);
    w.set_egg(&egg);
    w.want(Look::Watch, 0);
    const uint32_t t = arrive(w);
    for (uint32_t k = 0; k < 1000; k += FRAME_MS) w.update(t + k);
    const int want_row = toward_desk ? w.stage().desk_row(w.x(), w.y()) : ROW_FRONT, got = w.row();
    const int off = std::min((got - want_row + 8) % 8, (want_row - got + 8) % 8);
    check(w.phase() == Pet::Phase::Acting && (toward_desk ? off <= 1 : off == 0),
          tag(egg, d) + ": watching from " + at(int(w.x()), int(w.y())) + " it faces row " + std::to_string(got) +
              (toward_desk ? ", toward its screen is row " : ", its own Watch's is row ") + std::to_string(want_row));
}

void never_rests_behind_laptop(const Egg& egg, const Display& d) {
    int stops = 0, behind = 0, bx = -1, by = -1;
    for (Look s : {Look::Idle, Look::Wander}) {
        Pet pet;
        pet.set_display(d);
        pet.set_egg(&egg);
        pet.want(s, 0);
        float lx = -1, ly = -1;
        for (uint32_t t = 0; t < 600000; t += FRAME_MS) {
            pet.update(t);
            if (t % 512) continue;
            const bool still = pet.x() == lx && pet.y() == ly;
            lx = pet.x();
            ly = pet.y();
            if (!still) continue;
            stops++;
            if (pet.stage().hidden(pet.x(), pet.y()) && !behind++) {
                bx = int(pet.x());
                by = int(pet.y());
            }
        }
    }
    std::printf("%-22s idle and wandering: %d still moments\n", tag(egg, d).c_str(), stops);
    check(stops > 0, tag(egg, d) + ": it rests now and then");
    check(!behind, tag(egg, d) + ": it never rests behind its laptop (first at " + at(bx, by) + ")");
}

// The egg with a throw kit flying `f`, its target (for a hoop or a splash) put at the screen point `at` on `d`, and
// its scene still, so frame-to-frame changes are the throw's own.
Egg kit_egg(const Egg& egg, const Display& d, Flight f, std::optional<Point> at) {
    test::KitOptions o;
    o.flight = f;
    if (at) o.target = test::scene_point(egg, d, *at);
    Egg kit = test::with_throw(egg, o);
    kit.scene.ambient.clear();
    return kit;
}

// Where a hoop's rim and a splash's water are put: high up to one side, and low on the floor away from home.
Point rim_spot(const Display& d) { return {float(d.w / 2 + d.w / 5), float(d.h / 3)}; }

Point water_spot(const Egg& egg, const Display& d) {
    Pet probe;
    probe.set_display(d);
    probe.set_egg(&egg);
    return probe.stage().nearest_standable({float(d.w / 4), float(d.h * 3 / 4)});
}

// From every standable spot, the item's flight stays on screen in short hops: a hoop reaches the rim, drops
// through and bounces back to the pet's hands; a splash lands on its aim and is gone; a toss goes up and comes back
// down to the hands without needing a target.
void throw_arcs(const Egg& egg, const Display& d) {
    if (!egg.has_scene) return;
    for (Flight f : {Flight::Hoop, Flight::Splash, Flight::Toss}) {
        const char* name = f == Flight::Hoop ? "hoop" : f == Flight::Splash ? "splash" : "toss";
        const std::optional<Point> placed = f == Flight::Hoop     ? std::optional<Point>(rim_spot(d))
                                        : f == Flight::Splash ? std::optional<Point>(water_spot(egg, d))
                                                              : std::nullopt;
        const Egg kit = kit_egg(egg, d, f, placed);
        Pet h;
        h.set_display(d);
        h.set_egg(&kit);
        const std::string where = tag(egg, d) + ", a " + name;
        if (!check(h.can_throw(), where + ": it can throw")) continue;
        Point aim{-1000, -1000};                         // a toss ignores it
        if (placed) aim = {float(h.stage().target()->x), float(h.stage().target()->y)};
        const int u = kit.scale ? kit.scale : d.scale, fu = d.scale;   // the pet's pixels; the flight's
        const int w = d.w, hgt = d.h, air = air_ticks(f);
        int spots = 0, bad = 0, bx = -1, by = -1;
        for (int y = 1; y < hgt; y += GRID)
            for (int x = 1; x < w; x += GRID) {
                if (!h.stage().can_stand(x, y)) continue;
                spots++;
                const Point from{float(x), y - kit.tall * u * 1.0f}, hands{float(x), y - kit.tall * u * 0.25f};
                float qx = 0, qy = 0, px = from.x, py = from.y;
                auto item = [&](int k) {
                    const auto p = item_at(f, k, from, aim, hands, fu);
                    if (p) {
                        qx = p->x;
                        qy = p->y;
                    }
                    return p.has_value();
                };
                bool ok = !item(-1);
                for (int k = 0; k < air && ok; k++) {
                    ok = item(k) && qx >= 0 && qx < w && qy >= 0 && qy < hgt &&
                         std::hypot(qx - px, qy - py) <= 8 * fu;   // one tick is a short hop
                    if (ok && f == Flight::Hoop && k == FLY_TICKS - 1)   // the next tick lands on the rim
                        ok = std::hypot(2 * qx - px - aim.x, 2 * qy - py - aim.y) <= 2 * fu;
                    if (ok && f == Flight::Splash && k == FLY_TICKS - 1)   // its last tick is on the water
                        ok = std::abs(qx - aim.x) <= 1 && std::abs(qy - aim.y) <= 1;
                    px = qx;
                    py = qy;
                    if (ok && f == Flight::Hoop && k == FLY_TICKS)
                        ok = std::abs(qx - aim.x) <= 1 && std::abs(qy - aim.y) <= 1;
                    if (ok && f != Flight::Splash && k == air - 1)
                        ok = std::abs(qx - hands.x) <= 2 * fu && std::abs(qy - hands.y) <= 2 * fu;
                    if (ok && f == Flight::Toss) {               // the same whatever it is aimed at
                        const auto other = item_at(f, k, from, {from.x + 50, from.y + 50}, hands, fu);
                        ok = other && other->x == qx && other->y == qy;
                    }
                }
                ok = ok && !item(air);   // then it is in its hands, or gone
                if (!ok && !bad++) {
                    bx = x;
                    by = y;
                }
            }
        std::printf("%-22s %s: from %d spots, %d flights wrong\n", tag(egg, d).c_str(), name, spots, bad);
        check(spots > 0 && !bad, where + ": every flight is right (first wrong from " + at(bx, by) + ")");
    }
    check(air_ticks(Flight::Hoop) == HOOP_AIR_TICKS && air_ticks(Flight::Splash) == FLY_TICKS &&
              air_ticks(Flight::Toss) == TOSS_TICKS,
          "a hoop flies the arc, the drop and two bounces; a splash only the arc; a toss up and down");
    check(effect_ticks(Flight::Splash) == RIPPLE_TICKS && effect_ticks(Flight::Hoop) > DROP_TICKS &&
              effect_ticks(Flight::Toss) == 0,
          "ripples spread for a splash, a net swishes for a hoop, a toss has no moment at its target");
}

// Frame by frame, one throw from home: the item is drawn where its flight says (and nowhere once it has gone); a
// splash then draws its ripples round its aim, inside the screen, and nothing else on the screen changes.
void throw_on_screen(const Egg& egg, const Display& d) {
    if (!egg.has_scene) return;
    for (Flight f : {Flight::Hoop, Flight::Splash, Flight::Toss}) {
        const char* name = f == Flight::Hoop ? "hoop" : f == Flight::Splash ? "splash" : "toss";
        const std::optional<Point> placed = f == Flight::Toss ? std::nullopt : std::optional<Point>(rim_spot(d));
        const Egg kit = kit_egg(egg, d, f, placed);
        const std::string where = tag(egg, d) + ", a " + name + " thrown";
        Pet pet;
        pet.set_display(d);
        pet.set_egg(&kit);
        Canvas screen(d.w, d.h), last(d.w, d.h);
        uint32_t t = 1000;
        pet.want(Look::Idle, t);
        pet.update(t);
        pet.render(screen, t);
        const int before = test::item_pixels(screen);
        Act a;
        a.kind = Act::Kind::Throw;
        // a splash is aimed at the bottom edge of the screen (its ripples spill over it), the others at the target
        const Point splash_aim{float(d.w / 2), float(d.h - 4)};
        if (f == Flight::Splash) a.aim = {Spot::Kind::Point, splash_aim.x, splash_aim.y};
        const Point aim = f == Flight::Splash ? splash_aim : placed ? *placed : Point{};
        pet.act(a);
        pet.want(Look::Act, t);
        uint32_t start = 0;
        for (; t < 20000 && !start; t += FRAME_MS) {
            pet.update(t);
            if (pet.phase() == Pet::Phase::Acting) start = t;
        }
        if (!check(start > 0, where + ": it gets ready to throw")) continue;
        const Anim& thrower = *kit.find("Throw");
        const int release = test::release_ticks(thrower), u = pet.stage().unit(), fu = d.scale;
        const Point hands{pet.x(), pet.y() - kit.tall * u * 0.25f};
        const Point target = f == Flight::Toss ? Point{} : Point{float(pet.stage().target()->x),
                                                               float(pet.stage().target()->y)};
        const Point flight_aim = f == Flight::Splash ? aim : target;
        int seen = 0, expected = 0, misplaced = 0, stray = 0, rippled = 0;
        std::string first_wrong;
        pet.render(last, start);
        for (uint32_t ms = FRAME_MS; ms < 4000; ms += FRAME_MS) {
            pet.update(start + ms);
            const Point from = pet.item_from();      // the head as the last frame drew it
            pet.render(screen, start + ms);
            const int tick = int(ms_to_ticks(ms) % test::cycle_ticks(kit));
            const auto p = item_at(f, tick - release, from, flight_aim, hands, fu);
            int x0 = d.w, y0 = d.h, x1 = -1, y1 = -1;
            for (int y = 0; y < d.h; y++)
                for (int x = 0; x < d.w; x++)
                    if (screen.px[size_t(y) * d.w + x] == test::ITEM_COLOUR) {
                        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
                    }
            const bool drawn = x1 >= 0;
            if (p && d.visible(int(p->x), int(p->y), test::ITEM_FRAME * u)) {
                expected++;
                if (drawn) seen++;
                if (drawn && std::hypot((x0 + x1) / 2.0f - p->x, (y0 + y1) / 2.0f - p->y) > 2 * u) {
                    misplaced++;
                    if (first_wrong.empty())
                        first_wrong = at(int(p->x), int(p->y)) + " drawn at " + at((x0 + x1) / 2, (y0 + y1) / 2);
                }
            }
            if (!p && drawn && test::item_pixels(screen) > before) stray++;
            // after the splash: only its ripples change the screen, round its aim
            const int since = tick - release - FLY_TICKS;
            if (f == Flight::Splash && since > 0 && since < RIPPLE_TICKS) {
                const float rx = 12.0f * fu, ry = rx * 0.4f + fu;          // ripples in the screen's pixels
                for (int y = 0; y < d.h; y++)
                    for (int x = 0; x < d.w; x++) {
                        const size_t i = size_t(y) * d.w + x;
                        if (screen.px[i] == last.px[i]) continue;
                        if (std::abs(x - aim.x) <= rx && std::abs(y - aim.y) <= ry) rippled++;
                        else stray++;
                    }
            }
            last = screen;
        }
        check(expected > 10 && seen == expected,
              where + ": the item is drawn on every frame of its flight (" + std::to_string(seen) + " of " +
                  std::to_string(expected) + ")");
        check(!misplaced, where + ": where its flight says (" + first_wrong + ")");
        check(!stray, where + ": nothing else is drawn or changed (" + std::to_string(stray) + " px)");
        if (f == Flight::Splash) check(rippled > 0, where + ": ripples spread round its aim, inside the screen");
    }
}

// Typing fast with a kit, it throws again and again from its home; without one it never does.
void throws_while_typing_fast(const Egg& egg, const Display& d) {
    if (!egg.has_scene) return;
    for (bool with_kit : {true, false}) {
        const Egg kit = with_kit ? kit_egg(egg, d, Flight::Hoop, rim_spot(d)) : test::reloaded(egg);
        const std::string where = tag(egg, d) + (with_kit ? ", typing fast with a kit" : ", typing fast without one");
        Pet pet;
        pet.set_display(d);
        pet.set_egg(&kit);
        Canvas screen(d.w, d.h);
        pet.want(Look::TypeFast, 0);
        const uint32_t t = arrive(pet);
        const Pixel home = pet.stage().home();
        check(std::hypot(pet.x() - home.x, pet.y() - home.y) < 3, where + ": it goes home");
        int thrown = 0;
        const uint32_t cycle = with_kit ? ticks_to_ms(test::cycle_ticks(kit)) : 3000;
        bool was = false;
        for (uint32_t k = 0; k < 2 * cycle + 100; k += FRAME_MS) {
            pet.update(t + k);
            pet.render(screen, t + k);
            const bool now = test::item_pixels(screen) > 0;
            if (now && !was) thrown++;
            was = now;
        }
        check(with_kit ? thrown == 2 : thrown == 0,
              where + (with_kit ? ": it throws once a cycle" : ": nothing is thrown") + " (" + std::to_string(thrown) +
                  ")");
    }
}

// A kit that plays: in its free time the brain hands it a game, a few throws from each of a few places apart from
// each other, and it stops by itself; the next waits its gap. A kit that doesn't play never throws on its own.
void plays_in_free_time(const Egg& egg, const Display& d) {
    if (!egg.has_scene) return;
    for (bool plays : {true, false}) {
        Egg kit = kit_egg(egg, d, Flight::Hoop, rim_spot(d));
        kit.throw_kit.plays = plays;
        kit.pastimes.clear();                                    // the game alone, not taking turns with its own
        const std::string where = tag(egg, d) + (plays ? ", a kit that plays" : ", a kit that doesn't play");
        Brain b;
        Pet pet;
        pet.set_display(d);
        pet.set_egg(&kit);
        uint32_t t = 1000, started = 0, ended = 0;
        std::vector<Point> spots;
        auto frame = [&] {
            const Look s = b.decide(t);
            b.show(pet, t);
            pet.want(s, t);
            pet.update(t);
            const bool throwing = s == Look::Act && b.doing(s) == "throw";
            if (throwing && !started) started = t;
            if (!throwing && started && !ended) ended = t;
            if (throwing && pet.phase() == Pet::Phase::Acting) {
                bool known = false;
                for (const Point& p : spots) known = known || std::hypot(p.x - pet.x(), p.y - pet.y()) < 15;
                if (!known) spots.push_back({pet.x(), pet.y()});
            }
            t += FRAME_MS;
        };
        while (t < 1000 + FreeTime::PLAY_FIRST_MAX + 5000 && !started) frame();
        if (!plays) {
            while (t < 6 * 60 * 1000) frame();
            check(!started, where + ": it never throws on its own");
            continue;
        }
        if (!check(started >= 1000 + FreeTime::PLAY_FIRST_MIN, where + ": a game starts after 1-2.5 min of quiet"))
            continue;
        while (t < started + FreeTime::PLAY_MAX + 5000 && !ended) frame();
        check(ended && ended - started <= FreeTime::PLAY_MAX + 100, where + ": and ends by itself (" +
                                                                      std::to_string((ended - started) / 1000) + " s)");
        check(spots.size() >= 2, where + ": throwing from " + std::to_string(spots.size()) + " places");
        bool all_reachable = true;
        for (const Point& p : spots) all_reachable = all_reachable && pet.stage().reachable(p.x, p.y);
        check(all_reachable, where + ": each one on its floor");
        const uint32_t after = t;
        bool again = false;
        while (t < after + FreeTime::PLAY_GAP_MIN - 2000) {
            frame();
            again = again || (b.doing(pet.wanted()) == "throw");
        }
        check(!again, where + ": the next game waits its gap");
    }
}

// A pet's own pastimes: in its free time it plays one, says which in its reason, walks where its steps say and
// does what they say, then carries on; one limited to hours outside the time of day never comes.
void pastimes_in_free_time(const Egg& egg, const Display& d) {
    if (!egg.has_scene) return;
    for (bool in_hours : {true, false}) {
        Egg pets = test::reloaded(egg);
        pets.has_throw = false;
        pets.pastimes.clear();                                   // only the test's own
        Pastime p;
        p.name = "a test";
        if (!in_hours) p.from_hour = 3, p.to_hour = 4;
        Fields play, stay, look;
        play.set("at.0", "50");
        play.set("at.1", "70");
        play.set("do", "Idle");
        play.set("for", "2");
        play.set("show", "kite");
        stay.set("do", "Idle");
        stay.set("for", "1");
        stay.set("show", "kite");
        look.set("do", "Idle");
        look.set("for", "1");
        look.set("show", "cloud");
        p.steps = {play, stay, look};
        pets.pastimes.push_back(p);
        const std::string where = tag(egg, d) + (in_hours ? ", a pastime" : ", a pastime outside its hours");
        Brain b;
        b.clock(int64_t(14) * 3600, 1000);
        Pet pet;
        pet.set_display(d);
        pet.set_egg(&pets);
        uint32_t t = 1000, started = 0, ended = 0;
        bool walked = false, played = false;
        std::set<uint32_t> kite, cloud;                          // when each part was shown from
        while (t < 1000 + FreeTime::PLAY_FIRST_MAX + 30000 && !ended) {
            const Look s = b.decide(t);
            b.show(pet, t);
            pet.want(s, t);
            pet.update(t);
            const bool on = b.why() == "its own life: a test";
            if (on && !started) started = t;
            if (!on && started) ended = t;
            if (on && pet.phase() == Pet::Phase::Walking) walked = true;
            if (on && b.doing(s) == "Idle" && pet.phase() == Pet::Phase::Acting) played = true;
            if (on && pet.shown_part() == "kite") kite.insert(pet.shown_since());
            if (on && pet.shown_part() == "cloud") cloud.insert(pet.shown_since());
            t += FRAME_MS;
        }
        if (!in_hours) {
            check(!started, where + ": never comes");
            Fields keys;                                         // pressing P is typing to the computer
            parse_json(R"({"from":"mac","keys":6,"idle":0,"state":"awake"})", keys);
            b.user(keys, t);
            b.next_pastime(t);                                   // the simulator's P key: now, whatever its hours
            bool now = false, typed = false, through = false;
            for (uint32_t end = t + 30000; t < end && !through; t += FRAME_MS) {
                if (t % 1000 < FRAME_MS) b.user(keys, t);
                const Look s = b.decide(t);
                b.show(pet, t);
                pet.want(s, t);
                pet.update(t);
                const bool on = b.why() == "its own life: a test";
                now = now || on;
                if (!now || on) typed = typed || s == Look::Type;   // after it, the typing shows again
                through = now && !on;
            }
            check(now && !typed, where + ": unless the P key steps to it: at once, ahead of the typing P is");
            check(through, where + ": and it plays through to its end");
            continue;
        }
        check(started >= 1000 + FreeTime::PLAY_FIRST_MIN, where + ": comes after 1-2.5 min of quiet");
        check(walked && played && ended, where + ": walks, plays its step and ends");
        check(kite.size() == 1 && *kite.begin() <= started + FRAME_MS && cloud.size() == 1 &&
              *cloud.begin() > started + 3000,
              where + ": steps in a row that show a part keep it going from the first; another part starts afresh");
        const Point spot = pet.stage().nearest_standable(*pet.spot_point({Spot::Kind::Scene, 50, 70}));
        check(std::hypot(pet.x() - spot.x, pet.y() - spot.y) < 3, where + ": where its steps took it");
    }
}

// A pastime step that plays one of its animations once ends on that animation's last frame: never a wrapped first
// frame of a second play before the next step (a hat put on must not flash off again).
void one_play_ends_on_its_last_frame(const Egg& egg, const Display& d) {
    if (!egg.has_scene) return;
    const Anim* anim = nullptr;
    for (const Anim& a : egg.anims)
        if (a.frames >= 3 && a.name != "Idle" && a.name != "Walk" && a.name != "Sleep") {
            anim = &a;
            break;
        }
    if (!anim) return;
    Egg pets = test::reloaded(egg);
    pets.has_throw = false;
    pets.pastimes.clear();
    Pastime p;
    p.name = "a test play";
    Fields go, play, stay;
    go.set("at.0", "50");
    go.set("at.1", "70");
    go.set("do", "Idle");
    go.set("for", "0.5");
    play.set("do", anim->name);
    stay.set("do", "Idle");
    stay.set("for", "1");
    p.steps = {go, play, stay};
    pets.pastimes.push_back(p);
    Brain b;
    b.clock(int64_t(14) * 3600, 1000);
    Pet pet;
    pet.set_display(d);
    pet.set_egg(&pets);
    Canvas screen(d.w, d.h);
    std::vector<int> drawn;                                      // the frames it drew while playing the animation
    bool ended = false, started = false;
    for (uint32_t t = 1000; t < 1000 + FreeTime::PLAY_FIRST_MAX + 30000 && !ended; t += FRAME_MS) {
        const Look s = b.decide(t);
        b.show(pet, t);
        pet.want(s, t);
        pet.update(t);
        const bool on = b.why() == "its own life: a test play";
        started = started || on;
        ended = started && !on;
        if (!on || pet.phase() != Pet::Phase::Acting) continue;
        pet.render(screen, t);
        if (pet.playing().rfind(anim->name, 0) == 0) drawn.push_back(pet.drawn_frame());
    }
    bool forward = !drawn.empty();
    for (size_t i = 1; i < drawn.size(); i++) forward = forward && drawn[i] >= drawn[i - 1];
    check(forward && drawn.back() == anim->frames - 1,
          tag(egg, d) + ": a step playing " + anim->name + " once runs forward to its last frame and stops there");
}

// A scene's own watch spot is where it watches its screen from.
void watches_from_its_spot(const Egg& egg, const Display& d) {
    if (!egg.has_scene) return;
    Egg pets = test::reloaded(egg);
    Pet probe;
    probe.set_display(d);
    probe.set_egg(&pets);
    const Point spot = probe.stage().nearest_standable(probe.stage().from_scene(50, 70));
    const Point pc = probe.stage().to_scene(spot);
    pets.scene.watch_x = int(std::lround(pc.x * pets.scene.w / 100));
    pets.scene.watch_y = int(std::lround(pc.y * pets.scene.h / 100));
    Pet w;
    w.set_display(d);
    w.set_egg(&pets);
    const std::optional<Pixel> at = w.stage().watch_spot();
    if (!at || !w.stage().reachable(at->x, at->y)) return;       // cut off on this screen: it watches from home
    w.want(Look::Watch, 0);
    arrive(w);
    check(w.phase() == Pet::Phase::Acting && std::hypot(w.x() - at->x, w.y() - at->y) < 3,
          tag(egg, d) + ": with a watch spot in its scene it watches from there");
}

// A pastime that swims: it jumps from its floor into the scene's water, swims there (off its floor, inside the
// water), leaps clear of the water and back into it, jumps back out onto the floor and carries on. In the water it
// never stands: turning, it paddles. Something coming first while it swims has it jump out onto
// the nearest bank before it goes on.
void swims_in_free_time(const Egg& egg, const Display& d) {
    if (!egg.has_scene || egg.scene.target_x < 0) return;
    for (bool interrupted : {false, true}) {
        Egg pets = test::reloaded(egg);
        pets.has_throw = false;
        const int tx = egg.scene.target_x, ty = egg.scene.target_y;   // the scene's splash point
        pets.scene.water = {{tx - 14, ty - 8}, {tx + 14, ty - 8}, {tx + 14, ty + 8}, {tx - 14, ty + 8}};
        pets.scene.water_depth = 14;
        Pet probe;
        probe.set_display(d);
        probe.set_egg(&pets);
        const Point water = probe.stage().from_scene(100.0f * tx / egg.scene.w, 100.0f * ty / egg.scene.h);
        const Point bank = probe.stage().nearest_standable(water);
        const std::string where = tag(egg, d) + (interrupted ? ", a swim cut short" : ", a swim");
        if (!check(probe.stage().in_water(water.x, water.y) && !probe.stage().can_stand(water.x, water.y),
                   where + ": its test water is off its floor"))
            continue;
        if (!interrupted) {                                      // shallows: it sinks by how far it is from its floor
            check(probe.stage().water_deep(water.x, water.y) == 1, where + ": without shallows it sinks all the way");
            Egg shallow = test::reloaded(pets);
            shallow.scene.water_shallow = 40;
            Pet wade;
            wade.set_display(d);
            wade.set_egg(&shallow);
            const float len = std::max(1.0f, std::hypot(water.x - bank.x, water.y - bank.y));
            const float k = std::min(1.0f, 15.0f / len);         // a little way out from the bank
            const Point edge{bank.x + (water.x - bank.x) * k, bank.y + (water.y - bank.y) * k};
            const float near = wade.stage().water_deep(bank.x, bank.y), mid = wade.stage().water_deep(edge.x, edge.y);
            check(near < 0.1f && mid > near && mid < 1, where + ": with shallows it sinks gradually from its floor");
        }
        auto percent = [&](Point p) {
            const Point s = probe.stage().to_scene(p);
            return std::make_pair(std::to_string(s.x), std::to_string(s.y));   // percent of the scene
        };
        auto step = [&](Point p, const char* how, const char* value = "true") {
            Fields f;
            const auto [x, y] = percent(p);
            f.set("at.0", x);
            f.set("at.1", y);
            f.set(how, value);
            return f;
        };
        Pastime p;
        p.name = "a test swim";
        Fields stay;
        stay.set("do", "Idle");
        stay.set("for", "1");
        p.steps = {step(water, "jump"), step({water.x + 10, water.y}, "swim"),
                   step({water.x - 10, water.y}, "jump"),       // a leap from the water back into it
                   step({water.x + 6, water.y}, "swim", "Sleep"),   // swims back with an animation of its own
                   step(bank, "jump"), stay};
        pets.pastimes = {p};
        Brain b;
        b.clock(int64_t(14) * 3600, 1000);
        Pet pet;
        pet.set_display(d);
        pet.set_egg(&pets);
        uint32_t t = 1000, started = 0;
        bool jumped = false, swam = false, outside = false, ended = false, cut = false, own_swim = false;
        bool stood = false, was_jumping = false;
        Canvas screen(d.w, d.h);
        int swim_frames = 0, jumps = 0;
        float swim_head = 0, leap_top = 1e9f;                     // its head paddling, and at the top of its leap
        while (t < 1000 + FreeTime::PLAY_FIRST_MAX + 60000 && !ended) {
            const Look s = b.decide(t);
            b.show(pet, t);
            pet.want(cut ? Look::Type : s, t);
            pet.update(t);
            const bool on = b.why() == "its own life: a test swim";
            if (on && !started) started = t;
            if (started && !on && !cut) ended = true;
            jumped = jumped || pet.jumping();
            if (pet.jumping() && !was_jumping) jumps++;
            was_jumping = pet.jumping();
            if (jumps == 2 && pet.jumping()) {                   // its second jump: the leap within the water
                pet.render(screen, t);
                leap_top = std::min(leap_top, pet.drawn_head().y);
            }
            if (pet.swimming()) {
                swam = true;
                swim_frames++;
                outside = outside || !pet.stage().in_water(pet.x(), pet.y());
                pet.render(screen, t);                           // what it plays is known once drawn
                own_swim = own_swim || (pet.phase() == Pet::Phase::Walking && pet.playing().rfind("Sleep", 0) == 0);
                stood = stood || pet.playing().rfind("Idle", 0) == 0;
                if (!pet.jumping() && jumps == 1) swim_head = pet.drawn_head().y;
                if (interrupted && swim_frames == 20) cut = true;    // the user starts typing mid-swim
            }
            if (cut && pet.phase() == Pet::Phase::Acting && pet.look() == Look::Type) ended = true;
            t += FRAME_MS;
        }
        check(started && jumped && swam && !outside, where + ": it jumps in and swims inside the water");
        if (!interrupted) {
            check(own_swim, where + ": a step that swims with its own animation plays it on the way");
            if (pets.find("Swim")) check(!stood, where + ": in the water it paddles, never stands");
            const float clear = 0.85f * float((JUMP_HEIGHT + pets.scene.water_depth) * probe.stage().unit());
            check(jumps >= 3 && swim_head - leap_top > clear,      // up from its depth to a full jump above the water
                  where + ": a leap within the water rises clear of it");
        }
        check(ended && !pet.swimming() && pet.stage().can_stand(pet.x(), pet.y()),
              where + (interrupted ? ": typing comes first: it jumps out and goes to its desk"
                                   : ": then jumps out onto its floor"));
    }
}

// The laptop's screen is where the work spot's "on" picture differs from "off".
void typing_shows_on_laptop(const Egg& egg, const Display& d) {
    Pet pet;
    pet.set_display(d);
    pet.set_egg(&egg);
    pet.typing(6);
    pet.want(Look::Type, 0);
    const uint32_t now = arrive(pet);
    const Box& box = pet.stage().work_screen();
    if (box.empty()) return;
    Canvas a(pet.display().w, pet.display().h), b(pet.display().w, pet.display().h);
    pet.update(now);
    pet.render(a, now);
    for (uint32_t k = FRAME_MS; k <= 3000; k += FRAME_MS) pet.update(now + k);
    pet.render(b, now + 3000);
    int changed = 0;
    for (int y = box.y; y <= box.last_y(); y++)
        for (int x = box.x; x <= box.last_x(); x++) changed += a.px[size_t(y) * a.w + x] != b.px[size_t(y) * b.w + x];
    check(changed > 0, tag(egg, d) + ": typing changes its laptop's screen");
}

// A hole cut into the floor (scene "blocked"): the pet never stands in it, walks round it from one side to the
// other, and runs its laps clear of it. A square of open floor is found first, in the scene's own pixels.
void walks_round_a_hole(const Egg& egg, const Display& d) {
    if (!egg.has_scene || egg.scene.floor.empty()) return;
    constexpr int HALF = 5, SIDE = 4;  // the hole's half size and the floor beside it, scene px
    Pet open;
    open.set_display(d);
    open.set_egg(&egg);
    const Stage& st = open.stage();
    const int s = egg.scene.scale;
    const int ox = st.scene_x(), oy = st.scene_y();      // scene pixels to screen pixels, as the stage fits it
    auto screen = [&](int x, int y) { return Point{float(x * s - ox), float(y * s - oy)}; };
    int cx = -1, cy = -1;
    for (int y = 0; y < egg.scene.h && cx < 0; y += 2)
        for (int x = 0; x < egg.scene.w && cx < 0; x += 2) {
            const Point l = screen(x - HALF - SIDE, y), r = screen(x + HALF + SIDE, y);
            bool clear = st.can_stand(l.x, l.y) && st.can_stand(r.x, r.y) && st.line_clear(l.x, l.y, r.x, r.y);
            for (int dy = -HALF - SIDE; clear && dy <= HALF + SIDE; dy += 2)
                for (int dx = -HALF - SIDE; clear && dx <= HALF + SIDE; dx += 2) {
                    const Point p = screen(x + dx, y + dy);
                    clear = st.can_stand(p.x, p.y);
                }
            if (clear) cx = x, cy = y;
        }
    const std::string where = tag(egg, d) + ", a hole in the floor";
    if (!check(cx >= 0, where + ": its floor has room for one")) return;
    Egg holed = test::reloaded(egg);
    holed.scene.blocked.push_back({{cx - HALF, cy - HALF}, {cx + HALF, cy - HALF}, {cx + HALF, cy + HALF},
                                   {cx - HALF, cy + HALF}});
    Pet pet;
    pet.set_display(d);
    pet.set_egg(&holed);
    const Stage& hs = pet.stage();
    const Point c = screen(cx, cy), l = screen(cx - HALF - SIDE, cy), r = screen(cx + HALF + SIDE, cy);
    // in it: deeper than a line grazing a corner may cut in, the floor being checked a screen pixel apart
    auto in_hole = [&](Point p) {
        constexpr float GRAZE = 0.5f;
        const Point a = screen(cx - HALF, cy - HALF), b = screen(cx + HALF, cy + HALF);
        return p.x > a.x + GRAZE && p.x < b.x - GRAZE && p.y > a.y + GRAZE && p.y < b.y - GRAZE;
    };
    check(st.can_stand(c.x, c.y) && !hs.can_stand(c.x, c.y), where + ": its middle was floor and is not now");
    check(hs.can_stand(l.x, l.y) && hs.can_stand(r.x, r.y), where + ": both sides of it still are");
    check(!hs.line_clear(l.x, l.y, r.x, r.y), where + ": the straight line across it is not clear");
    const std::vector<Point> route = hs.path(l.x, l.y, r.x, r.y);
    bool round = !route.empty(), clear = true;
    Point from = l;
    float length = 0;
    for (const Point& p : route) {
        clear = clear && hs.line_clear(from.x, from.y, p.x, p.y);
        const int n = std::max(1, int(std::hypot(p.x - from.x, p.y - from.y)));
        for (int k = 0; k <= n; k++)
            if (in_hole({from.x + (p.x - from.x) * k / n, from.y + (p.y - from.y) * k / n})) clear = false;
        length += std::hypot(p.x - from.x, p.y - from.y);
        from = p;
    }
    round = round && std::hypot(from.x - r.x, from.y - r.y) < 1;
    check(round && clear, where + ": a walk from one side to the other goes round it");
    check(length > std::hypot(r.x - l.x, r.y - l.y) + 1, where + ": and is longer than straight across");
    const auto& ring = hs.lap();
    bool lap_clear = true;
    for (size_t i = 0; i < ring.size(); i++) {
        const Point& p = ring[i];
        const Point& q = ring[(i + 1) % ring.size()];
        const int n = std::max(1, int(std::hypot(q.x - p.x, q.y - p.y)));
        for (int k = 0; k <= n; k++)
            if (in_hole({p.x + (q.x - p.x) * k / n, p.y + (q.y - p.y) * k / n})) lap_clear = false;
    }
    check(lap_clear, where + ": a lap stays clear of it");

    // Frame by frame: wandering never ends up in it; a walk across goes round it without stopping or turning back
    // at its corners; a spot asked for inside it is walked to the nearest open point; home and the desk stay reachable.
    Pet w;
    w.set_display(d);
    w.set_egg(&holed);
    uint32_t t = 0;
    w.want(Look::Wander, t);
    bool never = true;
    int moved = 0;
    float px = w.x(), py = w.y();
    for (; t < 6 * 60000; t += FRAME_MS) {
        w.update(t);
        if (in_hole({w.x(), w.y()})) never = false;
        if (w.x() != px || w.y() != py) moved++;
        px = w.x(), py = w.y();
    }
    check(moved > 1000 && never, where + ": six minutes of wandering never stand in it");

    std::vector<float> progress;   // how far along its way each walking frame took it
    auto walk_to = [&](Pet& pet, Point to, uint32_t& clock, std::vector<Point>* track) {
        Act a;
        a.anim = "Idle";
        a.spot = {Spot::Kind::Point, to.x, to.y};
        pet.act(a);
        pet.want(Look::Act, clock);
        for (uint32_t end = clock + ARRIVE_MS; clock < end && pet.phase() != Pet::Phase::Acting; clock += FRAME_MS) {
            const float before = pet.route_left();
            pet.update(clock);
            // from its first step: before that it may turn on the spot to face its way
            const bool moved = track && (track->size() > 1 || pet.x() != track->back().x || pet.y() != track->back().y);
            if (track && pet.phase() == Pet::Phase::Walking && moved) {
                track->push_back({pet.x(), pet.y()});
                progress.push_back(before - pet.route_left());
            }
        }
        return pet.phase() == Pet::Phase::Acting;
    };
    Pet m;
    m.set_display(d);
    m.set_egg(&holed);
    t = 0;
    m.want(Look::Idle, t);
    for (int i = 0; i < 100; i++, t += FRAME_MS) m.update(t);
    const float step = (holed.pace > 0 ? holed.pace : WALK_PX_S) * FRAME_MS / 1000;
    for (const auto& [from, to] : {std::pair<Point, Point>{l, r}, std::pair<Point, Point>{r, l}}) {
        check(walk_to(m, from, t, nullptr), where + ": it walks to one side of it");
        std::vector<Point> track{{m.x(), m.y()}};
        progress.clear();
        const bool there = walk_to(m, to, t, &track);
        bool through = false, stopped = false, back = false;
        // speed is measured along its way: at a corner the straight line between two frames is shorter
        for (size_t i = 0; i + 1 < progress.size(); i++) stopped = stopped || progress[i] < step * 0.9f;
        for (size_t i = 1; i < track.size(); i++) {
            through = through || in_hole(track[i]);
            const float vx = track[i].x - track[i - 1].x, vy = track[i].y - track[i - 1].y;
            if (i >= 2) {
                const float ux = track[i - 1].x - track[i - 2].x, uy = track[i - 1].y - track[i - 2].y;
                if (ux * vx + uy * vy < 0) back = true;
            }
        }
        check(there && !through, where + ": it walks across round it, never through");
        check(!stopped && !back, where + ": without stopping or turning back at its corners");
    }
    const bool asked = walk_to(m, c, t, nullptr);
    const float off = std::hypot(m.x() - c.x, m.y() - c.y);
    check(asked && !in_hole({m.x(), m.y()}) && off <= (HALF + SIDE + 2) * s,
          where + ": a spot inside it is walked to the nearest open point (" + std::to_string(off) + " px off)");
    const Pixel home = hs.home();
    check(!hs.path(l.x, l.y, home.x, home.y).empty() && !hs.path(r.x, r.y, home.x, home.y).empty(),
          where + ": home is reachable from both sides");
    m.want(Look::Type, t);
    for (uint32_t end = t + ARRIVE_MS; t < end && m.phase() != Pet::Phase::Acting; t += FRAME_MS) m.update(t);
    check(m.phase() == Pet::Phase::Acting && m.look() == Look::Type, where + ": and it still gets to its desk");
}

// A wall across the floor (a blocked strip from top to bottom) leaves the far side standable but cut off from
// home: wandering never goes there, a spot asked for there is walked to the nearest point on home's side, laps
// stay on home's side.
void cut_off_floor(const Egg& egg, const Display& d) {
    if (!egg.has_scene || egg.scene.floor.empty()) return;
    Pet open;
    open.set_display(d);
    open.set_egg(&egg);
    const Stage& st = open.stage();
    const int s = egg.scene.scale;
    const int ox = st.scene_x(), oy = st.scene_y();      // scene pixels to screen pixels, as the stage fits it
    // the column of scene pixels farthest from home that still leaves standable floor beyond it
    int wall = -1, far_side = 0;
    for (int dir : {1, -1})
        for (int x = egg.scene.home_x + dir * 12; x > 2 && x < egg.scene.w - 2 && wall < 0; x += dir) {
            int beyond = 0;
            for (int y = 0; y < egg.scene.h; y += 2)
                for (int bx = x + dir * 3; bx > 0 && bx < egg.scene.w; bx += dir * 2)
                    if (st.can_stand(float(bx * s - ox), float(y * s - oy))) beyond++;
            if (beyond >= 6 && beyond < 40) { wall = x; far_side = dir; }
        }
    const std::string where = tag(egg, d) + ", a wall across the floor";
    if (!check(wall >= 0, where + ": its floor has room for one")) return;
    Egg walled = test::reloaded(egg);
    const int half = std::max(1, 2 / s);                 // the wall is 4 screen pixels wide at any scene scale
    walled.scene.blocked.push_back({{wall - half, -50}, {wall + half, -50}, {wall + half, egg.scene.h + 50},
                                    {wall - half, egg.scene.h + 50}});
    Pet pet;
    pet.set_display(d);
    pet.set_egg(&walled);
    const Stage& ws = pet.stage();
    auto beyond = [&](Point p) { return far_side > 0 ? p.x > (wall + half) * s - ox : p.x < (wall - half) * s - ox; };
    Point island{-1, -1};
    for (int y = 0; y < d.h && island.x < 0; y += 2)
        for (int x = 0; x < d.w; x += 2)
            if (beyond({float(x), float(y)}) && ws.can_stand(x, y)) { island = {float(x), float(y)}; break; }
    if (!check(island.x >= 0, where + ": there is floor beyond it")) return;
    check(!ws.reachable(island.x, island.y), where + ": the floor beyond it is cut off from home");
    const Point near = ws.nearest_standable(island);
    check(ws.reachable(near.x, near.y) && !beyond(near), where + ": its nearest reachable point is on home's side");
    uint32_t t = 0;
    pet.want(Look::Wander, t);
    bool never = true;
    for (; t < 3 * 60000; t += FRAME_MS) {
        pet.update(t);
        if (beyond({pet.x(), pet.y()})) never = false;
    }
    check(never, where + ": three minutes of wandering never cross it");
    Act a;
    a.anim = "Idle";
    a.spot = {Spot::Kind::Point, island.x, island.y};
    pet.act(a);
    pet.want(Look::Act, t);
    for (uint32_t end = t + ARRIVE_MS; t < end && pet.phase() != Pet::Phase::Acting; t += FRAME_MS) pet.update(t);
    check(pet.phase() == Pet::Phase::Acting && !beyond({pet.x(), pet.y()}) &&
              std::hypot(pet.x() - near.x, pet.y() - near.y) < 3,
          where + ": a spot asked for beyond it is walked to that nearest point");
    bool lap_ok = true;
    for (const Point& p : ws.lap()) lap_ok = lap_ok && !beyond(p);
    check(lap_ok, where + ": a lap stays on home's side");
}

// The floor it may go to is the part joined to its home. On a rectangular screen that is the whole floor. A round
// screen's edge can cut a piece off (it would have to walk off the screen to get there): that piece is reported,
// and it is never chosen: a place asked for there resolves to the nearest point it can reach.
void floor_joined_to_home(const Egg& egg, const Display& d) {
    Pet pet;
    pet.set_display(d);
    pet.set_egg(&egg);
    const Stage& st = pet.stage();
    int cut_off = 0;
    bool resolves = true;
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++)
            if (st.can_stand(x, y) && !st.reachable(x, y)) {
                cut_off++;
                if (cut_off % 25 == 1) {
                    const Point p = st.nearest_standable({float(x), float(y)});
                    resolves = resolves && st.reachable(p.x, p.y);
                }
            }
    if (!d.round()) {
        check(cut_off == 0, tag(egg, d) + ": its whole floor is joined to home (" + std::to_string(cut_off) +
                                " points cut off)");
        return;
    }
    if (cut_off)
        std::printf("%-22s %d points of floor cut off from home by the round screen's edge; never chosen\n",
                    tag(egg, d).c_str(), cut_off);
    check(resolves, tag(egg, d) + ": a place asked for on a cut-off piece resolves to one it can reach");
}

// A kit's release point is where the item leaves while the throw is drawn: the point of the frame's topmost row
// gives the top of the head again, a point further in moves it by as many of the pet's pixels; other animations
// keep the top of the head.
void release_point(const Egg& egg, const Display& d) {
    const std::string where = tag(egg, d) + ", a release point";
    Egg kit = kit_egg(egg, d, Flight::Toss, std::nullopt);
    auto throwing = [&](const Egg& e, Point& from, const FrameBox** box, int& u) {
        Pet pet;
        pet.set_display(d);
        pet.set_egg(&e);
        Canvas screen(d.w, d.h);
        uint32_t t = 1000;
        pet.want(Look::Idle, t);
        pet.update(t);
        pet.render(screen, t);
        const Point idle = pet.item_from();
        Act a;
        a.kind = Act::Kind::Throw;
        pet.act(a);
        pet.want(Look::Act, t);
        uint32_t start = 0;
        for (; t < 20000 && !start; t += FRAME_MS) {
            pet.update(t);
            if (pet.phase() == Pet::Phase::Acting) start = t;
        }
        const Anim& thrower = *e.find("Throw");
        const uint32_t held = start + ticks_to_ms(test::release_ticks(thrower) + 2);  // in the air, the last frame held
        pet.update(held);
        pet.render(screen, held);
        from = pet.item_from();
        if (box) *box = &thrower.box(pet.drawn_row(), thrower.frames - 1);
        u = pet.stage().unit();
        return idle;
    };
    Point head, from;
    const FrameBox* box = nullptr;
    int u = 1;
    const Point idle = throwing(kit, head, &box, u);
    if (!check(box && box->w > 0, where + ": the throw's last frame is drawn")) return;
    FrameCache cache;
    box = &pixels_of(*box, cache);
    int lo = -1, hi = -1;
    for (int sx = 0; sx < box->w; sx++)
        if (box->px[sx]) {
            if (lo < 0) lo = sx;
            hi = sx;
        }
    kit.throw_kit.release_x = box->x + (lo + hi) / 2;
    kit.throw_kit.release_y = box->y;
    const Point idle_too = throwing(kit, from, nullptr, u);
    check(std::abs(from.x - head.x) <= u && std::abs(from.y - head.y) <= u,
          where + ": at the top row's middle it leaves from the top of the head");
    check(idle_too.x == idle.x && idle_too.y == idle.y, where + ": other animations keep the top of the head");
    const Point at = from;
    kit.throw_kit.release_x += 5;
    kit.throw_kit.release_y += 7;
    throwing(kit, from, nullptr, u);
    check(from.x - at.x == 5 * u && from.y - at.y == 7 * u, where + ": 5 and 7 further in, it leaves 5 and 7 of its "
                                                                     "pixels further");
}

// A toss from near the top of the screen rises only as far as there is room.
void toss_stays_below_the_top() {
    for (int u : {1, 2}) {
        for (float top : {2.0f, 10.0f, 40.0f, 120.0f}) {
            const Point from{100, top}, hands{100, top + 20};
            float highest = 1e9f;
            for (int k = 0; k < TOSS_TICKS; k++)
                if (const auto p = item_at(Flight::Toss, k, from, {}, hands, u)) highest = std::min(highest, p->y);
            check(highest >= std::min(top, 4.0f * u) - 0.5f, "a toss from y " + std::to_string(int(top)) +
                                                                   " stays on screen (highest " +
                                                                   std::to_string(highest) + ")");
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    toss_stays_below_the_top();
    if (argc < 2) {
        std::fprintf(stderr, "usage: path_test EGG...\n");
        return 2;
    }
    for (int i = 1; i < argc; i++) {
        Egg loaded;
        if (!test::load_egg_file(argv[i], loaded)) return 1;
        // the throw is checked with test kits; these checks want one that can't
        const Egg egg = test::without_throw(loaded);
        for (auto& d : displays()) {
            edge_rows_match(egg, d);
            paths_home(egg, d);
            watches_its_screen(egg, d);
            never_rests_behind_laptop(egg, d);
            throw_arcs(egg, d);
            throw_on_screen(egg, d);
            release_point(egg, d);
            throws_while_typing_fast(egg, d);
            plays_in_free_time(egg, d);
            pastimes_in_free_time(egg, d);
            swims_in_free_time(loaded, d);
            one_play_ends_on_its_last_frame(egg, d);
            watches_from_its_spot(egg, d);
            typing_shows_on_laptop(egg, d);
            walks_round_a_hole(egg, d);
            floor_joined_to_home(egg, d);
            cut_off_floor(egg, d);
        }
    }
    return test::summary();
}
