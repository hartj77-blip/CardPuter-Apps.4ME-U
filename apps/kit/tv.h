// A TV or monitor on the HDMI port, for apps that use it themselves, without
// Dock mode: is one plugged in, telling ZeroShell the port is the app's, and
// playing a video on it.
//
// The Cardputer Zero has two DRM cards: the built-in LCD (which ZeroShell
// draws to) and vc4 with the HDMI connector. TvVideo runs mpv on the vc4 card
// alone (its DRM output, scaled by the GPU, H.264 decoded in hardware), as a
// program of its own, and works it through mpv's JSON IPC socket: pause,
// seek and where it is. The app stays on the built-in screen as the remote.
// Dock mode's desktop must not be running, as it holds the same card.
//
// Nothing here works in the simulator or in the browser: plugged() is
// false there and open() says why.
#pragma once

#include <string>
#include <vector>

namespace zs::kit {

// ---- The port

// A monitor is on the HDMI port now (sysfs; cheap, call it once a second).
bool tv_plugged();
// The DRM card with the HDMI connector, "/dev/dri/card1", or "" if none.
std::string tv_card();
// The mode to ask for, from the connector's list (sysfs "modes", the
// preferred one first): 1280x720 when the TV has it (a 720p stream, and the
// lightest for the GPU), else the first that is at most 1920x1080. "" if
// none: the player then takes the TV's preferred mode.
std::string tv_pick_mode(const std::vector<std::string>& modes);

// While an app uses the port, a monitor plugged in is for it, so ZeroShell
// mustn't open Dock over it. The app says so in
// $XDG_RUNTIME_DIR/zeroos-hdmi-app, "<pid> <app id>" (ZeroOS newer than
// 0.5.1 reads it; older ones open Dock anyway). Apps run inside ZeroShell,
// so the pid is ours and a file left by a crash counts for nothing.
// Claiming twice is fine; release() only removes our own claim.
std::string tv_claim_file();
void tv_claim(const std::string& app_id);
void tv_release();

// ---- A video on the TV

struct TvVideoState {
    bool open = false;       // mpv is running: loading, playing or paused
    bool paused = false;
    bool loading = false;    // starting, seeking or waiting on the network
    bool finished = false;   // played to the end (with open false)
    std::string error;       // why it stopped, when it failed (with open false)
    int pos_ms = 0;
    int dur_ms = -1;
};

struct TvVideoOptions {
    std::string url;          // a file or an http(s) address
    std::string include;      // an mpv options file (a sign-in header...), or ""
    int start_s = 0;          // where to start
};

// The program and its arguments for a video on `card`, controlled through
// the socket at `ipc`, in `mode` ("" for the TV's own).
std::vector<std::string> tv_mpv_args(const std::string& card, const std::string& mode, const std::string& ipc,
                                     const TvVideoOptions& o);

// Apply one line mpv sent on its socket (an event or a reply) to the state.
// Returns false for lines it doesn't understand.
bool tv_apply_mpv_line(TvVideoState& st, const std::string& line);

class TvVideo {
public:
    TvVideo() = default;
    ~TvVideo() { close(); }
    TvVideo(const TvVideo&) = delete;
    TvVideo& operator=(const TvVideo&) = delete;

    // Start mpv on the HDMI port. False, with why, if it can't (no TV, no
    // mpv, Dock mode has the port). It then takes a moment to start: the
    // state says loading until the first picture.
    bool open(const TvVideoOptions& o, std::string& error);
    // Read what mpv said and notice it quitting. Call it every tick (10 Hz
    // is plenty); it never waits.
    void poll();
    const TvVideoState& state() const { return st_; }
    bool is_open() const { return pid_ > 0; }

    void pause(bool paused);
    void seek(int seconds, bool absolute);   // relative unless absolute
    // The progress bar and time on the TV, for a few seconds.
    void show_progress();
    // Stop it. The state keeps where it got to.
    void close();

private:
    void connect();
    void send(const std::string& json);
    void finish(const std::string& error);

    TvVideoState st_;
    int pid_ = -1;
    int fd_ = -1;              // the IPC socket, once mpv has made it
    std::string ipc_;
    std::string in_;           // a line mpv hasn't finished yet
    std::vector<std::string> queued_;   // commands from before the socket was up
    unsigned started_at_ = 0;  // ms, for giving up on a socket that never comes
    bool ended_ = false;       // mpv said end-file
};

}  // namespace zs::kit
