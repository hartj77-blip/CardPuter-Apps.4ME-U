// Maps: the map file reader and the renderer, against the made-up town
// (make-town.py). maps_check.sh builds the town's map with zmap-build and
// runs this on it:  maps_test town.zmap
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "apps/maps/render.h"
#include "apps/maps/zmap.h"

using namespace zmap;

static int fails = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                                    \
        }                                                               \
    } while (0)

// The town's origin (make-town.py), and a point some metres east and north of it.
const double kLat0 = 51.500729, kLon0 = -0.124625;
static void at(double east, double north, double& x, double& y) {
    double m_lat = 111320.0, m_lon = m_lat * std::cos(kLat0 * kPi / 180);
    x = lon_to_x(kLon0 + east / m_lon);
    y = lat_to_y(kLat0 + north / m_lat);
}

static Renderer renderer;
static std::vector<uint16_t> pixels;
static std::vector<Label> labels;

static View view_at(double east, double north, double mpp, double up = 0) {
    View v;
    at(east, north, v.cx, v.cy);
    v.mpp = mpp, v.up = up;
    return v;
}
// The colour drawn at a place, at a scale.
static uint16_t colour(const Map& map, double east, double north, double mpp) {
    View v = view_at(east, north, mpp);
    pixels.assign(size_t(v.w) * v.h, 0);
    renderer.draw(map, v, day_theme(), pixels.data(), labels);
    return pixels[size_t(v.py) * v.w + v.px];
}
static bool has_label(const char* text, bool shield) {
    for (const Label& l : labels)
        if (std::string(l.text) == text && l.shield == shield) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: maps_test town.zmap\n");
        return 2;
    }
    const Theme& t = day_theme();
    Map map;
    CHECK(map.open(argv[1]));
    if (!map.ok()) {
        std::printf("FAIL %s: %s\n", argv[1], map.error().c_str());
        return 1;
    }
    CHECK(map.name() == "Zeroville");
    CHECK(map.date() == "2026-01-01");
    CHECK(map.imperial());
    CHECK(map.levels() == 4);
    double x, y;
    at(0, 0, x, y);
    CHECK(map.contains(x, y));
    at(5000, 0, x, y);
    CHECK(!map.contains(x, y));

    // The level for a scale, and which roads show at it.
    CHECK(Renderer::level_for(map, 2) == 3);
    CHECK(Renderer::level_for(map, 10) == 2);
    CHECK(Renderer::level_for(map, 30) == 1);
    CHECK(Renderer::level_for(map, 500) == 0);
    CHECK(Renderer::road_width(kMotorway, 2000) > 0);
    CHECK(Renderer::road_width(kPrimary, 2000) == 0);
    CHECK(Renderer::road_width(kResidential, 50) == 0);
    CHECK(Renderer::road_width(kResidential, 2) > Renderer::road_width(kResidential, 10));
    CHECK(Renderer::road_width(kWater, 2) == 0);

    // What is where, at every level that holds it.
    for (double mpp : {2.0, 8.0}) {
        CHECK(colour(map, 0, 0, mpp) == rgb565(t.road[kPrimary - kService]));   // High Street
        CHECK(colour(map, 540, -20, mpp) == rgb565(t.water));                   // the lake
        CHECK(colour(map, 630, -80, mpp) == rgb565(t.land));                    // its island
        CHECK(colour(map, 0, -600, mpp) == rgb565(t.sea));
        CHECK(colour(map, 160, -605, mpp) == rgb565(t.land));                   // the island at sea
        CHECK(colour(map, 115, 140, mpp) == rgb565(t.park));
        CHECK(colour(map, -375, -115, mpp) == rgb565(t.sand));
        CHECK(colour(map, 700, 600, mpp) == rgb565(t.forest));
        CHECK(colour(map, -900, 100, mpp) == rgb565(t.land));
        CHECK(colour(map, 0, -800, mpp) == rgb565(t.outside));                  // south of the region
        CHECK(colour(map, 1200, 0, mpp) == rgb565(t.outside));
    }
    // (A pixel is 30 or 80 m here, so these keep away from edges.)
    for (double mpp : {30.0, 80.0}) {
        CHECK(colour(map, 540, -60, mpp) == rgb565(t.water));
        CHECK(colour(map, -700, -600, mpp) == rgb565(t.sea));
        CHECK(colour(map, -900, 100, mpp) == rgb565(t.land));
        CHECK(colour(map, 0, -900, mpp) == rgb565(t.outside));
    }
    // What each level holds: the park (one hectare) from the third level, side streets
    // from the third, service roads only in the finest (and no driveways).
    {
        int have[4][64] = {};
        for (int li = 0; li < 4; li++) {
            const Level& L = map.level(li);
            for (uint32_t ty = L.ty0; ty < L.ty0 + L.nty; ty++)
                for (uint32_t tx = L.tx0; tx < L.tx0 + L.ntx; tx++) {
                    const uint8_t* data;
                    uint32_t size, count;
                    if (!map.tile(li, tx, ty, data, size, count)) continue;
                    TileWalk walk(data, size, count);
                    Feature f;
                    while (walk.next(f)) have[li][f.head.kind & 63] += f.head.nparts;
                }
        }
        CHECK(map.level(0).zoom == 7 && map.level(3).zoom == 13);
        CHECK(!have[1][kPark] && have[2][kPark] && have[3][kPark]);
        CHECK(have[1][kForest]);   // the wood is bigger: it is kept a level sooner
        CHECK(!have[1][kResidential] && have[2][kResidential]);
        CHECK(!have[2][kService] && have[3][kService] == 1);
        CHECK(have[0][kMotorway] && have[0][kPrimary] && !have[0][kSecondary] && have[1][kSecondary]);
        CHECK(have[0][kSea] && have[0][kWater] && have[0][kCoverage] && have[0][kTown]);
        CHECK(!have[1][kSuburb] && have[2][kSuburb]);
        CHECK(have[1][kRail] && have[1][kRiver] && !have[1][kSand] && have[2][kSand]);
    }
    // Far off the map there is nothing, and nothing breaks.
    CHECK(colour(map, 400000, 300000, 2) == rgb565(t.outside));
    CHECK(colour(map, 400000, 300000, 2000) == rgb565(t.outside));

    // Turned to the road's heading, High Street runs straight up the screen.
    {
        View v = view_at(0, 0, 3, 64);
        renderer.draw(map, v, t, pixels.data(), labels);
        uint16_t road = rgb565(t.road[kPrimary - kService]);
        CHECK(pixels[size_t(8) * v.w + 160] == road);
        CHECK(pixels[size_t(128) * v.w + 160] == road);
        CHECK(pixels[size_t(68) * v.w + 100] != road);
        CHECK(has_label("Zeroville", false));
        CHECK(has_label("A 1", true));          // a main road shows its number
        CHECK(!has_label("High Street", false));
        CHECK(has_label("Station Road", false));
        // No two names overlap, and all are on screen.
        for (size_t i = 0; i < labels.size(); i++) {
            CHECK(labels[i].x > 0 && labels[i].x < v.w && labels[i].y > 0 && labels[i].y < v.h);
            for (size_t j = i + 1; j < labels.size(); j++)
                CHECK(std::abs(labels[i].x - labels[j].x) > 12 || std::abs(labels[i].y - labels[j].y) > 12);
        }
        // Names keep out of the way of the app's own boxes.
        renderer.draw(map, v, t, pixels.data(), labels, {{0, 0, v.w, v.h}});
        CHECK(labels.empty());
        // Screen and world positions agree.
        double sx, sy, wx, wy;
        Renderer::to_world(v, 200, 30, wx, wy);
        Renderer::to_screen(v, wx, wy, sx, sy);
        CHECK(std::fabs(sx - 200) < 0.01 && std::fabs(sy - 30) < 0.01);
        Renderer::to_world(v, 160, 0, wx, wy);   // straight up the screen is 64 degrees
        CHECK(std::fabs(std::atan2(wx - v.cx, -(wy - v.cy)) * 180 / kPi - 64) < 0.5);
        // The marker and the compass draw on the map.
        renderer.marker(v, t, pixels.data(), 160, 68, 0, true);
        CHECK(pixels[size_t(68) * v.w + 160] == rgb565(t.marker));
        renderer.marker(v, t, pixels.data(), 40, 40, 0, false);
        CHECK(pixels[size_t(40) * v.w + 40] == rgb565(t.marker));
        renderer.compass(v, t, pixels.data(), 300, 14);
        CHECK(pixels[size_t(14) * v.w + 292] == rgb565(t.halo));
    }

    // The road you are on, and its limit.
    {
        at(0, 0, x, y);
        RoadHit h = nearest_road(map, x, y, -1, 30);
        CHECK(h.found && std::string(h.name) == "High Street" && std::string(h.ref) == "A 1");
        CHECK(h.speed == (30 | 0x80) && h.kind == kPrimary && h.metres < 2);
        at(-230, 75, x, y);
        h = nearest_road(map, x, y, -1, 20);
        CHECK(h.found && std::string(h.name) == "Station Road" && h.speed == 50);
        at(0, -650, x, y);
        CHECK(!nearest_road(map, x, y, -1, 30).found);
        // At a junction the heading decides: on Church Street, 7 m from High Street.
        at(87, 50, x, y);
        CHECK(std::string(nearest_road(map, x, y, 64, 30).name) == "High Street");
        CHECK(std::string(nearest_road(map, x, y, 244, 30).name) == "High Street");
        CHECK(std::string(nearest_road(map, x, y, 0, 30).name) == "Church Street");
        CHECK(std::string(nearest_road(map, x, y, -1, 30).name) == "Church Street");
        at(0, 0, x, y);
        double x2, y2;
        at(300, 400, x2, y2);
        CHECK(std::fabs(metres_between(x, y, x2, y2) - 500) < 2);
    }
    CHECK(std::fabs(y_to_lat(lat_to_y(40.5)) - 40.5) < 1e-6 && std::fabs(x_to_lon(lon_to_x(-74.5)) + 74.5) < 1e-6);

    // Files that aren't maps, or are damaged ones, are refused or drawn as
    // far as they go; they never crash the app.
    {
        std::ifstream in(argv[1], std::ios::binary);
        std::vector<char> good((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::string tmp = std::string(argv[1]) + ".damaged";
        auto try_file = [&](const std::vector<char>& bytes) {
            std::ofstream(tmp, std::ios::binary).write(bytes.data(), std::streamsize(bytes.size()));
            Map m;
            if (!m.open(tmp)) return false;
            for (double mpp : {1.0, 8.0, 30.0, 400.0}) {
                View v = view_at(0, 0, mpp, 33);
                renderer.draw(m, v, t, pixels.data(), labels);
                for (const Label& l : labels) CHECK(std::char_traits<char>::length(l.text) < 200);
            }
            double px, py;
            at(0, 0, px, py);
            nearest_road(m, px, py, 10, 50);
            return true;
        };
        CHECK(!try_file({}));
        CHECK(!try_file(std::vector<char>(4096, 'x')));
        for (size_t n = 1; n < good.size(); n += 61) try_file(std::vector<char>(good.begin(), good.begin() + long(n)));
        uint32_t seed = 12345;
        for (int round = 0; round < 400; round++) {
            std::vector<char> bad = good;
            for (int k = 0; k < 1 + round % 6; k++) {
                seed = seed * 1664525u + 1013904223u;
                size_t where = (seed >> 8) % bad.size();
                seed = seed * 1664525u + 1013904223u;
                bad[where] = char(seed >> 16);
            }
            try_file(bad);
        }
        std::remove(tmp.c_str());
        Map none;
        CHECK(!none.open("/nonexistent/map.zmap"));
        View v = view_at(0, 0, 3);
        renderer.draw(none, v, t, pixels.data(), labels);   // no map: a blank screen
        CHECK(pixels[0] == rgb565(t.outside));
    }

    if (fails) {
        std::printf("%d checks failed\n", fails);
        return 1;
    }
    std::printf("ok   maps: reader, renderer, road lookup, damaged files\n");
    return 0;
}
