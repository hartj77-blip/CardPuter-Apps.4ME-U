// Reading what Navit hands back, and writing what it reads (nav.cpp), with
// no Navit, LVGL or threads, so the unit tests can check every piece.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zmap::nav {

struct LatLon {
    double lat = 0, lon = 0;
};

// A turn on the route: where it is, and what to do there, as shown on
// screen ("Turn right into Wiggins Street").
struct Maneuver {
    LatLon at;
    std::string text;
    int kind = 0;   // what the arrow shows (Turn below)
};

enum Turn { kStraight, kLeft, kRight, kSlightLeft, kSlightRight, kSharpLeft, kSharpRight, kUturn, kRoundabout, kExitLeft,
            kExitRight, kArrive };

// The route's line from a dump of Navit's route map (map.dump over
// D-Bus): the street_route items' points, in Navit's "mg" coordinates,
// joined end to end in order.
std::vector<LatLon> parse_route_dump(const std::string& text);

// The turns from Navit's route export (export_as_geojson). Navit's file has
// trailing commas, which this reads anyway.
std::vector<Maneuver> parse_route_geojson(const std::string& text);

// Navit's spoken words as written to show: the distance taken out (the app
// shows its own, counting down), "US 1;US 9" as "US 1/US 9", lane hints
// ("|left") dropped. "Turn right in 500 meters into Bridge Street" ->
// "Turn right into Bridge Street".
std::string clean_instruction(const std::string& text);
int turn_kind(const std::string& text);

// A fix as the two NMEA sentences Navit's "pipe" vehicle reads (RMC and
// GGA, each ending in CR LF). utc is "HHMMSS" and date "DDMMYY".
std::string nmea(double lat, double lon, double speed_mps, double track_deg, const std::string& utc,
                 const std::string& date);

// Navit's "mg" projection.
LatLon from_mg(int32_t x, int32_t y);

// The value of "evaluate" over D-Bus, from dbus-send --print-reply:
//    string "33"  ->  33. Missing or "(no text[none])" -> fallback.
long dbus_number(const std::string& reply, long fallback);

// Pieces of Navit's own car settings, from its /etc/navit/navit.xml: the
// <vehicleprofile name="car"> element and <navigation> element. Empty if
// the file doesn't have them.
std::string xml_element(const std::string& xml, const std::string& open_tag_start, const std::string& close_tag);

// Where you are on a route line: the segment you're nearest (index of its
// first point) and how far along it, and how far you are off the line.
struct Progress {
    size_t seg = 0;
    double along_m = 0;    // from the line's start to you
    double off_m = 0;
};
Progress locate(const std::vector<LatLon>& line, const std::vector<double>& cumulative_m, LatLon at, size_t hint);
// cumulative_m[i]: metres from line[0] to line[i].
std::vector<double> cumulative(const std::vector<LatLon>& line);
double metres(LatLon a, LatLon b);

}  // namespace zmap::nav
