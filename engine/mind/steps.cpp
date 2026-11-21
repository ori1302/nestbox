#include "mind/steps.h"
#include <algorithm>
#include "mind/note.h"
#include "util/deadline.h"
#include "util/text.h"

namespace nestbox {

namespace {
constexpr double MAX_PERCENT = 1000;   // points are cut to this, far beyond any scene
constexpr float SLOW = 0.5f, FAST = 2.0f;

struct FaceName {
    const char* name;
    int row;
};
constexpr FaceName FACES[] = {
    {"front", 0}, {"front-right", 1}, {"right", 2}, {"back-right", 3},
    {"back", 4}, {"back-left", 5}, {"left", 6}, {"front-left", 7},
};

struct Word {
    const char* word;
    const char* means;
    Act::Kind kind;
    Look look;
    bool ends;                           // it ends by itself
};
const Word WORDS[] = {
    {"stand", "stands around", Act::Kind::Look, Look::Idle, false},
    {"stroll", "wanders about its floor", Act::Kind::Wander, Look::Wander, false},
    {"run", "runs a lap of its floor", Act::Kind::Laps, Look::Wander, true},
    {"sleep", "sleeps, at home", Act::Kind::Look, Look::Sleep, false},
    {"type", "works at its desk", Act::Kind::Look, Look::Type, false},
    {"watch", "watches its desk", Act::Kind::Look, Look::Watch, false},
    {"eat", "eats, at its desk", Act::Kind::Look, Look::Eat, true},
    {"wave", "tries to catch the user's eye", Act::Kind::Look, Look::Alert, true},
    {"celebrate", "celebrates", Act::Kind::Look, Look::Celebrate, true},
    {"sulk", "looks down", Act::Kind::Look, Look::Slump, false},
    {"dance", "sways", Act::Kind::Look, Look::Dance, false},
    {"throw", "throws its thing at the aim", Act::Kind::Throw, Look::Idle, true},
};
constexpr int EAT_RUNS = 3;              // eating is a few bites, not one

// "at", "aim" or "face": [x, y] in percent of the scene, or a place by name. False if it names nothing it knows.
bool read_spot(const Fields& f, const std::string& k, Spot& out) {
    out = Spot{};
    if (f.has(k + ".0") && f.has(k + ".1")) {
        out = {Spot::Kind::Scene, float(f.num_in(k + ".0", -MAX_PERCENT, MAX_PERCENT, 0)),
               float(f.num_in(k + ".1", -MAX_PERCENT, MAX_PERCENT, 0))};
        return true;
    }
    const std::string place = ascii_lower(f.str(k));
    if (place == "home") out.kind = Spot::Kind::Home;
    else if (place == "desk") out.kind = Spot::Kind::Desk;
    else if (place == "target") out.kind = Spot::Kind::Target;
    else if (place != "here") return false;
    return true;
}

const char* PLACES = "\"here\", \"home\", \"desk\", \"target\" or a point [x, y] in percent of the scene";
}  // namespace

std::string Step::words() const {
    if (!word.empty()) return word;
    return act.kind == Act::Kind::Anim && act.anim.empty() ? "walk" : act.anim;
}

const std::vector<Behaviour>& behaviours() {
    static const std::vector<Behaviour> all = [] {
        std::vector<Behaviour> v;
        for (const Word& w : WORDS) v.push_back({w.word, w.means, w.look, w.kind == Act::Kind::Look});
        return v;
    }();
    return all;
}

std::optional<int> facing_row(const std::string& face) {
    for (auto& f : FACES)
        if (face == f.name) return f.row;
    return std::nullopt;
}

const std::vector<std::string>& directions() {
    static const std::vector<std::string> all = [] {
        std::vector<std::string> v;
        for (auto& f : FACES) v.push_back(f.name);
        return v;
    }();
    return all;
}

bool StepChain::parse(const Fields& f, Step& out, std::vector<std::string>& warnings) {
    out = Step{};
    Act& act = out.act;
    bool asks = false, ends_itself = false;
    const std::string word = f.str("do");           // behaviours are lower case; anything else names an animation
    if (f.has("do") && word != "false" && !word.empty()) {
        asks = true;
        out.word = word;
        const Word* known = nullptr;
        for (const Word& w : WORDS)
            if (word == w.word) known = &w;
        if (known) {
            act.kind = known->kind;
            act.look = known->look;
            ends_itself = known->ends;
        } else {                                       // one of the pet's own animations
            act.kind = Act::Kind::Anim;
            act.anim = word;
            ends_itself = true;
        }
    }
    if (f.has("at") || f.has_object("at")) {
        if (read_spot(f, "at", act.spot)) asks = asks || act.spot.kind != Spot::Kind::Here;
        else warnings.push_back("unknown place \"" + f.str("at") + "\" for \"at\"; use " + PLACES);
    }
    if (f.has_object("through")) {
        for (size_t i = 0;; i++) {
            const std::string k = "through." + std::to_string(i);
            if (!f.has(k + ".0") && !f.has_object(k)) break;
            Spot p;
            if (i >= Pet::MAX_THROUGH) {
                warnings.push_back("\"through\" takes at most " + std::to_string(Pet::MAX_THROUGH) + " points");
                break;
            }
            if (read_spot(f, k, p) && p.kind == Spot::Kind::Scene) act.through.push_back(p);
            else warnings.push_back("\"through\" is a list of points [x, y] in percent of the scene");
        }
        if (act.spot.kind == Spot::Kind::Here) {
            warnings.push_back("\"through\" curves the way to \"at\"; without one it was left out");
            act.through.clear();
        }
    }
    if (f.has("moving")) act.moving = f.str("moving");
    if (f.has("spin")) {
        const double turns = f.num("spin", -1);
        if (turns >= 0) act.spin = float(std::min(turns, double(Pet::MAX_SPIN)));
        else warnings.push_back("\"spin\" is turns a second while it moves, such as 1.5");
    }
    if ((f.has("moving") || f.has("spin")) && act.spot.kind == Spot::Kind::Here) {
        warnings.push_back("\"moving\" and \"spin\" are for the way to \"at\"; without one they were left out");
        act.moving.clear();
        act.spin = 0;
    }
    if (f.has("aim") || f.has_object("aim")) {
        if (!read_spot(f, "aim", act.aim))
            warnings.push_back("unknown place \"" + f.str("aim") + "\" for \"aim\"; use " + PLACES);
        if (act.kind != Act::Kind::Throw) warnings.push_back("\"aim\" is for a throw; it was left out");
    }
    if (f.has("face") || f.has_object("face")) {
        const std::string face = ascii_lower(f.str("face"));
        if (const std::optional<int> row = facing_row(face)) {
            act.row = *row;
            asks = true;
        } else if (read_spot(f, "face", act.face) && act.face.kind != Spot::Kind::Here) {
            asks = true;
        } else {
            warnings.push_back("unknown direction \"" + f.str("face") +
                               "\"; use front, front-right, right, back-right, back, back-left, left, front-left, a "
                               "place or a point");
        }
    }
    if (f.has("speed")) {
        const std::string sp = ascii_lower(f.str("speed"));
        if (sp == "slow") act.speed = SLOW;
        else if (sp == "fast") act.speed = FAST;
        else if (f.num("speed", -1) > 0) {
            const double v = f.num("speed", 1);
            act.speed = float(std::clamp(v, double(Pet::MIN_ACT_SPEED), double(Pet::MAX_ACT_SPEED)));
            if (v != act.speed)
                warnings.push_back("\"speed\" was brought to " + std::to_string(act.speed).substr(0, 4));
        } else {
            warnings.push_back("\"speed\" is \"slow\", \"fast\" or a factor such as 1.5");
        }
    }
    out.say = utf8_cut(f.str("say") == "false" ? "" : f.str("say"), Note::MAX_SAY);
    if (f.has("say") && utf8_cut(f.str("say"), 1000).size() > Note::MAX_SAY)
        warnings.push_back("a step's \"say\" was cut to " + std::to_string(Note::MAX_SAY) + " characters");
    if (f.has("sign") && f.str("sign") != "false" && !sign_named(ascii_lower(f.str("sign")), out.mark))
        warnings.push_back("unknown sign \"" + f.str("sign") + "\"; the signs are listed at /v1/words");
    if (!asks) return false;

    int times = 1;
    if (f.has("times")) {
        times = std::clamp(f.integer("times", 1), 1, Step::MAX_COUNT);
        if (f.num("times", 1) != times) warnings.push_back("\"times\" was brought to " + std::to_string(times));
    }
    const std::optional<uint32_t> for_ms = f.has("for") ? duration_ms(f.str("for")) : std::nullopt;
    if (f.has("for") && !for_ms) warnings.push_back(FOR_WARNING);
    act.count = times;
    if (for_ms) {
        out.ends = Step::Ends::Time;
        out.ms = *for_ms;
    } else if (act.kind == Act::Kind::Laps || act.kind == Act::Kind::Throw) {
        out.ends = Step::Ends::Act;
    } else if (f.has("times") || ends_itself) {
        out.ends = Step::Ends::Runs;
        out.runs = f.has("times") ? times : (act.look == Look::Eat && act.kind == Act::Kind::Look ? EAT_RUNS : 1);
        if (act.kind == Act::Kind::Anim) act.plays = out.runs;
    } else {
        out.open_ended = true;                         // stand, stroll, sleep, a walk...: the note decides how long
    }
    const bool walk_only = act.kind == Act::Kind::Anim && act.anim.empty();
    out.waypoint = walk_only && act.row < 0 && act.face.kind == Spot::Kind::Here && !for_ms && out.say.empty() &&
                   out.mark == Bubble::None;
    return true;
}

void StepChain::start(std::vector<Step> steps, uint32_t now, bool loop) {
    steps_ = std::move(steps);
    loop_ = loop;
    finished_ = steps_.empty();
    enter(0, now);
}

void StepChain::enter(size_t index, uint32_t now) {
    const bool same_part = index > 0 && index == index_ + 1 && !steps_[index].show.empty() &&
                           steps_[index].show == steps_[index_].show;
    if (!same_part) shown_since_ = now;
    index_ = index;
    since_ = now;
    acting_since_ = 0;
    played_ms_ = 0;
    run_ms_ = 0;
    fresh_ = true;
}

void StepChain::next(uint32_t now) {
    if (index_ + 1 >= steps_.size()) {
        if (loop_) {                       // a lasting note's body: again, for as long as it has the body
            enter(0, now);
            return;
        }
        steps_.clear();                    // done: back to its own life
        index_ = 0;
        finished_ = true;
        return;
    }
    enter(index_ + 1, now);
}

void StepChain::pause(uint32_t now) {
    if (acting_since_) played_ms_ += now - acting_since_;
    since_ = now;
    acting_since_ = 0;
    fresh_ = true;
}

void StepChain::drive(Pet& pet, uint32_t now) {
    if (steps_.empty()) return;
    if (!pet.has_egg()) {              // no pet out: nothing can play them
        clear();
        return;
    }
    const Step& step = steps_[index_];
    const bool acting = pet.wanted() == Look::Act;   // else the body starts the step afresh when it gets to it
    pet.act(step.act, fresh_ && acting);
    if (!step.show.empty()) pet.show_part(step.show, shown_since_);
    fresh_ = false;
    if (!acting) {                     // something else came first: the step carries on after it, keeping its time
        pause(now);
        fresh_ = false;                  // the step was handed to the body above
        return;
    }
    if (step.waypoint && index_ + 1 < steps_.size()) {   // through it, on to the next step, still walking
        const bool walking = pet.phase() == Pet::Phase::Walking;
        if (!pet.jumping() && (!walking || pet.route_left() <= WAYPOINT_PX)) next(now);   // a jump lands first
        return;
    }
    const bool there = pet.phase() == Pet::Phase::Acting || pet.phase() == Pet::Phase::Free;   // Free: it roams
    if (there && !acting_since_) {                   // timed from its animation's first frame, so one play ends
        const uint32_t began = pet.acting_since();    // as the animation does, not a frame after it wraps
        const bool this_step = pet.phase() == Pet::Phase::Acting && !is_before(began, since_) && !is_after(began, now);
        acting_since_ = stamp(this_step ? began : now);
        run_ms_ = std::max<uint32_t>(pet.run_ms(), 1);
    }
    bool played = false;
    if (acting_since_) {
        const uint32_t played_for = played_ms_ + (now - acting_since_);
        switch (step.ends) {
            case Step::Ends::Time: played = played_for >= step.ms; break;
            case Step::Ends::Runs: played = played_for >= run_ms_ * uint32_t(step.runs); break;
            case Step::Ends::Act: played = pet.act_done(now) || played_for >= ACT_MAX_MS; break;
        }
    }
    // A spot it can't get to: no way there, so it never sets off (a long, slow walk still counts as getting there).
    const bool unreachable = !acting_since_ && pet.phase() != Pet::Phase::Walking && now - since_ >= STEP_WALK_MS;
    if (played || unreachable) next(now);
}

}  // namespace nestbox
