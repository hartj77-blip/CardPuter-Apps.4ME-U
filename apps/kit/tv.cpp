#include "apps/kit/tv.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define ZS_TV 1
#endif

namespace zs::kit {
namespace {

std::string runtime_dir() {
    const char* run = std::getenv("XDG_RUNTIME_DIR");
    return run && *run ? run : "/tmp";
}

#ifdef ZS_TV
std::string read_line(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream f(path);
    std::vector<std::string> out;
    for (std::string s; std::getline(f, s);)
        if (!s.empty()) out.push_back(s);
    return out;
}

// The DRM devices in sysfs. ZS_TV_SYSFS points elsewhere for the tests: a
// folder laid out like it, with a stand-in TV.
std::string drm_dir() {
    const char* d = std::getenv("ZS_TV_SYSFS");
    return d && *d ? d : "/sys/class/drm";
}

std::vector<std::string> list_drm() {
    std::vector<std::string> out;
    if (DIR* d = opendir(drm_dir().c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') out.push_back(e->d_name);
        closedir(d);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The connected HDMI connector's sysfs name, "card1-HDMI-A-1", or "".
std::string hdmi_connector() {
    for (auto& n : list_drm())
        if (n.find("-HDMI-A-") != std::string::npos && read_line(drm_dir() + "/" + n + "/status") == "connected")
            return n;
    return "";
}

unsigned now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return unsigned(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// The program's full path, as execvp would find it ("" if it isn't there).
std::string find_program(const std::string& prog) {
    const char* p = std::getenv("PATH");
    std::string path = p ? p : "/usr/local/bin:/usr/bin:/bin";
    size_t at = 0;
    while (at <= path.size()) {
        size_t end = path.find(':', at);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(at, end - at);
        std::string full = (dir.empty() ? "." : dir) + "/" + prog;
        if (access(full.c_str(), X_OK) == 0) return full;
        at = end + 1;
    }
    return "";
}
#endif

// ---- Just enough JSON for mpv's IPC lines: one object, its top-level keys.
struct JVal {
    char type = 0;   // 's' string, 'n' number, 'b' bool, 'z' null, 'o' anything else
    std::string s;
    double n = 0;
    bool b = false;
};

struct JParse {
    const std::string& t;
    size_t i = 0;

    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) ++i;
    }
    bool str(std::string& out) {
        if (i >= t.size() || t[i] != '"') return false;
        ++i;
        while (i < t.size() && t[i] != '"') {
            char ch = t[i++];
            if (ch != '\\') {
                out += ch;
                continue;
            }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // Only what an error message might hold; others become '?'.
                    if (i + 4 > t.size()) return false;
                    unsigned v = unsigned(std::strtoul(t.substr(i, 4).c_str(), nullptr, 16));
                    i += 4;
                    out += v < 0x80 ? char(v) : '?';
                    break;
                }
                default: out += e;
            }
        }
        if (i >= t.size()) return false;
        ++i;
        return true;
    }
    // Skip an object or array, minding strings.
    bool nested() {
        int depth = 0;
        while (i < t.size()) {
            char ch = t[i];
            if (ch == '"') {
                std::string skip;
                if (!str(skip)) return false;
                continue;
            }
            ++i;
            if (ch == '{' || ch == '[') ++depth;
            else if ((ch == '}' || ch == ']') && --depth == 0) return true;
        }
        return false;
    }
    bool value(JVal& v) {
        ws();
        if (i >= t.size()) return false;
        char ch = t[i];
        if (ch == '"') {
            v.type = 's';
            return str(v.s);
        }
        if (ch == '{' || ch == '[') {
            v.type = 'o';
            return nested();
        }
        if (t.compare(i, 4, "true") == 0) {
            v.type = 'b', v.b = true, i += 4;
            return true;
        }
        if (t.compare(i, 5, "false") == 0) {
            v.type = 'b', v.b = false, i += 5;
            return true;
        }
        if (t.compare(i, 4, "null") == 0) {
            v.type = 'z', i += 4;
            return true;
        }
        const char* start = t.c_str() + i;
        char* end = nullptr;
        v.n = std::strtod(start, &end);
        if (end == start) return false;
        v.type = 'n';
        i += size_t(end - start);
        return true;
    }
    bool object(std::map<std::string, JVal>& out) {
        ws();
        if (i >= t.size() || t[i] != '{') return false;
        ++i;
        ws();
        if (i < t.size() && t[i] == '}') return true;
        while (i < t.size()) {
            ws();
            std::string key;
            if (!str(key)) return false;
            ws();
            if (i >= t.size() || t[i] != ':') return false;
            ++i;
            JVal v;
            if (!value(v)) return false;
            out[key] = v;
            ws();
            if (i < t.size() && t[i] == ',') {
                ++i;
                continue;
            }
            return i < t.size() && t[i] == '}';
        }
        return false;
    }
};

std::string json_str(const std::string& s) {
    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"' || ch == '\\') out += '\\', out += ch;
        else if (static_cast<unsigned char>(ch) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(static_cast<unsigned char>(ch)));
            out += buf;
        } else out += ch;
    }
    return out + "\"";
}

}  // namespace

// ---- The port

bool tv_plugged() {
#ifdef ZS_TV
    return !hdmi_connector().empty();
#else
    return false;
#endif
}

std::string tv_card() {
#ifdef ZS_TV
    auto names = list_drm();
    for (auto& c : names) {
        if (c.rfind("card", 0) != 0 || c.find('-') != std::string::npos) continue;
        for (auto& n : names)
            if (n.rfind(c + "-HDMI-A-", 0) == 0) return "/dev/dri/" + c;
    }
#endif
    return "";
}

std::string tv_pick_mode(const std::vector<std::string>& modes) {
    for (auto& m : modes)
        if (m == "1280x720") return m;
    for (auto& m : modes) {
        int w = 0, h = 0;
        char tail = 0;
        // "1920x1080" (sysfs may add an "i" for interlaced: not those).
        if (std::sscanf(m.c_str(), "%dx%d%c", &w, &h, &tail) == 2 && w > 0 && h > 0 && w <= 1920 && h <= 1080)
            return m;
    }
    return "";
}

std::string tv_claim_file() { return runtime_dir() + "/zeroos-hdmi-app"; }

void tv_claim(const std::string& app_id) {
#ifdef ZS_TV
    std::string path = tv_claim_file(), tmp = path + ".tmp";
    if (FILE* f = std::fopen(tmp.c_str(), "w")) {
        std::fprintf(f, "%ld %s\n", long(getpid()), app_id.c_str());
        std::fclose(f);
        std::rename(tmp.c_str(), path.c_str());
    }
#else
    (void)app_id;
#endif
}

void tv_release() {
#ifdef ZS_TV
    std::ifstream f(tv_claim_file());
    long pid = 0;
    if (f >> pid && pid == long(getpid())) std::remove(tv_claim_file().c_str());
#endif
}

// ---- A video on the TV

std::vector<std::string> tv_mpv_args(const std::string& card, const std::string& mode, const std::string& ipc,
                                     const TvVideoOptions& o) {
    std::vector<std::string> a = {
        "mpv",
        // The HDMI card only: the GPU scales (the drm output if it can't).
        "--vo=gpu,drm", "--gpu-context=drm", "--drm-device=" + card,
        // H.264 in hardware, as Dock's player does; the rest on the CPU.
        "--hwdec=v4l2m2m-copy", "--hwdec-codecs=h264", "--vd-lavc-threads=4",
        "--cache=yes", "--demuxer-max-bytes=32MiB", "--demuxer-max-back-bytes=8MiB",
        // Worked from the Cardputer: no keys of its own, quit at the end.
        "--input-ipc-server=" + ipc, "--input-default-bindings=no", "--input-terminal=no",
        "--idle=no", "--keep-open=no", "--force-window=yes",
        // Only errors, for the log.
        "--msg-level=all=error", "--term-osd=no",
    };
    if (!mode.empty()) a.push_back("--drm-mode=" + mode);
    if (!o.include.empty()) a.push_back("--include=" + o.include);
    if (o.start_s > 0) a.push_back("--start=" + std::to_string(o.start_s));
    a.push_back("--");
    a.push_back(o.url);
    return a;
}

bool tv_apply_mpv_line(TvVideoState& st, const std::string& line) {
    std::map<std::string, JVal> m;
    JParse p{line};
    if (!p.object(m)) return false;
    auto get = [&](const char* k) -> const JVal* {
        auto it = m.find(k);
        return it == m.end() ? nullptr : &it->second;
    };
    const JVal* ev = get("event");
    if (!ev) return get("request_id") || get("error");   // a reply to a command
    if (ev->type != 's') return false;
    const std::string& e = ev->s;
    if (e == "property-change") {
        const JVal* name = get("name");
        const JVal* data = get("data");
        if (!name || name->type != 's') return false;
        if (!data || data->type == 'z') return true;   // not known yet
        if (name->s == "time-pos" && data->type == 'n') st.pos_ms = std::max(0, int(data->n * 1000));
        else if (name->s == "duration" && data->type == 'n') st.dur_ms = int(data->n * 1000);
        else if (name->s == "pause" && data->type == 'b') st.paused = data->b;
        else if (name->s == "paused-for-cache" && data->type == 'b') st.loading = data->b;
        else return false;
        return true;
    }
    if (e == "start-file" || e == "seek") {
        st.loading = true;
        return true;
    }
    if (e == "playback-restart") {   // playing: after the start or a seek
        st.loading = false;
        return true;
    }
    if (e == "end-file") {
        const JVal* reason = get("reason");
        std::string r = reason && reason->type == 's' ? reason->s : "";
        if (r == "eof") st.finished = true;
        else if (r == "error") {
            const JVal* why = get("file_error");
            st.error = "Can't play this on the TV";
            if (why && why->type == 's' && !why->s.empty()) st.error += ": " + why->s;
        }
        return true;
    }
    return true;   // other events (file-loaded, shutdown...) change nothing here
}

#ifdef ZS_TV

bool TvVideo::open(const TvVideoOptions& o, std::string& error) {
    close();
    st_ = TvVideoState{};
    ended_ = false;
    in_.clear();
    queued_.clear();
    std::string conn = hdmi_connector();
    std::string card = tv_card();
    if (conn.empty() || card.empty()) {
        error = "Plug a TV into the HDMI port";
        return false;
    }
    std::string mpv = find_program("mpv");
    if (mpv.empty()) {
        error = "mpv isn't installed";
        return false;
    }
    std::string mode = tv_pick_mode(read_lines(drm_dir() + "/" + conn + "/modes"));
    ipc_ = runtime_dir() + "/zeroos-tv-" + std::to_string(long(getpid())) + ".sock";
    std::remove(ipc_.c_str());
    std::vector<std::string> args = tv_mpv_args(card, mode, ipc_, o);

    // Everything the child needs, made before fork: after it, only calls
    // that are safe in a copy of a threaded process.
    std::vector<char*> argv;
    for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
    argv.push_back(nullptr);
    std::string log = runtime_dir() + "/zeroos-tv-video.log";
    int out = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    int null = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    pid_t parent = getpid();

    pid_t pid = fork();
    if (pid == 0) {
        // mpv goes with ZeroShell: it mustn't keep the TV after a crash.
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parent) _exit(127);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
        }
        if (out >= 0) dup2(out, 2);
        else if (null >= 0) dup2(null, 2);
        execv(mpv.c_str(), argv.data());
        _exit(127);
    }
    if (out >= 0) ::close(out);
    if (null >= 0) ::close(null);
    if (pid < 0) {
        error = "Can't start the TV player";
        return false;
    }
    pid_ = pid;
    started_at_ = now_ms();
    st_.open = true;
    st_.loading = true;
    st_.pos_ms = o.start_s > 0 ? o.start_s * 1000 : 0;
    return true;
}

void TvVideo::connect() {
    if (fd_ >= 0 || pid_ <= 0) return;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, ipc_.c_str(), sizeof addr.sun_path - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
        ::close(fd);
        return;   // not made yet: next time
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fd_ = fd;
    int id = 1;
    for (const char* prop : {"time-pos", "duration", "pause", "paused-for-cache"})
        send("{\"command\":[\"observe_property\"," + std::to_string(id++) + "," + json_str(prop) + "]}");
    auto q = std::move(queued_);
    queued_.clear();
    for (auto& c : q) send(c);
}

void TvVideo::send(const std::string& json) {
    if (fd_ < 0) {
        if (pid_ > 0) queued_.push_back(json);
        return;
    }
    std::string line = json + "\n";
    size_t at = 0;
    while (at < line.size()) {
        ssize_t n = ::send(fd_, line.data() + at, line.size() - at, MSG_NOSIGNAL);
        if (n <= 0) return;   // mpv is going away; poll() will notice
        at += size_t(n);
    }
}

void TvVideo::poll() {
    if (pid_ <= 0) return;
    connect();
    if (fd_ < 0 && now_ms() - started_at_ > 15000) {
        close();
        st_.error = "The TV player didn't start";
        return;
    }
    if (fd_ >= 0) {
        char buf[4096];
        for (;;) {
            ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
            if (n <= 0) break;
            in_.append(buf, size_t(n));
        }
        size_t nl;
        while ((nl = in_.find('\n')) != std::string::npos) {
            std::string line = in_.substr(0, nl);
            in_.erase(0, nl + 1);
            TvVideoState before = st_;
            tv_apply_mpv_line(st_, line);
            if (st_.finished != before.finished || st_.error != before.error) ended_ = true;
        }
        if (in_.size() > 65536) in_.clear();   // not mpv talking
    }
    int status = 0;
    if (waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        std::string why;
        if (!ended_ && st_.error.empty() && !st_.finished) {
            // It quit without saying why: the last error it logged.
            why = "The TV player stopped";
            auto lines = read_lines(runtime_dir() + "/zeroos-tv-video.log");
            if (!lines.empty()) why += ": " + lines.back().substr(0, 100);
        }
        finish(why);
    }
}

void TvVideo::finish(const std::string& error) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    if (!ipc_.empty()) std::remove(ipc_.c_str());
    st_.open = false;
    st_.loading = false;
    if (!error.empty()) st_.error = error;
}

void TvVideo::pause(bool paused) {
    st_.paused = paused;
    send(std::string("{\"command\":[\"set_property\",\"pause\",") + (paused ? "true" : "false") + "]}");
}

void TvVideo::seek(int seconds, bool absolute) {
    if (absolute) st_.pos_ms = std::max(0, seconds * 1000);
    send("{\"command\":[\"seek\"," + std::to_string(seconds) + "," + json_str(absolute ? "absolute" : "relative") +
         "]}");
}

void TvVideo::show_progress() { send("{\"command\":[\"show-progress\"]}"); }

void TvVideo::close() {
    if (pid_ <= 0) return;
    // Asked to quit, it lets go of the TV in a moment; with no socket yet
    // it is told firmly straight away.
    int status = 0;
    bool gone = false;
    if (fd_ >= 0) send("{\"command\":[\"quit\"]}");
    for (int i = 0; i < 50 && !gone && fd_ >= 0; ++i) {
        if (waitpid(pid_, &status, WNOHANG) == pid_) gone = true;
        else usleep(20000);
    }
    if (!gone) {
        kill(pid_, SIGTERM);
        for (int i = 0; i < 50 && !gone; ++i) {
            if (waitpid(pid_, &status, WNOHANG) == pid_) gone = true;
            else usleep(20000);
        }
    }
    if (!gone) {
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
    }
    pid_ = -1;
    queued_.clear();
    finish("");
}

#else   // no TV here (the browser)

bool TvVideo::open(const TvVideoOptions&, std::string& error) {
    error = "No HDMI port here";
    return false;
}
void TvVideo::poll() {}
void TvVideo::connect() {}
void TvVideo::send(const std::string&) {}
void TvVideo::finish(const std::string&) {}
void TvVideo::pause(bool) {}
void TvVideo::seek(int, bool) {}
void TvVideo::show_progress() {}
void TvVideo::close() {}

#endif

}  // namespace zs::kit
