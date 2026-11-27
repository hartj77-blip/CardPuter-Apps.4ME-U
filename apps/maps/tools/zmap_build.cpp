// zmap-build: turn an OpenStreetMap extract (.osm.pbf) into a .zmap file
// for the Maps app (../zmap.h, ../README.md).
//
//   zmap-build --name "New Jersey" --poly new-jersey.poly --imperial
//       new-jersey-latest.osm.pbf new-jersey.zmap
//
//   --name     what the app calls the map
//   --poly     the region's outline, in Osmosis .poly format (Geofabrik has
//              one beside every extract). Without it, the outline is the box
//              around the data. Outside the outline the app shows "no map"
//   --imperial the app shows miles and mph by default on this map
//   --date     the data's date (default: the file's own timestamp)
//   --search F also write the search index (streets and places) to F
//   --addresses F  also write the house numbers (from points and buildings
//              with addr:housenumber and addr:street) for Go to, to F (../addr.h)
//   --roads F  also write the roads a car can use, with their turn
//              restrictions, as OpenStreetMap XML to F: what Navit's maptool
//              makes Maps' routing map from (prepare.sh)
//
// It runs on a PC or in CI, not on the Cardputer: it holds every node of
// the extract in memory (about 0.5 GB for New Jersey).
//
// What it does: reads roads, railways, water, woods, parks, the coastline,
// the state line and place names; builds the sea from the coastline; and
// writes them at four levels of detail, each simplified for its scale and
// cut into tiles.
#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../addr.h"
#include "../fold.h"
#include "../zmap.h"
#include "pbf.h"

namespace {

using namespace zmap;

struct Pt {
    int32_t x, y;
    bool operator==(const Pt& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Pt& o) const { return !(*this == o); }
};
using Line = std::vector<Pt>;

struct Feat {
    uint8_t kind = 0, speed = 0;
    uint32_t name = 0, ref = 0;
    std::vector<Line> parts;
};

[[noreturn]] void die(const std::string& why) {
    std::fprintf(stderr, "zmap-build: %s\n", why.c_str());
    std::exit(1);
}

// ---- Geometry ------------------------------------------------------------

double ring_area(const Line& r) {   // > 0: clockwise on a north-up map (y grows south)
    double a = 0;
    for (size_t i = 0, n = r.size(); i < n; i++) {
        const Pt &p = r[i], &q = r[(i + 1) % n];
        a += double(p.x) * q.y - double(q.x) * p.y;
    }
    return a / 2;
}

// Douglas-Peucker: drop points closer than tol to the line through their
// neighbours. The ends stay.
void simplify(const Line& in, double tol, Line& out) {
    out.clear();
    size_t n = in.size();
    if (n < 3) {
        out = in;
        return;
    }
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    std::vector<std::pair<size_t, size_t>> stack{{0, n - 1}};
    double tol2 = tol * tol;
    while (!stack.empty()) {
        auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;
        double ax = in[a].x, ay = in[a].y, dx = in[b].x - ax, dy = in[b].y - ay;
        double len2 = dx * dx + dy * dy, worst = -1;
        size_t at = a;
        for (size_t i = a + 1; i < b; i++) {
            double px = in[i].x - ax, py = in[i].y - ay, d2;
            if (len2 <= 0) {
                d2 = px * px + py * py;
            } else {
                double t = std::clamp((px * dx + py * dy) / len2, 0.0, 1.0);
                double ex = px - t * dx, ey = py - t * dy;
                d2 = ex * ex + ey * ey;
            }
            if (d2 > worst) {
                worst = d2;
                at = i;
            }
        }
        if (worst > tol2) {
            keep[at] = 1;
            stack.push_back({a, at});
            stack.push_back({at, b});
        }
    }
    for (size_t i = 0; i < n; i++)
        if (keep[i]) out.push_back(in[i]);
}

// A ring (no repeated last point) is simplified as two halves, split at
// its first point and the point farthest from it, so it can't collapse.
void simplify_ring(const Line& in, double tol, Line& out) {
    out.clear();
    size_t n = in.size();
    if (n < 4) {
        out = in;
        return;
    }
    size_t far = 0;
    double best = -1;
    for (size_t i = 1; i < n; i++) {
        double dx = double(in[i].x) - in[0].x, dy = double(in[i].y) - in[0].y;
        if (dx * dx + dy * dy > best) {
            best = dx * dx + dy * dy;
            far = i;
        }
    }
    Line a(in.begin(), in.begin() + far + 1), b(in.begin() + far, in.end()), sa, sb;
    b.push_back(in[0]);
    simplify(a, tol, sa);
    simplify(b, tol, sb);
    out = sa;
    out.insert(out.end(), sb.begin() + 1, sb.end() - 1);
}

// Keep the part of each line on one side of an axis-parallel border.
// axis 0: x, 1: y. keep_low: keep coordinates <= bound, else >= bound.
Pt cut(const Pt& a, const Pt& b, int axis, int32_t bound) {
    if (axis == 0) {
        double t = double(bound - a.x) / double(b.x - a.x);
        return {bound, int32_t(std::lround(a.y + t * (double(b.y) - a.y)))};
    }
    double t = double(bound - a.y) / double(b.y - a.y);
    return {int32_t(std::lround(a.x + t * (double(b.x) - a.x))), bound};
}

void clip_lines(const std::vector<Line>& in, int axis, int32_t bound, bool keep_low, std::vector<Line>& out) {
    out.clear();
    auto inside = [&](const Pt& p) {
        int32_t v = axis == 0 ? p.x : p.y;
        return keep_low ? v <= bound : v >= bound;
    };
    for (const Line& l : in) {
        Line cur;
        for (size_t i = 0; i < l.size(); i++) {
            bool in_i = inside(l[i]);
            if (i > 0) {
                bool in_p = inside(l[i - 1]);
                if (in_p != in_i) {
                    Pt c = cut(l[i - 1], l[i], axis, bound);
                    if (in_p) {
                        cur.push_back(c);
                        if (cur.size() >= 2) out.push_back(cur);
                        cur.clear();
                    } else {
                        cur.push_back(c);
                    }
                }
            }
            if (in_i) cur.push_back(l[i]);
        }
        if (cur.size() >= 2) out.push_back(std::move(cur));
    }
}

// Sutherland-Hodgman against one border. Edges it adds run along the
// border; an even-odd fill doesn't mind them.
void clip_rings(const std::vector<Line>& in, int axis, int32_t bound, bool keep_low, std::vector<Line>& out) {
    out.clear();
    auto inside = [&](const Pt& p) {
        int32_t v = axis == 0 ? p.x : p.y;
        return keep_low ? v <= bound : v >= bound;
    };
    for (const Line& r : in) {
        Line o;
        size_t n = r.size();
        for (size_t i = 0; i < n; i++) {
            const Pt &a = r[i], &b = r[(i + 1) % n];
            bool ia = inside(a), ib = inside(b);
            if (ia) o.push_back(a);
            if (ia != ib) o.push_back(cut(a, b, axis, bound));
        }
        if (o.size() >= 3) out.push_back(std::move(o));
    }
}

// The region's outline, with a grid of rows for a fast inside test.
struct Outline {
    std::vector<Line> rings;        // outer rings, clockwise on the map
    std::vector<Line> holes;
    int32_t min_x = 0, min_y = 0, max_x = 0, max_y = 0;

    struct Edge {
        Pt a, b;
    };
    static constexpr int kRows = 2048;
    std::vector<std::vector<Edge>> rows;

    void finish() {
        if (rings.empty()) die("the outline has no rings");
        // A ring inside another one is a hole, whatever the file called it.
        for (size_t i = 0; i < rings.size();) {
            bool inside = false;
            for (size_t j = 0; j < rings.size() && !inside; j++)
                inside = j != i && std::fabs(ring_area(rings[j])) > std::fabs(ring_area(rings[i])) && in_ring(rings[j], rings[i][0]);
            if (inside) {
                holes.push_back(std::move(rings[i]));
                rings.erase(rings.begin() + long(i));
            } else {
                i++;
            }
        }
        min_x = min_y = INT32_MAX;
        max_x = max_y = INT32_MIN;
        for (Line& r : rings) {
            if (ring_area(r) < 0) std::reverse(r.begin(), r.end());
            for (const Pt& p : r) {
                min_x = std::min(min_x, p.x), max_x = std::max(max_x, p.x);
                min_y = std::min(min_y, p.y), max_y = std::max(max_y, p.y);
            }
        }
        rows.assign(kRows, {});
        auto add = [&](const Line& r) {
            for (size_t i = 0, n = r.size(); i < n; i++) {
                Edge e{r[i], r[(i + 1) % n]};
                if (e.a.y == e.b.y) continue;
                int r0 = row(std::min(e.a.y, e.b.y)), r1 = row(std::max(e.a.y, e.b.y));
                for (int k = r0; k <= r1; k++) rows[k].push_back(e);
            }
        };
        for (const Line& r : rings) add(r);
        for (const Line& r : holes) add(r);
    }
    static bool in_ring(const Line& r, const Pt& p) {
        bool in = false;
        for (size_t i = 0, n = r.size(); i < n; i++) {
            const Pt &a = r[i], &b = r[(i + 1) % n];
            if ((a.y > p.y) == (b.y > p.y)) continue;
            if (a.x + (double(p.y) - a.y) / (double(b.y) - a.y) * (double(b.x) - a.x) > p.x) in = !in;
        }
        return in;
    }
    int row(int32_t y) const {
        double f = (double(y) - min_y) / std::max(1.0, double(max_y) - min_y);
        return std::clamp(int(f * kRows), 0, kRows - 1);
    }
    bool contains(const Pt& p) const {
        if (p.x < min_x || p.x > max_x || p.y < min_y || p.y > max_y) return false;
        bool in = false;
        for (const Edge& e : rows[row(p.y)]) {
            if ((e.a.y > p.y) == (e.b.y > p.y)) continue;
            double x = e.a.x + (double(p.y) - e.a.y) / (double(e.b.y) - e.a.y) * (double(e.b.x) - e.a.x);
            if (x > p.x) in = !in;
        }
        return in;
    }
};

// ---- What the tags mean --------------------------------------------------

uint8_t road_kind(std::string_view hw) {
    if (hw == "motorway") return kMotorway;
    if (hw == "trunk") return kTrunk;
    if (hw == "primary") return kPrimary;
    if (hw == "secondary") return kSecondary;
    if (hw == "tertiary" || hw == "motorway_link" || hw == "trunk_link") return kTertiary;
    if (hw == "unclassified" || hw == "residential" || hw == "living_street" || hw == "road" ||
        hw == "primary_link" || hw == "secondary_link" || hw == "tertiary_link")
        return kResidential;
    if (hw == "service") return kService;
    return 0;
}

uint8_t line_kind(const pbf::Tags& t) {
    using pbf::tag;
    std::string_view hw = tag(t, "highway");
    if (!hw.empty()) {
        if (tag(t, "area") == "yes") return 0;
        uint8_t k = road_kind(hw);
        if (k == kService) {
            std::string_view s = tag(t, "service");
            if (s == "driveway" || s == "parking_aisle" || s == "drive-through") return 0;
        }
        return k;
    }
    std::string_view rw = tag(t, "railway");
    if (rw == "rail" || rw == "light_rail" || rw == "subway" || rw == "narrow_gauge") {
        std::string_view s = tag(t, "service");
        if (!s.empty() || tag(t, "tunnel") == "yes") return 0;   // yards, sidings; lines underground
        return kRail;
    }
    if (tag(t, "route") == "ferry") return kFerry;
    std::string_view ww = tag(t, "waterway");
    if (!ww.empty()) {
        if (tag(t, "tunnel") == "yes" || tag(t, "tunnel") == "culvert") return 0;
        if (ww == "river" || ww == "canal") return kRiver;
        if (ww == "stream") return kStream;
        return 0;
    }
    if (tag(t, "aeroway") == "runway" && tag(t, "area") != "yes") return kRunway;
    return 0;
}

uint8_t area_kind(const pbf::Tags& t) {
    using pbf::tag;
    std::string_view v = tag(t, "natural");
    if (v == "water") return kWater;
    if (v == "wood") return kForest;
    if (v == "wetland") return kWetland;
    if (v == "beach" || v == "sand") return kSand;
    v = tag(t, "waterway");
    if (v == "riverbank" || v == "dock") return kWater;
    v = tag(t, "landuse");
    if (v == "reservoir" || v == "basin") return kWater;
    if (v == "forest") return kForest;
    if (v == "cemetery" || v == "recreation_ground") return kPark;
    v = tag(t, "leisure");
    if (v == "park" || v == "golf_course" || v == "nature_reserve" || v == "garden") return kPark;
    if (tag(t, "aeroway") == "aerodrome") return kAirport;
    return 0;
}

uint8_t place_kind(std::string_view p) {
    if (p == "city") return kCity;
    if (p == "town") return kTown;
    if (p == "village") return kVillage;
    if (p == "suburb" || p == "hamlet" || p == "borough") return kSuburb;
    if (p == "neighbourhood" || p == "quarter") return kNeighbourhood;
    return 0;
}

// "55 mph" -> 55 with bit 7; "50" -> 50 (km/h). 0 if it isn't a number.
uint8_t speed_of(std::string_view s) {
    int v = 0;
    size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && v < 1000) v = v * 10 + (s[i++] - '0');
    if (i == 0 || v <= 0 || v > 127) return 0;
    return uint8_t(v) | (s.find("mph") != std::string_view::npos ? 0x80 : 0);
}

// Which features a level holds. Levels are named by their tile zoom.
bool level_has(int zoom, uint8_t kind) {
    switch (kind) {
        case kCoverage: case kSea: case kWater: case kBoundary:
        case kMotorway: case kTrunk: case kPrimary: case kCity: case kTown:
            return true;
        case kSecondary: case kTertiary: case kForest: case kPark: case kWetland: case kAirport:
        case kRiver: case kRail: case kFerry: case kVillage:
            return zoom >= 9;
        case kResidential: case kSand: case kSuburb: case kRunway:
            return zoom >= 11;
        default:
            return zoom >= 13;
    }
}

// ---- Routing data and search ---------------------------------------------

// Roads a car may drive on (Navit decides the rest from the tags).
bool car_road(const pbf::Tags& t) {
    static const char* const kRoads[] = {"motorway", "trunk", "primary", "secondary", "tertiary", "motorway_link",
                                         "trunk_link", "primary_link", "secondary_link", "tertiary_link",
                                         "unclassified", "residential", "living_street", "service", "road", nullptr};
    std::string_view hw = pbf::tag(t, "highway");
    if (hw.empty() || pbf::tag(t, "area") == "yes") return false;
    std::string_view service = pbf::tag(t, "service");   // as on the map: no driveways or car parks
    if (service == "driveway" || service == "parking_aisle" || service == "drive-through") return false;
    for (const char* const* k = kRoads; *k; k++)
        if (hw == *k) return true;
    return false;
}

const char* const kRoadKeys[] = {"highway", "name", "ref", "oneway", "junction", "maxspeed", "access", "motor_vehicle",
                                 "motorcar", "vehicle", "toll", "service", "lanes", "bridge", "tunnel", "layer",
                                 "destination", "destination:ref", nullptr};

std::string xml_escape(std::string_view v) {
    std::string o;
    for (char c : v) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += uint8_t(c) < 0x20 ? ' ' : c;
        }
    }
    return o;
}

std::string xml_tags(const pbf::Tags& t, const char* const* keys) {
    std::string o;
    for (const auto& kv : t)
        for (const char* const* k = keys; *k; k++)
            if (kv.first == *k) {
                o += "  <tag k=\"" + xml_escape(kv.first) + "\" v=\"" + xml_escape(kv.second) + "\"/>\n";
                break;
            }
    return o;
}

// ---- The builder ---------------------------------------------------------

struct Strings {
    std::string data = std::string(1, '\0');
    std::unordered_map<std::string, uint32_t> at;
    uint32_t add(std::string_view s, size_t max) {
        if (s.empty()) return 0;
        std::string k(s.substr(0, max));
        while (!k.empty() && (uint8_t(k.back()) & 0xC0) == 0x80) k.pop_back();   // not half a UTF-8 character
        if (k.size() < s.size() && !k.empty() && (uint8_t(k.back()) & 0x80)) k.pop_back();
        for (char& c : k)
            if (uint8_t(c) < 0x20) c = ' ';
        if (k.empty()) return 0;
        auto it = at.find(k);
        if (it != at.end()) return it->second;
        uint32_t off = uint32_t(data.size());
        data += k;
        data += '\0';
        at.emplace(std::move(k), off);
        return off;
    }
};

struct Builder {
    // Input
    std::vector<int64_t> node_id;
    std::vector<Pt> node_pt;
    bool nodes_sorted = true;
    std::vector<int64_t> way_id;          // every way, in file order
    std::vector<uint64_t> way_start;      // into way_refs; one more entry at the end
    std::vector<int64_t> way_refs;
    bool ways_sorted = true;

    struct WayFeat {
        size_t way;
        uint8_t kind, speed;
        uint32_t name, ref;
        bool area;
    };
    std::vector<WayFeat> way_feats;
    std::vector<size_t> coast_ways;
    struct Multi {
        uint8_t kind;
        std::vector<int64_t> outer, inner;
    };
    std::vector<Multi> multis;
    std::unordered_set<int64_t> boundary_ways;
    std::vector<Feat> feats;              // everything, in world coordinates
    // For --roads: car roads with the tags routing needs, and turn restrictions.
    bool want_roads = false;
    struct Road {
        size_t way;
        std::string tags;                 // as XML <tag/> lines
    };
    std::vector<Road> roads;
    struct Restriction {
        std::string members, tags;        // as XML lines
    };
    std::vector<Restriction> restrictions;
    // For --addresses: each street's key (fold_key), its name as written,
    // and every house number on it with where it is.
    bool want_addresses = false;
    struct AddrIn {
        uint32_t street, number, city;
        Pt at;
    };
    std::vector<AddrIn> addresses;
    std::unordered_map<std::string, uint32_t> addr_street_at;
    std::vector<std::pair<std::string, std::string>> addr_streets;   // key, name
    Strings addr_strings;
    void add_address(const pbf::Tags& tags, Pt at) {
        std::string_view number = pbf::tag(tags, "addr:housenumber"), street = pbf::tag(tags, "addr:street");
        if (number.empty() || street.empty() || number.size() > 16) return;
        std::string key = zmap::street_key(street);
        if (key.empty()) return;
        auto it = addr_street_at.find(key);
        uint32_t id;
        if (it == addr_street_at.end()) {
            id = uint32_t(addr_streets.size());
            addr_street_at.emplace(key, id);
            addr_streets.push_back({key, std::string(street.substr(0, 60))});
        } else {
            id = it->second;
        }
        addresses.push_back({id, addr_strings.add(number, 16), addr_strings.add(pbf::tag(tags, "addr:city"), 40), at});
    }
    Strings strings;
    Outline outline;
    bool have_outline = false;
    int64_t timestamp = 0;
    double mid_lat = 0;

    bool node(int64_t id, Pt& out) const {
        auto it = std::lower_bound(node_id.begin(), node_id.end(), id);
        if (it == node_id.end() || *it != id) return false;
        out = node_pt[size_t(it - node_id.begin())];
        return true;
    }
    long way_index(int64_t id) const {
        auto it = std::lower_bound(way_id.begin(), way_id.end(), id);
        return it == way_id.end() || *it != id ? -1 : long(it - way_id.begin());
    }
    // A way's points. Nodes the extract doesn't have are left out.
    Line way_line(size_t w) const {
        Line l;
        for (uint64_t i = way_start[w]; i < way_start[w + 1]; i++) {
            Pt p;
            if (node(way_refs[i], p) && (l.empty() || l.back() != p)) l.push_back(p);
        }
        return l;
    }

    void read(const std::string& path);
    void write_roads(const std::string& path) const;
    void write_search(const std::string& path) const;
    void write_addresses(const std::string& path) const;
    void make_features();
    void make_sea(std::vector<Line>& land_sample);
    void write(const std::string& path, const std::string& name, const std::string& date, bool imperial);
};

void Builder::read(const std::string& path) {
    pbf::Handler h;
    h.timestamp = [&](int64_t t) { timestamp = t; };
    h.node = [&](int64_t id, double lat, double lon, const pbf::Tags& tags) {
        if (lat > 85 || lat < -85) return;
        if (!node_id.empty() && id <= node_id.back()) nodes_sorted = false;
        Pt p{int32_t(std::lround(lon_to_x(lon))), int32_t(std::lround(lat_to_y(lat)))};
        node_id.push_back(id);
        node_pt.push_back(p);
        if (tags.empty()) return;
        if (want_addresses) add_address(tags, p);
        uint8_t k = place_kind(pbf::tag(tags, "place"));
        std::string_view name = pbf::tag(tags, "name");
        if (!k || name.empty()) return;
        Feat f;
        f.kind = k;
        f.name = strings.add(name, 40);
        f.parts.push_back({p});
        feats.push_back(std::move(f));
    };
    bool first_way = true;
    h.way = [&](int64_t id, const std::vector<int64_t>& refs, const pbf::Tags& tags) {
        if (first_way) {
            first_way = false;
            if (!nodes_sorted) {   // rare: sort the nodes by id once
                std::vector<size_t> order(node_id.size());
                for (size_t i = 0; i < order.size(); i++) order[i] = i;
                std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return node_id[a] < node_id[b]; });
                std::vector<int64_t> ids(order.size());
                std::vector<Pt> pts(order.size());
                for (size_t i = 0; i < order.size(); i++) ids[i] = node_id[order[i]], pts[i] = node_pt[order[i]];
                node_id.swap(ids);
                node_pt.swap(pts);
            }
        }
        if (refs.size() < 2) return;
        if (!way_id.empty() && id <= way_id.back()) ways_sorted = false;
        size_t w = way_id.size();
        way_id.push_back(id);
        way_start.push_back(way_refs.size());
        way_refs.insert(way_refs.end(), refs.begin(), refs.end());
        if (tags.empty()) return;
        if (want_addresses && !pbf::tag(tags, "addr:housenumber").empty()) {   // a building: its middle
            double sx = 0, sy = 0;
            size_t n = 0;
            for (size_t i = 0; i + (refs.front() == refs.back() ? 1 : 0) < refs.size(); i++) {
                Pt q;
                if (node(refs[i], q)) sx += q.x, sy += q.y, n++;
            }
            if (n) add_address(tags, {int32_t(std::lround(sx / double(n))), int32_t(std::lround(sy / double(n)))});
        }
        if (pbf::tag(tags, "natural") == "coastline") coast_ways.push_back(w);
        if (want_roads && car_road(tags)) roads.push_back({w, xml_tags(tags, kRoadKeys)});
        uint8_t k = line_kind(tags);
        if (k) {
            WayFeat f{w, k, 0, 0, 0, false};
            f.name = strings.add(pbf::tag(tags, "name"), 60);
            if (is_road(k)) {
                std::string_view ref = pbf::tag(tags, "ref");
                f.ref = strings.add(ref.substr(0, ref.find(';')), 12);
                f.speed = speed_of(pbf::tag(tags, "maxspeed"));
            }
            way_feats.push_back(f);
            return;
        }
        k = area_kind(tags);
        if (k && refs.size() >= 4 && refs.front() == refs.back()) way_feats.push_back({w, k, 0, 0, 0, true});
    };
    h.relation = [&](int64_t, const std::vector<pbf::Member>& members, const pbf::Tags& tags) {
        std::string_view type = pbf::tag(tags, "type");
        if (type == "multipolygon") {
            uint8_t k = area_kind(tags);
            if (!k) return;
            Multi m;
            m.kind = k;
            for (const pbf::Member& mb : members)
                if (mb.type == 1) (mb.role == "inner" ? m.inner : m.outer).push_back(mb.id);
            multis.push_back(std::move(m));
        } else if (want_roads && type == "restriction") {
            Restriction r;
            static const char* kTypes[] = {"node", "way", "relation"};
            for (const pbf::Member& mb : members)
                r.members += "  <member type=\"" + std::string(kTypes[mb.type % 3]) + "\" ref=\"" + std::to_string(mb.id) +
                             "\" role=\"" + xml_escape(mb.role) + "\"/>\n";
            static const char* const kKeys[] = {"type", "restriction", "except", nullptr};
            r.tags = xml_tags(tags, kKeys);
            restrictions.push_back(std::move(r));
        } else if (type == "boundary" && pbf::tag(tags, "boundary") == "administrative" &&
                   pbf::tag(tags, "admin_level") == "4") {
            for (const pbf::Member& mb : members)
                if (mb.type == 1) boundary_ways.insert(mb.id);
        }
    };
    pbf::read(path, h);
    way_start.push_back(way_refs.size());
    if (!ways_sorted) die("the ways in this file aren't sorted by id; sort it with: osmium sort");
    if (node_id.empty()) die("no nodes in " + path);
}

// Join ways end to end into rings. `closed` gets the rings (no repeated
// last point); `open` gets the chains that never closed. `directed`: a way
// only follows one that ends where it starts (the coastline, whose ways all
// run the same way round); otherwise ways are turned round as needed.
void join_ways(const Builder& b, const std::vector<int64_t>& ids, bool directed, std::vector<Line>& closed,
               std::vector<Line>& open) {
    struct Piece {
        int64_t first, last;
        Line pts;
        bool used = false;
    };
    std::vector<Piece> pieces;
    for (int64_t id : ids) {
        long w = b.way_index(id);
        if (w < 0) continue;
        Piece p;
        p.first = b.way_refs[b.way_start[size_t(w)]];
        p.last = b.way_refs[b.way_start[size_t(w) + 1] - 1];
        p.pts = b.way_line(size_t(w));
        if (p.pts.size() < 2) continue;
        pieces.push_back(std::move(p));
    }
    std::unordered_multimap<int64_t, size_t> by_first, by_last;
    for (size_t i = 0; i < pieces.size(); i++) {
        by_first.emplace(pieces[i].first, i);
        by_last.emplace(pieces[i].last, i);
    }
    // An unused piece with an end at this node: {index, turned round}.
    auto find = [&](int64_t node, bool at_first) -> std::pair<long, bool> {
        for (int turned = 0; turned < (directed ? 1 : 2); turned++) {
            auto& m = (at_first != bool(turned)) ? by_first : by_last;
            auto range = m.equal_range(node);
            for (auto it = range.first; it != range.second; ++it)
                if (!pieces[it->second].used) return {long(it->second), bool(turned)};
        }
        return {-1, false};
    };
    for (size_t i = 0; i < pieces.size(); i++) {
        if (pieces[i].used) continue;
        pieces[i].used = true;
        std::vector<std::pair<size_t, bool>> fwd{{i, false}}, back;
        int64_t start = pieces[i].first, end = pieces[i].last;
        while (end != start) {   // onwards from the end
            auto [next, turned] = find(end, true);
            if (next < 0) break;
            pieces[size_t(next)].used = true;
            fwd.push_back({size_t(next), turned});
            end = turned ? pieces[size_t(next)].first : pieces[size_t(next)].last;
        }
        while (end != start) {   // and backwards from the start
            auto [prev, turned] = find(start, false);
            if (prev < 0) break;
            pieces[size_t(prev)].used = true;
            back.push_back({size_t(prev), turned});
            start = turned ? pieces[size_t(prev)].last : pieces[size_t(prev)].first;
        }
        Line ring;
        auto append = [&](const std::pair<size_t, bool>& pc) {
            const Line& l = pieces[pc.first].pts;
            size_t skip = ring.empty() ? 0 : 1;
            if (pc.second) ring.insert(ring.end(), l.rbegin() + long(skip), l.rend());
            else ring.insert(ring.end(), l.begin() + long(skip), l.end());
        };
        for (auto it = back.rbegin(); it != back.rend(); ++it) append(*it);
        for (const auto& pc : fwd) append(pc);
        if (end == start) {
            if (ring.size() > 1 && ring.front() == ring.back()) ring.pop_back();
            if (ring.size() >= 3) closed.push_back(std::move(ring));
        } else {
            open.push_back(std::move(ring));
        }
    }
}

void Builder::make_features() {
    // Areas that are also the outer ring of a multipolygon of the same kind
    // are drawn once, by the multipolygon (which knows the holes).
    std::unordered_map<int64_t, uint8_t> outer_of;
    for (const Multi& m : multis)
        for (int64_t id : m.outer) outer_of[id] = m.kind;

    std::unordered_set<int64_t> seen_boundary;
    for (const WayFeat& wf : way_feats) {
        if (wf.area) {
            auto it = outer_of.find(way_id[wf.way]);
            if (it != outer_of.end() && it->second == wf.kind) continue;
        }
        Line l = way_line(wf.way);
        Feat f;
        f.kind = wf.kind, f.speed = wf.speed, f.name = wf.name, f.ref = wf.ref;
        if (wf.area) {
            if (l.size() > 1 && l.front() == l.back()) l.pop_back();
            if (l.size() < 3) continue;
        } else if (l.size() < 2) {
            continue;
        }
        f.parts.push_back(std::move(l));
        feats.push_back(std::move(f));
    }
    for (int64_t id : boundary_ways) {
        long w = way_index(id);
        if (w < 0) continue;
        Line l = way_line(size_t(w));
        if (l.size() < 2) continue;
        Feat f;
        f.kind = kBoundary;
        f.parts.push_back(std::move(l));
        feats.push_back(std::move(f));
    }
    size_t dropped = 0;
    for (const Multi& m : multis) {
        Feat f;
        f.kind = m.kind;
        for (const std::vector<int64_t>* ids : {&m.outer, &m.inner}) {
            std::vector<Line> open;
            join_ways(*this, *ids, false, f.parts, open);
            // A ring the extract cut off: close it if both ends are outside
            // the outline, where nothing is drawn anyway.
            for (Line& o : open) {
                if (have_outline && o.size() >= 3 && !outline.contains(o.front()) && !outline.contains(o.back()))
                    f.parts.push_back(std::move(o));
                else
                    dropped++;
            }
        }
        if (!f.parts.empty()) feats.push_back(std::move(f));
    }
    // Nothing of a feature inside the outline: it isn't part of this map.
    if (have_outline) {
        size_t before = feats.size();
        feats.erase(std::remove_if(feats.begin(), feats.end(),
                                   [&](const Feat& f) {
                                       for (const Line& l : f.parts)
                                           for (const Pt& p : l)
                                               if (outline.contains(p)) return false;
                                       return true;
                                   }),
                    feats.end());
        if (before != feats.size()) std::fprintf(stderr, "left out %zu features outside the outline\n", before - feats.size());
    }
    if (dropped) std::fprintf(stderr, "note: %zu multipolygon rings are incomplete in this extract and were left out\n", dropped);
}

// The sea. OpenStreetMap has no sea areas, only the coastline: ways with
// land on their left and water on their right. Inside the outline, each
// stretch of coast is joined to the next one along the outline itself,
// going clockwise, which keeps the water on the right. Islands are closed
// coastlines; they become holes (the fill is even-odd).
void Builder::make_sea(std::vector<Line>& land_sample) {
    if (coast_ways.empty()) return;
    // The outline without its holes: the sea is worked out against that.
    Outline shell;
    shell.rings = outline.rings;
    shell.finish();
    std::vector<int64_t> ids;
    for (size_t w : coast_ways) ids.push_back(way_id[w]);
    std::vector<Line> closed, open;
    join_ways(*this, ids, true, closed, open);

    Feat sea;
    sea.kind = kSea;
    std::vector<Line> chains = std::move(open);
    for (Line& r : closed) {
        size_t in = 0;
        for (const Pt& p : r) in += shell.contains(p);
        if (in == r.size()) {
            sea.parts.push_back(std::move(r));   // an island
        } else if (in > 0) {
            // Crosses the outline: start it at a point outside and treat it as a chain.
            size_t s = 0;
            while (shell.contains(r[s])) s++;
            std::rotate(r.begin(), r.begin() + long(s), r.end());
            r.push_back(r.front());
            chains.push_back(std::move(r));
        }
    }

    struct Cross {
        int ring = -1;
        double s = 0;   // edge number + how far along it
        Pt pt{};
    };
    struct Piece {
        Line pts;
        Cross in, out;
        bool used = false;
    };
    // Where a segment crosses the outline, nearest to `near`. If it never
    // does (the chain just stops inside), the nearest point of the shell.
    double max_gap = 3000 * units_per_metre(mid_lat);
    auto crossing = [&](const Pt& a, const Pt& b, const Pt& near, bool seg, Cross& c) {
        double best = 1e300;
        for (size_t ri = 0; ri < shell.rings.size(); ri++) {
            const Line& r = shell.rings[ri];
            for (size_t i = 0, n = r.size(); i < n; i++) {
                const Pt &p = r[i], &q = r[(i + 1) % n];
                double ex = double(q.x) - p.x, ey = double(q.y) - p.y;
                double t, px, py;
                if (seg) {
                    double dx = double(b.x) - a.x, dy = double(b.y) - a.y;
                    double den = dx * ey - dy * ex;
                    if (den == 0) continue;
                    double u = ((double(p.x) - a.x) * ey - (double(p.y) - a.y) * ex) / den;
                    t = ((double(p.x) - a.x) * dy - (double(p.y) - a.y) * dx) / den;
                    if (u < 0 || u > 1 || t < 0 || t > 1) continue;
                    px = a.x + u * dx, py = a.y + u * dy;
                } else {
                    double len2 = ex * ex + ey * ey;
                    t = len2 > 0 ? std::clamp(((double(near.x) - p.x) * ex + (double(near.y) - p.y) * ey) / len2, 0.0, 1.0) : 0;
                    px = p.x + t * ex, py = p.y + t * ey;
                }
                double d = (px - near.x) * (px - near.x) + (py - near.y) * (py - near.y);
                if (d < best) {
                    best = d;
                    c.ring = int(ri);
                    c.s = double(i) + std::min(t, 0.999999);
                    c.pt = {int32_t(std::lround(px)), int32_t(std::lround(py))};
                }
            }
        }
        return best < 1e300 && (seg || std::sqrt(best) <= max_gap);
    };

    std::vector<Piece> pieces;
    bool bad = false;
    for (const Line& ch : chains) {
        Piece cur;
        bool open_piece = false;
        for (size_t i = 0; i < ch.size(); i++) {
            bool in = shell.contains(ch[i]);
            bool was = i > 0 && shell.contains(ch[i - 1]);
            if (in && !was) {   // coming in
                cur = Piece{};
                bool ok = i > 0 ? crossing(ch[i - 1], ch[i], ch[i], true, cur.in) : false;
                if (!ok) ok = crossing(ch[i], ch[i], ch[i], false, cur.in);
                if (!ok) bad = true;
                cur.pts.push_back(cur.in.pt);
                open_piece = true;
            }
            if (in) cur.pts.push_back(ch[i]);
            bool last = i + 1 == ch.size();
            if (open_piece && ((!in && was) || (in && last))) {   // going out, or stopping inside
                const Pt& from = in ? ch[i] : ch[i - 1];
                bool ok = !in ? crossing(ch[i - 1], ch[i], from, true, cur.out) : false;
                if (!ok) ok = crossing(from, from, from, false, cur.out);
                if (!ok) bad = true;
                cur.pts.push_back(cur.out.pt);
                pieces.push_back(std::move(cur));
                open_piece = false;
            }
        }
    }
    if (bad) {
        std::fprintf(stderr, "warning: the coastline stops inside the outline; the sea is left out\n");
        return;
    }
    // Follow each piece, then the outline clockwise to the next piece's start.
    for (size_t first = 0; first < pieces.size() && !bad; first++) {
        if (pieces[first].used) continue;
        Line poly;
        size_t cur = first;
        for (;;) {
            Piece& p = pieces[cur];
            p.used = true;
            poly.insert(poly.end(), p.pts.begin(), p.pts.end());
            const Line& ring = shell.rings[size_t(p.out.ring)];
            double n = double(ring.size()), best = 1e300;
            long next = -1;
            for (size_t k = 0; k < pieces.size(); k++) {
                if (pieces[k].in.ring != p.out.ring) continue;
                double d = pieces[k].in.s - p.out.s;
                if (d < 0 || (d == 0 && k != cur)) d += n;
                if (d < best) best = d, next = long(k);
            }
            if (next < 0) {
                bad = true;
                break;
            }
            size_t i = size_t(p.out.s), j = size_t(pieces[size_t(next)].in.s);
            bool wraps = pieces[size_t(next)].in.s < p.out.s || (pieces[size_t(next)].in.s == p.out.s && size_t(next) != cur);
            for (size_t v = i + 1; wraps ? v < ring.size() : v <= j; v++) poly.push_back(ring[v]);
            if (wraps)
                for (size_t v = 0; v <= j; v++) poly.push_back(ring[v]);
            if (size_t(next) == first) break;
            if (pieces[size_t(next)].used) {
                bad = true;
                break;
            }
            cur = size_t(next);
        }
        if (!bad && poly.size() >= 3) sea.parts.push_back(std::move(poly));
    }
    if (bad) {
        std::fprintf(stderr, "warning: the coastline doesn't join up; the sea is left out\n");
        return;
    }
    // Check it against the roads: if many of them are under water, the
    // coastline is wrong somewhere, and a map with no sea beats one with
    // no land.
    size_t wet = 0, tried = 0;
    for (const Line& l : land_sample) {
        const Pt& p = l[l.size() / 2];
        if (!shell.contains(p)) continue;
        tried++;
        bool in = false;
        for (const Line& r : sea.parts)
            for (size_t i = 0, n = r.size(); i < n; i++) {
                const Pt &a = r[i], &b = r[(i + 1) % n];
                if ((a.y > p.y) == (b.y > p.y)) continue;
                if (a.x + (double(p.y) - a.y) / (double(b.y) - a.y) * (double(b.x) - a.x) > p.x) in = !in;
            }
        wet += in;
    }
    if (tried && wet * 10 > tried) {
        std::fprintf(stderr, "warning: %zu of %zu sampled roads would be under water; the sea is left out\n", wet, tried);
        return;
    }
    std::fprintf(stderr, "sea: %zu stretches of coast, %zu areas and islands, %zu of %zu sampled roads over water\n",
                 pieces.size(), sea.parts.size(), wet, tried);
    if (!sea.parts.empty()) feats.push_back(std::move(sea));
}

// OpenStreetMap XML of the car roads that reach into the region, their
// nodes, and the turn restrictions on them, for Navit's maptool.
void Builder::write_roads(const std::string& path) const {
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) die("can't write " + tmp);
    std::vector<char> keep(roads.size(), 0);
    std::vector<int64_t> nodes;
    for (size_t i = 0; i < roads.size(); i++) {
        size_t w = roads[i].way;
        bool inside = !have_outline;
        for (uint64_t k = way_start[w]; k < way_start[w + 1] && !inside; k++) {
            Pt p;
            inside = node(way_refs[k], p) && outline.contains(p);
        }
        if (!inside) continue;
        keep[i] = 1;
        nodes.insert(nodes.end(), way_refs.begin() + long(way_start[w]), way_refs.begin() + long(way_start[w + 1]));
    }
    std::sort(nodes.begin(), nodes.end());
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    std::fprintf(f, "<?xml version='1.0' encoding='UTF-8'?>\n<osm version=\"0.6\" generator=\"zmap-build\">\n");
    size_t n_nodes = 0, n_ways = 0;
    for (int64_t id : nodes) {
        Pt p;
        if (!node(id, p)) continue;
        std::fprintf(f, "<node id=\"%" PRId64 "\" lat=\"%.7f\" lon=\"%.7f\"/>\n", id, y_to_lat(p.y), x_to_lon(p.x));
        n_nodes++;
    }
    for (size_t i = 0; i < roads.size(); i++) {
        if (!keep[i]) continue;
        size_t w = roads[i].way;
        std::fprintf(f, "<way id=\"%" PRId64 "\">\n", way_id[w]);
        for (uint64_t k = way_start[w]; k < way_start[w + 1]; k++)
            if (std::binary_search(nodes.begin(), nodes.end(), way_refs[k])) {
                Pt p;
                if (node(way_refs[k], p)) std::fprintf(f, "  <nd ref=\"%" PRId64 "\"/>\n", way_refs[k]);
            }
        std::fputs(roads[i].tags.c_str(), f);
        std::fputs("</way>\n", f);
        n_ways++;
    }
    int64_t rid = 0;
    for (const Restriction& r : restrictions) {
        std::fprintf(f, "<relation id=\"%" PRId64 "\">\n%s%s</relation>\n", ++rid, r.members.c_str(), r.tags.c_str());
    }
    std::fputs("</osm>\n", f);
    if (std::fclose(f) != 0 || std::rename(tmp.c_str(), path.c_str()) != 0) die("can't write " + path);
    std::fprintf(stderr, "wrote %s: %zu nodes, %zu roads, %zu turn restrictions\n", path.c_str(), n_nodes, n_ways,
                 restrictions.size());
}

// The search index: one line a place, and one a named road in each town
// it runs through, sorted by name:
//   kind<TAB>name<TAB>town<TAB>lat<TAB>lon
// kind is c city, t town, v village, s suburb or hamlet, n neighbourhood,
// r road. "town" is the nearest city, town, village or suburb (for a
// place, the nearest bigger one), or empty.
void Builder::write_search(const std::string& path) const {
    struct Place {
        Pt p;
        uint8_t kind;
        const char* name;
    };
    std::vector<Place> places;
    for (const Feat& f : feats)
        if (is_place(f.kind) && !f.parts.empty() && !f.parts[0].empty() && f.name &&
            (!have_outline || outline.contains(f.parts[0][0])))
            places.push_back({f.parts[0][0], f.kind, strings.data.c_str() + f.name});
    // A grid of the towns, for "the nearest town".
    double cell = 10000 * units_per_metre(mid_lat);
    std::unordered_map<int64_t, std::vector<size_t>> grid;
    auto key = [&](int64_t cx, int64_t cy) { return cx * 1000003 + cy; };
    for (size_t i = 0; i < places.size(); i++)
        if (places[i].kind >= kSuburb)
            grid[key(int64_t(places[i].p.x / cell), int64_t(places[i].p.y / cell))].push_back(i);
    auto town_of = [&](const Pt& p, uint8_t above) -> const char* {
        int64_t cx = int64_t(p.x / cell), cy = int64_t(p.y / cell);
        double best = 1e300;
        const char* name = "";
        for (int64_t dx = -1; dx <= 1; dx++)
            for (int64_t dy = -1; dy <= 1; dy++) {
                auto it = grid.find(key(cx + dx, cy + dy));
                if (it == grid.end()) continue;
                for (size_t i : it->second) {
                    if (places[i].kind <= above) continue;
                    double d = std::hypot(double(places[i].p.x) - p.x, double(places[i].p.y) - p.y);
                    // A suburb counts only when it is much nearer than the town.
                    if (places[i].kind == kSuburb) d *= 2;
                    if (d < best) best = d, name = places[i].name;
                }
            }
        return name;
    };
    struct Entry {
        std::string sort, line;
    };
    std::vector<Entry> out;
    auto add = [&](char kind, const char* name, const char* town, const Pt& p) {
        if (!*name) return;
        std::string n = name, t = town;
        for (std::string* s : {&n, &t})
            for (char& c : *s)
                if (c == '\t' || c == '\n') c = ' ';
        char pos[48];
        std::snprintf(pos, sizeof pos, "%.6f\t%.6f", y_to_lat(p.y), x_to_lon(p.x));
        std::string low = n;
        for (char& c : low) c = char(std::tolower(uint8_t(c)));
        out.push_back({low + '\x01' + t, std::string(1, kind) + '\t' + n + '\t' + t + '\t' + pos});
    };
    static const char kKindChar[] = {'n', 's', 'v', 't', 'c'};
    for (const Place& pl : places)
        add(kKindChar[pl.kind - kNeighbourhood], pl.name, town_of(pl.p, pl.kind), pl.p);
    // Roads: one entry for each name in each town, at the middle of its longest piece.
    std::unordered_map<std::string, std::pair<size_t, Pt>> streets;
    for (const Feat& f : feats) {
        if (!is_road(f.kind) || !f.name || f.parts.empty() || f.parts[0].size() < 2) continue;
        const Line& l = f.parts[0];
        const Pt& mid = l[l.size() / 2];
        if (have_outline && !outline.contains(mid)) continue;
        const char* name = strings.data.c_str() + f.name;
        std::string k = std::string(name) + '\t' + town_of(mid, 0);
        auto it = streets.find(k);
        if (it == streets.end() || it->second.first < l.size()) streets[k] = {l.size(), mid};
    }
    for (const auto& kv : streets) {
        size_t tab = kv.first.find('\t');
        add('r', kv.first.substr(0, tab).c_str(), kv.first.c_str() + tab + 1, kv.second.second);
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.sort < b.sort; });
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) die("can't write " + tmp);
    std::fputs("#zeroos-maps-search 1\n", f);
    for (const Entry& e : out) std::fprintf(f, "%s\n", e.line.c_str());
    if (std::fclose(f) != 0 || std::rename(tmp.c_str(), path.c_str()) != 0) die("can't write " + path);
    std::fprintf(stderr, "wrote %s: %zu places and streets\n", path.c_str(), out.size());
}

// The house numbers (../addr.h): the streets sorted by key, and each
// street's addresses together. Only those inside the outline.
void Builder::write_addresses(const std::string& path) const {
    std::vector<uint32_t> order(addr_streets.size());
    for (uint32_t i = 0; i < order.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return addr_streets[a].first < addr_streets[b].first; });
    std::vector<std::vector<const AddrIn*>> by_street(addr_streets.size());
    size_t kept = 0;
    for (const AddrIn& a : addresses)
        if (!have_outline || outline.contains(a.at)) by_street[a.street].push_back(&a), kept++;
    Strings names = addr_strings;   // the numbers' strings, then the streets'
    std::vector<AddrStreet> streets;
    std::vector<Addr> out;
    for (uint32_t id : order) {
        if (by_street[id].empty()) continue;
        AddrStreet st{names.add(addr_streets[id].first, 200), names.add(addr_streets[id].second, 200), uint32_t(out.size()),
                      uint32_t(by_street[id].size())};
        for (const AddrIn* a : by_street[id])
            out.push_back({int32_t(std::lround(y_to_lat(a->at.y) * 1e6)), int32_t(std::lround(x_to_lon(a->at.x) * 1e6)), a->number,
                           a->city});
        streets.push_back(st);
    }
    AddrHeader h{};
    std::memcpy(h.magic, kAddrMagic, sizeof kAddrMagic);
    h.nstreets = uint32_t(streets.size());
    h.naddrs = uint32_t(out.size());
    h.streets_off = sizeof h;
    h.addrs_off = h.streets_off + streets.size() * sizeof(AddrStreet);
    h.strings_off = h.addrs_off + out.size() * sizeof(Addr);
    h.strings_size = names.data.size();
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    bool ok = f && std::fwrite(&h, sizeof h, 1, f) == 1 &&
              (streets.empty() || std::fwrite(streets.data(), sizeof(AddrStreet), streets.size(), f) == streets.size()) &&
              (out.empty() || std::fwrite(out.data(), sizeof(Addr), out.size(), f) == out.size()) &&
              std::fwrite(names.data.data(), 1, names.data.size(), f) == names.data.size();
    if (!f || std::fclose(f) != 0 || !ok || std::rename(tmp.c_str(), path.c_str()) != 0) die("can't write " + path);
    std::fprintf(stderr, "wrote %s: %zu house numbers on %zu streets\n", path.c_str(), kept, streets.size());
}

// ---- Writing -------------------------------------------------------------

struct TileFeat {
    uint8_t kind, speed;
    uint32_t name, ref;
    std::vector<std::vector<int16_t>> parts;   // x, y, x, y...
};

struct LevelOut {
    int zoom;
    int32_t tx0, ty0, ntx, nty;
    std::vector<std::vector<TileFeat>> tiles;
};

// Hand a feature's geometry to every tile it touches, halving the tile
// range until one tile is left.
void spread(const Feat& f, std::vector<Line>& parts, LevelOut& lv, int32_t x0, int32_t x1, int32_t y0, int32_t y1) {
    if (parts.empty()) return;
    int shift = kWorldBits - lv.zoom;
    if (x1 - x0 == 1 && y1 - y0 == 1) {
        int32_t ox = (lv.tx0 + x0) << shift, oy = (lv.ty0 + y0) << shift;
        int down = shift - kTileBits;
        std::vector<TileFeat>& tile = lv.tiles[size_t(y0) * size_t(lv.ntx) + size_t(x0)];
        TileFeat* into = nullptr;
        if (is_line(f.kind))   // runs of one road in a tile share a feature
            for (auto it = tile.rbegin(); it != tile.rend() && it - tile.rbegin() < 64; ++it)
                if (it->kind == f.kind && it->name == f.name && it->ref == f.ref && it->speed == f.speed) {
                    into = &*it;
                    break;
                }
        TileFeat fresh{f.kind, f.speed, f.name, f.ref, {}};
        if (!into) into = &fresh;
        size_t min_pts = is_area(f.kind) ? 3 : is_line(f.kind) ? 2 : 1;
        for (const Line& l : parts) {
            std::vector<int16_t> q;
            for (const Pt& p : l) {
                int16_t x = int16_t(std::clamp((p.x - ox) >> down, 0, kTileExtent));
                int16_t y = int16_t(std::clamp((p.y - oy) >> down, 0, kTileExtent));
                if (q.size() >= 2 && q[q.size() - 2] == x && q.back() == y) continue;
                q.push_back(x);
                q.push_back(y);
            }
            if (is_area(f.kind) && q.size() >= 4 && q[0] == q[q.size() - 2] && q[1] == q.back()) q.resize(q.size() - 2);
            if (q.size() / 2 >= min_pts) into->parts.push_back(std::move(q));
        }
        if (into == &fresh && !fresh.parts.empty()) tile.push_back(std::move(fresh));
        return;
    }
    bool split_x = x1 - x0 >= y1 - y0;
    int32_t mid = split_x ? (x0 + x1) / 2 : (y0 + y1) / 2;
    int32_t bound = ((split_x ? lv.tx0 : lv.ty0) + mid) << shift;
    std::vector<Line> low, high;
    auto clip = is_area(f.kind) ? clip_rings : clip_lines;
    if (is_place(f.kind)) {
        int32_t v = split_x ? parts[0][0].x : parts[0][0].y;
        (v < bound ? low : high) = parts;
    } else {
        clip(parts, split_x ? 0 : 1, bound, true, low);
        clip(parts, split_x ? 0 : 1, bound, false, high);
    }
    parts.clear();
    parts.shrink_to_fit();
    if (split_x) {
        spread(f, low, lv, x0, mid, y0, y1);
        spread(f, high, lv, mid, x1, y0, y1);
    } else {
        spread(f, low, lv, x0, x1, y0, mid);
        spread(f, high, lv, x0, x1, mid, y1);
    }
}

void Builder::write(const std::string& path, const std::string& name, const std::string& date, bool imperial) {
    static const int kZooms[] = {7, 9, 11, 13};
    double upm = units_per_metre(mid_lat);
    std::vector<LevelOut> levels;
    for (int zoom : kZooms) {
        LevelOut lv;
        lv.zoom = zoom;
        int shift = kWorldBits - zoom;
        lv.tx0 = outline.min_x >> shift, lv.ty0 = outline.min_y >> shift;
        lv.ntx = (outline.max_x >> shift) - lv.tx0 + 1, lv.nty = (outline.max_y >> shift) - lv.ty0 + 1;
        lv.tiles.resize(size_t(lv.ntx) * size_t(lv.nty));
        // The finest level keeps everything to 0.7 m. The others are drawn at
        // 5 * 2^(11 - zoom) metres a pixel and up, and keep half a pixel of that.
        double tol_m = zoom >= 13 ? 0.7 : 2.5 * std::pow(2.0, 11 - zoom);
        double tol = tol_m * upm, min_area = zoom >= 13 ? 0 : 36 * tol * tol;
        int32_t gx0 = lv.tx0 << shift, gy0 = lv.ty0 << shift;
        int32_t gx1 = (lv.tx0 + lv.ntx) << shift, gy1 = (lv.ty0 + lv.nty) << shift;
        size_t kept = 0;
        for (const Feat& f : feats) {
            if (!level_has(zoom, f.kind) || f.parts.empty()) continue;
            std::vector<Line> parts;
            int32_t bx0 = INT32_MAX, by0 = INT32_MAX, bx1 = INT32_MIN, by1 = INT32_MIN;
            for (const Line& l : f.parts) {
                Line s;
                // Woods, marshes and streams are the bulk of a map and the least
                // looked at: they get twice the tolerance, and small ones go sooner.
                bool soft = f.kind == kForest || f.kind == kWetland || f.kind == kPark || f.kind == kSand || f.kind == kStream;
                double ftol = soft ? 2 * tol : tol;
                if (is_area(f.kind)) {
                    if (min_area > 0 && std::fabs(ring_area(l)) < (soft ? 4 : 1) * min_area) continue;
                    simplify_ring(l, ftol, s);
                    if (s.size() < 3) continue;
                } else if (is_line(f.kind)) {
                    simplify(l, ftol, s);
                    if (s.size() == 2 && zoom < 13) {   // a stub too short to see
                        double dx = double(s[0].x) - s[1].x, dy = double(s[0].y) - s[1].y;
                        if (dx * dx + dy * dy < 4 * tol * tol) continue;
                    }
                } else {
                    s = l;
                }
                for (const Pt& p : s) {
                    bx0 = std::min(bx0, p.x), bx1 = std::max(bx1, p.x);
                    by0 = std::min(by0, p.y), by1 = std::max(by1, p.y);
                }
                parts.push_back(std::move(s));
            }
            if (parts.empty() || bx1 < gx0 || bx0 >= gx1 || by1 < gy0 || by0 >= gy1) continue;
            // Trim to the grid, then to the tiles the feature's box touches.
            if (!is_place(f.kind) && (bx0 < gx0 || bx1 > gx1 || by0 < gy0 || by1 > gy1)) {
                auto clip = is_area(f.kind) ? clip_rings : clip_lines;
                std::vector<Line> t;
                clip(parts, 0, gx0, false, t);
                clip(t, 0, gx1, true, parts);
                clip(parts, 1, gy0, false, t);
                clip(t, 1, gy1, true, parts);
                if (parts.empty()) continue;
                bx0 = std::max(bx0, gx0), bx1 = std::min(bx1, gx1), by0 = std::max(by0, gy0), by1 = std::min(by1, gy1);
            }
            int32_t tx0 = std::clamp((bx0 >> shift) - lv.tx0, 0, lv.ntx - 1), tx1 = std::clamp((bx1 >> shift) - lv.tx0, 0, lv.ntx - 1);
            int32_t ty0 = std::clamp((by0 >> shift) - lv.ty0, 0, lv.nty - 1), ty1 = std::clamp((by1 >> shift) - lv.ty0, 0, lv.nty - 1);
            spread(f, parts, lv, tx0, tx1 + 1, ty0, ty1 + 1);
            kept++;
        }
        std::fprintf(stderr, "zoom %d: %d x %d tiles, %zu features\n", zoom, lv.ntx, lv.nty, kept);
        levels.push_back(std::move(lv));
    }

    // Lay the file out: header, levels, indexes, tiles, strings.
    std::vector<uint8_t> out(sizeof(Header) + levels.size() * sizeof(Level));
    std::vector<uint64_t> index_at;
    for (const LevelOut& lv : levels) {
        index_at.push_back(out.size());
        out.resize(out.size() + lv.tiles.size() * sizeof(TileRef));
    }
    auto put = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        out.insert(out.end(), b, b + n);
    };
    for (size_t li = 0; li < levels.size(); li++) {
        LevelOut& lv = levels[li];
        for (size_t ti = 0; ti < lv.tiles.size(); ti++) {
            std::vector<TileFeat>& tile = lv.tiles[ti];
            if (tile.empty()) continue;
            std::stable_sort(tile.begin(), tile.end(), [](const TileFeat& a, const TileFeat& b) { return a.kind < b.kind; });
            TileRef ref{out.size(), 0, 0};
            for (const TileFeat& f : tile) {
                for (size_t first = 0; first < f.parts.size(); first += 65535) {   // nparts is 16 bits
                    size_t n = std::min<size_t>(65535, f.parts.size() - first);
                    FeatureHead h{f.kind, f.speed, uint16_t(n), f.name, f.ref};
                    put(&h, sizeof h);
                    for (size_t i = 0; i < n; i++) {
                        uint32_t c = uint32_t(f.parts[first + i].size() / 2);
                        put(&c, 4);
                    }
                    for (size_t i = 0; i < n; i++) put(f.parts[first + i].data(), f.parts[first + i].size() * 2);
                    ref.count++;
                }
            }
            ref.size = uint32_t(out.size() - ref.off);
            std::memcpy(out.data() + index_at[li] + ti * sizeof(TileRef), &ref, sizeof ref);
        }
    }
    Header head{};
    std::memcpy(head.magic, kMagic, sizeof kMagic);
    head.version = kVersion;
    head.nlevels = uint32_t(levels.size());
    head.min_x = outline.min_x, head.min_y = outline.min_y, head.max_x = outline.max_x, head.max_y = outline.max_y;
    head.flags = imperial ? kFlagImperial : 0;
    head.strings_off = out.size();
    head.strings_size = strings.data.size();
    std::snprintf(head.name, sizeof head.name, "%s", name.c_str());
    std::snprintf(head.date, sizeof head.date, "%s", date.c_str());
    put(strings.data.data(), strings.data.size());
    std::memcpy(out.data(), &head, sizeof head);
    for (size_t li = 0; li < levels.size(); li++) {
        const LevelOut& lv = levels[li];
        Level l{uint32_t(lv.zoom), uint32_t(lv.tx0), uint32_t(lv.ty0), uint32_t(lv.ntx), uint32_t(lv.nty), 0, index_at[li]};
        std::memcpy(out.data() + sizeof(Header) + li * sizeof(Level), &l, sizeof l);
    }
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f || std::fwrite(out.data(), 1, out.size(), f) != out.size() || std::fclose(f) != 0) die("can't write " + tmp);
    if (std::rename(tmp.c_str(), path.c_str()) != 0) die("can't write " + path);
    std::fprintf(stderr, "wrote %s: %.1f MB, names %.1f MB\n", path.c_str(), out.size() / 1e6, strings.data.size() / 1e6);
}

// An Osmosis .poly file: a name, then rings ("!name" = a hole) of "lon lat"
// lines, each closed by END, and a last END.
void read_poly(const std::string& path, Outline& o) {
    std::ifstream in(path);
    if (!in) die("can't open " + path);
    std::string line;
    std::getline(in, line);   // the file's name line
    Line ring;
    bool in_ring = false, hole = false;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string a;
        if (!(ss >> a)) continue;
        if (!in_ring) {
            if (a == "END") break;
            in_ring = true;
            hole = a[0] == '!';
            ring.clear();
            continue;
        }
        if (a == "END") {
            if (ring.size() > 1 && ring.front() == ring.back()) ring.pop_back();
            if (ring.size() >= 3) (hole ? o.holes : o.rings).push_back(ring);
            in_ring = false;
            continue;
        }
        double lon = std::atof(a.c_str()), lat = 0;
        if (!(ss >> lat)) die("bad line in " + path + ": " + line);
        ring.push_back({int32_t(std::lround(lon_to_x(lon))), int32_t(std::lround(lat_to_y(lat)))});
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string name, poly, date, in, out, search, roads_out, addresses;
    bool imperial = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) die(a + " needs a value");
            return argv[++i];
        };
        if (a == "--name") name = value();
        else if (a == "--poly") poly = value();
        else if (a == "--date") date = value();
        else if (a == "--imperial") imperial = true;
        else if (a == "--search") search = value();
        else if (a == "--roads") roads_out = value();
        else if (a == "--addresses") addresses = value();
        else if (a.rfind("--", 0) == 0) die("unknown option " + a);
        else if (in.empty()) in = a;
        else if (out.empty()) out = a;
        else die("too many arguments");
    }
    if (in.empty() || out.empty() || name.empty()) {
        std::fprintf(stderr, "usage: zmap-build --name NAME [--poly FILE.poly] [--imperial] [--date YYYY-MM-DD]\n"
                             "                  [--search OUT.tsv] [--addresses OUT.addr] [--roads OUT.osm] IN.osm.pbf OUT.zmap\n");
        return 2;
    }
    Builder b;
    b.want_roads = !roads_out.empty();
    b.want_addresses = !addresses.empty();
    try {
        b.read(in);
    } catch (const std::exception& e) {
        die(e.what());
    }
    std::fprintf(stderr, "read %zu nodes, %zu ways, %zu areas from relations\n", b.node_id.size(), b.way_id.size(), b.multis.size());
    if (!poly.empty()) {
        read_poly(poly, b.outline);
        b.have_outline = true;
    } else {   // the box around the data
        int32_t x0 = INT32_MAX, y0 = INT32_MAX, x1 = INT32_MIN, y1 = INT32_MIN;
        for (const Pt& p : b.node_pt) x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
        b.outline.rings.push_back({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}});
    }
    b.outline.finish();
    b.mid_lat = y_to_lat((double(b.outline.min_y) + b.outline.max_y) / 2);
    b.make_features();
    if (!roads_out.empty()) b.write_roads(roads_out);
    if (!search.empty()) b.write_search(search);
    if (!addresses.empty()) b.write_addresses(addresses);
    std::vector<Line> roads;
    for (size_t i = 0; i < b.feats.size(); i += 97)
        if (is_road(b.feats[i].kind)) roads.push_back(b.feats[i].parts[0]);
    b.make_sea(roads);
    Feat cover;
    cover.kind = kCoverage;
    cover.parts = b.outline.rings;
    cover.parts.insert(cover.parts.end(), b.outline.holes.begin(), b.outline.holes.end());
    b.feats.push_back(std::move(cover));
    if (date.empty()) {
        std::time_t t = b.timestamp ? std::time_t(b.timestamp) : std::time(nullptr);
        char buf[16];
        std::strftime(buf, sizeof buf, "%Y-%m-%d", std::gmtime(&t));
        date = buf;
    }
    b.write(out, name, date, imperial);
    return 0;
}
