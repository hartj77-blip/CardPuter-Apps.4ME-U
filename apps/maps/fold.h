// How Go to compares names (search.cpp), shared with the map builder
// (tools/zmap_build.cpp) so both write and look up the same keys:
// ASCII letters lower-cased, other ASCII that isn't a letter or digit a
// space ("St." matches "st"), everything else (accented letters) kept.
#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace zmap {

inline char fold_char(char c) {
    if (c >= 'A' && c <= 'Z') return char(c - 'A' + 'a');
    if (c == '\t' || c == '\n') return c;
    if (uint8_t(c) < 0x80 && !std::isalnum(uint8_t(c))) return ' ';
    return c;
}

// "Main  St." -> "main st": folded, words joined by one space.
inline std::string fold_key(std::string_view s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        char f = fold_char(c);
        if (f == ' ' || f == '\t' || f == '\n') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += f;
    }
    return out;
}

// A street's key for house numbers: fold_key() with the usual short forms
// written out after the first word ("Market St." -> "market street"; the
// first word is left, as "St." there is usually "Saint").
inline std::string street_key(std::string_view name) {
    static const char* const kLong[][2] = {{"st", "street"}, {"ave", "avenue"}, {"av", "avenue"}, {"rd", "road"},
                                           {"dr", "drive"}, {"blvd", "boulevard"}, {"ln", "lane"}, {"ct", "court"},
                                           {"pl", "place"}, {"hwy", "highway"}, {"pkwy", "parkway"}, {"ter", "terrace"},
                                           {"cir", "circle"}, {"sq", "square"}, {"tpke", "turnpike"}};
    std::string k = fold_key(name), out;
    size_t at = 0;
    bool first = true;
    while (at <= k.size()) {
        size_t e = k.find(' ', at);
        if (e == std::string::npos) e = k.size();
        std::string w = k.substr(at, e - at);
        if (!first)
            for (auto& p : kLong)
                if (w == p[0]) {
                    w = p[1];
                    break;
                }
        out += (first ? "" : " ") + w;
        first = false;
        at = e + 1;
    }
    return out;
}

}  // namespace zmap
