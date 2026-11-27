// nav_parse.h
#include "apps/maps/nav_parse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zmap::nav {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarth = 6371000.0;   // Navit's radius for mg

bool starts_with(const std::string& s, size_t at, const char* p) { return s.compare(at, std::strlen(p), p) == 0; }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

}  // namespace

LatLon from_mg(int32_t x, int32_t y) {
    LatLon l;
    l.lon = double(x) / kEarth * 180.0 / kPi;
    l.lat = (std::atan(std::exp(double(y) / kEarth)) - kPi / 4) * 360.0 / kPi;
    return l;
}

double metres(LatLon a, LatLon b) {
    double k = kPi / 180, la = (a.lat + b.lat) / 2 * k;
    double dx = (b.lon - a.lon) * k * std::cos(la) * 6371008.8, dy = (b.lat - a.lat) * k * 6371008.8;
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<LatLon> parse_route_dump(const std::string& text) {
    std::vector<LatLon> out;
    bool in_route = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        if (starts_with(text, at, "type=")) {
            in_route = starts_with(text, at, "type=street_route ");
        } else if (starts_with(text, at, "mg:") && in_route) {
            // mg:0xff815359 0x4af08a  (32-bit, two's complement)
            char* e = nullptr;
            unsigned long x = std::strtoul(text.c_str() + at + 3, &e, 16);
            if (e && *e == ' ') {
                char* e2 = nullptr;
                unsigned long y = std::strtoul(e + 1, &e2, 16);
                if (e2 != e + 1 && x <= 0xffffffffUL && y <= 0xffffffffUL) {
                    LatLon p = from_mg(int32_t(uint32_t(x)), int32_t(uint32_t(y)));
                    if (std::fabs(p.lat) <= 85 && std::fabs(p.lon) <= 180 &&
                        (out.empty() || out.back().lat != p.lat || out.back().lon != p.lon))
                        out.push_back(p);
                }
            }
        }
        at = end + 1;
    }
    return out;
}

std::vector<Maneuver> parse_route_geojson(const std::string& text) {
    // Not a general JSON reader: Navit writes one point feature per turn,
    //   { "type": "Feature", "properties": { "Instruction": "...", ... },
    //     "geometry": { "type": "Point", "coordinates": [ lon, lat ] } }
    std::vector<Maneuver> out;
    size_t at = 0;
    const std::string key = "\"Instruction\"";
    while ((at = text.find(key, at)) != std::string::npos) {
        at += key.size();
        size_t q = text.find('"', text.find(':', at));
        if (q == std::string::npos) break;
        std::string s;
        size_t i = q + 1;
        for (; i < text.size() && text[i] != '"'; i++) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                char c = text[++i];
                s += c == 'n' ? ' ' : c == 't' ? ' ' : c;
            } else {
                s += text[i];
            }
        }
        size_t c = text.find("\"coordinates\"", i);
        size_t next = text.find(key, i);
        if (c == std::string::npos || (next != std::string::npos && c > next)) {
            at = i;
            continue;
        }
        size_t b = text.find('[', c);
        if (b == std::string::npos) break;
        char* e = nullptr;
        double lon = std::strtod(text.c_str() + b + 1, &e);
        const char* comma = e ? std::strchr(e, ',') : nullptr;
        double lat = comma ? std::strtod(comma + 1, nullptr) : 0;
        if (comma && std::fabs(lat) <= 85 && std::fabs(lon) <= 180) {
            Maneuver m;
            m.at = {lat, lon};
            m.text = clean_instruction(s);
            m.kind = turn_kind(m.text);
            out.push_back(std::move(m));
        }
        at = b;
    }
    return out;
}

std::string clean_instruction(const std::string& in) {
    std::string s = in;
    // "in 500 meters", "in 2.1 kilometers", "in 350 feet", "in one mile",
    // "in half a mile", "in a quarter of a mile": " in ", up to five words
    // that are numbers, then a unit.
    static const char* const kUnits[] = {"meters", "metres", "meter", "metre", "kilometers", "kilometres", "kilometer",
                                         "kilometre", "feet", "foot", "yards", "yard", "miles", "mile", nullptr};
    static const char* const kNumbers[] = {"a", "an", "one", "two", "three", "four", "five", "six", "seven", "eight",
                                           "nine", "ten", "half", "quarter", "quarters", "of", "and", nullptr};
    auto word_in = [](const std::string& w, const char* const* list) {
        for (const char* const* k = list; *k; k++)
            if (w == *k) return true;
        return false;
    };
    for (size_t p = 0; (p = s.find(" in ", p)) != std::string::npos;) {
        size_t q = p + 4, end = std::string::npos;
        for (int words = 0; words < 6 && q < s.size(); words++) {
            size_t e = s.find(' ', q);
            if (e == std::string::npos) e = s.size();
            std::string w = lower(s.substr(q, e - q));
            if (word_in(w, kUnits) && words > 0) {
                end = e;
                break;
            }
            bool number = !w.empty() && std::all_of(w.begin(), w.end(), [](char c) {
                return std::isdigit(uint8_t(c)) || c == '.' || c == ',';
            });
            if (!number && !word_in(w, kNumbers)) break;
            q = e + 1;
        }
        if (end != std::string::npos) s.erase(p, end - p);
        else p++;
    }
    for (const char* w : {" soon", " now"}) {
        size_t n = std::strlen(w);
        if (s.size() >= n && s.compare(s.size() - n, n, w) == 0) s.erase(s.size() - n);
    }
    // Lane hints and the other separators Navit leaves in names.
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '|') {
            while (i + 1 < s.size() && std::isalpha(uint8_t(s[i + 1]))) i++;
            continue;
        }
        if (s[i] == ';') {
            o += '/';
            while (i + 1 < s.size() && s[i + 1] == ' ') i++;
            continue;
        }
        if (s[i] == ' ' && (o.empty() || o.back() == ' ')) continue;
        o += s[i];
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    if (o.rfind("the ", 0) == 0) o.erase(0, 4);
    return o;
}

int turn_kind(const std::string& text) {
    std::string t = lower(text);
    auto has = [&](const char* w) { return t.find(w) != std::string::npos; };
    if (has("reached your destination") || has("arrive")) return kArrive;
    if (has("turn around") || has("u-turn")) return kUturn;
    if (has("roundabout")) return kRoundabout;
    bool left = has("left"), right = has("right");
    if (has("exit")) return left ? kExitLeft : kExitRight;
    if (has("sharp") || has("strongly")) return left ? kSharpLeft : kSharpRight;
    if (has("keep") || has("slight") || has("easily") || has("merge")) return left ? kSlightLeft : right ? kSlightRight : kStraight;
    if (has("turn") && left) return kLeft;
    if (has("turn") && right) return kRight;
    return kStraight;
}

std::string nmea(double lat, double lon, double speed_mps, double track_deg, const std::string& utc,
                 const std::string& date) {
    auto dm = [](double v, int width) {
        v = std::fabs(v);
        int d = int(v);
        double m = (v - d) * 60;
        if (m >= 59.99995) {   // would print as 60.0000
            d++;
            m = 0;
        }
        char b[32];
        std::snprintf(b, sizeof b, width == 2 ? "%02d%07.4f" : "%03d%07.4f", d, m);
        return std::string(b);
    };
    auto sum = [](const std::string& s) {
        unsigned c = 0;
        for (char ch : s) c ^= uint8_t(ch);
        char b[4];
        std::snprintf(b, sizeof b, "%02X", c);
        return std::string(b);
    };
    std::string ns = lat >= 0 ? "N" : "S", ew = lon >= 0 ? "E" : "W";
    char tail[64];
    std::snprintf(tail, sizeof tail, ",%.1f,%.1f,", speed_mps / 0.514444, std::fmod(track_deg + 360.0, 360.0));
    std::string rmc = "GPRMC," + utc + ".00,A," + dm(lat, 2) + "," + ns + "," + dm(lon, 3) + "," + ew + tail + date + ",,,A";
    std::string gga = "GPGGA," + utc + ".00," + dm(lat, 2) + "," + ns + "," + dm(lon, 3) + "," + ew + ",1,08,1.0,10.0,M,0.0,M,,";
    return "$" + rmc + "*" + sum(rmc) + "\r\n$" + gga + "*" + sum(gga) + "\r\n";
}

long dbus_number(const std::string& reply, long fallback) {
    size_t s = reply.find("string \"");
    if (s == std::string::npos) return fallback;
    s += 8;
    char* e = nullptr;
    long v = std::strtol(reply.c_str() + s, &e, 10);
    return e && e != reply.c_str() + s && *e == '"' ? v : fallback;
}

std::string xml_element(const std::string& xml, const std::string& open_tag_start, const std::string& close_tag) {
    size_t a = xml.find(open_tag_start);
    if (a == std::string::npos) return "";
    // Not inside a comment.
    size_t c = xml.rfind("<!--", a);
    if (c != std::string::npos && xml.find("-->", c) > a) {
        size_t again = xml.find(open_tag_start, xml.find("-->", c));
        if (again == std::string::npos) return "";
        a = again;
    }
    size_t b = xml.find(close_tag, a);
    if (b == std::string::npos) return "";
    return xml.substr(a, b + close_tag.size() - a);
}

std::vector<double> cumulative(const std::vector<LatLon>& line) {
    std::vector<double> c(line.size(), 0);
    for (size_t i = 1; i < line.size(); i++) c[i] = c[i - 1] + metres(line[i - 1], line[i]);
    return c;
}

Progress locate(const std::vector<LatLon>& line, const std::vector<double>& cum, LatLon at, size_t hint) {
    Progress best;
    best.off_m = 1e300;
    if (line.size() < 2 || cum.size() != line.size()) {
        if (!line.empty()) best.off_m = metres(line[0], at);
        return best;
    }
    // Look near where we were first (the route is long; you move little
    // between looks), then everywhere if that isn't close.
    auto scan = [&](size_t from, size_t to) {
        double k = std::cos(at.lat * kPi / 180);
        for (size_t i = from; i < to && i + 1 < line.size(); i++) {
            double ax = (line[i].lon - at.lon) * k, ay = line[i].lat - at.lat;
            double bx = (line[i + 1].lon - at.lon) * k, by = line[i + 1].lat - at.lat;
            double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
            double t = len2 > 0 ? std::clamp(-(ax * dx + ay * dy) / len2, 0.0, 1.0) : 0;
            double px = ax + t * dx, py = ay + t * dy;
            double off = std::sqrt(px * px + py * py) * kPi / 180 * 6371008.8;
            if (off < best.off_m) {
                best.off_m = off;
                best.seg = i;
                best.along_m = cum[i] + t * (cum[i + 1] - cum[i]);
            }
        }
    };
    size_t from = hint > 20 ? hint - 20 : 0;
    scan(from, std::min(line.size(), hint + 400));
    if (best.off_m > 60) scan(0, line.size());
    return best;
}

}  // namespace zmap::nav
