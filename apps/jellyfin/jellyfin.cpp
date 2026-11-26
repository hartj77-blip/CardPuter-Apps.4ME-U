// Jellyfin: music and video from your Jellyfin server, dressed like
// Jellyfin's own apps: the near-black dark theme, the purple-to-blue logo,
// cover art everywhere, a home screen of card rows (My Media, Continue
// Watching, Next Up, Recently Added), poster grids, pages for an album, a
// film or an episode, a search that finds as you type, and a now-playing
// bar along the bottom.
//
// The kinds of screen (docs/UX.md in the OS), each with its keys in the
// soft-key bar and all its actions in the Tab menu:
//   Home      Control: arrows move between cards, Enter opens, F search,
//             Space pause, N / P next / previous, + / - volume
//   Grid      List: a library as a grid of covers. Typing narrows it,
//             Enter opens, Backspace or Esc go back
//   Tracks    List (ui/list_view.h): an album or playlist, its cover beside
//             its tracks. Typing narrows, 1 to 9 pick, Enter plays
//   Details   Control: a film or episode. Left / Right choose a button,
//             Enter presses it; R from the start, W watched, V favourite
//   Search    Text: results arrive as you type, a row for each kind of thing
//   Playing   Control: the track that is playing, with its cover
//   Video     Control: Enter or Space pause, Left / Right -/+10 s, Up / Down
//             +/-1 min, R from the start, M on the monitor, S or Esc stop
// Fn + Q / W / E, the keyboard's media keys, work everywhere.
//
// Nothing waits on the network on the screen's thread: lists, searches and
// pictures are fetched by work.h's threads (api.h, ui.h) and the screen
// fills in as they arrive.
//
// Music goes through the shell's queue (ui/player.h), so it carries on to
// the next track after you leave the app or the screen turns off. Video
// belongs to the app: it stops when you leave Jellyfin.
//
// The sign-in (jellyfin.* in prefs) is the one the Media app used to keep,
// so a device that was signed in there stays signed in.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <memory>
#include <random>
#include <unordered_map>

#include "apps/common.h"
#include "apps/jellyfin/api.h"
#include "apps/jellyfin/art.h"
#include "apps/jellyfin/ui.h"
#include "apps/jellyfin/work.h"
#include "platform/keys.h"
#include "ui/list_view.h"
#include "ui/player.h"
#include "ui/power.h"
#include "ui/prefs.h"

namespace zs {
namespace {
using namespace theme;
using namespace jfapp;

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
constexpr uint32_t kOsdMs = 3000;      // the info bar stays up this long after a key
constexpr uint32_t kPollMs = 40;       // how often finished work is looked for
constexpr uint32_t kIdlePollMs = 120;  // ... with none on its way
constexpr uint32_t kWatchingWaitMs = 2500;   // after a video stops, before asking what is next
constexpr uint32_t kSearchWaitMs = 350;   // after the last key, before asking the server
constexpr int kMaxItems = 5000;        // rows of one list kept in memory
constexpr int kMaxLatestRows = 6;      // "Recently Added" rows on Home
constexpr int kRecentSearches = 6;

// The simulator's hardware layer signs in with this token and has no server
// behind it: lists come from it, pictures are drawn.
constexpr char kDemoToken[] = "simulated";
// Its library views came with a later ZeroOS than some this app is built
// for; they were added at the end of the enum, so they go by number.
constexpr JfView kViewLibraries = JfView(13);
constexpr JfView kViewLibrary = JfView(14);

std::string mmss(int ms) {
    if (ms < 0) return "--:--";
    int s = ms / 1000;
    if (s >= 3600) return fmt("%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    return fmt("%d:%02d", s / 60, s % 60);
}

// "1h 12m" / "38m": how Jellyfin says how long something is.
std::string hm(int s) {
    if (s <= 0) return "";
    int m = (s + 59) / 60;
    return m >= 60 ? fmt("%dh %dm", m / 60, m % 60) : fmt("%dm", m);
}

std::string left_text(int s) { return s <= 0 ? "" : hm(s) + " left"; }

// a, " · ", b, leaving out what is empty.
std::string dot(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + " \xC2\xB7 " + b;
}

// The logo's colours, left to right.
void logo_gradient(lv_obj_t* o) {
    lv_obj_set_style_bg_color(o, c(kJfPurple), 0);
    lv_obj_set_style_bg_grad_color(o, c(kJfBlue), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, 0);
}

void vis(lv_obj_t* o, bool v) {
    if (!o) return;
    if (v) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* dots_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int w, int lines) {
    lv_obj_t* l = label(parent, "", font, color);
    lv_obj_set_size(l, w, lv_font_get_line_height(font) * lines);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

void make_dir(const std::string& path) { mkdir(path.c_str(), 0700); }

void empty_dir(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name != "." && name != "..") unlink((dir + "/" + name).c_str());
    }
    closedir(d);
}

std::string lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower((unsigned char)ch));
    return s;
}

// What a list says about an item beside its name.
std::string sub_text(const Item& it) {
    switch (it.kind) {
        case Kind::Episode: {
            std::string t = it.resume_s > 0 ? left_text(it.duration_s - it.resume_s) : hm(it.duration_s);
            return dot(it.episode_code(), t);
        }
        case Kind::Movie:
        case Kind::Video: {
            std::string t = it.resume_s > 0 ? left_text(it.duration_s - it.resume_s) : hm(it.duration_s);
            return dot(it.year > 0 ? std::to_string(it.year) : "", t);
        }
        case Kind::Album: return dot(it.detail, it.year > 0 ? std::to_string(it.year) : "");
        case Kind::Track: return dot(it.detail, it.duration_s > 0 ? mmss(it.duration_s * 1000) : "");
        case Kind::Series:
            return dot(it.year > 0 ? std::to_string(it.year) : "", it.unplayed > 0 ? fmt("%d to watch", it.unplayed) : "");
        case Kind::Season: return dot(it.detail, it.unplayed > 0 ? fmt("%d to watch", it.unplayed) : "");
        default: return "";
    }
}

// An item's name where its show isn't said elsewhere: "Bench Talk S2E2 Reading".
std::string full_name(const Item& it) {
    if (it.kind == Kind::Episode) {
        std::string t = it.detail;
        if (!it.episode_code().empty()) t += (t.empty() ? "" : " ") + it.episode_code();
        return t + (t.empty() ? "" : " ") + it.name;
    }
    return it.name;
}

Item pseudo(const std::string& name, const std::string& what) {
    Item it;
    it.kind = Kind::Folder;
    it.name = name;
    it.detail = what;
    return it;
}

class JellyfinApp : public App {
public:
    ~JellyfinApp() override {
        // Leaving the app: the screen is being torn down, so only stop the
        // video and tell the server where we got to.
        close_video(false);
        if (poll_timer_) lv_timer_delete(poll_timer_);
        worker_.stop();
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
        // A token that isn't plain characters could end the header it is
        // sent in: sign in again instead.
        if (!safe_token(jf_.token)) jf_.token.clear();

        panel(root, 0, 0, kScreenW, kH, kJfBg, 0);
        build_header();
        build_now_bar();
        poll_timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<JellyfinApp*>(lv_timer_get_user_data(t))->poll();
        }, kPollMs, this);
        start_session();
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
        if (Level* l = top(); l && l->type == Level::Playing) update_playing();
    }

    bool on_key(uint32_t key) override {
        if (vid_) return video_key(key);
        if (input_) return false;   // typing in the sign-in box
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
        Level* l = top();
        if (!l) return home_key(key);
        switch (l->type) {
            case Level::Grid: return grid_key(*l, key);
            case Level::Tracks: return tracks_key(key);
            case Level::Detail: return detail_key(*l, key);
            case Level::Search: return search_key(*l, key);
            case Level::Playing: return playing_key(key);
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
        if (Level* l = top()) {
            // Esc in a text box with something in it clears it first.
            if (l->type == Level::Search && search_box_ && lv_textarea_get_text(search_box_)[0]) {
                lv_textarea_set_text(search_box_, "");
                return true;
            }
            back();
            return true;
        }
        return false;
    }

private:
    // One screen above Home; the stack of them is where you are.
    struct Level {
        enum Type { Grid, Tracks, Detail, Search, Playing } type = Grid;
        uint32_t serial = 0;        // names the level to work that comes back later
        std::string title;
        Query query;
        std::vector<Item> items;
        int fetched = 0;            // rows of the list asked for so far
        int total = -1;
        bool loading = false;
        std::string error;
        Item parent;                // Tracks: the album or playlist. Detail: the item itself
        int focus = -1;             // the card or row to select when it is drawn
        std::string typed;          // Tracks: what was typed to narrow the list
        // Detail: the list it was opened from, so an episode can go on to the next.
        std::vector<Item> siblings;
        int at = 0;
        int button = 0;
        // Search
        std::string text;           // what is in the box
        std::string searched;       // what `sections` are the results for
        std::string note;           // shown when there are no results
        std::vector<Section> sections;
        std::string token;          // the card in focus
        uint32_t search_serial = 0;
    };

    // ---- The session and its work -------------------------------------------

    bool demo() const {
#ifdef __EMSCRIPTEN__
        if (jf_.signed_in()) return true;   // a web page has no curl to reach a server with
#endif
        return jf_.token == kDemoToken;
    }

    std::string auth() const { return jf::auth_header(jf_); }

    void start_session() {
        worker_.stop();
        art_.stop();
        if (!jf_.signed_in()) return;
        worker_.set_inline(demo());
        cache_dir_ = home_dir() + "/.cache/zeroos/jellyfin";
        if (!demo()) {
            make_dir(home_dir() + "/.cache");
            make_dir(home_dir() + "/.cache/zeroos");
            make_dir(cache_dir_);
            make_dir(cache_dir_ + "/art");
            make_dir(cache_dir_ + "/tmp");
            chmod(cache_dir_.c_str(), 0700);
            empty_dir(cache_dir_ + "/tmp");   // what a visit that was cut short left
        }
        art_.start(&worker_, jf_.server, auth(), cache_dir_ + "/art", cache_dir_ + "/tmp", demo());
    }

    // One question for the server, and its answer.
    struct Ask {
        std::string path;                 // after the server's address
        std::string legacy;               // where older servers keep it, or ""
        bool single = false;              // one item, not a list
        std::function<Page()> demo;       // what the simulator answers
    };
    struct Answer {
        bool ok = false;
        int status = 0;
        Page page;
    };
    using Answers = std::vector<Answer>;

    // Ask several things at once (one curl). done runs on the screen's
    // thread when every answer is in; never after signing out.
    void ask(std::vector<Ask> asks, std::function<void(Answers&)> done, bool urgent = true) {
        auto out = std::make_shared<Answers>(asks.size());
        const uint32_t session = session_;
        auto finish = [this, out, done, session] {
            if (session != session_ || !jf_.signed_in()) return;
            for (const Answer& a : *out)
                if (a.status == 401) {
                    signed_out_by_server();
                    return;
                }
            done(*out);
        };
        if (demo()) {
            for (size_t i = 0; i < asks.size(); ++i) {
                Answer& a = (*out)[i];
                a.ok = true;
                a.status = 200;
                if (asks[i].demo) a.page = asks[i].demo();
            }
            worker_.post(Worker::Urgent, [](std::atomic<int>&) {}, finish);
            return;
        }
        const std::string server = jf_.server, header = auth(), dir = cache_dir_ + "/tmp";
        worker_.post(
            urgent ? Worker::Urgent : Worker::Normal,
            [asks, out, server, header, dir](std::atomic<int>& pid) {
                auto fetch = [&](const std::vector<size_t>& which, bool legacy) {
                    std::vector<std::string> urls;
                    for (size_t i : which) urls.push_back(server + (legacy ? asks[i].legacy : asks[i].path));
                    std::vector<Reply> replies = http_get_all(urls, header, dir, &pid);
                    for (size_t k = 0; k < which.size(); ++k) {
                        Answer& a = (*out)[which[k]];
                        a.status = replies[k].status;
                        if (a.status != 200) continue;
                        if (asks[which[k]].single) {
                            Item it;
                            a.ok = parse_item(replies[k].body, it);
                            if (a.ok) a.page.items.push_back(std::move(it));
                        } else {
                            a.ok = parse_page(replies[k].body, a.page);
                        }
                    }
                };
                std::vector<size_t> all, again;
                for (size_t i = 0; i < asks.size(); ++i) {
                    if (asks[i].path.empty()) (*out)[i].status = -1;   // nothing to ask (a bad id)
                    else all.push_back(i);
                }
                if (!all.empty()) fetch(all, false);
                // Servers before 10.9 keep a few lists under /Users/<id>.
                for (size_t i : all)
                    if (((*out)[i].status == 404 || (*out)[i].status == 405) && !asks[i].legacy.empty())
                        again.push_back(i);
                if (!again.empty()) fetch(again, true);
            },
            finish);
    }

    Ask ask_for(const Query& q, int start = 0) {
        Ask a;
        a.path = list_path(q, jf_.user_id, start);
        if (start == 0) a.legacy = legacy_path(q, jf_.user_id);
        a.demo = [this, q, start] { return start == 0 ? demo_page(q) : Page{}; };
        return a;
    }

    std::string error_text(const Answer& a) {
        if (a.status == -1) return "Nothing to open";
        if (a.status == 200) return "Unexpected answer from the server";
        return status_error(a.status);
    }

    void signed_out_by_server() {
        shell_toast("Signed out by the server; sign in again");
        forget_session();
    }

    // ---- The simulator's answers ----------------------------------------

    static Item from_hal(const JfItem& j) {
        Item it;
        int k = int(j.kind);
        it.kind = k >= 0 && k <= int(Kind::Folder) ? Kind(k) : Kind::Folder;
        it.id = j.id;
        it.name = j.name;
        it.detail = j.detail;
        it.duration_s = j.duration_s;
        it.resume_s = j.resume_s;
        it.number = j.number;
        it.season = j.season;
        it.year = j.year;
        it.series_id = j.series_id;
        // So the simulator shows the badges too.
        if (it.kind == Kind::Series) it.unplayed = 3;
        if (it.kind == Kind::Season) {
            it.played = it.number == 1;
            it.unplayed = it.played ? 0 : 3;
        }
        if (it.kind == Kind::Episode && it.season == 1) it.played = true;
        if (it.kind == Kind::Movie && it.name == "Big Buck Bunny") it.played = true;
        if (it.playable_video())
            it.overview = "This is the simulator, with no server behind it. On your own Jellyfin, "
                          "the description of " + it.name + " shows here.";
        return it;
    }

    Page hal_list(JfView view, const std::string& arg, size_t most = 0) {
        Page p;
        std::vector<JfItem> got;
        std::string error;
        if (hal().jellyfin_list(jf_, view, arg, got, error))
            for (const JfItem& j : got) {
                if (most && p.items.size() >= most) break;
                p.items.push_back(from_hal(j));
            }
        p.count = int(p.items.size());
        return p;
    }

    Page demo_page(const Query& q) {
        switch (q.what) {
            case Query::Views: return hal_list(kViewLibraries, "");
            case Query::Resume: return hal_list(JfView::Resume, "");
            case Query::NextUp: {
                Page p = hal_list(JfView::SeasonEpisodes, "e1:f2");
                if (p.items.size() > 2) p.items = {p.items[2]};
                else p.items.clear();
                return p;
            }
            case Query::Latest: return hal_list(kViewLibrary, q.a + ":" + q.b, 8);
            case Query::LatestMusic: return hal_list(JfView::Latest, "");
            case Query::Library: return hal_list(kViewLibrary, q.a + ":" + q.b);
            case Query::Albums: return hal_list(JfView::Albums, "");
            case Query::Artists: return hal_list(JfView::Artists, "");
            case Query::Playlists: return hal_list(JfView::Playlists, "");
            case Query::Movies: return hal_list(JfView::Movies, "");
            case Query::Shows: return hal_list(JfView::Shows, "");
            case Query::Songs: return hal_list(JfView::AlbumTracks, "a1");
            case Query::Favorites: {
                Page p = hal_list(JfView::Albums, "", 2);
                Page m = hal_list(JfView::Movies, "", 1);
                p.items.insert(p.items.end(), m.items.begin(), m.items.end());
                for (Item& it : p.items) it.favorite = true;
                return p;
            }
            case Query::ArtistAlbums: return hal_list(JfView::ArtistAlbums, q.a);
            case Query::AlbumTracks: return hal_list(JfView::AlbumTracks, q.a);
            case Query::PlaylistTracks: return hal_list(JfView::PlaylistTracks, q.a);
            case Query::Seasons: return hal_list(JfView::Seasons, q.a);
            case Query::Episodes: return hal_list(JfView::SeasonEpisodes, q.a + ":" + q.b);
            case Query::Suggestions: {
                Page p = hal_list(JfView::Movies, "");
                Page a = hal_list(JfView::Albums, "", 3);
                p.items.insert(p.items.end(), a.items.begin(), a.items.end());
                return p;
            }
        }
        return Page{};
    }

    // The simulator's search: everything it has whose name holds the text.
    Page demo_search(const std::string& group, const std::string& text) {
        Page all;
        auto add = [&](Page p) { all.items.insert(all.items.end(), p.items.begin(), p.items.end()); };
        if (group == "Movies") add(hal_list(JfView::Movies, ""));
        else if (group == "Shows") add(hal_list(JfView::Shows, ""));
        else if (group == "Episodes") add(hal_list(JfView::SeasonEpisodes, "e1:f1")), add(hal_list(JfView::SeasonEpisodes, "e1:f2"));
        else if (group == "Playlists") add(hal_list(JfView::Playlists, ""));
        else if (group == "Artists") add(hal_list(JfView::Artists, ""));
        else if (group == "Albums") add(hal_list(JfView::Albums, ""));
        else if (group == "Songs") add(hal_list(JfView::AlbumTracks, "a1"));
        Page p;
        const std::string want = lower(text);
        for (Item& it : all.items)
            if (lower(it.name).find(want) != std::string::npos) p.items.push_back(it);
        return p;
    }

    // ---- The frame ----------------------------------------------------------

    void build_header() {
        header_ = panel(root_, 0, 0, kScreenW, kHeadH, kJfBg, 0);
        // The logo: the gradient mark and the lower-case name.
        logo_ = panel(header_, 6, 4, 12, 12, kJfPurple, 3);
        logo_gradient(logo_);
        panel(logo_, 4, 5, 4, 4, kJfBg, 2);
        word_ = label(header_, "jellyfin", &zs_font_16, kJfText);
        lv_obj_set_pos(word_, 22, 1);
        // In a list: a back arrow and the list's name instead.
        back_ = label(header_, LV_SYMBOL_LEFT, &zs_font_14, kJfText);
        lv_obj_set_pos(back_, 6, 3);
        title_ = dots_label(header_, &zs_font_14, kJfText, 236, 1);
        lv_obj_set_pos(title_, 22, 2);
        // Right: search, and the user's round avatar with an initial.
        avatar_ = panel(header_, kScreenW - 20, 3, 14, 14, kJfBlue, LV_RADIUS_CIRCLE);
        initial_ = label(avatar_, "", &zs_font_12, kJfText);
        lv_obj_center(initial_);
        glass_ = label(header_, ZS_ICON_SEARCH, &zs_font_12, kJfDim);
        lv_obj_set_pos(glass_, kScreenW - 38, 4);
        // Work in progress: a blue dash that runs along under the header.
        busy_ = panel(root_, 0, kHeadH - 2, 48, 2, kJfBlue, 0);
        vis(busy_, false);
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
        // The cover, then play or pause, the track and the time.
        now_art_box_ = panel(now_, 4, 3, 14, 14, kJfCard, 0);
        now_img_ = lv_image_create(now_art_box_);
        lv_obj_set_pos(now_img_, 0, 0);
        vis(now_img_, false);
        now_state_ = label(now_, LV_SYMBOL_PLAY, &zs_font_12, kJfBlue);
        lv_obj_set_pos(now_state_, 23, 3);
        now_title_ = dots_label(now_, &zs_font_12, kJfText, 200, 1);
        lv_obj_set_pos(now_title_, 38, 3);
        now_time_ = label(now_, "", &zs_font_12, kJfDim);
        lv_obj_align(now_time_, LV_ALIGN_TOP_RIGHT, -6, 3);
        lv_obj_add_flag(now_, LV_OBJ_FLAG_HIDDEN);
    }

    // The track that is playing, as the server knows it (for its cover).
    // Null until it is known; asks the server when the app didn't start it.
    const Item* now_item() {
        const QueueItem* q = player_current();
        if (!q) return nullptr;
        auto it = by_url_.find(q->path);
        if (it != by_url_.end()) return &it->second;
        // Started on an earlier visit: the id is in the stream's address.
        auto a = q->path.find("/Audio/");
        auto b = q->path.find("/universal");
        if (a == std::string::npos || b == std::string::npos || b < a + 7) return nullptr;
        std::string id = q->path.substr(a + 7, b - a - 7);
        if (!safe_id(id) || id == asked_track_ || demo() || !jf_.signed_in()) return nullptr;
        asked_track_ = id;
        Ask one;
        one.path = item_path(id, jf_.user_id);
        one.single = true;
        std::string url = q->path;
        ask({one}, [this, url](Answers& ans) {
            if (ans[0].ok && !ans[0].page.items.empty()) by_url_[url] = ans[0].page.items[0];
        }, false);
        return nullptr;
    }

    // The bar shows while music plays; the page above it gets shorter.
    void update_now_bar() {
        PlaybackState pb = hal().playback();
        Level* l = top();
        bool on = jf_.signed_in() && !pb.path.empty() && !(l && l->type == Level::Playing);
        if (on != now_shown_) {
            now_shown_ = on;
            vis(now_, on);
            if (!input_) show();   // every page is laid out for its height
        }
        if (on) {
            const QueueItem* q = player_current();
            std::string t = q ? (q->artist.empty() ? q->title : q->title + " - " + q->artist) : pb.path;
            if (t != now_text_) {
                now_text_ = t;
                lv_label_set_text(now_title_, t.c_str());
                now_art_.reset();
                vis(now_img_, false);
            }
            if (!now_art_) {
                if (const Item* it = now_item()) {
                    ArtPtr a;
                    if (art_.get(*it, Shape::Square, 14, 14, a) == ArtStore::Ready) {
                        now_art_ = a;
                        lv_image_set_src(now_img_, &a->dsc);
                        vis(now_img_, true);
                    } else {
                        art_.flush();
                    }
                }
            }
            lv_label_set_text(now_state_, pb.paused ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
            lv_label_set_text(now_time_, (mmss(pb.pos_ms) + " / " + mmss(pb.dur_ms)).c_str());
            lv_obj_align(now_time_, LV_ALIGN_TOP_RIGHT, -6, 3);
            lv_bar_set_value(now_bar_, pb.dur_ms > 0 ? int(1000LL * pb.pos_ms / pb.dur_ms) : 0, LV_ANIM_OFF);
        }
        // The playing track's row follows the queue (not under a text box).
        if (pb.path != listed_playing_ && !input_ && l && l->type == Level::Tracks) {
            show();
        }
    }

    int page_h() const { return kH - kHeadH - (now_shown_ ? kNowH : 0); }

    Level* top() { return stack_.empty() ? nullptr : stack_.back().get(); }

    Level* find_level(uint32_t serial) {
        for (auto& l : stack_)
            if (l->serial == serial) return l.get();
        return nullptr;
    }

    void update_header() {
        Level* l = top();
        bool home = !l, search = l && l->type == Level::Search;
        vis(logo_, home);
        vis(word_, home);
        vis(back_, !home);
        vis(title_, !home && !search);
        vis(glass_, !search && jf_.signed_in());
        if (l && !search) {
            std::string t = l->title;
            if (l->type == Level::Grid && l->total > 0) t += fmt("  (%d)", l->total);
            lv_label_set_text(title_, t.c_str());
        }
        vis(avatar_, jf_.signed_in());
        std::string ini = jf_.user.empty() ? "?" : std::string(1, char(std::toupper((unsigned char)jf_.user[0])));
        lv_label_set_text(initial_, ini.c_str());
        lv_obj_center(initial_);
    }

    // The actions of the screen on show (docs/UX.md). Control screens' letters
    // work as they are and the bar shows them; a List screen's are in the menu.
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
        Level* l = top();
        const bool playing = !hal().playback().path.empty();
        auto common = [&] {
            a.push_back({"Now playing", 'y', nullptr, [this] { open_playing(); }, playing});
            a.push_back({"Stop music", 't', nullptr, [] { player_stop(); }, playing});
            if (top()) a.push_back({"Home", 'h', nullptr, [this] { go_home(); }});
            a.push_back({"Refresh", 'r', nullptr, [this] { refresh(); }});
            a.push_back({"Sign out", 'o', nullptr, [this] { sign_out(); }, true, true, "Sign out of Jellyfin?"});
        };
        if (!l) {
            a.push_back({"Open", 0, "Enter", nullptr});
            a.push_back({"Search", 'f', "F", [this] { open_search(); }});
            a.push_back({"Pause", 0, "Space", nullptr});
            a.push_back({"Next", 'n', "N", [] { player_step(+1); }});
            a.push_back({"Previous", 'p', "P", [] { player_step(-1); }});
            a.push_back({"Volume", 0, "+ -", nullptr});
            common();
        } else if (l->type == Level::Grid || l->type == Level::Tracks) {
            a.push_back({l->type == Level::Tracks ? "Play" : "Open", 0, "Enter", nullptr});
            a.push_back({"Back", 0, "Esc", nullptr});
            a.push_back({"Pause", 'u', "Space", [this] { pause(); }});
            if (l->type == Level::Tracks) {
                a.push_back({"Play all", 'a', nullptr, [this] { play_all(false); }});
                a.push_back({"Shuffle", 's', nullptr, [this] { play_all(true); }});
                a.push_back({l->parent.favorite ? "Not a favourite" : "Favourite", 'v', nullptr,
                             [this] { toggle_favorite_parent(); }, !l->parent.id.empty()});
            } else if (sortable(l->query.what)) {
                a.push_back({std::string("Sort: ") + lower(sort_name(next_sort(l->query.sort))), 's', nullptr,
                             [this] { cycle_sort(); }});
            }
            a.push_back({"Search", 'f', nullptr, [this] { open_search(); }});
            a.push_back({"Next track", 'n', nullptr, [] { player_step(+1); }});
            a.push_back({"Previous track", 'p', nullptr, [] { player_step(-1); }});
            common();
        } else if (l->type == Level::Detail) {
            const Item& it = l->parent;
            // The bar has room for two or three; V works too, and is in the menu.
            const bool part = it.resume_s > 0;
            a.push_back({part ? "Resume" : "Play", 0, "Enter", nullptr});
            a.push_back({"Restart", 'r', part ? "R" : nullptr, [this] { detail_play(true); }});
            a.push_back({it.played ? "Not watched" : "Watched", 'w', "W", [this] { detail_toggle(false); }});
            a.push_back({it.favorite ? "Not a favourite" : "Favourite", 'v', nullptr, [this] { detail_toggle(true); }});
            a.push_back({"Search", 'f', nullptr, [this] { open_search(); }});
            common();
        } else if (l->type == Level::Search) {
            a.push_back({"Open", 0, "Enter", nullptr});
            a.push_back({"Back", 0, "Esc", nullptr});
            a.push_back({"Move", 0, LV_SYMBOL_UP LV_SYMBOL_DOWN, nullptr});
            common();
        } else {   // Playing
            a.push_back({"Pause", 0, "Space", nullptr});
            a.push_back({"Previous", 'p', "P", [] { player_step(-1); }});
            a.push_back({"Next", 'n', "N", [] { player_step(+1); }});
            a.push_back({"Volume", 0, "+ -", nullptr});
            a.push_back({"Open the album", 'a', nullptr, [this] { open_playing_album(); }});
            a.push_back({"Search", 'f', nullptr, [this] { open_search(); }});
            a.push_back({"Stop music", 't', nullptr, [this] {
                             player_stop();
                             back();
                         }});
            a.push_back({"Home", 'h', nullptr, [this] { go_home(); }});
            a.push_back({"Sign out", 'o', nullptr, [this] { sign_out(); }, true, true, "Sign out of Jellyfin?"});
        }
        shell_set_actions(std::move(a));
    }

    // Remember where the focus is on the page on show, for when it is drawn
    // again (after a visit to a level above it, or a rebuild).
    void save_focus() {
        if (!page_) return;
        Level* l = find_level(shown_serial_);
        if (shown_serial_ == 0) {
            if (rows_.built()) home_token_ = rows_.focus_token();
        } else if (l) {
            if (l->type == Level::Grid && grid_.built()) l->focus = grid_.focused();
            else if (l->type == Level::Tracks && list_built_) {
                l->focus = list_.focused();
                l->typed = list_.filter();
            }
            else if (l->type == Level::Search && rows_.built()) l->token = rows_.focus_token();
        }
    }

    // Draw the page: home, a level, or the sign-in screen.
    void show() {
        save_focus();
        Level* l = top();
        uint32_t serial = l ? l->serial : 0;
        // The same list drawn again keeps what was typed to narrow it.
        if (serial == shown_serial_ && l && l->type == Level::Grid) grid_.widgets_gone();
        else grid_.reset();
        // The bar shows while music plays, except on the page that says the same.
        now_shown_ = jf_.signed_in() && !hal().playback().path.empty() && !(l && l->type == Level::Playing);
        vis(now_, now_shown_);
        if (page_) {
            if (group) lv_group_remove_all_objs(group);
            lv_obj_delete(page_);
            page_ = nullptr;
        }
        if (search_box_) {
            if (group) lv_group_set_editing(group, false);
            lv_obj_delete(search_box_);
            search_box_ = nullptr;
        }
        rows_.reset();
        list_.reset();   // its widgets went with the page
        list_built_ = false;
        side_ = CardView{};
        detail_buttons_.clear();
        play_ = PlayingView{};
        shown_serial_ = serial;
        // Covers a screen that is gone asked for and nobody fetched yet.
        worker_.drop_covers();
        if (!input_) set_actions();   // the sign-in box keeps its own (OK, Cancel)
        update_header();
        listed_playing_ = hal().playback().path;

        page_ = lv_obj_create(root_);
        style_plain(page_);
        lv_obj_set_pos(page_, 0, kHeadH);
        lv_obj_set_size(page_, kScreenW, page_h());
        lv_obj_set_style_bg_color(page_, c(kJfBg), 0);
        lv_obj_set_style_bg_opa(page_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(page_, LV_OBJ_FLAG_SCROLLABLE);

        if (!jf_.signed_in()) build_welcome();
        else if (!l) build_home();
        else if (l->type == Level::Grid) build_grid(*l);
        else if (l->type == Level::Tracks) build_tracks(*l);
        else if (l->type == Level::Detail) build_detail(*l);
        else if (l->type == Level::Search) build_search(*l);
        else build_playing();
        if (input_) lv_obj_move_foreground(input_);
        lv_obj_move_foreground(now_);
        lv_obj_move_foreground(busy_);
    }

    // From the timer: finished work, new pictures, the search that is due.
    void poll() {
        worker_.poll();
        if (art_.serial() != art_serial_) {
            art_serial_ = art_.serial();
            if (!vid_) load_art();
        }
        bool busy = worker_.pending() > 0 && jf_.signed_in() && !vid_;
        vis(busy_, busy);
        if (busy) lv_obj_set_x(busy_, int(lv_tick_get() / 6 % uint32_t(kScreenW + 48)) - 48);
        if (watching_stale_ && !vid_ && lv_tick_elaps(watching_stale_at_) >= kWatchingWaitMs) {
            watching_stale_ = false;
            load_watching();
        }
        // Look often while something is on its way, seldom otherwise.
        lv_timer_set_period(poll_timer_, busy || search_due_ ? kPollMs : kIdlePollMs);
        if (search_due_ && lv_tick_elaps(search_due_at_) >= kSearchWaitMs) {
            search_due_ = false;
            if (Level* l = top(); l && l->type == Level::Search) run_search(*l);
        }
    }

    // Show the pictures that have come in, and ask for the ones on screen.
    void load_art() {
        if (rows_.built()) rows_.load_art();
        if (grid_.built()) grid_.load_art();
        Level* l = top();
        if (side_.box && l) {
            card_load(side_, l->type == Level::Playing ? play_.item : l->parent, side_shape_, art_);
            art_.flush();
        }
    }

    void note(const std::string& text) {
        lv_obj_t* t = label(page_, text.c_str(), &zs_font_14, kJfDim);
        lv_obj_set_width(t, kScreenW - 24);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -4);
    }

    // ---- Signed out ---------------------------------------------------------

    void build_welcome() {
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

    // Libraries whose newest things get a row of their own, as in Jellyfin.
    static bool has_latest(const Item& lib) {
        const std::string& t = lib.detail;
        return t == "movies" || t == "tvshows" || t == "music" || t == "homevideos" || t == "musicvideos" ||
               t.empty();
    }

    // The user's libraries, Continue Watching and Next Up in one go, then
    // what is new in each library. The page fills in as each arrives.
    void load_home() {
        if (home_loading_ || home_loaded_ || !jf_.signed_in()) return;
        home_loading_ = true;
        home_error_.clear();
        Query views, resume, next;
        views.what = Query::Views;
        resume.what = Query::Resume;
        next.what = Query::NextUp;
        ask({ask_for(views), ask_for(resume), ask_for(next)}, [this](Answers& ans) {
            home_loading_ = false;
            if (!ans[0].ok && !ans[1].ok) {
                home_error_ = error_text(ans[0]);
                if (!top()) show();
                return;
            }
            home_loaded_ = true;
            libs_ = ans[0].ok ? ans[0].page.items : std::vector<Item>{};
            resume_ = ans[1].ok ? ans[1].page.items : std::vector<Item>{};
            next_up_ = ans[2].ok ? ans[2].page.items : std::vector<Item>{};
            latest_.clear();
            if (!top()) show();
            load_latest();
        });
    }

    // Continue Watching and Next Up again, after something was watched.
    void load_watching() {
        if (demo() || !jf_.signed_in() || !home_loaded_) return;
        Query resume, next;
        resume.what = Query::Resume;
        next.what = Query::NextUp;
        ask({ask_for(resume), ask_for(next)}, [this](Answers& ans) {
            if (ans[0].ok) resume_ = ans[0].page.items;
            if (ans[1].ok) next_up_ = ans[1].page.items;
            if (!top() && !vid_ && !input_) show();
        }, false);
    }

    void load_latest() {
        std::vector<Ask> asks;
        std::vector<Item> of;
        for (const Item& lib : libs_) {
            if (!has_latest(lib) || int(of.size()) >= kMaxLatestRows) continue;
            Query q;
            q.what = Query::Latest;
            q.a = lib.id;
            q.b = lib.detail;
            asks.push_back(ask_for(q));
            of.push_back(lib);
        }
        if (asks.empty()) return;
        ask(std::move(asks), [this, of](Answers& ans) {
            latest_.clear();
            for (size_t i = 0; i < ans.size(); ++i)
                if (ans[i].ok && !ans[i].page.items.empty()) latest_.push_back({of[i], ans[i].page.items});
            if (!top()) {
                show();
            }
        }, false);
    }

    std::vector<Section> home_sections() const {
        std::vector<Section> v;
        Section media{"media", "My Media", Shape::Wide, {}, true};
        if (libs_.empty()) {
            // A server that lists no libraries: the lists across all of them.
            media.items = {pseudo("Movies", "browse:movies"), pseudo("Shows", "browse:shows"),
                           pseudo("Music", "browse:albums")};
            media.labels = false;
        } else {
            media.items = libs_;
        }
        v.push_back(media);
        if (!resume_.empty()) v.push_back({"resume", "Continue Watching", Shape::Wide, resume_, true});
        if (!next_up_.empty()) v.push_back({"nextup", "Next Up", Shape::Wide, next_up_, true});
        for (const auto& l : latest_) {
            Section s{"latest:" + l.first.id, "Latest in " + l.first.name, shape_for(l.second), l.second, false};
            v.push_back(s);
        }
        Section browse{"browse", "Browse", Shape::Wide,
                       {pseudo("Albums", "browse:albums"), pseudo("Artists", "browse:artists"),
                        pseudo("Playlists", "browse:playlists"), pseudo("Songs", "browse:songs"),
                        pseudo("Favourites", "browse:favorites")},
                       false};
        v.push_back(browse);
        return v;
    }

    // What the title line says about the card in focus.
    static std::string row_caption(const Section& s, const Item& it) {
        if (it.id.empty()) return "";
        if (it.kind == Kind::Episode) {
            std::string name = it.episode_code() + (it.episode_code().empty() ? "" : " ") + it.name;
            if (!s.labels) name = dot(it.detail, name);
            // What is left first: the end of a long name is what gets cut.
            if (it.resume_s > 0) return dot(left_text(it.duration_s - it.resume_s), name);
            return dot(name, hm(it.duration_s));
        }
        if (s.labels) return sub_text(it);
        return dot(it.name, sub_text(it));
    }

    void build_home() {
        if (!home_loaded_) {
            if (!home_error_.empty()) note(home_error_ + "\nTab, then R, tries again");
            else note("Loading your media...");
            return;
        }
        rows_.build(page_, kScreenW, page_h(), home_sections(), &art_, row_caption);
        rows_.focus_by_token(home_token_);
    }

    bool home_key(uint32_t key) {
        if (rows_.built() && rows_.key(key)) return true;
        switch (key) {
            case LV_KEY_ENTER:
            case '\r':
                if (rows_.built() && rows_.item()) {
                    const Section& s = *rows_.section();
                    // Continue Watching and Next Up play; the rest open.
                    bool play = s.key == "resume" || s.key == "nextup";
                    const std::vector<Item> items = s.items;
                    open_item(items, rows_.col(), play);
                }
                return true;
        }
        // Home is a Control screen: these letters are in its action list.
        auto& h = hal();
        switch (key) {
            case ' ': pause(); return true;
            case 'n': case 'N': player_step(+1); return true;
            case 'p': case 'P': player_step(-1); return true;
            case 'f': case 'F': open_search(); return true;
            case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
        }
        return false;
    }

    void go_home() {
        save_focus();
        stack_.clear();
        show();
    }

    // Ask the server again for everything on show.
    void refresh() {
        art_.retry();
        Level* l = top();
        if (!l) {
            home_loaded_ = false;
            home_loading_ = false;
            load_home();
            show();
        } else if (l->type == Level::Grid || l->type == Level::Tracks) {
            reload(*l);
        } else if (l->type == Level::Search) {
            l->searched = "\x01";   // nothing was searched for: ask again
            run_search(*l);
        } else {
            show();
        }
    }

    // ---- Opening things -----------------------------------------------------

    Level& push(Level::Type type, const std::string& title) {
        save_focus();
        auto l = std::make_unique<Level>();
        l->type = type;
        l->title = title;
        l->serial = ++serial_;
        stack_.push_back(std::move(l));
        return *stack_.back();
    }

    void back() {
        if (stack_.empty()) return;
        save_focus();
        stack_.pop_back();
        show();
    }

    void open_list(Level::Type type, const std::string& title, const Query& q, const Item& parent = Item{}) {
        Level& l = push(type, title);
        l.query = q;
        l.parent = parent;
        load_level(l);
        show();
    }

    void reload(Level& l) {
        l.items.clear();
        l.fetched = 0;
        l.total = -1;
        l.error.clear();
        l.serial = ++serial_;   // what is still on its way is for the old list
        load_level(l);
        show();
    }

    // Fetch a level's list, a page at a time until it is whole.
    void load_level(Level& l) {
        l.loading = true;
        const uint32_t serial = l.serial;
        const bool first = l.fetched == 0;
        ask({ask_for(l.query, l.fetched)}, [this, serial, first](Answers& ans) {
            Level* l = find_level(serial);
            if (!l) return;
            Answer& a = ans[0];
            if (!a.ok) {
                l->loading = false;
                // A later page that failed: what came so far stays.
                if (first) l->error = error_text(a);
                if (top() == l) {
                    show();
                }
                return;
            }
            l->fetched += a.page.count;
            if (a.page.total >= 0) l->total = a.page.total;
            if (l->query.what == Query::AlbumTracks && !l->parent.id.empty())
                for (Item& it : a.page.items) {
                    // The album's page knows the album, even where a track doesn't say.
                    if (it.album.empty()) it.album = l->parent.name;
                    if (it.album_id.empty()) it.album_id = l->parent.id;
                    if (!it.poster.any()) it.poster = l->parent.poster;
                }
            l->items.insert(l->items.end(), a.page.items.begin(), a.page.items.end());
            bool more = paged(l->query.what) && a.page.count > 0 && int(l->items.size()) < kMaxItems &&
                        (l->total >= 0 ? l->fetched < l->total : a.page.count >= kPageSize);
            l->loading = more;
            // A show with one season: straight to its episodes.
            if (first && l->query.what == Query::Seasons && l->items.size() == 1 &&
                !l->items[0].series_id.empty()) {
                Query q;
                q.what = Query::Episodes;
                q.a = l->items[0].series_id;
                q.b = l->items[0].id;
                l->query = q;
                l->items.clear();
                l->fetched = 0;
                l->total = -1;
                load_level(*l);
                return;
            }
            if (top() == l) {
                if (first || l->type != Level::Grid || !grid_.built()) {
                    show();
                } else {
                    grid_.items_changed();
                    update_header();
                }
            }
            if (more) load_level(*l);
        }, first);
    }

    // Enter on items[i]. play: start a film or episode at once (Continue
    // Watching), instead of opening its page.
    void open_item(const std::vector<Item>& items, int i, bool play = false) {
        if (i < 0 || i >= int(items.size())) return;
        const Item it = items[size_t(i)];
        Query q;
        switch (it.kind) {
            case Kind::Folder:
                if (it.id.empty()) {
                    open_pseudo(it);
                    return;
                }
                q.what = Query::Library;
                q.a = it.id;
                q.b = it.detail;
                open_list(Level::Grid, it.name, q);
                break;
            case Kind::Album:
                q.what = Query::AlbumTracks;
                q.a = it.id;
                open_list(Level::Tracks, it.name, q, it);
                break;
            case Kind::Playlist:
                q.what = Query::PlaylistTracks;
                q.a = it.id;
                open_list(Level::Tracks, it.name, q, it);
                break;
            case Kind::Artist:
                q.what = Query::ArtistAlbums;
                q.a = it.id;
                open_list(Level::Grid, it.name, q);
                break;
            case Kind::Series:
                q.what = Query::Seasons;
                q.a = it.id;
                open_list(Level::Grid, it.name, q);
                break;
            case Kind::Season:
                if (it.series_id.empty()) {
                    shell_toast("The server didn't say which show");
                    break;
                }
                q.what = Query::Episodes;
                q.a = it.series_id;
                q.b = it.id;
                open_list(Level::Grid, it.detail.empty() ? it.name : it.detail + ", " + it.name, q);
                break;
            case Kind::Movie:
            case Kind::Episode:
            case Kind::Video:
                if (play) open_video(items, i);
                else open_detail(items, i);
                break;
            case Kind::Track: play_tracks(items, i, false); break;
        }
    }

    // The cards that aren't things on the server.
    void open_pseudo(const Item& it) {
        const std::string& what = it.detail;
        Query q;
        if (what.compare(0, 7, "search:") == 0) {
            if (search_box_) lv_textarea_set_text(search_box_, what.substr(7).c_str());
            return;
        }
        if (what == "browse:albums") q.what = Query::Albums;
        else if (what == "browse:artists") q.what = Query::Artists;
        else if (what == "browse:playlists") q.what = Query::Playlists;
        else if (what == "browse:songs") q.what = Query::Songs;
        else if (what == "browse:favorites") q.what = Query::Favorites;
        else if (what == "browse:movies") q.what = Query::Movies;
        else if (what == "browse:shows") q.what = Query::Shows;
        else return;
        open_list(q.what == Query::Songs ? Level::Tracks : Level::Grid, it.name, q);
    }

    // Change an item wherever the app holds a copy of it.
    void update_item(const std::string& id, const std::function<void(Item&)>& change) {
        auto each = [&](std::vector<Item>& v) {
            for (Item& x : v)
                if (x.id == id) change(x);
        };
        for (auto& l : stack_) {
            each(l->items);
            each(l->siblings);
            if (l->parent.id == id) change(l->parent);
            for (Section& s : l->sections) each(s.items);
        }
        each(libs_);
        each(resume_);
        each(next_up_);
        for (auto& l : latest_) each(l.second);
        for (auto& u : by_url_)
            if (u.second.id == id) change(u.second);
    }

    // Tell the server an item is (not) a favourite, or (not) watched. The
    // screen changes at once; a failure says so and changes it back.
    void set_flag(const Item& it, bool favorite, bool on) {
        if (it.id.empty()) return;
        const std::string id = it.id;
        auto apply = [this, id, favorite](bool v) {
            update_item(id, [&](Item& x) {
                if (favorite) {
                    x.favorite = v;
                } else {
                    x.played = v;
                    if (v) x.resume_s = 0;
                }
            });
        };
        apply(on);
        shell_toast(favorite ? (on ? "Added to favourites" : "Removed from favourites")
                             : (on ? "Marked as watched" : "Marked as not watched"));
        if (!favorite && on)
            resume_.erase(std::remove_if(resume_.begin(), resume_.end(), [&](const Item& x) { return x.id == id; }),
                          resume_.end());
        if (demo()) return;
        const std::string url = jf_.server + (favorite ? favorite_path(id, jf_.user_id) : played_path(id, jf_.user_id));
        const std::string header = auth(), dir = cache_dir_ + "/tmp";
        auto status = std::make_shared<int>(0);
        const uint32_t session = session_;
        worker_.post(
            Worker::Urgent,
            [url, header, dir, on, status](std::atomic<int>& pid) {
                *status = http_send(on ? "POST" : "DELETE", url, header, dir, &pid).status;
            },
            [this, status, apply, on, session] {
                if (session != session_ || (*status >= 200 && *status < 300)) return;
                apply(!on);
                shell_toast("Not saved: " + status_error(*status));
                if (!vid_ && !input_) show();
            });
    }

    // ---- A grid: a library, an artist's albums, a show's seasons ------------

    static void grid_caption(const Level& l, const Item& it, std::string& title, std::string& detail) {
        if (it.kind == Kind::Episode && l.query.what == Query::Episodes) {
            title = it.number >= 0 ? std::to_string(it.number) + ". " + it.name : it.name;
            detail = it.resume_s > 0 ? left_text(it.duration_s - it.resume_s) : hm(it.duration_s);
            return;
        }
        title = full_name(it);
        detail = it.kind == Kind::Episode ? (it.resume_s > 0 ? left_text(it.duration_s - it.resume_s) : hm(it.duration_s))
                                          : sub_text(it);
    }

    void build_grid(Level& l) {
        grid_.set_note(l.loading ? "Loading..." : l.error.empty() ? "Nothing here" : l.error);
        const Level* lp = &l;
        grid_.build(page_, kScreenW, page_h(), shape_for(l.items), &l.items, &art_,
                    [lp](const Item& it, std::string& t, std::string& d) { grid_caption(*lp, it, t, d); });
        if (l.focus >= 0) grid_.focus(l.focus);
    }

    bool grid_key(Level& l, uint32_t key) {
        if (grid_.built() && grid_.key(key)) return true;
        switch (key) {
            case LV_KEY_ENTER:
            case '\r': {
                int i = grid_.focused();
                if (i >= 0) {
                    const std::vector<Item> items = l.items;   // opening a level may move this one
                    open_item(items, i);
                }
                return true;
            }
            case LV_KEY_BACKSPACE:
            case LV_KEY_DEL:
                back();   // nothing typed: back a step, as Esc
                return true;
            case ' ': pause(); return true;
        }
        return false;
    }

    static Query::Sort next_sort(Query::Sort s) {
        return s == Query::ByName ? Query::ByAdded : s == Query::ByAdded ? Query::ByYear
               : s == Query::ByYear ? Query::ByRandom : Query::ByName;
    }

    void cycle_sort() {
        Level* l = top();
        if (!l || !sortable(l->query.what)) return;
        l->query.sort = next_sort(l->query.sort);
        l->focus = -1;
        shell_toast(sort_name(l->query.sort));
        reload(*l);
    }

    // ---- An album or a playlist: its cover beside its tracks ----------------

    // A row: a track by its number on its album; elsewhere with its artist,
    // where the row is wide enough for both.
    static std::string row_text(const Item& it, const Level& l, bool narrow) {
        if (it.kind == Kind::Track) {
            if (l.query.what == Query::AlbumTracks) return it.number > 0 ? fmt("%d. ", it.number) + it.name : it.name;
            return narrow || it.detail.empty() ? it.name : it.detail + " - " + it.name;
        }
        return full_name(it);
    }

    // text, cut to w pixels with dots: a list row has one line.
    static std::string fit(std::string text, const lv_font_t* font, int w) {
        auto width = [&](const std::string& t) {
            lv_point_t size;
            lv_text_get_size(&size, t.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            return int(size.x);
        };
        if (width(text) <= w) return text;
        while (!text.empty() && width(text + "...") > w) {
            // One character, however many bytes it is.
            while (!text.empty() && (text.back() & 0xC0) == 0x80) text.pop_back();
            if (!text.empty()) text.pop_back();
        }
        while (!text.empty() && text.back() == ' ') text.pop_back();
        return text + "...";
    }

    void build_side(const Item& it, Shape shape, int w, int h) {
        side_shape_ = shape;
        card_create(side_, page_, 6, 4, w, h, false);
        card_bind(side_, it, "");
        card_load(side_, it, shape, art_);
        art_.flush();
    }

    void build_tracks(Level& l) {
        constexpr int kSideW = 86;   // the column the cover stands in
        const bool cover = !l.parent.id.empty();
        int x = 4;
        if (cover) {
            // The cover, and under it who and when.
            int side = std::min(68, page_h() - 34);
            build_side(l.parent, Shape::Square, side, side);
            lv_obj_set_x(side_.box, 4 + (kSideW - side) / 2);
            lv_obj_t* who = dots_label(page_, &zs_font_12, kJfText, kSideW, 1);
            lv_obj_set_style_text_align(who, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(who, 4, side + 6);
            lv_label_set_text(who, l.parent.detail.empty() ? l.parent.name.c_str() : l.parent.detail.c_str());
            int total = 0;
            for (const Item& it : l.items) total += std::max(0, it.duration_s);
            std::string facts = dot(l.parent.year > 0 ? std::to_string(l.parent.year) : "",
                                    l.items.empty() ? "" : hm(total));
            if (l.parent.favorite) facts = dot(facts, LV_SYMBOL_OK);
            lv_obj_t* f = dots_label(page_, &zs_font_12, kJfDim, kSideW, 1);
            lv_obj_set_style_text_align(f, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(f, 4, side + 19);
            lv_label_set_text(f, facts.c_str());
            x = kSideW + 10;
        }
        list_.build(page_, x, 2, kScreenW - x - 4, page_h() - 4);
        list_built_ = true;
        list_.set_empty_text(l.loading ? "Loading..." : l.error.empty() ? "Nothing here" : l.error);
        int now_row = -1;
        for (size_t i = 0; i < l.items.size(); ++i) {
            const Item& it = l.items[i];
            bool track = it.kind == Kind::Track;
            bool now = track && jf::stream_url(jf_, it.id) == listed_playing_;
            const char* icon = now ? LV_SYMBOL_VOLUME_MAX : icon_for(it);
            std::string right;
            if (it.playable_video() && it.resume_s > 0) right = left_text(it.duration_s - it.resume_s);
            else if (it.playable() && it.duration_s > 0) right = mmss(it.duration_s * 1000);
            int i_ = int(i);
            // What the row has room for, beside its number, icon and time.
            std::string text = fit(row_text(it, l, cover), &zs_font_14, kScreenW - x - 4 - 100);
            list_.add(icon, text, right, [this, i_] { activate_row(i_); });
            if (now) now_row = i_;
        }
        list_.finish();
        // Drawn again (the track changed, more rows came): what was typed
        // to narrow it is typed again.
        for (unsigned char ch : l.typed) list_.key(ch);
        if (l.focus >= 0 && l.focus < int(l.items.size())) list_.focus(l.focus);
        else if (now_row >= 0) list_.focus(now_row);
    }

    bool tracks_key(uint32_t key) {
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

    void activate_row(int i) {
        Level* l = top();
        if (!l || l->type != Level::Tracks) return;
        const std::vector<Item> items = l->items;
        l->focus = i;
        open_item(items, i);
    }

    // Space: pause or resume the music; with none, play the row you're on.
    void pause() {
        PlaybackState pb = hal().playback();
        if (!pb.path.empty()) {
            hal().set_paused(!pb.paused);
        } else if (Level* l = top(); l && l->type == Level::Tracks && list_built_) {
            activate_row(list_.focused());
        }
    }

    // Queue every track among items, starting with items[i].
    void play_tracks(const std::vector<Item>& items, int i, bool shuffle) {
        std::vector<QueueItem> q;
        std::vector<Item> tracks;
        int at = 0;
        const std::string header = auth();
        for (size_t k = 0; k < items.size(); ++k) {
            if (items[k].kind != Kind::Track) continue;
            if (int(k) == i) at = int(tracks.size());
            tracks.push_back(items[k]);
        }
        if (tracks.empty()) return;
        if (shuffle) {
            std::mt19937 rng{std::random_device{}()};
            std::shuffle(tracks.begin(), tracks.end(), rng);
            at = 0;
        }
        for (const Item& t : tracks) {
            std::string url = jf::stream_url(jf_, t.id);
            by_url_[url] = t;
            q.push_back({url, t.name, t.detail, t.duration_s, header});
        }
        queue_ = tracks;
        if (!player_play(std::move(q), at)) shell_toast("Can't play " + tracks[size_t(at)].name);
        show();
    }

    void play_all(bool shuffle) {
        Level* l = top();
        if (!l || l->type != Level::Tracks) return;
        const std::vector<Item> items = l->items;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].kind == Kind::Track) {
                play_tracks(items, int(i), shuffle);
                return;
            }
        shell_toast("No tracks here");
    }

    void toggle_favorite_parent() {
        Level* l = top();
        if (!l || l->parent.id.empty()) return;
        set_flag(l->parent, true, !l->parent.favorite);
        show();
    }

    // ---- A film or an episode: its page -------------------------------------

    void open_detail(const std::vector<Item>& items, int i) {
        const Item it = items[size_t(i)];
        Level& l = push(Level::Detail, it.kind == Kind::Episode && !it.detail.empty() ? it.detail : it.name);
        l.parent = it;
        l.siblings = items;
        l.at = i;
        // The list said the name and the year; ask for the rest.
        Ask one;
        one.path = item_path(it.id, jf_.user_id);
        one.single = true;
        one.demo = [it] {
            Page p;
            p.items.push_back(it);
            return p;
        };
        const uint32_t serial = l.serial;
        ask({one}, [this, serial](Answers& ans) {
            Level* l = find_level(serial);
            if (!l || !ans[0].ok || ans[0].page.items.empty()) return;
            const Item& full = ans[0].page.items[0];
            Item& it = l->parent;
            it.overview = full.overview;
            it.rated = full.rated;
            it.stars10 = full.stars10;
            it.played = full.played;
            it.favorite = full.favorite;
            if (full.duration_s > 0) it.duration_s = full.duration_s;
            it.resume_s = full.resume_s;
            if (!it.poster.any()) it.poster = full.poster;
            if (!it.wide.any()) it.wide = full.wide;
            if (top() == l) show();
        });
        show();
    }

    struct DetailButton {
        lv_obj_t* box;
        lv_obj_t* text;
        std::function<void()> run;
    };

    void build_detail(Level& l) {
        const Item& it = l.parent;
        const bool portrait = it.kind == Kind::Movie;
        const int aw = portrait ? 60 : 100, ah = portrait ? std::min(90, page_h() - 30) : 56;
        build_side(it, portrait ? Shape::Poster : Shape::Wide, portrait ? ah * 2 / 3 : aw, ah);
        const int x = (portrait ? ah * 2 / 3 : aw) + 14, w = kScreenW - x - 6;

        lv_obj_t* name = dots_label(page_, &zs_font_14, kJfText, w, 1);
        lv_obj_set_pos(name, x, 2);
        lv_label_set_text(name, (it.kind == Kind::Episode ? dot(it.episode_code(), it.name) : it.name).c_str());

        std::string facts = dot(it.year > 0 ? std::to_string(it.year) : "", hm(it.duration_s));
        facts = dot(facts, it.rated);
        if (it.stars10 > 0) facts = dot(facts, fmt("%d.%d / 10", it.stars10 / 10, it.stars10 % 10));
        if (it.played) facts = dot(facts, LV_SYMBOL_OK " Watched");
        if (it.favorite) facts = dot(facts, "Favourite");
        lv_obj_t* f = dots_label(page_, &zs_font_12, kJfDim, w, 1);
        lv_obj_set_pos(f, x, 19);
        lv_label_set_text(f, facts.c_str());

        const int by = page_h() - 22;
        const int line = lv_font_get_line_height(&zs_font_12);
        int lines = std::max(1, (by - 36) / line);
        lv_obj_t* about = dots_label(page_, &zs_font_12, 0xc8c8c8, w, lines);
        lv_obj_set_pos(about, x, 35);
        lv_label_set_text(about, it.overview.c_str());

        // The buttons: Enter presses the one in focus.
        detail_buttons_.clear();
        int bx = 6;
        auto button = [&](const std::string& text, std::function<void()> run) {
            lv_obj_t* box = panel(page_, bx, by, 10, 18, kJfRaised, 3);
            lv_obj_t* t = label(box, text.c_str(), &zs_font_12, kJfText);
            lv_obj_update_layout(t);
            int bw = lv_obj_get_width(t) + 14;
            lv_obj_set_width(box, bw);
            lv_obj_center(t);
            detail_buttons_.push_back({box, t, std::move(run)});
            bx += bw + 5;
        };
        if (it.resume_s > 0) {
            button(std::string(LV_SYMBOL_PLAY " Resume ") + mmss(it.resume_s * 1000), [this] { detail_play(false); });
            button("Restart", [this] { detail_play(true); });
        } else {
            button(LV_SYMBOL_PLAY " Play", [this] { detail_play(false); });
        }
        button(it.played ? "Not watched" : "Watched", [this] { detail_toggle(false); });
        button(it.favorite ? "Unfavourite" : "Favourite", [this] { detail_toggle(true); });
        l.button = std::clamp(l.button, 0, int(detail_buttons_.size()) - 1);
        focus_button(l);
    }

    void focus_button(Level& l) {
        for (size_t i = 0; i < detail_buttons_.size(); ++i)
            lv_obj_set_style_bg_color(detail_buttons_[i].box, c(int(i) == l.button ? kJfBlue : kJfRaised), 0);
    }

    bool detail_key(Level& l, uint32_t key) {
        switch (key) {
            case LV_KEY_LEFT:
                l.button = std::max(0, l.button - 1);
                focus_button(l);
                return true;
            case LV_KEY_RIGHT:
                l.button = std::min(int(detail_buttons_.size()) - 1, l.button + 1);
                focus_button(l);
                return true;
            case LV_KEY_UP:
            case LV_KEY_DOWN: return true;
            case LV_KEY_ENTER:
            case '\r':
                if (l.button >= 0 && l.button < int(detail_buttons_.size())) {
                    auto run = detail_buttons_[size_t(l.button)].run;   // it may redraw the page
                    run();
                }
                return true;
            case LV_KEY_BACKSPACE:
            case LV_KEY_DEL: back(); return true;
            case ' ': detail_play(false); return true;
            case 'r': case 'R': detail_play(true); return true;
            case 'w': case 'W': detail_toggle(false); return true;
            case 'v': case 'V': detail_toggle(true); return true;
            case 'f': case 'F': open_search(); return true;
        }
        return false;
    }

    void detail_play(bool from_start) {
        Level* l = top();
        if (!l || l->type != Level::Detail) return;
        std::vector<Item> items = l->siblings;
        int at = l->at;
        if (at < 0 || at >= int(items.size())) {
            items = {l->parent};
            at = 0;
        }
        items[size_t(at)] = l->parent;
        if (from_start) items[size_t(at)].resume_s = 0;
        open_video(items, at);
    }

    void detail_toggle(bool favorite) {
        Level* l = top();
        if (!l || l->type != Level::Detail) return;
        set_flag(l->parent, favorite, favorite ? !l->parent.favorite : !l->parent.played);
        show();
    }

    // ---- Search -------------------------------------------------------------

    void open_search() {
        if (Level* l = top(); l && l->type == Level::Search) return;
        Level& l = push(Level::Search, "Search");
        idle_search(l);
        show();
        if (suggestions_.empty() && !suggestions_asked_) {
            suggestions_asked_ = true;
            Query q;
            q.what = Query::Suggestions;
            ask({ask_for(q)}, [this](Answers& ans) {
                if (!ans[0].ok) return;
                suggestions_ = ans[0].page.items;
                Level* l = top();
                if (l && l->type == Level::Search && l->searched.empty() && clean_search(l->text).empty()) {
                    idle_search(*l);
                    fill_search(*l);
                }
            }, false);
        }
    }

    std::vector<std::string> recent_searches() const {
        std::vector<std::string> v;
        std::string all = prefs::get_str("jellyfin.searches", ""), one;
        for (char ch : all + "|") {
            if (ch != '|') {
                one += ch;
                continue;
            }
            if (!one.empty()) v.push_back(one);
            one.clear();
        }
        return v;
    }

    void remember_search(const std::string& text) {
        std::string term = clean_search(text);
        for (char& ch : term)
            if (ch == '|' || ch == '=') ch = ' ';
        if (term.size() < 2) return;
        std::vector<std::string> v = recent_searches();
        v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string& s) { return lower(s) == lower(term); }),
                v.end());
        v.insert(v.begin(), term);
        if (int(v.size()) > kRecentSearches) v.resize(size_t(kRecentSearches));
        std::string all;
        for (const std::string& s : v) all += (all.empty() ? "" : "|") + s;
        prefs::set_str("jellyfin.searches", all);
    }

    // The page with nothing typed: what you searched for before, and
    // something to watch or hear.
    void idle_search(Level& l) {
        l.sections.clear();
        l.searched.clear();
        l.note = "Type to search your server";
        Section recent{"recent", "Recent searches", Shape::Wide, {}, false};
        for (const std::string& s : recent_searches()) recent.items.push_back(pseudo(s, "search:" + s));
        if (!recent.items.empty()) l.sections.push_back(recent);
        if (!suggestions_.empty())
            l.sections.push_back({"suggest", "Suggestions", shape_for(suggestions_), suggestions_, false});
    }

    static Shape group_shape(const std::string& title) {
        if (title == "Movies" || title == "Shows" || title == "Collections") return Shape::Poster;
        if (title == "Episodes" || title == "Videos") return Shape::Wide;
        return Shape::Square;
    }

    // Ask the server for what is in the box: once per kind of thing, all in
    // one go. A search that finds nothing for several words tries the
    // longest of them.
    void run_search(Level& l, const std::string& instead = "") {
        const std::string term = clean_search(instead.empty() ? l.text : instead);
        const std::string typed = clean_search(l.text);
        if (instead.empty() && term == l.searched) return;
        if (term.empty()) {
            idle_search(l);
            fill_search(l);
            return;
        }
        std::vector<SearchGroup> groups = search_paths(term, jf_.user_id);
        std::vector<Ask> asks;
        for (const SearchGroup& g : groups) {
            Ask a;
            a.path = g.path;
            const std::string title = g.title;
            a.demo = [this, title, term] { return demo_search(title, term); };
            asks.push_back(std::move(a));
        }
        const uint32_t serial = l.serial, search = l.search_serial = ++search_serial_;
        const bool second = !instead.empty();
        ask(std::move(asks), [this, serial, search, groups, term, typed, second](Answers& ans) {
            Level* l = find_level(serial);
            if (!l || l->search_serial != search) return;   // more was typed since
            std::vector<Section> found;
            int failed = 0;
            const Answer* bad = nullptr;
            for (size_t i = 0; i < ans.size(); ++i) {
                if (!ans[i].ok) {
                    ++failed;
                    bad = &ans[i];
                    continue;
                }
                if (ans[i].page.items.empty()) continue;
                found.push_back({std::string("found:") + groups[i].title, groups[i].title,
                                 group_shape(groups[i].title), ans[i].page.items, false});
            }
            if (found.empty() && failed == int(ans.size()) && bad) {
                l->sections.clear();
                l->searched = "\x01";
                l->note = error_text(*bad);
                if (top() == l) fill_search(*l);
                return;
            }
            if (found.empty() && !second) {
                // "sintel 2010": the film is there, the year isn't in its name.
                std::string longest, word;
                int words = 0;
                for (char ch : typed + " ") {
                    if (ch != ' ') {
                        word += ch;
                        continue;
                    }
                    if (!word.empty()) ++words;
                    if (word.size() > longest.size()) longest = word;
                    word.clear();
                }
                if (words > 1 && longest.size() >= 3) {
                    run_search(*l, longest);
                    return;
                }
            }
            l->sections = std::move(found);
            l->searched = typed;
            if (l->sections.empty()) l->note = "Nothing found for \"" + typed + "\"";
            else if (second) l->sections[0].title += "  (for \"" + term + "\")";
            l->token.clear();
            if (top() == l) fill_search(*l);
        });
    }

    void build_search(Level& l) {
        // The box, where the header's title would be.
        lv_obj_t* ta = lv_textarea_create(header_);
        search_box_ = ta;
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_max_length(ta, 60);
        lv_textarea_set_placeholder_text(ta, "Search");
        lv_obj_set_pos(ta, 22, 1);
        lv_obj_set_size(ta, kScreenW - 22 - 26, 18);
        lv_obj_set_style_bg_color(ta, c(0x1c1c1c), 0);
        lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(ta, c(kJfText), 0);
        lv_obj_set_style_text_font(ta, &zs_font_14, 0);
        lv_obj_set_style_border_color(ta, c(kJfBlue), 0);
        lv_obj_set_style_border_width(ta, 1, 0);
        lv_obj_set_style_radius(ta, 3, 0);
        lv_obj_set_style_pad_ver(ta, 0, 0);
        lv_obj_set_style_pad_hor(ta, 4, 0);
        lv_obj_set_style_bg_color(ta, c(kJfBlue), LV_PART_CURSOR | LV_STATE_FOCUSED);
        // The blue edge is the focus mark here: not the theme's frame as well
        // (the header would cut it off).
        for (lv_state_t st : {lv_state_t(LV_STATE_FOCUSED), lv_state_t(LV_STATE_FOCUS_KEY), lv_state_t(LV_STATE_EDITED),
                              lv_state_t(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY),
                              lv_state_t(LV_STATE_FOCUSED | LV_STATE_EDITED),
                              lv_state_t(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY | LV_STATE_EDITED)})
            lv_obj_set_style_outline_width(ta, 0, st);
        // Titles aren't dictionary words: no word suggestions here (ui/suggest_bar.h).
        lv_obj_add_flag(ta, LV_OBJ_FLAG_USER_4);
        lv_textarea_set_text(ta, l.text.c_str());
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            static_cast<JellyfinApp*>(lv_event_get_user_data(e))->search_typed();
        }, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<JellyfinApp*>(lv_event_get_user_data(e));
            // Enter with no results yet: don't wait for the pause in typing.
            self->search_due_ = false;
            if (Level* l = self->top(); l && l->type == Level::Search) self->run_search(*l);
        }, LV_EVENT_READY, this);
        if (group) {
            lv_group_add_obj(group, ta);
            lv_group_focus_obj(ta);
            lv_group_set_editing(group, true);
        }
        fill_search(l);
    }

    // The results under the box (the box stays as it is).
    void fill_search(Level& l) {
        if (!page_) return;
        if (rows_.built()) l.token = rows_.focus_token();
        rows_.reset();
        lv_obj_clean(page_);
        if (l.sections.empty()) {
            note(l.note);
            return;
        }
        rows_.build(page_, kScreenW, page_h(), l.sections, &art_, row_caption);
        rows_.focus_by_token(l.token);
    }

    void search_typed() {
        Level* l = top();
        if (!l || l->type != Level::Search || !search_box_) return;
        std::string text = lv_textarea_get_text(search_box_);
        if (text == l->text) return;
        l->text = text;
        search_due_ = true;
        search_due_at_ = lv_tick_get();
    }

    bool search_key(Level& l, uint32_t key) {
        switch (key) {
            case LV_KEY_UP:
            case LV_KEY_DOWN:
                if (rows_.built()) rows_.key(key);
                return true;   // never out of the box
            case LV_KEY_LEFT:
            case LV_KEY_RIGHT:
                // Through the results when there are some; else in the text.
                return rows_.built() && rows_.key(key);
            case LV_KEY_ENTER:
            case '\r': {
                if (!rows_.built() || !rows_.item()) return false;   // the box: search now
                // Typed since these results came: they are for something else.
                if (clean_search(l.text) != l.searched && !clean_search(l.text).empty()) return false;
                const std::vector<Item> items = rows_.section()->items;
                int col = rows_.col();
                if (!items[size_t(col)].id.empty()) remember_search(l.text);
                open_item(items, col);
                return true;
            }
            case LV_KEY_BACKSPACE:
            case LV_KEY_DEL:
                if (search_box_ && lv_textarea_get_text(search_box_)[0] == '\0') {
                    back();
                    return true;
                }
                return false;
        }
        return false;   // typing goes to the box
    }

    // ---- Now playing --------------------------------------------------------

    struct PlayingView {
        lv_obj_t *title = nullptr, *artist = nullptr, *album = nullptr, *bar = nullptr, *time = nullptr,
                 *state = nullptr, *next = nullptr;
        std::string path;   // the track on show
        Item item;
    };

    void open_playing() {
        if (hal().playback().path.empty()) {
            shell_toast("Nothing is playing");
            return;
        }
        if (Level* l = top(); l && l->type == Level::Playing) return;
        push(Level::Playing, "Now playing");
        show();
    }

    void build_playing() {
        const int side = 96, x = side + 18, w = kScreenW - x - 8;
        play_ = PlayingView{};
        play_.title = dots_label(page_, &zs_font_16, kJfText, w, 2);
        lv_obj_set_pos(play_.title, x, 6);
        play_.artist = dots_label(page_, &zs_font_14, kJfDim, w, 1);
        lv_obj_set_pos(play_.artist, x, 46);
        play_.album = dots_label(page_, &zs_font_12, kJfDim, w, 1);
        lv_obj_set_pos(play_.album, x, 63);
        play_.bar = thin_bar(page_, w, 3, kJfBlue);
        lv_obj_set_style_bg_color(play_.bar, c(kJfRaised), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(play_.bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_bar_set_range(play_.bar, 0, 1000);
        lv_obj_set_pos(play_.bar, x, 82);
        play_.state = label(page_, LV_SYMBOL_PLAY, &zs_font_14, kJfBlue);
        lv_obj_set_pos(play_.state, x, 90);
        play_.time = label(page_, "", &zs_font_12, kJfDim);
        lv_obj_set_pos(play_.time, x + 18, 91);
        play_.next = dots_label(page_, &zs_font_12, kJfDim, kScreenW - 16, 1);
        lv_obj_set_pos(play_.next, 8, 0);
        vis(play_.next, false);
        update_playing(false);
    }

    // The track changed, or its details came in: draw its cover and names.
    // leave: go back when the music has stopped (not while the page is being drawn).
    void update_playing(bool leave = true) {
        if (!play_.title) return;
        PlaybackState pb = hal().playback();
        const QueueItem* q = player_current();
        if (pb.path.empty() || !q) {
            if (leave) back();
            return;
        }
        const Item* known = now_item();
        const bool fresh = pb.path != play_.path || (known && play_.item.id.empty());
        if (fresh) {
            play_.path = pb.path;
            Item it;
            if (known) it = *known;
            it.kind = Kind::Track;
            if (it.name.empty()) it.name = q->title;
            if (it.detail.empty()) it.detail = q->artist;
            play_.item = it;
            lv_label_set_text(play_.title, it.name.c_str());
            lv_label_set_text(play_.artist, it.detail.c_str());
            lv_label_set_text(play_.album, it.album.c_str());
            if (side_.box) lv_obj_delete(side_.box);
            side_ = CardView{};
            side_shape_ = Shape::Square;
            card_create(side_, page_, 8, 6, 96, 96, false);
            card_bind(side_, it, "");
            card_load(side_, it, Shape::Square, art_);
            art_.flush();
        }
        lv_label_set_text(play_.state, pb.paused ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        lv_label_set_text(play_.time, (mmss(pb.pos_ms) + " / " + mmss(pb.dur_ms)).c_str());
        lv_bar_set_value(play_.bar, pb.dur_ms > 0 ? int(1000LL * pb.pos_ms / pb.dur_ms) : 0, LV_ANIM_OFF);
    }

    bool playing_key(uint32_t key) {
        auto& h = hal();
        switch (key) {
            case ' ':
            case LV_KEY_ENTER:
            case '\r': pause(); return true;
            case LV_KEY_RIGHT: case 'n': case 'N': player_step(+1); return true;
            case LV_KEY_LEFT: case 'p': case 'P': player_step(-1); return true;
            case LV_KEY_UP: case '+': case '=':
                h.set_volume(std::min(100, h.volume() + 5));
                return true;
            case LV_KEY_DOWN: case '-': case '_':
                h.set_volume(std::max(0, h.volume() - 5));
                return true;
            case LV_KEY_BACKSPACE:
            case LV_KEY_DEL: back(); return true;
            case 'f': case 'F': open_search(); return true;
        }
        return false;
    }

    void open_playing_album() {
        const Item it = play_.item;
        if (it.album_id.empty()) {
            shell_toast("The server didn't say which album");
            return;
        }
        Item album;
        album.kind = Kind::Album;
        album.id = it.album_id;
        album.name = it.album.empty() ? "Album" : it.album;
        album.detail = it.detail;
        album.poster = it.poster;
        Query q;
        q.what = Query::AlbumTracks;
        q.a = album.id;
        open_list(Level::Tracks, album.name, q, album);
    }

    // ---- Signing in and out -------------------------------------------------

    void forget_session() {
        for (const char* k : {"jellyfin.user_id", "jellyfin.token"}) prefs::set_str(k, "");
        const bool was_demo = demo();
        jf_.user_id.clear();
        jf_.token.clear();
        ++session_;
        worker_.stop();
        art_.stop();
        stack_.clear();
        libs_.clear();
        resume_.clear();
        next_up_.clear();
        latest_.clear();
        suggestions_.clear();
        suggestions_asked_ = false;
        by_url_.clear();
        queue_.clear();
        home_token_.clear();
        home_loaded_ = home_loading_ = false;
        // The covers say what is on the server: they go with the sign-in.
        if (!was_demo && !cache_dir_.empty()) empty_dir(cache_dir_ + "/art");
        set_actions();
        show();
    }

    void sign_out() {
        // Music from this server is played with the sign-in being forgotten.
        if (!jf_.server.empty() && hal().playback().path.compare(0, jf_.server.size(), jf_.server) == 0) player_stop();
        forget_session();
        prefs::set_str("jellyfin.searches", "");
        shell_toast("Signed out of Jellyfin");
    }

    // Jellyfin's own three questions, then sign in. Only the token is kept,
    // not the password.
    void sign_in() {
        prompt("Server address (name or address)", jf_.server, false, [this](const std::string& server) {
            if (server.empty()) return;
            typed_server_ = server;
            prompt("User", jf_.user, false, [this](const std::string& user) {
                if (user.empty()) return;
                typed_user_ = user;
                // An address typed with http:// sends the password as it is.
                bool plain = typed_server_.find("http://") != std::string::npos;
                prompt(plain ? "Password (sent unencrypted: http)" : "Password", "", true,
                       [this](const std::string& password) {
                    shell_toast("Signing in...");
                    lv_refr_now(nullptr);
                    JfSession s;
                    std::string error;
                    if (!hal().jellyfin_login(typed_server_, typed_user_, password, jf_.device_id, s, error)) {
                        shell_toast(error);
                        return;
                    }
                    if (!safe_token(s.token)) {
                        shell_toast("That address isn't a Jellyfin server");
                        return;
                    }
                    jf_ = s;
                    prefs::set_str("jellyfin.server", s.server);
                    prefs::set_str("jellyfin.user", s.user);
                    prefs::set_str("jellyfin.user_id", s.user_id);
                    prefs::set_str("jellyfin.token", s.token);
                    ++session_;
                    stack_.clear();
                    home_loaded_ = home_loading_ = false;
                    // No https: say so, so it isn't used on a network nobody trusts.
                    shell_toast("Signed in as " + s.user +
                                (s.server.rfind("https://", 0) == 0 ? std::string() : std::string(" - not encrypted (http)")));
                    start_session();
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
    void prompt(const char* title, const std::string& initial, bool password, Done done) {
        done_ = std::move(done);
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
        // A server's name and a user's aren't dictionary words.
        lv_obj_add_flag(ta, LV_OBJ_FLAG_USER_4);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<JellyfinApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            // Enter is still down: without this its release would also
            // "click" whatever gets the focus next.
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
        lv_group_remove_obj(lv_obj_get_child(in, 1));
        lv_obj_delete_async(in);
        set_actions();
    }

    // ---- Video ----------------------------------------------------------

    static std::string new_session_id() {
        std::random_device rd;
        return fmt("%08x%08x%08x%08x", unsigned(rd()), unsigned(rd()), unsigned(rd()), unsigned(rd()));
    }

    // Play items[i] (a movie or episode) on the screen. The rest of the list
    // stays with it, so an episode can go on to the next one.
    void open_video(const std::vector<Item>& items, int i) {
        const Item it = items[size_t(i)];
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
        if (!hal().video_open(url, auth(), start, kVidW, kVidH, error)) {
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
        vid_title_ = dots_label(vid_osd_, &zs_font_12, kJfText, kVidW - 12, 1);
        lv_obj_set_pos(vid_title_, 6, 4);
        vid_bar_ = thin_bar(vid_osd_, kVidW - 12, 3, kJfBlue);
        lv_obj_set_style_bg_color(vid_bar_, c(0x555555), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(vid_bar_, LV_OPA_COVER, LV_PART_MAIN);
        lv_bar_set_range(vid_bar_, 0, 1000);
        lv_obj_set_pos(vid_bar_, 6, 21);
        vid_time_ = label(vid_osd_, "", &zs_font_12, kJfDim);
        lv_obj_set_pos(vid_time_, 6, 26);
        lv_label_set_text(vid_title_, full_name(vid_item_).c_str());

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
        vis(vid_wait_, *wait != 0);

        bool osd = st.paused || st.loading || now - vid_osd_at_ < kOsdMs;
        vis(vid_osd_, osd);
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
        const std::vector<Item> items = vid_items_;
        const Item was = vid_item_;
        const int next = vid_at_ + 1;
        close_video(true);
        if (!error.empty()) {
            shell_toast(error);
            return;
        }
        if (!finished) return;
        // An episode: on to the next one in the list. (Only within the
        // season: a list like Continue Watching mixes shows.)
        if (was.kind == Kind::Episode && next < int(items.size()) && items[size_t(next)].kind == Kind::Episode &&
            items[size_t(next)].series_id == was.series_id && items[size_t(next)].season == was.season &&
            items[size_t(next)].number > was.number) {
            std::vector<Item> rest = items;
            rest[size_t(next)].resume_s = 0;
            shell_toast("Up next: " + rest[size_t(next)].name);
            // The page under the video is the next episode's now.
            if (Level* l = top(); l && l->type == Level::Detail && l->parent.id == was.id) {
                l->parent = rest[size_t(next)];
                l->at = next;
                l->title = l->parent.detail.empty() ? l->parent.name : l->parent.detail;
            }
            open_video(rest, next);
        } else {
            shell_toast("Finished " + was.name);
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
        Item it = vid_item_;
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
            // Every list may have a stale resume point; show the place we stopped.
            const bool finished = st.finished || (vid_item_.duration_s > 0 && vid_pos_s_ >= vid_item_.duration_s);
            const int at = finished ? 0 : vid_pos_s_;
            const std::string id = vid_item_.id;
            update_item(id, [&](Item& x) {
                x.resume_s = at;
                if (finished) x.played = true;
            });
            // Continue Watching: what you just stopped goes first; what you
            // finished goes.
            resume_.erase(std::remove_if(resume_.begin(), resume_.end(), [&](const Item& x) { return x.id == id; }),
                          resume_.end());
            if (at > 0) {
                Item x = vid_item_;
                x.resume_s = at;
                resume_.insert(resume_.begin(), x);
            }
            // The server works out Next Up; ask once it has heard where we stopped.
            watching_stale_ = true;
            watching_stale_at_ = lv_tick_get();
            set_actions();
            show();
        }
        vid_ = nullptr;
        vid_pixels_.clear();
        vid_pixels_.shrink_to_fit();
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t *header_ = nullptr, *logo_ = nullptr, *word_ = nullptr, *back_ = nullptr, *title_ = nullptr;
    lv_obj_t *avatar_ = nullptr, *initial_ = nullptr, *glass_ = nullptr, *busy_ = nullptr;
    lv_obj_t *now_ = nullptr, *now_bar_ = nullptr, *now_state_ = nullptr, *now_title_ = nullptr,
             *now_time_ = nullptr, *now_art_box_ = nullptr, *now_img_ = nullptr;
    ArtPtr now_art_;
    bool now_shown_ = false;
    std::string now_text_;
    lv_obj_t* page_ = nullptr;         // home, a level, or the welcome
    lv_obj_t* search_box_ = nullptr;   // the Search level's text box, in the header
    lv_timer_t* poll_timer_ = nullptr;

    // What the page on show is made of.
    RowsView rows_;                    // Home, Search
    GridView grid_;
    ListView list_;                    // Tracks
    bool list_built_ = false;
    CardView side_;                    // the cover beside a track list, on a film's page, in the player
    Shape side_shape_ = Shape::Square;
    std::vector<DetailButton> detail_buttons_;
    PlayingView play_;
    uint32_t shown_serial_ = 0;        // the level the page shows, 0 for Home
    std::string listed_playing_;

    // Home
    std::vector<Item> libs_;           // the user's libraries
    std::vector<Item> resume_;         // Continue Watching
    std::vector<Item> next_up_;
    std::vector<std::pair<Item, std::vector<Item>>> latest_;   // Recently Added, by library
    bool home_loaded_ = false, home_loading_ = false;
    bool watching_stale_ = false;      // something was watched: the server knows what is next
    uint32_t watching_stale_at_ = 0;
    std::string home_error_, home_token_;

    // Search
    std::vector<Item> suggestions_;
    bool suggestions_asked_ = false;
    bool search_due_ = false;
    uint32_t search_due_at_ = 0, search_serial_ = 0;

    JfSession jf_;
    uint32_t session_ = 0;             // changes at each sign-in and sign-out
    std::string cache_dir_;
    Worker worker_;
    ArtStore art_;
    uint32_t art_serial_ = 0;
    std::vector<std::unique_ptr<Level>> stack_;
    uint32_t serial_ = 0;
    lv_obj_t* input_ = nullptr;        // the sign-in box
    Done done_;
    std::string typed_server_, typed_user_;

    // Music the app queued: what the server said about each track.
    std::unordered_map<std::string, Item> by_url_;
    std::vector<Item> queue_;
    std::string asked_track_;

    // Video
    lv_obj_t* vid_ = nullptr;   // the player view, over everything; null when no video
    lv_obj_t *vid_img_ = nullptr, *vid_wait_ = nullptr, *vid_osd_ = nullptr, *vid_title_ = nullptr,
             *vid_bar_ = nullptr, *vid_time_ = nullptr;
    lv_timer_t* vid_timer_ = nullptr;
    lv_image_dsc_t vid_dsc_{};
    std::vector<uint16_t> vid_pixels_;
    std::vector<Item> vid_items_;   // the list it was started from
    int vid_at_ = -1;
    Item vid_item_;
    std::string vid_session_;
    uint32_t vid_reported_at_ = 0, vid_osd_at_ = 0;
    int vid_pos_s_ = 0;
    bool vid_started_ = false;
};
}  // namespace

std::unique_ptr<App> make_jellyfin_app() { return std::make_unique<JellyfinApp>(); }

}  // namespace zs
