// Maps: reading Navit's answers (nav_parse.h), the search (search.h) and
// the stand-in engine (nav.h), with no Navit.
//   nav_test town.search        (maps_check.sh builds it)
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "apps/maps/nav.h"
#include "apps/maps/nav_parse.h"
#include "apps/maps/search.h"

using namespace zmap;
using namespace zmap::nav;

static int fails = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                                    \
        }                                                               \
    } while (0)
#define CHECK_EQ(a, b)                                                                                   \
    do {                                                                                                 \
        std::string x_ = (a), y_ = (b);                                                                  \
        if (x_ != y_) {                                                                                  \
            std::printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, x_.c_str(), y_.c_str()); \
            fails++;                                                                                     \
        }                                                                                                \
    } while (0)

int main(int argc, char** argv) {
    // ---- Navit's words, as shown --------------------------------------
    CHECK_EQ(clean_instruction("Turn right in 500 meters into Bridge Street "), "Turn right into Bridge Street");
    CHECK_EQ(clean_instruction("Keep right in 8 kilometers into the NJ 139 East towards Holland Tunnel"),
             "Keep right into the NJ 139 East towards Holland Tunnel");
    CHECK_EQ(clean_instruction("Take the exit on your left in 2.1 kilometers "), "Take the exit on your left");
    CHECK_EQ(clean_instruction("Turn right in one mile into North Harrison Street"), "Turn right into North Harrison Street");
    CHECK_EQ(clean_instruction("Turn left in half a mile into Main Street"), "Turn left into Main Street");
    CHECK_EQ(clean_instruction("Turn left in a quarter of a mile"), "Turn left");
    CHECK_EQ(clean_instruction("Turn right in 0 miles into Bridge Street"), "Turn right into Bridge Street");
    CHECK_EQ(clean_instruction("Turn right in 350 feet"), "Turn right");
    CHECK_EQ(clean_instruction("Merge in 600 meters onto Tonnelle Avenue|left "), "Merge onto Tonnelle Avenue");
    CHECK_EQ(clean_instruction("Keep left in 40 meters into the US 1;US 9 "), "Keep left into the US 1/US 9");
    CHECK_EQ(clean_instruction("Turn right soon"), "Turn right");
    CHECK_EQ(clean_instruction("Drive in Inner Lane"), "Drive in Inner Lane");   // not a distance
    CHECK_EQ(clean_instruction("You have reached your destination in 200 meters"), "You have reached your destination");
    CHECK(turn_kind("Turn right into Bridge Street") == kRight);
    CHECK(turn_kind("Turn left") == kLeft);
    CHECK(turn_kind("Turn sharp left into X") == kSharpLeft);
    CHECK(turn_kind("Turn strongly right into Church Street") == kSharpRight);
    CHECK(turn_kind("Keep right into Raymond Road") == kSlightRight);
    CHECK(turn_kind("Take the exit on your left") == kExitLeft);
    CHECK(turn_kind("Take the second exit at the roundabout") == kRoundabout);
    CHECK(turn_kind("When possible, please turn around") == kUturn);
    CHECK(turn_kind("You have reached your destination") == kArrive);
    CHECK(turn_kind("Continue straight") == kStraight);

    // ---- The route's line (map.dump) ----------------------------------
    {
        std::string dump =
            "mg:0xff815359 0x4af08a type=route_start\n"
            "type=street_route street_item='type=0x80000009 id=0x560,0x1a33' direction='1' length='127'\n"
            "mg:0xff815359 0x4af08a\n"
            "mg:0xff815322 0x4af128\n"
            "type=street_route street_item='x'\n"
            "mg:0xff815322 0x4af128\n"
            "mg:0xff815301 0x4af188\n"
            "type=something_else\n"
            "mg:0x00000000 0x00000000\n";
        std::vector<LatLon> l = parse_route_dump(dump);
        CHECK(l.size() == 3);   // the shared point once; nothing from other items
        if (l.size() == 3) {
            // Worked out by hand: lon = x / R in degrees, lat = 2 atan(e^(y/R)) - 90 degrees, R = 6371000.
            CHECK(std::fabs(l[0].lon - -74.659297) < 1e-5 && std::fabs(l[0].lat - 40.348694) < 1e-5);
            CHECK(l[2].lat > l[0].lat);
        }
        CHECK(parse_route_dump("").empty());
        CHECK(parse_route_dump("type=street_route\nmg:zz yy\nmg:0x1\n").empty());
        LatLon p = from_mg(0, 0);
        CHECK(std::fabs(p.lat) < 1e-9 && std::fabs(p.lon) < 1e-9);
    }

    // ---- The turns (export_as_geojson, with Navit's trailing commas) --
    {
        std::string json = R"({
  "type": "FeatureCollection",
  "features": [
    { "type": "Feature", "properties": { "name": "Navit route export" },
      "geometry": { "type": "LineString", "coordinates": [ [ -74.1, 40.7 ], [ -74.0, 40.8 ], ] } },
    { "type": "Feature", "properties": { "Instruction": "Turn right in 500 meters into Bridge Street ", "name": "" }, "geometry": { "type": "Point", "coordinates": [ -74.16976, 40.74450 ] } },
    { "type": "Feature", "properties": { "Instruction": "Say \"hi\" in 20 meters", "name": "" }, "geometry": { "type": "Point", "coordinates": [ -74.1, 40.75 ] } },
    { "type": "Feature", "properties": { "Instruction": "You have reached your destination in 200 meters", "name": "" }, "geometry": { "type": "Point", "coordinates": [ -74.02778, 40.73565 ] } }  ]
 })";
        std::vector<Maneuver> t = parse_route_geojson(json);
        CHECK(t.size() == 3);
        if (t.size() == 3) {
            CHECK_EQ(t[0].text, "Turn right into Bridge Street");
            CHECK(t[0].kind == kRight);
            CHECK(std::fabs(t[0].at.lat - 40.7445) < 1e-6 && std::fabs(t[0].at.lon - -74.16976) < 1e-6);
            CHECK_EQ(t[1].text, "Say \"hi\"");
            CHECK(t[2].kind == kArrive);
        }
        CHECK(parse_route_geojson("{}").empty());
        CHECK(parse_route_geojson("\"Instruction\": \"x\"").empty());
    }

    // ---- What goes to Navit -------------------------------------------
    {
        // Checked against Python's NMEA checksum of the same sentences.
        std::string n = nmea(40.7347, -74.1645, 0, 0, "170527", "271126");
        CHECK_EQ(n,
                 "$GPRMC,170527.00,A,4044.0820,N,07409.8700,W,0.0,0.0,271126,,,A*40\r\n"
                 "$GPGGA,170527.00,4044.0820,N,07409.8700,W,1,08,1.0,10.0,M,0.0,M,,*7B\r\n");
        std::string s = nmea(-33.5, 151.25, 10, -90, "000000", "010126");
        CHECK(s.find(",3330.0000,S,15115.0000,E,19.4,270.0,") != std::string::npos);
        CHECK(nmea(40.99999999, 0, 0, 0, "0", "0").find("4100.0000,N") != std::string::npos);   // never 60.0000
        CHECK(dbus_number("method return time=1 sender=:1.0\n   string \"33\"\n", -1) == 33);
        CHECK(dbus_number("   string \"(no text[none])\"\n", -1) == -1);
        CHECK(dbus_number("", 7) == 7);
        std::string xml = "<config><!-- <vehicleprofile name=\"car\">old</vehicleprofile> -->"
                          "<vehicleprofile name=\"bike\"/><vehicleprofile name=\"car\" x=\"1\"><r/></vehicleprofile></config>";
        CHECK_EQ(xml_element(xml, "<vehicleprofile name=\"car\"", "</vehicleprofile>"),
                 "<vehicleprofile name=\"car\" x=\"1\"><r/></vehicleprofile>");
        CHECK_EQ(xml_element(xml, "<navigation>", "</navigation>"), "");
    }

    // ---- Where you are on a route -------------------------------------
    {
        // East 1 km, then north 1 km.
        LatLon a{40.0, -74.0};
        double dlon = 1000 / (111194.9 * std::cos(40.0 * 3.14159265358979 / 180)), dlat = 1000 / 111194.9;
        std::vector<LatLon> line{a, {40.0, -74.0 + dlon}, {40.0 + dlat, -74.0 + dlon}};
        std::vector<double> cum = cumulative(line);
        CHECK(std::fabs(cum[1] - 1000) < 2 && std::fabs(cum[2] - 2000) < 4);
        Progress p = locate(line, cum, {40.0 + 20 / 111194.9, -74.0 + dlon / 2}, 0);   // 20 m north of the middle of the first leg
        CHECK(p.seg == 0 && std::fabs(p.along_m - 500) < 3 && std::fabs(p.off_m - 20) < 2);
        p = locate(line, cum, {40.0 + dlat / 4, -74.0 + dlon}, 0);
        CHECK(p.seg == 1 && std::fabs(p.along_m - 1250) < 4 && p.off_m < 1);
        auto r = make_route(line, {{line[1], "Turn left", kLeft}, {line[2], "You have reached your destination", kArrive}},
                            line[2], "There");
        CHECK(r->turn_along.size() == 2 && std::fabs(r->turn_along[0] - 1000) < 2 && std::fabs(r->turn_along[1] - 2000) < 4);
        CHECK(locate({}, {}, a, 0).off_m > 1e100);
    }

    // ---- The stand-in engine ------------------------------------------
    {
        Config c;
        c.simulated = true;
        auto e = make_engine(c);
        CHECK(e->take().state == Snapshot::kOff);
        Fix f;
        f.ok = true, f.lat = 51.5, f.lon = -0.12;
        e->position(f);
        e->go({51.49, -0.11}, "South Row", {0, 0});
        Snapshot s = e->take();
        CHECK(s.state == Snapshot::kRouting && s.route && s.route->line.size() == 3 && s.route->turns.size() == 2);
        CHECK(s.route && std::fabs(s.route->line[0].lat - 51.5) < 1e-9);   // from the fix, not `from`
        CHECK(s.left_m > 1000 && s.left_s > 0);
        unsigned gen = s.gen;
        e->stop();
        s = e->take();
        CHECK(s.state == Snapshot::kOff && !s.route && s.gen != gen);
        e->say("nothing happens");
    }

    // ---- Search -------------------------------------------------------
    {
        CHECK_EQ(std::string("st|x"), std::string("st|x"));
        std::vector<std::string> w = search_words("Washington St., HOBOKEN");
        CHECK(w.size() == 3 && w[0] == "washington" && w[1] == "st" && w[2] == "hoboken");
        SearchIndex none;
        CHECK(!none.load("/nonexistent"));
        CHECK(none.find("x", 5, false, 0, 0).empty());
        if (argc > 1) {
            SearchIndex s;
            CHECK(s.load(argv[1]));
            CHECK(s.size() == 11);
            std::vector<Place> r = s.find("zero", 5, false, 0, 0);   // the town, then streets in it
            CHECK(r.size() == 5 && r[0].kind == 't' && r[0].name == "Zeroville" && r[1].town == "Zeroville");
            r = s.find("row", 5, true, 51.5032, -0.1229);   // standing on North Row
            CHECK(r.size() == 2 && r[0].name == "North Row" && r[1].name == "South Row" && r[0].metres < 5);
            r = s.find("south row zeroville", 5, false, 0, 0);
            CHECK(r.size() == 1 && r[0].town == "Zeroville");
            CHECK(s.find("high st", 5, false, 0, 0).size() == 1);   // "st" starts "Street"
            CHECK(s.find("igh", 5, false, 0, 0).empty());           // words start where they start
            CHECK(s.find("zeroville", 5, false, 0, 0).size() == 5); // the town, and streets in it
            CHECK(s.find("zeroville", 5, false, 0, 0)[0].kind == 't');
            CHECK(s.find("51", 5, false, 0, 0).empty());            // not the numbers
            CHECK(s.find("", 5, false, 0, 0).empty());
        }
    }

    if (fails) {
        std::printf("%d checks failed\n", fails);
        return 1;
    }
    std::printf("ok   maps: Navit's answers, the position feed, the stand-in engine, search\n");
    return 0;
}
