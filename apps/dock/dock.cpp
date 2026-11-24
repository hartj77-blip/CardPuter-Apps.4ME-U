// Dock: use an HDMI monitor as a desktop (labwc), with web browser, video,
// terminal and files there, while this screen shows the status.
//
// Not docked: Start Dock mode, the monitor resolution, and what happens when
// a monitor is plugged in. Docked: the Cardputer's keys type on the monitor,
// so this screen only shows status; leave from the monitor's top bar or menu
// (or Ctrl+Alt+Del there), or unplug the monitor.
#include <functional>
#include <vector>

#include "apps/common.h"
#include "ui/power.h"
#include "ui/list_view.h"
#include "ui/prefs.h"

namespace zs {
namespace {
using namespace theme;

struct Choice {
    const char* label;
    const char* value;
};
const std::vector<Choice> kOnHdmi = {{"Ask", "ask"}, {"Start dock", "auto"}, {"Do nothing", "off"}};
const std::vector<Choice> kRes = {{"720p", "720p"}, {"1080p", "1080p"}, {"Monitor's best", "auto"}};

class DockApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        rebuild();
    }

    bool on_key(uint32_t key) override { return !docked_ && list_.key(key); }

    void tick() override {
        if (++n_ % 5) return;
        DisplayStatus d = hal().display();
        if (d.dock_running != docked_ || d.hdmi_connected != hdmi_) rebuild();
        else refresh(d);
    }

private:
    void rebuild() {
        lv_obj_clean(root_);
        list_.reset();
        hint_ = nullptr;
        if (group) lv_group_remove_all_objs(group);
        DisplayStatus d = hal().display();
        docked_ = d.dock_running;
        hdmi_ = d.hdmi_connected;
        // Docked, the keys type on the monitor: the bar only says how to leave.
        if (docked_) shell_set_actions({{"Back", 0, "Esc", nullptr}});
        else shell_set_actions({{"Select", 0, "Enter", nullptr}, {"Change", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr}});

        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        // Not docked, the status is a short panel on top and the controls a
        // full-width list under it, so their names and values both fit.
        lv_obj_set_size(panel, 308, docked_ ? kBodyH - 8 : 60);
        if (!docked_) lv_obj_set_style_pad_ver(panel, 3, 0);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);

        headline_ = label(panel, "", &zs_font_16, docked_ ? kGood : kText);
        if (docked_) {
            monitor_ = kv_row(panel, "Monitor", "");
            mem_ = kv_row(panel, "Free memory", "");
        } else {
            // Monitor and memory on the headline's line, at the right.
            monitor_ = label(panel, "", &zs_font_12, kMuted);
            lv_obj_add_flag(monitor_, LV_OBJ_FLAG_IGNORE_LAYOUT);
            lv_obj_align(monitor_, LV_ALIGN_TOP_RIGHT, 0, 2);
            mem_ = nullptr;
        }
        if (docked_) {
            lv_obj_t* hint = label(panel,
                                   "The keyboard now types on the monitor.\n"
                                   "Leave from the monitor's top bar, or\n"
                                   "unplug the monitor.",
                                   &zs_font_12, kMuted);
            lv_obj_set_width(hint, 290);
        } else {
            hint_ = label(panel, "", &zs_font_12, kMuted);
            lv_label_set_long_mode(hint_, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(hint_, 290);
            build_controls();
        }
        refresh(d);
    }

    // The controls: a List screen (docs/UX.md in the OS). Start on Enter, and
    // two value rows that Left/Right (or Enter) step through.
    void build_controls() {
        list_.build(root_, 4, 65, kScreenW - 8, kBodyH - 65);
        list_.add(LV_SYMBOL_PLAY, "Start Dock mode", "", [] {
            if (!hal().display().hdmi_connected) {
                shell_toast("Plug in a monitor first");
                return;
            }
            hal().dock_start();
            shell_toast("Starting the desktop...");   // tick() redraws once it runs
        });
        choice_row("On plug-in", kOnHdmi, "dock.on_hdmi", "ask");
        choice_row("Size", kRes, "dock.resolution", "720p");
        list_.finish();
    }

    // A value row for a setting kept in prefs under `key`.
    void choice_row(const char* name, const std::vector<Choice>& choices, const char* key, const char* def) {
        std::string cur = prefs::get_str(key, def);
        std::vector<std::string> labels;
        int at = 0;
        for (size_t i = 0; i < choices.size(); ++i) {
            labels.push_back(choices[i].label);
            if (cur == choices[i].value) at = int(i);
        }
        const std::vector<Choice>* list = &choices;
        list_.add_choice(LV_SYMBOL_SETTINGS, name, std::move(labels), at,
                         [list, key](int i) { prefs::set_str(key, (*list)[size_t(i)].value); });
    }

    void refresh(const DisplayStatus& d) {
        SystemInfo s = hal().system();
        lv_label_set_text(headline_, docked_ ? LV_SYMBOL_OK " Docked" : hdmi_ ? "Monitor ready" : "No monitor");
        std::string mon = d.hdmi_connected ? (d.hdmi_mode.empty() ? "connected" : d.hdmi_mode) : "none";
        if (mem_) {
            lv_label_set_text(monitor_, mon.c_str());
            lv_label_set_text(mem_, fmt("%d MB", s.mem_avail_mb).c_str());
        } else {
            lv_label_set_text(monitor_, fmt("Monitor %s   %d MB free", mon.c_str(), s.mem_avail_mb).c_str());
        }
        if (hint_)
            lv_label_set_text(hint_, hdmi_ ? "Opens a desktop with web, video, terminal and files on the monitor."
                                           : "Plug a monitor into the HDMI port. A USB keyboard and mouse help.");
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t *headline_ = nullptr, *monitor_ = nullptr, *mem_ = nullptr, *hint_ = nullptr;
    ListView list_;
    bool docked_ = false, hdmi_ = false;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_dock_app() { return std::make_unique<DockApp>(); }

}  // namespace zs
