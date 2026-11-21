// What a sender asks the body to do ("do"): one step, or a list of up to MAX_STEPS played in order. A step plays a
// behaviour or one of the pet's own animations, walks somewhere, strolls, runs laps or throws, at a speed, facing a
// way, and may say something or wear a sign meanwhile. A step's time starts once the pet has arrived and turned.
// Where there is nowhere to stand near a spot, the step plays where the pet is; a walk that can't set off within
// STEP_WALK_MS is skipped. A step with nothing but "at" (and maybe "speed"), followed by another step, is a point on
// the way: the pet walks on through it without stopping. Something higher in the order comes first, and the steps
// carry on after.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "body/pet.h"
#include "util/json.h"

namespace nestbox {

struct Step {
    static constexpr uint32_t IN_LIST_MS = 10000, MAX_MS = 24 * 3600 * 1000;
    static constexpr int MAX_COUNT = 20;
    enum class Ends {
        Time,                            // after `ms`
        Runs,                            // after `runs` runs of its animation
        Act,                             // when the body says its laps are run or its throws thrown
    };
    Act act;
    Ends ends = Ends::Time;
    uint32_t ms = IN_LIST_MS;
    int runs = 1;
    bool open_ended = false;             // no "for" or "times" and it doesn't end by itself: the note decides
    std::string say;                     // words while it plays
    Bubble mark = Bubble::None;          // a sign while it plays
    bool waypoint = false;               // only an "at": it walks on through it to the next step without stopping
    std::string word;                    // the "do" word as sent ("" for a walk)
    std::string show;                    // a pastime's cued scene part, shown while the step plays

    // What it does in a word: "dance", "honk", "walk", "stroll", "run", "throw".
    std::string words() const;
    bool operator==(const Step& o) const {
        return act == o.act && ends == o.ends && ms == o.ms && runs == o.runs && open_ended == o.open_ended &&
               say == o.say && mark == o.mark && waypoint == o.waypoint && word == o.word && show == o.show;
    }
};

// The behaviours every pet answers to, in the order the help lists them, with what each does.
struct Behaviour {
    const char* word;
    const char* means;
    Look look;                           // the look it plays (stroll and run: Wander; throw: Idle)
    bool is_look;                        // it simply plays that look (not a stroll, laps or a throw)
};
const std::vector<Behaviour>& behaviours();

// The facing row (0-7) for "front", "front-right", "right", "back-right", "back", "back-left", "left" or
// "front-left" (lower case). None if unknown.
std::optional<int> facing_row(const std::string& face);
const std::vector<std::string>& directions();

class StepChain {
public:
    static constexpr size_t MAX_STEPS = 8;
    static constexpr uint32_t STEP_WALK_MS = 30000;
    static constexpr uint32_t ACT_MAX_MS = 10 * 60 * 1000;   // laps or throws that never end are cut off here
    static constexpr float WAYPOINT_PX = 6;   // this close to a waypoint it already heads for the next step

    // One step from its fields ("do", "at", "aim", "face", "times", "for", "speed", "say", "sign"); a bare word is
    // read as {"do": word}. Adds a warning for each word it can't use. False if the step asks for nothing.
    static bool parse(const Fields& f, Step& out, std::vector<std::string>& warnings);

    // Starts a chain. `loop`: it starts over when it ends (a lasting note's body).
    void start(std::vector<Step> steps, uint32_t now_ms, bool loop = false);
    // Every step has played (never true for a looping chain).
    bool done() const { return finished_; }
    // Shows the current step on the pet and moves on when it has played.
    void drive(Pet& pet, uint32_t now_ms);
    // Something else has the body: the step keeps the time it has played and carries on later.
    void pause(uint32_t now_ms);
    void clear() {
        steps_.clear();
        finished_ = true;
    }

    bool empty() const { return steps_.empty(); }
    size_t left() const { return steps_.empty() ? 0 : steps_.size() - index_; }
    const Step& current() const { return steps_[index_]; }

private:
    void next(uint32_t now_ms);
    void enter(size_t index, uint32_t now_ms);

    std::vector<Step> steps_;
    size_t index_ = 0;
    uint32_t since_ = 0, acting_since_ = 0, run_ms_ = 0;
    uint32_t played_ms_ = 0;             // time the step played before something else came first
    uint32_t shown_since_ = 0;           // when the part the step shows began: steps in a row showing it share it
    bool fresh_ = false;                 // the step has not been handed to the body yet
    bool loop_ = false, finished_ = false;
};

}  // namespace nestbox
