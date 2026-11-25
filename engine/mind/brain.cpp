#include "mind/brain.h"
#include <algorithm>
#include <cmath>
#include "util/deadline.h"
#include "util/text.h"

namespace nestbox {

namespace {
// A pastime step's way of moving: true for the plain move, or one of the pet's animations ("swim": "SwimBall").
void read_move(const Fields& f, const char* key, Act::Move move, Act& act) {
    const std::string anim = f.str(key);
    if (f.flag(key)) {
        act.move = move;
    } else if (!anim.empty() && anim != "false") {
        act.move = move;
        act.move_anim = anim;
    }
}

constexpr uint32_t MIN_WORK_MS = 1000;         // once working, it keeps at it this long before slowing or stopping
constexpr uint32_t DEFAULT_REACTION_MS = 3000;
constexpr uint32_t STEPS_MAX_MS = 10 * 60 * 1000;   // a moment's steps that never end are cut off here
constexpr int BED_FROM = 23 * 60, BED_TO = 7 * 60;  // minutes after midnight, its own clock
constexpr uint16_t INK = CREAM, GREY = rgb(0x8a, 0x8f, 0x99);   // lines: the screen's ink, or grey once stale

// The order of importance: lower comes first.
int rank_of(State s) {
    switch (s) {
        case State::Asking: return 1;
        case State::Done: case State::Failed: case State::None: return 2;
        case State::Working: return 3;
        case State::Failing: return 4;
        case State::On: return 5;
        case State::Quiet: return 6;
        case State::Over: break;
    }
    return 7;
}

uint32_t default_ms(State s) {
    switch (s) {
        case State::Working: return Brain::WORKING_MS;
        case State::Asking: return Brain::ASKING_MS;
        case State::Failing: return Brain::FAILING_MS;
        case State::On: return Brain::ON_MS;
        case State::Quiet: return Brain::QUIET_MS;
        default: break;
    }
    return Brain::MOMENT_MS;
}

bool part_of(const std::string& child, const std::string& parent) {
    return child.size() > parent.size() && child.compare(0, parent.size(), parent) == 0 && child[parent.size()] == '/';
}

// "12m", "45s", "2h": a timer gauge's label.
std::string time_left(uint32_t ms) {
    const uint32_t s = (ms + 999) / 1000;
    if (s >= 3600) return std::to_string((s + 1799) / 3600) + "h";
    if (s >= 60) return std::to_string((s + 59) / 60) + "m";
    return std::to_string(s) + "s";
}

const char* doing_word(State s) {
    switch (s) {
        case State::Working: return " is working";
        case State::Asking: return " is asking";
        case State::Failing: return " is failing";
        case State::On: return " is on";
        case State::Quiet: return " asked for quiet";
        case State::Done: return " is done";
        case State::Failed: return " failed";
        default: break;
    }
    return " asked for it";
}
}  // namespace

std::string Brain::key_of(const std::string& name) { return ascii_lower(name); }

Brain::Kept* Brain::find(const std::string& key) {
    auto i = notes_.find(key);
    return i == notes_.end() ? nullptr : &i->second;
}

// ---- Telling --------------------------------------------------------------------------------------------------

Brain::Told Brain::tell(const Fields& f, uint32_t now) {
    update(now);
    Told t;
    Note n;
    t.used = parse_tell(f, n, t.warnings) || f.values.empty();
    t.from = n.from;
    t.id = n.id;
    t.state = state_key(n.state);
    if (!t.used) return t;
    const std::string key = key_of(n.name());
    if (n.state == State::Over || n.empty()) {
        t.ended = end_notes(key);
        t.state = "over";
        return t;
    }
    if (n.state == State::None && n.until == Until::None && quiet_except(ascii_lower(n.from), now)) {
        t.why = "quiet is on, so this was skipped";
        return t;
    }
    Kept k;
    Kept* old = find(key);
    k.first = old ? old->first : stamp(now);
    k.note = n;
    k.told = stamp(now);
    k.counted_to = now;
    k.rank = rank_of(n.state);
    if (old && old->note.state == n.state && old->note.say == n.say) {
        k.seen = old->seen;
        k.dismissed = old->dismissed;
    }
    // Telling a lasting note again restarts nothing on the screen; a moment told again plays again.
    const bool same_body = old && is_lasting(n.state) && old->note.state == n.state && old->note.body == n.body;
    if (same_body) {
        k.chain = std::move(old->chain);
        k.shown_from = old->shown_from;
        k.turn_from = old->turn_from;      // nor does it start a new turn
    } else {
        k.turn_from = stamp(now);
    }
    notes_[key] = std::move(k);
    Kept& kept = notes_[key];
    if (!same_body) finish_steps(kept, now);
    trim(key, t.warnings);
    const Pick p = pick(now);
    t.showing = showing(kept, p);
    if (!kept.shown_from && !t.showing.empty()) kept.shown_from = stamp(now);
    t.ends_in = ends_in(kept, now);
    t.why = why_not(kept, p, now);
    return t;
}

void Brain::tell_device(const Note& n, uint32_t now) {
    Kept k;
    k.note = n;
    k.note.from = DEVICE_SENDER;
    k.first = k.told = stamp(now);
    k.counted_to = now;
    k.rank = rank_of(n.state);
    const std::string key = key_of(k.note.name());
    notes_[key] = std::move(k);
    finish_steps(notes_[key], now);
}

void Brain::end_device() {
    for (auto i = notes_.begin(); i != notes_.end();)
        i = part_of(i->first, DEVICE_SENDER) ? notes_.erase(i) : std::next(i);
}

void Brain::end_device(const std::string& id) { notes_.erase(key_of(std::string(DEVICE_SENDER) + "/" + id)); }

int Brain::end_sender(const std::string& from) { return end_notes(key_of(from)); }

// Open-ended steps take their time from the note: 10 s in a list, the note's time in a moment, for as long as the
// note lasts in a lasting state, where the steps repeat.
void Brain::finish_steps(Kept& k, uint32_t now) {
    if (!k.note.body.set()) return;
    std::vector<Step> steps = k.note.body.value;
    const bool lasting = is_lasting(k.note.state);
    for (Step& s : steps) {
        if (!s.open_ended) continue;
        s.ends = Step::Ends::Time;
        s.ms = k.note.body_list ? Step::IN_LIST_MS
             : lasting          ? Step::MAX_MS
             : k.note.for_ms    ? k.note.for_ms
                                : MOMENT_MS;
    }
    k.chain.start(std::move(steps), now, lasting);
}

int Brain::end_notes(const std::string& key) {
    int n = 0;
    for (auto i = notes_.begin(); i != notes_.end();) {
        if (i->first == key || part_of(i->first, key)) {
            i = notes_.erase(i);
            n++;
        } else {
            ++i;
        }
    }
    return n;
}

// Keeps the notes few: a sender's oldest makes room for its newest, and the oldest of all for anyone's.
void Brain::trim(const std::string& keep, std::vector<std::string>& warnings) {
    const std::string from = keep.substr(0, keep.find('/'));
    auto oldest = [&](bool same_sender) {
        auto best = notes_.end();
        size_t count = 0;
        for (auto i = notes_.begin(); i != notes_.end(); ++i) {
            if (same_sender && i->first.substr(0, i->first.find('/')) != from) continue;
            count++;
            if (i->first == keep || part_of(i->first, DEVICE_SENDER) || i->first == DEVICE_SENDER) continue;
            if (best == notes_.end() || is_before(i->second.told, best->second.told)) best = i;
        }
        return std::make_pair(best, count);
    };
    for (;;) {
        auto [i, count] = oldest(true);
        if (count <= MAX_PER_SENDER || i == notes_.end()) break;
        warnings.push_back(from + " has " + std::to_string(MAX_PER_SENDER) + " notes; \"" + i->second.note.name() +
                           "\" made room");
        notes_.erase(i);
    }
    for (;;) {
        auto [i, count] = oldest(false);
        if (count <= MAX_NOTES || i == notes_.end()) break;
        warnings.push_back("the pet keeps " + std::to_string(MAX_NOTES) + " notes; \"" + i->second.note.name() +
                           "\" made room");
        notes_.erase(i);
    }
}

// ---- Lifetimes ------------------------------------------------------------------------------------------------

// What is meant for the user waits for the user: a question's own hour, a result's own moment, and "until back"
// count only while the user is there.
bool Brain::counts_presence(const Kept& k) const {
    const Note& n = k.note;
    if (n.until == Until::Back) return true;
    if (n.for_ms || n.until == Until::Seen) return false;
    return n.state == State::Asking || is_result(n.state);
}

uint32_t Brain::presence_length(const Kept& k) const {
    return k.note.state == State::Asking && !k.note.for_ms ? ASKING_MS : MOMENT_MS;
}

bool Brain::waits_for_parts(const Kept& k, uint32_t now) const {
    if (!is_result(k.note.state)) return false;
    const std::string key = key_of(k.note.name());
    for (const auto& [other, o] : notes_)
        if (part_of(other, key) && o.note.state == State::Working && !ended(o, now)) return true;
    return false;
}

bool Brain::ended(const Kept& k, uint32_t now) const {
    const Note& n = k.note;
    const uint32_t age = now - k.told;
    if (age >= DAY_MS) return true;
    if (n.for_ms && age >= n.for_ms) return true;
    if (n.until == Until::Seen) return age >= WAIT_MAX_MS;
    const bool steps_running = n.body.set() && !k.chain.done();
    if (counts_presence(k)) {
        if (age >= WAIT_MAX_MS) return true;
        if (k.counted_ms < presence_length(k)) return false;
        return !steps_running || (k.shown_from && now - k.shown_from >= STEPS_MAX_MS);
    }
    if (n.for_ms) return false;
    if (is_lasting(n.state)) return age >= default_ms(n.state);
    if (!k.shown_from) return age >= MOMENT_WAIT_MS;   // a moment that never got to show gives up
    if (now - k.shown_from < MOMENT_MS) return false;
    return !steps_running || now - k.shown_from >= STEPS_MAX_MS;
}

bool Brain::quiet(uint32_t now) const {
    for (const auto& [key, k] : notes_)
        if (k.note.state == State::Quiet && !ended(k, now)) return true;
    return false;
}

// Quiet from another sender: a sender's own quiet never skips its own tells (see hushed_for).
bool Brain::quiet_except(const std::string& sender, uint32_t now) const {
    for (const auto& [k, n] : notes_)
        if (sender_of(n) != sender && n.note.state == State::Quiet && !ended(n, now)) return true;
    return false;
}

// Quiet holds back every note but questions and the notes of the sender whose quiet it is: a calendar that says
// "in a meeting" still shows its own look and words, and everyone else waits. The user's quiet (from the device)
// holds back every sender.
bool Brain::hushed_for(const Kept& k, uint32_t now) const {
    if (k.note.state == State::Asking) return false;
    const std::string mine = sender_of(k);
    for (const auto& [key, q] : notes_)
        if (q.note.state == State::Quiet && !ended(q, now) && sender_of(q) != mine) return true;
    return false;
}

void Brain::update(uint32_t now) {
    if (forced_until_ && !live(forced_until_, now)) forced_until_ = 0;
    if (reaction_until_ && !live(reaction_until_, now)) reaction_until_ = 0;
    const bool there = user_.there(now);
    for (auto& [key, k] : notes_) {
        if (counts_presence(k)) {
            const bool counts = there && !hushed_for(k, now) && !waits_for_parts(k, now);
            if (counts) k.counted_ms += now - k.counted_to;
        }
        k.counted_to = now;
    }
    for (auto i = notes_.begin(); i != notes_.end();) i = ended(i->second, now) ? notes_.erase(i) : std::next(i);
}

// ---- What shows -----------------------------------------------------------------------------------------------

bool Brain::visible(const Kept& k, uint32_t now) const {
    if (k.dismissed) return false;
    const Note& n = k.note;
    const bool hushed = hushed_for(k, now);
    if (hushed && (is_result(n.state) || n.until != Until::None)) return false;   // they wait for quiet to end
    if (hushed && n.state == State::None) return false;
    return !waits_for_parts(k, now);
}

bool Brain::stale(const Kept& k) const {
    if (k.note.state != State::Asking) return false;
    if (k.note.for_ms) return false;
    return k.counted_ms * 2 >= ASKING_MS;
}

bool Brain::calling(const Kept& k, uint32_t now) const {
    if (k.note.state != State::Asking || k.seen || k.dismissed) return false;
    const uint32_t since = now - k.told;
    if (since < CALL_MS) return true;
    if (stale(k)) return false;
    return since >= RECALL_EVERY_MS && since % RECALL_EVERY_MS < RECALL_MS;
}

std::optional<Look> Brain::default_look(const Kept& k) const {
    switch (k.note.state) {
        case State::Working: return Look::Watch;
        case State::Asking: return Look::Alert;
        case State::Failing: return Look::Slump;
        case State::Done: return Look::Celebrate;
        case State::Failed: return Look::Slump;
        default: break;
    }
    return std::nullopt;
}

// The order of importance for the body: a question, then moments, then a "do" a sender chose on a lasting note
// during its turn, then the lasting states (working before failing). Lower comes first.
int Brain::body_rank(const Kept& k, uint32_t now) const {
    const State s = k.note.state;
    if (s == State::Asking) return 1;
    if (!is_lasting(s)) return 2;
    if (k.note.body.set() && now - k.turn_from < TURN_MS) return 3;
    return s == State::Working ? 4 : s == State::Failing ? 5 : 6;
}

uint32_t Brain::share_used(const std::string& sender) const {
    auto i = share_used_.find(sender);
    return i == share_used_.end() ? 0 : i->second;
}

// Whether `sender` may take the body now, or keep it.
bool Brain::has_share(const std::string& sender) const {
    const uint32_t used = share_used(sender);
    return sender == share_holder_ ? used < SHARE_MS : used + SHARE_AGAIN_MS <= SHARE_MS;
}

// Counts the time since the last frame: the sender that had the body uses its share if it was draining it, and the
// others get theirs back.
void Brain::count_share(uint32_t now) {
    const uint32_t dt = share_counted_to_ ? std::min(now - share_counted_to_, SHARE_BACK_MS) : 0;
    const uint32_t back = uint32_t(uint64_t(dt) * SHARE_MS / SHARE_BACK_MS);
    for (auto i = share_used_.begin(); i != share_used_.end();) {
        if (share_draining_ && i->first == share_holder_) {
            ++i;
            continue;
        }
        i->second = i->second > back ? i->second - back : 0;
        i = i->second ? std::next(i) : share_used_.erase(i);
    }
    if (share_draining_ && !share_holder_.empty() && dt) {
        uint32_t& used = share_used_[share_holder_];
        used = std::min(used + dt, SHARE_MS);
    }
    share_counted_to_ = stamp(now);
}

bool Brain::on_rests(const Kept& k, uint32_t now) const {
    return k.note.state == State::On && k.note.body.set() && (now - k.turn_from) % (ON_DO_MS + ON_REST_MS) >= ON_DO_MS;
}

bool Brain::has_body(const Kept& k, uint32_t now) const {
    const Note& n = k.note;
    if (n.body.off()) return false;
    if (n.state == State::Asking && !calling(k, now)) return false;
    if (n.state == State::On && user_.typing(now)) return false;   // over what is simply going on, even its "do"
    if (on_rests(k, now)) return false;
    if (n.body.set()) return !k.chain.done();
    if (n.state == State::Working && user_.typing(now)) return false;   // the user's typing shows over an agent's
    // A result waiting for the user plays its look for its moment, then rests until they are back; its line waits.
    if (is_result(n.state) && counts_presence(k) && !user_.there(now) && k.shown_from &&
        now - k.shown_from >= MOMENT_MS)
        return false;
    return default_look(k).has_value();
}

std::vector<const Brain::Kept*> Brain::ranked(uint32_t now) const {
    std::vector<const Kept*> out;
    for (const auto& [key, k] : notes_)
        if (visible(k, now)) out.push_back(&k);
    std::stable_sort(out.begin(), out.end(), [](const Kept* a, const Kept* b) {
        if (a->rank != b->rank) return a->rank < b->rank;
        return is_after(a->told, b->told);
    });
    return out;
}

std::string Brain::line_text(const Kept& k, uint32_t now) const {
    const Note& n = k.note;
    if (n.line.off()) return "";
    if (n.line.set()) return n.line.value;
    switch (n.state) {
        case State::Working: return n.from + " working";
        case State::Asking: {
            const uint32_t minutes = (now - k.told) / MINUTE_MS;
            return n.from + " needs you" + (minutes ? " - " + std::to_string(minutes) + " min" : "");
        }
        case State::Failing: return n.from + " failing";
        case State::Done: return n.from + " done";
        case State::Failed: return n.from + " failed";
        default: break;
    }
    return "";
}

uint16_t Brain::line_color(const Kept& k) const {
    if (stale(k)) return GREY;
    if (!k.note.color.empty()) return color_value(k.note.color);
    if (k.note.state == State::Failing || k.note.state == State::Failed) return RED;
    return INK;
}

Brain::Pick Brain::pick(uint32_t now) const {
    Pick p;
    const std::vector<const Kept*> list = ranked(now);
    auto hushed = [&](const Kept* k) { return hushed_for(*k, now); };
    auto key = [](const Kept* k) { return key_of(k->note.name()); };
    // The body has its own order (body_rank), and is shared: a sender with no share left gives way to the next.
    std::vector<const Kept*> bodies;
    for (const Kept* k : list)
        if (!hushed(k) && has_body(*k, now)) bodies.push_back(k);
    std::stable_sort(bodies.begin(), bodies.end(), [&](const Kept* x, const Kept* y) {
        const int a = body_rank(*x, now), b = body_rank(*y, now);
        if (a != b) return a < b;
        return is_after(x->told, y->told);
    });
    const Kept* chosen = nullptr;
    for (const Kept* k : bodies)
        if (has_share(sender_of(*k))) {
            chosen = k;
            break;
        }
    for (const Kept* k : bodies)   // nobody has a share left: the one that has the body keeps it
        if (!chosen && sender_of(*k) == share_holder_) chosen = k;
    if (!chosen && !bodies.empty()) {   // or else the one with the most left
        chosen = bodies.front();
        for (const Kept* k : bodies)
            if (share_used(sender_of(*k)) < share_used(sender_of(*chosen))) chosen = k;
    }
    if (chosen) {
        p.body = key(chosen);
        p.body_look = chosen->note.body.set() ? Look::Act : *default_look(*chosen);
        const std::string mine = sender_of(*chosen);
        p.draining = body_rank(*chosen, now) <= 3 &&
                     std::any_of(bodies.begin(), bodies.end(), [&](const Kept* k) { return sender_of(*k) != mine; });
    }
    // The bubble and the sign go with the body: of notes that rank the same, the one it plays for comes first.
    std::vector<const Kept*> voices = list;
    if (chosen) {
        const auto at = std::find(voices.begin(), voices.end(), chosen);
        std::rotate(std::find_if(voices.begin(), at, [&](const Kept* k) { return k->rank == chosen->rank; }), at,
                    at + 1);
    }
    for (const Kept* k : voices) {
        if (hushed(k)) continue;
        if (k->note.state == State::Asking && k->seen) continue;
        if (k->note.say.set() && !k->note.say.value.empty()) {
            p.bubble = key(k);
            break;
        }
    }
    for (const Kept* k : list) {
        if (hushed(k)) continue;
        if (k->note.caption.set() && !k->note.caption.value.empty()) {
            p.caption = key(k);
            break;
        }
    }
    for (const Kept* k : voices) {
        if (hushed(k)) continue;
        Bubble b = Bubble::None;
        if (k->note.sign.set()) b = k->note.sign.value;
        else if (!k->note.sign.off() && k->note.state == State::Asking) b = Bubble::Question;
        else if (!k->note.sign.off() && k->note.state == State::Failing) b = Bubble::Bang;
        if (b == Bubble::None) continue;
        p.sign = key(k);
        p.sign_value = b;
        break;
    }
    for (const Kept* k : list) {
        if (hushed(k)) continue;
        if (!k->note.effect.set()) continue;
        p.effect = key(k);
        p.effect_value = k->note.effect.value;
        break;
    }
    std::vector<const Kept*> by_first = list;
    std::stable_sort(by_first.begin(), by_first.end(),
                     [](const Kept* a, const Kept* b) { return is_before(a->first, b->first); });
    for (const Kept* k : by_first) {
        const std::string text = line_text(*k, now);
        if (!text.empty()) p.lines.push_back({key(k), text});
        if (k->note.gauge.set()) p.gauges.push_back(key(k));
    }
    if (p.gauges.size() > MAX_GAUGES) p.gauges.erase(p.gauges.begin(), p.gauges.end() - MAX_GAUGES);
    return p;
}

std::vector<std::string> Brain::showing(const Kept& k, const Pick& p) const {
    const std::string key = key_of(k.note.name());
    std::vector<std::string> out;
    if (p.body == key) out.push_back("do");
    if (p.bubble == key) out.push_back("say");
    if (p.caption == key) out.push_back("caption");
    if (std::any_of(p.lines.begin(), p.lines.end(), [&](auto& l) { return l.first == key; })) out.push_back("line");
    if (std::find(p.gauges.begin(), p.gauges.end(), key) != p.gauges.end()) out.push_back("gauge");
    if (p.sign == key) out.push_back("sign");
    if (p.effect == key) out.push_back("effect");
    return out;
}

long Brain::ends_in(const Kept& k, uint32_t now) const {
    const Note& n = k.note;
    const uint32_t age = now - k.told;
    long ms;
    if (n.for_ms) ms = long(n.for_ms) - long(age);
    else if (n.until == Until::Seen) ms = long(WAIT_MAX_MS) - long(age);
    else if (counts_presence(k)) ms = long(presence_length(k)) - long(k.counted_ms);
    else if (is_lasting(n.state)) ms = long(default_ms(n.state)) - long(age);
    else ms = long(MOMENT_MS) - (k.shown_from ? long(now - k.shown_from) : 0);
    return std::max(0L, (ms + 999) / 1000);
}

std::string Brain::why_not(const Kept& k, const Pick& p, uint32_t now) const {
    const Note& n = k.note;
    const std::string key = key_of(n.name());
    if (k.dismissed) return "the user dismissed it; it stays hidden until its state changes";
    if (waits_for_parts(k, now)) {
        int parts = 0;
        for (const auto& [other, o] : notes_)
            if (part_of(other, key) && o.note.state == State::Working) parts++;
        return "waiting for " + std::to_string(parts) + (parts == 1 ? " part that is" : " parts that are") +
               " still working";
    }
    const bool hushed = hushed_for(k, now);
    if (hushed && (is_result(n.state) || n.until != Until::None)) return "quiet is on; it waits until quiet ends";
    if (n.until == Until::Seen) return "it stays until the user taps the pet";
    if (counts_presence(k) && !user_.there(now)) return "the user is away; it stays until they are back";
    if (n.until == Until::Back) return "it stays until the user is back at the computer";
    if (n.state == State::Asking && k.seen) return "the user has seen it; it waits for an answer";
    const bool wants_body = n.body.set() || (!n.body.off() && default_look(k).has_value());
    if (wants_body && p.body != key && !(n.state == State::Asking && !calling(k, now))) {
        if (((n.state == State::Working && !n.body.set()) || n.state == State::On) && user_.typing(now))
            return "the user is typing";
        if (on_rests(k, now)) return "its \"do\" rests between rounds; the pet lives its own life for now";
        if (hushed) return "quiet is on";
        if (!p.body.empty()) {
            auto owner = notes_.find(p.body);
            if (owner != notes_.end()) {
                const std::string has = owner->second.note.name() + doing_word(owner->second.note.state) +
                                        " and has the body";
                if (n.body.set() && is_lasting(n.state) && now - k.turn_from >= TURN_MS)
                    return "its \"do\" had its turn; " + has;
                if (sender_of(owner->second) != sender_of(k) && !has_share(sender_of(k)))
                    return n.from + " had its share of the body for now; " + has;
                return has;
            }
        }
    }
    return "";
}

// ---- The user's hand ------------------------------------------------------------------------------------------

std::string Brain::tap(uint32_t now) {
    update(now);
    bool any = false;
    for (auto i = notes_.begin(); i != notes_.end();) {
        Kept& k = i->second;
        if (k.dismissed || part_of(i->first, DEVICE_SENDER)) {
            ++i;
            continue;
        }
        if (k.note.state == State::Asking && !k.seen) {
            k.seen = true;
            any = true;
            ++i;
        } else if ((is_result(k.note.state) && visible(k, now)) || k.note.until != Until::None) {
            i = notes_.erase(i);
            any = true;
        } else {
            ++i;
        }
    }
    if (any) {
        show_reaction(Look::Nod, now);
        return "seen";
    }
    show_reaction(Look::Petted, now);
    return "petted";
}

std::string Brain::dismiss(uint32_t now) {
    update(now);
    bool any = false;
    for (auto& [key, k] : notes_) {
        if (k.note.state == State::Quiet || part_of(key, DEVICE_SENDER) || k.dismissed || !visible(k, now)) continue;
        k.dismissed = true;
        any = true;
    }
    if (!any) return "";
    show_reaction(Look::Nod, now);
    return "dismissed";
}

// The long press. It shows both ways: a nod, then "quiet" in the status bar for as long as it lasts, or "quiet off"
// for a moment.
std::string Brain::quiet_press(uint32_t now) {
    update(now);
    const std::string device_quiet = std::string(DEVICE_SENDER) + "/quiet",
                      quiet_off = std::string(DEVICE_SENDER) + "/quiet-off";
    show_reaction(Look::Nod, now);
    Note n;
    if (find(device_quiet)) {
        for (auto i = notes_.begin(); i != notes_.end();)
            i = i->second.note.state == State::Quiet ? notes_.erase(i) : std::next(i);
        n.id = "quiet-off";
        n.state = State::On;
        n.line = {Part<std::string>::Kind::Set, "quiet off"};
        n.for_ms = QUIET_OFF_MS;
        tell_device(n, now);
        return "quiet off";
    }
    notes_.erase(quiet_off);
    n.id = "quiet";
    n.state = State::Quiet;
    n.line = {Part<std::string>::Kind::Set, "quiet"};
    n.for_ms = DEVICE_QUIET_MS;
    tell_device(n, now);
    return "quiet on";
}

std::string Brain::shake(uint32_t now) {
    show_reaction(Look::Dizzy, now);
    return "dizzy";
}

std::string Brain::computer(bool asleep, uint32_t now) {
    user_.computer(asleep, now);
    return asleep ? "computer asleep" : "computer awake";
}

void Brain::show_reaction(Look l, uint32_t now, uint32_t for_ms) {
    reaction_ = l;
    const uint32_t length = info(l).length_ms ? info(l).length_ms : DEFAULT_REACTION_MS;
    reaction_until_ = deadline(now, for_ms ? for_ms : length);
}

// ---- Deciding ---------------------------------------------------------------------------------------------------

Look Brain::decide(uint32_t now) {
    update(now);
    playing_ = false;
    count_share(now);
    const bool reacting = live(reaction_until_, now);
    const Pick p = pick(now);
    for (auto& [key, k] : notes_)
        if (!k.shown_from && !showing(k, p).empty()) k.shown_from = stamp(now);
    body_owner_ = p.body_look == Look::Act ? p.body : "";
    auto owner = notes_.find(p.body);
    share_holder_ = owner == notes_.end() ? "" : sender_of(owner->second);
    share_draining_ = p.draining;
    paused_ = false;
    Look s;
    if (reacting) {
        s = reaction_;
        why_ = s == Look::Wake ? "waking up" : "reacting to the user";
    } else if (held_) {
        s = held_look_;
        why_ = "held by a key";
    } else if (pastime_held_) {                        // the P key's pastime, played through
        body_owner_.clear();
        s = Look::Act;
        playing_ = true;
        why_ = "its own life: " + game_name_;
    } else if (!p.body.empty()) {
        s = p.body_look;
        why_ = notes_.at(p.body).note.name() + doing_word(notes_.at(p.body).note.state);
    } else if (quiet(now)) {
        s = Look::Sit;
        why_ = "quiet is on";
    } else {
        s = steady(own_life(now), now);
    }
    s = with_transitions(s, now, reacting);
    if (s != last_) {
        last_ = s;
        last_since_ = now;
    }
    return s;
}

Look Brain::own_life(uint32_t now) {
    const bool typing = user_.typing(now);
    const bool fast = user_.typing_fast(typing, now);
    if (typing && fast) {
        why_ = "the user is typing fast";
        return Look::TypeFast;
    }
    if (typing) {
        why_ = "the user is typing";
        return Look::Type;
    }
    if (user_.pausing(now)) {
        why_ = "the user paused typing";
        paused_ = true;
        return Look::Type;
    }
    if (user_.all_gone(now)) {
        why_ = "the computer is asleep or out of reach";
        return Look::Sleep;
    }
    if (user_.locked(now)) {
        why_ = "the screen is locked";
        return Look::Sleep;
    }
    if (user_.asleep(now)) {
        why_ = "the user is away";
        return Look::Sleep;
    }
    const FreeTime::Choice choice = free_time_.choose(now, user_.heard(now), bedtime(now), can_play_);
    why_ = choice.look == Look::Sleep ? "it is night" : choice.play ? "its own life: " + game_name_ : "its own life";
    playing_ = choice.play;
    if (playing_ && game_.empty() && picked_ == NO_PASTIME) {   // named now, so the reason says it from the start
        picked_ = pick_pastime(now);
        why_ = "its own life: " + game_name_;
    }
    return choice.look;
}

// Once working it keeps at it for MIN_WORK_MS before it slows or stops, so it doesn't flap.
Look Brain::steady(Look s, uint32_t now) {
    const bool working = last_ == Look::Type || last_ == Look::TypeFast;
    const bool calmer = s == Look::Idle || s == Look::Wander || (last_ == Look::TypeFast && s == Look::Type);
    if (s != last_ && working && calmer && now - last_since_ < MIN_WORK_MS) {
        s = last_;
        paused_ = s == Look::Type;
    }
    return s;
}

// Dozing off starts with a yawn. Out of sleep it wakes up first; a reaction that woke it plays right after.
Look Brain::with_transitions(Look s, uint32_t now, bool reacting) {
    Look body = s;
    if (s == Look::Act && !body_owner_.empty()) {
        const Kept& k = notes_.at(body_owner_);
        if (!k.chain.empty() && k.chain.current().act.kind == Act::Kind::Look) body = k.chain.current().act.look;
    }
    if (body == Look::Sleep && last_body_ != Look::Sleep && last_body_ != Look::Yawn && !reacting && !held_) {
        show_reaction(Look::Yawn, now);
        why_ = "getting sleepy";
        last_body_ = Look::Yawn;
        return Look::Yawn;
    }
    if (last_body_ == Look::Sleep && body != Look::Sleep && body != Look::Wake) {
        if (reacting) {
            pending_ = s;
            pending_ms_ = reaction_until_ - now;
        }
        show_reaction(Look::Wake, now);
        why_ = "waking up";
        last_body_ = Look::Wake;
        return Look::Wake;
    }
    if (!reacting && pending_ms_) {
        show_reaction(pending_, now, pending_ms_);
        pending_ms_ = 0;
        why_ = "reacting to the user";
        last_body_ = pending_;
        return pending_;
    }
    last_body_ = body;
    return s;
}

// The throwing game: a few throws from each of GAME_SPOTS places round its floor, apart from each other and clear of
// what it throws at.
std::vector<Step> Brain::game_steps(const Pet& pet) {
    const Stage& st = pet.stage();
    const Area& a = st.roaming_area();
    const std::optional<Pixel> target = st.target();
    std::vector<Point> spots;
    const auto fraction = [this] { return float(free_time_.random_between(0, 1000)) / 1000; };
    for (int tries = 0; tries < GAME_SPOT_TRIES && int(spots.size()) < GAME_SPOTS; tries++) {
        const float x = a.x0 + fraction() * (a.x1 - a.x0), y = a.y0 + fraction() * (a.y1 - a.y0);
        if (!st.reachable(x, y) || st.hidden(x, y)) continue;
        if (target && std::hypot(x - target->x, y - target->y) < GAME_TARGET_CLEAR_PX) continue;
        bool apart = true;
        for (const Point& p : spots) apart = apart && std::hypot(x - p.x, y - p.y) >= GAME_SPOT_APART_PX;
        if (!apart && tries < GAME_SPOT_STRICT_TRIES) continue;
        spots.push_back({x, y});
    }
    std::vector<Step> steps;
    for (const Point& p : spots) {
        Step s;
        s.word = "throw";
        s.ends = Step::Ends::Act;
        s.act.kind = Act::Kind::Throw;
        s.act.spot = {Spot::Kind::Point, p.x, p.y};
        s.act.count = int(free_time_.random_between(GAME_THROWS_MIN, GAME_THROWS_MAX));
        steps.push_back(s);
    }
    return steps;
}

void Brain::next_pastime(uint32_t now) {
    const int count = int(egg_ ? egg_->pastimes.size() : 0);
    if (!count && !plays_game_) return;
    int next = stepped_ == NO_PASTIME ? (plays_game_ ? GAME : 0) : stepped_ + 1;
    if (next >= count) next = plays_game_ ? GAME : 0;
    stepped_ = picked_ = next;
    game_name_ = pastime_name(next);
    game_.clear();
    free_time_.play_now(now);
    pastime_held_ = true;
}

bool Brain::play_pastime(const std::string& name, uint32_t now) {
    for (size_t i = 0; egg_ && i < egg_->pastimes.size(); i++) {
        if (egg_->pastimes[i].name != name) continue;
        stepped_ = picked_ = int(i);
        game_name_ = pastime_name(int(i));
        game_.clear();
        free_time_.play_now(now);
        pastime_held_ = true;
        return true;
    }
    return false;
}

// One of the pastimes it may play now (its own, within their hours, and the game), at random and not the one it
// played last when there is another; NO_PASTIME if none. Names it.
int Brain::pick_pastime(uint32_t now) {
    std::vector<int> options;
    if (plays_game_) options.push_back(GAME);
    const std::optional<int> minute = minute_of_day(now);
    for (size_t i = 0; egg_ && i < egg_->pastimes.size(); i++) {
        const Pastime& p = egg_->pastimes[i];
        if (p.from_hour >= 0) {
            if (!minute) continue;
            const int h = *minute / 60;
            const bool within = p.from_hour <= p.to_hour ? h >= p.from_hour && h < p.to_hour
                                                         : h >= p.from_hour || h < p.to_hour;
            if (!within) continue;
        }
        options.push_back(int(i));
    }
    if (options.size() > 1) options.erase(std::remove(options.begin(), options.end(), last_pastime_), options.end());
    if (options.empty()) return NO_PASTIME;
    const int pick = options[free_time_.random_between(0, uint32_t(options.size() - 1))];
    game_name_ = pastime_name(pick);
    return pick;
}

std::string Brain::pastime_name(int pick) const {
    if (pick == GAME) return "a game";
    const std::string& name = egg_->pastimes[size_t(pick)].name;
    return name.empty() ? "a pastime" : name;
}

// Its free time's pastime, played like a sender's steps. Something else taking over drops it; when it has played
// through, the next waits.
void Brain::pastime(Pet& pet, uint32_t now) {
    egg_ = pet.egg();
    plays_game_ = pet.plays();
    can_play_ = plays_game_ || (egg_ && !egg_->pastimes.empty());
    if (!playing_ || !can_play_) {
        game_.clear();
        picked_ = NO_PASTIME;
        pastime_held_ = false;
        return;
    }
    if (game_.empty()) {
        const int pick = picked_ != NO_PASTIME ? picked_ : pick_pastime(now);
        picked_ = NO_PASTIME;
        std::vector<Step> steps;
        if (pick == GAME) {
            steps = game_steps(pet);
        } else if (pick != NO_PASTIME) {
            std::vector<std::string> ignored;
            for (const Fields& f : egg_->pastimes[size_t(pick)].steps) {
                Step s;
                const bool asks = StepChain::parse(f, s, ignored);
                s.show = f.str("show");                  // a cued part of its scene (pastimes only)
                read_move(f, "swim", Act::Move::Swim, s.act);   // through its scene's water (pastimes only)
                read_move(f, "jump", Act::Move::Jump, s.act);   // in an arc, into, out of or within it
                if (asks || !s.show.empty()) steps.push_back(s);
            }
        }
        if (steps.empty()) {
            free_time_.played(now);
            pastime_held_ = false;
            return;
        }
        last_pastime_ = pick;
        game_.start(std::move(steps), now);
    }
    game_.drive(pet, now);
    if (game_.done()) {
        game_.clear();
        free_time_.played(now);
        pastime_held_ = false;
    }
}

bool Brain::bedtime(uint32_t now) const {
    const std::optional<int> minute = minute_of_day(now);
    return minute && (*minute >= BED_FROM || *minute < BED_TO);
}

// ---- Showing ------------------------------------------------------------------------------------------------

void Brain::show(Pet& pet, uint32_t now) {
    const Pick p = pick(now);
    Kept* owner = body_owner_.empty() ? nullptr : find(body_owner_);
    for (auto& [key, k] : notes_)
        if (k.note.body.set() && &k != owner) k.chain.pause(now);
    pet.show_part("", 0);                       // a cued scene part shows only while a step shows it
    if (owner) owner->chain.drive(pet, now);   // hands the step to the body before the body turns to it
    pastime(pet, now);

    // Words and a sign that belong to the step playing come first.
    const Step* step = owner && !owner->chain.empty()  ? &owner->chain.current()
                       : playing_ && !game_.empty() ? &game_.current()
                                                    : nullptr;   // a sender's step, or its own pastime's
    const bool step_playing = step && pet.wanted() == Look::Act;
    if (step_playing && !step->say.empty()) {
        pet.say(step->say, owner ? color_value(owner->note.color) : INK, text_size_);
    } else if (const Kept* k = p.bubble.empty() ? nullptr : find(p.bubble)) {
        pet.say(k->note.say.value, color_value(k->note.color), text_size_);
    } else {
        pet.say("", INK, text_size_);
    }
    if (const Kept* k = p.caption.empty() ? nullptr : find(p.caption))
        pet.caption(k->note.caption.value, color_value(k->note.color));
    else pet.caption("", INK);
    pet.mark(step_playing && step->mark != Bubble::None ? step->mark : p.sign_value);
    pet.effect(p.effect_value);

    StatusBar::Lines lines;
    for (const auto& [key, text] : p.lines) lines.push_back({text, line_color(notes_.at(key))});
    pet.status(std::move(lines));
    std::vector<MeterRing> rings;
    for (const std::string& key : p.gauges) {
        const Kept& k = notes_.at(key);
        const Gauge& g = k.note.gauge.value;
        MeterRing r;
        r.color = k.note.color.empty() ? color_value("green") : color_value(k.note.color);
        r.size = text_size_;
        if (g.timer) {
            const uint32_t total = k.note.for_ms ? k.note.for_ms : default_ms(k.note.state);
            const uint32_t left = total > now - k.told ? total - (now - k.told) : 0;
            r.fill = float(left) / float(total);
            r.label = k.note.label.empty() ? time_left(left) : k.note.label;
        } else {
            r.fill = float(g.value / 100.0);
            r.label = k.note.label.empty() ? std::to_string(int(g.value + 0.5)) : k.note.label;
        }
        rings.push_back(r);
    }
    pet.meters(std::move(rings));
    pet.typing(user_.kps(now), !paused_);
    pet.daypart(daypart(now));
}

// ---- Reading ----------------------------------------------------------------------------------------------------

std::vector<Brain::NoteView> Brain::notes(uint32_t now) {
    update(now);
    const Pick p = pick(now);
    std::vector<const Kept*> list;
    for (const auto& [key, k] : notes_) list.push_back(&k);
    std::stable_sort(list.begin(), list.end(),
                     [](const Kept* a, const Kept* b) { return is_before(a->first, b->first); });
    std::vector<NoteView> out;
    for (const Kept* k : list) {
        NoteView v;
        v.from = k->note.from;
        v.id = k->note.id;
        v.state = state_key(k->note.state);
        v.line = line_text(*k, now);
        v.color = k->note.color;
        v.showing = showing(*k, p);
        v.ends_in = ends_in(*k, now);
        v.why = why_not(*k, p, now);
        out.push_back(std::move(v));
    }
    return out;
}

std::string Brain::saying(uint32_t now) {
    const Pick p = pick(now);
    const Kept* owner = body_owner_.empty() ? nullptr : find(body_owner_);
    if (owner && !owner->chain.empty() && !owner->chain.current().say.empty()) return owner->chain.current().say;
    if (const Kept* k = p.bubble.empty() ? nullptr : find(p.bubble)) return k->note.say.value;
    return "";
}

std::string Brain::captioning(uint32_t now) {
    const Pick p = pick(now);
    const Kept* k = p.caption.empty() ? nullptr : find(p.caption);
    return k ? k->note.caption.value : "";
}

std::string Brain::first_line(uint32_t now) {
    const Pick p = pick(now);
    return p.lines.empty() ? "" : p.lines.front().second;
}

std::string Brain::doing(Look l) const {
    if (l == Look::Act && !body_owner_.empty()) {
        auto i = notes_.find(body_owner_);
        if (i != notes_.end() && !i->second.chain.empty()) return i->second.chain.current().words();
    }
    if (l == Look::Act && playing_ && !game_.empty()) return game_.current().words();
    if (l == Look::Sit) return "stay";
    const Look as = l == Look::TypeFast ? Look::Type : l == Look::Act ? Look::Idle : l;
    for (const Behaviour& b : behaviours())
        if (b.look == as) return b.word;
    return look_key(l);
}

size_t Brain::steps_left() const {
    size_t n = 0;
    for (const auto& [key, k] : notes_)
        if (k.note.body.set() && !k.chain.done()) n += k.chain.left();
    return n;
}

void Brain::force_time(std::optional<int> minute, uint32_t now) {
    if (!minute) {
        forced_until_ = 0;
        return;
    }
    forced_minute_ = *minute;
    forced_at_ = now;
    forced_until_ = deadline(now, HOUR_MS);
}

std::optional<int> Brain::minute_of_day(uint32_t now) const {
    if (live(forced_until_, now)) return int((forced_minute_ + (now - forced_at_) / MINUTE_MS) % (24 * 60));
    if (const std::optional<int> m = user_.minute_of_day(now)) return m;
    return clock_.minute_of_day(now);
}

Daypart Brain::daypart(uint32_t now) const {
    if (held_daypart_) return *held_daypart_;
    const std::optional<int> minute = minute_of_day(now);
    return minute ? daypart_at(*minute) : Daypart::Afternoon;
}

}  // namespace nestbox
