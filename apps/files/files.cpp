// Files: browse the filesystem, starting in the home folder. A List screen
// (docs/UX.md in the OS): typing filters the folder, Enter opens a folder (or
// a text file in Notes, music in the player, video on the monitor in Dock
// mode), Backspace with nothing typed goes up. The rest is in the menu (Tab,
// or Ctrl + the letter): rename, copy, move, paste, send with Taildrop, new
// folder, hidden files and delete.
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>

#include <cctype>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "apps/common.h"
#include "apps/kit/ui.h"
#include "ui/list_view.h"
#include "ui/remote.h"
#include "apps/kit/look.h"

namespace zs {


namespace {
using namespace theme;
namespace fs = std::filesystem;

struct Entry {
    std::string name;
    bool dir;
    long long size;
    bool file;   // a regular file: not a device, pipe or socket
};

std::string human(long long b) {
    if (b < 1024) return fmt("%lld B", b);
    if (b < 1024 * 1024) return fmt("%.1f KB", b / 1024.0);
    if (b < 1024LL * 1024 * 1024) return fmt("%.1f MB", b / 1048576.0);
    return fmt("%.1f GB", b / 1073741824.0);
}

// A name the user typed is one path component: no slashes, not . or ..
bool good_name(const std::string& n) {
    return !n.empty() && n != "." && n != ".." && n.find('/') == std::string::npos;
}

class FilesApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        cwd_ = home_dir();
        refresh();
    }

    bool on_key(uint32_t key) override {
        if (prompt_ != Prompt::None) return false;   // typing a name: the text box has the keys
        if (list_.key(key)) {
            set_actions();   // the focused row changed: Share and the questions follow it
            return true;
        }
        if (key == LV_KEY_BACKSPACE || key == 8) {   // nothing typed to take off: up a folder
            up();
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (prompt_ != Prompt::None) {
            refresh();
            return true;
        }
        if (!list_.filter().empty()) {
            list_.clear_filter();
            set_actions();
            return true;
        }
        if (cwd_ != "/" && cwd_ != home_dir()) {
            up();
            return true;
        }
        return false;
    }

    // A name typed in a prompt is acted on here, not inside the text box's
    // own event (that would delete the box while it runs).
    void tick() override {
        if (!submitted_) return;
        submitted_ = false;
        std::string name = typed_;
        if (prompt_ == Prompt::NewFolder) new_folder(name);
        else if (prompt_ == Prompt::Rename) rename_to(name);
    }

private:
    enum class Prompt { None, NewFolder, Rename };

    std::string path_of(const std::string& name) const { return cwd_ + (cwd_ == "/" ? "" : "/") + name; }

    // The entry in focus, or nullptr (an empty folder, or nothing matches).
    const Entry* focused() const {
        int i = list_.focused();
        return i >= 0 && i < int(entries_.size()) ? &entries_[size_t(i)] : nullptr;
    }

    void up() {
        if (cwd_ == "/") return;
        auto p = cwd_.find_last_of('/');
        std::string from = cwd_.substr(p + 1);
        cwd_ = p == 0 ? "/" : cwd_.substr(0, p);
        refresh(from);   // back on the folder we came out of
    }

    // The menu follows the row in focus: Share only for a file, Paste only
    // with something copied or cut.
    void set_actions() {
        const Entry* e = prompt_ == Prompt::None ? focused() : nullptr;
        std::string name = e ? e->name : "";
        std::vector<Action> a;
        a.push_back({"Open", 0, "Enter", nullptr});
        a.push_back({"Up", 0, "Bksp", nullptr});
        a.push_back({"Rename", 'e', nullptr, [this] { ask_rename(); }, e != nullptr});
        a.push_back({"Copy", 'c', nullptr, [this] { mark(false); }, e != nullptr});
        a.push_back({"Move", 'm', nullptr, [this] { mark(true); }, e != nullptr});
        std::string paste_label = clip_.empty() ? std::string("Paste here")
                                          : (clip_move_ ? "Move " : "Paste ") + fs::path(clip_).filename().string() + " here";
        a.push_back({paste_label, 'v', nullptr, [this] { paste(); }, !clip_.empty()});
        a.push_back({"Share to a device", 's', nullptr, [this] { share(); }, e && !e->dir});
        a.push_back({"New folder", 'n', nullptr, [this] { ask_new_folder(); }});
        a.push_back({show_hidden_ ? "Hide hidden files" : "Show hidden files", 'h', nullptr, [this] {
                         show_hidden_ = !show_hidden_;
                         const Entry* f = focused();
                         refresh(f ? f->name : "");
                     }});
        Action del{"Delete", 'd', nullptr, [this, name] { remove(name); }, e != nullptr, true};
        if (e) del.confirm = "Delete " + name + (e->dir ? " and all in it?" : "?");
        a.push_back(del);
        shell_set_actions(std::move(a));
    }

    // The folder anew, with `focus` (a name) in focus if it's there.
    void refresh(const std::string& focus = "") {
        prompt_ = Prompt::None;
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        entries_.clear();
        if (DIR* d = opendir(cwd_.c_str())) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n == "." || n == ".." || (n[0] == '.' && !show_hidden_)) continue;
                struct stat st{};
                std::string full = path_of(n);
                if (stat(full.c_str(), &st) != 0) continue;
                entries_.push_back({n, S_ISDIR(st.st_mode), (long long)st.st_size, S_ISREG(st.st_mode) != 0});
            }
            closedir(d);
        }
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
            if (a.dir != b.dir) return a.dir;
            return a.name < b.name;
        });

        set_actions();   // first: the list below is laid out above the bar
        std::string shown = cwd_;
        if (shown.rfind(home_dir(), 0) == 0) shown = "~" + shown.substr(home_dir().size());
        lv_obj_t* path = label(root_, shown.c_str(), &zs_font_12, kAccent);
        lv_obj_set_pos(path, 8, 3);
        lv_obj_set_width(path, 230);
        lv_label_set_long_mode(path, LV_LABEL_LONG_DOT);
        lv_obj_t* cnt = label(root_, fmt("%zu items", entries_.size()).c_str(), &zs_font_12, kMuted);
        lv_obj_align(cnt, LV_ALIGN_TOP_RIGHT, -8, 3);

        list_.build(root_, 2, 20, kScreenW - 4, kBodyH - 21);
        list_.set_empty_text(entries_.empty() ? "Empty folder" : "Nothing matches");
        int at = -1;
        for (size_t i = 0; i < entries_.size(); ++i) {
            auto& e = entries_[i];
            int row = list_.add(e.dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, e.name, e.dir ? "" : human(e.size),
                                [this, i] { open(int(i)); });
            if (e.name == focus) at = row;
        }
        list_.finish();
        if (at >= 0) list_.focus(at);
        set_actions();   // again, for the row now in focus
    }

    void share() {
        const Entry* e = focused();
        if (!e || e->dir) return;
        remote_set_send(path_of(e->name));
        shell_open_app("remote");
    }

    // Copy or Cut: remember the path; Paste in another folder finishes it.
    void mark(bool move) {
        const Entry* e = focused();
        if (!e) return;
        clip_ = path_of(e->name);
        clip_move_ = move;
        shell_toast((move ? "Moving " : "Copying ") + e->name + ": open a folder, then Tab > V");
        set_actions();
    }

    void paste() {
        if (clip_.empty()) return;
        fs::path from(clip_);
        std::string name = from.filename().string();
        std::string to = path_of(name);
        std::error_code ec;
        if (fs::exists(to, ec)) {
            shell_toast(name + " is already here");
            return;
        }
        if (cwd_.rfind(clip_ + "/", 0) == 0 || cwd_ == clip_) {
            shell_toast("Can't put a folder inside itself");
            return;
        }
        if (clip_move_) {
            fs::rename(from, to, ec);
            if (ec == std::errc::cross_device_link) {   // another card or drive: copy, then remove
                ec.clear();
                fs::copy(from, to, fs::copy_options::recursive, ec);
                if (!ec) fs::remove_all(from, ec);
            }
        } else {
            fs::copy(from, to, fs::copy_options::recursive, ec);
        }
        if (ec) {
            shell_toast("Couldn't " + std::string(clip_move_ ? "move " : "copy ") + name);
        } else {
            shell_toast((clip_move_ ? "Moved " : "Copied ") + name);
            clip_.clear();
        }
        refresh(name);
    }

    void remove(const std::string& name) {
        if (name.empty()) return;
        std::error_code ec;
        fs::remove_all(path_of(name), ec);
        shell_toast(ec ? "Couldn't delete " + name : "Deleted " + name);
        if (clip_ == path_of(name)) clip_.clear();
        refresh();
    }

    void ask(Prompt p, const char* title, const std::string& initial, const char* ok) {
        prompt_ = p;
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        list_.reset();
        kit::input_screen(root_, group, title, "Name", initial, [this](const std::string& t) {
            typed_ = t;
            submitted_ = true;
        }, nullptr, ok);
    }

    void ask_new_folder() { ask(Prompt::NewFolder, "New folder", "", "Make"); }

    void ask_rename() {
        const Entry* e = focused();
        if (!e) return;
        renaming_ = e->name;
        ask(Prompt::Rename, ("Rename " + e->name).c_str(), e->name, "Rename");
    }

    void new_folder(const std::string& name) {
        if (!good_name(name)) {
            shell_toast("Not a folder name");
            return;
        }
        std::error_code ec;
        if (fs::exists(path_of(name), ec)) shell_toast(name + " is already here");
        else if (!fs::create_directory(path_of(name), ec)) shell_toast("Couldn't make " + name);
        refresh(name);
    }

    void rename_to(const std::string& name) {
        if (name == renaming_) {
            refresh(name);
            return;
        }
        if (!good_name(name)) {
            shell_toast("Not a file name");
            return;
        }
        std::error_code ec;
        if (fs::exists(path_of(name), ec)) {
            shell_toast(name + " is already here");
            return;
        }
        fs::rename(path_of(renaming_), path_of(name), ec);
        if (ec) shell_toast("Couldn't rename " + renaming_);
        refresh(ec ? renaming_ : name);
    }

    // No NUL bytes and valid UTF-8 (ASCII is a subset). Files are < 64 KB here.
    static bool looks_like_text(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::string b(64 * 1024, '\0');
        f.read(&b[0], std::streamsize(b.size()));
        b.resize(size_t(f.gcount()));
        for (size_t i = 0; i < b.size();) {
            uint8_t ch = uint8_t(b[i]);
            if (ch == 0) return false;
            int n = ch < 0x80 ? 0 : (ch >> 5) == 0x6 ? 1 : (ch >> 4) == 0xe ? 2 : (ch >> 3) == 0x1e ? 3 : -1;
            if (n < 0 || i + size_t(n) >= b.size() + (n ? 0 : 1)) return false;
            for (int k = 1; k <= n; ++k)
                if ((uint8_t(b[i + size_t(k)]) & 0xc0) != 0x80) return false;
            i += size_t(n) + 1;
        }
        return true;
    }

    void open(int pick) {
        if (pick < 0 || pick >= int(entries_.size())) return;
        auto e = entries_[size_t(pick)];
        std::string full = path_of(e.name);
        std::string ext = e.name.substr(e.name.rfind('.') == std::string::npos ? e.name.size() : e.name.rfind('.'));
        for (auto& ch : ext) ch = char(std::tolower((unsigned char)ch));
        auto is = [&](std::initializer_list<const char*> l) {
            for (auto x : l) if (ext == x) return true;
            return false;
        };
        if (e.dir) {
            cwd_ = full;
            refresh();
        } else if (!e.file) {
            // A device, pipe or socket (/dev, a FIFO): reading one can block
            // for ever (a pipe) or never end (/dev/zero), freezing the screen.
            shell_toast("Not a file");
        } else if (is({".mp4", ".mkv", ".webm", ".mov", ".avi", ".m4v"})) {
            // Video plays on the HDMI monitor in Dock mode.
            if (hal().dock_open("video", full)) shell_toast("Playing on the monitor");
            else shell_toast("Videos play on a monitor: open Dock");
        } else if (is({".mp3", ".flac", ".ogg", ".wav", ".m4a", ".opus"})) {
            hal().play(full);
            shell_toast("Playing " + e.name);
        } else if (e.size < 64 * 1024) {
            // Notes would save an image or archive back as text (cut at the
            // first NUL), breaking it. Only open what reads as text.
            if (!looks_like_text(full)) {
                shell_toast("Not a text file");
                return;
            }
            if (!app_find("notes")) {
                shell_toast("Install Notes from Apps to open text files");
                return;
            }
            shell_open_app("notes", full);
        } else {
            shell_toast("Too large to open (" + human(e.size) + ")");
        }
    }

    lv_obj_t* root_ = nullptr;
    std::string cwd_;
    std::vector<Entry> entries_;
    ListView list_;
    bool show_hidden_ = false;
    // What Copy or Move marked ("" for nothing). Kept while Files is closed,
    // so another visit can paste it.
    static inline std::string clip_;
    static inline bool clip_move_ = false;
    Prompt prompt_ = Prompt::None;
    std::string renaming_, typed_;
    bool submitted_ = false;
};
}  // namespace

std::unique_ptr<App> make_files_app() { return std::make_unique<FilesApp>(); }

}  // namespace zs
