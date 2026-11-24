// UI pieces shared by the app packs (Dev Tools, Games): a one-line "type
// something" screen, a scrolling text panel for streamed command output, a
// title row with key hints, and running a command in the Terminal app.
#pragma once

#include <functional>
#include <string>

#include "lvgl.h"

namespace zs::kit {

// A one-line prompt (a Text screen, docs/UX.md in the OS). It sets the
// screen's actions (Enter `ok`, Esc Back), so the soft-key bar says what the
// keys do. on_submit(text) is called from the textarea's Enter; keep it light
// (set a flag, rebuild on the next tick) so no widget is deleted inside its
// own event. `help` is an optional line of explanation under the box (not key
// hints: the bar has those). Returns the page object.
lv_obj_t* input_screen(lv_obj_t* root, lv_group_t* group, const char* title, const char* placeholder,
                       const std::string& initial, std::function<void(const std::string&)> on_submit,
                       const char* help = nullptr, const char* ok = "OK");

// The title (accent, left) on the top row of an app's page, and an optional
// muted note on the right (a state such as "3 found", never key hints: the
// soft-key bar has those). Returns the title label.
lv_obj_t* title_row(lv_obj_t* root, const std::string& title, const std::string& note = "");

// The height of an app's area as it is now: kBodyH under the soft-key bar,
// kContentH without one. Set the screen's actions before building.
int body_h(lv_obj_t* root);

// A scrolling text view that fills the app area below `top` (down to
// `bottom_gap` above the bottom, or above the soft-key bar when the screen
// has actions: set them before build()). append() adds a line, auto-scrolls to the
// bottom and caps the backlog.
class OutputView {
public:
    void build(lv_obj_t* root, int top, int bottom_gap = 2);
    void append(const std::string& line);
    void clear();
    void set_placeholder(const std::string& text);
    bool empty() const { return lines_ == 0; }

private:
    void render();
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* label_ = nullptr;
    lv_obj_t* placeholder_ = nullptr;
    std::string text_;
    int lines_ = 0;
};

// Run a shell command in the Terminal app (tmux on the built-in screen), in
// `cwd` if given. False (and a toast) if the Terminal app isn't installed.
bool terminal_run(const std::string& command, const std::string& cwd = "");

}  // namespace zs::kit
