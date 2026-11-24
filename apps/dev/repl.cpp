// REPL: an interactive prompt for a language, in the Terminal app. Python,
// Lua, Node.js, a calculator (bc), SQLite and a shell. Languages that aren't
// installed say which package brings them.
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/kit/run.h"
#include "apps/kit/ui.h"
#include "ui/list_view.h"

namespace zs {
namespace {
using namespace theme;

struct Lang {
    const char* name;
    const char* command;   // what runs in the terminal
    const char* tool;      // the program, to check it is installed
    const char* package;   // Debian package with it
    const char* about;
};

const Lang kLangs[] = {
    {"Python", "python3", "python3", "python3", "python3"},
    {"Lua", "lua5.4", "lua5.4", "lua5.4", "Lua 5.4"},
    {"Node.js", "node", "node", "nodejs", "JavaScript"},
    {"Calculator", "bc -lq", "bc", "bc", "bc, with maths functions"},
    {"SQLite", "sqlite3 ~/Code/scratch.db", "sqlite3", "sqlite3", "~/Code/scratch.db"},
    {"Shell", "bash", "bash", "bash", "bash in ~/Code"},
};

class ReplApp : public App {
public:
    // A List screen (docs/UX.md in the OS): typing filters the languages.
    void build(lv_obj_t* root) override {
        shell_set_actions({{"Start", 0, "Enter", nullptr}});
        kit::title_row(root, "REPL");
        list_.build(root, 2, 20, kScreenW - 4, kit::body_h(root) - 20);
        for (const Lang& l : kLangs) {
            bool ok = kit::have(l.tool);
            list_.add(ok ? LV_SYMBOL_PLAY : LV_SYMBOL_DOWNLOAD, l.name,
                      ok ? l.about : std::string("not installed: ") + l.package, [&l] { start(l); });
        }
        list_.finish();
    }

    bool on_key(uint32_t key) override { return list_.key(key); }

private:
    static void start(const Lang& l) {
        if (!kit::have(l.tool)) {
            shell_toast(std::string("Install it with: sudo apt install ") + l.package);
            return;
        }
        std::string dir = home_dir() + "/Code";
        kit::make_dirs(dir);
        kit::terminal_run(l.command, dir);
    }

    ListView list_;
};
}  // namespace

std::unique_ptr<App> make_repl_app() { return std::make_unique<ReplApp>(); }

}  // namespace zs
