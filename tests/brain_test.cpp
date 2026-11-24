// The brain's rules with a made-up clock: what a tell does and how long a note lasts, replacement, endings and parts,
// the order of importance per part of the screen, questions that never hang, what waits for the user, quiet, the
// user's hand, the sources of activity and the pet's own life, warnings, and timers across the 49.7-day wrap of
// millis(). The HTTP rules are in api_test.cpp; steps played on a real body are in move_test.cpp.
// Usage: brain_test
#include "mind/brain.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#include "check.h"
#include "util/json.h"

using namespace nestbox;
using test::check;

namespace {

constexpr uint32_t S = 1000;    // one second, in ms
constexpr uint32_t M = 60 * S;  // one minute, in ms
constexpr uint32_t H = 60 * M;  // one hour, in ms

Fields fields(const std::string& json) {
    Fields f;
    if (!parse_json(json, f)) test::fail("test JSON parses: " + json);
    return f;
}

Brain::Told tell(Brain& b, const std::string& json, uint32_t now) { return b.tell(fields(json), now); }
void report(Brain& b, const std::string& json, uint32_t now) { b.user(fields(json), now); }

// The note named from/id as GET /v1 shows it; an empty view if there is none.
Brain::NoteView note(Brain& b, const std::string& from, const std::string& id, uint32_t now) {
    for (const Brain::NoteView& n : b.notes(now))
        if (n.from == from && n.id == id) return n;
    return {};
}

bool shows(const Brain::NoteView& n, const std::string& part) {
    for (auto& p : n.showing)
        if (p == part) return true;
    return false;
}

bool warned(const Brain::Told& t, const std::string& words) {
    for (auto& w : t.warnings)
        if (w.find(words) != std::string::npos) return true;
    return false;
}

void a_tell_replaces_the_note() {
    Brain b;
    tell(b, R"({"from":"ci","id":"b1","state":"working","line":"building main","gauge":40})", 1000);
    auto n = note(b, "ci", "b1", 1000);
    check(n.state == "working" && n.line == "building main" && shows(n, "gauge"),
          "a working note with a line and gauge");
    tell(b, R"({"from":"ci","id":"b1","state":"working"})", 2000);
    n = note(b, "ci", "b1", 2000);
    check(n.line == "ci working" && !shows(n, "gauge"), "a tell replaces it: what it leaves out is gone or default");
    tell(b, R"({"from":"ci","id":"b1","state":"working","line":false})", 3000);
    check(note(b, "ci", "b1", 3000).line.empty(), "false switches the state's line off");
    check(b.decide(3000) == Look::Watch && b.why() == "ci/b1 is working", "working watches its desk: " + b.why());
}

void endings() {
    Brain b;
    tell(b, R"({"from":"ci","id":"b1","state":"working","line":"building"})", 1000);
    auto t = tell(b, R"({"from":"ci","id":"b1","state":"done"})", 2000);
    check(t.state == "done" && t.ends_in == 8, "done is a moment of 8 s");
    check(b.decide(2000) == Look::Celebrate, "done celebrates");
    check(note(b, "ci", "b1", 2000).line == "ci done", "its line says so, and the working line is gone");
    b.decide(10500);
    check(note(b, "ci", "b1", 10500).from.empty(), "then it is gone");
    check(b.decide(10500) != Look::Celebrate, "and the pet goes back to its own life");

    tell(b, R"({"from":"ci","id":"b2","state":"working"})", 20000);
    t = tell(b, R"({"from":"ci","id":"b2","state":"over"})", 21000);
    check(t.ended == 1 && note(b, "ci", "b2", 21000).from.empty(), "over ends it with no reaction");

    tell(b, R"({"from":"sam","say":"hi"})", 30000);
    t = tell(b, R"({"from":"sam"})", 31000);
    check(t.ended == 1, "a tell with nothing to show ends the note, as over does");

    tell(b, R"({"from":"claude","id":"s1","state":"working"})", 40000);
    tell(b, R"({"from":"claude","id":"s2","state":"working"})", 40000);
    tell(b, R"({"from":"claude","id":"s1/a","state":"working"})", 40000);
    t = tell(b, R"({"from":"claude","id":"s1","state":"over"})", 41000);
    check(t.ended == 2 && !note(b, "claude", "s2", 41000).from.empty(), "over ends a session and its parts only");
    t = tell(b, R"({"from":"claude","state":"over"})", 42000);
    check(t.ended == 1, "over without an id ends everything of the sender");
}

void lifetimes() {
    Brain b;
    tell(b, R"({"from":"a","state":"working"})", 1000);
    b.decide(1000 + 15 * M - S);
    check(!note(b, "a", "", 1000 + 15 * M - S).from.empty(), "working lasts 15 minutes");
    b.decide(1000 + 15 * M + S);
    check(note(b, "a", "", 1000 + 15 * M + S).from.empty(), "and ends by itself after them");
    tell(b, R"({"from":"a","state":"working","for":"2m"})", 2 * H);
    check(note(b, "a", "", 2 * H).ends_in == 120, "for gives its own time");
    tell(b, R"({"from":"a","state":"working","for":"2m"})", 2 * H + M);
    check(note(b, "a", "", 2 * H + M).ends_in == 120, "telling again renews it");
    tell(b, R"({"from":"f","state":"failing"})", 3 * H);
    check(note(b, "f", "", 3 * H).ends_in == 3600, "failing lasts an hour");
    tell(b, R"({"from":"o","state":"on","line":"x"})", 3 * H);
    check(note(b, "o", "", 3 * H).ends_in == 15 * 60, "on lasts 15 minutes");
    tell(b, R"({"from":"q","state":"on","for":"30h"})", 4 * H);
    b.decide(4 * H + 25 * H);
    check(note(b, "q", "", 4 * H + 25 * H).from.empty(), "nothing lasts more than a day");
}

void moments() {
    Brain b;
    auto t = tell(b, R"({"from":"disk","say":"disk 91% full","sign":"!"})", 1000);
    check(t.state.empty() && t.showing.size() == 2 && b.saying(1000) == "disk 91% full",
          "a passing moment shows its words");
    b.decide(5000);
    check(b.saying(5000) == "disk 91% full", "for at least 8 s");
    b.decide(9500);
    check(b.saying(9500).empty(), "then it is gone");
    tell(b, R"({"from":"sam","say":"back in 5","for":"30s"})", 20000);
    b.decide(45000);
    check(b.saying(45000) == "back in 5", "a moment's for gives its time");
}

void questions_never_hang() {
    Brain b;
    tell(b, R"({"from":"claude","id":"s1","state":"working","line":"editing"})", 1000);
    auto t = tell(b, R"({"from":"claude","id":"s1","state":"asking","say":"Allow it?"})", 2000);
    check(t.ends_in == 3600 && b.decide(2000) == Look::Alert, "a question waves, for an hour of the user's time");
    check(b.saying(2000) == "Allow it?", "with the question in a bubble");
    auto n = note(b, "claude", "s1", 2000);
    check(n.line == "claude needs you" && shows(n, "sign"), "a line and a ? over its head");
    b.decide(2000 + M + S);
    check(b.decide(2000 + M + S) != Look::Alert, "after a minute it stops waving");
    check(b.saying(2000 + M + S) == "Allow it?", "but the question still shows");
    check(b.decide(2000 + 10 * M + S) == Look::Alert, "it waves again ten minutes in");
    check(b.decide(2000 + 10 * M + 10 * S) != Look::Alert, "briefly");
    check(note(b, "claude", "s1", 2000 + 5 * M).line == "claude needs you - 5 min", "the line says how long it waits");
    b.tap(2000 + 12 * M);
    check(b.saying(2000 + 12 * M + S).empty(), "a tap: seen, the bubble goes");
    n = note(b, "claude", "s1", 2000 + 12 * M + S);
    check(shows(n, "line") && shows(n, "sign"), "but the line and sign stay: seen is not answered");
    check(b.decide(2000 + 20 * M + S) != Look::Alert, "a seen question does not wave again");
    tell(b, R"({"from":"claude","id":"s1","state":"working"})", 2000 + 21 * M);
    check(note(b, "claude", "s1", 2000 + 21 * M).state == "working", "the session moving on ends it");
    tell(b, R"({"from":"claude","id":"s2","state":"asking","say":"Delete it?"})", 3 * H);
    b.decide(3 * H + 61 * M);
    check(note(b, "claude", "s2", 3 * H + 61 * M).from.empty(), "a question nobody answers ends after an hour");

    Brain away;
    report(away, R"({"from":"mac","idle":600,"state":"awake","next":3600})", 1000);
    tell(away, R"({"from":"claude","state":"asking","say":"ok?"})", 2000);
    away.decide(2000 + 2 * H);
    check(!note(away, "claude", "", 2000 + 2 * H).from.empty(), "while the user is away its hour does not count");
    tell(away, R"({"from":"door","state":"asking","say":"door","for":"2m"})", 3 * H);
    away.decide(3 * H + 3 * M);
    check(note(away, "door", "", 3 * H + 3 * M).from.empty(), "but a for the sender gives is real time");
}

void results_wait_for_the_user() {
    Brain b;
    report(b, R"({"from":"mac","idle":600,"state":"awake","next":3600})", 1000);
    tell(b, R"({"from":"claude","id":"s1","state":"failed","line":"tests failed"})", 2000);
    auto n = note(b, "claude", "s1", 2000);
    check(n.why == "the user is away; it stays until they are back", "a result while the user is away: " + n.why);
    check(b.decide(2000 + 5 * S) == Look::Slump, "showing it: failed sulks for its moment");
    b.decide(2000 + H);
    n = note(b, "claude", "s1", 2000 + H);
    check(!n.from.empty(), "it stays while they are away");
    check(b.decide(2000 + H) != Look::Slump && !shows(n, "do") && shows(n, "line"),
          "but the body rests after its moment; the line waits");
    report(b, R"({"from":"mac","idle":0,"state":"awake","next":3600})", 2000 + H + S);
    check(b.decide(2000 + H + 5 * S) == Look::Slump, "back: it sulks again");
    check(!note(b, "claude", "s1", 2000 + H + 5 * S).from.empty(), "and shows its 8 s now");
    b.decide(2000 + H + 10 * S);
    check(note(b, "claude", "s1", 2000 + H + 10 * S).from.empty(), "and then ends");

    Brain done;   // a finished session while the user is away: one celebration, not hours of it
    report(done, R"({"from":"mac","idle":600,"state":"awake","next":3600})", 1000);
    tell(done, R"({"from":"claude","id":"s1","state":"working"})", 2000);
    tell(done, R"({"from":"claude","id":"s2","state":"done"})", 3000);
    check(done.decide(3000 + 2 * S) == Look::Celebrate, "done while away: it celebrates");
    check(done.decide(3000 + 10 * M) == Look::Watch, "then the other session's work has the body again");
    report(done, R"({"from":"mac","idle":0,"state":"awake","next":3600})", 3000 + 10 * M + S);
    check(done.decide(3000 + 10 * M + 2 * S) == Look::Celebrate, "back: it celebrates for the user");
}

void until_seen_and_back() {
    Brain b;
    tell(b, R"({"from":"sam","say":"call the dentist","until":"seen"})", 1000);
    b.decide(5 * H);
    check(b.saying(5 * H) == "call the dentist", "until seen: it stays");
    check(b.tap(5 * H) == "seen" && b.saying(5 * H + S).empty(), "until the user taps the pet");
    report(b, R"({"from":"mac","idle":600,"state":"awake","next":3600})", 6 * H);
    tell(b, R"({"from":"claude","say":"your turn","until":"back"})", 6 * H);
    b.decide(7 * H);
    check(b.saying(7 * H) == "your turn", "until back: it stays while the user is away");
    report(b, R"({"from":"mac","idle":0,"state":"awake","next":3600})", 7 * H + S);
    b.decide(7 * H + 10 * S);
    check(b.saying(7 * H + 10 * S).empty(), "and goes a moment after they are back");
    tell(b, R"({"from":"claude","id":"s1","state":"asking","say":"Allow?"})", 8 * H);
    tell(b, R"({"from":"claude","say":"your turn","until":"back"})", 8 * H + S);
    check(note(b, "claude", "s1", 8 * H + S).state == "asking", "your turn never replaces a session's question");
    check(b.saying(8 * H + S) == "Allow?", "the question keeps the bubble");
}

void the_order_of_importance() {
    Brain b;
    tell(b, R"({"from":"claude","id":"s1","state":"working","line":"editing app.ts"})", 1000);
    tell(b, R"({"from":"server","state":"failing","line":"api down"})", 2000);
    auto n = note(b, "server", "", 2000);
    check(!shows(n, "do") && shows(n, "line") && shows(n, "sign"),
          "an outage while an agent works keeps its line and sign");
    check(n.why == "claude/s1 is working and has the body", "and says why: " + n.why);
    check(b.decide(2000) == Look::Watch, "the agent has the body");
    tell(b, R"({"from":"claude","id":"s1","state":"over"})", 3000);
    check(b.decide(3000) == Look::Slump, "the outage gets the body when the agent stops");
    tell(b, R"({"from":"music","state":"on","do":"dance","effect":"notes"})", 4000);
    check(b.decide(4000) == Look::Act && shows(note(b, "music", "", 4000), "effect") &&
              shows(note(b, "server", "", 4000), "line"),
          "music's chosen dance gets the body over the failure's default look; the failure keeps its line");
    tell(b, R"({"from":"x","state":"working","do":"dance"})", 5000);
    check(b.decide(5000) == Look::Act, "a look of the sender's own: working, dancing");

    Brain c;
    tell(c, R"({"from":"claude","id":"s1","state":"working","line":"editing"})", 1000);
    tell(c, R"({"from":"me","state":"failing","line":"main is red","do":"sulk"})", 2000);
    check(c.decide(2000) == Look::Act && shows(note(c, "me", "", 2000), "do"),
          "a do a sender chose comes before an agent's default look, whatever its state");
    tell(c, R"({"from":"claude","id":"s1","state":"working","line":"testing"})", 3000);
    check(shows(note(c, "me", "", 3000), "do"), "even when the agent tells again");
    tell(c, R"({"from":"server","state":"failing","line":"api down"})", 4000);
    check(shows(note(c, "me", "", 4000), "do"), "and before another failure's default look");
    tell(c, R"({"from":"ci","state":"done"})", 5000);
    check(c.decide(5000) == Look::Celebrate, "a moment still comes first");
}

void parts() {
    Brain b;
    tell(b, R"({"from":"claude","id":"s1","state":"working"})", 1000);
    tell(b, R"({"from":"claude","id":"s1/a41c","state":"working","line":"sub-agent: tests"})", 1000);
    auto t = tell(b, R"({"from":"claude","id":"s1","state":"done"})", 2000);
    check(t.why == "waiting for 1 part that is still working", "a result waits for its parts: " + t.why);
    check(b.decide(2000) == Look::Watch, "the sub-agent still has the body");
    b.decide(20000);
    check(!note(b, "claude", "s1", 20000).from.empty(), "it keeps waiting");
    tell(b, R"({"from":"claude","id":"s1/a41c","state":"over"})", 30000);
    check(b.decide(30000) == Look::Celebrate, "the last part ends: it celebrates");
}

void quiet() {
    Brain b;
    tell(b, R"({"from":"cal","state":"quiet","for":"45m","gauge":"timer","line":"in a meeting"})", 1000);
    check(b.decide(1000) == Look::Sit && b.quiet(1000), "quiet: the pet stays put");
    auto t = tell(b, R"({"from":"deploy","say":"shipped"})", 2000);
    check(t.why == "quiet is on, so this was skipped" && t.ends_in == 0, "a passing moment is skipped");
    tell(b, R"({"from":"ci","state":"done"})", 3000);
    check(note(b, "ci", "", 3000).why == "quiet is on; it waits until quiet ends", "a result waits for quiet to end");
    tell(b, R"({"from":"door","state":"asking","say":"Someone is at the door","for":"2m"})", 4000);
    check(b.decide(4000) == Look::Alert && b.saying(4000) == "Someone is at the door", "a question gets through");
    check(shows(note(b, "cal", "", 4000), "gauge"), "lines and gauges stay");
    t = tell(b, R"({"from":"cal","say":"lunch!"})", 4500);
    check(t.why.empty() && !b.quiet(4500), "a sender's own tell replaces its quiet, and is not skipped");
    tell(b, R"({"from":"cal","state":"quiet"})", 4600);
    tell(b, R"({"from":"cal","state":"over"})", 5000);
    check(!b.quiet(5000), "the sender ends its quiet");
    check(b.quiet_press(6000) == "quiet on" && b.quiet(6000), "the long press: quiet for the device");
    tell(b, R"({"from":"cal","state":"over"})", 7000);
    check(b.quiet(7000), "a sender can't end the device's quiet");
    tell(b, R"({"from":"cal","state":"quiet"})", 8000);
    check(b.quiet_press(9000) == "quiet off" && !b.quiet(9000), "switching it off on the device ends every quiet");
}

// The long press shows: a nod each way, "quiet" in the status bar while it lasts, "quiet off" for a moment after.
void quiet_shows_on_the_device() {
    Brain b;
    b.quiet_press(1000);
    check(b.decide(1000) == Look::Nod, "quiet on: a nod");
    auto n = note(b, "device", "quiet", 1000);
    check(n.line == "quiet" && shows(n, "line"), "quiet on: \"quiet\" in the status bar");
    tell(b, R"({"from":"claude","state":"working","line":"reading"})", 2000);
    check(shows(note(b, "device", "quiet", 600000), "line") && shows(note(b, "claude", "", 600000), "line"),
          "it stays for as long as quiet does, beside the other lines");
    b.quiet_press(3600000);
    check(b.decide(3600000) == Look::Nod, "quiet off: a nod");
    auto off = note(b, "device", "quiet-off", 3600000);
    check(off.line == "quiet off" && shows(off, "line"), "quiet off: \"quiet off\" in the status bar");
    check(note(b, "device", "quiet", 3600000).from.empty(), "and \"quiet\" is gone");
    check(note(b, "device", "quiet-off", 3610000).from.empty(), "\"quiet off\" goes after a moment");
    b.quiet_press(3601000);
    check(note(b, "device", "quiet-off", 3601000).from.empty(), "quiet on again clears \"quiet off\"");
}

void the_users_hand() {
    Brain b;
    check(b.tap(1000) == "petted" && b.decide(1000) == Look::Petted, "a tap with nothing waiting pats the pet");
    tell(b, R"({"from":"ci","state":"done"})", 10000);
    check(b.tap(10000) == "seen" && note(b, "ci", "", 10000).from.empty(), "a tap ends a result");
    tell(b, R"({"from":"server","state":"failing","line":"api down"})", 20000);
    check(b.dismiss(20000) == "dismissed", "a double press dismisses");
    auto n = note(b, "server", "", 20000);
    check(n.showing.empty() && n.why.find("dismissed") != std::string::npos, "the note is hidden");
    tell(b, R"({"from":"server","state":"failing","line":"api down"})", 21000);
    check(note(b, "server", "", 21000).showing.empty(), "repeating itself does not undo the user");
    tell(b, R"({"from":"server","state":"done","line":"api is back"})", 22000);
    check(shows(note(b, "server", "", 22000), "line"), "a new state shows again");
    check(b.dismiss(40000).empty(), "with nothing to dismiss, the device brings out the next pet");
    check(b.shake(41000) == "dizzy" && b.decide(41000) == Look::Dizzy, "a shake makes it dizzy");
    b.quiet_press(50000);
    check(b.tap(50000) == "petted" && b.decide(50000) == Look::Petted, "the user's own hand reacts during quiet");
}

void the_users_activity() {
    Brain b;
    check(b.user_word(1000) == "unknown", "no source: unknown");
    report(b, R"({"from":"mac","keys":5,"idle":0,"state":"awake","time":"14:32"})", 1000);
    check(b.decide(1000) == Look::Type && b.user_word(1000) == "typing", "keys: it types");
    check(b.minute_of_day(1000) == 14 * 60 + 32, "the time comes from the report");
    report(b, R"({"from":"mac","keys":0,"idle":1})", 2000);
    check(b.user_word(2000) == "paused" && b.decide(2000) == Look::Type, "a pause keeps it at its desk");
    tell(b, R"({"from":"claude","state":"working"})", 2500);
    report(b, R"({"from":"mac","keys":5,"idle":0})", 3000);
    check(b.decide(3000) == Look::Type, "the user's typing shows over an agent's working");
    tell(b, R"({"from":"claude","state":"working","do":"watch"})", 3500);
    check(b.decide(3500) == Look::Act, "unless the agent chose the body itself");
    Brain m;
    tell(m, R"({"from":"music","state":"on","do":"dance","effect":"notes"})", 1000);
    check(m.decide(1000) == Look::Act, "a song: it dances");
    report(m, R"({"from":"mac","keys":5,"idle":0})", 1500);
    check(m.decide(1500) == Look::Type && note(m, "music", "", 1500).why == "the user is typing",
          "the user's typing shows over what is simply going on, even its \"do\"");
    report(m, R"({"from":"mac","keys":0,"idle":3})", 5000);
    check(m.decide(5000) == Look::Act, "it dances again once the user stops typing");
    tell(b, R"({"from":"claude","state":"over"})", 3600);
    report(b, R"({"from":"mac","keys":0,"idle":0,"state":"locked"})", 4000);
    b.decide(4000);
    check(b.decide(8000) == Look::Sleep && b.user_word(8000) == "asleep", "locked: it sleeps");
    b.decide(9 * S);
    report(b, R"({"from":"mac","idle":0,"state":"awake","next":5})", 20 * S);
    b.decide(20 * S);
    b.decide(24 * S);
    b.decide(40 * S);
    check(b.decide(45 * S) == Look::Sleep && b.why() == "the computer is asleep or out of reach",
          "a source silent for twice its next is gone: " + b.why());
    Brain two;
    report(two, R"({"from":"mac","idle":600,"state":"awake","next":60})", 1000);
    report(two, R"({"from":"pc","idle":0,"state":"awake","next":60})", 1000);
    check(two.user_word(1000) == "here", "away only if every source says so");
    auto w = two.user(fields(R"({"from":"mac","keys":-1,"state":"dozing","mood":"good"})"), 2000);
    check(w.size() == 3, "bad values and unknown keys are warned about");
}

// Typing speed is the keys of the current stretch of typing over the time its reports cover (1.5 to 3 s), not over
// the gap between two reports: reports come at once when typing starts or stops, so two can be a fraction of a second
// apart. Typing fast is rare: about 96 words a minute, kept up.
void typing_speed() {
    Brain b;
    report(b, R"({"from":"mac","keys":0,"idle":0})", 1000);
    report(b, R"({"from":"mac","keys":3,"idle":0})", 1250);     // typing starts: reported at once, 0.25 s later
    b.decide(1250);
    check(b.decide(1300) == Look::Type && b.user_word(1300) == "typing", "a few quick keys: typing, not fast");
    Brain fast;
    for (uint32_t t = 1000; t <= 9000; t += 1000) {
        report(fast, R"({"from":"mac","keys":12,"idle":0})", t);
        fast.decide(t);
    }
    check(fast.decide(9000) == Look::TypeFast, "12 keys a second, kept up: typing fast");
    Brain steady;
    bool steady_fast = false;
    for (uint32_t t = 1000; t <= 15000; t += 1000) {
        report(steady, R"({"from":"mac","keys":5,"idle":0})", t);
        steady_fast |= steady.decide(t) == Look::TypeFast;
    }
    check(!steady_fast, "5 keys a second (60 words a minute), kept up: never fast");
    Brain word;
    report(word, R"({"from":"mac","keys":0,"idle":0})", 1000);
    bool word_fast = false;
    for (uint32_t t = 1250; t <= 6000; t += 250) {                // one quick word, then ordinary typing
        if (t <= 1750 || (t / 250) % 4)                          // then three keys a second: every fourth quarter none
            report(word, t <= 1750 ? R"({"from":"mac","keys":4,"idle":0})" : R"({"from":"mac","keys":1,"idle":0})", t);
        word_fast |= word.decide(t) == Look::TypeFast;
    }
    check(!word_fast, "a quick first word (16 keys a second), then 3 a second: never fast");

    // Quick words: every third second a burst of 12 keys, 2 a second in between (64 words a minute in all).
    Brain bursts;
    bool ever_fast = false;
    for (uint32_t t = 1000; t <= 31000; t += 250) {
        const bool burst = (t / 1000) % 3 == 0;
        if (burst || (t / 250) % 2)                               // two keys a second between: every other quarter
            report(bursts, burst ? R"({"from":"mac","keys":3,"idle":0})" : R"({"from":"mac","keys":1,"idle":0})", t);
        ever_fast |= bursts.decide(t) == Look::TypeFast;
    }
    check(!ever_fast, "quick words in a quick run: never fast");

    // As the Mac app reports: four times a second while typing, and at once when it stops.
    Brain quick;
    uint32_t t = 1000, fast_at = 0;
    for (; t <= 9000; t += 250) {
        report(quick, R"({"from":"mac","keys":3,"idle":0})", t);
        if (quick.decide(t) == Look::TypeFast && !fast_at) fast_at = t;
    }
    check(fast_at && fast_at - 1000 >= 2000 && fast_at - 1000 <= 4500,
          "12 keys a second: fast after a few seconds (" + std::to_string(fast_at - 1000) + " ms)");
    uint32_t slow_at = 0;
    const uint32_t slower = t;
    for (; t <= slower + 8000; t += 250) {
        report(quick, R"({"from":"mac","keys":1,"idle":0})", t);
        if (quick.decide(t) == Look::Type && !slow_at) slow_at = t;
    }
    check(slow_at && slow_at - slower <= 6000, "slower: back to typing within a few seconds (" +
                                                   std::to_string(slow_at - slower) + " ms)");
    report(quick, R"({"from":"mac","keys":0,"idle":0})", t);
    check(quick.user_word(t) == "paused", "it stops at once: a pause at its desk");
    for (uint32_t end = t + 3000; t < end; t += 250) quick.decide(t);
    report(quick, R"({"from":"mac","keys":3,"idle":0})", t);
    check(quick.decide(t) == Look::Type, "typing again after a pause: fast only once kept up again");

    // Reports a little early or late (once a second, 9 keys each): the speed holds steady between them.
    Brain jitter;
    float lowest = 100;
    for (uint32_t i = 1, now = 1000; i <= 12; i++) {
        const uint32_t at = i * 1000 + (i % 2 ? 40 : 0);
        for (; now < at; now += 50)
            if (now > 5000) lowest = std::min(lowest, jitter.kps(now));
        report(jitter, R"({"from":"mac","keys":9,"idle":0})", at);
    }
    check(lowest >= 8.5f,
          "reports a little early or late: no dips in the speed (lowest " + std::to_string(lowest) + ")");
}

// A moment told again plays again; a lasting note told again keeps its steps going.
void telling_again() {
    Brain b;
    Pet pet;
    const std::string seq = R"({"from":"me","do":["celebrate","stand"],"say":"watch this"})";
    tell(b, seq, 1000);
    for (uint32_t t = 1000; t < 40000; t += 100) {
        b.decide(t);
        b.show(pet, t);
    }
    check(b.steps_left() == 0, "the moment's steps have played");
    auto t = tell(b, seq, 40000);
    check(b.steps_left() == 2 && std::find(t.showing.begin(), t.showing.end(), "do") != t.showing.end(),
          "telling the same moment again plays it again");
    Brain c;
    tell(c, R"({"from":"radio","state":"on","do":"dance"})", 50000);
    c.decide(50000);
    tell(c, R"({"from":"radio","state":"on","do":"dance"})", 51000);
    check(c.steps_left() == 1, "a lasting note told again keeps its steps going");
}

void the_pets_own_life() {
    Brain b;
    b.clock(int64_t(23) * 3600, 1000);
    b.decide(1000);
    check(b.decide(1000 + 5 * S) == Look::Sleep && b.why() == "it is night", "bedtime by its own clock: " + b.why());
    b.force_time(14 * 60, 10 * S);
    check(b.minute_of_day(10 * S) == 14 * 60, "a forced time of day");
    b.force_time(std::nullopt, 11 * S);
    check(b.minute_of_day(11 * S) == 23 * 60, "auto lets the clock decide again");
}

void warnings() {
    Brain b;
    auto t = tell(b,
                  R"({"from":"ci","state":"running","colour":"red",)"
                  R"("line":"deploying the new release of the web app to production, eu-west and us-east"})",
                  1000);
    check(t.used && warned(t, "unknown state \"running\"") && warned(t, "unknown key \"colour\"") &&
              warned(t, "\"line\" was cut to 60"),
          "bad input does the sensible part and says what it changed");
    t = tell(b, R"({"from":"a b","for":"soon","gauge":200,"sign":"wink","until":"later","say":"x"})", 2000);
    check(warned(t, "\"from\" takes") && warned(t, "\"for\" is a time") && warned(t, "\"gauge\" is 0 to 100") &&
              warned(t, "unknown sign") && warned(t, "\"until\" is"),
          "each bad value has its own warning");
    t = tell(b, R"({"color":"blue"})", 3000);
    check(t.used, "a tell with known keys is used");
    t = tell(b, R"({"mood":"great"})", 4000);
    check(!t.used, "a tell with nothing known is not");
    t = tell(b, R"({"do":["stand","stand","stand","stand","stand","stand","stand","stand","stand","stand"]})", 5000);
    check(warned(t, "at most 8 steps"), "a list longer than the device's limit is cut");
}

void limits() {
    Brain b;
    Brain::Told t;
    for (int i = 0; i < 9; i++)
        t = tell(b, R"({"from":"ci","id":"b)" + std::to_string(i) + R"(","state":"working"})", 1000 + i);
    check(warned(t, "ci has 8 notes"), "a sender keeps 8 notes; its oldest makes room");
    for (int i = 0; i < 30; i++)
        tell(b, R"({"from":"s)" + std::to_string(i) + R"(","state":"on","line":"x"})", 2000 + i);
    check(b.notes(3000).size() == Brain::MAX_NOTES, "the pet keeps 24 notes in all");
}

void timers_across_the_wrap() {
    Brain b;
    const uint32_t near = 0xFFFFFFFFu - 5 * S;
    tell(b, R"({"from":"a","state":"working","for":"1m"})", near);
    b.decide(near + 30 * S);
    check(!note(b, "a", "", near + 30 * S).from.empty(), "a note told just before millis() wraps lives on after it");
    b.decide(near + 70 * S);
    check(note(b, "a", "", near + 70 * S).from.empty(), "and ends on time");
}

void signs_effects_and_off() {
    Brain b;
    tell(b, R"({"from":"f","state":"failing","sign":false,"do":false})", 1000);
    auto n = note(b, "f", "", 1000);
    check(!shows(n, "sign") && !shows(n, "do") && shows(n, "line"), "false switches off the state's sign and body");
    tell(b, R"({"from":"s","sign":"star","effect":"confetti"})", 2000);
    n = note(b, "s", "", 2000);
    check(shows(n, "sign") && shows(n, "effect"), "signs and effects by name");
    const auto warned = tell(b, R"({"from":"slack","id":"D1","say":"Dana wrote to you","effect":"letter","for":20})", 3000);
    n = note(b, "slack", "D1", 3000);
    check(shows(n, "effect") && shows(n, "say") && warned.warnings.empty(), "a letter, as Slack sends it");
    const auto jumped = tell(b, R"({"from":"slack","id":"D2","say":"Sam wrote to you","effect":"letter","for":20,)"
                                R"("do":[{"do":"surprised","face":"front"}]})", 4000);
    check(jumped.warnings.empty() && b.decide(4000) == Look::Act, "surprised is a behaviour");
}

void turns_and_shares() {
    Brain b;
    tell(b, R"({"from":"claude","id":"s1","state":"working"})", 1000);
    tell(b, R"({"from":"me","state":"working","do":"dance"})", 2000);
    check(b.decide(2000) == Look::Act, "a do on a lasting note comes first");
    tell(b, R"({"from":"me","state":"working","do":"dance"})", 6000);
    tell(b, R"({"from":"claude","id":"s1","state":"working"})", 7000);
    check(b.decide(9000) == Look::Act, "for its turn, even when the agent tells again");
    check(b.decide(10500) == Look::Watch, "then the note goes back to its state's place: telling the same again "
                                          "started no new turn");
    check(note(b, "me", "", 10500).why == "its \"do\" had its turn; claude/s1 is working and has the body",
          "and says why: " + note(b, "me", "", 10500).why);
    tell(b, R"({"from":"me","state":"working","do":"wave"})", 45000);
    check(b.decide(45000) == Look::Act, "a new do is a new turn");

    // A sender that keeps sending moments while another's failure waits gets no more than its share.
    Brain c;
    tell(c, R"({"from":"server","state":"failing","line":"api down"})", 1000);
    int bot = 0, server = 0, first_server = 0, switches = 0;
    Look last = Look::Idle;
    std::string why;
    for (uint32_t now = 1000; now < 61000; now += 100) {
        if (now % 1000 == 0) tell(c, R"({"from":"bot","state":"done"})", now);
        const Look l = c.decide(now);
        if (now < 2000) continue;
        if (l != last) switches++;
        last = l;
        if (l == Look::Celebrate) bot++;
        else if (l == Look::Slump) {
            server++;
            if (!first_server) {
                first_server = now;
                why = note(c, "bot", "", now).why;
            }
        }
    }
    check(first_server && first_server <= int(2000 + 8 * S + 500),
          "the failure gets the body after the other's share: " + std::to_string(first_server));
    check(why == "bot had its share of the body for now; server is failing and has the body", "it says why: " + why);
    check(bot * 5 >= bot + server && bot * 5 <= 2 * (bot + server),
          "over a minute the sender sending moments gets a fair part, not most: " + std::to_string(bot) + " to " +
              std::to_string(server));
    check(switches < 20, "they take turns, not frames: " + std::to_string(switches) + " changes");

    // A state's own look uses no share: an agent that has been working beside a build still celebrates at once.
    Brain e;
    for (uint32_t now = 1000; now < 41000; now += 100) {
        if (now % 1000 == 0) tell(e, R"({"from":"claude","id":"s1","state":"working"})", now);
        if (now % 3000 == 0) tell(e, R"({"from":"ci","state":"working"})", now);
        e.decide(now);
    }
    tell(e, R"({"from":"claude","id":"s1","state":"done"})", 41000);
    check(e.decide(41000) == Look::Celebrate, "working beside another sender used no share");

    Brain d;
    for (uint32_t now = 1000; now < 41000; now += 100) {
        if (now % 1000 == 0) tell(d, R"({"from":"bot","state":"done"})", now);
        d.decide(now);
    }
    check(d.decide(41000) == Look::Celebrate, "alone, a sender keeps the body as long as it likes");

    // Two questions take turns with the body; the bubble and the sign shown are always the one it waves for.
    Brain q;
    tell(q, R"({"from":"claude","state":"asking","say":"Allow it?"})", 1000);
    tell(q, R"({"from":"codex","state":"asking","say":"Delete it?"})", 1500);
    std::set<std::string> bodies;
    std::string apart;
    for (uint32_t now = 1500; now < 40000 && apart.empty(); now += 100) {
        q.decide(now);
        for (const std::string from : {"claude", "codex"}) {
            const Brain::NoteView n = note(q, from, "", now);
            if (!shows(n, "do")) continue;
            bodies.insert(from);
            if (!shows(n, "say") || !shows(n, "sign"))
                apart = from + " has the body at " + std::to_string(now / S) + " s, the bubble says " + q.saying(now);
        }
    }
    check(bodies.size() == 2, "two questions take turns with the body");
    check(apart.empty(), "the bubble and the sign go with the question the body waves for (" + apart + ")");
}

// A pet with a toy it plays with gets a game now and then in its free time: the first after 1-2.5 min, the next
// 3-6 min after one ends, none past PLAY_MAX; a pet without one never plays.
void free_time_games() {
    FreeTime none;
    bool played = false;
    for (uint32_t t = 1000; t < 30 * 60 * S; t += 500) played = played || none.choose(t, true, false, false).play;
    check(!played, "a pet without a toy it plays with never plays");

    FreeTime f;
    uint32_t first = 0;
    for (uint32_t t = 1000; t < 10 * 60 * S && !first; t += 500)
        if (f.choose(t, true, false, true).play) first = t;
    check(first >= 1000 + FreeTime::PLAY_FIRST_MIN && first <= 1000 + FreeTime::PLAY_FIRST_MAX + 500,
          "the first game comes after 1-2.5 min (" + std::to_string(first / S) + " s)");
    const FreeTime::Choice c = f.choose(first + 500, true, false, true);
    check(c.play && c.look == Look::Act, "a game is played as steps (Look::Act)");
    f.played(first + 20 * S);
    uint32_t next = 0;
    for (uint32_t t = first + 20 * S; t < first + 20 * S + 10 * 60 * S && !next; t += 500)
        if (f.choose(t, true, false, true).play) next = t;
    const uint32_t gap = next - (first + 20 * S);
    check(next && gap >= FreeTime::PLAY_GAP_MIN && gap <= FreeTime::PLAY_GAP_MAX + 500,
          "the next game comes 3-6 min after one ends (" + std::to_string(gap / S) + " s)");
    uint32_t end = 0;
    for (uint32_t t = next; t < next + 5 * 60 * S && !end; t += 500)
        if (!f.choose(t, true, false, true).play) end = t;
    check(end && end - next <= FreeTime::PLAY_MAX + 500, "a game nobody ends stops at PLAY_MAX");
    bool soon = false;
    for (uint32_t t = end; t < end + FreeTime::PLAY_GAP_MIN - 1000; t += 500)
        soon = soon || f.choose(t, true, false, true).play;
    check(!soon, "and the next waits its gap");
    check(f.choose(end, false, true, true).look == Look::Sleep, "bedtime still wins");
}

}  // namespace

// An `on` note's "do" takes turns with the pet's own life; its line and effect stay.
void on_rounds() {
    Brain b;
    tell(b, R"({"from":"music","state":"on","do":"dance","effect":"notes","line":"a song"})", 1000);
    check(b.decide(1000) == Look::Act && b.decide(1000 + Brain::ON_DO_MS - 1000) == Look::Act,
          "a song: it dances for its round");
    const uint32_t rest = 1000 + Brain::ON_DO_MS + 1000;
    const Brain::NoteView n = note(b, "music", "", rest);
    check(b.decide(rest) != Look::Act && n.why.find("rests between rounds") != std::string::npos &&
              shows(n, "line") && shows(n, "effect"),
          "then the pet's own life, with the song's line and notes still showing: " + n.why);
    const uint32_t again = 1000 + Brain::ON_DO_MS + Brain::ON_REST_MS + 1000;
    for (uint32_t t = rest; t < again; t += 1000) b.decide(t);
    check(b.decide(again) == Look::Act, "and it dances again in the next round");

    // As the music helper tells it: every 5 s, with the time left as "for".
    Brain h;
    bool rested = false;
    for (uint32_t t = 1000; t < 1000 + Brain::ON_DO_MS + 20000; t += 5000) {
        const std::string left = std::to_string(300 - (t - 1000) / 1000);
        tell(h, R"({"from":"music","state":"on","do":"dance","effect":"notes","line":"a song","for":)" + left + "}", t);
        for (uint32_t u = t; u < t + 5000; u += 500) rested = rested || h.decide(u) != Look::Act;
    }
    check(rested, "renewed every 5 s with the time left, it still rests between rounds");
}

// A sender's quiet holds back the others, not the sender itself: a meeting watches its desk and says so once.
void own_quiet() {
    Brain b;
    report(b, R"({"from":"mac","keys":0,"idle":0,"state":"awake","next":60})", 500);
    tell(b, R"({"from":"ci","state":"working","line":"building"})", 1000);
    tell(b, R"({"from":"calendar","state":"quiet","do":"watch","line":"in a meeting","for":600})", 2000);
    tell(b, R"({"from":"calendar","id":"start","say":"Meeting time"})", 2000);
    const Look first = b.decide(2000);
    check(first == Look::Act && b.saying(2000) == "Meeting time",
          "the calendar's own look and words show through its quiet: " + std::to_string(int(first)) + " \"" +
              b.saying(2000) + "\" " + b.why());
    check(note(b, "ci", "", 2000).why == "quiet is on", "another sender's body waits: " + note(b, "ci", "", 2000).why);
    for (uint32_t t = 2000; t <= 2000 + Brain::MOMENT_MS + 1000; t += 500) b.decide(t);
    check(b.saying(2000 + Brain::MOMENT_MS + 1000).empty(), "the words were a moment: said once");
    check(b.decide(2000 + Brain::MOMENT_MS + 1000) == Look::Act, "and it goes on watching its desk");
    b.quiet_press(20000);
    tell(b, R"({"from":"calendar","id":"start","say":"Meeting time"})", 21000);
    check(b.saying(21000).empty(), "the user's own quiet holds back the calendar too");
}

int main() {
    a_tell_replaces_the_note();
    on_rounds();
    own_quiet();
    endings();
    lifetimes();
    moments();
    questions_never_hang();
    results_wait_for_the_user();
    until_seen_and_back();
    the_order_of_importance();
    turns_and_shares();
    parts();
    quiet();
    quiet_shows_on_the_device();
    the_users_hand();
    the_users_activity();
    typing_speed();
    telling_again();
    the_pets_own_life();
    free_time_games();
    warnings();
    limits();
    timers_across_the_wrap();
    signs_effects_and_off();
    return test::summary();
}
