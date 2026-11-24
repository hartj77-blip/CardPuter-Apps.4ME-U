// Media: your own music and video files, dressed as a classic desktop MP3
// player: a green LCD with the time and a spectrum analyser, a scrolling
// title, transport buttons, and a file explorer underneath in place of the
// playlist. It plays what is on the device (the SD card), on a USB drive,
// or on a server whose share is mounted (SMB, NFS, SSHFS), and http(s)
// addresses. Jellyfin has its own app now. Voice notes are in Recorder.
//
//   Up/Down      choose                   Enter        open / play
//   Left         up a folder              Right        open a folder
//   X            play the focused file, or everything in a folder
//   Z C V B      previous, pause, stop, next (the old desktop keys)
//   Space        pause / resume           N / P        next / previous
//   S            shuffle on or off        + / -        volume
//   Fn+Q/W/E     the keyboard's media keys: play/pause, previous, next
//   /            search this folder       Esc / Del    up a folder, or clear the search
//   Tab          the menu (open an address, shuffle, stop, rescan)
//
// Playing goes through the shell's queue (ui/player.h), so it carries on to
// the next track after you leave the app or the screen turns off.
//
// A video file plays full width in the app (README.md):
//   Space pause   Left/Right -/+10 s   Up/Down +/-1 min   0 from the start
//   + / - volume  M on the monitor     Esc or S stop
// Video belongs to the app: it stops when you leave Media.
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <random>

#include "apps/common.h"
#include "platform/keys.h"
#include "ui/player.h"
#include "ui/power.h"
#include "ui/prefs.h"

namespace zs {
namespace {
using namespace theme;

// The look: gunmetal chrome, a black LCD with green digits, a blue bar on
// the playlist's selected row.
constexpr uint32_t kChrome = 0x262637;
constexpr uint32_t kChromeHi = 0x50506e;
constexpr uint32_t kChromeLo = 0x101018;
constexpr uint32_t kLcd = 0x000000;
constexpr uint32_t kGreen = 0x00e000;
constexpr uint32_t kGreenDim = 0x0f5a0f;
constexpr uint32_t kGold = 0xdcb85a;
constexpr uint32_t kGoldDim = 0x6e5c2c;
constexpr uint32_t kSelect = 0x1414b4;
constexpr uint32_t kButton = 0x3a3a55;
constexpr uint32_t kButtonText = 0xc4c4d8;
constexpr uint32_t kFolder = 0xdcb85a;   // folders in the list: gold, like the title bar

constexpr int kH = kBodyH;            // the soft-key bar takes the bottom
constexpr int kBars = 19;             // spectrum analyser columns
constexpr int kBarH = 18;
constexpr int kLcdW = 116, kLcdH = 50;
constexpr int kRightX = 4 + kLcdW + 4, kRightW = kScreenW - kRightX - 4;
constexpr int kListY = 70;

// The video picture: the whole content area. 16:9 fills it at 267 x 150.
constexpr int kVidW = kScreenW;
constexpr int kVidH = kContentH;
constexpr uint32_t kOsdMs = 3000;     // the info bar stays up this long after a key

// Playing a whole folder looks this many folders down, for this many files.
constexpr int kDeep = 5;
constexpr size_t kMaxQueue = 3000;

std::string mmss(int ms) {
    if (ms < 0) return "--:--";
    int s = ms / 1000;
    if (s >= 3600) return fmt("%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    return fmt("%d:%02d", s / 60, s % 60);
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
    return s;
}

std::string ext_of(const std::string& name) {
    auto dot = name.rfind('.');
    auto slash = name.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string e = lower(name.substr(dot));
    auto q = e.find_first_of("?#");   // an address's query
    return q == std::string::npos ? e : e.substr(0, q);
}

bool is_audio(const std::string& name) {
    static const char* const k[] = {".mp3", ".flac", ".ogg", ".oga", ".wav", ".m4a", ".opus", ".aac", ".wma"};
    std::string e = ext_of(name);
    for (auto x : k)
        if (e == x) return true;
    return false;
}

bool is_video(const std::string& name) {
    static const char* const k[] = {".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v", ".mpg", ".mpeg", ".ts"};
    std::string e = ext_of(name);
    for (auto x : k)
        if (e == x) return true;
    return false;
}

bool is_playlist(const std::string& name) {
    std::string e = ext_of(name);
    return e == ".m3u" || e == ".m3u8" || e == ".pls";
}

bool is_url(const std::string& s) { return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0; }

// "02 - Some Song.flac" -> "02 - Some Song".
std::string stem(const std::string& path) {
    std::string n = path.substr(path.rfind('/') + 1);
    auto q = n.find_first_of("?#");
    if (q != std::string::npos) n = n.substr(0, q);
    auto dot = n.rfind('.');
    return dot == std::string::npos || dot == 0 ? n : n.substr(0, dot);
}

std::string join(const std::string& dir, const std::string& name) {
    return dir == "/" ? "/" + name : dir + "/" + name;
}

std::string parent_of(const std::string& path) {
    auto p = path.find_last_of('/');
    return p == 0 || p == std::string::npos ? "/" : path.substr(0, p);
}

bool is_dir(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string human(long long b) {
    if (b < 1024) return fmt("%lld B", b);
    if (b < 1024 * 1024) return fmt("%.0f KB", b / 1024.0);
    if (b < 1024LL * 1024 * 1024) return fmt("%.1f MB", b / 1048576.0);
    return fmt("%.1f GB", b / 1073741824.0);
}

// A plain box: no padding, no scrolling, a 1-pixel border.
lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg, uint32_t border) {
    lv_obj_t* o = lv_obj_create(parent);
    style_plain(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, c(border), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// One row of the explorer.
struct Entry {
    enum Type { Place, Dir, Audio, Video, List, Address } type = Dir;
    std::string name;
    std::string path;
    long long size = 0;
    const char* icon = nullptr;   // places only
    std::string note;             // places: what it is ("USB", "SMB" ...)
};

// Mount points of drives and shares: /media/*, /mnt/*, /run/media/*, and any
// network filesystem wherever it is mounted.
std::vector<Entry> mounted_places() {
    std::vector<Entry> v;
    std::ifstream f("/proc/mounts");
    std::string dev, dir, type, rest;
    while (f >> dev >> dir >> type) {
        std::getline(f, rest);
        // Spaces in a mount point come as \040.
        for (size_t p; (p = dir.find("\\040")) != std::string::npos;) dir.replace(p, 4, " ");
        bool net = type == "cifs" || type == "smb3" || type == "nfs" || type == "nfs4" || type == "fuse.sshfs" ||
                   type == "davfs" || type == "fuse.rclone";
        bool removable = dir.rfind("/media/", 0) == 0 || dir.rfind("/mnt/", 0) == 0 ||
                         dir.rfind("/run/media/", 0) == 0;
        if (!net && !removable) continue;
        if (dir == "/" || dir == "/boot" || dir.rfind("/boot/", 0) == 0) continue;
        Entry e;
        e.type = Entry::Place;
        e.path = dir;
        e.name = dir.substr(dir.rfind('/') + 1);
        if (net) {
            std::string host = dev;
            if (host.rfind("//", 0) == 0) host = host.substr(2);   // //nas/music
            host = host.substr(0, host.find_first_of(":/"));
            e.icon = LV_SYMBOL_WIFI;
            e.note = type == "cifs" || type == "smb3" ? "SMB" : type.rfind("nfs", 0) == 0 ? "NFS"
                     : type == "fuse.sshfs"                    ? "SSH"
                                                               : "NET";
            if (!host.empty()) e.note += " " + host;
        } else {
            e.icon = LV_SYMBOL_USB;
            e.note = "DRIVE";
        }
        bool dup = false;
        for (auto& x : v) dup = dup || x.path == e.path;
        if (!dup) v.push_back(e);
    }
    return v;
}

class MediaApp : public App {
public:
    ~MediaApp() override {
        // Leaving the app: the screen is being torn down, so only stop the video.
        if (anim_) lv_timer_delete(anim_);
        close_video(false);
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        shuffle_ = prefs::get_str("media.shuffle", "0") == "1";
        // Back where you were last time, if it is still there.
        std::string last = prefs::get_str("media.folder", "");
        std::string base = prefs::get_str("media.place", "");
        if (!last.empty() && !base.empty() && is_dir(last) && last.rfind(base, 0) == 0) {
            place_ = base;
            cwd_ = last;
        }

        lv_obj_t* frame = box(root, 0, 0, kScreenW, kH, kChrome, kChrome);
        lv_obj_set_style_border_width(frame, 0, 0);
        build_title_bar(frame);
        build_main_window(frame);

        anim_ = lv_timer_create([](lv_timer_t* t) { static_cast<MediaApp*>(lv_timer_get_user_data(t))->animate(); },
                                60, this);
        set_actions();
        load();
    }

    void tick() override {
        if (vid_) {
            video_tick();
            return;
        }
        auto& h = hal();
        PlaybackState pb = h.playback();
        playing_ = !pb.path.empty() && !pb.paused;

        // The LCD: state, time, and what is playing.
        const char* glyph = pb.path.empty() ? LV_SYMBOL_STOP : pb.paused ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
        lv_label_set_text(state_, glyph);
        // Paused: the time blinks, as it always has on these players.
        bool blink_off = pb.paused && (lv_tick_get() / 500) % 2;
        lv_label_set_text(time_, pb.path.empty() ? "0:00" : blink_off ? "" : mmss(pb.pos_ms).c_str());
        lv_obj_set_style_text_color(time_, c(pb.path.empty() ? kGreenDim : kGreen), 0);

        const QueueItem* q = player_current();
        int at = -1;
        for (size_t i = 0; i < queue_paths_.size(); ++i)
            if (queue_paths_[i] == pb.path) at = int(i);
        std::string marquee;
        if (pb.path.empty()) {
            marquee = "ZeroOS Media  ***  Enter plays, X plays a whole folder  ***  ";
        } else {
            std::string name = q ? q->title : stem(pb.path);
            std::string artist = q ? q->artist : "";
            marquee = (at >= 0 ? fmt("%d. ", at + 1) : std::string()) + (artist.empty() ? "" : artist + " - ") +
                      name + " (" + mmss(pb.dur_ms) + ")  ***  ";
        }
        if (marquee != shown_marquee_) {
            lv_label_set_text(marquee_, marquee.c_str());
            shown_marquee_ = marquee;
        }
        lv_label_set_text(kind_, pb.path.empty() ? "---" : format_of(pb.path).c_str());
        lv_label_set_text(pos_, at >= 0 ? fmt("%d/%zu", at + 1, queue_paths_.size()).c_str() : "-/-");
        lv_obj_set_style_text_color(shuf_, c(shuffle_ ? kGreen : kGreenDim), 0);
        lv_bar_set_value(vol_, h.muted() ? 0 : h.volume(), LV_ANIM_OFF);
        int permille = pb.dur_ms > 0 ? int(1000LL * pb.pos_ms / pb.dur_ms) : 0;
        lv_bar_set_value(seek_, permille, LV_ANIM_OFF);

        // The play mark follows the queue, but not under an open text box:
        // rebuilding the list takes the box out of the focus group.
        if (pb.path != listed_playing_ && !input_) rebuild_list();

        uint32_t now = lv_tick_get();
        for (int i = 0; i < 5; ++i)
            if (flash_at_[i] && now - flash_at_[i] > 180) {
                flash_at_[i] = 0;
                lv_obj_set_style_bg_color(buttons_[i], c(kButton), 0);
                lv_obj_set_style_text_color(lv_obj_get_child(buttons_[i], 0), c(kButtonText), 0);
            }
    }

    bool on_key(uint32_t key) override {
        if (vid_) return video_key(key);
        if (input_) return false;   // typing in the text box
        auto& h = hal();
        switch (key) {
            case LV_KEY_LEFT:
            case LV_KEY_BACKSPACE:
                up();
                return true;
            case LV_KEY_RIGHT: {
                int i = focused_index();
                if (i >= 0 && i < int(entries_.size()) &&
                    (entries_[size_t(i)].type == Entry::Dir || entries_[size_t(i)].type == Entry::Place))
                    activate(i);
                return true;
            }
            // The keyboard's media keys (Fn + Q / W / E), and a headset's
            // when the shell passes them on: here they act on the list too.
            case kKeyPlayPause:
            case 'c': case 'C':
            case ' ': {
                flash(key == 'c' || key == 'C' ? kPause : kPlay);
                PlaybackState pb = h.playback();
                if (!pb.path.empty()) h.set_paused(!pb.paused);
                else activate(focused_index());
                return true;
            }
            case 'x': case 'X':
                flash(kPlay);
                play_focused();
                return true;
            case kKeyNextTrack:
            case 'b': case 'B':
            case 'n': case 'N':
                flash(kNext);
                step(+1);
                return true;
            case kKeyPrevTrack:
            case 'z': case 'Z':
            case 'p': case 'P':
                flash(kPrev);
                step(-1);
                return true;
            case 'v': case 'V':
                flash(kStop);
                stop();
                return true;
            case 's': case 'S': toggle_shuffle(); return true;
            case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
            case '/': search(); return true;
        }
        return false;
    }

    bool on_back() override {
        if (vid_) {
            close_video(true);
            return true;
        }
        if (input_) {
            close_input();
            return true;
        }
        if (!filter_.empty()) {
            filter_.clear();
            rebuild_list();
            return true;
        }
        if (!cwd_.empty()) {
            up();
            return true;
        }
        return false;
    }

private:
    enum Button { kPrev, kPlay, kPause, kStop, kNext };

    // ---- Building the screen --------------------------------------------

    void build_title_bar(lv_obj_t* frame) {
        lv_obj_t* t = label(frame, "ZEROOS MEDIA", &zs_font_12, kGold);
        lv_obj_set_pos(t, 6, 0);
        lv_obj_update_layout(t);
        int x = 6 + lv_obj_get_width(t) + 8;
        // The grip ridges between the name and where you are.
        for (int k = 0; k < 2; ++k) {
            lv_obj_t* r = box(frame, x, 5 + k * 3, kScreenW - x - 6, 1, kGoldDim, kGoldDim);
            lv_obj_set_style_border_width(r, 0, 0);
        }
        crumb_ = label(frame, "", &zs_font_12, kGold);
        lv_obj_set_style_bg_color(crumb_, c(kChrome), 0);
        lv_obj_set_style_bg_opa(crumb_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(crumb_, 5, 0);
        lv_obj_align(crumb_, LV_ALIGN_TOP_RIGHT, -6, 0);
    }

    void build_main_window(lv_obj_t* frame) {
        // The LCD: play state, time, spectrum analyser.
        lv_obj_t* lcd = box(frame, 4, 16, kLcdW, kLcdH, kLcd, kChromeHi);
        lv_obj_set_style_border_side(lcd, lv_border_side_t(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT), 0);
        state_ = label(lcd, LV_SYMBOL_STOP, &zs_font_12, kGreen);
        lv_obj_set_pos(state_, 5, 5);
        time_ = label(lcd, "0:00", &zs_font_20, kGreenDim);
        lv_obj_set_style_text_align(time_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(time_, 84);
        lv_obj_set_pos(time_, 24, 0);
        for (int i = 0; i < kBars; ++i) {
            int x = 5 + i * 5, y = kLcdH - 4 - kBarH;
            // Each column is the whole green-yellow-red gradient, covered
            // from the top down to its level; a grey cap marks the peak.
            lv_obj_t* g = box(lcd, x, y, 4, kBarH, 0xe01e00, 0);
            lv_obj_set_style_border_width(g, 0, 0);
            lv_obj_set_style_bg_grad_color(g, c(0x10c010), 0);
            lv_obj_set_style_bg_main_stop(g, 40, 0);
            lv_obj_set_style_bg_grad_dir(g, LV_GRAD_DIR_VER, 0);
            mask_[i] = box(lcd, x, y, 4, kBarH, kLcd, 0);
            lv_obj_set_style_border_width(mask_[i], 0, 0);
            peak_obj_[i] = box(lcd, x, y + kBarH - 1, 4, 1, 0x9a9aa8, 0);
            lv_obj_set_style_border_width(peak_obj_[i], 0, 0);
        }

        // The title, scrolling, in its own strip of LCD.
        lv_obj_t* strip = box(frame, kRightX, 16, kRightW, 16, kLcd, kChromeHi);
        lv_obj_set_style_border_side(strip, lv_border_side_t(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT), 0);
        marquee_ = label(strip, "", &zs_font_12, kGreen);
        lv_obj_set_width(marquee_, kRightW - 6);
        lv_label_set_long_mode(marquee_, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_pos(marquee_, 3, 0);

        // Small LCD boxes: the format, place in the queue, shuffle; volume.
        auto lcd_box = [&](int x, int w, const char* text) {
            lv_obj_t* b = box(frame, x, 35, w, 14, kLcd, kChromeHi);
            lv_obj_set_style_border_side(b, lv_border_side_t(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT), 0);
            lv_obj_t* l = label(b, text, &zs_font_12, kGreen);
            lv_obj_center(l);
            return l;
        };
        kind_ = lcd_box(kRightX, 38, "---");
        pos_ = lcd_box(kRightX + 41, 50, "-/-");
        shuf_ = lcd_box(kRightX + 94, 38, "SHUF");
        lv_obj_t* vl = label(frame, "VOL", &zs_font_12, kGoldDim);
        lv_obj_set_pos(vl, kRightX + 136, 35);
        vol_ = thin_bar(frame, kRightW - 164, 5, kGreen);
        lv_obj_set_style_bg_color(vol_, c(kChromeLo), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vol_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_pos(vol_, kRightX + 162, 40);
        lv_bar_set_range(vol_, 0, 100);

        // The transport buttons, then the seek bar.
        static const char* const syms[5] = {LV_SYMBOL_PREV, LV_SYMBOL_PLAY, LV_SYMBOL_PAUSE, LV_SYMBOL_STOP,
                                            LV_SYMBOL_NEXT};
        for (int i = 0; i < 5; ++i) {
            buttons_[i] = box(frame, kRightX + i * 25, 53, 22, 13, kButton, kChromeHi);
            lv_obj_set_style_border_side(buttons_[i], lv_border_side_t(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT), 0);
            lv_obj_center(label(buttons_[i], syms[i], &zs_font_12, kButtonText));
        }
        seek_ = thin_bar(frame, kRightW - 130, 4, kGold);
        lv_obj_set_style_bg_color(seek_, c(kChromeLo), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(seek_, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_pos(seek_, kRightX + 128, 58);
        lv_bar_set_range(seek_, 0, 1000);
    }

    // A transport button lights up for a moment when its key is pressed.
    void flash(Button b) {
        flash_at_[b] = lv_tick_get() | 1;
        lv_obj_set_style_bg_color(buttons_[b], c(kGold), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(buttons_[b], 0), c(kChromeLo), 0);
        for (int i = 0; i < 5; ++i)
            if (i != b) lv_obj_set_style_text_color(lv_obj_get_child(buttons_[i], 0), c(kButtonText), 0);
    }

    // The analyser: lively while something plays, falling flat otherwise.
    // (It is for the look: the player doesn't hand over the sound itself.)
    void animate() {
        if (vid_ || power().screen_off()) return;
        if (!playing_ && idle_frames_ > 40) return;   // settled: nothing to draw
        idle_frames_ = playing_ ? 0 : idle_frames_ + 1;
        for (int i = 0; i < kBars; ++i) {
            int target = 0;
            if (playing_) {
                // More in the bass, a little less up top, never quite still.
                int ceiling = kBarH - (i * 7) / kBars;
                target = 2 + std::rand() % std::max(1, ceiling - 1);
                if (std::rand() % 5 == 0) target = std::min(kBarH, target + 6);
            }
            level_[i] = target > level_[i] ? (level_[i] + target * 3) / 4 : std::max(0, level_[i] - 2);
            if (level_[i] >= peak_[i]) {
                peak_[i] = level_[i];
                peak_hold_[i] = 6;
            } else if (peak_hold_[i] > 0) {
                --peak_hold_[i];
            } else if (peak_[i] > 0) {
                --peak_[i];
            }
            lv_obj_set_height(mask_[i], kBarH - level_[i]);
            int py = kLcdH - 4 - std::max(1, peak_[i] + 1);
            lv_obj_set_y(peak_obj_[i], py);
        }
    }

    void set_actions() {
        if (vid_) {
            shell_set_actions({});   // the picture takes the whole area
            return;
        }
        std::vector<Action> a;
        a.push_back({"Open / play", 0, "Enter", nullptr});
        a.push_back({"Up a folder", 0, LV_SYMBOL_LEFT, nullptr});
        a.push_back({"Play folder", 0, "X", nullptr});
        a.push_back({"Search folder", 'f', "/", [this] { search(); }});
        a.push_back({"Pause", 0, "Space", nullptr});
        a.push_back({"Previous", 0, "Z", nullptr});
        a.push_back({"Stop", 't', "V", [this] { stop(); }});
        a.push_back({"Next", 0, "B", nullptr});
        a.push_back({shuffle_ ? "Shuffle off" : "Shuffle on", 's', "S", [this] { toggle_shuffle(); }});
        a.push_back({"Volume", 0, "+ -", nullptr});
        a.push_back({"Open an address", 'o', nullptr, [this] { open_address(); }});
        a.push_back({"Places", 'h', nullptr, [this] { go_places(); }});
        a.push_back({"Rescan", 'r', nullptr, [this] {
                         int i = focused_index();
                         load(i >= 0 && i < int(entries_.size()) ? entries_[size_t(i)].path : "");
                     }});
        shell_set_actions(std::move(a));
    }

    static std::string format_of(const std::string& path) {
        std::string e = ext_of(path);
        if (e.size() < 2 || e.size() > 5) return is_url(path) ? "NET" : "---";
        std::string up = e.substr(1);
        for (auto& ch : up) ch = char(std::toupper((unsigned char)ch));
        return up;
    }

    // ---- The explorer -----------------------------------------------------

    // The first screen: the folders that hold media, drives, shares.
    std::vector<Entry> places() const {
        std::vector<Entry> v;
        std::string home = home_dir();
        auto add = [&](const std::string& name, const std::string& path, const char* icon, const std::string& note) {
            Entry e;
            e.type = Entry::Place;
            e.name = name;
            e.path = path;
            e.icon = icon;
            e.note = note;
            v.push_back(e);
        };
        add("Music", home + "/Music", LV_SYMBOL_AUDIO, "SD");
        add("Videos", home + "/Videos", LV_SYMBOL_VIDEO, "SD");
        if (is_dir(home + "/Downloads")) add("Downloads", home + "/Downloads", LV_SYMBOL_DOWNLOAD, "SD");
        add("SD card", home, LV_SYMBOL_SD_CARD, "HOME");
        for (auto& m : mounted_places()) v.push_back(m);
        Entry addr;
        addr.type = Entry::Address;
        addr.name = "Open an address...";
        addr.icon = ZS_ICON_GLOBE;
        addr.note = "HTTP";
        v.push_back(addr);
        return v;
    }

    // Read the current folder (or the places) into entries_.
    void load(const std::string& focus = "") {
        entries_.clear();
        if (cwd_.empty()) {
            entries_ = places();
        } else if (DIR* d = opendir(cwd_.c_str())) {
            std::vector<Entry> dirs, files;
            while (dirent* de = readdir(d)) {
                std::string n = de->d_name;
                if (n.empty() || n[0] == '.') continue;
                std::string full = join(cwd_, n);
                struct stat st{};
                if (stat(full.c_str(), &st) != 0) continue;
                Entry e;
                e.name = n;
                e.path = full;
                e.size = (long long)st.st_size;
                if (S_ISDIR(st.st_mode)) {
                    e.type = Entry::Dir;
                    dirs.push_back(e);
                    continue;
                }
                if (!S_ISREG(st.st_mode)) continue;
                if (is_audio(n)) e.type = Entry::Audio;
                else if (is_video(n)) e.type = Entry::Video;
                else if (is_playlist(n)) e.type = Entry::List;
                else continue;   // only what Media can play
                files.push_back(e);
            }
            closedir(d);
            auto by_name = [](const Entry& a, const Entry& b) { return lower(a.name) < lower(b.name); };
            std::sort(dirs.begin(), dirs.end(), by_name);
            std::sort(files.begin(), files.end(), by_name);
            entries_ = std::move(dirs);
            entries_.insert(entries_.end(), files.begin(), files.end());
        }
        if (!cwd_.empty()) {
            prefs::set_str("media.place", place_);
            prefs::set_str("media.folder", cwd_);
        }
        rebuild_list(focus, false);
    }

    // The place you are in and the folder: "Music/Artist/Album".
    std::string crumb_text() const {
        if (cwd_.empty()) return "PLACES";
        std::string name;
        for (auto& p : places())
            if (p.path == place_) name = p.name;
        if (name.empty()) name = place_.substr(place_.rfind('/') + 1);
        std::string t = name + cwd_.substr(place_.size());
        if (!filter_.empty()) t = "\"" + filter_ + "\" in " + t;
        if (t.size() > 26) t = "..." + t.substr(t.size() - 24);   // the deepest part matters most
        return t;
    }

    bool shown(const Entry& e) const {
        return filter_.empty() || lower(e.name).find(lower(filter_)) != std::string::npos;
    }

    lv_obj_t* add_row(const char* symbol, const std::string& text, const std::string& right, int index,
                      uint32_t color, bool now = false) {
        lv_obj_t* b = list_button(list_, symbol, text.c_str(), on_click, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)index);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        for (lv_style_selector_t st : {lv_style_selector_t(LV_STATE_FOCUSED), lv_style_selector_t(LV_STATE_FOCUS_KEY),
                                       lv_style_selector_t(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY)}) {
            lv_obj_set_style_bg_color(b, c(kSelect), st);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, st);
            lv_obj_set_style_text_color(b, c(0xffffff), st);
        }
        lv_obj_set_style_text_color(b, c(now ? 0xffffff : color), 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_outline_width(b, 0, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_pad_ver(b, 0, 0);
        lv_obj_set_style_pad_hor(b, 3, 0);
        lv_obj_set_style_pad_column(b, 4, 0);
        lv_obj_set_style_min_height(b, 0, 0);
        lv_obj_set_style_text_font(b, &zs_font_12, 0);
        lv_obj_t* lbl = lv_obj_get_child(b, symbol ? 1 : 0);
        if (lbl) lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        if (!right.empty()) {
            // No colour of its own: it follows the row's, white when selected.
            lv_obj_t* r = lv_label_create(b);
            lv_label_set_text(r, right.c_str());
            lv_obj_set_style_text_font(r, &zs_font_12, 0);
        }
        return b;
    }

    void note(const std::string& text) {
        lv_obj_t* t = lv_list_add_text(list_, text.c_str());
        lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(t, c(kGreenDim), 0);
        lv_obj_set_style_text_font(t, &zs_font_12, 0);
        lv_obj_set_style_pad_ver(t, 0, 0);
        lv_obj_set_style_pad_hor(t, 3, 0);
    }

    // focus_path: the row to select. Otherwise the one selected now stays
    // selected (keep), else what is playing, else the first row.
    void rebuild_list(const std::string& focus_path = "", bool keep_focus = true) {
        int keep = keep_focus ? focused_index() : -1;
        std::string keep_path = !focus_path.empty() ? focus_path
                                : keep >= 0 && keep < int(entries_.size()) ? entries_[size_t(keep)].path
                                                                           : std::string();
        if (list_) {
            lv_group_remove_all_objs(group);
            lv_obj_delete(list_);
        }
        list_ = lv_list_create(root_);
        lv_obj_set_pos(list_, 4, kListY);
        lv_obj_set_size(list_, kScreenW - 8, kH - kListY - 2);
        lv_obj_set_style_bg_color(list_, c(kLcd), 0);
        lv_obj_set_style_bg_opa(list_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(list_, c(kChromeHi), 0);
        lv_obj_set_style_border_width(list_, 1, 0);
        lv_obj_set_style_border_side(list_, lv_border_side_t(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT), 0);
        lv_obj_set_style_radius(list_, 0, 0);
        lv_obj_set_style_pad_all(list_, 1, 0);
        lv_obj_set_style_pad_row(list_, 0, 0);
        lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_ACTIVE);
        lv_obj_set_style_bg_color(list_, c(kGreenDim), LV_PART_SCROLLBAR);

        listed_playing_ = hal().playback().path;
        lv_obj_t* focus = nullptr;
        lv_obj_t* now_row = nullptr;
        int files = 0, shown_n = 0;
        if (!cwd_.empty() && entries_.empty()) {
            note(is_dir(cwd_) ? "No music or video here" : "Can't open this folder");
            if (place_ == home_dir() + "/Music") note("Copy music to ~/Music on the SD card");
            if (place_ == home_dir() + "/Videos") note("Copy videos to ~/Videos on the SD card");
        }
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            if (!shown(e)) continue;
            ++shown_n;
            lv_obj_t* b = nullptr;
            switch (e.type) {
                case Entry::Place:
                case Entry::Address:
                    b = add_row(e.icon, e.name, e.note, int(i), kGreen);
                    break;
                case Entry::Dir: b = add_row(LV_SYMBOL_DIRECTORY, e.name, "", int(i), kFolder); break;
                case Entry::List: b = add_row(LV_SYMBOL_LIST, stem(e.name), "M3U", int(i), kGreen); break;
                case Entry::Video:
                    b = add_row(LV_SYMBOL_VIDEO, stem(e.name), human(e.size), int(i), kGreen);
                    break;
                case Entry::Audio: {
                    bool now = e.path == listed_playing_;
                    b = add_row(nullptr, fmt("%d. ", ++files) + stem(e.name), format_of(e.name), int(i), kGreen, now);
                    if (now) now_row = b;
                    break;
                }
            }
            if (!keep_path.empty() && e.path == keep_path) focus = b;
            if (!focus && keep_path.empty() && int(i) == keep) focus = b;
        }
        if (!entries_.empty() && shown_n == 0) note("Nothing matches \"" + filter_ + "\" (Esc clears)");
        if (!focus) focus = now_row;
        if (!focus && lv_obj_get_child_count(list_) > 0) {
            for (uint32_t k = 0; k < lv_obj_get_child_count(list_); ++k) {
                lv_obj_t* ch = lv_obj_get_child(list_, int32_t(k));
                if (lv_obj_check_type(ch, &lv_list_button_class)) {
                    focus = ch;
                    break;
                }
            }
        }
        if (focus) {
            lv_group_focus_obj(focus);
            lv_obj_scroll_to_view(focus, LV_ANIM_OFF);
        }
        if (input_) lv_obj_move_foreground(input_);
        lv_label_set_text(crumb_, crumb_text().c_str());
        lv_obj_align(crumb_, LV_ALIGN_TOP_RIGHT, -6, 0);
    }

    int focused_index() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f || !list_ || lv_obj_get_parent(f) != list_) return -1;
        return int((intptr_t)lv_obj_get_user_data(f));
    }

    void go_places() {
        std::string was = place_;
        cwd_.clear();
        place_.clear();
        filter_.clear();
        load(was);
    }

    void open_dir(const std::string& path) {
        cwd_ = path;
        filter_.clear();
        load();
    }

    // Up a folder; from the top of a place, back to the places.
    void up() {
        if (cwd_.empty()) return;
        if (!filter_.empty()) filter_.clear();
        if (cwd_ == place_) {
            go_places();
            return;
        }
        std::string was = cwd_;
        cwd_ = parent_of(cwd_);
        load(was);   // the folder we came out of stays selected
    }

    // ---- Playing ------------------------------------------------------------

    // Shuffle: the chosen track first, the rest in a random order.
    std::vector<QueueItem> arrange(std::vector<QueueItem> q, int& at) {
        if (shuffle_ && q.size() > 1 && at >= 0 && at < int(q.size())) {
            std::swap(q[0], q[size_t(at)]);
            std::shuffle(q.begin() + 1, q.end(), std::mt19937(std::random_device{}()));
            at = 0;
        }
        queue_paths_.clear();
        for (auto& x : q) queue_paths_.push_back(x.path);
        return q;
    }

    void play_queue(std::vector<QueueItem> q, int at, const std::string& name) {
        if (q.empty()) {
            shell_toast("No music in " + name);
            return;
        }
        q = arrange(std::move(q), at);
        if (!player_play(std::move(q), at)) shell_toast("Can't play " + name);
        else if (queue_paths_.size() > 1) shell_toast(fmt("Playing %zu tracks", queue_paths_.size()));
        rebuild_list();
    }

    // The artist: the folder two up from a track (Artist/Album/track), or
    // the one it is in.
    std::string artist_of(const std::string& path) const {
        std::string dir = parent_of(path);
        std::string top = place_.empty() ? std::string("/") : place_;
        std::string up2 = parent_of(dir);
        if (up2.size() > top.size() && up2.rfind(top, 0) == 0) return up2.substr(up2.rfind('/') + 1);
        if (dir.size() > top.size()) return dir.substr(dir.rfind('/') + 1);
        return "";
    }

    QueueItem item_for(const std::string& path) const {
        return {path, stem(path), is_url(path) ? "" : artist_of(path), -1, ""};
    }

    // Every track in a folder and the folders under it, in name order.
    void collect(const std::string& dir, int depth, std::vector<QueueItem>& out) const {
        if (depth > kDeep || out.size() >= kMaxQueue) return;
        DIR* d = opendir(dir.c_str());
        if (!d) return;
        std::vector<std::string> names;
        while (dirent* de = readdir(d))
            if (de->d_name[0] != '.') names.push_back(de->d_name);
        closedir(d);
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        std::vector<std::string> subdirs;
        for (auto& n : names) {
            std::string full = join(dir, n);
            struct stat st{};
            if (stat(full.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) subdirs.push_back(full);
            else if (S_ISREG(st.st_mode) && is_audio(n) && out.size() < kMaxQueue) out.push_back(item_for(full));
        }
        for (auto& s : subdirs) collect(s, depth + 1, out);
    }

    // A .m3u or .pls: one address or path per line; relative paths are
    // from the playlist's folder.
    std::vector<QueueItem> read_playlist(const std::string& path) const {
        std::vector<QueueItem> q;
        std::ifstream f(path);
        std::string line, dir = parent_of(path);
        bool pls = ext_of(path) == ".pls";
        while (std::getline(f, line) && q.size() < kMaxQueue) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (pls) {
                if (line.rfind("File", 0) != 0) continue;
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                line = line.substr(eq + 1);
            }
            if (line.rfind("file://", 0) == 0) line = line.substr(7);
            std::string p = is_url(line) || line[0] == '/' ? line : join(dir, line);
            if (is_url(p) || is_audio(p)) q.push_back(item_for(p));
        }
        return q;
    }

    // Enter on row i.
    void activate(int i) {
        if (i < 0 || i >= int(entries_.size())) return;
        const Entry e = entries_[size_t(i)];   // a copy: loading replaces entries_
        switch (e.type) {
            case Entry::Place:
                if (!is_dir(e.path)) {
                    // ~/Videos may not exist yet: make the folders that are ours.
                    if (e.path.rfind(home_dir() + "/", 0) == 0) mkdir(e.path.c_str(), 0755);
                    if (!is_dir(e.path)) {
                        shell_toast("Can't open " + e.name);
                        return;
                    }
                }
                place_ = e.path;
                open_dir(e.path);
                break;
            case Entry::Address: open_address(); break;
            case Entry::Dir: open_dir(e.path); break;
            case Entry::List: play_queue(read_playlist(e.path), 0, e.name); break;
            case Entry::Video: open_video(e.path, stem(e.name)); break;
            case Entry::Audio: {
                // The folder plays on from this track (what the search shows, if any).
                std::vector<QueueItem> q;
                int at = 0;
                for (size_t k = 0; k < entries_.size(); ++k) {
                    const Entry& x = entries_[k];
                    if (x.type != Entry::Audio || !shown(x)) continue;
                    if (int(k) == i) at = int(q.size());
                    q.push_back(item_for(x.path));
                }
                play_queue(std::move(q), at, e.name);
                break;
            }
        }
    }

    // X: a folder plays everything in it and under it; anything else as Enter.
    void play_focused() {
        int i = focused_index();
        if (i < 0 || i >= int(entries_.size())) {
            if (!cwd_.empty()) {
                std::vector<QueueItem> q;
                collect(cwd_, 0, q);
                play_queue(std::move(q), 0, crumb_text());
            }
            return;
        }
        const Entry e = entries_[size_t(i)];
        if (e.type == Entry::Dir || (e.type == Entry::Place && is_dir(e.path))) {
            shell_toast("Finding music in " + e.name + "...");
            lv_refr_now(nullptr);
            if (e.type == Entry::Place) place_ = e.path;
            std::vector<QueueItem> q;
            collect(e.path, 0, q);
            if (e.type == Entry::Place && cwd_.empty()) place_.clear();
            play_queue(std::move(q), 0, e.name);
            return;
        }
        activate(i);
    }

    // A stream or a file on a web server: music plays in the queue, video
    // full screen.
    void open_address() {
        ask("Address (http:// or https://)", last_address_, false, [this](const std::string& typed) {
            std::string url = typed;
            while (!url.empty() && url.back() == ' ') url.pop_back();
            while (!url.empty() && url.front() == ' ') url.erase(0, 1);
            if (url.empty()) return;
            if (!is_url(url)) url = "http://" + url;
            last_address_ = url;
            if (is_video(url)) {
                open_video(url, stem(url));
                return;
            }
            std::vector<QueueItem> q = {{url, stem(url).empty() ? url : stem(url), "", -1, ""}};
            play_queue(std::move(q), 0, url);
        });
    }

    // '/': narrows the folder you are in.
    void search() {
        ask("Search this folder", filter_, false, [this](const std::string& text) {
            filter_ = text;
            rebuild_list();
        });
    }

    // One-line text box over the screen. Enter accepts, Esc cancels.
    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, bool password, Done done) {
        done_ = std::move(done);
        input_ = box(root_, 0, 0, 300, 50, kChrome, kGold);
        lv_obj_align(input_, LV_ALIGN_TOP_MID, 0, 30);
        lv_obj_set_style_pad_all(input_, 5, 0);
        label(input_, title, &zs_font_12, kGold);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, password);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 288);
        lv_obj_set_style_bg_color(ta, c(kLcd), 0);
        lv_obj_set_style_text_color(ta, c(kGreen), 0);
        lv_obj_set_style_border_color(ta, c(kChromeHi), 0);
        lv_obj_set_style_radius(ta, 0, 0);
        lv_obj_set_style_pad_ver(ta, 2, 0);
        lv_obj_set_style_bg_color(ta, c(kGreen), LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<MediaApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            // Enter is still down: without this its release would also
            // "click" whichever list row gets the focus next.
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
            self->close_input();
            if (d) d(v);   // may open the next box
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
        // Focus goes back to the list (its first row).
        int n = int(lv_obj_get_child_count(list_));
        for (int i = 0; i < n; ++i) {
            lv_obj_t* ch = lv_obj_get_child(list_, i);
            if (lv_obj_check_type(ch, &lv_list_button_class)) {
                lv_group_focus_obj(ch);
                break;
            }
        }
    }

    void step(int dir) {
        if (player_step(dir)) rebuild_list();
        else activate(focused_index());
    }

    void stop() {
        player_stop();
        rebuild_list();
    }

    void toggle_shuffle() {
        shuffle_ = !shuffle_;
        prefs::set_str("media.shuffle", shuffle_ ? "1" : "0");
        shell_toast(shuffle_ ? "Shuffle on (from the next track you pick)" : "Shuffle off");
        set_actions();
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<MediaApp*>(lv_event_get_user_data(e));
        self->pending_ = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        // Deferred: activating rebuilds the list, which deletes the clicked button.
        lv_async_call([](void* p) {
            auto* s = static_cast<MediaApp*>(p);
            s->activate(s->pending_);
        }, self);
    }

    // ---- Video ----------------------------------------------------------
    // A file on the card, a drive or a share, or an address. mpv decodes it
    // and scales each picture to the screen.

    void open_video(const std::string& path, const std::string& title) {
        close_video(true);
        player_stop();   // music and video share the speaker
        queue_paths_.clear();
        vid_pixels_.assign(size_t(kVidW) * kVidH, 0);
        std::string error;
        if (!hal().video_open(path, "", 0, kVidW, kVidH, error)) {
            shell_toast(error.empty() ? "Can't play " + title : error);
            vid_pixels_.clear();
            return;
        }
        vid_path_ = path;
        vid_name_ = title;
        build_video_view();
        set_actions();
        show_osd();
    }

    void build_video_view() {
        vid_ = lv_obj_create(root_);
        style_plain(vid_);
        lv_obj_set_pos(vid_, 0, 0);
        lv_obj_set_size(vid_, kVidW, kVidH);
        lv_obj_set_style_bg_color(vid_, c(0x000000), 0);
        lv_obj_set_style_bg_opa(vid_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(vid_, LV_OBJ_FLAG_SCROLLABLE);

        vid_dsc_ = lv_image_dsc_t{};
        vid_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        vid_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        vid_dsc_.header.w = kVidW;
        vid_dsc_.header.h = kVidH;
        vid_dsc_.header.stride = kVidW * 2;
        vid_dsc_.data = reinterpret_cast<const uint8_t*>(vid_pixels_.data());
        vid_dsc_.data_size = uint32_t(vid_pixels_.size() * 2);
        vid_img_ = lv_image_create(vid_);
        lv_image_set_src(vid_img_, &vid_dsc_);
        lv_obj_set_pos(vid_img_, 0, 0);

        vid_wait_ = label(vid_, "Loading...", &zs_font_14, kGreen);
        lv_obj_center(vid_wait_);

        // The info bar along the bottom, in the player's colours: title,
        // time, a gold progress line.
        vid_osd_ = box(vid_, 0, kVidH - 40, kVidW, 40, kChrome, kChromeHi);
        lv_obj_set_style_border_side(vid_osd_, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_bg_opa(vid_osd_, LV_OPA_80, 0);
        vid_title_ = label(vid_osd_, vid_name_.c_str(), &zs_font_12, kGreen);
        lv_obj_set_width(vid_title_, kVidW - 12);
        lv_label_set_long_mode(vid_title_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(vid_title_, 6, 2);
        vid_bar_ = thin_bar(vid_osd_, kVidW - 12, 3, kGold);
        lv_obj_set_style_bg_color(vid_bar_, c(kChromeLo), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vid_bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_bar_set_range(vid_bar_, 0, 1000);
        lv_obj_set_pos(vid_bar_, 6, 19);
        vid_time_ = label(vid_osd_, "", &zs_font_12, kGreen);
        lv_obj_set_pos(vid_time_, 6, 24);
        lv_obj_t* hint = label(vid_osd_, "Space pause  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " seek  Esc stop",
                               &zs_font_12, kGoldDim);
        lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -6, 24);

        if (group) lv_group_focus_obj(nullptr);
        // Pictures come faster than the shell's 10 Hz tick.
        vid_timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<MediaApp*>(lv_timer_get_user_data(t))->video_pump();
        }, 20, this);
    }

    void video_pump() {
        if (!vid_ || power().screen_off()) return;   // nobody is looking; the tick pauses it
        if (hal().video_frame(vid_pixels_.data())) lv_obj_invalidate(vid_img_);
    }

    void video_tick() {
        VideoState st = hal().video();
        uint32_t now = lv_tick_get();
        if (!st.open) {
            std::string error = st.error;
            bool finished = st.finished;
            std::string name = vid_name_;
            close_video(true);
            if (!error.empty()) shell_toast(error);
            else if (finished) shell_toast("Finished " + name);
            return;
        }
        if (power().screen_off()) {
            if (!st.paused) hal().video_pause(true);   // face down or timed out: keep the place
        } else if (!st.paused) {
            power().stay_awake(now);
        }

        const char* wait = st.loading ? "Loading..." : st.paused ? LV_SYMBOL_PAUSE "  Paused" : "";
        lv_label_set_text(vid_wait_, wait);
        if (*wait) lv_obj_remove_flag(vid_wait_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(vid_wait_, LV_OBJ_FLAG_HIDDEN);

        bool osd = st.paused || st.loading || now - vid_osd_at_ < kOsdMs;
        if (osd) lv_obj_remove_flag(vid_osd_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(vid_osd_, LV_OBJ_FLAG_HIDDEN);
        if (osd) {
            int permille = st.dur_ms > 0 ? int(1000LL * st.pos_ms / st.dur_ms) : 0;
            lv_bar_set_value(vid_bar_, permille, LV_ANIM_OFF);
            lv_label_set_text(vid_time_, (mmss(st.pos_ms) + " / " + mmss(st.dur_ms) +
                                          fmt("   VOL %d%%", hal().volume()))
                                             .c_str());
        }
    }

    void show_osd() { vid_osd_at_ = lv_tick_get(); }

    bool video_key(uint32_t key) {
        auto& h = hal();
        show_osd();
        switch (key) {
            case ' ':
            case LV_KEY_ENTER:
            case kKeyPlayPause: h.video_pause(!h.video().paused); return true;
            case LV_KEY_LEFT: h.video_seek(-10, false); return true;
            case LV_KEY_RIGHT: h.video_seek(+10, false); return true;
            case LV_KEY_UP: h.video_seek(+60, false); return true;
            case LV_KEY_DOWN: h.video_seek(-60, false); return true;
            case kKeyPrevTrack: h.video_seek(-10, false); return true;
            case kKeyNextTrack: h.video_seek(+30, false); return true;
            case '0': h.video_seek(0, true); return true;
            case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
            case 's': case 'S': case 'v': case 'V': close_video(true); return true;
            case 'm': case 'M': video_to_monitor(); return true;
            case LV_KEY_ESC: return false;   // on_back() closes it
        }
        // The shell's own keys (volume, brightness) still work; the list
        // underneath gets nothing.
        return !is_shell_key(key);
    }

    // Dock mode: the monitor's player has hardware decoding, so a big file
    // plays better there.
    void video_to_monitor() {
        if (!hal().display().dock_running) {
            shell_toast("Start Dock mode for the monitor first");
            return;
        }
        std::string path = vid_path_;
        close_video(true);
        if (!hal().dock_open("video", path)) {
            shell_toast("Can't open the video on the monitor");
            return;
        }
        shell_toast("Playing on the monitor");
    }

    // Stop the video and, with ui, take the picture off the screen.
    void close_video(bool ui) {
        if (vid_timer_) {
            lv_timer_delete(vid_timer_);
            vid_timer_ = nullptr;
        }
        if (!vid_) return;
        hal().video_close();
        if (ui) {
            lv_obj_delete(vid_);
            vid_ = nullptr;
            set_actions();
            rebuild_list(vid_path_);
        }
        vid_ = nullptr;
        vid_pixels_.clear();
        vid_pixels_.shrink_to_fit();
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* crumb_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t *state_ = nullptr, *time_ = nullptr, *marquee_ = nullptr;
    lv_obj_t *kind_ = nullptr, *pos_ = nullptr, *shuf_ = nullptr, *vol_ = nullptr, *seek_ = nullptr;
    lv_obj_t* buttons_[5] = {};
    uint32_t flash_at_[5] = {};
    lv_obj_t* mask_[kBars] = {};
    lv_obj_t* peak_obj_[kBars] = {};
    int level_[kBars] = {}, peak_[kBars] = {}, peak_hold_[kBars] = {};
    lv_timer_t* anim_ = nullptr;
    int idle_frames_ = 0;
    bool playing_ = false;
    std::string shown_marquee_;

    std::string place_;                // the place we are in ("" on the places screen)
    std::string cwd_;                  // the folder shown, "" for the places
    std::vector<Entry> entries_;
    std::string filter_;               // the search, within this folder
    std::vector<std::string> queue_paths_;   // what we queued, for "3/12"
    bool shuffle_ = false;
    std::string listed_playing_;       // what the list last marked as playing
    std::string last_address_;
    int pending_ = -1;
    lv_obj_t* input_ = nullptr;
    Done done_;

    // Video
    lv_obj_t* vid_ = nullptr;   // the player view, over everything; null when no video
    lv_obj_t *vid_img_ = nullptr, *vid_wait_ = nullptr, *vid_osd_ = nullptr, *vid_title_ = nullptr,
             *vid_bar_ = nullptr, *vid_time_ = nullptr;
    lv_timer_t* vid_timer_ = nullptr;
    lv_image_dsc_t vid_dsc_{};
    std::vector<uint16_t> vid_pixels_;
    std::string vid_path_, vid_name_;
    uint32_t vid_osd_at_ = 0;
};
}  // namespace

std::unique_ptr<App> make_media_app() { return std::make_unique<MediaApp>(); }

}  // namespace zs
