// Notes: plain-text notes in ~/Notes. The first screen is a List of them
// (docs/UX.md in the OS: typing filters, Enter opens); a note opens in a Text
// screen that saves a few seconds after you stop typing, and when you leave.
// The menu (Tab, or Ctrl + the letter) has New, Rename, Share and Delete on
// the list, and Find and Share in a note. Files opens a text file straight in
// the editor by passing its path; Esc then goes back to Files.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "apps/common.h"
#include "apps/kit/ui.h"
#include "ui/fileio.h"
#include "ui/list_view.h"
#include "ui/remote.h"

namespace zs {

namespace {
using namespace theme;

constexpr long long kMaxSize = 64 * 1024;   // larger files would make the editor slow on this screen

struct Note {
    std::string name;
    time_t modified;
    long long size;
};

class NotesApp : public App {
public:
    ~NotesApp() override { save(); }

    // ~/Notes (synced with the Notes folder). Older images kept the one note
    // in ~/notes.txt; move it there the first time.
    static std::string notes_dir() {
        std::string dir = home_dir() + "/Notes", p = dir + "/notes.txt", old = home_dir() + "/notes.txt";
        mkdir(dir.c_str(), 0755);
        if (access(p.c_str(), F_OK) != 0 && access(old.c_str(), F_OK) == 0) rename(old.c_str(), p.c_str());
        return dir;
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        dir_ = notes_dir();
        // Files opens a text file here by passing its path.
        std::string pending = shell_app_arg();
        if (!pending.empty()) {
            from_files_ = true;
            if (!open_note(pending)) show_list();
        } else {
            show_list();
        }
    }

    bool on_key(uint32_t key) override {
        if (mode_ != Mode::List) return false;   // typing: the text box has the keys
        if (list_.key(key)) {
            set_list_actions();
            return true;
        }
        return false;
    }

    bool on_back() override {
        switch (mode_) {
            case Mode::List:
                if (list_.filter().empty()) return false;
                list_.clear_filter();
                set_list_actions();
                return true;
            case Mode::Edit:
                save();
                if (from_files_) return false;   // back to Files
                show_list(name_of(path_));
                return true;
            case Mode::Prompt:
                if (prompt_ == Prompt::Find) open_note(path_, cursor_);
                else show_list(prompt_ == Prompt::Rename ? renaming_ : "");
                return true;
        }
        return false;
    }

    void tick() override {
        // Autosave 3 s after the last edit, so a crash or flat battery loses
        // at most a few seconds of typing.
        if (mode_ == Mode::Edit && dirty_ && lv_tick_elaps(edited_at_) > 3000) save();
        // A name typed in a prompt is acted on here, not inside the text
        // box's own event (that would delete the box while it runs).
        if (mode_ == Mode::Prompt && submitted_) {
            submitted_ = false;
            if (prompt_ == Prompt::New) make_note(typed_);
            else if (prompt_ == Prompt::Rename) rename_note(typed_);
            else find(typed_);
        }
    }

private:
    enum class Mode { List, Edit, Prompt };
    enum class Prompt { New, Rename, Find };

    static std::string name_of(const std::string& path) { return path.substr(path.find_last_of('/') + 1); }
    std::string path_of(const std::string& name) const { return dir_ + "/" + name; }

    void clear() {
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        list_.reset();
        ta_ = status_ = nullptr;
    }

    // ---- The list of notes ----------------------------------------------

    // The notes, newest first, with `focus` (a file name) in focus if it's there.
    void show_list(const std::string& focus = "") {
        clear();
        mode_ = Mode::List;
        notes_.clear();
        if (DIR* d = opendir(dir_.c_str())) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n[0] == '.') continue;
                struct stat st{};
                if (stat(path_of(n).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                notes_.push_back({n, st.st_mtime, (long long)st.st_size});
            }
            closedir(d);
        }
        std::sort(notes_.begin(), notes_.end(), [](const Note& a, const Note& b) {
            return a.modified != b.modified ? a.modified > b.modified : a.name < b.name;
        });

        set_list_actions();   // first: the list below is laid out above the bar
        kit::title_row(root_, "Notes", fmt("%zu", notes_.size()));
        list_.build(root_, 2, 20, kScreenW - 4, kBodyH - 21);
        list_.set_empty_text(notes_.empty() ? "No notes yet. Tab > N makes one." : "Nothing matches");
        int at = -1;
        for (size_t i = 0; i < notes_.size(); ++i) {
            const Note& n = notes_[i];
            char when[16];
            tm lt{};
            localtime_r(&n.modified, &lt);
            strftime(when, sizeof when, "%d %b", &lt);
            int row = list_.add(ZS_ICON_NOTE, n.name, when, [this, i] {
                if (i < notes_.size()) open_note(path_of(notes_[i].name));
            });
            if (n.name == focus) at = row;
        }
        list_.finish();
        if (at >= 0) list_.focus(at);
        set_list_actions();   // again, for the row now in focus
    }

    const Note* focused() const {
        int i = mode_ == Mode::List ? list_.focused() : -1;
        return i >= 0 && i < int(notes_.size()) ? &notes_[size_t(i)] : nullptr;
    }

    void set_list_actions() {
        const Note* n = focused();
        std::string name = n ? n->name : "";
        std::vector<Action> a;
        a.push_back({"Open", 0, "Enter", nullptr});
        a.push_back({"New note", 'n', nullptr, [this] { ask(Prompt::New, "New note", "", "Make"); }});
        a.push_back({"Rename", 'e', nullptr, [this, name] {
                         renaming_ = name;
                         ask(Prompt::Rename, "Rename " + name, name, "Rename");
                     }, n != nullptr});
        a.push_back({"Share to a device", 's', nullptr, [this, name] { share(path_of(name)); }, n != nullptr});
        Action del{"Delete", 'd', nullptr, [this, name] { remove_note(name); }, n != nullptr, true};
        if (n) del.confirm = "Delete " + name + "?";
        a.push_back(del);
        shell_set_actions(std::move(a));
    }

    void ask(Prompt p, const std::string& title, const std::string& initial, const char* ok) {
        save();
        if (ta_) cursor_ = lv_textarea_get_cursor_pos(ta_);
        clear();
        mode_ = Mode::Prompt;
        prompt_ = p;
        kit::input_screen(root_, group, title.c_str(), p == Prompt::Find ? "Text to find" : "Name", initial,
                          [this](const std::string& t) {
                              typed_ = t;
                              submitted_ = true;
                          }, nullptr, ok);
    }

    // A typed name as a file name: one path part, .txt added when it has no
    // extension of its own. "" if it can't be one.
    static std::string file_name(std::string n) {
        while (!n.empty() && n.back() == ' ') n.pop_back();
        while (!n.empty() && n.front() == ' ') n.erase(0, 1);
        if (n.empty() || n[0] == '.' || n.find('/') != std::string::npos) return "";
        if (n.find('.') == std::string::npos) n += ".txt";
        return n;
    }

    void make_note(const std::string& typed) {
        std::string name = file_name(typed);
        if (name.empty()) {
            shell_toast("Not a note name");
            return;
        }
        if (access(path_of(name).c_str(), F_OK) != 0 && !write_file_atomic(path_of(name), "")) {
            shell_toast("Couldn't make " + name);
            return;
        }
        open_note(path_of(name));
    }

    void rename_note(const std::string& typed) {
        std::string name = file_name(typed);
        if (name.empty()) {
            shell_toast("Not a note name");
            return;
        }
        if (name != renaming_) {
            if (access(path_of(name).c_str(), F_OK) == 0) {
                shell_toast(name + " is already there");
                return;
            }
            if (rename(path_of(renaming_).c_str(), path_of(name).c_str()) != 0) {
                shell_toast("Couldn't rename " + renaming_);
                name = renaming_;
            }
        }
        show_list(name);
    }

    void remove_note(const std::string& name) {
        if (name.empty()) return;
        shell_toast(unlink(path_of(name).c_str()) == 0 ? "Deleted " + name : "Couldn't delete " + name);
        show_list();
    }

    // Taildrop, through Remote (as Files does).
    void share(const std::string& path) {
        save();
        remote_set_send(path);
        shell_open_app("remote");
    }

    // ---- One note ---------------------------------------------------------

    bool open_note(const std::string& path, int cursor = -1) {
        struct stat st{};
        if (stat(path.c_str(), &st) == 0 && st.st_size > kMaxSize) {
            shell_toast("Too large to open here");
            if (mode_ != Mode::List) show_list(name_of(path));
            return false;
        }
        clear();
        mode_ = Mode::Edit;
        path_ = path;
        dirty_ = false;
        shell_set_actions({{"Back", 0, "Esc", nullptr},
                           {"Find in note", 'f', nullptr, [this] { ask(Prompt::Find, "Find in " + name_of(path_), find_, "Find"); }},
                           {"Share to a device", 's', nullptr, [this] { share(path_); }}});

        lv_obj_t* t = label(root_, name_of(path_).c_str(), &zs_font_12, kAccent);
        lv_obj_set_pos(t, 8, 3);
        status_ = label(root_, "", &zs_font_12, kMuted);
        lv_obj_align(status_, LV_ALIGN_TOP_RIGHT, -8, 3);

        ta_ = lv_textarea_create(root_);
        lv_obj_set_pos(ta_, 4, 20);
        lv_obj_set_size(ta_, kScreenW - 8, kBodyH - 22);
        lv_obj_set_style_bg_color(ta_, c(kSurface), 0);
        lv_obj_set_style_border_width(ta_, 0, 0);
        lv_obj_set_style_text_font(ta_, &zs_font_12, 0);
        lv_obj_set_style_pad_all(ta_, 6, 0);
        lv_obj_set_style_bg_color(ta_, c(kAccent), LV_PART_CURSOR | LV_STATE_FOCUSED);

        std::ifstream f(path_);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        if (text.empty() && !f && name_of(path_) == "notes.txt")
            text = "Welcome to ZeroOS Notes.\n\nType anything. It saves as you go.\n";
        lv_textarea_set_text(ta_, text.c_str());
        if (cursor >= 0) lv_textarea_set_cursor_pos(ta_, cursor);
        lv_obj_add_event_cb(ta_, on_change, LV_EVENT_VALUE_CHANGED, this);
        shell_add_focus(ta_);
        lv_group_focus_obj(ta_);
        update_status();
        return true;
    }

    // The next place `what` is, after where the cursor was, wrapping round
    // to the top; capitals don't count.
    void find(const std::string& what) {
        find_ = what;
        int at = cursor_;
        if (!what.empty()) {
            std::ifstream f(path_);
            std::stringstream ss;
            ss << f.rdbuf();
            auto lower = [](std::string s) {
                for (auto& ch : s) ch = char(std::tolower((unsigned char)ch));
                return s;
            };
            std::string text = lower(ss.str()), w = lower(what);
            // The cursor counts characters, not bytes: find in bytes, then count.
            size_t from = 0;
            for (int k = 0; k < cursor_ + 1 && from < text.size(); ++k) from += utf8_len(text, from);
            size_t hit = text.find(w, from);
            if (hit == std::string::npos) hit = text.find(w);
            if (hit == std::string::npos) {
                shell_toast("Not in this note: " + what);
            } else {
                at = 0;
                for (size_t i = 0; i < hit; i += utf8_len(text, i)) ++at;
            }
        }
        open_note(path_, at);
    }

    static size_t utf8_len(const std::string& s, size_t i) {
        uint8_t ch = uint8_t(s[i]);
        size_t n = ch < 0x80 ? 1 : (ch >> 5) == 0x6 ? 2 : (ch >> 4) == 0xe ? 3 : (ch >> 3) == 0x1e ? 4 : 1;
        return std::min(n, s.size() - i);
    }

    static void on_change(lv_event_t* e) {
        auto* self = static_cast<NotesApp*>(lv_event_get_user_data(e));
        self->dirty_ = true;
        self->edited_at_ = lv_tick_get();
        self->update_status();
    }

    void update_status() {
        std::string t = lv_textarea_get_text(ta_);
        size_t words = 0;
        bool in = false;
        for (char ch : t) {
            bool ws = ch == ' ' || ch == '\n' || ch == '\t';
            if (!ws && !in) ++words;
            in = !ws;
        }
        lv_label_set_text(status_, fmt("%zu words%s", words, dirty_ ? "  *" : "").c_str());
    }

    // Written to a temp file and renamed over the note, so a power cut or a
    // full card never leaves it half-written. On failure the edits stay
    // marked unsaved and the next autosave tries again.
    void save() {
        if (!dirty_ || !ta_) return;
        mode_t mode = 0644;
        struct stat st;
        if (stat(path_.c_str(), &st) == 0) mode = st.st_mode & 07777;   // keep the file's permissions
        if (write_file_atomic(path_, lv_textarea_get_text(ta_), mode)) {
            dirty_ = false;
            failed_ = false;
        } else if (!failed_) {
            failed_ = true;   // say it once, not on every retry
            shell_toast("Couldn't save the note (card full or read-only?)");
        }
        if (status_) update_status();
    }

    lv_obj_t* root_ = nullptr;
    Mode mode_ = Mode::List;
    Prompt prompt_ = Prompt::New;
    ListView list_;
    std::vector<Note> notes_;
    std::string dir_, path_;
    std::string renaming_, typed_, find_;
    bool submitted_ = false;
    bool from_files_ = false;
    int cursor_ = 0;
    lv_obj_t* ta_ = nullptr;
    lv_obj_t* status_ = nullptr;
    bool dirty_ = false, failed_ = false;
    uint32_t edited_at_ = 0;
};
}  // namespace

std::unique_ptr<App> make_notes_app() { return std::make_unique<NotesApp>(); }

}  // namespace zs
