// A small reader and writer for the JSON the pet speaks. The reader keeps values as text (numbers are read with
// num() or integer()). Nested objects become dotted keys: {"text": {"say": "hi"}} gives "text.say"; an array gives
// "key.0", "key.1"... and its raw text under "key". Nesting is capped at 8 levels. Header-only: the companion CLI uses
// it without linking the engine. An empty object that is an item of a list is kept as "{}", so the list keeps its
// length.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace nestbox {

struct Fields {
    std::map<std::string, std::string> values;

    bool has(const std::string& k) const { return values.count(k) > 0; }
    std::string str(const std::string& k, const std::string& def = "") const {
        auto i = values.find(k);
        return i == values.end() ? def : i->second;
    }
    // The number under `k`; `def` if absent, not a number or not finite.
    double num(const std::string& k, double def = 0) const {
        double d;
        return number(k, d) && std::isfinite(d) ? d : def;
    }
    // The number under `k` clamped to [lo, hi]; `def` if absent, not a number or not finite.
    double num_in(const std::string& k, double lo, double hi, double def) const {
        double d;
        return number(k, d) && std::isfinite(d) ? std::clamp(d, lo, hi) : def;
    }
    // The number under `k` truncated toward zero and clamped to Int's range; `def` if absent or not a number.
    template <typename Int = int>
    Int integer(const std::string& k, Int def = 0) const {
        double d;
        if (!number(k, d) || std::isnan(d)) return def;
        if (d <= double(std::numeric_limits<Int>::min())) return std::numeric_limits<Int>::min();
        if (d >= double(std::numeric_limits<Int>::max())) return std::numeric_limits<Int>::max();
        return Int(d);
    }
    bool flag(const std::string& k) const { auto s = str(k); return s == "true" || s == "1"; }
    void set(const std::string& k, const std::string& v) { values[k] = v; }
    // The fields under "prefix." (a nested object), with the prefix taken off.
    Fields under(const std::string& prefix) const {
        Fields out;
        std::string p = prefix + ".";
        for (auto i = values.lower_bound(p); i != values.end() && i->first.compare(0, p.size(), p) == 0; ++i)
            out.values[i->first.substr(p.size())] = i->second;
        return out;
    }
    bool has_object(const std::string& prefix) const {
        auto i = values.lower_bound(prefix + ".");
        return i != values.end() && i->first.compare(0, prefix.size() + 1, prefix + ".") == 0;
    }

private:
    bool number(const std::string& k, double& out) const {
        auto i = values.find(k);
        if (i == values.end() || i->second.empty()) return false;
        char* end = nullptr;
        out = std::strtod(i->second.c_str(), &end);
        return end && *end == 0;
    }
};

namespace json_detail {

constexpr int MAX_DEPTH = 8;

inline uint32_t hex_digit(char h) {
    return uint32_t(h >= '0' && h <= '9'   ? h - '0'
                    : h >= 'a' && h <= 'f' ? h - 'a' + 10
                    : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                           : 99);
}

// RFC 8259's number grammar: no leading zeros, no "+", no bare "." or exponent, no NaN or Infinity.
inline bool is_number(const std::string& t) {
    size_t i = 0;
    auto digits = [&] {
        const size_t from = i;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
        return i > from;
    };
    if (i < t.size() && t[i] == '-') i++;
    if (i < t.size() && t[i] == '0') i++;
    else if (!digits()) return false;
    if (i < t.size() && t[i] == '.' && (++i, !digits())) return false;
    if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
        i++;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) i++;
        if (!digits()) return false;
    }
    return i == t.size();
}

inline void append_utf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) {
        o += char(cp);
    } else if (cp < 0x800) {
        o += char(0xC0 | cp >> 6);
        o += char(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        o += char(0xE0 | cp >> 12);
        o += char(0x80 | ((cp >> 6) & 63));
        o += char(0x80 | (cp & 63));
    } else {
        o += char(0xF0 | cp >> 18);
        o += char(0x80 | ((cp >> 12) & 63));
        o += char(0x80 | ((cp >> 6) & 63));
        o += char(0x80 | (cp & 63));
    }
}

struct Reader {
    const std::string& s;
    size_t i = 0;
    Fields& out;

    bool at(char c) const { return i < s.size() && s[i] == c; }
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }

    bool hex4(size_t from, uint32_t& v) const {
        if (from + 4 > s.size()) return false;
        v = 0;
        for (size_t k = from; k < from + 4; k++) {
            const uint32_t d = hex_digit(s[k]);
            if (d > 15) return false;
            v = v * 16 + d;
        }
        return true;
    }
    // A \uXXXX escape with `i` on the "u", including a UTF-16 surrogate pair; a lone surrogate becomes U+FFFD.
    bool unicode_escape(std::string& o) {
        uint32_t cp, lo;
        if (!hex4(i + 1, cp)) return false;
        i += 4;
        bool high = cp >= 0xD800 && cp < 0xDC00;
        if (high && i + 6 < s.size() && s[i + 1] == '\\' && s[i + 2] == 'u' && hex4(i + 3, lo) && lo >= 0xDC00 &&
            lo < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            i += 6;
        } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD;
        }
        append_utf8(o, cp);
        return true;
    }
    bool string(std::string& o) {
        if (!at('"')) return false;
        for (i++; i < s.size() && s[i] != '"'; i++) {
            if (s[i] != '\\') { o += s[i]; continue; }
            if (++i >= s.size()) return false;
            switch (char c = s[i]) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': if (!unicode_escape(o)) return false; break;
                default: o += c;
            }
        }
        if (i >= s.size()) return false;
        i++;
        return true;
    }
    // An object or array at `key`; members are stored under "key.member" / "key.N".
    bool container(const std::string& key, int depth) {
        const bool obj = s[i] == '{';
        const char close = obj ? '}' : ']';
        size_t start = i++;
        ws();
        if (at(close)) {
            i++;
        } else {
            for (int n = 0;;) {
                std::string sub;
                if (obj) {
                    ws();
                    if (!string(sub)) return false;
                    ws();
                    if (!at(':')) return false;
                    i++;
                } else {
                    sub = std::to_string(n++);
                }
                const std::string member = key.empty() ? sub : key + "." + sub;
                ws();
                const size_t item = i;
                if (!value(member, depth + 1)) return false;
                if (!obj && s.compare(item, 1, "{") == 0 && !out.values.count(member) && !out.has_object(member))
                    out.values[member] = "{}";   // an empty object in a list still takes its place
                ws();
                if (at(',')) { i++; continue; }
                if (at(close)) { i++; break; }
                return false;
            }
        }
        if (!obj && !key.empty()) out.values[key] = s.substr(start, i - start);
        return true;
    }
    // A string, a number, true, false or null, kept as text.
    bool scalar(const std::string& key) {
        std::string val;
        if (at('"')) {
            if (!string(val)) return false;
        } else {
            size_t start = i;
            while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' &&
                   s[i] != '\r' && s[i] != '\t')
                i++;
            val = s.substr(start, i - start);
            if (val != "true" && val != "false" && val != "null" && !is_number(val)) return false;
        }
        out.values[key] = val;
        return true;
    }
    // One value at `key` ("" at the top, which must be an object).
    bool value(const std::string& key, int depth) {
        ws();
        if (i >= s.size() || depth > MAX_DEPTH) return false;
        if (s[i] == '{' || s[i] == '[') return container(key, depth);
        return !key.empty() && scalar(key);
    }
};

}  // namespace json_detail

// Returns false if the text is not a JSON object.
inline bool parse_json(const std::string& s, Fields& out) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || s[first] != '{') return false;
    json_detail::Reader r{s, 0, out};
    if (!r.value("", 0)) return false;
    r.ws();
    return r.i == s.size();
}

// Text for a JSON string value: quotes, backslashes and newlines escaped, other control characters as spaces.
inline std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (uint8_t(c) < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

// Builds compact JSON with ", " and ": " separators. Keys are written as given (they are code literals).
class JsonWriter {
public:
    enum class Items { Inline, OnePerLine };

    JsonWriter& begin_object() { open('{', Items::Inline); return *this; }
    JsonWriter& end_object() { return close('}'); }
    // OnePerLine puts each item on its own line, indented two spaces, and the closing bracket on a line of its own.
    JsonWriter& begin_array(Items items = Items::Inline) { open('[', items); return *this; }
    JsonWriter& end_array() { return close(']'); }

    JsonWriter& key(const std::string& k) {
        separate();
        out_ += '"' + k + "\": ";
        after_key_ = true;
        return *this;
    }
    JsonWriter& str(const std::string& v) { separate(); out_ += '"' + json_escape(v) + '"'; return *this; }
    JsonWriter& num(long long v) { separate(); out_ += std::to_string(v); return *this; }
    // `decimals` places after the point; null if `v` is not finite (JSON has no NaN or Infinity).
    JsonWriter& num(double v, int decimals) {
        if (!std::isfinite(v)) return raw("null");
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
        return raw(buf);
    }
    // The shortest form printf's %g gives (0.6, 1, 0.125); null if `v` is not finite.
    JsonWriter& num_short(double v) {
        if (!std::isfinite(v)) return raw("null");
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", v);
        return raw(buf);
    }
    JsonWriter& boolean(bool v) { separate(); out_ += v ? "true" : "false"; return *this; }
    // A value that is already JSON.
    JsonWriter& raw(const std::string& json) { separate(); out_ += json; return *this; }

    JsonWriter& field(const std::string& k, const std::string& v) { return key(k).str(v); }
    JsonWriter& field(const std::string& k, const char* v) { return key(k).str(v); }
    JsonWriter& field(const std::string& k, bool v) { return key(k).boolean(v); }
    JsonWriter& field(const std::string& k, int v) { return key(k).num(static_cast<long long>(v)); }
    JsonWriter& field(const std::string& k, long long v) { return key(k).num(v); }

    const std::string& text() const { return out_; }

private:
    struct Container {
        bool empty = true;
        Items items = Items::Inline;
    };

    void separate() {
        if (after_key_) { after_key_ = false; return; }
        if (open_.empty()) return;
        Container& c = open_.back();
        if (c.items == Items::OnePerLine) out_ += c.empty ? "\n  " : ",\n  ";
        else if (!c.empty) out_ += ", ";
        c.empty = false;
    }
    void open(char bracket, Items items) {
        separate();
        out_ += bracket;
        open_.push_back({true, items});
    }
    JsonWriter& close(char bracket) {
        if (open_.back().items == Items::OnePerLine) out_ += '\n';
        out_ += bracket;
        open_.pop_back();
        return *this;
    }

    std::string out_;
    std::vector<Container> open_;
    bool after_key_ = false;
};

}  // namespace nestbox
