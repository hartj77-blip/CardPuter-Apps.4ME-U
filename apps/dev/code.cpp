// Code: your projects in ~/Code. Make one from a template, browse its files,
// edit them in micro (in the Terminal app) and run the project with one key.
//
//   Projects   Enter open      menu: N new project
//   Files      Enter edit      menu: R run, T terminal here, N new file, G Git
//              Backspace (with no filter) or Esc: up a folder, then back
//
// Both are List screens (docs/UX.md in the OS): typing filters them, so
// their actions are in the Tab menu (and on Ctrl + the letter).
//
// How Run runs a project: the command in its .zeroos-run file if it has one,
// otherwise what its files suggest (Makefile, Cargo.toml, main.py, ...).
#include <cstdint>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/dev/dev.h"
#include "apps/kit/run.h"
#include "apps/kit/ui.h"
#include "platform/keys.h"
#include "ui/list_view.h"

namespace zs {
namespace {
using namespace theme;

struct Template {
    const char* name;
    const char* about;
    std::vector<std::pair<const char*, const char*>> files;   // name, contents
};

const std::vector<Template>& templates() {
    static const std::vector<Template> t = {
        {"Python", "main.py, runs with python3",
         {{"main.py",
           "# Run in Code's menu (Ctrl+R) runs this; Enter on a file edits it.\n"
           "name = input(\"What's your name? \")\n"
           "print(f\"Hello, {name}! This is Python on ZeroOS.\")\n"}}},
        {"C", "main.c and a Makefile, builds with make",
         {{"main.c",
           "#include <stdio.h>\n\n"
           "int main(void) {\n"
           "    printf(\"Hello from C on ZeroOS!\\n\");\n"
           "    return 0;\n"
           "}\n"},
          {"Makefile",
           "CFLAGS ?= -Wall -O2\n\n"
           "main: main.c\n\t$(CC) $(CFLAGS) -o $@ $^\n\n"
           "run: main\n\t./main\n\n"
           "clean:\n\trm -f main\n\n"
           ".PHONY: run clean\n"},
          {".gitignore", "main\n"}}},
        {"Shell script", "main.sh, runs with bash",
         {{"main.sh",
           "#!/usr/bin/env bash\n"
           "set -euo pipefail\n"
           "echo \"Hello from $(hostname), up $(uptime -p | sed 's/up //').\"\n"}}},
        {"Lua", "main.lua, runs with lua5.4",
         {{"main.lua", "print(\"Hello from Lua on ZeroOS!\")\n"}}},
        {"Web page", "index.html, served on port 8000",
         {{"index.html",
           "<!doctype html>\n<meta charset=\"utf-8\">\n<title>Hello</title>\n"
           "<h1>Hello from a Cardputer</h1>\n"
           "<p>Run it from Code's menu (Ctrl+R) to serve this page, then open\n"
           "http://&lt;this device&gt;:8000 on your phone or PC.</p>\n"}}},
        {"Empty", "just a README",
         {{"README.md", "# New project\n"}}},
    };
    return t;
}

// A project or file name: letters, digits, - _ . and spaces; no path tricks.
std::string clean_name(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    std::string out;
    for (char ch : s) {
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                  ch == '-' || ch == '_' || ch == '.' || ch == ' ';
        out += ok ? ch : '-';
    }
    if (out.empty() || out[0] == '.') return "";
    return out;
}

const char* file_symbol(const std::string& name) {
    auto ext = name.substr(name.find_last_of('.') == std::string::npos ? name.size() : name.find_last_of('.'));
    if (ext == ".png" || ext == ".jpg" || ext == ".gif") return LV_SYMBOL_IMAGE;
    if (name == "Makefile" || ext == ".sh") return LV_SYMBOL_SETTINGS;
    return LV_SYMBOL_FILE;
}

class CodeApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        base_ = dev::projects_dir();
        kit::make_dirs(base_);
        std::string arg = shell_app_arg();
        if (!arg.empty() && kit::is_dir(arg)) {
            project_ = arg;
            show_files();
        } else {
            show_projects();
        }
    }

    void tick() override {
        if (!pending_submit_) return;
        pending_submit_ = false;
        submitted();
    }

    bool on_key(uint32_t key) override {
        if (page_ != Page::Projects && page_ != Page::Files && page_ != Page::NewKind) return false;   // typing
        if (list_.key(key)) return true;
        // Backspace with nothing to take off the filter: up a folder.
        if (page_ == Page::Files && !sub_.empty() && (key == LV_KEY_BACKSPACE || key == kKeyBackspace)) {
            on_back();
            return true;
        }
        return false;
    }

    bool on_back() override {
        switch (page_) {
            case Page::Projects: return false;
            case Page::Files:
                if (!sub_.empty()) {
                    auto slash = sub_.find_last_of('/');
                    sub_ = slash == std::string::npos ? "" : sub_.substr(0, slash);
                    show_files();
                } else {
                    show_projects();
                }
                return true;
            case Page::NewName: show_projects(); return true;
            case Page::NewKind: ask_project_name(); return true;
            case Page::NewFile: show_files(); return true;
        }
        return false;
    }

private:
    enum class Page { Projects, Files, NewName, NewKind, NewFile };

    void clear() {
        if (group) lv_group_remove_all_objs(group);
        list_.reset();
        lv_obj_clean(root_);
    }

    // A title and a list under it, `foot` px short of the bottom. Set the
    // screen's actions first: they decide the height.
    void list_page(const std::string& title, int foot = 0) {
        kit::title_row(root_, title);
        list_.build(root_, 2, 20, kScreenW - 4, kit::body_h(root_) - 20 - foot);
    }

    // ---- Projects
    void show_projects() {
        page_ = Page::Projects;
        project_.clear();
        sub_.clear();
        clear();
        shell_set_actions({{"Open", 0, "Enter", nullptr},
                           {"New project", 'n', nullptr, [this] { ask_project_name(); }}});
        list_page("Projects");
        for (auto& d : kit::list_dirs(base_))
            list_.add(LV_SYMBOL_DIRECTORY, d, dev::project_kind(base_ + "/" + d), [this, d] {
                project_ = base_ + "/" + d;
                sub_.clear();
                show_files();
            });
        list_.add(LV_SYMBOL_PLUS, "New project", "", [this] { ask_project_name(); });
        list_.finish();
    }

    // ---- Files of a project
    std::string here() const { return sub_.empty() ? project_ : project_ + "/" + sub_; }

    void show_files() {
        page_ = Page::Files;
        clear();
        std::vector<Action> a;
        a.push_back({"Edit", 0, "Enter", nullptr});
        if (!sub_.empty()) a.push_back({"Up", 0, "Bksp", nullptr});
        a.push_back({"Run", 'r', nullptr, [this] { run(); }});
        a.push_back({"Terminal here", 't', nullptr, [this] { kit::terminal_run("bash", here()); }});
        a.push_back({"New file", 'n', nullptr, [this] { ask_file_name(); }});
        a.push_back({"Git", 'g', nullptr, [this] {
                         if (!app_find("git")) shell_toast("Git is part of Dev Tools");
                         else shell_open_app("git", project_);
                     }});
        shell_set_actions(std::move(a));
        std::string name = project_.substr(project_.find_last_of('/') + 1);
        if (!sub_.empty()) name += "/" + sub_;
        list_page(name, 16);
        bool any = false;
        if (!sub_.empty()) list_.add(LV_SYMBOL_UP, "..", "", [this] { on_back(); });
        for (auto& d : kit::list_dirs(here())) {
            any = true;
            list_.add(LV_SYMBOL_DIRECTORY, d + "/", "", [this, d] {
                sub_ = sub_.empty() ? d : sub_ + "/" + d;
                show_files();
            });
        }
        for (auto& f : kit::list_files(here())) {
            any = true;
            list_.add(file_symbol(f), f, "", [this, f] { edit(here() + "/" + f); });
        }
        if (!any) list_.add(LV_SYMBOL_PLUS, "New file", "", [this] { ask_file_name(); });
        list_.finish();
        dev::RunPlan plan = dev::run_plan(project_);
        lv_obj_t* foot = label(root_, plan.command.empty() ? "No run command found (add .zeroos-run)"
                                                           : ("Runs: " + plan.command).c_str(),
                               &zs_font_12, kMuted);
        lv_obj_set_width(foot, kScreenW - 8);
        lv_label_set_long_mode(foot, LV_LABEL_LONG_DOT);
        lv_obj_align(foot, LV_ALIGN_BOTTOM_LEFT, 6, -2);
    }

    void run() {
        dev::RunPlan plan = dev::run_plan(project_);
        if (plan.command.empty()) {
            shell_toast("Nothing to run: add main.py, a Makefile or .zeroos-run");
            return;
        }
        if (!plan.tool.empty() && !kit::have(plan.tool)) {
            shell_toast(plan.tool + " is not installed (sudo apt install " + plan.package + ")");
            return;
        }
        kit::terminal_run(plan.command, project_);
    }

    void edit(const std::string& path) {
        std::string ed = dev::editor();
        std::string file = path.substr(path.find_last_of('/') + 1);
        kit::terminal_run(ed + " " + kit::quote(file), path.substr(0, path.find_last_of('/')));
    }

    // ---- New project: name, then template
    void ask_project_name() {
        page_ = Page::NewName;
        clear();
        kit::input_screen(root_, group, "Name the new project", "e.g. weather-station", "",
                          [this](const std::string& v) { typed_ = v; pending_submit_ = true; });
    }

    void ask_template() {
        page_ = Page::NewKind;
        clear();
        shell_set_actions({{"Create", 0, "Enter", nullptr}, {"Back", 0, "Esc", nullptr}});
        list_page("New project: " + new_name_);
        for (auto& t : templates())
            list_.add(LV_SYMBOL_FILE, t.name, t.about, [this, &t] { create_project(t); });
        list_.finish();
    }

    void create_project(const Template& t) {
        std::string dir = base_ + "/" + new_name_;
        if (!kit::make_dirs(dir)) {
            shell_toast("Could not make " + dir);
            return;
        }
        for (auto& [name, text] : t.files) {
            std::string p = dir + "/" + name;
            if (!kit::exists(p)) kit::write_file(p, text, std::string(name).find(".sh") != std::string::npos);
        }
        if (kit::simulated()) kit::make_dirs(dir + "/.git");   // as git init would
        else if (!kit::exists(dir + "/.git") && kit::have("git")) kit::capture({"git", "init", "-q"}, dir);
        project_ = dir;
        sub_.clear();
        show_files();
        shell_toast("Made " + new_name_);
    }

    // ---- New file in the current folder
    void ask_file_name() {
        page_ = Page::NewFile;
        clear();
        kit::input_screen(root_, group, "Name the new file", "e.g. sensors.py", "",
                          [this](const std::string& v) { typed_ = v; pending_submit_ = true; });
    }

    void submitted() {
        std::string name = clean_name(typed_);
        if (name.empty()) {
            shell_toast("Use letters, digits, - _ and .");
            return;
        }
        if (page_ == Page::NewName) {
            if (kit::exists(base_ + "/" + name)) {
                project_ = base_ + "/" + name;   // already there: just open it
                sub_.clear();
                show_files();
                return;
            }
            new_name_ = name;
            ask_template();
        } else if (page_ == Page::NewFile) {
            std::string p = here() + "/" + name;
            if (!kit::exists(p)) kit::write_file(p, "");
            show_files();
            edit(p);
        }
    }

    lv_obj_t* root_ = nullptr;
    Page page_ = Page::Projects;
    std::string base_, project_, sub_, new_name_, typed_;
    ListView list_;
    bool pending_submit_ = false;
};
}  // namespace

std::unique_ptr<App> make_code_app() { return std::make_unique<CodeApp>(); }

}  // namespace zs
