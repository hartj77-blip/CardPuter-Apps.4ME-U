// Recorder: voice notes from the microphone, kept in ~/Recordings as 16 kHz
// mono WAV files (sox records them). Sync can keep the folder on a PC.
//
//   R            record / stop          Enter   play the note (again: pause)
//   Space        pause / resume         V       stop playing
//   E            rename the note        X, Del  delete it (asks first)
//   + / -        volume                 Fn+Q    play / pause
//
// Playing goes through the shell's queue (ui/player.h), like Media's music.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <functional>

#include "apps/common.h"
#include "platform/keys.h"
#include "ui/player.h"

namespace zs {
namespace {
using namespace theme;

constexpr int kBars = 24;
constexpr int kPanelH = 46;
constexpr uint32_t kRec = 0xe01b24;

std::string mmss(int ms) {
    if (ms < 0) return "--:--";
    int s = ms / 1000;
    return fmt("%d:%02d", s / 60, s % 60);
}

// "note-20261123-101500.wav" -> "" (it only says when; the date column
// shows that), "Shopping list.wav" -> "Shopping list".
std::string display_name(const std::string& file) {
    std::string n = file;
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".wav") == 0) n.resize(n.size() - 4);
    if (n.rfind("note-", 0) == 0 && n.size() == 20) return "";
    return n;
}

class RecorderApp : public App {
public:
    ~RecorderApp() override {
        if (timer_) lv_timer_delete(timer_);
    }

    void build(lv_obj_t* root) override {
        root_ = root;

        lv_obj_t* panel = lv_obj_create(root);
        style_panel(panel);
        lv_obj_set_pos(panel, 4, 3);
        lv_obj_set_size(panel, kScreenW - 8, kPanelH);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(panel, 4, 0);

        dot_ = label(panel, LV_SYMBOL_BULLET, &zs_font_20, kMuted);
        lv_obj_set_pos(dot_, 2, 2);
        state_ = label(panel, "Ready", &zs_font_12, kMuted);
        lv_obj_set_pos(state_, 18, 0);
        time_ = label(panel, "0:00", &zs_font_20, kText);
        lv_obj_set_pos(time_, 18, 13);

        for (int i = 0; i < kBars; ++i) {
            bars_[i] = lv_obj_create(panel);
            style_plain(bars_[i]);
            lv_obj_set_style_bg_opa(bars_[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(bars_[i], 1, 0);
            lv_obj_set_size(bars_[i], 5, 2);
        }
        set_actions();
        rebuild_list();
        // The meter moves faster than the shell's 10 Hz tick.
        timer_ = lv_timer_create([](lv_timer_t* t) { static_cast<RecorderApp*>(lv_timer_get_user_data(t))->meter(); },
                                 60, this);
    }

    void tick() override {
        auto& h = hal();
        RecordState rs = h.record_state();
        PlaybackState pb = h.playback();
        bool mine = !pb.path.empty() && is_note(pb.path);
        if (rs.recording) {
            bool blink = (lv_tick_get() / 500) % 2;
            lv_obj_set_style_text_color(dot_, c(blink ? kRec : kSurface2), 0);
            lv_label_set_text(state_, "Recording   R stops");
            lv_obj_set_style_text_color(state_, c(kRec), 0);
            lv_label_set_text(time_, mmss(rs.elapsed_ms).c_str());
        } else if (mine) {
            lv_obj_set_style_text_color(dot_, c(pb.paused ? kWarn : kGood), 0);
            std::string name = pb.path.substr(pb.path.rfind('/') + 1);
            std::string shown = display_name(name);
            lv_label_set_text(state_, ((pb.paused ? "Paused  " : "Playing  ") + (shown.empty() ? name : shown)).c_str());
            lv_obj_set_style_text_color(state_, c(pb.paused ? kWarn : kGood), 0);
            lv_label_set_text(time_, (mmss(pb.pos_ms) + " / " + mmss(pb.dur_ms)).c_str());
        } else {
            lv_obj_set_style_text_color(dot_, c(kMuted), 0);
            lv_label_set_text(state_, notes_.empty() ? "Press R to record" : fmt("%zu notes   R records", notes_.size()).c_str());
            lv_obj_set_style_text_color(state_, c(kMuted), 0);
            lv_label_set_text(time_, "0:00");
        }
        live_ = rs.recording || (mine && !pb.paused);
        recording_ = rs.recording;
        if (rs.recording != was_recording_) {
            was_recording_ = rs.recording;
            if (!rs.recording) rebuild_list(true);   // the new note, at the top
        }
        std::string playing = mine ? pb.path : "";
        if (playing != listed_playing_ && !input_) rebuild_list();
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;   // typing in the text box
        auto& h = hal();
        switch (key) {
            case 'r': case 'R': toggle_record(); return true;
            case ' ':
            case kKeyPlayPause: {
                PlaybackState pb = h.playback();
                if (!pb.path.empty()) h.set_paused(!pb.paused);
                else play(focused_index());
                return true;
            }
            case 'v': case 'V':
                player_stop();
                rebuild_list();
                return true;
            case 'e': case 'E': rename_focused(); return true;
            case 'x': case 'X': case LV_KEY_DEL: delete_focused(); return true;
            case '+': case '=': h.set_volume(std::min(100, h.volume() + 5)); return true;
            case '-': case '_': h.set_volume(std::max(0, h.volume() - 5)); return true;
        }
        return false;
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        return false;
    }

private:
    void set_actions() {
        std::vector<Action> a;
        a.push_back({"Record", 'r', "R", [this] { toggle_record(); }});
        a.push_back({"Play", 0, "Enter", nullptr});
        a.push_back({"Pause", 0, "Space", nullptr});
        a.push_back({"Stop playing", 's', "V", [this] {
                         player_stop();
                         rebuild_list();
                     }});
        a.push_back({"Rename", 'e', "E", [this] { rename_focused(); }});
        a.push_back({"Volume", 0, "+ -", nullptr});
        a.push_back({"Delete", 'd', "X", [this] { delete_focused(true); }, true, true});
        shell_set_actions(std::move(a));
    }

    static bool is_note(const std::string& path) {
        return path.find("/Recordings/") != std::string::npos;
    }

    void toggle_record() {
        auto& h = hal();
        if (h.record_state().recording) {
            h.record_stop();
            shell_toast("Voice note saved");
        } else {
            player_stop();
            shell_toast(h.record_start() ? "Recording... R stops" : "No microphone found");
        }
    }

    // The level meter: lively while recording or playing, flat otherwise.
    // (For the look: the recorder doesn't hand over the sound itself.)
    void meter() {
        if (!live_ && settled_) return;
        settled_ = !live_;
        uint32_t col = recording_ ? kRec : kAccent;
        for (int i = 0; i < kBars; ++i) {
            int target = live_ ? 3 + std::rand() % 30 : 2;
            levels_[i] = (levels_[i] + target) / 2;
            lv_obj_set_size(bars_[i], 5, levels_[i]);
            lv_obj_set_pos(bars_[i], 140 + i * 7, kPanelH - 9 - levels_[i]);
            lv_obj_set_style_bg_color(bars_[i], c(live_ ? col : kSurface2), 0);
        }
    }

    void rebuild_list(bool focus_first = false) {
        int keep = focus_first ? 0 : focused_index();
        if (list_) {
            lv_group_remove_all_objs(group);
            lv_obj_delete(list_);
        }
        notes_ = hal().recordings();
        PlaybackState pb = hal().playback();
        listed_playing_ = !pb.path.empty() && is_note(pb.path) ? pb.path : "";
        list_ = make_list(root_, 4, kPanelH + 6, kScreenW - 8, kBodyH - kPanelH - 8);
        if (notes_.empty()) lv_list_add_text(list_, "No voice notes yet: press R");
        lv_obj_t* focus = nullptr;
        for (size_t i = 0; i < notes_.size(); ++i) {
            const Track& t = notes_[i];
            bool now = t.path == listed_playing_;
            std::string name = display_name(t.title);
            std::string text = name.empty() ? t.artist : name + "   " + t.artist;   // artist: the date
            lv_obj_t* b = list_button(list_, now ? LV_SYMBOL_PLAY : LV_SYMBOL_FILE, text.c_str(), on_click, this);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
            if (lv_obj_t* lbl = lv_obj_get_child(b, 1)) lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
            label(b, mmss(t.duration_s * 1000).c_str(), &zs_font_12, kMuted);
            if (now || (!focus && int(i) == keep)) focus = b;
        }
        finish_list(list_);
        // Rows a little tighter than the default, so more fit.
        for (uint32_t k = 0; k < lv_obj_get_child_count(list_); ++k)
            lv_obj_set_style_pad_ver(lv_obj_get_child(list_, int32_t(k)), 3, 0);
        if (focus) lv_group_focus_obj(focus);
    }

    int focused_index() {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f || !list_ || lv_obj_get_parent(f) != list_) return -1;
        return int((intptr_t)lv_obj_get_user_data(f));
    }

    // Enter: play the note; Enter on the one playing pauses or resumes it.
    void play(int i) {
        if (i < 0) i = 0;
        if (i >= int(notes_.size())) return;
        PlaybackState pb = hal().playback();
        if (pb.path == notes_[i].path) {
            hal().set_paused(!pb.paused);
            return;
        }
        const Track& t = notes_[i];
        std::string name = display_name(t.title);
        if (!player_play({{t.path, name.empty() ? "Voice note " + t.artist : name, "Recorder", t.duration_s, ""}}, 0))
            shell_toast("Can't play that note");
        rebuild_list();
    }

    void delete_focused(bool from_menu = false) {
        int i = focused_index();
        if (i < 0 || i >= int(notes_.size())) return;
        std::string path = notes_[i].path;
        std::string name = display_name(notes_[i].title);
        if (name.empty()) name = "the note from " + notes_[i].artist;
        auto run = [this, path, name] {
            if (hal().playback().path == path) player_stop();
            if (hal().delete_recording(path)) shell_toast("Deleted " + name);
            else shell_toast("Couldn't delete it");
            rebuild_list();
        };
        if (from_menu) run();   // the menu has asked already
        else shell_confirm("Delete " + name + "?", "It can't be got back.", "Delete", run);
    }

    void rename_focused() {
        int i = focused_index();
        if (i < 0 || i >= int(notes_.size())) return;
        std::string path = notes_[i].path;
        ask("New name", display_name(notes_[i].title), [this, path](const std::string& typed) {
            std::string name;
            for (char ch : typed)
                if (ch != '/' && ch != '\\' && (unsigned char)ch >= 0x20) name += ch;
            while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
            while (!name.empty() && (name[0] == ' ' || name[0] == '.')) name.erase(0, 1);
            if (name.empty()) return;
            std::string to = path.substr(0, path.rfind('/') + 1) + name + ".wav";
            if (to == path) return;
            if (access(to.c_str(), F_OK) == 0) {
                shell_toast("There's already a note called " + name);
                return;
            }
            if (hal().playback().path == path) player_stop();
            if (std::rename(path.c_str(), to.c_str()) != 0) {
                shell_toast("Couldn't rename it");
                return;
            }
            shell_toast("Renamed to " + name);
            rebuild_list();
        });
    }

    // One-line text box over the screen. Enter accepts, Esc cancels.
    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, Done done) {
        done_ = std::move(done);
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(input_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<RecorderApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            // Enter is still down: its release would also "click" a row.
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
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
        rebuild_list();
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<RecorderApp*>(lv_event_get_user_data(e));
        self->pending_ = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        // Deferred: playing rebuilds the list, which deletes the clicked button.
        lv_async_call([](void* p) {
            auto* s = static_cast<RecorderApp*>(p);
            s->play(s->pending_);
        }, self);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* list_ = nullptr;
    lv_obj_t *dot_ = nullptr, *state_ = nullptr, *time_ = nullptr;
    lv_obj_t* bars_[kBars] = {};
    int levels_[kBars] = {};
    lv_timer_t* timer_ = nullptr;
    bool live_ = false, recording_ = false, settled_ = false, was_recording_ = false;
    std::vector<Track> notes_;
    std::string listed_playing_;
    int pending_ = -1;
    lv_obj_t* input_ = nullptr;
    Done done_;
};
}  // namespace

std::unique_ptr<App> make_recorder_app() { return std::make_unique<RecorderApp>(); }

}  // namespace zs
