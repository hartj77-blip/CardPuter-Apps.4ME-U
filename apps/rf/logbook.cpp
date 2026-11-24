// Logbook: amateur radio contacts, kept as ADIF in ~/Documents/logbook.adi
// so any logging program can read it (and Sync copies it off the device).
//
// The log is a List screen (typing filters it); New in its menu (Tab, N)
// starts a contact, filled in from what Spectrum is tuned to. Enter on a
// contact shows it, and Delete there asks first. Times are UTC.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "adif.h"
#include "apps/common.h"
#include "rf_state.h"
#include "aprs.h"
#include "ui/list_view.h"
#include "ui/prefs.h"

namespace zs {
namespace {
using namespace theme;

const size_t kShown = 40;   // newest contacts listed; the file keeps them all
const char* const kModes[] = {"USB", "LSB", "CW", "FM", "AM", "DIGI"};
enum { kCall, kFreq, kMode, kSent, kRcvd, kComment, kSave, kFormRows };
const char* const kLabels[] = {"Callsign", "Frequency MHz", "Mode", "Report sent", "Report received", "Comment", "Save contact"};

std::string log_path() { return home_dir() + "/Documents/logbook.adi"; }

class LogbookApp : public App {
public:
    ~LogbookApp() override { lv_async_call_cancel(rebuild_cb, this); }

    void build(lv_obj_t* root) override {
        root_ = root;
        load();
        rebuild();
    }

    bool on_key(uint32_t key) override {
        if (view_ == View::Detail) {
            if (key == 'd' || key == 'D' || key == LV_KEY_DEL) { ask_delete(); return true; }
            if (key == LV_KEY_ENTER) { view_ = View::Log; request_rebuild(); return true; }
            return false;
        }
        if (view_ == View::Form && editing_ >= 0) {
            if (key == LV_KEY_ENTER) {
                stop_editing();
            } else if (key == LV_KEY_BACKSPACE) {
                if (!entry().empty()) entry().pop_back();
            } else if (key >= 32 && key < 127 && entry().size() < (editing_ == kComment ? 60u : 14u)) {
                char c = char(key);
                if (editing_ == kCall) c = char(std::toupper(static_cast<unsigned char>(c)));
                bool ok = editing_ == kFreq ? std::isdigit(static_cast<unsigned char>(c)) || c == '.'
                          : editing_ == kComment ? true : c != ' ';
                if (ok) entry() += c;
            } else if (key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
                return true;   // stay on the line being typed into
            } else {
                return false;   // Tab (the menu), Esc and the shell's keys
            }
            refresh_form();
            return true;
        }
        return list_.key(key);   // Up, Down, numbers and the filter; Enter opens the row
    }

    bool on_back() override {
        if (view_ == View::Form && editing_ >= 0) { stop_editing(); return true; }
        if (view_ != View::Log) { view_ = View::Log; request_rebuild(); return true; }
        return false;
    }

private:
    enum class View { Log, Form, Detail };

    std::string& entry() { return form_[editing_]; }

    void load() {
        std::ifstream f(log_path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        log_ = zrf::adif_parse(ss.str());
    }

    // The whole file, written beside the old one and swapped in, so a power
    // cut can't leave half a logbook.
    bool save_all() {
        mkdir((home_dir() + "/Documents").c_str(), 0755);
        std::string tmp = log_path() + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f << zrf::adif_header();
            for (auto& q : log_) f << zrf::adif_record(q);
            if (!f.good()) return false;
        }
        return std::rename(tmp.c_str(), log_path().c_str()) == 0;
    }

    bool append(const zrf::Qso& q) {
        mkdir((home_dir() + "/Documents").c_str(), 0755);
        struct stat st {};
        bool fresh = stat(log_path().c_str(), &st) != 0 || st.st_size == 0;
        std::ofstream f(log_path(), std::ios::binary | std::ios::app);
        if (fresh) f << zrf::adif_header();
        f << zrf::adif_record(q);
        f.flush();
        return f.good();
    }

    void remove(int index) {
        if (index < 0 || index >= int(log_.size())) return;
        log_.erase(log_.begin() + index);
        shell_toast(save_all() ? "Contact deleted" : "Could not write the logbook");
    }

    void start_contact() {
        for (auto& s : form_) s.clear();
        double hz = g_rf_last_hz;
        if (hz > 0) form_[kFreq] = fmt("%.5f", hz / 1e6);
        // Spectrum's listening mode if it has one, else the usual mode there.
        std::string m = g_rf_last_mode.empty() && hz > 0 ? suggested(hz) : g_rf_last_mode;
        mode_ = m == "lsb" ? 1 : m == "cw" ? 2 : m == "nfm" || m == "wfm" ? 3 : m == "am" ? 4 : 0;
        form_[kSent] = form_[kRcvd] = mode_ == 2 ? "599" : "59";
        editing_ = kCall;   // straight into the callsign
        view_ = View::Form;
        request_rebuild();
    }

    void stop_editing() {
        editing_ = -1;
        set_actions();
        refresh_form();
    }

    void ask_delete() {
        if (showing_ < 0 || showing_ >= int(log_.size())) return;
        shell_confirm("Delete the contact with " + log_[size_t(showing_)].call + "?", "It is taken out of the logbook file.",
                      "Delete", [this] {
                          remove(showing_);
                          view_ = View::Log;
                          request_rebuild();
                      });
    }

    // What the keys do on each view.
    void set_actions() {
        std::vector<Action> a;
        if (view_ == View::Log) {
            a.push_back({"Show", 0, "Enter", nullptr});
            a.push_back({"New contact", 'n', nullptr, [this] { start_contact(); }});
        } else if (view_ == View::Form && editing_ >= 0) {
            a.push_back({"Done", 0, "Enter", nullptr});
            a.push_back({"Save contact", 's', nullptr, [this] { save_contact(); }});
        } else if (view_ == View::Form) {
            a.push_back({"Edit", 0, "Enter", nullptr});
            a.push_back({"Mode", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
            a.push_back({"Save contact", 's', nullptr, [this] { save_contact(); }});
            a.push_back({"Cancel", 0, "Esc", nullptr});
        } else {
            a.push_back({"Back", 0, "Enter", nullptr});
            a.push_back({"Delete", 'd', "D", [this] { ask_delete(); }});
        }
        shell_set_actions(std::move(a));
    }

    static std::string suggested(double hz) {
        // Same convention as Spectrum: lower sideband below 10 MHz, FM on VHF and up.
        if (hz >= 30e6) return hz >= 50e6 && hz < 54e6 ? "usb" : "nfm";
        return hz < 10e6 && !(hz > 5.3e6 && hz < 5.5e6) ? "lsb" : "usb";
    }

    void save_contact() {
        if (!zrf::looks_like_callsign(form_[kCall])) { shell_toast("Enter the other station's callsign"); return; }
        double mhz = std::atof(form_[kFreq].c_str());
        if (!form_[kFreq].empty() && (mhz < 0.1 || mhz > 250000)) { shell_toast("Frequency is in MHz, like 14.225"); return; }
        zrf::Qso q;
        q.call = form_[kCall];
        time_t t = time(nullptr);
        tm utc{};
        gmtime_r(&t, &utc);
        char buf[16];
        strftime(buf, sizeof buf, "%Y%m%d", &utc);
        q.date = buf;
        strftime(buf, sizeof buf, "%H%M%S", &utc);
        q.time = buf;
        q.freq_mhz = mhz;
        q.mode = kModes[mode_];
        q.rst_sent = form_[kSent];
        q.rst_rcvd = form_[kRcvd];
        q.comment = form_[kComment];
        q.my_call = prefs::get_str("rf.callsign", "");
        q.my_grid = prefs::get_str("rf.grid", "");
        if (!append(q)) { shell_toast("Could not write the logbook"); return; }
        log_.push_back(q);
        shell_toast("Logged " + q.call);
        view_ = View::Log;
        request_rebuild();
    }

    // Rows can't be rebuilt from inside a row's own click: do it just after.
    void request_rebuild() { lv_async_call(rebuild_cb, this); }
    static void rebuild_cb(void* self) { static_cast<LogbookApp*>(self)->rebuild(); }

    void rebuild() {
        // A fresh page each time: the actions first, so it fits above the bar.
        if (group) lv_group_remove_all_objs(group);
        lv_obj_clean(root_);
        list_.reset();
        set_actions();
        title_ = label(root_, "", &zs_font_14, kAccent);
        lv_obj_set_pos(title_, 4, 0);
        help_ = label(root_, "", &zs_font_12, kMuted);
        lv_obj_align(help_, LV_ALIGN_BOTTOM_LEFT, 4, -1);
        lv_obj_set_width(help_, kScreenW - 8);
        lv_label_set_long_mode(help_, LV_LABEL_LONG_DOT);

        if (view_ == View::Log) {
            list_.build(root_, 4, 18, kScreenW - 8, kBodyH - 18 - 16);
            list_.set_empty_text("No contacts yet");
            size_t first = log_.size() > kShown ? log_.size() - kShown : 0;
            for (size_t i = log_.size(); i-- > first;) {   // newest at the top
                const zrf::Qso& q = log_[i];
                std::string when = q.date.size() == 8 && q.time.size() >= 4
                                       ? q.date.substr(4, 2) + "-" + q.date.substr(6, 2) + " " + q.time.substr(0, 2) + ":" + q.time.substr(2, 2)
                                       : q.date;
                std::string band = q.freq_mhz > 0 ? fmt("%.3f ", q.freq_mhz) : "";
                int index = int(i);
                list_.add(nullptr, when + "  " + q.call, band + q.mode + "  " + q.rst_sent + "/" + q.rst_rcvd, [this, index] {
                    showing_ = index;
                    view_ = View::Detail;
                    request_rebuild();
                });
            }
            list_.finish();
            lv_label_set_text(title_, fmt("Logbook   %d contact%s", int(log_.size()), log_.size() == 1 ? "" : "s").c_str());
            std::string call = prefs::get_str("rf.callsign", "");
            lv_label_set_text(help_, log_.empty() ? "Log one with New in the menu. Times are UTC."
                                     : call.empty() ? "Set your callsign in Station so it goes in the log"
                                     : log_.size() > kShown ? "Showing the newest 40. The file holds them all."
                                     : "Saved as ADIF in Documents/logbook.adi");
        } else if (view_ == View::Form) {
            list_.build(root_, 4, 18, kScreenW - 8, kBodyH - 18 - 16);
            for (int i = 0; i < kFormRows; ++i) {
                if (i == kMode)
                    list_.add_choice(nullptr, kLabels[i], std::vector<std::string>(std::begin(kModes), std::end(kModes)), mode_, [this](int m) {
                        set_mode(m);
                    });
                else
                    list_.add(nullptr, kLabels[i], "", [this, i] { form_row(i); });
            }
            list_.finish();
            lv_label_set_text(title_, "New contact");
            refresh_form();
        } else {
            lv_obj_t* detail = lv_obj_create(root_);
            style_panel(detail);
            lv_obj_set_pos(detail, 4, 18);
            lv_obj_set_size(detail, kScreenW - 8, kBodyH - 18 - 2);
            lv_obj_t* text = label(detail, "", &zs_font_12, kText);
            lv_obj_set_width(text, kScreenW - 30);
            const zrf::Qso& q = log_[size_t(showing_)];
            std::string t = q.call + "\n";
            if (q.date.size() == 8) t += q.date.substr(0, 4) + "-" + q.date.substr(4, 2) + "-" + q.date.substr(6, 2);
            if (q.time.size() >= 4) t += "  " + q.time.substr(0, 2) + ":" + q.time.substr(2, 2) + " UTC";
            t += "\n";
            if (q.freq_mhz > 0) t += fmt("%.5f MHz  %s  ", q.freq_mhz, zrf::adif_band(q.freq_mhz).c_str());
            t += q.mode + "\nSent " + q.rst_sent + "   received " + q.rst_rcvd + "\n";
            if (!q.comment.empty()) t += q.comment + "\n";
            if (!q.my_call.empty()) t += "Logged as " + q.my_call + (q.my_grid.empty() ? "" : " in " + q.my_grid) + "\n";
            lv_label_set_text(text, t.c_str());
            lv_label_set_text(title_, "Contact");
            lv_label_set_text(help_, "");
        }
    }

    void refresh_form() {
        for (int i = 0; i < kFormRows; ++i) {
            if (i == kMode) continue;   // a value row: it shows its own
            std::string v = i == kSave ? LV_SYMBOL_OK : form_[i];
            if (i == editing_) v += "_";
            list_.set_value(i, v);
        }
        if (editing_ >= 0) {
            list_.focus(editing_);
            lv_label_set_text(help_, "");
            // Worked this station before? Say so while the callsign is typed.
            if (editing_ == kCall && form_[kCall].size() >= 3) {
                int n = 0;
                const zrf::Qso* last = nullptr;
                for (auto& q : log_)
                    if (q.call == form_[kCall]) { ++n; last = &q; }
                if (last) {
                    std::string d = last->date.size() == 8
                                        ? last->date.substr(0, 4) + "-" + last->date.substr(4, 2) + "-" + last->date.substr(6, 2)
                                        : last->date;
                    lv_label_set_text(help_, fmt("Worked %d time%s before, last %s %s", n, n == 1 ? "" : "s", d.c_str(),
                                                  zrf::adif_band(last->freq_mhz).c_str()).c_str());
                }
            }
        } else {
            lv_label_set_text(help_, "Fill in the lines, then Save contact.");
        }
    }

    void set_mode(int m) {
        mode_ = m;
        // Keep the customary report for the mode unless it was changed by hand.
        bool cw = mode_ == 2;
        for (int k : {int(kSent), int(kRcvd)})
            if (form_[k] == (cw ? "59" : "599")) form_[k] = cw ? "599" : "59";
        refresh_form();
    }

    void form_row(int i) {
        if (view_ != View::Form || editing_ >= 0) return;
        if (i == kSave) {
            save_contact();
            return;
        }
        editing_ = i;
        set_actions();
        refresh_form();
    }

    lv_obj_t *root_ = nullptr, *title_ = nullptr, *help_ = nullptr;
    ListView list_;
    std::vector<zrf::Qso> log_;
    View view_ = View::Log;
    std::string form_[kFormRows];
    int mode_ = 0, editing_ = -1, showing_ = -1;
};
}  // namespace

std::unique_ptr<App> make_logbook_app() { return std::make_unique<LogbookApp>(); }

}  // namespace zs
