// Sync: Notes, Documents and Recordings, the same on your PC and phone
// (Syncthing; see docs/SYNC.md).
//
// Left: status. Right: a List screen (docs/UX.md in the OS) of requests to
// accept, folders, devices and actions. The menu (Tab, or Ctrl + the letter):
//   A  accept the request in focus (Enter too)
//   D  ignore the request, or stop syncing with the device (asks first); Del too
//   I  show my device ID: QR code + the ID, to add this Cardputer elsewhere
//   N  add a device by its ID      R  sync now
// Enter on a folder looks for changes now.
#include <algorithm>
#include <vector>

#include "apps/common.h"
#include "ui/list_view.h"
#include "ui/sync.h"

namespace zs {
namespace {
using namespace theme;

const char* mode_label(const std::string& m) {
    return m == "always" ? "Always on" : m == "manual" ? "Only when asked" : "While charging";
}

// The Runs row's values, short enough for the list; in the order Enter steps.
const char* const kModes[] = {"charging", "always", "manual"};
const char* const kModeShort[] = {"Charging", "Always", "Asked"};

class SyncApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        if (!hal().sync_running()) hal().sync_start();
        rebuild();
    }

    void tick() override {
        // Never rebuild under an open box or ID/QR overlay: it would vanish
        // mid-typing. Closing it sets dirty_, so the refresh comes then.
        if (input_) return;
        if (dirty_) {
            dirty_ = false;
            rebuild();
            return;
        }
        if (++n_ % 20) return;   // every 2 s; the device caches for 4 s
        SyncStatus s = hal().sync();
        if (signature(s) != sig_) rebuild();
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;
        if (key == LV_KEY_DEL) {   // not a letter: on a List screen letters filter
            remove_focused(true);
            return true;
        }
        if (list_.key(key)) {
            set_actions();   // the menu follows the row in focus
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (input_) {
            close_overlay();
            return true;
        }
        return false;
    }

private:
    // What a list row is: index into st_.requests, folders or devices.
    struct RowRef {
        enum Kind { Request, Folder, Device, Other } kind;
        size_t i;
    };

    RowRef focused_ref() const {
        int r = input_ ? -1 : list_.focused();
        return r >= 0 && r < int(refs_.size()) ? refs_[size_t(r)] : RowRef{RowRef::Other, 0};
    }

    void accept(size_t i) {
        if (i >= st_.requests.size()) return;
        SyncRequest r = st_.requests[i];
        bool ok = hal().sync_answer(r, true);
        shell_toast(!ok         ? std::string("Couldn't accept")
                    : r.is_folder ? "Syncing " + r.folder_label + " into ~/Sync"
                                  : "Now syncing with " + r.device_name);
        dirty_ = true;
    }

    void ignore(size_t i) {
        if (i >= st_.requests.size()) return;
        hal().sync_answer(st_.requests[i], false);
        shell_toast("Ignored");
        dirty_ = true;
    }

    void stop_syncing(const std::string& id, const std::string& name) {
        hal().sync_remove_device(id);
        shell_toast("Stopped syncing with " + name);
        dirty_ = true;
    }

    // Del: ignore a request at once, ask before stopping a device.
    void remove_focused(bool ask) {
        RowRef f = focused_ref();
        if (f.kind == RowRef::Request) {
            ignore(f.i);
        } else if (f.kind == RowRef::Device && f.i < st_.devices.size()) {
            std::string id = st_.devices[f.i].id, name = st_.devices[f.i].name;
            if (ask)
                shell_confirm("Stop syncing with " + name + "?", "Its folders stay on both devices.", "Stop syncing",
                              [this, id, name] { stop_syncing(id, name); });
            else
                stop_syncing(id, name);
        }
    }

    void sync_now_run() {
        sync_now();
        shell_toast("Syncing until up to date");
        dirty_ = true;
    }

    void set_actions() {
        RowRef f = focused_ref();
        std::string enter = f.kind == RowRef::Request  ? "Accept"
                            : f.kind == RowRef::Folder ? "Look for changes"
                            : f.kind == RowRef::Device ? "Details"
                                                       : "Select";
        std::vector<Action> a;
        a.push_back({enter, 0, "Enter", nullptr});
        a.push_back({"Accept", 'a', nullptr, [this, f] { accept(f.i); }, f.kind == RowRef::Request});
        a.push_back({"Show my device ID", 'i', nullptr, [this] { show_id(); }, st_.installed});
        a.push_back({"Add device by ID", 'n', nullptr, [this] { ask_id(); }, st_.installed});
        a.push_back({"Sync now", 'r', nullptr, [this] { sync_now_run(); }, st_.installed});
        if (f.kind == RowRef::Request) {
            a.push_back({"Ignore", 'd', nullptr, [this, f] { ignore(f.i); }});
        } else {
            bool dev = f.kind == RowRef::Device && f.i < st_.devices.size();
            Action d{"Stop syncing", 'd', nullptr, [this] { remove_focused(false); }, dev, true};
            if (dev) d.confirm = "Stop syncing with " + st_.devices[f.i].name + "?";
            a.push_back(d);
        }
        shell_set_actions(std::move(a));
    }

    static std::string signature(const SyncStatus& s) {
        std::string k = std::to_string(s.running) + std::to_string(s.ready) + std::to_string(sync_now_active());
        for (auto& f : s.folders) k += "|" + f.id + f.state + std::to_string(f.percent);
        for (auto& d : s.devices) k += "|" + d.id + (d.connected ? "+" : "-") + std::to_string(d.percent);
        for (auto& r : s.requests) k += "|" + r.device_id + r.folder_id;
        return k;
    }

    // One line for the top of the panel.
    std::string headline(uint32_t& color) const {
        color = kText;
        if (!st_.installed) return "Not installed";
        if (!st_.running) return "Sync off";
        if (!st_.ready) return "Starting...";
        for (auto& f : st_.folders)
            if (!f.error.empty() || f.state == "error") {
                color = kBad;
                return "Problem";
            }
        int need = 0, pct = 100;
        for (auto& f : st_.folders) {
            need += f.need_files;
            if (f.percent < pct) pct = f.percent;
            if (f.state == "syncing" || f.state == "scanning") need += need ? 0 : 1;
        }
        int online = 0;
        for (auto& d : st_.devices) online += d.connected;
        if (st_.devices.empty()) return "No devices yet";
        if (need) return fmt("Syncing %d%%", pct);
        if (!online) return "Waiting for devices";
        color = kGood;
        return LV_SYMBOL_OK " Up to date";
    }

    void rebuild() {
        st_ = hal().sync();
        sig_ = signature(st_);
        // Keep the highlighted row across refreshes (percentages change
        // every couple of seconds while syncing).
        int keep = input_ ? -1 : list_.focused();
        lv_obj_clean(root_);
        input_ = nullptr;
        refs_.clear();
        if (group) lv_group_remove_all_objs(group);
        set_actions();   // first: the screen is laid out above the bar

        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 144, kBodyH - 8);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);
        uint32_t color;
        std::string head = headline(color);
        label(panel, head.c_str(), &zs_font_16, color);
        if (st_.ready) {
            int online = 0;
            for (auto& d : st_.devices) online += d.connected;
            kv_row(panel, "Online", fmt("%d of %zu", online, st_.devices.size()).c_str());
            kv_row(panel, "ID", st_.device_id.substr(0, 7).c_str());
        }
        const char* hint = !st_.installed ? "Syncthing is missing: reinstall Sync in Apps."
                           : !st_.requests.empty() ? "Someone asks to sync: the request is at the top of the list."
                           : st_.devices.empty() ? "Add this Cardputer in Syncthing on your PC or phone: Show my device ID."
                           : "Notes, Documents and Recordings, the same everywhere.";
        lv_obj_t* h = label(panel, hint, &zs_font_12, st_.requests.empty() ? kMuted : kAccent);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(h, 130);

        list_.build(root_, 154, 4, kScreenW - 156, kBodyH - 8);
        for (size_t i = 0; i < st_.requests.size(); ++i) {
            const SyncRequest& r = st_.requests[i];
            std::string t = r.is_folder ? r.device_name + ": " + r.folder_label : r.device_name;
            int row = list_.add(r.is_folder ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_PLUS, t, "asks", [this, i] { accept(i); });
            list_.set_value_color(row, kAccent);
            refs_.push_back({RowRef::Request, i});
        }
        for (size_t i = 0; i < st_.folders.size(); ++i) {
            const SyncFolder& f = st_.folders[i];
            bool bad = !f.error.empty() || f.state == "error";
            std::string v = bad                               ? std::string("error")
                            : f.percent < 100 || f.need_files ? fmt("%d%%", f.percent)
                            : f.state == "scanning"           ? std::string("...")
                                                              : std::string(LV_SYMBOL_OK);
            int row = list_.add(LV_SYMBOL_DIRECTORY, f.label, v, [] {
                hal().sync_rescan();
                shell_toast("Looking for changes");
            });
            if (bad) list_.set_value_color(row, kBad);
            refs_.push_back({RowRef::Folder, i});
        }
        for (size_t i = 0; i < st_.devices.size(); ++i) {
            const SyncDevice& d = st_.devices[i];
            std::string v = !d.connected ? "off" : d.percent < 100 ? fmt("%d%%", d.percent) : "on";
            int row = list_.add(LV_SYMBOL_DRIVE, d.name, v, [this, i] {
                if (i < st_.devices.size())
                    shell_toast(st_.devices[i].name + ": " + (st_.devices[i].connected ? "online" : "offline") +
                                ", ID " + st_.devices[i].id.substr(0, 7));
            });
            list_.set_value_color(row, d.connected ? kGood : kMuted);
            refs_.push_back({RowRef::Device, i});
        }
        if (st_.installed) {
            list_.add(LV_SYMBOL_REFRESH, sync_now_active() ? "Syncing now..." : "Sync now", "", [this] { sync_now_run(); });
            list_.add(LV_SYMBOL_EYE_OPEN, "My device ID", "", [this] { show_id(); });
            list_.add(LV_SYMBOL_KEYBOARD, "Add device by ID", "", [this] { ask_id(); });
            std::string m = sync_mode();
            int at = m == "always" ? 1 : m == "manual" ? 2 : 0;
            list_.add_choice(LV_SYMBOL_SETTINGS, "Runs", {kModeShort[0], kModeShort[1], kModeShort[2]}, at,
                             [](int i) { set_sync_mode(kModes[i]); });
            for (int k = 0; k < 4; ++k) refs_.push_back({RowRef::Other, 0});
        }
        list_.set_empty_text("Starting...");
        list_.finish();
        if (keep > 0 && list_.size()) list_.focus(std::min(keep, list_.size() - 1));
        set_actions();   // again, for the row now in focus
    }

    lv_obj_t* overlay(int h) {
        input_ = lv_obj_create(root_);
        style_panel(input_);
        lv_obj_set_size(input_, 308, h);
        lv_obj_center(input_);
        lv_obj_remove_flag(input_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_border_color(input_, c(kAccent), 0);
        lv_obj_set_style_border_width(input_, 1, 0);
        return input_;
    }

    // QR code and the ID in four lines of two groups (XXXXXXX-XXXXXXX).
    void show_id() {
        if (st_.device_id.empty()) {
            shell_toast("Still starting; try again in a moment");
            return;
        }
        shell_set_actions({{"Close", 0, "Esc", nullptr}});
        lv_obj_t* o = overlay(kBodyH - 6);
        lv_obj_t* qr = lv_qrcode_create(o);
        lv_qrcode_set_size(qr, 112);
        lv_qrcode_set_dark_color(qr, c(0x000000));
        lv_qrcode_set_light_color(qr, c(0xffffff));
        lv_qrcode_update(qr, st_.device_id.c_str(), uint32_t(st_.device_id.size()));
        lv_obj_set_style_border_color(qr, c(0xffffff), 0);
        lv_obj_set_style_border_width(qr, 4, 0);
        lv_obj_align(qr, LV_ALIGN_LEFT_MID, 0, 0);
        std::string lines;
        for (size_t i = 0; i < st_.device_id.size(); i += 16)
            lines += st_.device_id.substr(i, 15) + "\n";
        lv_obj_t* t = label(o, "This Cardputer's ID", &zs_font_12, kMuted);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 128, 0);
        lv_obj_t* id = label(o, lines.c_str(), &zs_font_14, kText);
        lv_obj_align(id, LV_ALIGN_TOP_LEFT, 128, 18);
        lv_obj_t* hint = label(o, "Add it in Syncthing there.", &zs_font_12, kMuted);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, 160);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_LEFT, 128, 0);
        // Keep keys here until Esc.
        lv_obj_t* sink = lv_obj_create(o);
        lv_obj_set_size(sink, 1, 1);
        lv_obj_set_style_opa(sink, LV_OPA_TRANSP, 0);
        lv_group_add_obj(group, sink);
        lv_group_focus_obj(sink);
    }

    void ask_id() {
        shell_set_actions({{"Add", 0, "Enter", nullptr}, {"Back", 0, "Esc", nullptr}});
        lv_obj_t* o = overlay(74);
        label(o, "Device ID (from Syncthing: Actions > Show ID)", &zs_font_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(o);
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_width(ta, 288);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<SyncApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            self->close_overlay();
            if (!v.empty()) shell_toast(hal().sync_add_device(v, "") ? "Added; accept it on the other device too"
                                                                       : "That isn't a device ID");
            self->dirty_ = true;
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    void close_overlay() {
        if (!input_) return;
        lv_group_set_editing(group, false);
        lv_obj_t* in = input_;
        input_ = nullptr;
        lv_obj_delete_async(in);
        dirty_ = true;
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* input_ = nullptr;
    ListView list_;
    std::vector<RowRef> refs_;   // what each list row is, by the index add() gave it
    SyncStatus st_;
    std::string sig_;
    bool dirty_ = false;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_sync_app() { return std::make_unique<SyncApp>(); }

}  // namespace zs
