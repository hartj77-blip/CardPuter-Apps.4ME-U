// Games: play your own games for classic consoles on the built-in screen, with
// libretro emulator cores (the ones RetroArch uses). See docs/GAMES.md.
//
//   Library   a List: Enter a system; menu C Cores
//   Games     a List: Enter play; menu C Cores, D Delete save
//   Cores     a List: Enter download (from libretro); menu D Remove (Del too)
//   Playing   a Game screen: the game has the keys, and no soft-key bar.
//             W A S D / arrows, K = A, J = B, I = X, U = Y, Q/E = L/R,
//             Enter = Start, Space = Select, hold Backspace = fast forward,
//             Esc = menu (save, load, players, TV, border, picture, quit)
//   Gamepads  up to four players, in the order they connect; Home = menu,
//             and the d-pad, A (choose) and B (back) work the menu
//   TV        with a monitor on the HDMI port, the game plays on it (tv.h)
//
// Settings (border, smooth picture, carry on where you left off, TV) are kept
// in ~/Games/settings. Quitting a game keeps its moment in
// ~/Games/states/<game>.resume.state, and the next start carries on from it.
//
// Games go in ~/Games/<system>/ (nes, gb, gbc, gba, snes, md...): copy them
// with Files, Sync, Taildrop or scp. Only use games you own.
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/games/border.h"
#include "apps/games/emu.h"
#include "apps/games/systems.h"
#include "apps/kit/run.h"
#include "apps/kit/ui.h"
#include "hal/hal.h"
#include "ui/list_view.h"
#include "ui/power.h"

namespace zs {
namespace {
using namespace theme;
using namespace games;

// While Games is open it looks after the HDMI port itself: a monitor plugged
// in then is for the game, so ZeroShell mustn't open Dock over it. ZeroShell
// (newer than 0.5.1) reads this file and leaves HDMI alone while it names its
// own process; apps run inside ZeroShell, so that's our pid. A file left by a
// crash names a process that's gone and is ignored.
std::string hdmi_claim_file() {
    const char* run = std::getenv("XDG_RUNTIME_DIR");
    return std::string(run && *run ? run : "/tmp") + "/zeroos-hdmi-app";
}

class GamesApp : public App {
public:
    ~GamesApp() override {
        stop_game();
        if (!kit::simulated()) std::remove(hdmi_claim_file().c_str());
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        if (!kit::simulated()) kit::write_file(hdmi_claim_file(), std::to_string(getpid()) + " games\n");
        load_settings();
        kit::make_dirs(games_dir());
        kit::make_dirs(borders_dir());
        for (auto& s : systems())
            if (!s.hidden) kit::make_dirs(games_dir() + "/" + s.folder);
        show_library();
    }

    void tick() override {
        if (pending_load_) {   // "Starting..." is on screen: now load
            pending_load_ = false;
            start_game();
            return;
        }
        if (page_ == Page::Playing) {
            if (emu_.quit_requested()) { stop_game(); return; }
            // Face down, or the screen went off: pause, so nothing runs unseen
            // (on a TV the game is still seen).
            if (!menu_ && power().screen_off() && !emu_.on_tv()) open_menu();
            if (emu_.pad().take_menu()) {
                if (menu_) close_menu();
                else open_menu();
            }
            if (int p = emu_.pad().take_joined()) hint(fmt("Gamepad connected: player %d", p));
            watch_tv();
        }
        if (page_ == Page::Cores && dl_.started()) {
            std::vector<std::string> lines;
            dl_.drain(lines);
            if (!dl_.running()) {
                bool ok = dl_.status() == kit::Done::Ok;
                std::string name = dl_name_;
                dl_.reset();
                if (kit::simulated() && ok) sim_downloaded_.push_back(name);
                shell_toast(ok ? name + " is ready" : "Download failed: online? (needs curl and unzip)");
                show_cores();
            }
        }
    }

    bool on_key(uint32_t key) override {
        if (page_ == Page::Playing) {
            if (key == LV_KEY_ESC) {
                if (menu_) close_menu();
                else open_menu();
                return true;
            }
            if (menu_) return false;   // the menu list takes Up/Down/Enter
            if (!emu_.pad().has_devices()) emu_.pad().press_key(key);
            return true;               // everything else is the game's
        }
        // Del on a core removes it (asking first), once no filter is typed.
        if (page_ == Page::Cores && (key == LV_KEY_DEL || key == LV_KEY_BACKSPACE) && list_.filter().empty()) {
            ask_remove_core();
            return true;
        }
        bool used = list_.key(key);
        if (page_ == Page::Games || page_ == Page::Cores) set_actions();   // for the row now in focus
        return used;
    }

    bool on_back() override {
        switch (page_) {
            case Page::Library: return false;
            case Page::Games: show_library(); return true;
            case Page::Cores: dl_.reset(); show_library(); return true;
            case Page::Playing: return true;   // handled in on_key
        }
        return false;
    }

private:
    enum class Page { Library, Games, Cores, Playing };

    // ---- Settings: ~/Games/settings, "key=value" lines
    void load_settings() {
        std::string text = kit::read_file(games_dir() + "/settings", 4096);
        size_t at = 0;
        while (at < text.size()) {
            size_t nl = text.find('\n', at);
            std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
            at = nl == std::string::npos ? text.size() : nl + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            if (k == "border") border_ = border_from_name(v);
            else if (k == "smooth") smooth_ = v == "1";
            else if (k == "resume") resume_ = v != "0";
            else if (k == "tv") tv_wanted_ = v != "0";
        }
    }

    void save_settings() {
        std::string b = border_name(border_);
        for (auto& ch : b) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        kit::write_file(games_dir() + "/settings", "border=" + b + "\nsmooth=" + (smooth_ ? "1" : "0") +
                                                        "\nresume=" + (resume_ ? "1" : "0") +
                                                        "\ntv=" + (tv_wanted_ ? "1" : "0") + "\n");
    }

    void clear() {
        lv_obj_clean(root_);
        if (group) lv_group_remove_all_objs(group);
        rows_.clear();
        list_.reset();
    }

    // The soft-key bar and the Tab menu for the page (and the row in focus:
    // what Delete save and Remove name).
    void set_actions() {
        std::vector<Action> a;
        int i = list_.focused();
        std::string row = i >= 0 && size_t(i) < rows_.size() ? rows_[size_t(i)] : "";
        if (page_ == Page::Library) {
            if (!rows_.empty()) a.push_back({"Open", 0, "Enter", nullptr});
            a.push_back({"Cores", 'c', nullptr, [this] { show_cores(); }});
        } else if (page_ == Page::Games) {
            a.push_back({"Play", 0, "Enter", nullptr});
            a.push_back({"Cores", 'c', nullptr, [this] { show_cores(); }});
            Action del{"Delete save", 'd', nullptr, [this, row] { delete_saves(row); }};
            del.enabled = !save_files(row).empty();
            del.danger = true;
            del.confirm = "Delete the saves for " + game_title(row) + "?";
            a.push_back(del);
        } else if (page_ == Page::Cores) {
            const CoreDef* def = core_def(row);
            a.push_back({"Get", 0, "Enter", nullptr});
            Action rm{"Remove", 'd', nullptr, [this, row] { remove_core(row); }};
            rm.enabled = downloaded(row);
            rm.danger = true;
            rm.confirm = std::string("Remove ") + (def ? def->name : row.c_str()) + "?";
            a.push_back(rm);
        }
        shell_set_actions(std::move(a));
    }

    // ---- Library: the systems that have games
    void show_library() {
        page_ = Page::Library;
        clear();
        std::vector<const System*> have;
        for (auto& s : systems())
            if (!games_for(s).empty()) have.push_back(&s);
        for (const System* s : have) rows_.push_back(s->id);
        set_actions();
        kit::title_row(root_, "Games");
        if (have.empty()) {
            lv_obj_t* l = label(root_,
                                "No games yet.\n\n"
                                "Copy your games into ~/Games/<system>/ with Files,\n"
                                "Sync, Taildrop or scp: nes, gb, gbc, gba, snes,\n"
                                "md, sms, pce, a26, psx, ws.\n\n"
                                "Cores, in the menu, gets emulators for more systems.",
                                &zs_font_12, kMuted);
            lv_obj_set_pos(l, 10, 26);
            return;
        }
        list_.build(root_, 0, 18, kScreenW, kBodyH - 18);
        for (const System* s : have) {
            size_t n = games_for(*s).size();
            bool core = !core_for(*s).empty();
            std::string games = fmt("%d game%s", int(n), n == 1 ? "" : "s");
            int r = list_.add(LV_SYMBOL_PLAY, s->name, core ? games : games + ", needs a core",
                              [this, s] { show_games(*s); });
            if (!core) list_.set_value_color(r, kWarn);
        }
        list_.finish();
    }

    // ---- The games for one system
    void show_games(const System& s) {
        page_ = Page::Games;
        sys_ = &s;
        clear();
        std::string file;
        std::string core = core_for(s, &file);
        const CoreDef* def = core_def(file);
        kit::title_row(root_, s.name, core.empty() ? "no core yet" : (def ? def->name : file.c_str()));
        list_.build(root_, 0, 18, kScreenW, kBodyH - 18);
        int focus = 0;
        for (auto& g : games_for(s)) {
            int i = int(rows_.size());
            if (g == game_) focus = i;   // back from playing it
            list_.add(LV_SYMBOL_PLAY, game_title(g), "", [this, i] { picked(i); });
            rows_.push_back(g);
        }
        list_.finish();
        list_.focus(focus);
        set_actions();
    }

    // A game's battery save and save states (emu.cpp names them so).
    static std::vector<std::string> save_files(const std::string& game) {
        std::vector<std::string> have;
        if (game.empty()) return have;
        std::string t = game_title(game);
        for (const std::string& f : {saves_dir() + "/" + t + ".srm", states_dir() + "/" + t + ".state",
                                     states_dir() + "/" + t + kResume})
            if (kit::exists(f)) have.push_back(f);
        return have;
    }

    void delete_saves(const std::string& game) {
        auto files = save_files(game);
        if (files.empty()) {
            shell_toast("No saves for that game");
            return;
        }
        for (auto& f : files) std::remove(f.c_str());
        shell_toast("Saves deleted");
        set_actions();
    }

    // ---- Cores: what's installed, download more
    bool installed(const CoreDef& c) {
        if (kit::simulated())
            for (auto& n : sim_downloaded_)
                if (n == c.name) return true;
        return !find_core(c.file).empty();
    }

    bool downloaded(const std::string& file) {
        if (file.empty()) return false;
        if (kit::simulated())
            for (auto& n : sim_downloaded_)
                if (const CoreDef* d = core_def(file); d && n == d->name) return true;
        return kit::exists(user_cores_dir() + "/" + file + "_libretro.so");
    }

    void show_cores() {
        int keep = page_ == Page::Cores ? list_.focused() : -1;
        page_ = Page::Cores;
        clear();
        kit::title_row(root_, "Emulator cores", dl_.started() ? "downloading..." : "");
        list_.build(root_, 0, 18, kScreenW, kBodyH - 18);
        for (auto& core : cores()) {
            if (std::string(core.file) == "zstest") continue;
            std::string for_what;
            for (auto& s : systems())
                for (const char* f : s.cores)
                    if (std::string(f) == core.file && !s.hidden)
                        for_what += (for_what.empty() ? "" : ", ") + std::string(s.id);
            bool in = installed(core);
            std::string state = in ? "installed"
                                : dl_.started() && dl_name_ == core.name ? "downloading..."
                                : core.download ? fmt("%.1f MB", core.size_kb / 1024.0)
                                                : std::string("apt: ") + core.package;
            std::string text = std::string(core.name) + " (" + for_what + ")";
            int i = int(rows_.size());
            int r = list_.add(in ? LV_SYMBOL_OK : LV_SYMBOL_DOWNLOAD, text, state, [this, i] { picked(i); });
            if (in) list_.set_value_color(r, kGood);
            rows_.push_back(core.file);
        }
        list_.finish();
        if (keep >= 0) list_.focus(keep);
        set_actions();
    }

    void download(const CoreDef& c) {
        if (dl_.started()) {
            shell_toast("One download at a time");
            return;
        }
        if (!c.download) {
            shell_toast(std::string("Install it with: sudo apt install ") + c.package);
            return;
        }
        std::string dir = user_cores_dir();
        kit::make_dirs(dir);
        dl_name_ = c.name;
        kit::RunOpts o;
        o.cwd = dir;
        o.sim = {"downloading", "unzipping"};
        // Into ~/Games/cores: the app's own folder, nothing system-wide changes.
        dl_.start({"sh", "-c",
                   "set -e; f=\"$1_libretro.so\"; curl -fsSL --retry 2 -o \"$f.zip.part\" \"$2\"; "
                   "unzip -o -q \"$f.zip.part\" \"$f\"; rm -f \"$f.zip.part\"; test -s \"$f\"",
                   "sh", c.file, core_url(c)},
                  std::move(o));
        show_cores();
    }

    // Del on the Cores page: the same as Remove in the menu, asking first.
    void ask_remove_core() {
        int i = list_.focused();
        if (i < 0 || size_t(i) >= rows_.size()) return;
        std::string file = rows_[size_t(i)];
        if (!downloaded(file)) {
            shell_toast("Only downloaded cores can be removed here");
            return;
        }
        const CoreDef* def = core_def(file);
        shell_confirm(std::string("Remove ") + (def ? def->name : file.c_str()) + "?", "It can be downloaded again.",
                      "Remove", [this, file] { remove_core(file); });
    }

    void remove_core(const std::string& file) {
        if (!downloaded(file)) {
            shell_toast("Only downloaded cores can be removed here");
            return;
        }
        if (kit::simulated()) {
            const CoreDef* def = core_def(file);
            std::vector<std::string> keep;
            for (auto& n : sim_downloaded_)
                if (!def || n != def->name) keep.push_back(n);
            sim_downloaded_ = keep;
        }
        std::remove((user_cores_dir() + "/" + file + "_libretro.so").c_str());
        shell_toast("Removed");
        show_cores();
    }

    void picked(int i) {
        if (i < 0 || size_t(i) >= rows_.size()) return;
        std::string row = rows_[size_t(i)];
        if (page_ == Page::Games) {
            game_ = row;
            if (core_for(*sys_).empty()) {
                shell_toast("No core for this system yet: get one in Cores (menu)");
                return;
            }
            show_starting();
        } else if (page_ == Page::Cores) {
            const CoreDef* c = core_def(row);
            if (!c) return;
            if (installed(*c)) shell_toast(std::string(c->name) + " is installed");
            else download(*c);
        }
    }

    // ---- Playing
    void show_starting() {
        page_ = Page::Playing;
        menu_ = nullptr;
        // A Game screen: the game has the keys and the whole screen, so no
        // soft-key bar or menu (the pause menu is the game's own, on Esc).
        shell_set_actions({});
        stage_ = lv_obj_create(lv_layer_top());
        style_plain(stage_);
        lv_obj_set_size(stage_, kScreenW, kScreenH);
        lv_obj_set_pos(stage_, 0, 0);
        lv_obj_set_style_bg_color(stage_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(stage_, LV_OPA_COVER, 0);
        lv_obj_remove_flag(stage_, LV_OBJ_FLAG_SCROLLABLE);
        note_ = label(stage_, ("Starting " + game_title(game_) + "...").c_str(), &zs_font_12, kMuted);
        lv_obj_center(note_);
        pending_load_ = true;
    }

    void start_game() {
        std::string core = core_for(*sys_);
        std::string error;
        if (!emu_.load(core, game_, kScreenW, kScreenH, error)) {
            close_stage();
            show_games(*sys_);
            shell_toast(error);
            return;
        }
        if (note_) lv_obj_delete(note_);
        note_ = nullptr;
        pix_.assign(size_t(kScreenW) * kScreenH, 0);
        img_w_ = img_h_ = 0;
        border_pix_.assign(size_t(kScreenW) * kScreenH, 0);
        border_dsc_ = {};
        border_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        border_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        border_dsc_.header.w = uint32_t(kScreenW);
        border_dsc_.header.h = uint32_t(kScreenH);
        border_dsc_.header.stride = uint32_t(kScreenW * 2);
        border_dsc_.data = reinterpret_cast<const uint8_t*>(border_pix_.data());
        border_dsc_.data_size = uint32_t(border_pix_.size() * 2);
        border_img_ = lv_image_create(stage_);   // under the picture
        lv_obj_set_pos(border_img_, 0, 0);
        lv_image_set_src(border_img_, &border_dsc_);
        lv_obj_add_flag(border_img_, LV_OBJ_FLAG_HIDDEN);
        img_ = lv_image_create(stage_);
        load_border_image_for_game();
        emu_.set_smooth(smooth_);
        bool resumed = resume_ && emu_.has_state(kResume) && emu_.load_state(kResume);
        // The screen redraws at 30 Hz for apps; a game wants every frame.
        if (lv_timer_t* r = lv_display_get_refr_timer(lv_display_get_default())) lv_timer_set_period(r, 16);
        // On the device the emulator keeps its own time; the simulator's clock
        // can be virtual (headless), so there it runs in step with LVGL.
        tv_note_ = label(stage_, "", &zs_font_12, kMuted);
        lv_obj_center(tv_note_);
        lv_obj_add_flag(tv_note_, LV_OBJ_FLAG_HIDDEN);
        tv_failed_ = false;
        tv_check_at_ = lv_tick_get();
        if (tv_wanted_ && tv_plugged()) use_tv();
        emu_.start(!kit::simulated());
        frame_timer_ = lv_timer_create(on_frame, 5, this);   // pick each new picture up quickly
        if (resumed) hint("Carrying on where you left off (Esc > Reset to restart)");
        else if (int n = emu_.pad().gamepads(); n > 1) hint(fmt("%d players: Home for the menu", n));
        else hint(n ? "Esc or Home: menu" : "Esc menu   WASD move   K/J A/B   Enter start");
    }

    // ---- The TV (tv.h): on when a monitor is plugged in (and Play on TV is on)
    // Not in the simulator: the PC's own monitor isn't ours to take.
    static bool tv_plugged() { return !kit::simulated() && Tv::plugged(); }

    void watch_tv() {
        if (lv_tick_elaps(tv_check_at_) < 1000) return;
        tv_check_at_ = lv_tick_get();
        bool plugged = tv_plugged();
        if (!plugged) tv_failed_ = false;   // try again on the next plug-in
        if (emu_.on_tv() && !plugged) leave_tv("TV unplugged: playing on this screen");
        else if (!emu_.on_tv() && plugged && tv_wanted_ && !tv_failed_) use_tv();
    }

    void use_tv() {
        if (!stage_ || emu_.on_tv()) return;
        std::string error;
        if (hal().display().dock_running) error = "Leave Dock mode to play on the TV";
        else if (!tv_.open(error) && error.empty()) error = "The TV can't be used";
        if (!error.empty()) {
            tv_failed_ = true;
            hint(error);
            return;
        }
        emu_.set_tv(&tv_);
        lv_obj_add_flag(img_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(border_img_, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(tv_note_, fmt(LV_SYMBOL_VIDEO "  Playing on the TV (%dx%d)\n\nEsc: menu", tv_.width(),
                                        tv_.height()).c_str());
        lv_obj_set_style_text_align(tv_note_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(tv_note_);
        lv_obj_remove_flag(tv_note_, LV_OBJ_FLAG_HIDDEN);
        if (menu_) tv_menu_at_ = 0;   // put the menu on the TV now
        hint("Playing on the TV");
    }

    void leave_tv(const std::string& why) {
        if (!emu_.on_tv()) return;
        emu_.set_tv(nullptr);
        tv_.close();
        if (tv_note_) lv_obj_add_flag(tv_note_, LV_OBJ_FLAG_HIDDEN);
        if (img_) lv_obj_remove_flag(img_, LV_OBJ_FLAG_HIDDEN);
        img_w_ = img_h_ = 0;   // the next picture sets the screen up again (and its border)
        if (!why.empty()) hint(why);
    }

    // A snapshot of an LVGL object (the menu, a note), onto the TV.
    void tv_overlay(Tv::Place where, lv_obj_t* obj) {
        if (!emu_.on_tv()) return;
        lv_draw_buf_t* snap = obj ? lv_snapshot_take(obj, LV_COLOR_FORMAT_RGB565) : nullptr;
        if (snap) {
            tv_.overlay(where, reinterpret_cast<const uint16_t*>(snap->data), int(snap->header.w),
                        int(snap->header.h), int(snap->header.stride / 2));
            lv_draw_buf_destroy(snap);
        } else {
            tv_.overlay(where, nullptr, 0, 0, 0);
        }
        if (emu_.paused()) tv_.redraw();   // else the next picture shows it
    }

    void hide_hint() {
        if (!toast_ || lv_obj_has_flag(toast_, LV_OBJ_FLAG_HIDDEN)) return;
        lv_obj_add_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        tv_overlay(Tv::Place::Bottom, nullptr);
    }

    void hint(const std::string& text) {
        if (!stage_) return;
        if (!toast_) {
            toast_ = label(stage_, "", &zs_font_12, kText);
            lv_obj_set_style_bg_color(toast_, c(kSurface), 0);
            lv_obj_set_style_bg_opa(toast_, LV_OPA_80, 0);
            lv_obj_set_style_pad_hor(toast_, 6, 0);
            lv_obj_set_style_pad_ver(toast_, 2, 0);
            lv_obj_set_style_radius(toast_, 4, 0);
            lv_obj_align(toast_, LV_ALIGN_BOTTOM_MID, 0, -4);
        }
        lv_label_set_text(toast_, text.c_str());
        lv_obj_remove_flag(toast_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(toast_);
        toast_at_ = lv_tick_get();
        if (emu_.on_tv()) {
            lv_obj_update_layout(toast_);
            tv_overlay(Tv::Place::Bottom, toast_);
        }
    }

    static void on_frame(lv_timer_t* t) { static_cast<GamesApp*>(lv_timer_get_user_data(t))->frame(); }

    void frame() {
        emu_.step(lv_tick_get());
        if (menu_pick_ >= 0) {
            menu_action();
            menu_pick_ = -1;
            if (!stage_) return;   // quit
        }
        int w = 0, h = 0;
        if (emu_.take_frame(pix_, w, h) && w > 0 && h > 0) {
            if (w != img_w_ || h != img_h_) {   // first frame, or the core changed size
                img_w_ = w;
                img_h_ = h;
                dsc_ = {};
                dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
                dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
                dsc_.header.w = uint32_t(w);
                dsc_.header.h = uint32_t(h);
                dsc_.header.stride = uint32_t(w * 2);
                pic_ = {(kScreenW - w) / 2, (kScreenH - h) / 2, w, h};
                lv_obj_set_pos(img_, pic_.x, pic_.y);
                draw_border(true);
            }
            dsc_.data = reinterpret_cast<const uint8_t*>(pix_.data());   // pix_ may have moved
            dsc_.data_size = uint32_t(size_t(w) * size_t(h) * 2);
            lv_image_set_src(img_, &dsc_);
            lv_obj_invalidate(img_);
            // Glow follows the game, a few times a second (it's the whole
            // screen to send to the panel, so not every frame).
            if (border_ == Border::Glow && lv_tick_elaps(glow_at_) > 120) draw_border(false);
        }
        if (emu_.fast() != fast_shown_) {
            fast_shown_ = emu_.fast();
            if (fast_shown_) hint(LV_SYMBOL_NEXT " Fast forward");
            else hide_hint();
        }
        std::string m = emu_.take_message();
        if (!m.empty()) hint(m);
        if (toast_ && !lv_obj_has_flag(toast_, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(toast_at_) > 2500) hide_hint();
        if (menu_) menu_by_gamepad();
        // The menu on the TV too, as it changes (a few times a second).
        if (menu_ && emu_.on_tv() && lv_tick_elaps(tv_menu_at_) > 100) {
            tv_menu_at_ = lv_tick_get();
            tv_overlay(Tv::Place::Middle, menu_);
        }
        // A gamepad isn't a ZeroShell key: tell the power manager someone is playing.
        if (emu_.pad().take_activity() && lv_tick_elaps(active_at_) > 1000) {
            active_at_ = lv_tick_get();
            power().on_key(active_at_);
        }
    }

    // The border around the picture (border.h). all: the whole screen
    // changed (new picture size, or another border); else only the bars.
    void draw_border(bool all) {
        if (!border_img_ || img_w_ <= 0) return;
        if (border_ == Border::Off) {
            lv_obj_add_flag(border_img_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        if (border_ == Border::Frame || (border_ == Border::Image && border_image_.empty())) {
            if (!all) return;   // (no picture for Image: the Frame instead)
            draw_frame(border_pix_.data(), kScreenW, kScreenH, pic_, sys_ ? sys_->id : "");
        } else if (border_ == Border::Image) {
            if (!all) return;
            draw_image(border_pix_.data(), kScreenW, kScreenH, pic_, border_image_);
        } else {
            draw_glow(border_pix_.data(), kScreenW, kScreenH, pic_, pix_.data());
            glow_at_ = lv_tick_get();
        }
        lv_obj_remove_flag(border_img_, LV_OBJ_FLAG_HIDDEN);
        lv_image_cache_drop(&border_dsc_);
        if (all) {
            lv_obj_invalidate(border_img_);
            return;
        }
        // Only the bars: the picture redraws itself.
        lv_area_t bars[4] = {{0, 0, kScreenW - 1, pic_.y - 1},
                             {0, pic_.y + pic_.h, kScreenW - 1, kScreenH - 1},
                             {0, pic_.y, pic_.x - 1, pic_.y + pic_.h - 1},
                             {pic_.x + pic_.w, pic_.y, kScreenW - 1, pic_.y + pic_.h - 1}};
        for (auto& a : bars)
            if (a.x2 >= a.x1 && a.y2 >= a.y1) lv_obj_invalidate_area(border_img_, &a);
    }

    std::string border_item() const {
        std::string s = std::string("Border: ") + border_name(border_);
        if (border_ == Border::Image && border_image_.empty()) s += " (none yet)";
        return s;
    }

    static std::string borders_dir() { return games_dir() + "/borders"; }

    // A border picture for this game, its console, or every game (border.h).
    void load_border_image_for_game() {
        border_image_ = BorderImage{};
        border_image_error_.clear();
        std::string path = find_border_image(borders_dir(), sys_ ? sys_->id : "", game_title(game_));
        if (!path.empty() && !load_border_image(path, border_image_, border_image_error_))
            border_image_ = BorderImage{};
    }
    std::string picture_item() const { return smooth_ ? "Picture: Smooth" : "Picture: Sharp"; }
    std::string players_item() {
        int n = emu_.pad().gamepads();
        return n ? fmt("Swap players 1 and 2 (%d pad%s)", n, n == 1 ? "" : "s") : std::string("Swap players 1 and 2");
    }
    std::string tv_item() const {
        if (!tv_wanted_) return "Play on TV: Off";
        return emu_.on_tv() ? "Play on TV: On" : "Play on TV: On (no TV)";
    }
    std::string resume_item() const { return resume_ ? "Carry on next time: On" : "Carry on next time: Off"; }

    static void on_menu(lv_event_t* e) {
        auto* self = static_cast<GamesApp*>(lv_event_get_user_data(e));
        // Acted on from the frame timer: not inside the list's own event.
        self->menu_pick_ = int(lv_obj_get_index(lv_event_get_target_obj(e)));
    }

    void open_menu() {
        if (!stage_ || menu_) return;
        emu_.set_paused(true);
        menu_ = lv_obj_create(stage_);
        style_panel(menu_);
        lv_obj_set_size(menu_, 220, 160);
        lv_obj_center(menu_);
        lv_obj_set_style_pad_all(menu_, 4, 0);
        lv_obj_t* list = make_list(menu_, 0, 0, 210, 150);
        menu_list_ = list;
        if (group) lv_group_remove_all_objs(group);
        list_button(list, LV_SYMBOL_PLAY, "Resume", on_menu, this);
        list_button(list, LV_SYMBOL_SAVE, "Save state", on_menu, this);
        lv_obj_t* l = list_button(list, LV_SYMBOL_UPLOAD, "Load state", on_menu, this);
        if (!emu_.has_state()) lv_obj_set_style_text_color(l, c(kMuted), 0);
        list_button(list, LV_SYMBOL_SHUFFLE, players_item().c_str(), on_menu, this);
        list_button(list, LV_SYMBOL_VIDEO, tv_item().c_str(), on_menu, this);
        list_button(list, LV_SYMBOL_IMAGE, border_item().c_str(), on_menu, this);
        list_button(list, LV_SYMBOL_EYE_OPEN, picture_item().c_str(), on_menu, this);
        list_button(list, LV_SYMBOL_LOOP, resume_item().c_str(), on_menu, this);
        list_button(list, LV_SYMBOL_REFRESH, "Reset", on_menu, this);
        list_button(list, LV_SYMBOL_CLOSE, "Quit game", on_menu, this);
        finish_list(list);
        pad_prev_ = emu_.pad().gamepad_buttons();   // the Home press isn't a menu key
        tv_menu_at_ = 0;
    }

    // The d-pad moves in the menu, A or Start chooses, B goes back to the game.
    void menu_by_gamepad() {
        uint16_t now = emu_.pad().gamepad_buttons();
        uint16_t pressed = now & ~pad_prev_;
        pad_prev_ = now;
        if (!pressed || !group) return;
        auto down = [&](unsigned b) { return (pressed >> b) & 1; };
        if (down(retro::kUp)) lv_group_focus_prev(group);
        else if (down(retro::kDown)) lv_group_focus_next(group);
        else if (down(retro::kB)) close_menu();
        else if (down(retro::kA) || down(retro::kStart)) {
            if (lv_obj_t* f = lv_group_get_focused(group)) lv_obj_send_event(f, LV_EVENT_CLICKED, nullptr);
        }
        tv_menu_at_ = 0;
    }

    void close_menu() {
        if (!menu_) return;
        lv_obj_delete(menu_);
        menu_ = nullptr;
        tv_overlay(Tv::Place::Middle, nullptr);
        if (group) lv_group_remove_all_objs(group);
        emu_.hold_until_release();   // the Enter that chose Resume is not Start
        emu_.set_paused(false);
    }

    // The menu's rows, in order (open_menu).
    enum MenuRow { kRowResume, kRowSave, kRowLoad, kRowPlayers, kRowTv, kRowBorder, kRowPicture, kRowCarryOn, kRowReset, kRowQuit };

    void menu_action() {
        if (!menu_) return;
        switch (menu_pick_) {
            case kRowResume: close_menu(); break;
            case kRowSave:
                close_menu();
                hint(emu_.save_state() ? "State saved" : "This core can't save states");
                break;
            case kRowLoad:
                close_menu();
                hint(emu_.load_state() ? "State loaded" : "No saved state");
                break;
            case kRowPlayers:   // the settings change in place: the menu stays open
            case kRowTv:
            case kRowBorder:
            case kRowPicture:
            case kRowCarryOn: {
                std::string text;
                if (menu_pick_ == kRowPlayers) {
                    emu_.pad().swap_players();
                    text = players_item();
                    hint(emu_.pad().gamepads() ? "Players: " + emu_.pad().players_text()
                                               : std::string("No gamepads yet: the keyboard is player 1"));
                } else if (menu_pick_ == kRowTv) {
                    tv_wanted_ = !tv_wanted_;
                    tv_failed_ = false;
                    if (!tv_wanted_) leave_tv("");
                    else if (tv_plugged()) use_tv();
                    else hint("Plug a TV into the HDMI port");
                    text = tv_item();
                } else if (menu_pick_ == kRowBorder) {
                    border_ = next_border(border_);
                    draw_border(true);
                    text = border_item();
                    if (border_ == Border::Image && border_image_.empty())
                        hint(border_image_error_.empty()
                                 ? "No PNG in ~/Games/borders yet"
                                 : border_image_error_);
                } else if (menu_pick_ == kRowPicture) {
                    smooth_ = !smooth_;
                    emu_.set_smooth(smooth_);
                    text = picture_item();
                } else {
                    resume_ = !resume_;
                    if (!resume_) emu_.remove_state(kResume);
                    text = resume_item();
                }
                save_settings();
                lv_obj_t* b = lv_obj_get_child(menu_list_, menu_pick_);
                if (b) lv_list_set_button_text(menu_list_, b, text.c_str());
                tv_menu_at_ = 0;
                break;
            }
            case kRowReset: close_menu(); emu_.reset(); hint("Reset"); break;
            case kRowQuit: stop_game(); break;
            default: break;
        }
    }

    void close_stage() {
        if (frame_timer_) lv_timer_delete(frame_timer_);
        frame_timer_ = nullptr;
        if (lv_timer_t* r = lv_display_get_refr_timer(lv_display_get_default()))
            lv_timer_set_period(r, LV_DEF_REFR_PERIOD);
        emu_.set_tv(nullptr);   // (the emulator has stopped)
        tv_.close();
        if (stage_) lv_obj_delete(stage_);
        stage_ = img_ = border_img_ = note_ = menu_ = menu_list_ = toast_ = tv_note_ = nullptr;
        fast_shown_ = false;
    }

    void stop_game() {
        // Keep this moment to carry on from next time (not when the game itself shut down).
        if (resume_ && emu_.running() && !emu_.quit_requested()) emu_.save_state(kResume);
        emu_.stop();   // saves the game's battery save
        close_stage();
        if (page_ == Page::Playing && sys_) show_games(*sys_);
    }

    lv_obj_t* root_ = nullptr;
    Page page_ = Page::Library;
    const System* sys_ = nullptr;
    std::vector<std::string> rows_;   // what each list row is: a system id, a game's path or a core
    ListView list_;
    std::string game_;
    bool pending_load_ = false;

    Emulator emu_;
    Tv tv_;
    lv_obj_t* tv_note_ = nullptr;      // "Playing on the TV" on the built-in screen
    bool tv_wanted_ = true, tv_failed_ = false;
    uint32_t tv_check_at_ = 0, tv_menu_at_ = 0;
    uint16_t pad_prev_ = 0;            // gamepad buttons last time (the menu)
    static constexpr const char* kResume = ".resume.state";
    lv_obj_t *stage_ = nullptr, *img_ = nullptr, *note_ = nullptr, *menu_ = nullptr, *toast_ = nullptr;
    lv_obj_t *border_img_ = nullptr, *menu_list_ = nullptr;
    lv_image_dsc_t border_dsc_{};
    std::vector<uint16_t> border_pix_;
    Rect pic_;
    Border border_ = Border::Frame;
    BorderImage border_image_;
    std::string border_image_error_;
    bool smooth_ = true, resume_ = true, fast_shown_ = false;
    uint32_t glow_at_ = 0;
    lv_timer_t* frame_timer_ = nullptr;
    lv_image_dsc_t dsc_{};
    std::vector<uint16_t> pix_;
    int img_w_ = 0, img_h_ = 0;
    int menu_pick_ = -1;
    uint32_t toast_at_ = 0, active_at_ = 0;

    kit::Job dl_;
    std::string dl_name_;
    std::vector<std::string> sim_downloaded_;
};
}  // namespace

std::unique_ptr<App> make_games_app() { return std::make_unique<GamesApp>(); }

}  // namespace zs
