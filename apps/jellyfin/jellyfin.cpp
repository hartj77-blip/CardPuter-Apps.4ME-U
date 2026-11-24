// Jellyfin: music and video from your Jellyfin server, dressed like
// Jellyfin's own apps: the near-black dark theme, the purple-to-blue logo,
// a home screen of card rows (My Media, Continue Watching, Music), lists,
// and a now-playing bar along the bottom.
//
// The kinds of screen (docs/UX.md in the OS), each with its keys in the
// soft-key bar and all its actions in the Tab menu:
//   Home     Control: arrows move between cards, Enter opens, F search,
//            Space pause, N / P next / previous, + / - volume
//   Lists    List (ui/list_view.h): typing narrows the list, 1 to 9 pick,
//            Enter plays or opens, Space pauses, Backspace or Esc go back
//   Video    Control: Enter or Space pause, Left / Right -/+10 s, Up / Down
//            +/-1 min, R from the start, M on the monitor, S or Esc stop
// Fn + Q / W / E, the keyboard's media keys, work everywhere.
//
// Music goes through the shell's queue (ui/player.h), so it carries on to
// the next track after you leave the app or the screen turns off. Video
// belongs to the app: it stops when you leave Jellyfin.
//
// The sign-in (jellyfin.* in prefs) is the one the Media app used to keep,
// so a device that was signed in there stays signed in.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <random>

#include "apps/common.h"
#include "platform/keys.h"
#include "ui/list_view.h"
#include "ui/player.h"
#include "ui/power.h"
#include "ui/prefs.h"

namespace zs {
namespace {
using namespace theme;

// Jellyfin's dark theme and logo colours.
constexpr uint32_t kJfBg = 0x101010;       // page
constexpr uint32_t kJfHeader = 0x101010;   // the header is the page colour
constexpr uint32_t kJfCard = 0x242424;     // cards with no picture
constexpr uint32_t kJfRaised = 0x303030;   // dialogs, the now-playing bar's trough
constexpr uint32_t kJfBar = 0x202020;      // the now-playing bar
constexpr uint32_t kJfText = 0xffffff;
constexpr uint32_t kJfDim = 0x9a9a9a;      // secondary text
constexpr uint32_t kJfBlue = 0x00a4dc;     // the accent
constexpr uint32_t kJfPurple = 0xaa5cc3;   // the logo's other end

constexpr int kH = kBodyH;            // the soft-key bar takes the bottom
constexpr int kHeadH = 20;
constexpr int kNowH = 18;             // the now-playing bar

// The video picture: the app area above the soft-key bar. 16:9 fills it
// at 242 x 136.
constexpr int kVidW = kScreenW;
constexpr int kVidH = kBodyH;
// What we ask the server for: a picture about the size of the screen (it
// scales down, never up) at a bitrate Wi-Fi on a Pi Zero 2 keeps up with.
constexpr int kStreamW = 427, kStreamH = 240, kStreamKbps = 700;
// And for the monitor in Dock mode: 720p, decoded in hardware there.
constexpr int kDockW = 1280, kDockH = 720, kDockKbps = 4000;
constexpr uint32_t kReportEveryMs = 30000;

// Library lists came with a later ZeroOS than some this app is built for
// (releases build against the newest released ZeroOS too). The values were
// added at the end of their enums, so they are spelled by number here; an
// older ZeroOS answers "Nothing to open" and the fixed cards stay.
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

// "1h 12m" / "38m": how Jellyfin says how long is left.
std::string left_text(int s) {
    if (s <= 0) return "";
    int m = (s + 59) / 60;
    return m >= 60 ? fmt("%dh %dm left", m / 60, m % 60) : fmt("%dm left", m);
}

lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg, int radius) {
    lv_obj_t* o = lv_obj_create(parent);
    style_plain(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// The logo's colours, left to right.
void logo_gradient(lv_obj_t* o) {
    lv_obj_set_style_bg_color(o, c(kJfPurple), 0);
    lv_obj_set_style_bg_grad_color(o, c(kJfBlue), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, 0);
}

class JellyfinApp : public App {
public:
    ~JellyfinApp() override {
        // Leaving the app: the screen is being torn down, so only stop the
        // video and tell the server where we got to.
        close_video(false);
    }

    void build(lv_obj_t* root) override {
        root_ = root;
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

        lv_obj_t* bg = panel(root, 0, 0, kScreenW, kH, kJfBg, 0);
        (void)bg;
        build_header();
        build_now_bar();
        set_actions();
        load_home();
        show();
    }

    void tick() override {
        if (vid_) {
            video_tick();
            return;
        }
        update_now_bar();
    }

    bool on_key(uint32_t key) override {
        if (vid_) return video_key(key);
        if (input_) return false;   // typing in the text box
        if (!jf_.signed_in()) {
            if (key == LV_KEY_ENTER || key == '\r') {
                sign_in();
                return true;
            }
            return false;
        }
        // The keyboard's media keys, on every screen.
        switch (key) {
            case kKeyPlayPause: pause(); return true;
            case kKeyNextTrack: player_step(+1); return true;
            case kKeyPrevTrack: player_step(-1); return true;
        }
        return stack_.empty() ? home_key(key) : list_key(key);
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
        if (!stack_.empty()) {
            back();
            return true;
        }
        return false;
    }

private:
    // A list: a List screen, so letters narrow it and its other actions are
    // in the menu. Space pauses while nothing is typed.
    bool list_key(uint32_t key) {
        if (list_built_ && list_.key(key)) return true;
        switch (key) {
            case LV_KEY_BACKSPACE:
            case LV_KEY_DEL:
                back();   // nothing typed: back a step, as Esc
                return true;
            case ' ': pause(); return true;
        }
        return false;
    }

    // Space: pause or resume the music; with none, play the row you're on.
    void pause() {
        PlaybackState pb = hal().playback();
        if (!pb.path.empty()) hal().set_paused(!pb.paused);
        else if (!stack_.empty()) activate(focused_index());
    }

    static constexpr int kMoreRow = -2;   // "More..." at the end of a long list

    // One screen of results; the stack of them is where you are. Empty: home.
    struct Level {
        std::string title;
        std::vector<JfItem> items;
        JfView view = JfView::Latest;
        std::string arg;          // what was asked, without the page
        bool more = false;        // came back a whole page long
    };

    // A card on the home screen.
    enum CardKind { kLibrary, kResumeItem, kMovies, kShows, kAlbums, kLatest, kArtists, kPlaylists, kSearch };
    struct Card {
        CardKind kind;
        std::string title;
        std::string sub;
        const char* icon;
        JfItem item;          // kLibrary, kResumeItem
        int progress = -1;    // permille watched, -1 for none
    };
    struct Section {
        std::string title;
        std::vector<Card> cards;
        int card_w = 74;
        std::vector<lv_obj_t*> objs;
        lv_obj_t* title_obj = nullptr;
    };

    // ---- The frame ----------------------------------------------------------

    void build_header() {
        header_ = panel(root_, 0, 0, kScreenW, kHeadH, kJfHeader, 0);
        // The logo: the gradient mark and the lower-case name.
        logo_ = panel(header_, 6, 4, 12, 12, kJfPurple, 3);
        logo_gradient(logo_);
        lv_obj_t* inner = panel(logo_, 4, 5, 4, 4, kJfHeader, 2);
        (void)inner;
        word_ = label(header_, "jellyfin", &zs_font_16, kJfText);
        lv_obj_set_pos(word_, 22, 1);
        // In a list: a back arrow and the list's name instead.
        back_ = label(header_, LV_SYMBOL_LEFT, &zs_font_14, kJfText);
        lv_obj_set_pos(back_, 6, 3);
        title_ = label(header_, "", &zs_font_14, kJfText);
        lv_obj_set_pos(title_, 22, 2);
        lv_obj_set_width(title_, 210);
        lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
        // Right: search, and the user's round avatar with an initial.
        avatar_ = panel(header_, kScreenW - 20, 3, 14, 14, kJfBlue, LV_RADIUS_CIRCLE);
        initial_ = label(avatar_, "", &zs_font_12, kJfText);
        lv_obj_center(initial_);
        lv_obj_t* s = label(header_, ZS_ICON_SEARCH, &zs_font_12, kJfDim);
        lv_obj_set_pos(s, kScreenW - 38, 4);
    }

    void build_now_bar() {
        now_ = panel(root_, 0, kH - kNowH, kScreenW, kNowH, kJfBar, 0);
        now_bar_ = thin_bar(now_, kScreenW, 2, kJfBlue);
        lv_obj_set_style_radius(now_bar_, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(now_bar_, 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(now_bar_, c(kJfRaised), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(now_bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_bar_set_range(now_bar_, 0, 1000);
        lv_obj_set_pos(now_bar_, 0, 0);
        now_state_ = label(now_, LV_SYMBOL_PLAY, &zs_font_12, kJfBlue);
        lv_obj_set_pos(now_state_, 6, 3);
        now_title_ = label(now_, "", &zs_font_12, kJfText);
        lv_obj_set_pos(now_title_, 22, 3);
        lv_obj_set_width(now_title_, 220);
        lv_label_set_long_mode(now_title_, LV_LABEL_LONG_DOT);
        now_time_ = label(now_, "", &zs_font_12, kJfDim);
        lv_obj_align(now_time_, LV_ALIGN_TOP_RIGHT, -6, 3);
        lv_obj_add_flag(now_, LV_OBJ_FLAG_HIDDEN);
    }

    // The bar shows while music plays; the page above it gets shorter.
    void update_now_bar() {
        PlaybackState pb = hal().playback();
        bool on = !pb.path.empty();
        if (on != now_shown_) {
            now_shown_ = on;
            if (on) lv_obj_remove_flag(now_, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(now_, LV_OBJ_FLAG_HIDDEN);
            if (page_) lv_obj_set_height(page_, page_h());
            if (page_ && stack_.empty() && focused_obj_) focus_card();
            // A list's rows are laid out for its height: draw it again.
            if (page_ && !stack_.empty() && !input_) {
                keep_row_ = focused_index();
                show();
            }
        }
        if (on) {
            const QueueItem* q = player_current();
            std::string t = q ? (q->artist.empty() ? q->title : q->title + " - " + q->artist) : pb.path;
            if (t != now_text_) {
                now_text_ = t;
                lv_label_set_text(now_title_, t.c_str());
            }
            lv_label_set_text(now_state_, pb.paused ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
            lv_label_set_text(now_time_, (mmss(pb.pos_ms) + " / " + mmss(pb.dur_ms)).c_str());
            lv_obj_align(now_time_, LV_ALIGN_TOP_RIGHT, -6, 3);
            lv_bar_set_value(now_bar_, pb.dur_ms > 0 ? int(1000LL * pb.pos_ms / pb.dur_ms) : 0, LV_ANIM_OFF);
        }
        // The playing track's row follows the queue (not under a text box).
        if (pb.path != listed_playing_ && !input_ && !stack_.empty()) {
            keep_row_ = focused_index();
            show();
        }
    }

    int page_h() const { return kH - kHeadH - (now_shown_ ? kNowH : 0); }

    void update_header() {
        bool home = stack_.empty();
        auto vis = [](lv_obj_t* o, bool v) {
            if (v) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        };
        vis(logo_, home);
        vis(word_, home);
        vis(back_, !home);
        vis(title_, !home);
        if (!home) lv_label_set_text(title_, stack_.back().title.c_str());
        vis(avatar_, jf_.signed_in());
        std::string ini = jf_.user.empty() ? "?" : std::string(1, char(std::toupper((unsigned char)jf_.user[0])));
        lv_label_set_text(initial_, ini.c_str());
        lv_obj_center(initial_);
    }

    // The actions of the screen on show (docs/UX.md): Home is a Control
    // screen, so its letters work as they are and the bar shows them; a list
    // is a List screen, so the same actions are in the menu only.
    void set_actions() {
        std::vector<Action> a;
        if (vid_) {
            a.push_back({"Pause", 0, "Enter", nullptr});
            a.push_back({"Seek 10 s", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
            a.push_back({"Stop", 's', "S", [this] { close_video(true); }});
            a.push_back({"On the monitor", 'm', "M", [this] { video_to_monitor(); }});
            a.push_back({"Pause", 0, "Space", nullptr});
            a.push_back({"Seek 1 min", 0, LV_SYMBOL_UP LV_SYMBOL_DOWN, nullptr});
            a.push_back({"From the start", 'r', "R", [this] { video_restart(); }});
            a.push_back({"Volume", 0, "+ -", nullptr});
            shell_set_actions(std::move(a));
            return;
        }
        if (!jf_.signed_in()) {
            a.push_back({"Sign in", 'i', "Enter", [this] { sign_in(); }});
            shell_set_actions(std::move(a));
            return;
        }
        bool home = stack_.empty();
        a.push_back({"Open", 0, "Enter", nullptr});
        if (home) {
            a.push_back({"Search", 'f', "F", [this] { search(); }});
            a.push_back({"Pause", 0, "Space", nullptr});
            a.push_back({"Next", 'n', "N", [this] { player_step(+1); }});
            a.push_back({"Previous", 'p', "P", [this] { player_step(-1); }});
            a.push_back({"Volume", 0, "+ -", nullptr});
        } else {
            a.push_back({"Back", 0, "Esc", nullptr});
            a.push_back({"Pause", 'u', "Space", [this] { pause(); }});
            a.push_back({"Search", 'f', nullptr, [this] { search(); }});
            a.push_back({"Next track", 'n', nullptr, [this] { player_step(+1); }});
            a.push_back({"Previous track", 'p', nullptr, [this] { player_step(-1); }});
            a.push_back({"Home", 'h', nullptr, [this] {
                             stack_.clear();
                             back_rows_.clear();
                             show();
                         }});
        }
        a.push_back({"Stop music", 't', nullptr, [this] { player_stop(); }});
        a.push_back({"Refresh", 'r', nullptr, [this] {
                         stack_.clear();
                         back_rows_.clear();
                         home_loaded_ = false;
                         load_home();
                         show();
                     }});
        a.push_back({"Sign out", 'o', nullptr, [this] { sign_out(); }, true, true, "Sign out of Jellyfin?"});
        shell_set_actions(std::move(a));
    }

    // Draw the page: home, a list, or the sign-in screen.
    void show() {
        if (page_) {
            if (group) lv_group_remove_all_objs(group);
            lv_obj_delete(page_);
            page_ = nullptr;
            focused_obj_ = nullptr;
        }
        list_.reset();   // its widgets went with the page
        list_built_ = false;
        if (!input_) set_actions();   // a text box keeps its own (OK, Cancel)
        update_header();
        listed_playing_ = hal().playback().path;
        if (!jf_.signed_in()) build_welcome();
        else if (stack_.empty()) build_home();
        else build_list();
        if (input_) lv_obj_move_foreground(input_);
        lv_obj_move_foreground(now_);
    }

    lv_obj_t* new_page(bool scroll) {
        page_ = lv_obj_create(root_);
        style_plain(page_);
        lv_obj_set_pos(page_, 0, kHeadH);
        lv_obj_set_size(page_, kScreenW, page_h());
        lv_obj_set_style_bg_color(page_, c(kJfBg), 0);
        lv_obj_set_style_bg_opa(page_, LV_OPA_COVER, 0);
        lv_obj_set_scrollbar_mode(page_, LV_SCROLLBAR_MODE_OFF);
        if (!scroll) lv_obj_remove_flag(page_, LV_OBJ_FLAG_SCROLLABLE);
        return page_;
    }

    // ---- Signed out ---------------------------------------------------------

    void build_welcome() {
        new_page(false);
        lv_obj_t* mark = panel(page_, kScreenW / 2 - 14, 10, 28, 28, kJfPurple, 7);
        logo_gradient(mark);
        panel(mark, 10, 11, 8, 8, kJfBg, 3);
        lv_obj_t* w = label(page_, "jellyfin", &zs_font_20, kJfText);
        lv_obj_align(w, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_t* t = label(page_, "Your music, movies and shows, from your own server", &zs_font_12, kJfDim);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 66);
        lv_obj_t* btn = panel(page_, kScreenW / 2 - 50, 84, 100, 20, kJfBlue, 4);
        lv_obj_center(label(btn, "Sign in", &zs_font_14, kJfText));
    }

    // ---- Home ---------------------------------------------------------------

    static const char* folder_icon(const std::string& type) {
        if (type == "music") return LV_SYMBOL_AUDIO;
        if (type == "movies" || type == "tvshows" || type == "homevideos" || type == "musicvideos")
            return LV_SYMBOL_VIDEO;
        if (type == "playlists") return LV_SYMBOL_LIST;
        if (type == "books") return LV_SYMBOL_FILE;
        return LV_SYMBOL_DIRECTORY;
    }

    // The user's libraries and Continue watching, asked for once (Refresh
    // asks again). A ZeroOS too old to know libraries, or a server that
    // won't say, leaves fixed Movies / TV shows / Albums cards in their place.
    void load_home() {
        if (home_loaded_ || !jf_.signed_in()) return;
        home_loaded_ = true;
        libs_.clear();
        resume_.clear();
        shell_toast("Loading your media...");
        lv_refr_now(nullptr);
        std::string error;
        if (!hal().jellyfin_list(jf_, kViewLibraries, "", libs_, error)) {
            libs_.clear();
            // "Nothing to open": this ZeroOS predates library lists.
            if (error != "Nothing to open") {
                shell_toast(error);
                return;   // the server is away: don't wait for it twice
            }
        }
        if (!hal().jellyfin_list(jf_, JfView::Resume, "", resume_, error)) resume_.clear();
    }

    std::vector<Section> sections() const {
        std::vector<Section> v;
        Section media{"My Media", {}, 74, {}};
        if (libs_.empty()) {
            media.cards.push_back({kMovies, "Movies", "", LV_SYMBOL_VIDEO, {}});
            media.cards.push_back({kShows, "Shows", "", LV_SYMBOL_VIDEO, {}});
            media.cards.push_back({kAlbums, "Music", "", LV_SYMBOL_AUDIO, {}});
        }
        for (const JfItem& lib : libs_) media.cards.push_back({kLibrary, lib.name, "", folder_icon(lib.detail), lib});
        v.push_back(media);

        if (!resume_.empty()) {
            Section cont{"Continue Watching", {}, 120, {}};
            for (const JfItem& it : resume_) {
                Card cd{kResumeItem, it.name, "", LV_SYMBOL_VIDEO, it};
                if (it.kind == JfItem::Episode) {
                    cd.title = it.detail;
                    cd.sub = it.episode_code() + " " + it.name;
                } else {
                    cd.sub = left_text(it.duration_s - it.resume_s);
                    if (it.year > 0) cd.sub = fmt("%d  ", it.year) + cd.sub;
                }
                if (it.duration_s > 0) cd.progress = std::clamp(1000 * it.resume_s / it.duration_s, 0, 1000);
                cont.cards.push_back(cd);
            }
            v.push_back(cont);
        }

        Section music{"Music", {}, 74, {}};
        music.cards.push_back({kLatest, "Latest", "", LV_SYMBOL_BELL, {}});
        music.cards.push_back({kAlbums, "Albums", "", LV_SYMBOL_AUDIO, {}});
        music.cards.push_back({kArtists, "Artists", "", LV_SYMBOL_DIRECTORY, {}});
        music.cards.push_back({kPlaylists, "Playlists", "", LV_SYMBOL_LIST, {}});
        v.push_back(music);
        return v;
    }

    void build_home() {
        new_page(true);
        home_ = sections();
        constexpr int kTitleH = 16, kCardH = 42, kGap = 4;
        int y = 0;
        for (size_t s = 0; s < home_.size(); ++s) {
            Section& sec = home_[s];
            lv_obj_t* t = label(page_, sec.title.c_str(), &zs_font_14, kJfText);
            lv_obj_set_pos(t, 6, y);
            sec.title_obj = t;
            y += kTitleH;
            // A row of cards that scrolls sideways.
            lv_obj_t* row = lv_obj_create(page_);
            style_plain(row);
            lv_obj_set_pos(row, 0, y);
            lv_obj_set_size(row, kScreenW, kCardH + 4);
            lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_scroll_dir(row, LV_DIR_HOR);
            sec.objs.clear();
            for (size_t i = 0; i < sec.cards.size(); ++i)
                sec.objs.push_back(build_card(row, sec.cards[i], 6 + int(i) * (sec.card_w + kGap), 2, sec.card_w,
                                              kCardH));
            // Room after the last card, so it can scroll fully into view.
            lv_obj_t* end = panel(row, 6 + int(sec.cards.size()) * (sec.card_w + kGap), 2, 2, 2, kJfBg, 0);
            (void)end;
            y += kCardH + 4 + kGap;
        }
        // Room under the last row, so it too can scroll up to the top.
        int room = page_h() - (kTitleH + kCardH + 4 + kGap);
        if (room > 0) panel(page_, 0, y, 1, room, kJfBg, 0);
        sel_row_ = std::clamp(sel_row_, 0, int(home_.size()) - 1);
        sel_col_ = std::clamp(sel_col_, 0, std::max(0, int(home_[size_t(sel_row_)].cards.size()) - 1));
        focus_card();
    }

    lv_obj_t* build_card(lv_obj_t* row, const Card& cd, int x, int y, int w, int h) {
        lv_obj_t* o = panel(row, x, y, w, h, kJfCard, 4);
        lv_obj_set_style_border_color(o, c(kJfBlue), 0);
        lv_obj_set_style_border_width(o, 0, 0);
        bool wide = cd.kind == kResumeItem;
        if (wide) {
            // Continue watching: the name, what's left, and how far you got.
            lv_obj_t* ic = label(o, cd.icon, &zs_font_12, kJfDim);
            lv_obj_set_pos(ic, 5, 3);
            lv_obj_t* t = label(o, cd.title.c_str(), &zs_font_12, kJfText);
            lv_obj_set_pos(t, 20, 3);
            lv_obj_set_width(t, w - 24);
            lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
            lv_obj_t* s = label(o, cd.sub.c_str(), &zs_font_12, kJfDim);
            lv_obj_set_pos(s, 5, 18);
            lv_obj_set_width(s, w - 10);
            lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
            if (cd.progress >= 0) {
                lv_obj_t* b = thin_bar(o, w, 3, kJfBlue);
                lv_obj_set_style_radius(b, 0, LV_PART_MAIN);
                lv_obj_set_style_radius(b, 0, LV_PART_INDICATOR);
                lv_obj_set_style_bg_color(b, c(0x000000), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(b, LV_OPA_50, LV_PART_MAIN);
                lv_bar_set_range(b, 0, 1000);
                lv_bar_set_value(b, std::max(20, cd.progress), LV_ANIM_OFF);
                lv_obj_set_pos(b, 0, h - 3);
            }
        } else {
            // A library: its icon over its name, as a card with no picture.
            lv_obj_t* ic = label(o, cd.icon, &zs_font_16, kJfDim);
            lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 4);
            lv_obj_t* t = label(o, cd.title.c_str(), &zs_font_12, kJfText);
            lv_obj_set_width(t, w - 6);
            lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
            lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -3);
        }
        return o;
    }

    // The selected card: a blue frame, as Jellyfin marks focus on a TV.
    void focus_card() {
        if (focused_obj_) {
            lv_obj_set_style_border_width(focused_obj_, 0, 0);
            lv_obj_set_style_bg_color(focused_obj_, c(kJfCard), 0);
        }
        focused_obj_ = nullptr;
        if (home_.empty()) return;
        Section& sec = home_[size_t(sel_row_)];
        if (sec.objs.empty()) return;
        lv_obj_t* o = sec.objs[size_t(sel_col_)];
        lv_obj_set_style_border_width(o, 2, 0);
        lv_obj_set_style_bg_color(o, c(0x2c3a42), 0);
        focused_obj_ = o;
        lv_obj_scroll_to_view(o, LV_ANIM_OFF);   // along its row
        // The page scrolls so the section starts at the top, title and all.
        if (sec.title_obj) lv_obj_scroll_to_y(page_, std::max(0, int(lv_obj_get_y(sec.title_obj)) - 2), LV_ANIM_OFF);
    }

    bool home_key(uint32_t key) {
        switch (key) {
            case LV_KEY_LEFT:
                if (home_.empty()) return false;
                if (sel_col_ > 0) --sel_col_;
                focus_card();
                return true;
            case LV_KEY_RIGHT:
                if (home_.empty()) return false;
                if (sel_col_ + 1 < int(home_[size_t(sel_row_)].cards.size())) ++sel_col_;
                focus_card();
                return true;
            case LV_KEY_UP:
                if (home_.empty()) return false;
                if (sel_row_ > 0) {
                    --sel_row_;
                    sel_col_ = std::min(sel_col_, int(home_[size_t(sel_row_)].cards.size()) - 1);
                }
                focus_card();
                return true;
            case LV_KEY_DOWN:
                if (home_.empty()) return false;
                if (sel_row_ + 1 < int(home_.size())) {
                    ++sel_row_;
                    sel_col_ = std::min(sel_col_, int(home_[size_t(sel_row_)].cards.size()) - 1);
                }
                focus_card();
                return true;
            case LV_KEY_ENTER:
            case '\r':
                if (home_.empty()) return false;
                open_card(home_[size_t(sel_row_)].cards[size_t(sel_col_)]);
                return true;
        }
        // Home is a Control screen: these letters are in its action list.
        auto& h = hal();
        switch (key) {
            case ' ': pause(); return true;
            case 'n': case 'N': player_step(+1); return true;
            case 'p': case 'P': player_step(-1); return true;
            case 'f': case 'F': search(); return true;
            case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
        }
        return false;
    }

    void open_card(const Card& cd) {
        switch (cd.kind) {
            case kLibrary: open_level(cd.item.name, kViewLibrary, cd.item.id + ":" + cd.item.detail); break;
            case kResumeItem: {
                std::vector<JfItem> one = {cd.item};
                open_video(one, 0);
                break;
            }
            case kMovies: open_level("Movies", JfView::Movies, ""); break;
            case kShows: open_level("Shows", JfView::Shows, ""); break;
            case kAlbums: open_level("Albums", JfView::Albums, ""); break;
            case kLatest: open_level("Latest music", JfView::Latest, ""); break;
            case kArtists: open_level("Artists", JfView::Artists, ""); break;
            case kPlaylists: open_level("Playlists", JfView::Playlists, ""); break;
            case kSearch: search(); break;
        }
    }

    // ---- Lists --------------------------------------------------------------

    // A list row: episodes say which, movies their year, and anything
    // half-watched how much is left.
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
        return t;
    }

    void build_list() {
        int keep = keep_row_;
        keep_row_ = -1;
        new_page(false);
        list_.build(page_, 4, 2, kScreenW - 8, page_h() - 4);
        list_built_ = true;
        list_.set_empty_text("Nothing here");
        const Level& lv = stack_.back();
        int now_row = -1;
        for (size_t i = 0; i < lv.items.size(); ++i) {
            const JfItem& it = lv.items[i];
            bool track = it.kind == JfItem::Track;
            bool now = track && jf::stream_url(jf_, it.id) == listed_playing_;
            const char* icon = track ? (now ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_AUDIO)
                               : it.playable_video() ? LV_SYMBOL_PLAY
                               : it.kind == JfItem::Playlist ? LV_SYMBOL_LIST
                               : it.kind == JfItem::Album ? LV_SYMBOL_AUDIO
                               : it.kind == kFolder ? folder_icon(it.detail)
                                                    : LV_SYMBOL_DIRECTORY;
            std::string right;
            if (it.playable_video() && it.resume_s > 0) right = left_text(it.duration_s - it.resume_s);
            else if ((track || it.playable_video()) && it.duration_s > 0) right = mmss(it.duration_s * 1000);
            int i_ = int(i);
            list_.add(icon, row_text(it, lv.view), right, [this, i_] { activate(i_); });
            if (now) now_row = i_;
        }
        if (lv.more) list_.add(LV_SYMBOL_DOWN, "More...", fmt("%zu shown", lv.items.size()), [this] { load_more(); });
        list_.finish();
        if (keep >= 0) list_.focus(keep);
        else if (now_row >= 0) list_.focus(now_row);
    }

    int focused_index() const {
        if (!list_built_ || stack_.empty()) return -1;
        int i = list_.focused();
        return i < int(stack_.back().items.size()) ? i : kMoreRow;
    }

    void back() {
        stack_.pop_back();
        keep_row_ = back_rows_.empty() ? -1 : back_rows_.back();
        if (!back_rows_.empty()) back_rows_.pop_back();
        show();
    }

    // Enter on row i of the list.
    void activate(int i) {
        if (!jf_.signed_in()) {
            sign_in();
            return;
        }
        if (stack_.empty()) return;
        if (i == kMoreRow) {
            load_more();
            return;
        }
        // Copies: opening a level reallocates the stack this item lives in.
        const std::vector<JfItem> items = stack_.back().items;
        if (i < 0 || i >= int(items.size())) return;
        const JfItem it = items[size_t(i)];
        switch (it.kind) {
            case JfItem::Album: open_level(it.name, JfView::AlbumTracks, it.id, i); break;
            case JfItem::Artist: open_level(it.name, JfView::ArtistAlbums, it.id, i); break;
            case JfItem::Playlist: open_level(it.name, JfView::PlaylistTracks, it.id, i); break;
            case JfItem::Series: open_level(it.name, JfView::Seasons, it.id, i); break;
            case kFolder: open_level(it.name, kViewLibrary, it.id + ":" + it.detail, i); break;
            case JfItem::Season:
                if (it.series_id.empty()) {
                    shell_toast("The server didn't say which show");
                    break;
                }
                open_level(it.detail.empty() ? it.name : it.detail + ", " + it.name, JfView::SeasonEpisodes,
                           it.series_id + ":" + it.id, i);
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
                if (!player_play(std::move(q), at)) shell_toast("Can't play " + it.name);
                keep_row_ = i;
                show();
                break;
            }
        }
    }

    static bool paged(JfView v) {
        return v == JfView::Albums || v == JfView::Artists || v == JfView::Playlists || v == JfView::Movies ||
               v == JfView::Shows || v == kViewLibrary;
    }

    // Ask the server for a list and show it as a new level. from: the row
    // it was opened from, selected again on the way back.
    void open_level(const std::string& title, JfView view, const std::string& arg, int from = -1) {
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
        shell_toast(lv.items.empty() ? "Nothing found" : fmt("%zu items", lv.items.size()));
        back_rows_.push_back(from);
        stack_.push_back(std::move(lv));
        keep_row_ = -1;
        show();
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
        keep_row_ = first_new;
        show();
    }

    void search() {
        if (!jf_.signed_in()) {
            sign_in();
            return;
        }
        ask("Search", "", false, [this](const std::string& text) {
            if (!text.empty()) open_level("\"" + text + "\"", JfView::Search, text);
        });
    }

    void sign_out() {
        for (const char* k : {"jellyfin.user_id", "jellyfin.token"}) prefs::set_str(k, "");
        jf_.user_id.clear();
        jf_.token.clear();
        stack_.clear();
        back_rows_.clear();
        libs_.clear();
        resume_.clear();
        home_loaded_ = false;
        shell_toast("Signed out of Jellyfin");
        set_actions();
        show();
    }

    // Jellyfin's own three questions, then sign in. Only the token is kept,
    // not the password.
    void sign_in() {
        ask("Server address (name or address)", jf_.server, false, [this](const std::string& server) {
            if (server.empty()) return;
            typed_server_ = server;
            ask("User", jf_.user, false, [this](const std::string& user) {
                if (user.empty()) return;
                typed_user_ = user;
                // An address typed with http:// sends the password as it is.
                bool plain = typed_server_.find("http://") != std::string::npos;
                ask(plain ? "Password (sent unencrypted: http)" : "Password", "", true,
                    [this](const std::string& password) {
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
                    back_rows_.clear();
                    home_loaded_ = false;
                    // No https: say so, so it isn't used on a network nobody trusts.
                    shell_toast("Signed in as " + s.user +
                                (s.server.rfind("https://", 0) == 0 ? std::string() : std::string(" - not encrypted (http)")));
                    set_actions();
                    load_home();
                    show();
                });
            });
        });
    }

    // A Jellyfin-style dialog: a dark card, the question, a blue-edged box
    // (a Text screen while it is up).
    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, bool password, Done done) {
        done_ = std::move(done);
        ask_row_ = focused_index();
        shell_set_actions({{"OK", 0, "Enter", nullptr}, {"Cancel", 0, "Esc", nullptr}});
        input_ = panel(root_, 0, 0, 300, 54, kJfRaised, 6);
        lv_obj_align(input_, LV_ALIGN_TOP_MID, 0, 28);
        lv_obj_set_style_pad_all(input_, 6, 0);
        label(input_, title, &zs_font_12, kJfText);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, password);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 288);
        lv_obj_set_style_bg_color(ta, c(0x1c1c1c), 0);
        lv_obj_set_style_text_color(ta, c(kJfText), 0);
        lv_obj_set_style_border_color(ta, c(kJfBlue), 0);
        lv_obj_set_style_border_width(ta, 1, 0);
        lv_obj_set_style_radius(ta, 4, 0);
        lv_obj_set_style_pad_ver(ta, 3, 0);
        lv_obj_set_style_bg_color(ta, c(kJfBlue), LV_PART_CURSOR | LV_STATE_FOCUSED);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<JellyfinApp*>(lv_event_get_user_data(e));
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
        lv_group_remove_obj(lv_obj_get_child(in, 1));
        set_actions();
        // Focus goes back to the list, if there is one.
        if (list_built_ && !stack_.empty()) list_.focus(ask_row_ >= 0 ? ask_row_ : 0);
    }

    // ---- Video ----------------------------------------------------------

    static std::string new_session_id() {
        std::random_device rd;
        return fmt("%08x%08x%08x%08x", unsigned(rd()), unsigned(rd()), unsigned(rd()), unsigned(rd()));
    }

    // Play items[i] (a movie or episode) on the screen. The rest of the list
    // stays with it, so an episode can go on to the next one.
    void open_video(const std::vector<JfItem>& items, int i) {
        const JfItem it = items[size_t(i)];
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
        build_video_view();
        set_actions();
        show_osd();
        if (start > 0) shell_toast("Resuming at " + mmss(start * 1000));
    }

    void build_video_view() {
        vid_ = panel(root_, 0, 0, kVidW, kVidH, 0x000000, 0);

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

        // Loading: Jellyfin's spinner colour.
        vid_wait_ = label(vid_, "Loading...", &zs_font_14, kJfBlue);
        lv_obj_center(vid_wait_);

        // The info bar along the bottom, fading up from black as in
        // Jellyfin's player: title, a blue progress line, the time.
        vid_osd_ = panel(vid_, 0, kVidH - 42, kVidW, 42, 0x000000, 0);
        lv_obj_set_style_bg_opa(vid_osd_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_grad_color(vid_osd_, c(0x000000), 0);
        lv_obj_set_style_bg_main_opa(vid_osd_, LV_OPA_20, 0);
        lv_obj_set_style_bg_grad_opa(vid_osd_, LV_OPA_90, 0);
        lv_obj_set_style_bg_grad_dir(vid_osd_, LV_GRAD_DIR_VER, 0);
        vid_title_ = label(vid_osd_, "", &zs_font_12, kJfText);
        lv_obj_set_width(vid_title_, kVidW - 12);
        lv_label_set_long_mode(vid_title_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(vid_title_, 6, 4);
        vid_bar_ = thin_bar(vid_osd_, kVidW - 12, 3, kJfBlue);
        lv_obj_set_style_bg_color(vid_bar_, c(0x555555), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vid_bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_bar_set_range(vid_bar_, 0, 1000);
        lv_obj_set_pos(vid_bar_, 6, 21);
        vid_time_ = label(vid_osd_, "", &zs_font_12, kJfDim);
        lv_obj_set_pos(vid_time_, 6, 26);
        std::string t = vid_item_.kind == JfItem::Episode
                            ? vid_item_.detail + " " + vid_item_.episode_code() + " " + vid_item_.name
                            : vid_item_.name;
        lv_label_set_text(vid_title_, t.c_str());

        if (group) lv_group_focus_obj(nullptr);
        // Pictures come faster than the shell's 10 Hz tick.
        vid_timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<JellyfinApp*>(lv_timer_get_user_data(t))->video_pump();
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
            if (!st.paused) hal().video_pause(true);
        } else if (!st.paused) {
            power().stay_awake(now);
        }
        vid_pos_s_ = st.pos_ms / 1000;
        if (now - vid_reported_at_ >= kReportEveryMs) {
            vid_reported_at_ = now;
            hal().jellyfin_report(jf_, JfReport::Progress, vid_item_.id, vid_session_, vid_pos_s_, st.paused);
        }

        const char* wait = st.loading ? "Loading..." : st.paused ? LV_SYMBOL_PAUSE : "";
        lv_label_set_text(vid_wait_, wait);
        lv_obj_set_style_text_font(vid_wait_, st.paused && !st.loading ? &zs_font_28 : &zs_font_14, 0);
        lv_obj_set_style_text_color(vid_wait_, c(st.paused && !st.loading ? kJfText : kJfBlue), 0);
        lv_obj_center(vid_wait_);
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
            shell_toast("Up next: " + items[size_t(next)].name);
            open_video(items, next);
        } else {
            shell_toast("Finished " + vid_item_.name);
        }
    }

    void show_osd() { vid_osd_at_ = lv_tick_get(); }

    void video_toggle_pause() {
        VideoState st = hal().video();
        hal().video_pause(!st.paused);
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
            case 'r': case 'R': video_restart(); return true;
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
        // The shell's own keys (volume, brightness, Tab for the menu, 0 for
        // the key sheet, Ctrl + a letter) still work; the page underneath
        // gets nothing (1 to 9 would pick a row of the list behind).
        return !(is_shell_key(key) || is_ctrl_key(key) || key == kKeyNext || key == '0');
    }

    void video_restart() {
        show_osd();
        hal().video_seek(0, true);
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
            // The lists may have a stale resume point; show the place we stopped.
            int at = st.finished ? 0 : vid_pos_s_;
            for (auto& lvl : stack_)
                for (auto& x : lvl.items)
                    if (x.id == vid_item_.id) x.resume_s = at;
            for (auto it = resume_.begin(); it != resume_.end();) {
                if (it->id == vid_item_.id && at == 0) {
                    it = resume_.erase(it);
                    continue;
                }
                if (it->id == vid_item_.id) it->resume_s = at;
                ++it;
            }
            if (at > 0 && std::none_of(resume_.begin(), resume_.end(),
                                       [&](const JfItem& x) { return x.id == vid_item_.id; })) {
                JfItem x = vid_item_;
                x.resume_s = at;
                resume_.insert(resume_.begin(), x);
            }
            keep_row_ = focused_index() >= 0 ? focused_index() : vid_at_;
            set_actions();
            show();
        }
        vid_ = nullptr;
        vid_pixels_.clear();
        vid_pixels_.shrink_to_fit();
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t *header_ = nullptr, *logo_ = nullptr, *word_ = nullptr, *back_ = nullptr, *title_ = nullptr;
    lv_obj_t *avatar_ = nullptr, *initial_ = nullptr;
    lv_obj_t *now_ = nullptr, *now_bar_ = nullptr, *now_state_ = nullptr, *now_title_ = nullptr,
             *now_time_ = nullptr;
    bool now_shown_ = false;
    std::string now_text_;
    lv_obj_t* page_ = nullptr;   // home, a list, or the welcome
    ListView list_;
    bool list_built_ = false;    // list_ has widgets (a list is on show)
    int ask_row_ = -1;           // the row in focus when a text box opened
    std::string listed_playing_;
    int keep_row_ = -1;          // the row to select when the list is drawn

    // Home
    std::vector<Section> home_;
    int sel_row_ = 0, sel_col_ = 0;
    lv_obj_t* focused_obj_ = nullptr;
    std::vector<JfItem> libs_;     // the user's libraries
    std::vector<JfItem> resume_;   // Continue watching
    bool home_loaded_ = false;

    JfSession jf_;
    std::vector<Level> stack_;
    std::vector<int> back_rows_;   // for each level, the row it was opened from
    lv_obj_t* input_ = nullptr;
    Done done_;
    std::string typed_server_, typed_user_;

    // Video
    lv_obj_t* vid_ = nullptr;   // the player view, over everything; null when no video
    lv_obj_t *vid_img_ = nullptr, *vid_wait_ = nullptr, *vid_osd_ = nullptr, *vid_title_ = nullptr,
             *vid_bar_ = nullptr, *vid_time_ = nullptr;
    lv_timer_t* vid_timer_ = nullptr;
    lv_image_dsc_t vid_dsc_{};
    std::vector<uint16_t> vid_pixels_;
    std::vector<JfItem> vid_items_;   // the list it was started from
    int vid_at_ = -1;
    JfItem vid_item_;
    std::string vid_session_;
    uint32_t vid_reported_at_ = 0, vid_osd_at_ = 0;
    int vid_pos_s_ = 0;
    bool vid_started_ = false;
};
}  // namespace

std::unique_ptr<App> make_jellyfin_app() { return std::make_unique<JellyfinApp>(); }

}  // namespace zs
