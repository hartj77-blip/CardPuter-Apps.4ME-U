// Media: your own music, dressed as a classic desktop MP3 player: an LCD
// with the time and a spectrum analyser, a scrolling title, transport
// buttons, and a file explorer underneath in place of the playlist. It plays
// what is on the device (the SD card), on a USB drive, or on a server whose
// share is mounted (SMB, NFS, SSHFS), and http(s) addresses. Jellyfin has its
// own app. Voice notes are in Recorder. Music only: video files aren't listed.
//
// Two tabs, Left / Right between them (docs/UX.md in the OS):
//   Library   the explorer, a List screen: typing narrows the folder, 1 to 9
//             pick a row, Enter opens or plays, Backspace goes up a folder,
//             Space pauses while nothing is typed.
//             Everything else is in the Tab menu (Ctrl + its letter).
//   Player    a Control screen in the explorer's place: Enter or Space
//             pause, N next, P previous (Left / Right too), S stop,
//             H shuffle, T the next skin, + / - volume. Esc: the Library.
// The keyboard's media keys (Fn + Q / W / E) work on both, and in every app.
//
// Skins (skins.h): besides the classic look, whole-screen pixel-art skins
// with a player screen and a playlist screen; the playlist is the Library
// tab and the player the Player tab (L flips between them too). In a skin
// the app draws the explorer itself, so it moves the selection here rather
// than through LVGL's focus.
//
// Playing goes through the shell's queue (ui/player.h), so it carries on to
// the next track after you leave the app or the screen turns off.
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <random>

#include "apps/common.h"
#include "skins.h"
#include "platform/keys.h"
#include "ui/player.h"
#include "ui/power.h"
#include "ui/prefs.h"

namespace zs {
namespace {
using namespace theme;

// The classic look: gunmetal chrome, a black LCD with green digits, a blue
// bar on the playlist's selected row.
constexpr uint32_t kChrome = 0x262637;
constexpr uint32_t kChromeHi = 0x50506e;
constexpr uint32_t kChromeLo = 0x101018;
constexpr uint32_t kLcd = 0x000000;
constexpr uint32_t kGreen = 0x00e000;
constexpr uint32_t kGreenDim = 0x0f5a0f;
constexpr uint32_t kGold = 0xdcb85a;
constexpr uint32_t kGoldDim = 0x6e5c2c;
constexpr uint32_t kSelect = 0x1414b4;
constexpr uint32_t kSelectText = 0xffffff;
constexpr uint32_t kButton = 0x3a3a55;
constexpr uint32_t kButtonText = 0xc4c4d8;
constexpr uint32_t kFolder = 0xdcb85a;
constexpr uint32_t kSpecLo = 0x10c010, kSpecHi = 0xe01e00, kPeak = 0x9a9aa8;

// The skins T steps through; 0 is the classic look above.
const char* const kSkinNames[] = {"Classic", "Hi-Fi Receiver", "Liquid Chrome"};
constexpr int kSkinCount = 3;

constexpr int kH = kBodyH;            // the soft-key bar takes the bottom
constexpr int kBars = 19;             // spectrum analyser columns
constexpr int kBarH = 18;
constexpr int kLcdW = 116, kLcdH = 50;
constexpr int kRightX = 4 + kLcdW + 4, kRightW = kScreenW - kRightX - 4;
constexpr int kListY = 70;

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
    enum Type { Place, Dir, Audio, List, Address } type = Dir;
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
        if (anim_) lv_timer_delete(anim_);
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        shuffle_ = prefs::get_str("media.shuffle", "0") == "1";
        std::string want = prefs::get_str("media.skin", kSkinNames[0]);
        for (int i = 0; i < kSkinCount; ++i)
            if (want == kSkinNames[i]) skin_ = i;
        // Back where you were last time, if it is still there.
        std::string last = prefs::get_str("media.folder", "");
        std::string base = prefs::get_str("media.place", "");
        if (!last.empty() && !base.empty() && is_dir(last) && last.rfind(base, 0) == 0) {
            place_ = base;
            cwd_ = last;
        }

        build_player();
        anim_ = lv_timer_create([](lv_timer_t* t) { static_cast<MediaApp*>(lv_timer_get_user_data(t))->animate(); },
                                60, this);
        set_actions();
        load();
    }

    void tick() override {
        auto& h = hal();
        PlaybackState pb = h.playback();
        playing_ = !pb.path.empty() && !pb.paused;
        if (skin_) return;   // the skin draws on its own timer

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
            marquee = std::string("ZeroOS Media  ***  ") + kSkinNames[skin_] + "  ***  ";
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
        if (player_page_) update_now(pb, q, at);
        else if (pb.path != listed_playing_ && !input_) rebuild_list();

        uint32_t now = lv_tick_get();
        for (int i = 0; i < 5; ++i)
            if (flash_at_[i] && now - flash_at_[i] > 180) {
                flash_at_[i] = 0;
                lv_obj_set_style_bg_color(buttons_[i], c(kButton), 0);
                lv_obj_set_style_text_color(lv_obj_get_child(buttons_[i], 0), c(kButtonText), 0);
            }
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;   // typing in the text box
        if (skin_ && skin_key(key)) return true;
        // The keyboard's media keys (Fn + Q / W / E), and a headset's when
        // the shell passes them on: the same on both tabs.
        switch (key) {
            case kKeyPlayPause: play_pause(); return true;
            case kKeyPrevTrack:
                flash(kPrev);
                step(-1);
                return true;
            case kKeyNextTrack:
                flash(kNext);
                step(+1);
                return true;
        }
        return on_player() ? player_key(key) : library_key(key);
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (on_player()) {
            show_library();
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

    // The whole player in the current skin. Called again to change it.
    void build_player() {
        if (skin_ == 0) {
            skin_obj_.reset();
            lv_obj_t* frame = box(root_, 0, 0, kScreenW, kH, kChrome, kChrome);
            lv_obj_set_style_border_width(frame, 0, 0);
            build_title_bar(frame);
            build_main_window(frame);
            return;
        }
        skin_obj_ = skin_ == 1 ? media::make_hifi_skin() : media::make_chrome_skin();
        pixels_.assign(size_t(kScreenW) * kH, 0);
        canvas_ = lv_canvas_create(root_);
        lv_canvas_set_buffer(canvas_, pixels_.data(), kScreenW, kH, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(canvas_, 0, 0);
    }

    // T: the next skin. Everything is rebuilt; the place in the list stays.
    void next_skin() {
        if (input_) return;
        int keep = focused_index();
        std::string keep_path = keep >= 0 && keep < int(entries_.size()) ? entries_[size_t(keep)].path : "";
        if (player_page_) keep_path = back_to_;   // the classic Player tab has no list
        bool was_player = on_player();            // stay on the same tab
        skin_ = (skin_ + 1) % kSkinCount;
        prefs::set_str("media.skin", kSkinNames[skin_]);
        lv_group_remove_all_objs(group);
        lv_obj_clean(root_);
        list_ = nullptr;
        now_ = nullptr;
        canvas_ = nullptr;
        for (int i = 0; i < kBars; ++i) level_[i] = peak_[i] = peak_hold_[i] = 0;
        for (auto& f : flash_at_) f = 0;
        shown_marquee_.clear();
        build_player();
        if (skin_) {
            // The skin's own screens stand for the tabs.
            player_page_ = false;
            list_view_ = !was_player;
            set_actions();
            rebuild_list(keep_path, false);
            render_skin();
        } else {
            player_page_ = was_player;
            set_actions();
            if (player_page_) {
                back_to_ = keep_path;
                build_now();
                lv_label_set_text(crumb_, "PLAYER");
                lv_obj_align(crumb_, LV_ALIGN_TOP_RIGHT, -6, 0);
            } else {
                rebuild_list(keep_path, false);
            }
            tick();
        }
        shell_toast(std::string("Skin: ") + kSkinNames[skin_]);
    }

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
            // Each column is the whole gradient (the theme's bottom colour
            // to its top one), covered from the top down to its level; a
            // cap marks the peak.
            lv_obj_t* g = box(lcd, x, y, 4, kBarH, kSpecHi, 0);
            lv_obj_set_style_border_width(g, 0, 0);
            lv_obj_set_style_bg_grad_color(g, c(kSpecLo), 0);
            lv_obj_set_style_bg_main_stop(g, 40, 0);
            lv_obj_set_style_bg_grad_dir(g, LV_GRAD_DIR_VER, 0);
            mask_[i] = box(lcd, x, y, 4, kBarH, kLcd, 0);
            lv_obj_set_style_border_width(mask_[i], 0, 0);
            peak_obj_[i] = box(lcd, x, y + kBarH - 1, 4, 1, kPeak, 0);
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
        if (skin_) {
            skin_flash_ = b;
            skin_flash_at_ = lv_tick_get();
            return;
        }
        flash_at_[b] = lv_tick_get() | 1;
        lv_obj_set_style_bg_color(buttons_[b], c(kGold), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(buttons_[b], 0), c(kChrome), 0);
        for (int i = 0; i < 5; ++i)
            if (i != b) lv_obj_set_style_text_color(lv_obj_get_child(buttons_[i], 0), c(kButtonText), 0);
    }

    // The analyser: lively while something plays, falling flat otherwise.
    // (It is for the look: the player doesn't hand over the sound itself.)
    void animate() {
        if (power().screen_off()) return;
        if (skin_) {
            // Full speed while music plays; otherwise a few frames a second
            // keep the slow things moving (keys redraw at once anyway).
            if (playing_ || ++idle_frames_ % 4 == 0) render_skin();
            return;
        }
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

    // The Library is a List screen: letters narrow it, so its actions are
    // in the menu only. The Player is a Control screen: its letters work as
    // they are, and the bar shows them.
    void set_actions() {
        std::vector<Action> a;
        auto shuffle = [this] { return std::string(shuffle_ ? "Shuffle off" : "Shuffle on"); };
        auto skin = [this] { return std::string("Skin: ") + kSkinNames[skin_] + " (next)"; };
        if (on_player()) {
            a.push_back({"Play / pause", 0, "Enter", nullptr});
            a.push_back({"Next", 'n', "N", [this] { next(); }});
            a.push_back({"Previous", 'p', "P", [this] { previous(); }});
            a.push_back({"Library", 0, "Esc", nullptr});
            a.push_back({"Play / pause", 0, "Space", nullptr});
            a.push_back({"Previous / next", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
            a.push_back({"Volume", 0, "+ -", nullptr});
            a.push_back({"Stop", 's', "S", [this] { stop(); }});
            a.push_back({shuffle(), 'h', "H", [this] { toggle_shuffle(); }});
            a.push_back({skin(), 't', "T", [this] { next_skin(); }});
            a.push_back({"Open an address", 'o', nullptr, [this] { open_address(); }});
        } else {
            a.push_back({"Open", 0, "Enter", nullptr});
            a.push_back({"Player", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
            if (!cwd_.empty()) a.push_back({"Up", 0, "Bksp", nullptr});
            a.push_back({"Play all", 'a', nullptr, [this] {
                             flash(kPlay);
                             play_focused();
                         }});
            a.push_back({"Pause", 'u', "Space", [this] { play_pause(); }});
            a.push_back({"Next track", 'n', nullptr, [this] { next(); }});
            a.push_back({"Previous track", 'p', nullptr, [this] { previous(); }});
            a.push_back({"Stop", 's', nullptr, [this] { stop(); }});
            a.push_back({shuffle(), 'h', nullptr, [this] { toggle_shuffle(); }});
            a.push_back({skin(), 't', nullptr, [this] { next_skin(); }});
            a.push_back({"Open an address", 'o', nullptr, [this] { open_address(); }});
            a.push_back({"Go to places", 'g', nullptr, [this] { go_places(); }});
            a.push_back({"Rescan", 'r', nullptr, [this] {
                             int i = focused_index();
                             load(i >= 0 && i < int(entries_.size()) ? entries_[size_t(i)].path : "");
                         }});
        }
        shell_set_actions(std::move(a));
    }

    // ---- Keys -------------------------------------------------------------

    // The Library: a List screen. Letters, space and punctuation narrow the
    // folder; 1 to 9 pick one of the first nine rows; Left / Right go to
    // the Player tab.
    bool library_key(uint32_t key) {
        switch (key) {
            case LV_KEY_LEFT:
            case LV_KEY_RIGHT:
                show_player();
                return true;
            case ' ':
                if (!filter_.empty()) break;   // a space in the search
                play_pause();                  // as in every ZeroOS list (ui/list_view.h)
                return true;
            case LV_KEY_BACKSPACE:
                if (!filter_.empty()) {
                    filter_.pop_back();
                    rebuild_list("", false);
                } else {
                    up();
                }
                return true;
        }
        if (key >= '1' && key <= '9') {
            if (!skin_) pick(int(key - '1'));   // a skin's preset keys are in skin_key()
            return true;
        }
        if (key >= 32 && key < 127 && key != '0') {   // 0 is the shell's key sheet
            filter_ += char(key);
            rebuild_list("", false);
            return true;
        }
        return false;
    }

    // The Player: a Control screen. Its letters are declared in set_actions().
    bool player_key(uint32_t key) {
        auto& h = hal();
        switch (key) {
            case LV_KEY_ENTER:
            case '\r':
            case ' ': play_pause(); return true;
            case 'n': case 'N':
            case LV_KEY_RIGHT: next(); return true;
            case 'p': case 'P':
            case LV_KEY_LEFT: previous(); return true;
            case 's': case 'S': stop(); return true;
            case 'h': case 'H': toggle_shuffle(); return true;
            case 't': case 'T': next_skin(); return true;
            case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
        }
        return false;
    }

    // Pause or resume; with nothing playing, the Library's row plays (or,
    // on the Player, the Library comes back to pick something).
    void play_pause() {
        auto& h = hal();
        PlaybackState pb = h.playback();
        flash(!pb.path.empty() && !pb.paused ? kPause : kPlay);
        if (!pb.path.empty()) h.set_paused(!pb.paused);
        else if (on_player()) show_library();
        else activate(focused_index());
    }

    void next() {
        flash(kNext);
        step(+1);
    }

    void previous() {
        flash(kPrev);
        step(-1);
    }

    // The n-th row the search leaves (0 = the first): focus it, then as
    // Enter on it.
    void pick(int n) {
        if (skin_) {
            for (size_t r = 0; r < row_entry_.size(); ++r)
                if (row_entry_[r] >= 0 && n-- == 0) {
                    sel_ = int(r);
                    scroll_to_sel();
                    activate(row_entry_[r]);
                    return;
                }
            return;
        }
        if (!list_) return;
        int rows = int(lv_obj_get_child_count(list_));
        for (int k = 0; k < rows; ++k) {
            lv_obj_t* ch = lv_obj_get_child(list_, k);
            if (!lv_obj_check_type(ch, &lv_list_button_class) || n-- > 0) continue;
            lv_group_focus_obj(ch);
            lv_obj_scroll_to_view(ch, LV_ANIM_OFF);
            activate(int((intptr_t)lv_obj_get_user_data(ch)));
            return;
        }
    }

    // ---- The two tabs -------------------------------------------------------

    // Which tab is up. In a skin its player screen is the Player tab and its
    // playlist screen the Library.
    bool on_player() const { return skin_ ? !list_view_ : player_page_; }

    void show_player() {
        if (on_player()) return;
        if (skin_) {
            show_list(false);
            return;
        }
        if (list_) {
            int i = focused_index();
            back_to_ = i >= 0 && i < int(entries_.size()) ? entries_[size_t(i)].path : "";
            lv_group_remove_all_objs(group);
            lv_obj_delete(list_);
            list_ = nullptr;
        }
        player_page_ = true;
        build_now();
        lv_label_set_text(crumb_, "PLAYER");
        lv_obj_align(crumb_, LV_ALIGN_TOP_RIGHT, -6, 0);
        set_actions();
        tick();
    }

    void show_library() {
        if (!on_player()) return;
        if (skin_) {
            show_list(true);
            return;
        }
        player_page_ = false;
        if (now_) lv_obj_delete(now_);
        now_ = nullptr;
        set_actions();
        rebuild_list(back_to_, false);
    }

    // The Player tab, where the explorer was: what is playing, in big
    // letters, where it is in the queue, and what comes next.
    void build_now() {
        now_ = box(root_, 4, kListY, kScreenW - 8, kH - kListY - 2, kLcd, kChromeHi);
        lv_obj_set_style_border_side(now_, lv_border_side_t(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT), 0);
        auto line = [&](int y, const lv_font_t* font, uint32_t color) {
            lv_obj_t* l = label(now_, "", font, color);
            lv_obj_set_pos(l, 6, y);
            lv_obj_set_width(l, kScreenW - 22);
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            return l;
        };
        now_title_ = line(2, &zs_font_14, kGreen);
        now_artist_ = line(20, &zs_font_12, kGreenDim);
        now_info_ = line(34, &zs_font_12, kGreen);
        now_next_ = line(48, &zs_font_12, kGreenDim);
        now_shown_.clear();
    }

    void update_now(const PlaybackState& pb, const QueueItem* q, int at) {
        if (!now_) return;
        std::string title, artist, info, next;
        if (pb.path.empty()) {
            title = "Nothing playing";
            artist = "Pick some music in the Library";
        } else {
            title = q ? q->title : stem(pb.path);
            artist = q ? q->artist : "";
            info = mmss(pb.pos_ms) + " / " + mmss(pb.dur_ms) + "   " + format_of(pb.path);
            if (at >= 0) info += fmt("   %d of %zu", at + 1, queue_paths_.size());
            if (pb.paused) info += "   PAUSED";
            if (at >= 0 && queue_paths_.size() > 1)
                next = "Next: " + stem(queue_paths_[size_t(at + 1) % queue_paths_.size()]);
        }
        std::string all = title + '\n' + artist + '\n' + info + '\n' + next;
        if (all == now_shown_) return;   // ten times a second: only redraw what changed
        now_shown_ = all;
        lv_label_set_text(now_title_, title.c_str());
        lv_label_set_text(now_artist_, artist.c_str());
        lv_label_set_text(now_info_, info.c_str());
        lv_label_set_text(now_next_, next.c_str());
    }

    static std::string format_of(const std::string& path) {
        std::string e = ext_of(path);
        if (e.size() < 2 || e.size() > 5) return is_url(path) ? "NET" : "---";
        std::string up = e.substr(1);
        for (auto& ch : up) ch = char(std::toupper((unsigned char)ch));
        return up;
    }

    // ---- Skins ------------------------------------------------------------

    // The explorer as rows for the skin; select keep_path, else what is
    // playing, else the first row.
    void skin_rows(const std::string& keep_path) {
        rows_.clear();
        row_entry_.clear();
        std::string playing = hal().playback().path;
        listed_playing_ = playing;
        auto add_note = [&](const std::string& t) {
            media::Row r;
            r.text = t;
            r.note = true;
            rows_.push_back(r);
            row_entry_.push_back(-1);
        };
        if (!cwd_.empty() && entries_.empty()) {
            add_note(is_dir(cwd_) ? "No music here" : "Can't open this folder");
            if (place_ == home_dir() + "/Music") add_note("Copy music to ~/Music on the SD card");
        }
        int files = 0, focus = -1, now = -1;
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            if (!shown(e)) continue;
            media::Row r;
            r.path = e.path;
            switch (e.type) {
                case Entry::Place:
                case Entry::Address:
                    r.icon = e.icon;
                    r.text = e.name;
                    r.right = e.note;
                    r.folder = true;
                    break;
                case Entry::Dir:
                    r.icon = LV_SYMBOL_DIRECTORY;
                    r.text = e.name;
                    r.folder = true;
                    break;
                case Entry::List:
                    r.icon = LV_SYMBOL_LIST;
                    r.text = stem(e.name);
                    r.right = "M3U";
                    break;
                case Entry::Audio:
                    r.text = fmt("%d. ", ++files) + stem(e.name);
                    r.right = format_of(e.name);
                    break;
            }
            if (!keep_path.empty() && e.path == keep_path) focus = int(rows_.size());
            if (!playing.empty() && e.path == playing) now = int(rows_.size());
            rows_.push_back(r);
            row_entry_.push_back(int(i));
        }
        if (!entries_.empty() && rows_.empty()) add_note("Nothing matches \"" + filter_ + "\" (Esc clears)");
        sel_ = focus >= 0 ? focus : now >= 0 ? now : 0;
        // The first row that can be opened, not a note.
        while (sel_ < int(rows_.size()) - 1 && row_entry_[size_t(sel_)] < 0) ++sel_;
        top_ = 0;
        scroll_to_sel();
        render_skin();
    }

    void scroll_to_sel() {
        int shown_n = skin_obj_ ? skin_obj_->rows_shown() : 6;
        if (sel_ < top_) top_ = sel_;
        if (sel_ >= top_ + shown_n) top_ = sel_ - shown_n + 1;
        top_ = std::max(0, std::min(top_, int(rows_.size()) - shown_n));
    }

    void show_list(bool on) {
        if (list_view_ == on) return;
        list_view_ = on;
        set_actions();
        render_skin();
    }

    void move_sel(int d) {
        if (rows_.empty()) return;
        sel_ = std::max(0, std::min(int(rows_.size()) - 1, sel_ + d));
        scroll_to_sel();
        render_skin();
    }

    // Keys a skin handles itself: the moving about LVGL's focus does in the
    // classic look, the screen flip, and the preset keys 1-6.
    bool skin_key(uint32_t key) {
        int page = skin_obj_ ? skin_obj_->rows_shown() : 6;
        switch (key) {
            case 'l': case 'L':
                if (list_view_) break;   // on the playlist (the Library) letters search
                show_list(true);
                return true;
            case LV_KEY_UP:
            case LV_KEY_DOWN:
                if (!list_view_) show_list(true);
                else move_sel(key == LV_KEY_UP ? -1 : 1);
                return true;
            case kKeyPageUp:
            case kKeyPageDown:
                show_list(true);
                move_sel(key == kKeyPageUp ? -page : page);
                return true;
            case LV_KEY_ENTER:
                if (!list_view_) break;   // the Player tab: play / pause
                activate(focused_index());
                return true;
        }
        // On the playlist, 1-6 are the preset keys: open that row.
        if (list_view_ && key >= '1' && key < uint32_t('1' + page)) {
            int r = top_ + int(key - '1');
            if (r < int(rows_.size())) {
                sel_ = r;
                activate(focused_index());
            }
            return true;
        }
        return false;
    }

    // One frame of the skin: what is playing, and the explorer.
    void render_skin() {
        if (!skin_obj_ || !canvas_) return;
        auto& h = hal();
        PlaybackState pb = h.playback();
        // Opening and playing reload the list; the rows follow what is playing.
        if (pb.path != listed_playing_) {
            listed_playing_ = pb.path;
            for (size_t i = 0; i < rows_.size(); ++i)
                if (!pb.path.empty() && rows_[i].path == pb.path && !list_view_) {
                    sel_ = int(i);
                    scroll_to_sel();
                }
        }
        media::View v;
        v.t = float(lv_tick_get() % 3600000u) / 1000.f;
        v.stopped = pb.path.empty();
        v.paused = pb.paused;
        v.pos_ms = pb.pos_ms;
        v.dur_ms = pb.dur_ms;
        v.playing_path = pb.path;
        if (!v.stopped) {
            const QueueItem* q = player_current();
            v.title = q ? q->title : stem(pb.path);
            v.artist = q ? q->artist : "";
            if (!is_url(pb.path)) {
                std::string dir = parent_of(pb.path);
                std::string album = dir.substr(dir.rfind('/') + 1);
                if (album != v.artist) v.album = album;
            }
            v.format = format_of(pb.path);
            for (size_t i = 0; i < queue_paths_.size(); ++i)
                if (queue_paths_[i] == pb.path) v.q_at = int(i);
            if (v.q_at >= 0) v.q_n = int(queue_paths_.size());
        }
        v.shuffle = shuffle_;
        v.muted = h.muted();
        v.volume = h.volume();
        if (skin_flash_ >= 0 && lv_tick_get() - skin_flash_at_ < 180) v.flash = skin_flash_;
        v.rows = &rows_;
        v.sel = sel_;
        v.top = top_;
        v.crumb = crumb_text();
        v.list = list_view_;
        skin_obj_->draw(canvas_, pixels_.data(), v);
        lv_obj_invalidate(canvas_);
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
        set_actions();   // Backspace goes up a folder only in a folder
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

    // pick: the row's place among those shown; the first nine show the
    // number that picks them (docs/UX.md: number badges).
    lv_obj_t* add_row(const char* symbol, const std::string& text, const std::string& right, int index,
                      int pick, uint32_t color, bool now = false) {
        lv_obj_t* b = list_button(list_, symbol, text.c_str(), on_click, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)index);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        for (lv_style_selector_t st : {lv_style_selector_t(LV_STATE_FOCUSED), lv_style_selector_t(LV_STATE_FOCUS_KEY),
                                       lv_style_selector_t(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY)}) {
            lv_obj_set_style_bg_color(b, c(kSelect), st);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, st);
            lv_obj_set_style_text_color(b, c(kSelectText), st);
        }
        lv_obj_set_style_text_color(b, c(now ? kSelectText : color), 0);
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
        // The number, first in the row, in the row's colour like the rest.
        // In a box of its own, so the row's name (the menu's title, for
        // one) is still its first label.
        lv_obj_t* badge = lv_obj_create(b);
        style_plain(badge);
        lv_obj_set_size(badge, 8, LV_SIZE_CONTENT);
        lv_obj_remove_flag(badge, lv_obj_flag_t(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        lv_obj_t* num = lv_label_create(badge);
        lv_label_set_text(num, pick < 9 ? std::to_string(pick + 1).c_str() : "");
        lv_obj_set_style_text_font(num, &zs_font_12, 0);
        lv_obj_move_to_index(badge, 0);
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
        if (player_page_) return;   // the Library rebuilds it on the way back
        int keep = keep_focus ? focused_index() : -1;
        std::string keep_path = !focus_path.empty() ? focus_path
                                : keep >= 0 && keep < int(entries_.size()) ? entries_[size_t(keep)].path
                                                                           : std::string();
        if (skin_) {
            skin_rows(keep_path);
            return;
        }
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
        int shown_n = 0;
        if (!cwd_.empty() && entries_.empty()) {
            note(is_dir(cwd_) ? "No music here" : "Can't open this folder");
            if (place_ == home_dir() + "/Music") note("Copy music to ~/Music on the SD card");
        }
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            if (!shown(e)) continue;
            int n = shown_n++;
            lv_obj_t* b = nullptr;
            switch (e.type) {
                case Entry::Place:
                case Entry::Address:
                    b = add_row(e.icon, e.name, e.note, int(i), n, kGreen);
                    break;
                case Entry::Dir: b = add_row(LV_SYMBOL_DIRECTORY, e.name, "", int(i), n, kFolder); break;
                case Entry::List: b = add_row(LV_SYMBOL_LIST, stem(e.name), "M3U", int(i), n, kGreen); break;
                case Entry::Audio: {
                    bool now = e.path == listed_playing_;
                    b = add_row(now ? LV_SYMBOL_PLAY : nullptr, stem(e.name), format_of(e.name), int(i), n, kGreen,
                                now);
                    if (now) now_row = b;
                    break;
                }
            }
            if (!keep_path.empty() && e.path == keep_path) focus = b;
            if (!focus && keep_path.empty() && int(i) == keep) focus = b;
        }
        if (!entries_.empty() && shown_n == 0) note("Nothing matches \"" + filter_ + "\"");
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
        if (skin_) return sel_ >= 0 && sel_ < int(row_entry_.size()) ? row_entry_[size_t(sel_)] : -1;
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
                    // ~/Music may not exist yet: make the folders that are ours.
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

    // A stream or a file on a web server: it plays in the queue.
    void open_address() {
        ask("Address (http:// or https://)", last_address_, false, [this](const std::string& typed) {
            std::string url = typed;
            while (!url.empty() && url.back() == ' ') url.pop_back();
            while (!url.empty() && url.front() == ' ') url.erase(0, 1);
            if (url.empty()) return;
            if (!is_url(url)) url = "http://" + url;
            last_address_ = url;
            std::vector<QueueItem> q = {{url, stem(url).empty() ? url : stem(url), "", -1, ""}};
            play_queue(std::move(q), 0, url);
        });
    }

    // One-line text box over the screen (a Text screen while it is up).
    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, bool password, Done done) {
        done_ = std::move(done);
        shell_set_actions({{"OK", 0, "Enter", nullptr}, {"Cancel", 0, "Esc", nullptr}});
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
        set_actions();
        if (!list_) return;   // the Player tab, or a skin: no LVGL list
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
    int skin_ = 0;                     // kSkinNames[skin_]; 0 is the classic look

    // A skin: it draws the whole app area into one canvas.
    std::unique_ptr<media::Skin> skin_obj_;
    lv_obj_t* canvas_ = nullptr;
    std::vector<uint16_t> pixels_;
    std::vector<media::Row> rows_;     // what the explorer shows, as the skin draws it
    std::vector<int> row_entry_;       // rows_ -> entries_ index (-1: a note)
    int sel_ = 0, top_ = 0;            // the selected row; the first shown
    bool list_view_ = false;           // the skin's playlist screen, else its player
    int skin_flash_ = -1;
    uint32_t skin_flash_at_ = 0;
    std::string listed_playing_;       // what the list last marked as playing
    std::string last_address_;
    int pending_ = -1;
    lv_obj_t* input_ = nullptr;
    Done done_;

    // The Player tab, in the explorer's place while it is open.
    bool player_page_ = false;
    lv_obj_t* now_ = nullptr;
    lv_obj_t *now_title_ = nullptr, *now_artist_ = nullptr, *now_info_ = nullptr, *now_next_ = nullptr;
    std::string now_shown_;
    std::string back_to_;              // the Library row to select on the way back

};
}  // namespace

std::unique_ptr<App> make_media_app() { return std::make_unique<MediaApp>(); }

}  // namespace zs
