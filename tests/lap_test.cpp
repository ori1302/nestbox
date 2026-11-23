// Laps (Play::lap), frame by frame with a real egg and its own Zoomies run (or its Walk as one): the lap stays on
// standable ground and closes, celebrating runs from where the pet stands at three times its walking pace (its own pace
// when the egg has one) and comes back there within the reaction (with no time for a lap, it plays in place), a party
// runs, rests and runs again, a new look takes over mid-lap without a jump, and pets without an 8-row Zoomies play in
// place; the guide for agents says which. A step's laps run as many rings as asked and are then done; with no lap to
// run they are done at once. A step's laps on its walk face the way it runs, at 1.8 times its pace, in step.
// Usage: lap_test EGG...
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include "body/parts.h"
#include "body/pet.h"
#include "body/plays.h"
#include "check.h"

using namespace nestbox;
using test::check;

namespace {

constexpr uint32_t FRAME_MS = 16;
constexpr float PACED = 40;              // a slower pet's own pace (pet.json "pace")
constexpr uint32_t LAP_SPARE_MS = 600;   // a reaction's laps leave this much of it unused

// The longest step of a frame at three times the pet's walking pace.
float max_step(const Egg& e) { return (e.pace > 0 ? e.pace : WALK_PX_S) * 3 * FRAME_MS / 1000 + 1; }

// The egg with its own Zoomies run, or, if it has none, its Walk under that name; with `rows` of its facings.
Egg with_zoomies(const Egg& e, int rows) {
    Egg z = test::reloaded(e);
    Anim run = z.find("Zoomies") ? *z.find("Zoomies") : *z.find("Walk");
    z.anims.erase(std::remove_if(z.anims.begin(), z.anims.end(), [](const Anim& a) { return a.name == "Zoomies"; }),
                  z.anims.end());
    run.name = "Zoomies";
    if (rows < run.rows) {
        run.boxes.resize(size_t(rows) * run.frames);
        run.rows = rows;
    }
    z.anims.push_back(run);
    return z;
}

// The egg without a Zoomies run.
Egg without_zoomies(const Egg& e) {
    Egg z = test::reloaded(e);
    z.anims.erase(std::remove_if(z.anims.begin(), z.anims.end(), [](const Anim& a) { return a.name == "Zoomies"; }),
                  z.anims.end());
    return z;
}

struct Run {
    Pet pet;
    Canvas screen;
    uint32_t t = 1000;
    float max_step = 0;
    explicit Run(const Display& d, const Egg& e) : screen(d.w, d.h) {
        pet.set_display(d);
        pet.set_egg(&e);
        settle();
    }
    void frame() {
        const float x = pet.x(), y = pet.y();
        pet.update(t);
        pet.render(screen, t);
        max_step = std::max(max_step, std::hypot(pet.x() - x, pet.y() - y));
        t += FRAME_MS;
    }
    void settle() {
        pet.want(Look::Idle, t);
        for (int i = 0; i < 200; i++) frame();
    }
    bool running() const { return pet.playing().find("Zoomies") != std::string::npos; }
};

void lap_is_standable(const Stage& st, const std::string& where) {
    const auto& ring = st.lap();
    bool on = true, clear = true;
    for (size_t i = 0; i < ring.size(); i++) {
        const Point& p = ring[i];
        const Point& q = ring[(i + 1) % ring.size()];
        on = on && st.can_stand(p.x, p.y) && !st.hidden(p.x, p.y);
        clear = clear && st.line_clear(p.x, p.y, q.x, q.y);
    }
    check(on, where + ": every corner of the lap is standable");
    check(clear, where + ": the lap's sides cross only standable ground");
}

// One lap asked of the pet where it settles, from the asking until it is back: what a reaction's laps take.
uint32_t one_lap_ms(const Display& d, const Egg& z) {
    Run r(d, z);
    Act a;
    a.kind = Act::Kind::Laps;
    r.pet.act(a);
    const uint32_t start = r.t;
    r.pet.want(Look::Act, r.t);
    while (!r.pet.act_done(r.t) && r.t - start < 60000) r.frame();
    return r.t - start;
}

// Celebrating runs its laps when one fits in the reaction, else it plays in place.
void celebrate(const Display& d, const Egg& z, const std::string& where) {
    Run r(d, z);
    const float x0 = r.pet.x(), y0 = r.pet.y();
    r.pet.want(Look::Celebrate, r.t);
    const uint32_t length = info(Look::Celebrate).length_ms;
    std::set<int> rows;
    float far = 0;
    bool ran = false, back = false;
    for (uint32_t end = r.t + length; r.t < end;) {
        r.frame();
        if (r.running()) {
            ran = true;
            rows.insert(r.pet.row());
        }
        far = std::max(far, std::hypot(r.pet.x() - x0, r.pet.y() - y0));
        if (ran && !r.running() && r.pet.phase() == Pet::Phase::Acting) back = true;
    }
    if (!ran) {
        const uint32_t need = one_lap_ms(d, z);
        check(need + LAP_SPARE_MS > length, where + ": it plays in place only when a lap does not fit (" +
                                                std::to_string(need) + " of " + std::to_string(length) + " ms)");
        check(far < 1, where + ": playing in place, it stays where it is");
        return;
    }
    const auto& ring = r.pet.stage().lap();
    float half = 0;
    for (auto& p : ring) half = std::max(half, std::abs(p.x - ring[0].x) / 2);
    check(rows.size() >= 6, where + ": the run turns through its facings (" + std::to_string(rows.size()) + ")");
    check(far >= half, where + ": it really leaves its spot");
    check(back, where + ": the laps end within the reaction");
    check(std::hypot(r.pet.x() - x0, r.pet.y() - y0) <= 2.5f, where + ": it ends where it started");
    const float most = max_step(z);
    check(r.max_step <= most, where + ": no step longer than three times walking (" +
                                  std::to_string(r.max_step) + " px)");
    check(r.max_step >= most - 2.5f, where + ": it runs at three times its walking pace");
}

void party(const Display& d, const Egg& z, const std::string& where) {
    Run r(d, z);
    r.pet.want(Look::Party, r.t);
    int runs = 0;
    bool was = false, rested = false;
    for (int i = 0; i < 900 && runs < 2; i++) {
        r.frame();
        const bool now = r.running();
        if (now && !was) runs++;
        if (!now && runs == 1 && r.pet.phase() == Pet::Phase::Acting) rested = true;
        was = now;
    }
    check(runs == 2 && rested, where + ": a party runs a lap, rests, and runs again");
    for (int i = 0; i < 20; i++) r.frame();
    const float x = r.pet.x(), y = r.pet.y();
    r.max_step = 0;
    r.pet.want(Look::Idle, r.t);
    r.frame();
    check(std::hypot(r.pet.x() - x, r.pet.y() - y) <= max_step(z), where + ": a new look takes over where it is");
    for (int i = 0; i < 300; i++) r.frame();
    check(!r.running(), where + ": and the laps stop");
}

void plays_in_place(const Display& d, const Egg& e, const std::string& where) {
    Run r(d, e);
    const float x0 = r.pet.x(), y0 = r.pet.y();
    for (Look s : {Look::Celebrate, Look::Party}) {
        r.pet.want(s, r.t);
        float far = 0;
        bool ran = false;
        for (int i = 0; i < 260; i++) {
            r.frame();
            ran = ran || r.running();
            far = std::max(far, std::hypot(r.pet.x() - x0, r.pet.y() - y0));
        }
        check(!ran && far < 1, where + ": " + look_key(s) + " plays in place");
    }
}

// A step's laps (Act::Kind::Laps): it runs as many as asked from where it stands, back to the same spot, and only
// then is the act done; each extra lap adds one ring's length to the run.
void asked_laps(const Display& d, const Egg& z, const std::string& where) {
    auto run = [&](int count, float& travelled, bool& early, float& off) {
        Run r(d, z);
        const float x0 = r.pet.x(), y0 = r.pet.y();
        Act a;
        a.kind = Act::Kind::Laps;
        a.count = count;
        r.pet.act(a);
        r.pet.want(Look::Act, r.t);
        travelled = 0;
        early = false;
        bool done = false;
        for (int i = 0; i < 6000 && !done; i++) {
            const float x = r.pet.x(), y = r.pet.y();
            r.frame();
            if (r.pet.phase() == Pet::Phase::Acting) travelled += std::hypot(r.pet.x() - x, r.pet.y() - y);
            done = r.pet.act_done(r.t);
            if (r.running() && done) early = true;
        }
        off = std::hypot(r.pet.x() - x0, r.pet.y() - y0);
        return done;
    };
    const Run probe(d, z);
    const auto& ring = probe.pet.stage().lap();
    float perimeter = 0;
    for (size_t i = 0; i < ring.size(); i++)
        perimeter += std::hypot(ring[(i + 1) % ring.size()].x - ring[i].x, ring[(i + 1) % ring.size()].y - ring[i].y);
    float one = 0, three = 0, off1 = 0, off3 = 0;
    bool early1 = false, early3 = false;
    const bool done1 = run(1, one, early1, off1), done3 = run(3, three, early3, off3);
    check(done1 && done3 && !early1 && !early3, where + ": a step's laps are done once they are run, not before");
    check(off1 <= 2.5f && off3 <= 2.5f, where + ": and end where they began");
    check(std::abs((three - one) - 2 * perimeter) <= 0.1f * 2 * perimeter,
          where + ": three laps run two rings more than one (" + std::to_string(int(three - one)) + " px, a ring is " +
              std::to_string(int(perimeter)) + ")");
}

// Laps on its walk (no run of its own): it faces the way it runs all the way round, goes a little under twice its
// walking pace, and its walk keeps step with the ground: as many walk cycles as the distance at its own pace.
void walk_laps(const Display& d, const Egg& plain, const std::string& where) {
    Run r(d, plain);
    Act a;
    a.kind = Act::Kind::Laps;
    a.count = 1;
    r.pet.act(a);
    r.pet.want(Look::Act, r.t);
    const float pace = plain.pace > 0 ? plain.pace : WALK_PX_S;
    const Anim* walk = plain.find("Walk");
    const float cycle_ms = float(ticks_to_ms(walk->total_ticks()));
    int moved = 0, facing_off = 0, facing_far = 0, wraps = 0, last = -1;
    float travelled = 0, fastest = 0;
    for (int i = 0; i < 6000 && !r.pet.act_done(r.t); i++) {
        const float x = r.pet.x(), y = r.pet.y();
        r.frame();
        const float dx = r.pet.x() - x, dy = r.pet.y() - y, step = std::hypot(dx, dy);
        if (r.pet.phase() != Pet::Phase::Acting || step < 0.5f) continue;
        moved++;
        travelled += step;
        fastest = std::max(fastest, step);
        const int apart = turns_apart(r.pet.row(), row_toward(dx, dy));   // a frame over a corner: one facing apart
        facing_off += apart > 0;
        facing_far += apart > 1;
        const int f = r.pet.drawn_frame();
        if (last >= 0 && f < last) wraps++;
        last = f;
    }
    check(moved > 20 && facing_far == 0 && facing_off * 10 <= moved,
          where + ": round its lap it faces the way it runs (" + std::to_string(facing_off) + " of " +
              std::to_string(moved) + " frames a facing off, " + std::to_string(facing_far) + " more)");
    const float per_frame = pace * 1.8f * FRAME_MS / 1000;
    check(fastest <= per_frame + 1 && fastest >= per_frame * 0.8f,
          where + ": at 1.8 times its walking pace (" + std::to_string(fastest) + " px a frame)");
    const float cycles = travelled / pace * 1000 / cycle_ms;
    check(std::abs(float(wraps) - cycles) <= 1.5f, where + ": its walk keeps step with the ground (" +
                                                        std::to_string(wraps) + " cycles over " +
                                                        std::to_string(cycles) + " cycles' distance)");
}

// With no lap to run (no room for a ring, or no eight-facing run), a step's laps are done at once, in place.
void laps_without_a_ring(const Display& d, const Egg& e, const std::string& where) {
    Run r(d, e);
    const float x0 = r.pet.x(), y0 = r.pet.y();
    Act a;
    a.kind = Act::Kind::Laps;
    a.count = 3;
    r.pet.act(a);
    r.pet.want(Look::Act, r.t);
    const uint32_t start = r.t;
    bool done = false, ran = false;
    for (int i = 0; i < 200 && !done; i++) {
        r.frame();
        ran = ran || r.running();
        done = r.pet.act_done(r.t);
    }
    check(done && !ran && r.t - start <= 1500 && std::hypot(r.pet.x() - x0, r.pet.y() - y0) < 1,
          where + ": laps it cannot run are done at once, in place (" + std::to_string(r.t - start) + " ms)");
}

// The egg's floor cut down to a small square round its home: no room for a lap.
Egg cramped(const Egg& e) {
    Egg c = test::reloaded(e);
    const int hx = c.scene.home_x >= 0 ? c.scene.home_x : c.scene.w / 2,
              hy = c.scene.home_y >= 0 ? c.scene.home_y : c.scene.h / 2;
    c.scene.floor = {{hx - 10, hy - 6}, {hx + 10, hy - 6}, {hx + 10, hy + 6}, {hx - 10, hy + 6}};
    c.scene.blocked.clear();
    return c;
}

// The egg with neither a Zoomies run nor an eight-facing Walk.
Egg one_row_walk(const Egg& e) {
    Egg c = without_zoomies(e);
    for (Anim& a : c.anims)
        if (a.name == "Walk") {
            a.boxes.resize(size_t(a.frames));
            a.rows = 1;
        }
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: lap_test EGG...\n");
        return 2;
    }
    int laps = 0;
    for (int a = 1; a < argc; a++) {
        Egg egg;
        if (!test::load_egg_file(argv[a], egg)) return 1;
        if (!egg.find("Walk") || egg.find("Walk")->rows < FACING_ROWS) continue;
        const Anim* own = egg.find("Zoomies");
        if (own && own->rows < FACING_ROWS) continue;     // its own one-row Zoomies: it plays in place, not laps
        const Egg z = with_zoomies(egg, FACING_ROWS), one_row = with_zoomies(egg, 1), plain = without_zoomies(egg);
        for (Look s : {Look::Party, Look::Celebrate}) {
            const PlayChoice c = play_for(z, info(s)), c1 = play_for(one_row, info(s));
            check(c.found && std::string(c.play.anim) == "Zoomies" && c.play.lap && c.own,
                  egg.name + ": the guide says " + look_key(s) + " runs its own laps");
            check(c1.found && std::string(c1.play.anim) != "Zoomies",
                  egg.name + ": the guide passes over a one-row Zoomies for " + look_key(s));
        }
        for (auto& d : displays()) {
            const std::string where = egg.name + " on " + d.id;
            plays_in_place(d, plain, where + " without Zoomies");
            plays_in_place(d, one_row, where + " with a one-row Zoomies");
            const Egg flat = one_row_walk(egg);
            laps_without_a_ring(d, flat, where + " with no eight-facing run");
            if (egg.has_scene) {
                const Egg small = cramped(z);
                Pet tight;
                tight.set_display(d);
                tight.set_egg(&small);
                if (check(tight.stage().lap().empty(), where + ": a cramped floor has no lap"))
                    laps_without_a_ring(d, small, where + ", a cramped floor");
            }
            Pet probe;
            probe.set_display(d);
            probe.set_egg(&z);
            if (!probe.stage().lap().empty()) walk_laps(d, plain, where + " on its walk");
            if (probe.stage().lap().empty()) {
                plays_in_place(d, z, where + ", no room for a lap");
                laps_without_a_ring(d, z, where + ", no room for a lap");
                continue;
            }
            laps++;
            asked_laps(d, z, where);
            lap_is_standable(probe.stage(), where);
            celebrate(d, z, where);
            party(d, z, where);
            Egg slow = with_zoomies(egg, FACING_ROWS);
            slow.pace = PACED;
            celebrate(d, slow, where + " at its own pace");
        }
    }
    check(laps > 0, "some egg on some screen has room for a lap");
    return test::summary();
}
