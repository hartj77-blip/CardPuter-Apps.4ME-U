// Maps with the real Navit: a route across the made-up town, from a GPS
// fix on High Street to South Row. navit_check.sh runs it when Navit,
// maptool, Xvfb and D-Bus are installed (CI installs them).
//   navit_test town.navit
#include <unistd.h>

#include <cstdio>
#include <string>

#include "apps/maps/nav.h"

using namespace zmap::nav;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    Config c;
    c.navit_map = argv[1];
    c.imperial = true;
    auto e = make_engine(c);
    e->set_voice(false);
    Fix f;
    f.ok = true;
    f.lat = 51.500729, f.lon = -0.124625, f.speed_mps = 8, f.track_deg = 64;   // High Street, going north-east
    e->position(f);
    e->go({51.499247, -0.122893}, "South Row", {0, 0});
    Snapshot s;
    for (int i = 0; i < 120; i++) {   // a minute at most
        e->position(f);
        s = e->take();
        if (s.state == Snapshot::kFailed) {
            std::printf("FAIL the engine: %s\n", s.error.c_str());
            return 1;
        }
        if (s.route && !s.route->turns.empty() && s.left_m > 0) break;
        usleep(500000);
    }
    if (!s.route || s.route->turns.empty()) {
        std::printf("FAIL no route with turns (state %d, %zu points)\n", int(s.state), s.route ? s.route->line.size() : size_t(0));
        return 1;
    }
    const Route& r = *s.route;
    double km = r.cum.back() / 1000;
    std::printf("route: %zu points, %.2f km, %zu turns, %.0f m and %.0f s left:", r.line.size(), km, r.turns.size(), s.left_m,
                s.left_s);
    for (const Maneuver& m : r.turns) std::printf(" [%s]", m.text.c_str());
    std::printf("\n");
    int fails = 0;
    auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("FAIL %s\n", what);
            fails++;
        }
    };
    // South Row is about 250 m away as the crow flies; by road under a kilometre.
    check(km > 0.2 && km < 1.5, "a route of a sensible length");
    check(r.line.size() >= 3, "the route follows the roads");
    check(r.turns.back().kind == kArrive, "the last turn is arriving");
    check(metres(r.line.back(), {51.499247, -0.122893}) < 80, "the route ends at South Row");
    check(metres(r.line.front(), {51.500729, -0.124625}) < 80, "the route starts at the fix");
    bool named = false;
    for (const Maneuver& m : r.turns) named |= m.text.find("Row") != std::string::npos || m.text.find("Street") != std::string::npos ||
                                             m.text.find("Avenue") != std::string::npos || m.text.find("Lane") != std::string::npos;
    check(named, "the turns name the town's streets");
    e->stop();
    for (int i = 0; i < 20 && e->take().state != Snapshot::kOff; i++) usleep(100000);
    check(e->take().state == Snapshot::kOff, "it stops");
    e.reset();   // Navit, its display and its bus go away with it
    if (fails) return 1;
    std::printf("ok   maps: a route by Navit across the town\n");
    return 0;
}
