// Small string helpers. Text from the network can be in any language, so cuts happen at UTF-8 code point
// boundaries and broken byte sequences are dropped (keeps replies valid JSON).
#pragma once
#include <cctype>
#include <string>
#include <vector>

namespace nestbox {

// At most `max_bytes` bytes of `s`, valid UTF-8, cut at a character boundary.
inline std::string utf8_cut(const std::string& s, size_t max_bytes) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        bool ok = n > 0 && i + n <= s.size();
        for (size_t k = 1; ok && k < n; k++) ok = (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
        if (!ok) { i++; continue; }
        if (out.size() + n > max_bytes) break;
        out.append(s, i, n);
        i += n;
    }
    return out;
}

// ASCII letters lower-cased; other bytes (UTF-8 included) unchanged.
inline std::string ascii_lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// `s` with its first letter upper-cased, if that is an ASCII letter.
inline std::string capitalized(std::string s) {
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

inline bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Replaces every occurrence of `from`; inserted text is not searched again.
inline void replace_all(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
}

// The items with ", " between them.
inline std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) out += (out.empty() ? "" : ", ") + item;
    return out;
}

// `s` without leading and trailing spaces.
inline std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(' ') - first + 1);
}

}  // namespace nestbox
