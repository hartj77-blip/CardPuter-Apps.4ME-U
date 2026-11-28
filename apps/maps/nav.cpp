// nav.h: the routing engine. Everything slow (starting Navit, asking it
// things over D-Bus, reading its route) happens on the engine's thread.
#include "apps/maps/nav.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>

extern char** environ;

namespace zmap::nav {

// The car profile with every road weighted the same: Navit then finds the
// shortest way, not the fastest (the speeds stay, for the time it takes).
std::string shortest_profile(const std::string& profile) {
    std::string out = profile;
    // A road's own speed limit would otherwise weigh in again (maxspeed_handling 0).
    size_t mh = out.find("maxspeed_handling=\"");
    if (mh != std::string::npos) {
        size_t end = out.find('"', mh + 19);
        if (end != std::string::npos) out.replace(mh + 19, end - mh - 19, "2");
    }
    for (size_t at = 0; (at = out.find("route_weight=\"", at)) != std::string::npos;) {
        at += 14;
        size_t end = out.find('"', at);
        if (end == std::string::npos) break;
        out.replace(at, end - at, "50");
    }
    return out;
}

std::shared_ptr<Route> make_route(std::vector<LatLon> line, std::vector<Maneuver> turns, LatLon dest, std::string name) {
    auto r = std::make_shared<Route>();
    r->line = std::move(line);
    r->cum = cumulative(r->line);
    r->turns = std::move(turns);
    r->dest = dest;
    r->dest_name = std::move(name);
    // Each turn is at a point of the line, in order: find them going forward.
    size_t hint = 0;
    for (const Maneuver& m : r->turns) {
        Progress p = locate(r->line, r->cum, m.at, hint);
        hint = p.seg;
        r->turn_along.push_back(p.along_m);
    }
    return r;
}

namespace {

using Clock = std::chrono::steady_clock;

// ---- Child processes -----------------------------------------------------

struct Child {
    pid_t pid = -1;
    int out = -1;   // its stdout (or the fd it was asked to write to), if captured
};

// Start argv[0] from PATH in a process group of its own, with `env` added
// to ours. With `capture`, its stdout comes back as Child::out; with
// `info_fd`, a pipe is its fd 3 instead (Xvfb -displayfd 3, dbus-daemon
// --print-address=3) and stdout goes to /dev/null.
Child spawn(const std::vector<std::string>& argv, const std::vector<std::string>& env, bool capture, bool info_fd = false) {
    Child c;
    int p[2] = {-1, -1};
    if ((capture || info_fd) && pipe2(p, O_CLOEXEC) != 0) return c;
    std::vector<std::string> envs;
    for (char** e = environ; *e; e++) {
        std::string s = *e;
        bool replaced = false;
        for (const std::string& n : env)
            if (s.compare(0, s.find('=') + 1, n, 0, n.find('=') + 1) == 0) replaced = true;
        if (!replaced) envs.push_back(s);
    }
    envs.insert(envs.end(), env.begin(), env.end());
    std::vector<char*> av, ev;
    for (const std::string& a : argv) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    for (std::string& e : envs) ev.push_back(e.data());
    ev.push_back(nullptr);
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGTERM);   // if ZeroShell dies, so does this
        int null = open("/dev/null", O_RDWR);
        dup2(null, 0);
        if (capture) {
            dup2(p[1], 1);
            dup2(null, 2);
        } else {
            dup2(null, 1);
            dup2(null, 2);
            if (info_fd) dup2(p[1], 3);
        }
        signal(SIGPIPE, SIG_DFL);
        execvpe(av[0], av.data(), ev.data());
        _exit(127);
    }
    if (p[1] >= 0) close(p[1]);
    if (pid < 0) {
        if (p[0] >= 0) close(p[0]);
        return c;
    }
    c.pid = pid;
    c.out = p[0];
    return c;
}

void kill_child(Child& c) {
    if (c.pid > 0) {
        // Closing Maps waits for this, so not for long.
        kill(-c.pid, SIGTERM);
        for (int i = 0; i < 25; i++) {
            if (waitpid(c.pid, nullptr, WNOHANG) == c.pid) {
                c.pid = -1;
                break;
            }
            usleep(20000);
        }
        if (c.pid > 0) {
            kill(-c.pid, SIGKILL);
            waitpid(c.pid, nullptr, 0);
        }
    }
    if (c.out >= 0) close(c.out);
    c = Child{};
}

bool alive(Child& c) { return c.pid > 0 && waitpid(c.pid, nullptr, WNOHANG) == 0; }

// Read what is there now (or wait up to ms for some). Appends; false at end of file.
bool read_some(int fd, std::string& into, int ms) {
    pollfd pf{fd, POLLIN, 0};
    if (poll(&pf, 1, ms) <= 0) return true;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf);
    if (n <= 0) return false;
    into.append(buf, size_t(n));
    return true;
}

// Run a program to the end and return its stdout. Killed after `ms`.
std::string run(const std::vector<std::string>& argv, const std::vector<std::string>& env, int ms, bool* ok = nullptr) {
    Child c = spawn(argv, env, true);
    std::string out;
    auto end = Clock::now() + std::chrono::milliseconds(ms);
    while (c.out >= 0 && Clock::now() < end) {
        int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count());
        if (!read_some(c.out, out, std::max(left, 1))) break;
    }
    int status = -1;
    bool done = c.pid > 0 && waitpid(c.pid, &status, WNOHANG) == c.pid;
    if (!done && c.pid > 0) {
        kill(-c.pid, SIGKILL);
        waitpid(c.pid, &status, 0);
    }
    if (c.out >= 0) close(c.out);
    if (ok) *ok = done && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return out;
}

bool on_path(const std::string& prog) {
    const char* p = std::getenv("PATH");
    std::string path = p ? p : "/usr/local/bin:/usr/bin:/bin";
    size_t at = 0;
    while (at <= path.size()) {
        size_t e = path.find(':', at);
        if (e == std::string::npos) e = path.size();
        std::string f = path.substr(at, e - at) + "/" + prog;
        if (access(f.c_str(), X_OK) == 0) return true;
        at = e + 1;
    }
    return false;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---- Shared state --------------------------------------------------------

class Base : public Engine {
public:
    void position(const Fix& f) override {
        std::lock_guard<std::mutex> l(mu_);
        fix_ = f;
    }
    void set_voice(bool on) override { voice_ = on; }
    Snapshot take() override {
        std::lock_guard<std::mutex> l(mu_);
        Snapshot s = snap_;
        snap_.said.clear();
        return s;
    }

protected:
    void publish(const std::function<void(Snapshot&)>& f) {
        std::lock_guard<std::mutex> l(mu_);
        f(snap_);
    }
    std::mutex mu_;
    Snapshot snap_;
    Fix fix_;
    std::atomic<bool> voice_{true};
};

// ---- The stand-in --------------------------------------------------------

class FakeEngine : public Base {
public:
    void say(const std::string&) override {}
    void set_shortest(bool) override {   // the same L, planned again, as Navit would
        if (planned_) go(last_dest_, last_name_, last_from_);
    }
    void go(LatLon dest, const std::string& name, LatLon from) override {
        planned_ = true;
        last_dest_ = dest, last_name_ = name, last_from_ = from;
        Fix f;
        {
            std::lock_guard<std::mutex> l(mu_);
            f = fix_;
        }
        LatLon a = f.ok ? LatLon{f.lat, f.lon} : from;
        LatLon corner{a.lat, dest.lon};
        std::vector<LatLon> line{a, corner, dest};
        bool east = dest.lon > a.lon, north = dest.lat > a.lat;
        // Facing east and then going north is a left turn.
        std::string turn = east == north ? "Turn left into Market Street" : "Turn right into Market Street";
        std::vector<Maneuver> turns{{corner, turn, east == north ? kLeft : kRight}, {dest, "You have reached your destination", kArrive}};
        auto r = make_route(line, turns, dest, name);
        double len = r->cum.back();
        publish([&](Snapshot& s) {
            s.state = Snapshot::kRouting;
            s.route = r;
            s.gen++;
            s.left_m = len;
            s.left_s = len / 13.4;
            s.error.clear();
        });
    }
    void stop() override {
        planned_ = false;
        publish([](Snapshot& s) {
            s.state = Snapshot::kOff;
            s.route.reset();
            s.gen++;
            s.left_m = s.left_s = -1;
        });
    }

private:
    bool planned_ = false;
    LatLon last_dest_, last_from_;
    std::string last_name_;
};

// ---- Navit ---------------------------------------------------------------

class NavitEngine : public Base {
public:
    explicit NavitEngine(Config c) : cfg_(std::move(c)) {}
    ~NavitEngine() override {
        {
            std::lock_guard<std::mutex> l(mu_);
            quit_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void go(LatLon dest, const std::string& name, LatLon from) override {
        {
            std::lock_guard<std::mutex> l(mu_);
            want_ = true;
            want_dest_ = dest;
            want_name_ = name;
            want_from_ = from;
            new_request_ = true;
            if (snap_.state != Snapshot::kRouting) snap_.state = snap_.state == Snapshot::kOff || snap_.state == Snapshot::kFailed
                                                                     ? Snapshot::kStarting
                                                                     : Snapshot::kPlanning;
            snap_.error.clear();
        }
        if (!thread_.joinable()) thread_ = std::thread([this] { main(); });
        cv_.notify_all();
    }

    void say(const std::string& text) override {
        if (!voice_) return;
        {
            std::lock_guard<std::mutex> l(mu_);
            to_say_ = text;
        }
        cv_.notify_all();
    }

    void set_shortest(bool on) override {
        {
            std::lock_guard<std::mutex> l(mu_);
            if (cfg_.shortest == on) return;
            cfg_.shortest = on;
            // Navit takes its car settings at start: start it again, then plan again.
            if (want_) {
                restart_ = true;
                new_request_ = true;
                snap_.state = Snapshot::kPlanning;
            }
        }
        cv_.notify_all();
    }

    void stop() override {
        {
            std::lock_guard<std::mutex> l(mu_);
            want_ = false;
            new_request_ = true;
            snap_.state = Snapshot::kOff;
            snap_.route.reset();
            snap_.gen++;
            snap_.left_m = snap_.left_s = -1;
        }
        cv_.notify_all();
    }

private:
    void fail(const std::string& why) {
        publish([&](Snapshot& s) {
            s.state = Snapshot::kFailed;
            s.error = why;
        });
    }

    // The thread: start Navit when a route is wanted, look after it while
    // one is, and stop it (and its display and bus) when none is.
    void main() {
        for (;;) {
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return quit_ || want_; });
                if (quit_) break;
            }
            if (!start()) {
                shutdown();
                std::unique_lock<std::mutex> l(mu_);
                want_ = false;   // until asked again
                continue;
            }
            serve();
            shutdown();
            std::lock_guard<std::mutex> l(mu_);
            if (quit_) break;
        }
        shutdown();
        if (speech_.pid > 0) kill_child(speech_);
    }

    bool start() {
        {
            std::lock_guard<std::mutex> l(mu_);
            restart_ = false;
            new_request_ = true;   // the destination goes to the new Navit
        }
        for (const char* p : {"navit", "Xvfb", "dbus-daemon", "dbus-send"})
            if (!on_path(p)) {
                fail(std::string("Routing needs ") + p + ": reinstall Maps");
                return false;
            }
        if (access(cfg_.navit_map.c_str(), R_OK) != 0) {
            fail("No routing map for this map");
            return false;
        }
        const char* rt = std::getenv("XDG_RUNTIME_DIR");
        std::string base = rt && *rt ? rt : "/tmp";
        // Folders an earlier run couldn't tidy away (it crashed, or was killed).
        run({"sh", "-c", "for d in \"$0\"/zeroos-maps-*; do [ -O \"$d\" ] && rm -rf -- \"$d\"; done", base}, {}, 3000);
        char tmpl[512];
        std::snprintf(tmpl, sizeof tmpl, "%s/zeroos-maps-XXXXXX", base.c_str());
        if (!mkdtemp(tmpl)) {
            fail("Couldn't make a folder for Navit");
            return false;
        }
        dir_ = tmpl;
        fifo_path_ = dir_ + "/nmea";
        if (mkfifo(fifo_path_.c_str(), 0600) != 0) {
            fail("Couldn't make Navit's position pipe");
            return false;
        }
        // Read-write, so opening never waits for Navit and writes never fail
        // while it hasn't opened its end yet.
        fifo_ = open(fifo_path_.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fifo_ < 0 || !write_config()) {
            fail("Couldn't write Navit's settings");
            return false;
        }

        // A screen in memory; it tells us its number on fd 3.
        xvfb_ = spawn({"Xvfb", "-displayfd", "3", "-screen", "0", "64x64x16", "-nolisten", "tcp"}, {}, false, true);
        std::string disp;
        if (!read_line(xvfb_.out, disp, 10000)) {
            fail("Couldn't start Xvfb");
            return false;
        }
        bus_ = spawn({"dbus-daemon", "--session", "--nofork", "--nopidfile", "--print-address=3"}, {}, false, true);
        std::string addr;
        if (!read_line(bus_.out, addr, 10000)) {
            fail("Couldn't start a D-Bus for Navit");
            return false;
        }
        env_ = {"DISPLAY=:" + disp, "DBUS_SESSION_BUS_ADDRESS=" + addr, "LANG=C.UTF-8", "LC_ALL=C.UTF-8"};
        navit_ = spawn({"navit", "-c", dir_ + "/navit.xml"}, env_, true);
        if (navit_.pid < 0) {
            fail("Couldn't start Navit");
            return false;
        }
        fcntl(navit_.out, F_SETFL, O_NONBLOCK);
        // Up when it answers.
        auto until = Clock::now() + std::chrono::seconds(60);
        while (Clock::now() < until) {
            bool ok = false;
            ask("route.route_status", &ok);
            if (ok) return true;
            if (!alive(navit_)) break;
            if (wait_ms(500)) return false;
        }
        fail("Navit didn't start");
        return false;
    }

    // True if the engine is being closed. With `work`, something to say or
    // a new destination ends the wait early.
    bool wait_ms(int ms, bool work = false) {
        std::unique_lock<std::mutex> l(mu_);
        cv_.wait_for(l, std::chrono::milliseconds(ms),
                     [&] { return quit_ || (work && (!to_say_.empty() || new_request_ || !want_ || restart_)); });
        return quit_;
    }

    static bool read_line(int fd, std::string& line, int ms) {
        std::string buf;
        auto end = Clock::now() + std::chrono::milliseconds(ms);
        while (fd >= 0 && Clock::now() < end) {
            if (!read_some(fd, buf, 200)) break;
            size_t nl = buf.find('\n');
            if (nl != std::string::npos) {
                line = buf.substr(0, nl);
                return !line.empty();
            }
        }
        return false;
    }

    bool write_config() {
        std::string sys = read_file("/etc/navit/navit.xml");
        std::string profile = xml_element(sys, "<vehicleprofile name=\"car\"", "</vehicleprofile>");
        bool shortest;
        {
            std::lock_guard<std::mutex> l(mu_);
            shortest = cfg_.shortest;
        }
        if (shortest) profile = shortest_profile(profile);
        std::string navigation = xml_element(sys, "<navigation>", "</navigation>");
        if (profile.empty()) return false;
        std::ofstream o(dir_ + "/navit.xml");
        o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<!DOCTYPE config SYSTEM \"navit.dtd\">\n"
             "<!-- Written by ZeroOS Maps for one run of Navit (apps/maps/nav.cpp). -->\n"
             "<config xmlns:xi=\"http://www.w3.org/2001/XInclude\">\n"
             "<plugins>\n"
             "  <plugin path=\"$NAVIT_LIBDIR/*/${NAVIT_LIBPREFIX}lib*.so\" ondemand=\"yes\"/>\n"
             "  <plugin path=\"$NAVIT_LIBDIR/*/${NAVIT_LIBPREFIX}libbinding_dbus.so\" active=\"yes\"/>\n"
             "</plugins>\n"
             "<navit center=\"0 0\" zoom=\"64\" tracking=\"1\" orientation=\"-1\" recent_dest=\"0\""
          << (cfg_.imperial ? " imperial=\"1\"" : "")
          << " default_layout=\"Car-simple\">\n"
             "  <graphics type=\"gtk_drawing_area\" w=\"64\" h=\"64\"/>\n"
             "  <gui type=\"internal\" enabled=\"yes\"/>\n"
             "  <vehicle name=\"ZeroOS\" profilename=\"car\" enabled=\"yes\" active=\"1\" follow=\"1\" source=\"pipe:cat "
          << fifo_path_
          << "\"/>\n"
             "  <tracking cdf_histsize=\"0\"/>\n"
             "  <route destination_distance=\"50\"/>\n"
          << profile << "\n"
          << (navigation.empty() ? "" : navigation + "\n")
          << "  <speech type=\"cmdline\" data=\"echo ZSSAY: %s\"/>\n"
             "  <xi:include href=\"$NAVIT_SHAREDIR/navit_layout_car_simple.xml\"/>\n"
             "  <mapset enabled=\"yes\"><map type=\"binfile\" enabled=\"yes\" data=\""
          << cfg_.navit_map
          << "\"/></mapset>\n"
             "</navit>\n"
             "</config>\n";
        return bool(o);
    }

    std::vector<std::string> dbus(const std::string& method) {
        return {"dbus-send", "--print-reply", "--session", "--dest=org.navit_project.navit",
                "/org/navit_project/navit/default_navit", "org.navit_project.navit.navit." + method};
    }
    std::string ask(const std::string& what, bool* ok = nullptr) {
        auto a = dbus("evaluate");
        a.push_back("string:" + what);
        return run(a, env_, 5000, ok);
    }
    bool call(const std::string& method, std::vector<std::string> args, int ms = 5000) {
        auto a = dbus(method);
        a.insert(a.end(), args.begin(), args.end());
        bool ok = false;
        run(a, env_, ms, &ok);
        return ok;
    }

    static std::string geo(LatLon p) {
        char b[64];
        std::snprintf(b, sizeof b, "geo: %.6f %.6f", p.lon, p.lat);
        return b;
    }

    // Looking after a running Navit until no route is wanted.
    void serve() {
        long last_status = -1;
        auto last_fetch = Clock::now() - std::chrono::hours(1);
        auto last_fix = Clock::now() - std::chrono::hours(1);
        std::string out;
        bool fed = false;
        for (;;) {
            bool fresh = false, want = false;
            LatLon dest, from;
            std::string name;
            Fix f;
            std::string say;
            {
                std::lock_guard<std::mutex> l(mu_);
                if (quit_ || restart_) return;
                say.swap(to_say_);
                want = want_;
                fresh = new_request_;
                new_request_ = false;
                dest = want_dest_, from = want_from_, name = want_name_;
                f = fix_;
            }
            if (!say.empty() && voice_ && on_path("espeak-ng")) speak(say);   // "You have arrived" comes as it stops
            if (!want) return;
            if (!alive(navit_)) {
                fail("Navit stopped");
                std::lock_guard<std::mutex> l(mu_);
                want_ = false;
                return;
            }
            // The position, once a second (Navit counts a GPS that goes quiet as lost).
            if (f.ok && Clock::now() - last_fix >= std::chrono::milliseconds(900)) {
                std::time_t t = std::time(nullptr);
                char utc[16], date[16];
                std::strftime(utc, sizeof utc, "%H%M%S", std::gmtime(&t));
                std::strftime(date, sizeof date, "%d%m%y", std::gmtime(&t));
                std::string s = nmea(f.lat, f.lon, f.speed_mps, f.track_deg, utc, date);
                if (write(fifo_, s.data(), s.size()) < 0) {}
                last_fix = Clock::now();
                fed = true;
            }
            if (fresh) {
                if (!fed) call("set_position", {"string:" + geo(from)});
                call("set_destination", {"string:" + geo(dest), "string:" + name});
                dest_ = dest;
                dest_name_ = name;
                last_status = -1;
                publish([](Snapshot& s) {
                    if (s.state != Snapshot::kRouting) s.state = Snapshot::kPlanning;
                });
            }
            // What Navit says.
            for (size_t before = ~size_t(0); out.size() != before && out.size() < 65536;) {
                before = out.size();
                if (!read_some(navit_.out, out, 0)) break;
            }
            size_t nl;
            while ((nl = out.find('\n')) != std::string::npos) {
                std::string line = out.substr(0, nl);
                out.erase(0, nl + 1);
                if (line.rfind("ZSSAY: ", 0) != 0) continue;
                std::string text = line.substr(7);
                while (!text.empty() && (text.back() == ' ' || text.back() == '\r')) text.pop_back();
                if (text.empty()) continue;
                publish([&](Snapshot& s) { s.said.push_back(text); });
            }
            if (out.size() > 65536) out.clear();

            long status = dbus_number(ask("route.route_status"), -1);
            long len = dbus_number(ask("route.destination_length"), -1);
            long time = dbus_number(ask("route.destination_time"), -1);
            if (std::getenv("ZS_MAPS_NAVIT_DEBUG"))
                std::fprintf(stderr, "navit: status %ld, %ld m, %ld ds%s\n", status, len, time, fresh ? " (new destination)" : "");
            // route_status: 1 destination set, 3 not found, 5 and 13 working,
            // 17 a new route, 33 the route kept up to date as you move.
            bool done = status == 17 || status == 33;
            // Again every 10 s for the route kept up to date, and soon if Navit
            // hadn't worked out the turns yet when the line was read.
            auto again = no_turns_ ? std::chrono::seconds(2) : std::chrono::seconds(10);
            bool refetch = done && (status != last_status || Clock::now() - last_fetch > again);
            if (status == 3) {
                publish([](Snapshot& s) { s.state = Snapshot::kNoRoute; });
            } else if (refetch) {
                fetch();
                last_fetch = Clock::now();
            }
            if (done)
                publish([&](Snapshot& s) {
                    s.left_m = len >= 0 ? double(len) : -1;
                    s.left_s = time >= 0 ? double(time) / 10 : -1;   // Navit counts tenths of a second
                });
            last_status = status;
            if (wait_ms(500, true)) return;
        }
    }

    void fetch() {
        std::string dump = dir_ + "/route.txt", json = dir_ + "/route.json";
        std::remove(dump.c_str());
        std::remove(json.c_str());
        // The route's map: its object path, then dump it to a file.
        auto a = dbus("get_attr");
        a.push_back("string:route");
        std::string r = run(a, env_, 5000);
        std::string route_path = object_path(r);
        if (route_path.empty()) return;
        std::string m = run({"dbus-send", "--print-reply", "--session", "--dest=org.navit_project.navit", route_path,
                             "org.navit_project.navit.route.get_attr", "string:map"},
                            env_, 5000);
        std::string map_path = object_path(m);
        if (map_path.empty()) return;
        run({"dbus-send", "--print-reply", "--session", "--dest=org.navit_project.navit", map_path,
             "org.navit_project.navit.map.dump", "string:" + dump},
            env_, 15000);
        call("export_as_geojson", {"string:" + json}, 15000);
        std::vector<LatLon> line = parse_route_dump(read_file(dump));
        std::vector<Maneuver> turns = parse_route_geojson(read_file(json));
        if (line.size() < 2) return;
        no_turns_ = turns.empty();
        auto route = make_route(std::move(line), std::move(turns), dest_, dest_name_);
        publish([&](Snapshot& s) {
            if (s.state == Snapshot::kOff) return;   // stopped meanwhile
            s.state = Snapshot::kRouting;
            s.route = route;
            s.gen++;
        });
    }

    static std::string object_path(const std::string& reply) {
        size_t a = reply.find("object path \"");
        if (a == std::string::npos) return "";
        a += 13;
        size_t b = reply.find('"', a);
        return b == std::string::npos ? "" : reply.substr(a, b - a);
    }

    // One thing at a time: a newer instruction cuts the last one short.
    void speak(const std::string& text) {
        if (speech_.pid > 0) {
            if (alive(speech_)) kill(-speech_.pid, SIGTERM);
            waitpid(speech_.pid, nullptr, 0);
            speech_ = Child{};
        }
        speech_ = spawn({"espeak-ng", "-v", "en-us", "-s", "165", text}, {}, false);
    }

    void shutdown() {
        // The last words (arriving) are left to finish; speak() or the
        // engine's end tidies them away.
        kill_child(navit_);
        kill_child(bus_);
        kill_child(xvfb_);
        if (fifo_ >= 0) close(fifo_);
        fifo_ = -1;
        if (!dir_.empty()) {
            for (const char* f : {"/nmea", "/navit.xml", "/route.txt", "/route.json"}) std::remove((dir_ + f).c_str());
            rmdir(dir_.c_str());
            dir_.clear();
        }
        env_.clear();
    }

    Config cfg_;
    std::thread thread_;
    std::condition_variable cv_;
    bool quit_ = false, want_ = false, new_request_ = false, no_turns_ = false, restart_ = false;
    LatLon want_dest_, want_from_, dest_;
    std::string want_name_, dest_name_, to_say_;
    std::string dir_, fifo_path_;
    int fifo_ = -1;
    Child xvfb_, bus_, navit_, speech_;
    std::vector<std::string> env_;
};

}  // namespace

std::unique_ptr<Engine> make_engine(const Config& c) {
    if (c.simulated) return std::make_unique<FakeEngine>();
    return std::make_unique<NavitEngine>(c);
}

}  // namespace zmap::nav
