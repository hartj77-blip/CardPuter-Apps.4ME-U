// Desktop: remote desktop to your other computers, VNC or RDP.
//
//   List     your saved computers, then Tailscale devices not saved yet (a
//            List screen: typing filters). Enter connects on the built-in
//            screen; the menu has N New, E Edit, M Open on the monitor (Dock
//            mode) and D Delete.
//   Edit     name, address, VNC/RDP, user, password, RDP size, domain, as
//            rows; the menu has D Delete. Esc saves and goes back.
//   Viewer   the remote screen in the 320 x 150 app area, like a Game
//            screen: no soft-key bar, and keys type on the remote computer.
//            Esc opens a menu of the rest (mouse mode, zoom, Ctrl/Alt/Win,
//            Ctrl+Alt+Del, F-keys, leave).
//
// The connection runs in zeroos-desktop-view (view/view.cpp), a process of
// its own: it hands the screen over in shared memory (frame.h) and takes
// keys and pointer moves as lines on a socket.
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/desktop/frame.h"
#include "apps/desktop/hosts.h"
#include "ui/list_view.h"
#include "ui/power.h"
#include "ui/prefs.h"
#include "ui/remote.h"

extern char** environ;

namespace zs {

namespace {
using namespace theme;
using namespace desktop;

constexpr int kViewW = kScreenW;    // 320
constexpr int kViewH = kContentH;   // 150

std::string viewer_path() {
    if (const char* e = std::getenv("ZS_DESKTOP_VIEW"); e && e[0]) return e;
    const char* installed = "/usr/lib/zeroos/zeroos-desktop-view";
    if (access(installed, X_OK) == 0) return installed;
#ifdef ZS_DESKTOP_VIEW_DEV
    if (access(ZS_DESKTOP_VIEW_DEV, X_OK) == 0) return ZS_DESKTOP_VIEW_DEV;
#endif
    return "";
}

// Run a program in the background, detached from ZeroShell (the monitor's
// viewers). False if it couldn't start.
bool spawn_detached(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, argv[0], &fa, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) return false;
    // The script backgrounds the viewer and exits at once; reap it.
    for (int i = 0; i < 50; ++i) {
        if (waitpid(pid, nullptr, WNOHANG) != 0) break;
        usleep(10000);
    }
    return true;
}

// ---- The viewer -----------------------------------------------------------

// A small arrow for the pointer: X = outline, o = fill.
const char* const kCursor[] = {
    "X", "XX", "XoX", "XooX", "XoooX", "XooooX", "XoooooX", "XooooooX",
    "XoooooXXX", "XooXooX", "XoX XooX", "XX  XooX", "X    XX",
};

class Viewer {
public:
    enum class End { None, Quit, Error, Auth };

    Viewer(lv_obj_t* parent, const Host& h, const std::string& pass) : host_(h) {
        zoom_ = prefs::get_int("desktop.zoom", 0) % 3;
        pixels_.assign(size_t(kViewW) * kViewH, 0);

        box_ = lv_obj_create(parent);
        style_plain(box_);
        lv_obj_set_pos(box_, 0, 0);
        lv_obj_set_size(box_, kViewW, kViewH);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(box_, c(0x000000), 0);
        lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);

        canvas_ = lv_canvas_create(box_);
        lv_canvas_set_buffer(canvas_, pixels_.data(), kViewW, kViewH, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(canvas_, 0, 0);

        status_ = label(box_, "", &zs_font_14, kText);
        lv_obj_set_width(status_, kViewW - 20);
        lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(status_);

        hint_ = label(box_, "", &zs_font_12, kText);
        lv_obj_set_width(hint_, kViewW);
        lv_label_set_long_mode(hint_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_bg_color(hint_, c(0x000000), 0);
        lv_obj_set_style_bg_opa(hint_, LV_OPA_80, 0);
        lv_obj_set_style_pad_all(hint_, 3, 0);
        lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);

        mods_ = label(box_, "", &zs_font_12, kOnAccent);
        lv_obj_set_style_bg_color(mods_, c(kAccent), 0);
        lv_obj_set_style_bg_opa(mods_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(mods_, 4, 0);
        lv_obj_align(mods_, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_add_flag(mods_, LV_OBJ_FLAG_HIDDEN);

        start(pass);
        timer_ = lv_timer_create(on_timer, 33, this);
    }

    ~Viewer() {
        if (timer_) lv_timer_delete(timer_);
        stop();
        unmap();
        if (box_) lv_obj_delete(box_);
    }

    // Done: why it ended (the app reads it after the process has gone).
    End ended() const { return end_; }
    const std::string& message() const { return message_; }

    bool key(uint32_t k) {
        last_key_ms_ = lv_tick_get();
        if (fkey_) return fkey_pick(k);
        if (menu_) return menu_pick(k);
        if (k == 27) {
            show_menu();
            return true;
        }
        if (!connected_) return true;
        if (mouse_ && mouse_key(k)) return true;
        type(keysym_for_key(k));
        return true;
    }

private:
    // ---- The connection process ----

    void start(const std::string& pass) {
        std::string path = viewer_path();
        if (path.empty()) {
            fail(End::Error, "The viewer (zeroos-desktop-view) is missing: reinstall Desktop from Apps");
            return;
        }
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
            fail(End::Error, "Couldn't start the viewer");
            return;
        }
        std::string log = home_dir() + "/.cache/zeroos";
        mkdir((home_dir() + "/.cache").c_str(), 0755);
        mkdir(log.c_str(), 0755);
        log += "/desktop-view.log";
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, sv[1], 0);
        posix_spawn_file_actions_adddup2(&fa, sv[1], 1);
        posix_spawn_file_actions_addopen(&fa, 2, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        std::string proto = host_.proto == Proto::Vnc ? "vnc" : "rdp";
        char* argv[] = {const_cast<char*>(path.c_str()), const_cast<char*>(proto.c_str()),
                        const_cast<char*>(host_.address.c_str()), nullptr};
        int rc = posix_spawn(&pid_, path.c_str(), &fa, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&fa);
        close(sv[1]);
        if (rc != 0) {
            close(sv[0]);
            pid_ = 0;
            fail(End::Error, "Couldn't start the viewer");
            return;
        }
        fd_ = sv[0];
        fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
        const RdpSize& sz = rdp_sizes()[size_t(std::clamp(host_.size, 0, int(rdp_sizes().size()) - 1))];
        std::string hello;
        if (!host_.user.empty()) hello += "user " + clean_field(host_.user) + "\n";
        if (!pass.empty()) hello += "pass " + clean_field(pass) + "\n";
        if (!host_.domain.empty()) hello += "domain " + clean_field(host_.domain) + "\n";
        hello += "size " + std::to_string(sz.w) + "x" + std::to_string(sz.h) + "\ngo\n";
        send_line(hello);
        set_status("Connecting to " + host_.name + "...");
    }

    void stop() {
        if (fd_ >= 0) {
            send_line("q\n");
            close(fd_);
            fd_ = -1;
        }
        if (pid_ > 0) {
            // It leaves on "q" or when the socket closes; make sure.
            for (int i = 0; i < 30; ++i) {
                if (waitpid(pid_, nullptr, WNOHANG) != 0) {
                    pid_ = 0;
                    break;
                }
                usleep(10000);
            }
            if (pid_ > 0) {
                kill(pid_, SIGKILL);
                waitpid(pid_, nullptr, 0);
                pid_ = 0;
            }
        }
    }

    void send_line(const std::string& s) {
        if (fd_ < 0) return;
        // MSG_NOSIGNAL: a viewer that has gone must not take ZeroShell with it.
        ssize_t n = send(fd_, s.data(), s.size(), MSG_NOSIGNAL);
        (void)n;
    }

    void fail(End why, const std::string& msg) {
        if (end_ == End::None) {
            end_ = why;
            message_ = msg;
        }
    }

    void poll_process() {
        if (fd_ < 0) return;
        char buf[1024];
        bool gone = false;
        while (true) {
            ssize_t n = recv(fd_, buf, sizeof buf, 0);
            if (n > 0) {
                in_.append(buf, size_t(n));
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            gone = n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK);
            break;
        }
        // Its last words first: they say why it ended.
        size_t nl;
        while ((nl = in_.find('\n')) != std::string::npos) {
            std::string l = in_.substr(0, nl);
            in_.erase(0, nl + 1);
            line(l);
        }
        if (gone) {
            close(fd_);
            fd_ = -1;
            if (pid_ > 0) {
                waitpid(pid_, nullptr, 0);
                pid_ = 0;
            }
            fail(connected_ ? End::Quit : End::Error, connected_ ? "Disconnected" : "Couldn't connect");
        }
    }

    void line(const std::string& l) {
        if (l.rfind("status ", 0) == 0) {
            if (!connected_) set_status(l.substr(7));
        } else if (l == "connected") {
            connected_ = true;
            set_status("");
            hint(mouse_ ? mouse_hint() : "Esc: menu (mouse, zoom, Ctrl/Alt, leave)", 3000);
        } else if (l.rfind("frame ", 0) == 0) {
            map(l.substr(6));
        } else if (l.rfind("error auth", 0) == 0) {
            fail(End::Auth, l.size() > 11 ? l.substr(11) : "Password needed");
        } else if (l.rfind("error ", 0) == 0) {
            fail(End::Error, l.substr(6));
        }
    }

    // "PATH W H": map the new frame, then let go of the old one.
    void map(const std::string& arg) {
        size_t sp = arg.find(' ');
        if (sp == std::string::npos) return;
        std::string path = arg.substr(0, sp);
        if (path.rfind("/dev/shm/zeroos-desktop-", 0) != 0) return;
        int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return;
        struct stat st {};
        void* m = MAP_FAILED;
        if (fstat(fd, &st) == 0 && size_t(st.st_size) >= sizeof(FrameHeader))
            m = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        unlink(path.c_str());   // the mapping keeps it; nothing is left behind
        if (m == MAP_FAILED) return;
        auto* h = static_cast<const FrameHeader*>(m);
        if (__atomic_load_n(&h->magic, __ATOMIC_ACQUIRE) != kFrameMagic || h->width == 0 || h->height == 0 ||
            frame_bytes(h->width, h->height) > uint64_t(st.st_size) || h->stride != h->width * 2) {
            munmap(m, size_t(st.st_size));
            return;
        }
        unmap();
        frame_ = h;
        frame_size_ = size_t(st.st_size);
        fw_ = int(h->width);
        fh_ = int(h->height);
        seen_seq_ = ~0u;
        if (px_ < 0 || px_ >= fw_ || py_ < 0 || py_ >= fh_) {
            px_ = fw_ / 2;
            py_ = fh_ / 2;
        }
        place_view();
    }

    void unmap() {
        if (frame_) munmap(const_cast<FrameHeader*>(frame_), frame_size_);
        frame_ = nullptr;
    }

    // ---- Drawing ----

    double scale() const {
        if (!frame_) return 1;
        if (zoom_ == 1) return 1;
        if (zoom_ == 2) return 2;
        return std::min(double(kViewW) / fw_, double(kViewH) / fh_);
    }

    // Where the view is: centred when it all fits, else following the pointer.
    void place_view() {
        if (!frame_) return;
        double s = scale();
        double vw = kViewW / s, vh = kViewH / s;
        auto axis = [](double& v, double size, double full, int p) {
            if (size >= full) {
                v = (full - size) / 2;   // negative: borders
                return;
            }
            double margin = size / 5;
            if (p < v + margin) v = p - margin;
            if (p > v + size - margin) v = p - size + margin;
            v = std::clamp(v, 0.0, full - size);
        };
        axis(vx_, vw, fw_, px_);
        axis(vy_, vh, fh_, py_);
        dirty_ = true;
    }

    void draw() {
        uint16_t* out = pixels_.data();
        if (!frame_) return;
        const uint16_t* src = frame_pixels(frame_);
        double s = scale();
        // 16.16 fixed point: source position of each output pixel's centre.
        const int64_t step = int64_t(65536.0 / s);
        const int64_t x0 = int64_t(vx_ * 65536.0) + step / 2;
        const int64_t y0 = int64_t(vy_ * 65536.0) + step / 2;
        const bool smooth = step >= 2 * 65536;   // shrinking: average 4 samples
        const int64_t q = step / 4;
        auto at = [&](int64_t fx, int64_t fy) -> uint16_t {
            int x = int(fx >> 16), y = int(fy >> 16);
            x = std::clamp(x, 0, fw_ - 1);
            y = std::clamp(y, 0, fh_ - 1);
            return src[size_t(y) * size_t(fw_) + size_t(x)];
        };
        for (int dy = 0; dy < kViewH; ++dy) {
            int64_t fy = y0 + dy * step;
            uint16_t* row = out + size_t(dy) * kViewW;
            int sy = int(fy >> 16);
            if (fy < 0 || sy >= fh_) {
                std::fill(row, row + kViewW, 0);
                continue;
            }
            for (int dx = 0; dx < kViewW; ++dx) {
                int64_t fx = x0 + dx * step;
                int sx = int(fx >> 16);
                if (fx < 0 || sx >= fw_) {
                    row[dx] = 0;
                    continue;
                }
                if (!smooth) {
                    row[dx] = src[size_t(sy) * size_t(fw_) + size_t(sx)];
                    continue;
                }
                uint16_t a = at(fx - q, fy - q), b = at(fx + q, fy - q), cc = at(fx - q, fy + q),
                         d = at(fx + q, fy + q);
                uint32_t r = ((a >> 11) + (b >> 11) + (cc >> 11) + (d >> 11)) >> 2;
                uint32_t g = (((a >> 5) & 63) + ((b >> 5) & 63) + ((cc >> 5) & 63) + ((d >> 5) & 63)) >> 2;
                uint32_t bl = ((a & 31) + (b & 31) + (cc & 31) + (d & 31)) >> 2;
                row[dx] = uint16_t((r << 11) | (g << 5) | bl);
            }
        }
        // The pointer, where the remote one is. VNC servers mostly draw
        // their own; RDP's is never in the picture.
        if (host_.proto == Proto::Vnc && !mouse_) return;
        int cx = int((px_ - vx_) * s), cy = int((py_ - vy_) * s);
        for (int r = 0; r < int(sizeof kCursor / sizeof kCursor[0]); ++r) {
            for (int i = 0; kCursor[r][i]; ++i) {
                char ch = kCursor[r][i];
                if (ch == ' ') continue;
                int x = cx + i, y = cy + r;
                if (x < 0 || y < 0 || x >= kViewW || y >= kViewH) continue;
                out[size_t(y) * kViewW + size_t(x)] = ch == 'X' ? 0x0000 : (mouse_ ? 0xffff : 0xc618);
            }
        }
    }

    static void on_timer(lv_timer_t* t) {
        auto* self = static_cast<Viewer*>(lv_timer_get_user_data(t));
        self->poll_process();
        if (self->frame_) {
            uint32_t seq = __atomic_load_n(&self->frame_->seq, __ATOMIC_ACQUIRE);
            if (seq != self->seen_seq_) {
                self->seen_seq_ = seq;
                self->dirty_ = true;
            }
        }
        if (self->dirty_) {
            self->dirty_ = false;
            self->draw();
            lv_obj_invalidate(self->canvas_);
        }
        if (self->hint_until_ && lv_tick_elaps(self->hint_since_) > self->hint_until_ && !self->menu_ && !self->fkey_) {
            self->hint_until_ = 0;
            lv_obj_add_flag(self->hint_, LV_OBJ_FLAG_HIDDEN);
        }
        if (self->connected_ || self->fd_ >= 0) remote_note_use();
    }

    void set_status(const std::string& s) {
        lv_label_set_text(status_, s.c_str());
        if (s.empty()) lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(status_, LV_OBJ_FLAG_HIDDEN);
    }

    // A note over the bottom of the screen; ms = 0 keeps it up.
    void hint(const std::string& s, uint32_t ms) {
        lv_label_set_text(hint_, s.c_str());
        lv_obj_remove_flag(hint_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(hint_, LV_ALIGN_BOTTOM_MID, 0, 0);
        hint_since_ = lv_tick_get();
        hint_until_ = ms;
    }
    void hide_hint() {
        hint_until_ = 0;
        lv_obj_add_flag(hint_, LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Keys ----

    static const char* mouse_hint() {
        return "Mouse: arrows move, Enter click, R right-click, D drag, W/S scroll. Esc M: keyboard";
    }

    void show_menu() {
        menu_ = true;
        hint("Esc: send Esc   Q: leave   M: mouse   Z: zoom\n"
             "C/A/S/W: next key with Ctrl/Alt/Shift/Win\n"
             "D: Ctrl+Alt+Del   F: F1-F12   G: `   Tab: Shift+Tab", 0);
    }

    bool menu_pick(uint32_t k) {
        menu_ = false;
        hide_hint();
        if (k >= 'A' && k <= 'Z') k = k - 'A' + 'a';
        switch (k) {
            case 27: tap(ks::Escape); break;
            case 'q': fail(End::Quit, ""); break;
            case 'm':
                mouse_ = !mouse_;
                if (mouse_) {
                    if (frame_) place_view();
                    hint(mouse_hint(), 4000);
                } else {
                    release_drag();
                    hint("Keyboard: keys type on the computer", 2000);
                }
                dirty_ = true;
                break;
            case 'z': {
                zoom_ = (zoom_ + 1) % 3;
                prefs::set_int("desktop.zoom", zoom_);
                place_view();
                static const char* const kNames[] = {"Zoom: whole screen", "Zoom: 1:1", "Zoom: 2x"};
                hint(kNames[zoom_], 1500);
                break;
            }
            case 'c': add_mod(ks::ControlL); break;
            case 'a': add_mod(ks::AltL); break;
            case 's': add_mod(ks::ShiftL); break;
            case 'w': add_mod(ks::SuperL); break;
            case 'd': {
                std::vector<uint32_t> keep = mods_held_;
                mods_held_ = {ks::ControlL, ks::AltL};
                type(ks::Delete);
                mods_held_ = keep;
                show_mods();
                break;
            }
            case 'f':
                fkey_ = true;
                hint("F-key: 1-9, 0 = F10, - = F11, = = F12", 0);
                break;
            case 'g': type('`'); break;
            case 9: {
                std::vector<uint32_t> keep = mods_held_;
                mods_held_ = {ks::ShiftL};
                type(ks::Tab);
                mods_held_ = keep;
                show_mods();
                break;
            }
            default: break;   // anything else just closes the menu
        }
        return true;
    }

    bool fkey_pick(uint32_t k) {
        fkey_ = false;
        hide_hint();
        int n = 0;
        if (k >= '1' && k <= '9') n = int(k - '0');
        else if (k == '0') n = 10;
        else if (k == '-') n = 11;
        else if (k == '=') n = 12;
        if (n) type(ks::F1 + uint32_t(n - 1));
        return true;
    }

    void add_mod(uint32_t m) {
        if (std::find(mods_held_.begin(), mods_held_.end(), m) == mods_held_.end()) mods_held_.push_back(m);
        show_mods();
    }

    void show_mods() {
        std::string s;
        for (uint32_t m : mods_held_) {
            s += m == ks::ControlL ? "Ctrl+" : m == ks::AltL ? "Alt+" : m == ks::ShiftL ? "Shift+" : "Win+";
        }
        lv_label_set_text(mods_, s.c_str());
        if (s.empty()) lv_obj_add_flag(mods_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(mods_, LV_OBJ_FLAG_HIDDEN);
    }

    void key_event(uint32_t sym, bool down) {
        char buf[32];
        snprintf(buf, sizeof buf, "k %x %d\n", sym, down ? 1 : 0);
        send_line(buf);
    }

    void tap(uint32_t sym) {
        key_event(sym, true);
        key_event(sym, false);
    }

    // One key, with the modifiers picked from the menu, which then let go.
    void type(uint32_t sym) {
        if (!sym) return;
        for (uint32_t m : mods_held_) key_event(m, true);
        tap(sym);
        for (auto it = mods_held_.rbegin(); it != mods_held_.rend(); ++it) key_event(*it, false);
        if (!mods_held_.empty()) {
            mods_held_.clear();
            show_mods();
        }
    }

    void pointer() {
        if (!frame_) return;
        char buf[48];
        snprintf(buf, sizeof buf, "p %d %d %d\n", px_, py_, buttons_);
        send_line(buf);
    }

    void click(int bit) {
        int keep = buttons_;
        buttons_ |= bit;
        pointer();
        buttons_ = keep & ~bit;
        pointer();
    }

    void release_drag() {
        if (buttons_ & 1) {
            buttons_ &= ~1;
            pointer();
        }
    }

    bool mouse_key(uint32_t k) {
        int dx = 0, dy = 0;
        switch (k) {
            case 17: dy = -1; break;
            case 18: dy = 1; break;
            case 19: dx = 1; break;
            case 20: dx = -1; break;
            case 10: case 13:
                if (buttons_ & 1) release_drag();
                else click(1);
                return true;
            case 'r': case 'R': click(4); return true;
            case 'w': case 'W': click(8); return true;
            case 's': case 'S': click(16); return true;
            case 'd': case 'D':
                buttons_ ^= 1;
                pointer();
                hint(buttons_ & 1 ? "Dragging: move, then Enter or D to drop" : "Dropped", 1500);
                return true;
            default: return false;
        }
        // Held arrows speed up.
        uint32_t now = lv_tick_get();
        if (k == last_arrow_ && now - last_arrow_ms_ < 250) speed_ = std::min(speed_ * 1.35, 24.0);
        else speed_ = 3;
        last_arrow_ = k;
        last_arrow_ms_ = now;
        double step = speed_ / scale();
        if (step < 1) step = 1;
        if (frame_) {
            px_ = std::clamp(px_ + int(dx * step), 0, fw_ - 1);
            py_ = std::clamp(py_ + int(dy * step), 0, fh_ - 1);
        }
        pointer();
        place_view();
        return true;
    }

    Host host_;
    lv_obj_t* box_ = nullptr;
    lv_obj_t* canvas_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* hint_ = nullptr;
    lv_obj_t* mods_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    std::vector<uint16_t> pixels_;

    pid_t pid_ = 0;
    int fd_ = -1;
    std::string in_;
    bool connected_ = false;
    End end_ = End::None;
    std::string message_;

    const FrameHeader* frame_ = nullptr;
    size_t frame_size_ = 0;
    int fw_ = 0, fh_ = 0;
    uint32_t seen_seq_ = ~0u;
    bool dirty_ = true;

    int zoom_ = 0;          // 0 whole screen, 1 1:1, 2 2x
    double vx_ = 0, vy_ = 0;
    int px_ = -1, py_ = -1;
    int buttons_ = 0;
    bool mouse_ = false;
    bool menu_ = false;
    bool fkey_ = false;
    std::vector<uint32_t> mods_held_;
    uint32_t last_arrow_ = 0, last_arrow_ms_ = 0;
    double speed_ = 3;
    uint32_t last_key_ms_ = 0;
    uint32_t hint_since_ = 0, hint_until_ = 0;
};

// ---- The app --------------------------------------------------------------

class DesktopApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        hosts_ = load_hosts(home_dir());
        remote_note_use();
        if (tailscale_mode() != "off" && hal().tailscale().installed && !hal().tailscale().running)
            hal().tailscale_start();
        std::string arg = shell_app_arg();
        if (arg.rfind("peer:", 0) == 0) open_peer(arg.substr(5));
        rebuild();
    }

    ~DesktopApp() override { viewer_.reset(); }

    void tick() override {
        if (viewer_) {
            if (viewer_->ended() != Viewer::End::None) finish_view();
            return;
        }
        if (input_) return;   // never rebuild under the open text box
        if (dirty_) {
            dirty_ = false;
            rebuild();
        }
        run_queued();   // a password box, or a connection, after the rebuild
        if (viewer_ || input_) return;
        if (page_ == Page::List && ++n_ % 30 == 0) {
            remote_note_use();
            if (peers_signature() != peers_sig_ && list_.filter().empty()) {
                keep_row_ = list_.focused();
                rebuild();
            }
        }
    }

    bool on_key(uint32_t key) override {
        if (viewer_) return viewer_->key(key);
        if (input_) return false;
        bool used = list_.key(key);
        if (page_ == Page::List) set_actions();   // Enter's word follows the row in focus
        return used;
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (page_ == Page::Edit) {
            save_edit();
            page_ = Page::List;
            dirty_ = true;
            return true;
        }
        return false;
    }

private:
    enum class Page { List, Edit };

    // ---- Pages ----

    void rebuild() {
        lv_obj_clean(root_);
        input_ = nullptr;
        list_.reset();
        if (group) lv_group_remove_all_objs(group);
        if (page_ == Page::Edit) build_edit();
        else build_list();
        if (!input_) set_actions();   // (a new computer opens straight on its address box)
    }

    // The soft-key bar and the Tab menu (docs/UX.md in the OS). Both pages
    // are List screens, so the letters are menu actions, not plain keys.
    void set_actions() {
        std::vector<Action> a;
        if (page_ == Page::Edit) {
            a.push_back({"Change", 0, "Enter", nullptr});
            a.push_back({"Save", 0, "Esc", nullptr});
            Action del{"Delete", 'd', nullptr, [this] { delete_host(edit_index_); }};
            del.enabled = edit_index_ >= 0;
            del.danger = true;
            del.confirm = "Delete " + edit_.name + "?";
            a.push_back(del);
            shell_set_actions(std::move(a));
            return;
        }
        int i = focused_host();
        a.push_back({i >= 0 ? "Connect" : "Add", 0, "Enter", nullptr});
        a.push_back({"New computer", 'n', nullptr, [this] { new_host(); }});
        Action edit{"Edit", 'e', nullptr, [this] { edit_host(focused_host()); }};
        edit.enabled = i >= 0;
        a.push_back(edit);
        Action mon{"Open on the monitor", 'm', nullptr, [this] {
                       int k = focused_host();
                       if (k >= 0) open_on_monitor(hosts_[size_t(k)]);
                   }};
        mon.enabled = i >= 0 && power().docked();
        a.push_back(mon);
        Action del{"Delete", 'd', nullptr, [this] { delete_host(focused_host()); }};
        del.enabled = i >= 0;
        del.danger = true;
        del.confirm = i >= 0 ? "Delete " + hosts_[size_t(i)].name + "?" : "";
        a.push_back(del);
        shell_set_actions(std::move(a));
    }

    void new_host() {
        edit_ = Host{};
        edit_.name.clear();
        edit_index_ = -1;
        page_ = Page::Edit;
        dirty_ = true;
        ask_address_ = true;
    }

    void edit_host(int i) {
        if (i < 0 || i >= int(hosts_.size())) return;
        edit_ = hosts_[size_t(i)];
        edit_index_ = i;
        page_ = Page::Edit;
        dirty_ = true;
    }

    void delete_host(int i) {
        if (i < 0 || i >= int(hosts_.size())) return;
        std::string name = hosts_[size_t(i)].name;
        hosts_.erase(hosts_.begin() + i);
        save_hosts(home_dir(), hosts_);
        shell_toast("Deleted " + name);
        edit_index_ = -1;
        focus_ = 0;
        page_ = Page::List;
        dirty_ = true;
    }

    void build_list() {
        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 120, kBodyH - 8);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 4, 0);
        label(panel, ZS_ICON_LAPTOP " Desktop", &zs_font_16, kText);
        lv_obj_t* h = label(panel, "Reach a Windows PC (RDP), a Mac or Linux (VNC) from here.", &zs_font_12, kMuted);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(h, 106);
        if (power().docked()) {
            lv_obj_t* d = label(panel, "Docked: a computer can open on the monitor too.", &zs_font_12, kAccent);
            lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(d, 106);
        }

        // Saved computers first, so a row's index is its host's.
        list_.build(root_, 132, 4, 182, kBodyH - 8);
        for (size_t i = 0; i < hosts_.size(); ++i) {
            int k = int(i);
            list_.add(ZS_ICON_DESKTOP, hosts_[i].name, proto_name(hosts_[i].proto), [this, k] {
                focus_ = k;
                connect_after_ = k;
                dirty_ = true;
            });
        }
        list_.add(LV_SYMBOL_PLUS, "Add a computer", "", [this] { new_host(); });
        // Tailscale devices that could be added with a key press.
        peers_sig_ = peers_signature();
        TsStatus ts = hal().tailscale();
        if (ts.state == "Running") {
            for (const TsPeer& p : ts.peers) {
                if (p.os == "iOS" || p.os == "android") continue;
                bool saved = false;
                for (const Host& hh : hosts_) {
                    std::string host;
                    int port = 0;
                    if (split_address(hh.address, 0, host, port) && (host == p.name || host == p.ip)) saved = true;
                }
                if (saved) continue;
                std::string peer = p.name + ":" + p.os;
                int r = list_.add(LV_SYMBOL_PLUS, p.name, p.online ? "" : "off", [this, peer] {
                    open_peer(peer);
                    dirty_ = true;
                });
                list_.set_value_color(r, kMuted);
            }
        }
        list_.finish();
        int want = keep_row_ >= 0 ? keep_row_ : focus_;
        keep_row_ = -1;
        if (want >= 0) list_.focus(want);
    }

    // The form: a row per field (Enter types it), Type and Size as value
    // rows (Left / Right). Deleting is in the menu (D), with the shell's
    // confirm box.
    void build_edit() {
        Host& h = edit_;
        list_.build(root_, 6, 4, kScreenW - 12, kBodyH - 8);
        list_.add(LV_SYMBOL_EDIT, "Name", h.name.empty() ? std::string("as the address") : h.name, [this] {
            ask("Name", edit_.name, false, [this](const std::string& v) { edit_.name = v; });
        });
        list_.add(LV_SYMBOL_GPS, "Address", h.address.empty() ? std::string("none") : h.address,
                  [this] { ask_address(); });
        list_.add_choice(LV_SYMBOL_SHUFFLE, "Type", {"VNC (Mac, Linux)", "RDP (Windows, xrdp)"},
                         h.proto == Proto::Vnc ? 0 : 1, [this](int i) {
                             edit_.proto = i == 0 ? Proto::Vnc : Proto::Rdp;
                             refocus_ = 2;
                             dirty_ = true;   // RDP has more rows
                         });
        list_.add(LV_SYMBOL_HOME, "User", h.user.empty() ? std::string(h.proto == Proto::Vnc ? "none" : "ask") : h.user,
                  [this] {
                      ask("User name (a Mac's needs one; most VNC servers don't)", edit_.user, false,
                          [this](const std::string& v) { edit_.user = v; });
                  });
        list_.add(LV_SYMBOL_EYE_CLOSE, "Password", h.password.empty() ? "ask each time" : "saved", [this] {
            ask("Password to save (empty: ask each time)", "", true, [this](const std::string& v) { edit_.password = v; });
        });
        if (h.proto == Proto::Rdp) {
            std::vector<std::string> sizes;
            for (auto& sz : rdp_sizes()) sizes.push_back(sz.label);
            list_.add_choice(LV_SYMBOL_IMAGE, "Size", sizes, std::clamp(h.size, 0, int(sizes.size()) - 1),
                             [this](int i) { edit_.size = i; });
            list_.add(LV_SYMBOL_LIST, "Domain", h.domain.empty() ? std::string("none") : h.domain, [this] {
                ask("Windows domain (usually empty)", edit_.domain, false,
                    [this](const std::string& v) { edit_.domain = v; });
            });
        }
        list_.add(LV_SYMBOL_PLAY, "Connect", "", [this] {
            if (!save_edit()) return;
            page_ = Page::List;
            connect_after_ = focus_;
            dirty_ = true;
        });
        if (power().docked()) {
            list_.add(ZS_ICON_DESKTOP, "Open on the monitor", "", [this] {
                if (!save_edit()) return;
                open_on_monitor(edit_);
            });
        }
        list_.finish();
        if (refocus_ > 0) list_.focus(refocus_);
        refocus_ = 0;
        if (ask_address_) {
            ask_address_ = false;
            ask_address();
        }
    }

    // ---- Hosts ----

    std::string peers_signature() {
        TsStatus t = hal().tailscale();
        std::string s = t.state;
        for (auto& p : t.peers) s += "|" + p.name + (p.online ? "+" : "-");
        return s;
    }

    // The saved computer in focus on the list page, or -1 (the Add row, a
    // Tailscale device, or the edit page).
    int focused_host() const {
        if (page_ != Page::List || input_) return -1;
        int i = list_.focused();
        return i >= 0 && i < int(hosts_.size()) ? i : -1;
    }

    // "NAME:OS", from the list or from Remote's D key: the saved computer
    // with that address, or a new one filled in.
    void open_peer(const std::string& arg) {
        size_t colon = arg.rfind(':');
        std::string name = colon == std::string::npos ? arg : arg.substr(0, colon);
        std::string os = colon == std::string::npos ? "" : arg.substr(colon + 1);
        if (name.empty()) return;
        for (size_t i = 0; i < hosts_.size(); ++i) {
            std::string host;
            int port = 0;
            if (split_address(hosts_[i].address, 0, host, port) && host == name) {
                focus_ = int(i);
                page_ = Page::List;
                shell_toast(hosts_[i].name + " is saved already");
                return;
            }
        }
        edit_ = Host{};
        edit_.name = name;
        edit_.address = name;
        edit_.proto = os == "windows" ? Proto::Rdp : Proto::Vnc;
        edit_index_ = -1;
        page_ = Page::Edit;
        refocus_ = 3;   // User
        shell_toast(std::string("New computer: ") + (os == "windows" ? "RDP" : "VNC") + ", check the details");
    }

    bool save_edit() {
        std::string host;
        int port = 0;
        if (edit_.address.empty()) {
            return false;   // nothing to keep
        }
        if (!split_address(edit_.address, default_port(edit_.proto), host, port)) {
            shell_toast("That address doesn't look right");
            return false;
        }
        if (edit_.name.empty()) edit_.name = edit_.address;
        if (edit_index_ >= 0 && edit_index_ < int(hosts_.size())) {
            hosts_[size_t(edit_index_)] = edit_;
        } else {
            hosts_.push_back(edit_);
            edit_index_ = int(hosts_.size()) - 1;
        }
        focus_ = edit_index_;
        if (!save_hosts(home_dir(), hosts_)) shell_toast("Couldn't save the list");
        return true;
    }

    void ask_address() {
        const char* title = edit_.proto == Proto::Vnc ? "Address (pc, pc:1, 10.0.0.5:5901)" : "Address (pc, 10.0.0.5:3390)";
        ask(title, edit_.address, false, [this](const std::string& v) { edit_.address = v; });
    }

    void open_on_monitor(const Host& h) {
        if (!power().docked()) {
            shell_toast("Plug in a monitor and start Dock mode first");
            return;
        }
        std::vector<std::string> args = {"/usr/lib/zeroos/zeroos-desktop-dock"};
        if (h.proto == Proto::Vnc) {
            std::string d = vnc_display_arg(h.address);
            if (d.empty()) {
                // "pc:1" is display 1 already.
                std::string host;
                int port = 0;
                if (split_address(h.address, 5900, host, port) && port < 100)
                    d = (host.find(':') != std::string::npos ? "[" + host + "]" : host) + ":" + std::to_string(port);
            }
            if (d.empty()) {
                shell_toast("On the monitor, VNC needs a port from 5900 to 5999");
                return;
            }
            args.push_back("vnc");
            args.push_back(d);
        } else {
            args.push_back("rdp");
            args.push_back(h.address);
            args.push_back(h.user);
            args.push_back(h.domain);
        }
#if ZS_BACKEND_DEVICE
        if (!spawn_detached(args)) {
            shell_toast("Couldn't open it on the monitor");
            return;
        }
#else
        (void)spawn_detached;
#endif
        shell_toast("Opening " + h.name + " on the monitor");
    }

    // ---- Connecting ----

    void connect(int i, const std::string& pass) {
        if (i < 0 || i >= int(hosts_.size())) return;
        connected_index_ = i;
        lv_obj_clean(root_);
        input_ = nullptr;
        list_.reset();
        if (group) lv_group_remove_all_objs(group);
        // Like a Game screen: the remote computer has the keys and the whole
        // app area (kViewH), so no soft-key bar. Its own menu is on Esc.
        shell_set_actions({});
        viewer_ = std::make_unique<Viewer>(root_, hosts_[size_t(i)], pass);
    }

    void finish_view() {
        Viewer::End why = viewer_->ended();
        std::string msg = viewer_->message();
        viewer_.reset();
        page_ = Page::List;
        focus_ = connected_index_;
        rebuild();
        if (why == Viewer::End::Auth) {
            Host& h = hosts_[size_t(connected_index_)];
            shell_toast(msg);
            int i = connected_index_;
            auto ask_pass = [this, i]() {
                ask("Password for " + hosts_[size_t(i)].name, "", true, [this, i](const std::string& v) {
                    if (!v.empty()) {
                        pending_pass_ = v;
                        connect_after_ = i;
                    }
                });
            };
            if (h.user.empty() && h.proto == Proto::Rdp) {
                ask("User name on " + h.name, "", false, [this, i, ask_pass](const std::string& v) {
                    if (v.empty()) return;
                    hosts_[size_t(i)].user = v;
                    save_hosts(home_dir(), hosts_);
                    after_input_ = ask_pass;
                });
            } else {
                ask_pass();
            }
        } else if (why == Viewer::End::Error) {
            shell_toast(msg.empty() ? "Couldn't connect" : msg);
        } else if (!msg.empty()) {
            shell_toast(msg);
        }
    }

    // ---- The text box ----

    using Done = std::function<void(const std::string&)>;
    void ask(const std::string& title, const std::string& initial, bool secret, Done done) {
        done_ = std::move(done);
        ask_focus_ = std::max(0, list_.focused());
        // A Text screen while it's open: letters and digits type.
        shell_set_actions({{"OK", 0, "Enter", nullptr}, {"Cancel", 0, "Esc", nullptr}});
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        lv_obj_t* t = label(input_, title.c_str(), &zs_font_12, kMuted);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, 280);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, secret);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            self->close_input();
            if (d) d(v);
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    void close_input() {
        if (!input_) return;
        lv_group_set_editing(group, false);
        lv_obj_t* in = input_;
        input_ = nullptr;
        lv_obj_delete_async(in);
        dirty_ = true;
        if (page_ == Page::Edit && refocus_ == 0) refocus_ = ask_focus_;
    }

    // Run whatever the last step queued, once the screen is rebuilt.
    void run_queued() {
        if (after_input_) {
            auto f = std::move(after_input_);
            after_input_ = nullptr;
            f();
        }
        if (connect_after_ >= 0) {
            int i = connect_after_;
            connect_after_ = -1;
            std::string pass = pending_pass_.empty() ? hosts_[size_t(i)].password : pending_pass_;
            pending_pass_.clear();
            connect(i, pass);
        }
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* input_ = nullptr;
    Done done_;
    std::function<void()> after_input_;
    std::vector<Host> hosts_;
    ListView list_;
    int keep_row_ = -1;   // the list row to keep in focus over a rebuild
    std::string peers_sig_;
    std::unique_ptr<Viewer> viewer_;
    Page page_ = Page::List;
    Host edit_;
    int edit_index_ = -1;
    int focus_ = 0;
    int refocus_ = 0;     // the edit row to focus after a rebuild
    int ask_focus_ = 0;   // the row the text box was opened from
    int connect_after_ = -1;
    int connected_index_ = 0;
    std::string pending_pass_;
    bool ask_address_ = false;
    bool dirty_ = false;
    int n_ = 0;
};

}  // namespace

std::unique_ptr<App> make_desktop_app() { return std::make_unique<DesktopApp>(); }

}  // namespace zs
