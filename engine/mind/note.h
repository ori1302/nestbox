// A note: what one sender told the pet about one thing (POST /v1/tell). A sender is `from`; its things are named by
// `id` ("build-12", a session "7f3a", a part of it "7f3a/a41c"). A note holds a state, which carries the behaviour
// (rank, lifetime, quiet, waiting for the user), and a look whose every part can be given, switched off, or left to
// the state's default. Each tell replaces the whole note of its name.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "mind/steps.h"
#include "screen/hud.h"
#include "util/json.h"

namespace nestbox {

enum class State { None, Working, Asking, Failing, On, Quiet, Done, Failed, Over };
enum class Until { None, Seen, Back };

const char* state_key(State s);           // "" for None
const std::vector<State>& states();       // in the order the help lists them
const char* state_means(State s);
bool is_lasting(State s);                 // working, asking, failing, on, quiet
bool is_result(State s);                  // done, failed

// One part of a note's look: left to the state, switched off with false, or given.
template <typename T>
struct Part {
    enum class Kind { Default, Off, Set };
    Kind kind = Kind::Default;
    T value{};
    bool set() const { return kind == Kind::Set; }
    bool off() const { return kind == Kind::Off; }
    bool operator==(const Part& o) const { return kind == o.kind && (kind != Kind::Set || value == o.value); }
};

struct Gauge {
    bool timer = false;                  // empties over the note's time, showing the time left
    double value = 0;                    // 0..100
    bool operator==(const Gauge& o) const { return timer == o.timer && value == o.value; }
};

struct Note {
    static constexpr size_t MAX_FROM = 24, MAX_ID = 48;
    static constexpr size_t MAX_SAY = 72, MAX_CAPTION = 60, MAX_LINE = 60, MAX_LABEL = 8;

    std::string from;                    // as written
    std::string id;                      // "" for the sender's own note
    State state = State::None;
    Part<std::vector<Step>> body;        // "do" (with "at", "aim", "face", "times", "speed" for a single step)
    bool body_list = false;              // "do" was a list of steps
    Part<std::string> say, caption, line;
    Part<Gauge> gauge;
    std::string label;
    Part<Bubble> sign;
    Part<Effect> effect;
    std::string color;                   // a colour name, "" for none
    uint32_t for_ms = 0;                 // a "for" the sender gave, in real time; 0: the state's own time
    Until until = Until::None;

    // "from" or "from/id".
    std::string name() const { return id.empty() ? from : from + "/" + id; }
    // Nothing but a name: such a tell ends the note, as "over" does.
    bool empty() const;
};

// Reads one tell into `out`, with a warning for everything it could not use. False if nothing could be used at all.
bool parse_tell(const Fields& f, Note& out, std::vector<std::string>& warnings);
// The sender a tell is from, as parse_tell() reads "from".
std::string tell_sender(const Fields& f);
// A name a sender can have: letters, digits, "-", "_" and ".", up to Note::MAX_FROM.
bool valid_sender(const std::string& name);

// "8s", "5m", "2h" or a bare number of seconds, as ms; none if it is none of those or not above zero.
std::optional<uint32_t> duration_ms(const std::string& text);
constexpr const char* FOR_WARNING = "\"for\" is a time such as \"8s\", \"5m\" or \"2h\"; a bare number is seconds";

// The colour names and their RGB565 values.
const std::vector<std::pair<const char*, uint16_t>>& color_names();
uint16_t color_value(const std::string& name);      // the screen's ink for "" or an unknown name

}  // namespace nestbox
