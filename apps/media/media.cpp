// Media: music from ~/Music and music and video from a Jellyfin server, in
// two tabs, dressed as a classic desktop MP3 player: a green LCD with the
// time and a spectrum analyser, a scrolling title, transport buttons, and
// the playlist underneath. Voice notes moved to the Recorder app.
//
//   Left/Right   Library / Jellyfin tab   Up/Down   choose
//   Enter        play / open              /         search
//   Z X C V B    previous, play, pause, stop, next (the old desktop keys)
//   Space        pause / resume           N / P     next / previous
//   S            shuffle on or off        + / -     volume
//   Fn+Q/W/E     the keyboard's media keys: play/pause, previous, next
//   Esc          back one level, or clear the search
//   Tab          the menu (search, shuffle, stop, refresh, sign out)
//
// Playing goes through the shell's queue (ui/player.h), so it carries on to
// the next track after you leave the app or the screen turns off.
//
// A Jellyfin movie or episode plays full width in the app (README.md):
//   Space pause   Left/Right -/+10 s   Up/Down +/-1 min   0 from the start
//   Fn+Q pause    Fn+W/E -10/+30 s     + / - volume   M on the monitor
//   Esc or S stop
// Video belongs to the app: it stops when you leave Media.
#include <algorithm>
#include <cctype>
#include <cstdlib>
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

constexpr int kH = kBodyH;            // the soft-key bar takes the bottom
constexpr int kBars = 19;             // spectrum analyser columns
constexpr int kBarH = 18;
constexpr int kLcdW = 116, kLcdH = 50;
constexpr int kRightX = 4 + kLcdW + 4, kRightW = kScreenW - kRightX - 4;
constexpr int kListY = 70;

// The video picture: the whole content area. 16:9 fills it at 267 x 150.
constexpr int kVidW = kScreenW;
constexpr int kVidH = kContentH;
// What we ask the server for: a picture about the size of the screen (it
// scales down, never up) at a bitrate Wi-Fi on a Pi Zero 2 keeps up with.
constexpr int kStreamW = 427, kStreamH = 240, kStreamKbps = 700;
// And for the monitor in Dock mode: 720p, decoded in hardware there.
constexpr int kDockW = 1280, kDockH = 720, kDockKbps = 4000;
constexpr uint32_t kReportEveryMs = 30000;

// Library lists came with a later ZeroOS than some this app is built for
// (releases build against the newest released ZeroOS too). The values were
// added at the end of their enums, so they are spelled by number here; an
// older ZeroOS answers "Nothing to open" and the fixed rows stay.
constexpr JfView kViewLibraries = JfView(13);   // JfView::Libraries
constexpr JfView kViewLibrary = JfView(14);     // JfView::Library
constexpr JfItem::Kind kFolder = JfItem::Kind(9);   // JfItem::Folder
constexpr int kPage = 200;                       // kJfPage: rows in one reply
constexpr uint32_t kOsdMs = 3000;   // the info bar stays up this long after a key

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

class MediaApp : public App {
public:
    ~MediaApp() override {
        // Leaving the app: the screen is being torn down, so only stop the
        // video and tell the server where we got to.
        if (anim_) lv_timer_delete(anim_);
        close_video(false);
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        shuffle_ = prefs::get_str("media.shuffle", "0") == "1";

        jf_.server = prefs::get_str("jellyfin.server", "");
        jf_.user = prefs::get_str("jellyfin.user", "");
        jf_.user_id = prefs::get_str("jellyfin.user_id", "");
        jf_.token = prefs::get_str("jellyfin.token", "");
        jf_.device_id = prefs::get_str("jellyfin.device", "");
        if (jf_.device_id.empty()) {
            std::random_device rd;
            jf_.device_id = fmt("zeroos-%08x%08x", unsigned(rd()), unsigned(rd()));
            prefs::set_str("jellyfin.device", jf_.device_id);
        }

        lv_obj_t* frame = box(root, 0, 0, kScreenW, kH, kChrome, kChrome);
        lv_obj_set_style_border_width(frame, 0, 0);
        build_title_bar(frame);
        build_main_window(frame);

        anim_ = lv_timer_create([](lv_timer_t* t) { static_cast<MediaApp*>(lv_timer_get_user_data(t))->animate(); },
                                60, this);
        show_tab(0);
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
            marquee = "ZeroOS Media  ***  / to search, Enter to play  ***  ";
        } else {
            std::string name = q ? q->title : pb.path.substr(pb.path.rfind('/') + 1);
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
                lv_obj_set_style_text_color(buttons_[i], c(kButtonText), 0);
            }
    }

    bool on_key(uint32_t key) override {
        if (vid_) return video_key(key);
        if (input_) return false;   // typing in the text box
        auto& h = hal();
        switch (key) {
            case LV_KEY_LEFT: show_tab((tab_ + kTabs - 1) % kTabs); return true;
            case LV_KEY_RIGHT: show_tab((tab_ + 1) % kTabs); return true;
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
                activate(focused_index());
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
        if (tab_ == kLibraryTab && !filter_.empty()) {
            filter_.clear();
            rebuild_list();
            return true;
        }
        if (tab_ == kJellyfinTab && !stack_.empty()) {
            stack_.pop_back();
            rebuild_list();
            return true;
        }
        return false;
    }

private:
    static constexpr int kTabs = 2;
    static constexpr int kLibraryTab = 0, kJellyfinTab = 1;
    enum Button { kPrev, kPlay, kPause, kStop, kNext };
    static constexpr int kMoreRow = -2;   // "More..." at the end of a long list

    // One screen of Jellyfin results; the stack of them is where you are.
    struct Level {
        std::string title;
        std::vector<JfItem> items;
        JfView view = JfView::Latest;
        std::string arg;          // what was asked, without the page
        bool more = false;        // came back a whole page long
    };

    // The Jellyfin tab's first screen: fixed rows around the user's libraries.
    enum RootKind { kSearch, kResume, kLibrary, kMovies, kShows, kAlbums, kLatest, kArtists, kPlaylists };
    struct RootRow {
        RootKind kind;
        std::string text;
        const char* icon;
        JfItem lib;   // kLibrary
    };

    // ---- Building the screen --------------------------------------------

    void build_title_bar(lv_obj_t* frame) {
        static const char* const names[kTabs] = {"LIBRARY", "JELLYFIN"};
        int x = 6;
        for (int i = 0; i < kTabs; ++i) {
            tabs_[i] = label(frame, names[i], &zs_font_12, kGoldDim);
            lv_obj_set_pos(tabs_[i], x, 0);
            lv_obj_update_layout(tabs_[i]);
            x += lv_obj_get_width(tabs_[i]) + 10;
        }
        // The grip ridges between the tabs and where you are.
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
            lv_obj_set_style_text_color(buttons_[i], c(kButtonText), 0);
            lv_obj_center(label(buttons_[i], syms[i], &zs_font_12, kButtonText));
            lv_obj_set_style_text_color(lv_obj_get_child(buttons_[i], 0), c(kButtonText), 0);
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
        a.push_back({"Play", 0, "Enter", nullptr});
        a.push_back({"Search", 'f', "/", [this] { search(); }});
        a.push_back({"Pause", 0, "Space", nullptr});
        a.push_back({"Previous", 0, "Z", nullptr});
        a.push_back({"Play focused", 0, "X", nullptr});
        a.push_back({"Pause", 0, "C", nullptr});
        a.push_back({"Stop", 't', "V", [this] { stop(); }});
        a.push_back({"Next", 0, "B", nullptr});
        a.push_back({shuffle_ ? "Shuffle off" : "Shuffle on", 's', "S", [this] { toggle_shuffle(); }});
        a.push_back({"Volume", 0, "+ -", nullptr});
        a.push_back({"Library / Jellyfin", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
        if (tab_ == kJellyfinTab && jf_.signed_in()) {
            a.push_back({"Refresh libraries", 'r', nullptr, [this] {
                             libs_tried_ = false;
                             stack_.clear();
                             rebuild_list();
                         }});
            a.push_back({"Sign out", 'o', nullptr, [this] { sign_out(); }, true, true,
                         "Sign out of Jellyfin?"});
        } else if (tab_ == kJellyfinTab) {
            a.push_back({"Sign in", 'i', nullptr, [this] { sign_in(); }});
        }
        shell_set_actions(std::move(a));
    }

    void show_tab(int t) {
        tab_ = t;
        for (int i = 0; i < kTabs; ++i) lv_obj_set_style_text_color(tabs_[i], c(i == t ? kGold : kGoldDim), 0);
        set_actions();
        if (t == kJellyfinTab) load_libraries();
        rebuild_list();
    }

    static std::string format_of(const std::string& path) {
        if (path.rfind("http", 0) == 0) return "NET";
        auto dot = path.rfind('.');
        if (dot == std::string::npos || path.size() - dot > 5) return "---";
        std::string ext = path.substr(dot + 1);
        for (auto& ch : ext) ch = char(std::toupper((unsigned char)ch));
        return ext;
    }

    // ---- The playlist -----------------------------------------------------

    lv_obj_t* add_row(const char* symbol, const std::string& text, const std::string& right, int index,
                      bool now = false) {
        lv_obj_t* b = list_button(list_, symbol, text.c_str(), on_click, this);
        lv_obj_set_user_data(b, (void*)(intptr_t)index);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        for (lv_style_selector_t st : {lv_style_selector_t(LV_STATE_FOCUSED), lv_style_selector_t(LV_STATE_FOCUS_KEY),
                                       lv_style_selector_t(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY)}) {
            lv_obj_set_style_bg_color(b, c(kSelect), st);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, st);
            lv_obj_set_style_text_color(b, c(0xffffff), st);
        }
        lv_obj_set_style_text_color(b, c(now ? 0xffffff : kGreen), 0);
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

    void rebuild_list() {
        int keep = focused_index();
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
        lv_obj_t* focus = tab_ == kJellyfinTab ? build_jellyfin(keep) : build_library(keep);
        if (focus) lv_group_focus_obj(focus);
        if (input_) lv_obj_move_foreground(input_);
        update_crumb();
    }

    void update_crumb() {
        std::string t;
        if (tab_ == kLibraryTab) t = filter_.empty() ? "~/Music" : "\"" + filter_ + "\"";
        else if (!jf_.signed_in()) t = "Not signed in";
        else if (stack_.empty()) t = host_of(jf_.server);
        else t = stack_.back().title;
        if (t.size() > 26) t = t.substr(0, 25) + "...";   // the ridges keep the rest
        lv_label_set_text(crumb_, t.c_str());
        lv_obj_align(crumb_, LV_ALIGN_TOP_RIGHT, -6, 0);
    }

    lv_obj_t* build_library(int keep) {
        music_ = hal().music_library();
        lv_obj_t* focus = nullptr;
        if (music_.empty()) note("Copy music to ~/Music");
        std::string f = lower(filter_);
        int shown = 0;
        for (size_t i = 0; i < music_.size(); ++i) {
            const Track& t = music_[i];
            if (!f.empty() && lower(t.title + " " + t.artist).find(f) == std::string::npos) continue;
            bool now = t.path == listed_playing_;
            std::string text = fmt("%zu. ", i + 1) + (t.artist.empty() ? "" : t.artist + " - ") + t.title;
            lv_obj_t* b = add_row(nullptr, text, t.duration_s > 0 ? mmss(t.duration_s * 1000) : "", int(i), now);
            if (now || (!focus && int(i) == keep)) focus = b;
            ++shown;
        }
        if (!music_.empty() && shown == 0) note("Nothing matches \"" + filter_ + "\" (Esc clears)");
        return focus;
    }

    // ---- Jellyfin ---------------------------------------------------------

    static std::string host_of(const std::string& url) {
        auto a = url.find("://");
        std::string h = a == std::string::npos ? url : url.substr(a + 3);
        return h.substr(0, h.find_first_of(":/"));
    }

    static const char* folder_icon(const std::string& type) {
        if (type == "music") return LV_SYMBOL_AUDIO;
        if (type == "movies" || type == "tvshows" || type == "homevideos" || type == "musicvideos")
            return LV_SYMBOL_VIDEO;
        if (type == "playlists") return LV_SYMBOL_LIST;
        if (type == "books") return LV_SYMBOL_FILE;
        return LV_SYMBOL_DIRECTORY;
    }

    // The user's libraries, asked for once (Refresh in the menu asks again).
    // A ZeroOS too old to know them, or a server that won't say, leaves the
    // fixed Movies / TV shows / Albums rows in their place.
    void load_libraries() {
        if (libs_tried_ || !jf_.signed_in()) return;
        libs_tried_ = true;
        libs_.clear();
        shell_toast("Loading libraries...");
        lv_refr_now(nullptr);
        std::string error;
        if (!hal().jellyfin_list(jf_, kViewLibraries, "", libs_, error)) {
            libs_.clear();
            // "Nothing to open": this ZeroOS predates library lists.
            if (error != "Nothing to open") shell_toast(error);
        }
    }

    std::vector<RootRow> root_rows() const {
        std::vector<RootRow> r;
        r.push_back({kSearch, "Search...", ZS_ICON_SEARCH, {}});
        r.push_back({kResume, "Continue watching", LV_SYMBOL_PLAY, {}});
        if (libs_.empty()) {
            r.push_back({kMovies, "Movies", LV_SYMBOL_VIDEO, {}});
            r.push_back({kShows, "TV shows", LV_SYMBOL_VIDEO, {}});
            r.push_back({kAlbums, "Albums", LV_SYMBOL_AUDIO, {}});
        }
        for (const JfItem& lib : libs_) r.push_back({kLibrary, lib.name, folder_icon(lib.detail), lib});
        r.push_back({kLatest, "Latest albums", LV_SYMBOL_BELL, {}});
        r.push_back({kArtists, "Artists", LV_SYMBOL_DIRECTORY, {}});
        r.push_back({kPlaylists, "Playlists", LV_SYMBOL_LIST, {}});
        return r;
    }

    // A list row: episodes say which, movies their year, and anything
    // half-watched where you stopped.
    static std::string row_text(const JfItem& it, JfView view) {
        std::string t = it.name;
        if (it.kind == JfItem::Episode) {
            if (view == JfView::SeasonEpisodes && it.number >= 0) t = std::to_string(it.number) + ". " + it.name;
            else t = it.detail + " " + it.episode_code() + " " + it.name;
        } else if (it.kind == JfItem::Movie && it.year > 0) {
            t += fmt(" (%d)", it.year);
        } else if ((it.kind == JfItem::Track || it.kind == JfItem::Album) && !it.detail.empty() &&
                   view != JfView::AlbumTracks) {
            t = it.detail + " - " + it.name;
        }
        if (it.playable_video() && it.resume_s > 0) t += "  @" + mmss(it.resume_s * 1000);
        return t;
    }

    lv_obj_t* build_jellyfin(int keep) {
        lv_obj_t* focus = nullptr;
        if (!jf_.signed_in()) {
            note("Music and video from your server");
            add_row(LV_SYMBOL_RIGHT, "Sign in...", "", 0);
            return nullptr;
        }
        if (stack_.empty()) {
            auto rows = root_rows();
            for (size_t i = 0; i < rows.size(); ++i) {
                lv_obj_t* b = add_row(rows[i].icon, rows[i].text, "", int(i));
                if (int(i) == keep) focus = b;
            }
            return focus;
        }
        const Level& lv = stack_.back();
        if (lv.items.empty()) note("Nothing here");
        int track_no = 0;
        for (size_t i = 0; i < lv.items.size(); ++i) {
            const JfItem& it = lv.items[i];
            bool track = it.kind == JfItem::Track;
            bool now = track && jf::stream_url(jf_, it.id) == listed_playing_;
            const char* icon = track ? nullptr
                               : it.playable_video() ? LV_SYMBOL_VIDEO
                               : it.kind == JfItem::Playlist ? LV_SYMBOL_LIST
                               : it.kind == kFolder ? folder_icon(it.detail)
                                                           : LV_SYMBOL_DIRECTORY;
            std::string text = row_text(it, lv.view);
            if (track) text = fmt("%d. ", ++track_no) + text;
            std::string right = (track || it.playable_video()) && it.duration_s > 0 ? mmss(it.duration_s * 1000) : "";
            lv_obj_t* b = add_row(icon, text, right, int(i), now);
            if (now || (!focus && int(i) == keep)) focus = b;
        }
        if (lv.more) add_row(LV_SYMBOL_DOWN, "More...", fmt("%zu shown", lv.items.size()), kMoreRow);
        return focus;
    }

    int focused_index() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f || !list_ || lv_obj_get_parent(f) != list_) return -1;
        return int((intptr_t)lv_obj_get_user_data(f));
    }

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
        q = arrange(std::move(q), at);
        if (!player_play(std::move(q), at)) shell_toast("Can't play " + name);
        rebuild_list();
    }

    // Enter on row i of the current list.
    void activate(int i) {
        if (tab_ == kJellyfinTab) {
            activate_jellyfin(i < 0 && i != kMoreRow ? 0 : i);
            return;
        }
        if (i < 0) {   // nothing focused: the first row that's shown
            if (lv_obj_t* f = lv_obj_get_child(list_, 0)) i = int((intptr_t)lv_obj_get_user_data(f));
        }
        if (i < 0 || i >= int(music_.size())) return;
        // Music continues down the list (what the search shows, if any).
        std::vector<QueueItem> q;
        int at = 0;
        std::string f = lower(filter_);
        for (size_t k = 0; k < music_.size(); ++k) {
            const Track& t = music_[k];
            if (!f.empty() && lower(t.title + " " + t.artist).find(f) == std::string::npos) continue;
            if (int(k) == i) at = int(q.size());
            q.push_back({t.path, t.title, t.artist, t.duration_s, ""});
        }
        if (q.empty()) return;
        play_queue(std::move(q), at, music_[size_t(i)].title);
    }

    void activate_jellyfin(int i) {
        if (!jf_.signed_in()) {
            sign_in();
            return;
        }
        if (stack_.empty()) {
            auto rows = root_rows();
            if (i < 0 || i >= int(rows.size())) return;
            const RootRow& r = rows[size_t(i)];
            switch (r.kind) {
                case kSearch: search(); break;
                case kResume: open_level("Continue watching", JfView::Resume, ""); break;
                case kLibrary: open_level(r.lib.name, kViewLibrary, r.lib.id + ":" + r.lib.detail); break;
                case kMovies: open_level("Movies", JfView::Movies, ""); break;
                case kShows: open_level("TV shows", JfView::Shows, ""); break;
                case kAlbums: open_level("Albums", JfView::Albums, ""); break;
                case kLatest: open_level("Latest albums", JfView::Latest, ""); break;
                case kArtists: open_level("Artists", JfView::Artists, ""); break;
                case kPlaylists: open_level("Playlists", JfView::Playlists, ""); break;
            }
            return;
        }
        if (i == kMoreRow) {
            load_more();
            return;
        }
        // Copies: opening a level reallocates the stack this item lives in.
        const std::vector<JfItem> items = stack_.back().items;
        if (i < 0 || i >= int(items.size())) return;
        const JfItem it = items[size_t(i)];
        switch (it.kind) {
            case JfItem::Album: open_level(it.name, JfView::AlbumTracks, it.id); break;
            case JfItem::Artist: open_level(it.name, JfView::ArtistAlbums, it.id); break;
            case JfItem::Playlist: open_level(it.name, JfView::PlaylistTracks, it.id); break;
            case JfItem::Series: open_level(it.name, JfView::Seasons, it.id); break;
            case kFolder: open_level(it.name, kViewLibrary, it.id + ":" + it.detail); break;
            case JfItem::Season:
                if (it.series_id.empty()) {
                    shell_toast("The server didn't say which show");
                    break;
                }
                open_level(it.detail.empty() ? it.name : it.detail + ", " + it.name, JfView::SeasonEpisodes,
                           it.series_id + ":" + it.id);
                break;
            case JfItem::Movie:
            case JfItem::Episode:
            case JfItem::Video: open_video(items, i); break;
            case JfItem::Track: {
                // Queue every track on this screen, starting with this one.
                std::vector<QueueItem> q;
                int at = 0;
                std::string auth = jf::auth_header(jf_);
                for (size_t k = 0; k < items.size(); ++k) {
                    if (items[k].kind != JfItem::Track) continue;
                    if (int(k) == i) at = int(q.size());
                    q.push_back({jf::stream_url(jf_, items[k].id), items[k].name, items[k].detail,
                                 items[k].duration_s, auth});
                }
                play_queue(std::move(q), at, it.name);
                break;
            }
        }
    }

    static bool paged(JfView v) {
        return v == JfView::Albums || v == JfView::Artists || v == JfView::Playlists || v == JfView::Movies ||
               v == JfView::Shows || v == kViewLibrary;
    }

    // Ask the server for a list and show it as a new level.
    void open_level(const std::string& title, JfView view, const std::string& arg) {
        shell_toast("Loading...");
        lv_refr_now(nullptr);   // the request blocks; show the notice first
        Level lv;
        lv.title = title;
        lv.view = view;
        lv.arg = arg;
        std::string error;
        if (!hal().jellyfin_list(jf_, view, arg, lv.items, error)) {
            shell_toast(error);
            return;
        }
        lv.more = paged(view) && int(lv.items.size()) >= kPage;
        shell_toast(lv.items.empty() ? "Nothing found" : fmt("%zu found", lv.items.size()));
        stack_.push_back(std::move(lv));
        rebuild_list();
        if (lv_obj_get_child_count(list_) > 0) lv_group_focus_obj(lv_obj_get_child(list_, 0));
    }

    // The next page of a long list, added to the end of it.
    void load_more() {
        Level& lv = stack_.back();
        shell_toast("Loading more...");
        lv_refr_now(nullptr);
        std::vector<JfItem> more;
        std::string error;
        if (!hal().jellyfin_list(jf_, lv.view, lv.arg + fmt("@%zu", lv.items.size()), more, error)) {
            shell_toast(error);
            return;
        }
        int first_new = int(lv.items.size());
        lv.more = int(more.size()) >= kPage;
        lv.items.insert(lv.items.end(), more.begin(), more.end());
        shell_toast(fmt("%zu in all", lv.items.size()));
        rebuild_list();
        // Focus the first new row.
        for (uint32_t k = 0; k < lv_obj_get_child_count(list_); ++k) {
            lv_obj_t* ch = lv_obj_get_child(list_, int32_t(k));
            if (int((intptr_t)lv_obj_get_user_data(ch)) == first_new) {
                lv_group_focus_obj(ch);
                break;
            }
        }
    }

    // '/': the Library tab narrows its own list; Jellyfin asks the server.
    void search() {
        if (tab_ == kLibraryTab) {
            ask("Search ~/Music (artist or title)", filter_, false, [this](const std::string& text) {
                filter_ = text;
                rebuild_list();
                if (!filter_.empty() && lv_obj_get_child_count(list_) > 0)
                    lv_group_focus_obj(lv_obj_get_child(list_, 0));
            });
            return;
        }
        if (!jf_.signed_in()) {
            sign_in();
            return;
        }
        ask("Search Jellyfin: music, movies, shows", "", false, [this](const std::string& text) {
            if (!text.empty()) open_level("\"" + text + "\"", JfView::Search, text);
        });
    }

    void sign_out() {
        for (const char* k : {"jellyfin.user_id", "jellyfin.token"}) prefs::set_str(k, "");
        jf_.user_id.clear();
        jf_.token.clear();
        stack_.clear();
        libs_.clear();
        libs_tried_ = false;
        shell_toast("Signed out of Jellyfin");
        set_actions();
        rebuild_list();
    }

    // Three questions, then sign in. Only the token is kept, not the password.
    void sign_in() {
        ask("Jellyfin server (name or address)", jf_.server, false, [this](const std::string& server) {
            if (server.empty()) return;
            typed_server_ = server;
            ask("User name", jf_.user, false, [this](const std::string& user) {
                if (user.empty()) return;
                typed_user_ = user;
                ask("Password", "", true, [this](const std::string& password) {
                    shell_toast("Signing in...");
                    lv_refr_now(nullptr);
                    JfSession s;
                    std::string error;
                    if (!hal().jellyfin_login(typed_server_, typed_user_, password, jf_.device_id, s, error)) {
                        shell_toast(error);
                        return;
                    }
                    jf_ = s;
                    prefs::set_str("jellyfin.server", s.server);
                    prefs::set_str("jellyfin.user", s.user);
                    prefs::set_str("jellyfin.user_id", s.user_id);
                    prefs::set_str("jellyfin.token", s.token);
                    stack_.clear();
                    libs_tried_ = false;
                    shell_toast("Signed in as " + s.user);
                    set_actions();
                    load_libraries();
                    rebuild_list();
                });
            });
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

    static std::string new_session_id() {
        std::random_device rd;
        return fmt("%08x%08x%08x%08x", unsigned(rd()), unsigned(rd()), unsigned(rd()), unsigned(rd()));
    }

    // Play items[i] (a movie or episode) on the screen. The rest of the list
    // stays with it, so an episode can go on to the next one.
    void open_video(const std::vector<JfItem>& items, int i) {
        const JfItem& it = items[size_t(i)];
        close_video(true);
        player_stop();   // music and video share the speaker
        std::string session = new_session_id();
        std::string url = jf::video_url(jf_, it.id, session, kStreamW, kStreamH, kStreamKbps);
        if (url.empty()) {
            shell_toast("Can't play " + it.name);
            return;
        }
        vid_pixels_.assign(size_t(kVidW) * kVidH, 0);
        std::string error;
        int start = it.resume_s > 0 ? it.resume_s : 0;
        if (!hal().video_open(url, jf::auth_header(jf_), start, kVidW, kVidH, error)) {
            shell_toast(error.empty() ? "Can't play " + it.name : error);
            vid_pixels_.clear();
            return;
        }
        vid_items_ = items;
        vid_at_ = i;
        vid_item_ = it;
        vid_session_ = session;
        vid_reported_at_ = lv_tick_get();
        vid_started_ = false;
        vid_auto_paused_ = false;
        build_video_view();
        set_actions();
        show_osd();
        if (start > 0) shell_toast("Resuming at " + mmss(start * 1000) + "   0: from the start");
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

        vid_wait_ = label(vid_, "Loading...", &zs_font_14, kText);
        lv_obj_center(vid_wait_);

        // The info bar along the bottom: title, time, a progress line.
        vid_osd_ = lv_obj_create(vid_);
        style_plain(vid_osd_);
        lv_obj_set_size(vid_osd_, kVidW, 40);
        lv_obj_align(vid_osd_, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(vid_osd_, c(0x000000), 0);
        lv_obj_set_style_bg_opa(vid_osd_, LV_OPA_70, 0);
        lv_obj_remove_flag(vid_osd_, LV_OBJ_FLAG_SCROLLABLE);
        vid_title_ = label(vid_osd_, "", &zs_font_12, kText);
        lv_obj_set_width(vid_title_, kVidW - 12);
        lv_label_set_long_mode(vid_title_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(vid_title_, 6, 2);
        vid_bar_ = thin_bar(vid_osd_, kVidW - 12, 3, kAccent);
        lv_bar_set_range(vid_bar_, 0, 1000);
        lv_obj_set_pos(vid_bar_, 6, 19);
        vid_time_ = label(vid_osd_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(vid_time_, 6, 24);
        vid_hint_ = label(vid_osd_, "Space pause  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " seek  Esc stop", &zs_font_12,
                          kMuted);
        lv_obj_align(vid_hint_, LV_ALIGN_TOP_RIGHT, -6, 24);
        std::string t = vid_item_.kind == JfItem::Episode
                            ? vid_item_.detail + " " + vid_item_.episode_code() + " " + vid_item_.name
                            : vid_item_.name;
        lv_label_set_text(vid_title_, t.c_str());

        if (group) lv_group_focus_obj(nullptr);
        // Pictures come faster than the shell's 10 Hz tick.
        vid_timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<MediaApp*>(lv_timer_get_user_data(t))->video_pump();
        }, 20, this);
    }

    void video_pump() {
        if (!vid_) return;
        if (power().screen_off()) return;   // nobody is looking; the tick pauses it
        if (hal().video_frame(vid_pixels_.data())) {
            lv_obj_invalidate(vid_img_);
            if (!vid_started_) {
                vid_started_ = true;
                hal().jellyfin_report(jf_, JfReport::Start, vid_item_.id, vid_session_, vid_item_.resume_s, false);
            }
        }
    }

    void video_tick() {
        VideoState st = hal().video();
        uint32_t now = lv_tick_get();
        if (!st.open) {
            video_ended(st);
            return;
        }
        if (power().screen_off()) {
            // Face down or timed out: stop watching, keep the place.
            if (!st.paused) {
                hal().video_pause(true);
                vid_auto_paused_ = true;
            }
        } else if (!st.paused) {
            power().stay_awake(now);
        }
        vid_pos_s_ = st.pos_ms / 1000;
        if (now - vid_reported_at_ >= kReportEveryMs) {
            vid_reported_at_ = now;
            hal().jellyfin_report(jf_, JfReport::Progress, vid_item_.id, vid_session_, vid_pos_s_, st.paused);
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
                                          fmt("   " LV_SYMBOL_VOLUME_MID " %d%%", hal().volume()))
                                             .c_str());
        }
    }

    void video_ended(const VideoState& st) {
        bool finished = st.finished;
        std::string error = st.error;
        // Finished: the server should forget the resume point.
        if (finished) vid_pos_s_ = vid_item_.duration_s > 0 ? vid_item_.duration_s : vid_pos_s_;
        close_video(true);
        if (!error.empty()) {
            shell_toast(error);
            return;
        }
        if (!finished) return;
        // An episode: on to the next one in the list.
        int next = vid_at_ + 1;
        // (Only within the season: a list like Continue watching mixes shows.)
        if (vid_item_.kind == JfItem::Episode && next < int(vid_items_.size()) &&
            vid_items_[size_t(next)].kind == JfItem::Episode &&
            vid_items_[size_t(next)].series_id == vid_item_.series_id &&
            vid_items_[size_t(next)].season == vid_item_.season &&
            vid_items_[size_t(next)].number > vid_item_.number) {
            std::vector<JfItem> items = vid_items_;
            items[size_t(next)].resume_s = 0;
            shell_toast("Next: " + items[size_t(next)].name);
            open_video(items, next);
        } else {
            shell_toast("Finished " + vid_item_.name);
        }
    }

    void show_osd() { vid_osd_at_ = lv_tick_get(); }

    void video_toggle_pause() {
        VideoState st = hal().video();
        hal().video_pause(!st.paused);
        vid_auto_paused_ = false;
        hal().jellyfin_report(jf_, JfReport::Progress, vid_item_.id, vid_session_, st.pos_ms / 1000, !st.paused);
    }

    bool video_key(uint32_t key) {
        auto& h = hal();
        show_osd();
        switch (key) {
            case ' ':
            case LV_KEY_ENTER:
            case kKeyPlayPause: video_toggle_pause(); return true;
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
            case 's': case 'S': close_video(true); return true;
            case 'm': case 'M': video_to_monitor(); return true;
            case LV_KEY_ESC: return false;   // on_back() closes it
        }
        // The shell's own keys (volume, brightness) still work; the list
        // underneath gets nothing.
        return !is_shell_key(key);
    }

    // Dock mode: hand the video to the monitor's player, from where we are.
    void video_to_monitor() {
        if (!hal().display().dock_running) {
            shell_toast("Start Dock mode for the monitor first");
            return;
        }
        JfItem it = vid_item_;
        int at = hal().video().pos_ms / 1000;
        std::string url = jf::video_url(jf_, it.id, new_session_id(), kDockW, kDockH, kDockKbps);
        close_video(true);
        if (!jf::write_dock_options(jf_, at) || !hal().dock_open("video", url)) {
            shell_toast("Can't open the video on the monitor");
            return;
        }
        shell_toast("Playing on the monitor");
    }

    // Stop the video, tell the server where it stopped and, with ui, take
    // the picture off the screen.
    void close_video(bool ui) {
        if (vid_timer_) {
            lv_timer_delete(vid_timer_);
            vid_timer_ = nullptr;
        }
        if (!vid_) return;
        VideoState st = hal().video();
        if (st.open) vid_pos_s_ = st.pos_ms / 1000;
        hal().video_close();
        hal().jellyfin_report(jf_, JfReport::Stop, vid_item_.id, vid_session_, vid_pos_s_, false);
        if (ui) {
            lv_obj_delete(vid_);
            vid_ = nullptr;
            // The list may have a stale resume point; show the place we stopped.
            for (auto& lvl : stack_)
                for (auto& x : lvl.items)
                    if (x.id == vid_item_.id) x.resume_s = st.finished ? 0 : vid_pos_s_;
            set_actions();
            rebuild_list();
        }
        vid_ = nullptr;
        vid_pixels_.clear();
        vid_pixels_.shrink_to_fit();
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* tabs_[kTabs] = {};
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

    std::vector<Track> music_;
    std::string filter_;               // the Library tab's search
    std::vector<std::string> queue_paths_;   // what we queued, for "3/12"
    bool shuffle_ = false;
    std::string listed_playing_;       // what the list last marked as playing
    int tab_ = 0;
    int pending_ = -1;

    JfSession jf_;
    std::vector<Level> stack_;
    std::vector<JfItem> libs_;         // the user's libraries
    bool libs_tried_ = false;
    lv_obj_t* input_ = nullptr;
    Done done_;
    std::string typed_server_, typed_user_;

    // Video
    lv_obj_t* vid_ = nullptr;   // the player view, over everything; null when no video
    lv_obj_t *vid_img_ = nullptr, *vid_wait_ = nullptr, *vid_osd_ = nullptr, *vid_title_ = nullptr,
             *vid_bar_ = nullptr, *vid_time_ = nullptr, *vid_hint_ = nullptr;
    lv_timer_t* vid_timer_ = nullptr;
    lv_image_dsc_t vid_dsc_{};
    std::vector<uint16_t> vid_pixels_;
    std::vector<JfItem> vid_items_;   // the list it was started from
    int vid_at_ = -1;
    JfItem vid_item_;
    std::string vid_session_;
    uint32_t vid_reported_at_ = 0, vid_osd_at_ = 0;
    int vid_pos_s_ = 0;
    bool vid_started_ = false, vid_auto_paused_ = false;
};
}  // namespace

std::unique_ptr<App> make_media_app() { return std::make_unique<MediaApp>(); }

}  // namespace zs
