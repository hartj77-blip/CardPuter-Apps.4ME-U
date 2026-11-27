// The TV (kit/tv.h): the mode it asks for, the arguments mpv gets, what it
// makes of mpv's messages, the HDMI claim, and a whole video on a stand-in
// TV: a fake sysfs (ZS_TV_SYSFS) and a fake mpv (Python, on its IPC socket)
// that answers the way mpv does. No TV or mpv needed.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "apps/kit/tv.h"

using namespace zs::kit;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

void write(const std::string& path, const std::string& text) {
    std::ofstream f(path);
    f << text;
}

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool has(const std::vector<std::string>& v, const std::string& s) {
    for (auto& x : v)
        if (x == s) return true;
    return false;
}

// Poll until done() or about five seconds.
template <class F>
bool wait_for(TvVideo& v, F done) {
    for (int i = 0; i < 250; ++i) {
        v.poll();
        if (done()) return true;
        usleep(20000);
    }
    return false;
}

// mpv as far as TvVideo uses it: the socket, observed properties, pause,
// seek, quit, and the end of the file (FAKE_MPV=eof) or a failure (=error).
const char* kFakeMpv = R"PY(#!/usr/bin/env python3
import json, os, socket, sys
args = sys.argv[1:]
open(os.environ["FAKE_MPV_ARGS"], "w").write("\n".join(args))
ipc = [a.split("=", 1)[1] for a in args if a.startswith("--input-ipc-server=")][0]
start = 0.0
for a in args:
    if a.startswith("--start="): start = float(a.split("=", 1)[1])
mode = os.environ.get("FAKE_MPV", "play")
if mode == "crash":
    sys.stderr.write("[vo/gpu/drm] Failed to acquire DRM master\n")
    sys.exit(1)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(ipc)
srv.listen(1)
c, _ = srv.accept()
f = c.makefile("rwb", buffering=0)
def say(**m): f.write((json.dumps(m) + "\n").encode())
pos, observed = start, {}
say(event="start-file")
while True:
    line = f.readline()
    if not line: break
    cmd = json.loads(line)["command"]
    if cmd[0] == "observe_property":
        observed[cmd[2]] = cmd[1]
        if len(observed) == 4:
            say(event="property-change", id=observed["duration"], name="duration", data=600.0)
            say(event="property-change", id=observed["time-pos"], name="time-pos", data=pos)
            say(event="property-change", id=observed["pause"], name="pause", data=False)
            say(event="file-loaded")
            if mode == "error":
                say(event="end-file", reason="error", file_error="loading failed")
                break
            say(event="playback-restart")
            if mode == "eof":
                say(event="property-change", id=observed["time-pos"], name="time-pos", data=600.0)
                say(event="end-file", reason="eof")
                break
    elif cmd[0] == "set_property" and cmd[1] == "pause":
        say(request_id=0, error="success")
        say(event="property-change", id=observed["pause"], name="pause", data=cmd[2])
    elif cmd[0] == "seek":
        pos = cmd[1] if cmd[2] == "absolute" else pos + cmd[1]
        say(request_id=0, error="success")
        say(event="seek")
        say(event="property-change", id=observed["time-pos"], name="time-pos", data=pos)
        say(event="playback-restart")
    elif cmd[0] == "quit":
        break
    else:
        say(request_id=0, error="success")
os.unlink(ipc)
)PY";

}  // namespace

int main() {
    // ---- The mode
    check(tv_pick_mode({"1920x1080", "1280x720", "720x576"}) == "1280x720", "720p when the TV has it");
    check(tv_pick_mode({"3840x2160", "1920x1080", "1024x768"}) == "1920x1080", "else the first up to 1080p");
    check(tv_pick_mode({"1920x1080i", "1600x900"}) == "1600x900", "not interlaced");
    check(tv_pick_mode({"3840x2160"}).empty(), "only 4K: the TV's own choice");
    check(tv_pick_mode({}).empty(), "no list: the TV's own choice");

    // ---- mpv's arguments
    TvVideoOptions o;
    o.url = "https://jf.example/Videos/1/master.m3u8";
    o.include = "/home/x/.config/zeroos/jellyfin-mpv.conf";
    o.start_s = 75;
    auto a = tv_mpv_args("/dev/dri/card1", "1280x720", "/run/s.sock", o);
    check(a.front() == "mpv" && a.back() == o.url && a[a.size() - 2] == "--", "mpv ... -- url (an address isn't an option)");
    check(has(a, "--drm-device=/dev/dri/card1") && has(a, "--gpu-context=drm"), "on the HDMI card");
    check(has(a, "--drm-mode=1280x720"), "at the mode picked");
    check(has(a, "--input-ipc-server=/run/s.sock"), "worked through the socket");
    check(has(a, "--include=" + o.include), "with the sign-in options file");
    check(has(a, "--start=75"), "from where it was");
    check(has(a, "--hwdec=v4l2m2m-copy"), "decoded in hardware");
    TvVideoOptions bare;
    bare.url = "/v.mkv";
    auto b = tv_mpv_args("/dev/dri/card1", "", "/s", bare);
    bool none = true;
    for (auto& x : b)
        if (x.rfind("--drm-mode", 0) == 0 || x.rfind("--include", 0) == 0 || x.rfind("--start", 0) == 0) none = false;
    check(none, "no mode, options file or start when there are none");

    // ---- mpv's messages
    TvVideoState st;
    st.loading = true;
    check(tv_apply_mpv_line(st, R"({"event":"property-change","id":1,"name":"time-pos","data":12.5})") &&
              st.pos_ms == 12500, "time-pos");
    check(tv_apply_mpv_line(st, R"({"event":"property-change","id":2,"name":"duration","data":3600.25})") &&
              st.dur_ms == 3600250, "duration");
    check(tv_apply_mpv_line(st, R"({"event":"property-change","id":1,"name":"time-pos","data":null})") &&
              st.pos_ms == 12500, "null (not known yet) keeps what was");
    tv_apply_mpv_line(st, R"({"event":"playback-restart"})");
    check(!st.loading, "playing after playback-restart");
    tv_apply_mpv_line(st, R"({"event":"seek"})");
    check(st.loading, "loading while it seeks");
    tv_apply_mpv_line(st, R"({"event":"playback-restart"})");
    tv_apply_mpv_line(st, R"({"event":"property-change","id":4,"name":"paused-for-cache","data":true})");
    check(st.loading, "loading while waiting on the network");
    tv_apply_mpv_line(st, R"({"event":"property-change","id":4,"name":"paused-for-cache","data":false})");
    tv_apply_mpv_line(st, R"( { "event" : "property-change", "id":3, "name":"pause", "data" : true } )");
    check(st.paused && !st.loading, "paused (spaces anywhere)");
    check(tv_apply_mpv_line(st, R"({"request_id":0,"error":"success","data":null})"), "a reply");
    check(!tv_apply_mpv_line(st, "not json"), "not mpv: refused");
    check(!tv_apply_mpv_line(st, R"({"event":"property-change","name":"time-pos","data":"x"})"), "a wrong type: refused");
    TvVideoState e;
    tv_apply_mpv_line(e, R"({"event":"end-file","reason":"error","file_error":"loading \"failed\"","playlist_entry_id":1})");
    check(e.error == "Can't play this on the TV: loading \"failed\"" && !e.finished, "end-file: error, with why");
    TvVideoState f;
    tv_apply_mpv_line(f, R"({"event":"end-file","reason":"eof","extra":{"a":[1,"}"]}})");
    check(f.finished && f.error.empty(), "end-file: played to the end (nested values skipped)");
    TvVideoState q;
    tv_apply_mpv_line(q, R"({"event":"end-file","reason":"quit"})");
    check(!q.finished && q.error.empty(), "end-file: quit is neither");

    // ---- A stand-in TV
    char tmpl[] = "/tmp/zs-tv-test-XXXXXX";
    std::string dir = mkdtemp(tmpl) ? tmpl : "";
    check(!dir.empty(), "a folder for the stand-in");
    if (dir.empty()) return 1;
    std::string sys = dir + "/drm", bin = dir + "/bin", run = dir + "/run";
    for (auto& d : {sys, bin, run, sys + "/card0", sys + "/card0-SPI-1", sys + "/card1", sys + "/card1-HDMI-A-1"})
        mkdir(d.c_str(), 0700);
    write(sys + "/card0-SPI-1/status", "connected\n");
    write(sys + "/card1-HDMI-A-1/status", "disconnected\n");
    write(sys + "/card1-HDMI-A-1/modes", "1920x1080\n1280x720\n720x480\n");
    write(bin + "/mpv", kFakeMpv);
    chmod((bin + "/mpv").c_str(), 0755);
    setenv("ZS_TV_SYSFS", sys.c_str(), 1);
    setenv("XDG_RUNTIME_DIR", run.c_str(), 1);
    const std::string path = std::getenv("PATH") ? std::getenv("PATH") : "/usr/bin:/bin";
    setenv("PATH", (bin + ":" + path).c_str(), 1);
    std::string args_file = dir + "/args";
    setenv("FAKE_MPV_ARGS", args_file.c_str(), 1);

    check(!tv_plugged(), "unplugged: no TV (the LCD isn't one)");
    check(tv_card() == "/dev/dri/card1", "the HDMI card, not the LCD's");
    TvVideo v;
    std::string err;
    bool refused = !v.open(o, err);
    check(refused && err == "Plug a TV into the HDMI port", "unplugged: open says why (" + err + ")");
    write(sys + "/card1-HDMI-A-1/status", "connected\n");
    check(tv_plugged(), "plugged in");

    // The claim.
    tv_claim("jellyfin");
    std::string claim = slurp(tv_claim_file());
    check(claim == std::to_string(long(getpid())) + " jellyfin\n", "claim: our pid and the app");
    tv_release();
    check(access(tv_claim_file().c_str(), F_OK) != 0, "released");
    write(tv_claim_file(), "1 games\n");
    tv_release();
    check(slurp(tv_claim_file()) == "1 games\n", "someone else's claim is left alone");
    std::remove(tv_claim_file().c_str());

    // Playing, pausing, seeking, stopping.
    setenv("FAKE_MPV", "play", 1);
    o.start_s = 30;
    check(v.open(o, err), "open on the stand-in TV");
    check(v.state().open && v.state().loading && v.state().pos_ms == 30000, "loading, from 30 s");
    v.pause(false);   // before the socket is up: sent once it is
    check(wait_for(v, [&] { return !v.state().loading && v.state().dur_ms == 600000; }), "playing, 10 minutes long");
    auto got = slurp(args_file);
    check(got.find("--drm-mode=1280x720") != std::string::npos, "mpv was asked for 720p");
    check(got.find("--start=30") != std::string::npos, "and to start at 30 s");
    v.pause(true);
    check(wait_for(v, [&] { return v.state().paused; }), "paused");
    v.seek(+60, false);
    check(wait_for(v, [&] { return v.state().pos_ms == 90000 && !v.state().loading; }), "60 s on");
    v.seek(5, true);
    check(wait_for(v, [&] { return v.state().pos_ms == 5000; }), "to 5 s");
    v.show_progress();
    v.close();
    check(!v.is_open() && !v.state().open && v.state().error.empty() && !v.state().finished, "stopped, no error");
    check(v.state().pos_ms == 5000, "keeps where it got to");

    // To the end.
    setenv("FAKE_MPV", "eof", 1);
    o.start_s = 0;
    check(v.open(o, err), "open again");
    check(wait_for(v, [&] { return !v.state().open; }), "it ends");
    check(v.state().finished && v.state().error.empty() && v.state().pos_ms == 600000, "finished, at the end");

    // Failures: one mpv reports, and one that quits at once.
    setenv("FAKE_MPV", "error", 1);
    check(v.open(o, err), "open one that fails");
    check(wait_for(v, [&] { return !v.state().open; }), "it stops");
    check(v.state().error == "Can't play this on the TV: loading failed", "with mpv's reason");
    setenv("FAKE_MPV", "crash", 1);
    check(v.open(o, err), "open one that crashes");
    check(wait_for(v, [&] { return !v.state().open; }), "it stops");
    check(v.state().error == "The TV player stopped: [vo/gpu/drm] Failed to acquire DRM master",
          "with the last thing it logged (" + v.state().error + ")");

    // No mpv at all.
    setenv("PATH", "/nonexistent", 1);
    check(!v.open(o, err) && err == "mpv isn't installed", "no mpv: says so");
    setenv("PATH", path.c_str(), 1);

    std::string rm = "rm -rf '" + dir + "'";
    if (std::system(rm.c_str()) != 0) std::printf("note: %s didn't clean up\n", dir.c_str());
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
