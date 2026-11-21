#include "mind/note.h"
#include <algorithm>
#include <cstdlib>
#include "screen/canvas.h"
#include "util/text.h"

namespace nestbox {

namespace {
struct StateName {
    State state;
    const char* key;
    const char* means;
};
constexpr StateName STATES[] = {
    {State::Working, "working", "something is in progress"},
    {State::Asking, "asking", "it needs the user"},
    {State::Failing, "failing", "something is wrong and still going on"},
    {State::On, "on", "something is simply going on"},
    {State::Quiet, "quiet", "the user should not be disturbed"},
    {State::Done, "done", "it ended well"},
    {State::Failed, "failed", "it ended badly"},
    {State::Over, "over", "it ended, and that is all"},
};

const char* const KEYS[] = {"from",    "id",   "state", "do",    "at",     "aim",   "face",  "times", "speed",
                            "say",     "caption", "line", "gauge", "label", "sign",  "effect", "color", "for",
                            "until"};

// Letters, digits, "-", "_" and "." (and "/" in an id); anything else becomes "-".
std::string clean_name(const std::string& raw, bool slashes, size_t max, bool& changed) {
    std::string out;
    for (char c : raw) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                        c == '_' || c == '.' || (slashes && c == '/');
        if (slashes && c == '/' && (out.empty() || out.back() == '/')) {
            changed = true;
            continue;
        }
        out += ok ? c : '-';
        changed = changed || !ok;
    }
    while (!out.empty() && out.back() == '/') out.pop_back();
    if (out.size() > max) {
        out.resize(max);
        changed = true;
    }
    return out;
}

// A text part: given, switched off with false, or left out.
void read_text(const Fields& f, const char* key, size_t max, Part<std::string>& out, std::vector<std::string>& w) {
    if (!f.has(key)) return;
    const std::string v = f.str(key);
    if (v == "false") {
        out.kind = Part<std::string>::Kind::Off;
        return;
    }
    out.kind = Part<std::string>::Kind::Set;
    out.value = utf8_cut(v, max);
    if (out.value.size() < v.size())
        w.push_back("\"" + std::string(key) + "\" was cut to " + std::to_string(max) + " characters");
}

bool is_list(const Fields& f, const std::string& k) { return f.has(k + ".0") || f.has_object(k + ".0"); }
}  // namespace

const char* state_key(State s) {
    for (const StateName& n : STATES)
        if (n.state == s) return n.key;
    return "";
}

const char* state_means(State s) {
    for (const StateName& n : STATES)
        if (n.state == s) return n.means;
    return "a passing moment";
}

const std::vector<State>& states() {
    static const std::vector<State> all = [] {
        std::vector<State> v;
        for (const StateName& n : STATES) v.push_back(n.state);
        return v;
    }();
    return all;
}

bool is_lasting(State s) {
    return s == State::Working || s == State::Asking || s == State::Failing || s == State::On || s == State::Quiet;
}
bool is_result(State s) { return s == State::Done || s == State::Failed; }

bool Note::empty() const {
    return state == State::None && body.kind == Part<std::vector<Step>>::Kind::Default && !say.set() &&
           !caption.set() && !line.set() && !gauge.set() && !sign.set() && !effect.set();
}

const std::vector<std::pair<const char*, uint16_t>>& color_names() {
    static const std::vector<std::pair<const char*, uint16_t>> all = {
        {"red", RED},   {"orange", rgb(0xd9, 0x77, 0x57)}, {"yellow", YELLOW},
        {"green", GREEN}, {"blue", BLUE},   {"purple", PURPLE},
        {"pink", rgb(0xe8, 0x62, 0x8a)},   {"white", rgb(0xff, 0xff, 0xff)},
    };
    return all;
}

uint16_t color_value(const std::string& name) {
    for (const auto& [n, v] : color_names())
        if (name == n) return v;
    return CREAM;                          // the screen's ink
}

bool parse_tell(const Fields& f, Note& out, std::vector<std::string>& w) {
    out = Note{};
    bool any = false;
    for (const auto& [k, v] : f.values) {
        const std::string top = k.substr(0, k.find('.'));
        if (std::none_of(std::begin(KEYS), std::end(KEYS), [&](const char* key) { return top == key; })) {
            if (k.find('.') == std::string::npos || !f.has(top))
                w.push_back("unknown key \"" + top + "\"; the keys are listed at /v1/words");
        } else {
            any = true;
        }
    }
    bool changed = false;
    out.from = clean_name(f.str("from"), false, Note::MAX_FROM, changed);
    if (out.from.empty()) out.from = "anon";
    if (changed) w.push_back("\"from\" takes letters, digits, -, _ and . up to " + std::to_string(Note::MAX_FROM) +
                             " characters; it was read as \"" + out.from + "\"");
    changed = false;
    out.id = clean_name(f.str("id"), true, Note::MAX_ID, changed);
    if (changed) w.push_back("\"id\" takes letters, digits, -, _, . and / up to " + std::to_string(Note::MAX_ID) +
                             " characters; it was read as \"" + out.id + "\"");

    if (f.has("state")) {
        const std::string word = ascii_lower(f.str("state"));
        bool known = false;
        for (const StateName& n : STATES)
            if (word == n.key) {
                out.state = n.state;
                known = true;
            }
        if (!known) {
            std::vector<std::string> keys;
            for (const StateName& n : STATES) keys.push_back(n.key);
            w.push_back("unknown state \"" + f.str("state") + "\"; the states are " + joined(keys));
        }
    }

    // The body: "do" as a word, an object or a list of steps; "at", "aim", "face", "times", "speed" and "for" make
    // the single step's own.
    if (f.str("do") == "false") {
        out.body.kind = Part<std::vector<Step>>::Kind::Off;
    } else if (is_list(f, "do")) {
        out.body_list = true;
        std::vector<Step> steps;
        size_t i = 0;
        for (;; i++) {
            const std::string k = "do." + std::to_string(i);
            if (!f.has(k) && !f.has_object(k)) break;
            if (i >= StepChain::MAX_STEPS) continue;
            Fields step = f.has_object(k) ? f.under(k) : Fields{};
            if (!f.has_object(k)) step.set("do", f.str(k));
            Step s;
            if (StepChain::parse(step, s, w)) steps.push_back(s);
            else w.push_back("step " + std::to_string(i + 1) + " asks for nothing; it was left out");
        }
        if (i > StepChain::MAX_STEPS)
            w.push_back("a list has at most " + std::to_string(StepChain::MAX_STEPS) + " steps on this device; " +
                        std::to_string(i - StepChain::MAX_STEPS) + " were left out");
        for (const char* k : {"at", "aim", "face", "times", "speed"})
            if (f.has(k) || f.has_object(k))
                w.push_back("\"" + std::string(k) + "\" belongs in each step of a list; the one outside was left out");
        if (!steps.empty()) {
            out.body.kind = Part<std::vector<Step>>::Kind::Set;
            out.body.value = std::move(steps);
        }
    } else {
        Fields step;
        for (const char* k : {"do", "at", "aim", "face", "times", "speed", "for"})
            for (const auto& [key, v] : f.values)
                if (key == k || key.rfind(std::string(k) + ".", 0) == 0) step.set(key, v);
        if (f.has_object("do")) step = f.under("do");
        Step s;
        if (StepChain::parse(step, s, w)) {
            out.body.kind = Part<std::vector<Step>>::Kind::Set;
            out.body.value = {s};
        } else if (f.has("times")) {
            w.push_back("\"times\" goes with a \"do\"; it was left out");
        }
    }

    read_text(f, "say", Note::MAX_SAY, out.say, w);
    read_text(f, "caption", Note::MAX_CAPTION, out.caption, w);
    read_text(f, "line", Note::MAX_LINE, out.line, w);
    if (f.has("label")) {
        out.label = utf8_cut(f.str("label"), Note::MAX_LABEL);
        if (out.label.size() < f.str("label").size())
            w.push_back("\"label\" was cut to " + std::to_string(Note::MAX_LABEL) + " characters");
    }
    if (f.has("gauge")) {
        const std::string g = ascii_lower(f.str("gauge"));
        if (g == "false") {
            out.gauge.kind = Part<Gauge>::Kind::Off;
        } else if (g == "timer") {
            out.gauge = {Part<Gauge>::Kind::Set, {true, 0}};
        } else if (f.num("gauge", -1e9) > -1e9) {
            const double v = f.num("gauge", 0);
            out.gauge = {Part<Gauge>::Kind::Set, {false, std::clamp(v, 0.0, 100.0)}};
            if (v < 0 || v > 100) w.push_back("\"gauge\" is 0 to 100; it was brought into range");
        } else {
            w.push_back("\"gauge\" is a number from 0 to 100, or \"timer\"");
        }
    }
    if (f.has("sign")) {
        Bubble b;
        if (f.str("sign") == "false") out.sign.kind = Part<Bubble>::Kind::Off;
        else if (sign_named(ascii_lower(f.str("sign")), b)) out.sign = {Part<Bubble>::Kind::Set, b};
        else w.push_back("unknown sign \"" + f.str("sign") + "\"; the signs are listed at /v1/words");
    }
    if (f.has("effect")) {
        Effect e;
        if (f.str("effect") == "false") out.effect.kind = Part<Effect>::Kind::Off;
        else if (effect_named(ascii_lower(f.str("effect")), e)) out.effect = {Part<Effect>::Kind::Set, e};
        else w.push_back("unknown effect \"" + f.str("effect") + "\"; the effects are notes, sparkles and confetti");
    }
    if (f.has("color")) {
        const std::string c = ascii_lower(f.str("color"));
        if (std::any_of(color_names().begin(), color_names().end(), [&](auto& n) { return c == n.first; }))
            out.color = c;
        else w.push_back("unknown color \"" + f.str("color") + "\"; the colors are listed at /v1/words");
    }
    if (f.has("for")) {
        if (const std::optional<uint32_t> ms = duration_ms(f.str("for"))) out.for_ms = *ms;
        else w.push_back(FOR_WARNING);
    }
    if (f.has("until")) {
        const std::string u = ascii_lower(f.str("until"));
        if (u == "seen") out.until = Until::Seen;
        else if (u == "back") out.until = Until::Back;
        else w.push_back("\"until\" is \"seen\" or \"back\"");
    }
    return any;
}

std::optional<uint32_t> duration_ms(const std::string& text) {
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    const double n = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !(n > 0)) return std::nullopt;
    double unit = 1000;
    const std::string rest = ascii_lower(end);
    if (rest == "s" || rest.empty()) unit = 1000;
    else if (rest == "m") unit = 60000;
    else if (rest == "h") unit = 3600000;
    else return std::nullopt;
    return uint32_t(std::min<double>(n * unit, Step::MAX_MS));
}

}  // namespace nestbox
