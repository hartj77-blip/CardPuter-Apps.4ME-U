#include "apps/mesh/core/text.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace zs::mesh {
namespace {

struct Emoji {
    uint32_t cp;
    const char* word;    // in running text
    const char* brief;   // as a reaction
};
// The ones people actually send on a mesh. Everything else becomes "?".
const Emoji kEmoji[] = {
    {0x1F44D, ":thumbsup:", "+1"},    {0x1F44E, ":thumbsdown:", "-1"}, {0x2764, "<3", "<3"},
    {0x1F499, "<3", "<3"},            {0x1F49A, "<3", "<3"},           {0x1F49C, "<3", "<3"},
    {0x1F602, ":'D", ":'D"},          {0x1F923, ":'D", ":'D"},         {0x1F600, ":D", ":D"},
    {0x1F603, ":D", ":D"},            {0x1F604, ":D", ":D"},           {0x1F601, ":D", ":D"},
    {0x1F606, "XD", "XD"},            {0x1F642, ":)", ":)"},           {0x1F60A, ":)", ":)"},
    {0x263A, ":)", ":)"},             {0x1F609, ";)", ";)"},           {0x1F61B, ":P", ":P"},
    {0x1F61C, ";P", ";P"},            {0x1F60E, "B)", "B)"},           {0x1F610, ":|", ":|"},
    {0x1F615, ":/", ":/"},            {0x1F641, ":(", ":("},           {0x2639, ":(", ":("},
    {0x1F61E, ":(", ":("},            {0x1F622, ":'(", ":'("},         {0x1F62D, ":'(", ":'("},
    {0x1F62E, ":O", ":O"},            {0x1F632, ":O", ":O"},           {0x1F631, ":O", ":O"},
    {0x1F620, ">:(", ">:("},          {0x1F621, ">:(", ">:("},         {0x1F914, ":thinking:", "hmm"},
    {0x1F64F, ":thanks:", "thx"},     {0x1F44B, ":wave:", "hi"},       {0x1F44C, ":ok:", "ok"},
    {0x1F44F, ":clap:", "clap"},      {0x1F4AA, ":strong:", "strong"}, {0x270C, ":v:", "v"},
    {0x1F91D, ":handshake:", "deal"}, {0x1F389, ":party:", "yay"},     {0x1F525, ":fire:", "fire"},
    {0x2705, ":yes:", "yes"},         {0x2714, ":yes:", "yes"},        {0x274C, ":no:", "no"},
    {0x2753, "?", "?"},               {0x2754, "?", "?"},              {0x2757, "!", "!"},
    {0x203C, "!!", "!!"},             {0x26A0, ":warning:", "warn"},   {0x1F6A8, ":alert:", "alert"},
    {0x1F514, ":bell:", "bell"},      {0x1F4CD, ":pin:", "pin"},       {0x1F4E1, ":antenna:", "ant"},
    {0x1F4F6, ":signal:", "sig"},     {0x1F50B, ":battery:", "batt"},  {0x26A1, ":zap:", "zap"},
    {0x2600, ":sun:", "sun"},         {0x1F327, ":rain:", "rain"},     {0x2744, ":snow:", "snow"},
    {0x1F4A9, ":poop:", "poop"},      {0x1F480, ":skull:", "skull"},   {0x1F440, ":eyes:", "eyes"},
    {0x1F37A, ":beer:", "beer"},      {0x2615, ":coffee:", "coffee"},  {0x1F680, ":rocket:", "go"},
    {0x1F4AF, ":100:", "100"},        {0x1F605, ":')", ":')"},         {0x1F970, ":)", "<3"},
    {0x1F60D, ":heart_eyes:", "<3"},  {0x1F618, ":*", ":*"},           {0x1F44A, ":fist:", "fist"},
};

const Emoji* find_emoji(uint32_t cp) {
    for (const Emoji& e : kEmoji)
        if (e.cp == cp) return &e;
    return nullptr;
}

// One code point from s at i (advancing i); 0xFFFD for a broken sequence.
uint32_t next_cp(std::string_view s, size_t& i) {
    uint8_t c = uint8_t(s[i++]);
    if (c < 0x80) return c;
    int more = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (more < 0) return 0xFFFD;
    uint32_t cp = c & (0xFF >> (more + 2));
    for (int k = 0; k < more; ++k) {
        if (i >= s.size() || (uint8_t(s[i]) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (uint8_t(s[i++]) & 0x3F);
    }
    return cp;
}

void put_cp(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

// In the OS's fonts (shell/tools/fonts.sh): ASCII, Latin-1 and a few marks.
bool drawable(uint32_t cp) {
    if (cp >= 0x20 && cp <= 0x7E) return true;
    if (cp >= 0xA0 && cp <= 0xFF) return true;
    switch (cp) {
    case 0x2013: case 0x2014: case 0x2018: case 0x2019: case 0x201C: case 0x201D: case 0x2022: case 0x2026: return true;
    default: return false;
    }
}
// Joiners, variation selectors, skin tones, the keycap mark: part of an
// emoji, nothing on their own.
bool invisible(uint32_t cp) {
    return cp == 0x200D || cp == 0x200B || cp == 0xFEFF || (cp >= 0xFE00 && cp <= 0xFE0F) ||
           (cp >= 0x1F3FB && cp <= 0x1F3FF) || cp == 0x20E3;
}

std::string convert(std::string_view s, bool brief) {
    std::string out;
    bool last_unknown = false;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = next_cp(s, i);
        if (cp == '\n' || cp == '\t') {
            out.push_back(cp == '\n' ? '\n' : ' ');
            last_unknown = false;
        } else if (cp < 0x20 || cp == 0x7F || invisible(cp)) {
            continue;   // the bell (7) and its friends
        } else if (drawable(cp)) {
            put_cp(out, cp);
            last_unknown = false;
        } else if (const Emoji* e = find_emoji(cp)) {
            out += brief ? e->brief : e->word;
            last_unknown = false;
        } else {
            if (!last_unknown) out.push_back('?');   // a run of them is one "?"
            last_unknown = true;
        }
    }
    return out;
}

}  // namespace

std::string display_text(std::string_view utf8) { return convert(utf8, false); }
std::string reaction_text(std::string_view utf8) {
    std::string r = convert(utf8, true);
    return r.empty() ? "?" : utf8_cut(r, 8);
}

std::string utf8_cut(std::string_view s, size_t max_bytes) {
    if (s.size() <= max_bytes) return std::string(s);
    size_t n = max_bytes;
    while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80) --n;
    return std::string(s.substr(0, n));
}

std::string one_line(std::string_view s, size_t max_bytes) {
    std::string out;
    for (char ch : s) out.push_back(ch == '\n' || ch == '\r' || ch == '\t' ? ' ' : ch);
    if (out.size() <= max_bytes) return out;
    return utf8_cut(out, max_bytes > 3 ? max_bytes - 3 : 0) + "...";
}

std::string age_text(long now, long then) {
    if (then <= 0 || then > now + 60) return "";
    long s = now - then;
    char b[16];
    if (s < 60) return "now";
    if (s < 3600) std::snprintf(b, sizeof b, "%ldm", s / 60);
    else if (s < 86400) std::snprintf(b, sizeof b, "%ldh", s / 3600);
    else if (s < 14 * 86400) std::snprintf(b, sizeof b, "%ldd", s / 86400);
    else std::snprintf(b, sizeof b, "%ldw", s / (7 * 86400));
    return b;
}

std::string clock_text(long now, long then) {
    if (then <= 0) return "";
    time_t t = time_t(then), n = time_t(now);
    struct tm a {}, b {};
    localtime_r(&t, &a);
    localtime_r(&n, &b);
    char buf[24];
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) std::strftime(buf, sizeof buf, "%H:%M", &a);
    else if (now - then < 6 * 86400 && now >= then) std::strftime(buf, sizeof buf, "%a %H:%M", &a);
    else std::strftime(buf, sizeof buf, "%e %b", &a);
    std::string s = buf;
    if (!s.empty() && s[0] == ' ') s.erase(0, 1);
    return s;
}

std::string distance_text(double m, bool miles) {
    char b[24];
    if (miles) {
        double mi = m / 1609.344;
        if (mi < 0.1) std::snprintf(b, sizeof b, "%.0f ft", m * 3.28084);
        else if (mi < 10) std::snprintf(b, sizeof b, "%.1f mi", mi);
        else std::snprintf(b, sizeof b, "%.0f mi", mi);
    } else if (m < 1000) {
        std::snprintf(b, sizeof b, "%.0f m", m);
    } else if (m < 10000) {
        std::snprintf(b, sizeof b, "%.1f km", m / 1000);
    } else {
        std::snprintf(b, sizeof b, "%.0f km", m / 1000);
    }
    return b;
}

std::string duration_text(long s) {
    char b[24];
    if (s < 60) std::snprintf(b, sizeof b, "%lds", s < 0 ? 0 : s);
    else if (s < 3600) std::snprintf(b, sizeof b, "%ldm", s / 60);
    else if (s < 86400) std::snprintf(b, sizeof b, "%ldh %ldm", s / 3600, (s % 3600) / 60);
    else std::snprintf(b, sizeof b, "%ldd %ldh", s / 86400, (s % 86400) / 3600);
    return b;
}

static double rad(double d) { return d * 3.14159265358979323846 / 180.0; }

double distance_m(double lat1, double lon1, double lat2, double lon2) {
    double dlat = rad(lat2 - lat1), dlon = rad(lon2 - lon1);
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(rad(lat1)) * std::cos(rad(lat2)) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 6371000.0 * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

double bearing_deg(double lat1, double lon1, double lat2, double lon2) {
    double dlon = rad(lon2 - lon1);
    double y = std::sin(dlon) * std::cos(rad(lat2));
    double x = std::cos(rad(lat1)) * std::sin(rad(lat2)) - std::sin(rad(lat1)) * std::cos(rad(lat2)) * std::cos(dlon);
    double b = std::atan2(y, x) * 180.0 / 3.14159265358979323846;
    return b < 0 ? b + 360.0 : b;
}

const char* compass_point(double deg) {
    static const char* const kPoints[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    int i = int(std::floor((deg + 22.5) / 45.0)) % 8;
    return kPoints[i < 0 ? i + 8 : i];
}

// LoRa decodes below the noise: around -7 dB is still a fair link at the
// long-range presets, and it falls apart near -15 to -20.
const char* snr_quality(float snr) {
    if (snr >= -7) return "Good";
    if (snr >= -15) return "Fair";
    return "Weak";
}
int snr_bars(float snr) {
    if (snr >= 0) return 4;
    if (snr >= -7) return 3;
    if (snr >= -13) return 2;
    if (snr >= -18) return 1;
    return 0;
}

}  // namespace zs::mesh
