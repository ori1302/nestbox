#include "mind/user.h"
#include <algorithm>
#include "util/deadline.h"
#include "util/text.h"

namespace nestbox {

namespace {
// Typing fast from 6.8 keys a second (about 80 words a minute, at 5 keys a word) held for FAST_HOLD_MS, back below 5.3
// (about 65) for CALM_MS. Single words reach 10 keys a second, which RATE_MIN_MS and the hold even out.
constexpr float FAST_KPS = 6.8f, CALM_KPS = 5.3f;
constexpr uint32_t FAST_HOLD_MS = 2000, CALM_MS = 2000;
constexpr double MAX_KEYS = 1000;
constexpr double MAX_IDLE_S = 1e7;

// "14:32" or "14:32:05" as minutes after midnight; -1 if it is neither.
int parse_time(const std::string& t) {
    int h = 0, m = 0, s = 0;
    char extra = 0;
    const int n = std::sscanf(t.c_str(), "%d:%d:%d%c", &h, &m, &s, &extra);
    if (n < 2 || n > 3 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return -1;
    return h * 60 + m;
}
}  // namespace

std::vector<std::string> User::report(const Fields& f, uint32_t now) {
    std::vector<std::string> warnings;
    std::string name = utf8_cut(f.str("from", "computer"), MAX_NAME);
    if (name.empty()) name = "computer";
    if (ascii_lower(name) == DEVICE_SENDER) {
        warnings.push_back("\"device\" is the device's own name; this report counts as \"computer\"");
        name = "computer";
    }
    if (!sources_.count(name) && sources_.size() >= MAX_SOURCES) {
        auto oldest = sources_.begin();
        for (auto i = sources_.begin(); i != sources_.end(); ++i)
            if (is_before(i->second.last, oldest->second.last)) oldest = i;
        sources_.erase(oldest);
        warnings.push_back("too many sources; the one heard from longest ago made room");
    }
    Source& s = sources_[name];
    const uint32_t before = s.last;
    s.last = stamp(now);
    if (f.has("next")) s.next_ms = uint32_t(f.num_in("next", 1, NEXT_MAX_S, NEXT_DEFAULT_S) * 1000);
    if (f.has("keys")) {
        double keys = 0;
        if (f.num("keys", -1) < 0) {
            warnings.push_back("\"keys\" is a count of key presses since the last report");
        } else {
            keys = std::min(f.num("keys", 0), MAX_KEYS);
            s.keys_known = true;
            if (keys > 0 && (!s.last_keys || now - s.last_keys >= TYPING_AFTER_MS)) s.run_from = stamp(now);
            if (keys > 0) s.recent.push_back({before, now, uint32_t(keys)});
            while (!s.recent.empty() && now - s.recent.front().when >= RATE_WINDOW_MS) s.recent.pop_front();
            if (keys > 0) s.last_keys = s.typed_at = stamp(now);
            else s.last_keys = 0;
        }
    } else {
        s.keys_known = false;
        s.recent.clear();
        s.last_keys = 0;
    }
    s.idle_known = f.has("idle");
    s.idle_s = float(f.num_in("idle", 0, MAX_IDLE_S, 0));
    if (f.has("state")) {
        const std::string st = ascii_lower(f.str("state"));
        if (st == "awake" || st == "locked" || st == "asleep") s.state = st;
        else warnings.push_back("unknown state \"" + f.str("state") + "\"; use awake, locked or asleep");
    }
    if (f.has("time")) {
        const int m = parse_time(f.str("time"));
        if (m < 0) {
            warnings.push_back("\"time\" is the local time as \"14:32\"");
        } else {
            s.minute = m;
            s.minute_at = s.last;
        }
    }
    for (const auto& [k, v] : f.values) {
        if (k.find('.') != std::string::npos) continue;
        if (k != "from" && k != "keys" && k != "idle" && k != "state" && k != "time" && k != "next")
            warnings.push_back("unknown key \"" + k + "\"; this route takes from, keys, idle, state, time and next");
    }
    return warnings;
}

void User::computer(bool asleep, uint32_t now) {
    if (!asleep) {
        sources_.erase(DEVICE_SENDER);
        return;
    }
    Source& s = sources_[DEVICE_SENDER];
    s.last = stamp(now);
    s.next_ms = NEXT_MAX_S * 1000;
    s.state = "asleep";
}

bool User::gone(const Source& s, uint32_t now) const { return now - s.last > 2 * s.next_ms; }

bool User::source_away(const Source& s, uint32_t now) const {
    if (gone(s, now) || s.state != "awake") return true;
    return s.idle_known && s.idle_s + (now - s.last) / 1000.0f >= AWAY_S;
}

bool User::heard(uint32_t now) const {
    return std::any_of(sources_.begin(), sources_.end(), [&](auto& e) { return !gone(e.second, now); });
}

bool User::all_gone(uint32_t now) const { return !sources_.empty() && !heard(now); }

bool User::typing(uint32_t now) const {
    for (const auto& [name, s] : sources_)
        if (!gone(s, now) && s.state == "awake" && s.last_keys && now - s.last_keys < TYPING_AFTER_MS) return true;
    return false;
}

bool User::typing_fast(bool typing_now, uint32_t now) {
    if (!typing_now) {
        fast_ = false;
        calm_since_ = fast_since_ = 0;
        return false;
    }
    const float rate = kps(now);
    if (!fast_) {
        calm_since_ = 0;
        if (rate < FAST_KPS) fast_since_ = 0;
        else if (!fast_since_) fast_since_ = stamp(now);
        else if (now - fast_since_ >= FAST_HOLD_MS) fast_ = true;
    } else if (rate >= CALM_KPS) {
        calm_since_ = 0;
    } else if (!calm_since_) {
        calm_since_ = stamp(now);
    } else if (now - calm_since_ >= CALM_MS) {
        fast_ = false;
        calm_since_ = fast_since_ = 0;
    }
    return fast_;
}

bool User::pausing(uint32_t now) const {
    if (typing(now)) return false;
    for (const auto& [name, s] : sources_) {
        if (gone(s, now) || s.state != "awake" || !s.typed_at || now - s.typed_at >= PAUSE_MS) continue;
        if (!s.idle_known || (s.idle_s + (now - s.last) / 1000.0f) * 1000 < PAUSE_MS) return true;
    }
    return false;
}

bool User::there(uint32_t now) const {
    if (sources_.empty()) return true;
    for (const auto& [name, s] : sources_)
        if (!source_away(s, now)) return true;
    return false;
}

bool User::locked(uint32_t now) const {
    bool any = false;
    for (const auto& [name, s] : sources_) {
        if (gone(s, now)) continue;
        if (s.state == "awake") return false;
        any = true;
    }
    return any;
}

bool User::asleep(uint32_t now) const {
    if (sources_.empty()) return false;
    for (const auto& [name, s] : sources_) {
        if (gone(s, now) || s.state != "awake") continue;
        if (!s.idle_known || s.idle_s + (now - s.last) / 1000.0f < LONG_AWAY_S) return false;
    }
    return true;
}

float User::kps(uint32_t now) const {
    float best = 0;
    for (const auto& [name, s] : sources_) {
        if (gone(s, now) || !s.last_keys || now - s.last_keys >= TYPING_AFTER_MS) continue;
        uint32_t keys = 0, start = 0, end = 0;
        for (const Keys& k : s.recent) {
            // The first report of a stretch holds keys pressed before it began, over a time nobody measured.
            if (now - k.when >= RATE_WINDOW_MS || !is_after(k.when, s.run_from)) continue;
            if (!keys) start = is_before(k.from, s.run_from) || !k.from ? s.run_from : k.from;   // the first counted
            keys += k.keys;
            end = k.when;
        }
        if (keys) best = std::max(best, keys * 1000.0f / std::max(end - start, RATE_MIN_MS));
    }
    return best;
}

std::string User::word(uint32_t now) const {
    if (sources_.empty()) return "unknown";
    if (typing(now)) return fast_ ? "typing-fast" : "typing";
    if (pausing(now)) return "paused";
    if (asleep(now)) return "asleep";
    return there(now) ? "here" : "away";
}

std::optional<int> User::minute_of_day(uint32_t now) const {
    const Source* newest = nullptr;
    for (const auto& [name, s] : sources_)
        if (s.minute >= 0 && (!newest || is_after(s.minute_at, newest->minute_at))) newest = &s;
    if (!newest) return std::nullopt;
    const uint32_t minutes = (now - newest->minute_at) / 60000;
    return int((newest->minute + minutes) % (24 * 60));
}

}  // namespace nestbox
