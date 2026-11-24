// Station: the operator's details and the radio hardware settings the RF apps
// share. A List screen: Enter edits a row (typing goes into it until Enter
// saves or Esc cancels), Left/Right turn a switch on or off.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#include "apps/common.h"
#include "aprs.h"
#include "ui/list_view.h"
#include "ui/prefs.h"
#include "apps/kit/look.h"

namespace zs {
namespace {
using namespace theme;

enum class Kind { Text, Number, Toggle };

struct Field {
    const char* label;
    const char* key;
    Kind kind;
    const char* fallback;   // shown when nothing is set
    const char* help;
};

const Field kFields[] = {
    {"Callsign", "rf.callsign", Kind::Text, "not set", "Your amateur callsign"},
    {"Grid locator", "rf.grid", Kind::Text, "not set", "Maidenhead, like FN31pr. Used for distances"},
    {"Frequency correction", "rf.ppm", Kind::Number, "0", "Dongle crystal error in ppm, -500 to 500"},
    {"Antenna power", "rf.biastee", Kind::Toggle, "0", "Bias tee: 4.5 V up the coax for an amplifier"},
    {"APRS frequency", "rf.aprs_khz", Kind::Number, "144390", "In kHz. 144390 Americas, 144800 Europe"},
    {"Rig model", "rf.rig_model", Kind::Number, "0", "Hamlib number (rigctl -l lists them). 0 = none"},
    {"Rig port", "rf.rig_port", Kind::Text, "/dev/ttyUSB0", "Serial device the rig is on"},
    {"Rig speed", "rf.rig_baud", Kind::Number, "0", "Baud rate. 0 = the model's default"},
};

class StationApp : public App {
public:
    void build(lv_obj_t* root) override {
        set_actions();
        title_ = label(root, "", &zs_font_14, kAccent);
        lv_obj_set_pos(title_, 4, 0);
        help_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_align(help_, LV_ALIGN_BOTTOM_LEFT, 4, -1);
        lv_obj_set_width(help_, kScreenW - 8);
        lv_label_set_long_mode(help_, LV_LABEL_LONG_DOT);

        list_.build(root, 4, 18, kScreenW - 8, kBodyH - 18 - 16);
        for (size_t i = 0; i < std::size(kFields); ++i) {
            const Field& f = kFields[i];
            if (f.kind == Kind::Toggle)
                list_.add_choice(nullptr, f.label, {"off", "on"}, prefs::get_int(f.key, 0) ? 1 : 0, [this, i](int v) {
                    toggle(int(i), v);
                });
            else
                list_.add(nullptr, f.label, "", [this, i] { edit(int(i)); });
        }
        list_.finish();
        refresh();
    }

    void tick() override {
        // The help line follows the focus.
        int f = list_.focused();
        if (f >= 0 && f != focused_) {
            focused_ = f;
            refresh();
        }
    }

    bool on_key(uint32_t key) override {
        if (editing_ < 0) return list_.key(key);
        const Field& f = kFields[editing_];
        if (key == LV_KEY_ENTER) {
            commit();
        } else if (key == LV_KEY_BACKSPACE) {
            if (!entry_.empty()) entry_.pop_back();
        } else if (key >= 32 && key < 127 && entry_.size() < 24) {
            char c = char(key);
            bool ok = f.kind == Kind::Number ? std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && entry_.empty())
                                             : c != ' ' && c != '=';
            if (ok) entry_ += c;
        } else if (key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            return true;   // stay on the row being typed into
        } else {
            return false;   // Tab (the menu), Esc and the shell's keys
        }
        refresh();
        return true;
    }

    bool on_back() override {
        if (editing_ < 0) return false;
        editing_ = -1;
        set_actions();
        refresh();
        return true;
    }

private:
    // A List screen, or a Text one while a row is being typed into.
    void set_actions() {
        if (editing_ >= 0) shell_set_actions({{"Save", 0, "Enter", nullptr}, {"Cancel", 0, "Esc", nullptr}});
        else shell_set_actions({{"Edit", 0, "Enter", nullptr}, {"Switch", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr}});
    }

    void toggle(int i, int v) {
        const Field& f = kFields[i];
        prefs::set_int(f.key, v);
        if (std::string(f.key) == "rf.biastee" && v) shell_toast("On from the next time an RF tool opens");
        refresh();
    }

    void edit(int i) {
        if (editing_ >= 0) return;
        const Field& f = kFields[i];
        editing_ = i;
        entry_ = f.kind == Kind::Number ? std::to_string(prefs::get_int(f.key, std::atoi(f.fallback))) : prefs::get_str(f.key, "");
        if (f.kind == Kind::Number && entry_ == "0") entry_.clear();
        set_actions();
        refresh();
    }

    void commit() {
        const Field& f = kFields[editing_];
        std::string key = f.key;
        if (f.kind == Kind::Number) {
            int v = std::atoi(entry_.c_str());
            if (key == "rf.ppm" && (v < -500 || v > 500)) { shell_toast("Between -500 and 500"); return; }
            if (key == "rf.aprs_khz" && (v < 144000 || v > 148000)) { shell_toast("Between 144000 and 148000 kHz"); return; }
            if ((key == "rf.rig_model" || key == "rf.rig_baud") && v < 0) { shell_toast("Can't be negative"); return; }
            prefs::set_int(key, v);
        } else {
            std::string v = entry_;
            if (key == "rf.callsign") {
                for (char& c : v) c = char(std::toupper(static_cast<unsigned char>(c)));
                if (!v.empty() && !zrf::looks_like_callsign(v)) { shell_toast("That doesn't look like a callsign"); return; }
            }
            if (key == "rf.grid" && !v.empty()) {
                double lat, lon;
                if (!zrf::grid_to_latlon(v, lat, lon)) { shell_toast("Use 4 or 6 characters, like FN31 or FN31pr"); return; }
            }
            prefs::set_str(key, v);
        }
        editing_ = -1;
        set_actions();
    }

    std::string shown(const Field& f) const {
        std::string key = f.key;
        if (f.kind == Kind::Toggle) return prefs::get_int(f.key, 0) ? "on" : "off";
        if (f.kind == Kind::Text) {
            std::string v = prefs::get_str(f.key, "");
            return v.empty() ? f.fallback : v;
        }
        int v = prefs::get_int(f.key, std::atoi(f.fallback));
        if (key == "rf.ppm") return fmt("%d ppm", v);
        if (key == "rf.aprs_khz") return fmt("%.3f MHz", v / 1000.0);
        if (key == "rf.rig_model") return v ? fmt("%d", v) : "none";
        if (key == "rf.rig_baud") return v ? fmt("%d", v) : "default";
        return fmt("%d", v);
    }

    void refresh() {
        for (size_t i = 0; i < std::size(kFields); ++i)
            if (kFields[i].kind != Kind::Toggle) list_.set_value(int(i), int(i) == editing_ ? entry_ + "_" : shown(kFields[i]));
        std::string call = prefs::get_str("rf.callsign", "");
        if (editing_ >= 0) lv_label_set_text(title_, (std::string("Editing: ") + kFields[editing_].label).c_str());
        else lv_label_set_text(title_, call.empty() ? "Station" : ("Station " + call).c_str());
        lv_label_set_text(help_, kFields[editing_ >= 0 ? editing_ : focused_].help);
    }

    lv_obj_t *title_ = nullptr, *help_ = nullptr;
    ListView list_;
    int editing_ = -1, focused_ = 0;
    std::string entry_;
};
}  // namespace

std::unique_ptr<App> make_station_app() { return std::make_unique<StationApp>(); }

}  // namespace zs
