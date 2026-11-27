// Routing for Maps: Navit plans the route and the turns; the app draws
// them (README.md, "Directions"). The engine runs on its own thread, so
// nothing here ever waits on the screen's thread.
//
// Navit needs a display and a D-Bus session even with nobody looking, so
// the real engine starts its own: Xvfb (a screen in memory, 64 x 64), a
// private dbus-daemon, and navit with the routing map. It feeds Navit the
// GPS position as NMEA through a FIFO, asks it over D-Bus (with dbus-send)
// for the route's state, and reads the route's line and turns back when a
// new route is ready. What Navit would say comes out on its stdout; the
// app decides what is spoken (maps.cpp), so that is only read, for arriving.
//
// In the simulator a stand-in plans an L-shaped route instead, so the
// screens can be tried and tested with no Navit (set ZS_MAPS_NAVIT=1 to use
// the real one on a PC that has it).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "apps/maps/nav_parse.h"

namespace zmap::nav {

struct Route {
    std::vector<LatLon> line;
    std::vector<double> cum;             // metres along the line to each point
    std::vector<Maneuver> turns;
    std::vector<double> turn_along;      // metres along the line to each turn
    LatLon dest;
    std::string dest_name;
};

struct Snapshot {
    enum State { kOff, kStarting, kPlanning, kRouting, kNoRoute, kFailed };
    State state = kOff;
    std::string error;                   // kFailed: why, to show
    unsigned gen = 0;                    // changes when `route` does
    std::shared_ptr<const Route> route;
    double left_m = -1, left_s = -1;     // to the destination, from Navit; -1: not known
    std::vector<std::string> said;       // what Navit said since the last take()
};

struct Fix {
    bool ok = false;
    double lat = 0, lon = 0, speed_mps = 0, track_deg = 0;
};

class Engine {
public:
    virtual ~Engine() = default;
    // Plan a route to `dest` from the last position given (or `from`, if
    // there has been no fix). Replaces any route.
    virtual void go(LatLon dest, const std::string& name, LatLon from) = 0;
    virtual void stop() = 0;
    virtual void position(const Fix& f) = 0;
    virtual void set_voice(bool on) = 0;
    // Speak this (with espeak-ng), cutting short anything still being said.
    // Nothing when the voice is off, or in the simulator.
    virtual void say(const std::string& text) = 0;
    // A copy of the state; `said` is handed over once.
    virtual Snapshot take() = 0;
};

struct Config {
    std::string navit_map;   // Navit's routing map (.navit, a binfile)
    bool imperial = false;
    bool simulated = false;  // the stand-in, not Navit
};

std::unique_ptr<Engine> make_engine(const Config& c);

// Built from a line and turns: the turns' places along the line.
std::shared_ptr<Route> make_route(std::vector<LatLon> line, std::vector<Maneuver> turns, LatLon dest, std::string name);

}  // namespace zmap::nav
