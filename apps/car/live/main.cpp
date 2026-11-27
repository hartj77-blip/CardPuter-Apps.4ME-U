// zeroos-car-live: serves the made-up stick of live_stick.h as one file,
// DIR/stick.img, which the USB mass-storage gadget offers to the car
// (files/zeroos-usb-stick sets that up; README.md explains the mode).
//
//   zeroos-car-live [--kbps N] [--backlog-ms N] [--no-keys] DIR -- ENCODER...
//
// ENCODER is a command that writes MP3 at N kbit/s, 44.1 kHz, to its
// output, for as long as it runs: the sound to send. It is started again
// if it stops, and the car hears silence meanwhile.
//
// The file is a FUSE mount, spoken to through /dev/fuse directly (it is one
// read-only file; no library needed). It is opened without the kernel's
// cache in between, so every read the car makes arrives here once, as it
// happens, and what the car reads twice it gets fresh.
//
// The car's next, previous and pause (worked out by LiveStick from how it
// reads) leave as key presses of a made-up keyboard (uinput), which
// ZeroShell reads like a Bluetooth car's buttons.
//
// Runs as root (it mounts), started by zeroos-car-live.service.
#include <dirent.h>
#include <fcntl.h>
#include <linux/fuse.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "car_keys.h"
#include "live_stick.h"

using namespace car_live;
using car_keys::open_keys;
using car_keys::press;

namespace {

const char kFileName[] = "stick.img";
constexpr uint64_t kRootNode = 1, kFileNode = 2;   // FUSE's root is always 1

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// --- The encoder: its output into the stream, silence when it has none ---

// Starts ENCODER with its output on a pipe. Returns the pipe's read end.
int start_encoder(char** argv, pid_t& pid) {
    int p[2];
    if (pipe2(p, O_CLOEXEC) != 0) return -1;
    pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        setpgid(0, 0);                       // so the whole pipeline can be told to stop
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        dup2(p[1], STDOUT_FILENO);
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) dup2(null, STDIN_FILENO);
        signal(SIGPIPE, SIG_DFL);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(p[1]);
    return p[0];
}

void stop_encoder(pid_t pid) {
    if (pid <= 0) return;
    kill(-pid, SIGTERM);
    kill(pid, SIGTERM);
    for (int i = 0; i < 20 && waitpid(pid, nullptr, WNOHANG) == 0; ++i) usleep(50 * 1000);
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

// Keeps the stream growing at the speed of sound: the encoder's frames as
// they come (never faster than they play, so a car can't be fed the
// future), and silent frames whenever the encoder has gone quiet. Only
// whole frames go in: the encoder stops mid-frame when the sound does, and
// silence put into the middle of a frame would come out as a click.
void pump(Stream& stream, int kbps, char** encoder) {
    const std::vector<uint8_t> quiet = silent_frame(kbps);
    const double byte_rate = kbps * 125.0;
    pid_t pid = -1;
    int fd = -1;
    int64_t retry_at = 0, last_data = now_ms();
    int64_t t0 = now_ms();
    const double allowed = 32 * 1024;   // bytes the encoder may be ahead of the clock
    uint64_t taken = 0;
    double silence_due = 0;
    int64_t silence_clock = now_ms();
    std::vector<uint8_t> buf(16384);
    std::vector<uint8_t> part;   // the start of a frame the encoder hasn't finished
    bool was_quiet = false;
    while (!g_stop) {
        int64_t now = now_ms();
        if (fd < 0 && now >= retry_at) {
            fd = start_encoder(encoder, pid);
            if (fd < 0) retry_at = now + 2000;
        }
        bool got = false;
        // Reading waits while the encoder is ahead of the clock.
        bool ahead = double(taken) > (now - t0) / 1000.0 * byte_rate * 1.02 + allowed;
        if (fd >= 0 && !ahead) {
            pollfd p{fd, POLLIN, 0};
            if (poll(&p, 1, 100) > 0) {
                ssize_t n = read(fd, buf.data(), buf.size());
                if (n > 0) {
                    part.insert(part.end(), buf.data(), buf.data() + n);
                    size_t at = 0;
                    while (at + 4 <= part.size()) {
                        int len = mp3_frame_bytes(part.data() + at);
                        if (len == 0) {
                            ++at;   // not a frame: skip to the next one
                        } else if (at + size_t(len) <= part.size()) {
                            stream.push(part.data() + at, size_t(len));
                            at += size_t(len);
                        } else {
                            break;
                        }
                    }
                    part.erase(part.begin(), part.begin() + long(at));
                    taken += uint64_t(n);
                    last_data = now_ms();
                    got = true;
                } else if (n == 0 || (errno != EINTR && errno != EAGAIN)) {
                    close(fd);
                    fd = -1;
                    part.clear();
                    stop_encoder(pid);
                    pid = -1;
                    retry_at = now_ms() + 1000;
                    fprintf(stderr, "zeroos-car-live: the encoder stopped; starting it again\n");
                }
            }
        } else {
            usleep(20 * 1000);
        }
        // Nothing to read for a third of a second: silence, as fast as it
        // plays. (Not while we hold the encoder back: it has sound waiting.)
        now = now_ms();
        bool is_quiet = !got && !ahead && now - last_data > 300;
        if (is_quiet) {
            if (!was_quiet) silence_clock = now;
            silence_due += (now - silence_clock) / 1000.0 * byte_rate;
            while (silence_due >= double(quiet.size())) {
                stream.push(quiet.data(), quiet.size());
                silence_due -= double(quiet.size());
            }
        } else {
            silence_due = 0;
        }
        silence_clock = now;
        was_quiet = is_quiet;
    }
    if (fd >= 0) close(fd);
    stop_encoder(pid);
}

// --- FUSE: a folder with one read-only file ------------------------------

void fill_attr(fuse_attr& a, uint64_t node, uint64_t size) {
    memset(&a, 0, sizeof a);
    a.ino = node;
    a.nlink = 1;
    a.blksize = 4096;
    if (node == kRootNode) {
        a.mode = S_IFDIR | 0555;
        a.nlink = 2;
    } else {
        a.mode = S_IFREG | 0444;
        a.size = size;
        a.blocks = size / 512;
    }
}

void reply(int fd, uint64_t unique, int error, const void* body = nullptr, size_t len = 0) {
    fuse_out_header h{};
    h.len = uint32_t(sizeof h + (error ? 0 : len));
    h.error = error;
    h.unique = unique;
    iovec v[2] = {{&h, sizeof h}, {const_cast<void*>(body), len}};
    if (writev(fd, v, error || len == 0 ? 1 : 2) < 0 && errno != ENOENT)   // ENOENT: the asker gave up
        fprintf(stderr, "zeroos-car-live: reply failed: %s\n", strerror(errno));
}

int usage() {
    fprintf(stderr, "usage: zeroos-car-live [--kbps N] [--backlog-ms N] [--no-keys] DIR -- ENCODER...\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    Config cfg;
    bool keys = true;
    std::string dir;
    char** encoder = nullptr;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--") {
            encoder = argv + i + 1;
            break;
        } else if (a == "--kbps" && i + 1 < argc) {
            cfg.kbps = atoi(argv[++i]);
        } else if (a == "--backlog-ms" && i + 1 < argc) {
            cfg.backlog_ms = atoi(argv[++i]);
        } else if (a == "--no-keys") {
            keys = false;
        } else if (dir.empty() && a[0] != '-') {
            dir = a;
        } else {
            return usage();
        }
    }
    if (dir.empty() || !encoder || !encoder[0]) return usage();
    if (mp3_frame_bytes(silent_frame(cfg.kbps).data()) != 144000 * cfg.kbps / 44100 || cfg.kbps < 96) {
        fprintf(stderr, "zeroos-car-live: %d isn't an MP3 bit rate of 96 or more\n", cfg.kbps);
        return 2;
    }
    if (cfg.backlog_ms < 500 || cfg.backlog_ms > 10000) {
        fprintf(stderr, "zeroos-car-live: the backlog is 500 to 10000 ms\n");
        return 2;
    }

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    int fuse = open("/dev/fuse", O_RDWR | O_CLOEXEC);
    if (fuse < 0) {
        perror("zeroos-car-live: /dev/fuse");
        return 1;
    }
    umount2(dir.c_str(), MNT_DETACH);   // one left by a run that was killed
    mkdir(dir.c_str(), 0755);
    char opts[128];
    snprintf(opts, sizeof opts, "fd=%d,rootmode=40000,user_id=%d,group_id=%d", fuse, int(getuid()), int(getgid()));
    if (mount("zeroos-car-live", dir.c_str(), "fuse.zeroos-car-live", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RDONLY, opts) != 0) {
        fprintf(stderr, "zeroos-car-live: can't mount on %s: %s\n", dir.c_str(), strerror(errno));
        return 1;
    }

    Stream stream;
    LiveStick stick(stream, cfg);
    std::thread pump_thread(pump, std::ref(stream), cfg.kbps, encoder);
    int key_fd = keys ? open_keys() : -1;
    if (keys && key_fd < 0) fprintf(stderr, "zeroos-car-live: no /dev/uinput, so the car's buttons won't work\n");

    // A request is a header and at most a name; a read's answer is the
    // largest thing we send.
    std::vector<uint8_t> in(FUSE_MIN_READ_BUFFER + 4096);
    std::vector<uint8_t> out;
    bool was_playing = false;
    while (!g_stop) {
        pollfd p{fuse, POLLIN, 0};
        int ready = poll(&p, 1, 250);
        if (ready > 0) {
            ssize_t n = read(fuse, in.data(), in.size());
            if (n < 0) {
                if (errno == ENODEV) break;   // unmounted
                if (errno == EINTR || errno == EAGAIN || errno == ENOENT) continue;
                perror("zeroos-car-live: reading /dev/fuse");
                break;
            }
            if (size_t(n) < sizeof(fuse_in_header)) continue;
            const fuse_in_header& h = *reinterpret_cast<const fuse_in_header*>(in.data());
            const uint8_t* arg = in.data() + sizeof h;
            switch (h.opcode) {
                case FUSE_INIT: {
                    const fuse_init_in& i = *reinterpret_cast<const fuse_init_in*>(arg);
                    fuse_init_out o{};
                    o.major = FUSE_KERNEL_VERSION;
                    o.minor = std::min<uint32_t>(i.minor, FUSE_KERNEL_MINOR_VERSION);
                    o.max_write = 4096;
                    o.time_gran = 1;
                    // An older kernel knows a shorter answer; it takes the first 24 bytes.
                    reply(fuse, h.unique, 0, &o, i.minor < 23 ? 24 : sizeof o);
                    break;
                }
                case FUSE_GETATTR: {
                    if (h.nodeid != kRootNode && h.nodeid != kFileNode) {
                        reply(fuse, h.unique, -ENOENT);
                        break;
                    }
                    fuse_attr_out o{};
                    o.attr_valid = 3600;
                    fill_attr(o.attr, h.nodeid, stick.size());
                    reply(fuse, h.unique, 0, &o, sizeof o);
                    break;
                }
                case FUSE_LOOKUP: {
                    in[size_t(n) - 1] = 0;
                    if (h.nodeid != kRootNode || strcmp(reinterpret_cast<const char*>(arg), kFileName) != 0) {
                        reply(fuse, h.unique, -ENOENT);
                        break;
                    }
                    fuse_entry_out o{};
                    o.nodeid = kFileNode;
                    o.generation = 1;
                    o.entry_valid = o.attr_valid = 3600;
                    fill_attr(o.attr, kFileNode, stick.size());
                    reply(fuse, h.unique, 0, &o, sizeof o);
                    break;
                }
                case FUSE_OPEN: {
                    const fuse_open_in& i = *reinterpret_cast<const fuse_open_in*>(arg);
                    if (h.nodeid != kFileNode) {
                        reply(fuse, h.unique, -EISDIR);
                    } else if ((i.flags & O_ACCMODE) != O_RDONLY) {
                        reply(fuse, h.unique, -EROFS);
                    } else {
                        fuse_open_out o{};
                        o.open_flags = FOPEN_DIRECT_IO;   // no cache: each read comes here
                        reply(fuse, h.unique, 0, &o, sizeof o);
                    }
                    break;
                }
                case FUSE_READ: {
                    const fuse_read_in& i = *reinterpret_cast<const fuse_read_in*>(arg);
                    if (h.nodeid != kFileNode) {
                        reply(fuse, h.unique, -EISDIR);
                        break;
                    }
                    uint64_t size = stick.size();
                    size_t len = i.offset >= size ? 0 : size_t(std::min<uint64_t>(i.size, size - i.offset));
                    out.resize(len);
                    if (len) stick.read(i.offset, out.data(), len, now_ms());
                    reply(fuse, h.unique, 0, out.data(), len);
                    break;
                }
                case FUSE_OPENDIR: {
                    fuse_open_out o{};
                    reply(fuse, h.unique, h.nodeid == kRootNode ? 0 : -ENOTDIR, &o, sizeof o);
                    break;
                }
                case FUSE_READDIR: {
                    const fuse_read_in& i = *reinterpret_cast<const fuse_read_in*>(arg);
                    out.clear();
                    if (i.offset == 0) {
                        const struct { const char* name; uint64_t node; uint32_t type; } entries[] = {
                            {".", kRootNode, DT_DIR}, {"..", kRootNode, DT_DIR}, {kFileName, kFileNode, DT_REG}};
                        uint64_t next = 1;
                        for (auto& e : entries) {
                            fuse_dirent d{};
                            d.ino = e.node;
                            d.off = next++;
                            d.namelen = uint32_t(strlen(e.name));
                            d.type = e.type;
                            size_t at = out.size();
                            out.resize(at + FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + d.namelen), 0);
                            memcpy(out.data() + at, &d, FUSE_NAME_OFFSET);
                            memcpy(out.data() + at + FUSE_NAME_OFFSET, e.name, d.namelen);
                        }
                    }
                    if (out.size() > i.size) out.clear();
                    reply(fuse, h.unique, 0, out.data(), out.size());
                    break;
                }
                case FUSE_STATFS: {
                    fuse_statfs_out o{};
                    o.st.bsize = o.st.frsize = 512;
                    o.st.blocks = stick.size() / 512;
                    o.st.namelen = 255;
                    reply(fuse, h.unique, 0, &o, sizeof o);
                    break;
                }
                case FUSE_RELEASE:
                case FUSE_RELEASEDIR:
                case FUSE_FLUSH:
                case FUSE_ACCESS:
                    reply(fuse, h.unique, 0);
                    break;
                case FUSE_FORGET:
                case FUSE_BATCH_FORGET:
                case FUSE_INTERRUPT:
                    break;   // these get no answer
                case FUSE_DESTROY:
                    reply(fuse, h.unique, 0);
                    g_stop = true;
                    break;
                default:
                    reply(fuse, h.unique, -ENOSYS);
            }
        } else if (ready < 0 && errno != EINTR) {
            break;
        }
        stick.tick(now_ms());
        for (Event e : stick.take_events()) {
            const char* name = e == Event::Next ? "next" : e == Event::Previous ? "previous" : e == Event::Pause ? "pause" : "play";
            fprintf(stderr, "zeroos-car-live: the car says %s\n", name);
            press(key_fd, e == Event::Next ? KEY_NEXTSONG : e == Event::Previous ? KEY_PREVIOUSSONG
                          : e == Event::Pause ? KEY_PAUSECD : KEY_PLAYCD);
        }
        if (stick.car_playing() != was_playing) {
            was_playing = stick.car_playing();
            if (was_playing) fprintf(stderr, "zeroos-car-live: the car is playing\n");
        }
    }
    g_stop = true;
    pump_thread.join();
    umount2(dir.c_str(), MNT_DETACH);
    close(fuse);
    car_keys::close_keys(key_fd);
    return 0;
}
