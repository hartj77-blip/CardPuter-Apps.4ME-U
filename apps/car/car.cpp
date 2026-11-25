// Car: ways to play music through a car stereo (README.md).
//
//   USB stick mode   the Cardputer shows up as a USB stick holding ~/Music;
//                    the car plays the files and its own buttons work.
//   Bluetooth        the Cardputer plays (Jellyfin too) and the car, a
//                    speaker or headphones is the loudspeaker; the car's
//                    play and skip buttons drive the play queue.
//   From a phone     a phone sends its music to the Cardputer over
//                    Bluetooth, which plays it out of the headphone jack
//                    (into the car's aux) or on to a Bluetooth car. Calls
//                    stay on the phone. Its row shows the song; Enter
//                    pauses, the menu skips (phone_player.h).
//
// A List screen (docs/UX.md in the OS) with the focused row explained on the
// right. Enter turns USB stick mode on or off, or connects and disconnects a
// device; the menu (Tab) has Forget, which asks first (Del does the same).
#include "apps/common.h"
#include "ui/list_view.h"
#include "apps/kit/look.h"

#include <unistd.h>

#include <map>

#include "phone_player.h"

namespace zs {
namespace {
using namespace theme;

constexpr int kListW = 184;
namespace phone = car_phone;

class CarApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        shell_set_actions({{"Select", 0, "Enter", nullptr}});   // before laying out: the bar's room
        panel_ = lv_obj_create(root);
        style_panel(panel_);
        lv_obj_set_pos(panel_, kListW + 8, 6);
        lv_obj_set_size(panel_, kScreenW - kListW - 14, kBodyH - 12);
        lv_obj_remove_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);
        head_ = label(panel_, "", &zs_font_14, kText);
        lv_obj_set_width(head_, kScreenW - kListW - 26);
        lv_label_set_long_mode(head_, LV_LABEL_LONG_WRAP);
        help_ = label(panel_, "", &zs_font_12, kMuted);
        lv_obj_set_width(help_, kScreenW - kListW - 26);
        lv_label_set_long_mode(help_, LV_LABEL_LONG_WRAP);

        // Car's WirePlumber settings, if it hasn't loaded them yet (just
        // installed or updated): restarts it once.
        phone::apply_sound_settings();
        devices_ = hal().bt_devices();
        stick_ = hal().usb_stick();
        find_phone();
        poll_phone();
        rebuild(0);
    }

    void tick() override {
        if (++n_ % 10 == 0) {   // once a second
            UsbStickStatus st = hal().usb_stick();
            bool changed = st.state != stick_.state || st.percent != stick_.percent;
            stick_ = st;
            if (changed) {
                list_.set_value(0, usb_value());
                show_panel(focused());
                set_actions();
            }
        }
        if (n_ % 50 == 0) refresh_devices();   // a car connects by itself when it starts
        else if (n_ % 20 == 0 && !phone_addr_.empty() && poll_phone()) show_phone();
    }

    bool on_key(uint32_t key) override {
        if (key == LV_KEY_DEL) {   // not a letter: on a List screen letters filter
            int i = focused();
            if (can_forget(i)) {
                BtDevice d = rows_[size_t(i)].dev;
                shell_confirm("Forget " + d.name + "?", "Pair it again to use it.", "Forget", [this, d] { forget(d); });
            }
            return true;
        }
        if (list_.key(key)) {
            show_panel(focused());   // the panel and the menu follow the focus
            set_actions();
            return true;
        }
        return false;
    }

private:
    struct Row {
        enum Kind { Usb, Device, AddPhone, Scan, Visible } kind;
        BtDevice dev;
    };

    std::string usb_value() const {
        switch (stick_.state) {
            case UsbStickStatus::Off: return "Off";
            case UsbStickStatus::Building: return fmt("%d%%", stick_.percent);
            case UsbStickStatus::Ready: return "On";
            case UsbStickStatus::Attached: return "In use";
            case UsbStickStatus::Error: return "Failed";
        }
        return "";
    }

    bool can_forget(int i) const {
        return i >= 0 && i < int(rows_.size()) && rows_[size_t(i)].kind == Row::Device && rows_[size_t(i)].dev.paired;
    }

    // --- A phone's music. Which devices are phones is asked once each.
    bool is_phone(const std::string& address) {
        auto it = phone_.find(address);
        if (it != phone_.end()) return it->second;
        bool yes = phone::is_phone(address);
        phone_[address] = yes;
        return yes;
    }

    // The connected phone, if any.
    void find_phone() {
        std::string was = phone_addr_;
        phone_addr_.clear();
        for (auto& d : devices_)
            if (d.connected && is_phone(d.address)) {
                phone_addr_ = d.address;
                break;
            }
        if (phone_addr_ != was) {
            player_.clear();
            track_ = {};
        }
    }

    // What the phone plays. True if that changed.
    bool poll_phone() {
        if (phone_addr_.empty()) return false;
        player_ = phone::player_of(phone_addr_);
        phone::Info now = phone::read_player(player_);
        bool changed = now.status != track_.status || now.title != track_.title || now.artist != track_.artist;
        track_ = now;
        return changed;
    }

    std::string phone_value() const {
        if (track_.playing()) return "Playing";
        if (track_.status == "paused") return "Paused";
        return "On";
    }

    // The phone's row and the panel after a poll.
    void show_phone() {
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].kind == Row::Device && rows_[i].dev.address == phone_addr_) list_.set_value(int(i), phone_value());
        show_panel(focused());
        set_actions();
    }

    // Enter and the menu on the phone's row.
    void phone_key(phone::Key k) {
        if (player_.empty()) poll_phone();
        if (player_.empty()) {
            shell_toast("Start the music on the phone first");
            return;
        }
        phone::send(player_, k, track_.playing());
        usleep(300 * 1000);   // the phone says what it did a moment later
        poll_phone();
        show_phone();
    }

    bool is_connected_phone(int i) const {
        return i >= 0 && i < int(rows_.size()) && rows_[size_t(i)].kind == Row::Device &&
               rows_[size_t(i)].dev.connected && rows_[size_t(i)].dev.address == phone_addr_;
    }

    // Where a phone's music comes out: a Bluetooth car or speaker that is
    // connected too, else the headphone jack (or the speaker).
    std::string sound_goes_to() const {
        for (auto& d : devices_)
            if (d.connected && d.address != phone_addr_) {
                auto it = phone_.find(d.address);
                if (it == phone_.end() || !it->second) return d.name;
            }
        return "";
    }

    void forget(const BtDevice& d) {
        int i = focused();
        hal().bt_forget(d.address);
        shell_toast("Forgot " + d.name);
        phone_.erase(d.address);
        devices_ = hal().bt_devices();
        find_phone();
        rebuild(i);
    }

    // Enter's label says what it does to the row in focus; Forget only for
    // a paired device.
    void set_actions() {
        int i = focused();
        std::string enter = "Select";
        if (i >= 0 && i < int(rows_.size())) {
            const Row& r = rows_[size_t(i)];
            switch (r.kind) {
                case Row::Usb:
                    enter = stick_.state == UsbStickStatus::Off || stick_.state == UsbStickStatus::Error ? "Turn on"
                                                                                                       : "Turn off";
                    break;
                case Row::Device:
                    enter = is_connected_phone(i) ? (track_.playing() ? "Pause" : "Play")
                            : r.dev.connected     ? "Disconnect"
                            : r.dev.paired        ? "Connect"
                                                  : "Pair";
                    break;
                case Row::AddPhone: enter = "Be found"; break;
                case Row::Scan: enter = "Scan"; break;
                case Row::Visible: enter = "Be found"; break;
            }
        }
        std::vector<Action> a;
        a.push_back({enter, 0, "Enter", nullptr});
        if (is_connected_phone(i)) {
            BtDevice d = rows_[size_t(i)].dev;
            a.push_back({"Next song", 'n', nullptr, [this] { phone_key(phone::Key::Next); }});
            a.push_back({"Previous song", 'b', nullptr, [this] { phone_key(phone::Key::Previous); }});
            a.push_back({"Disconnect", 'x', nullptr, [this, d] { disconnect(d); }});
        }
        Action f{"Forget", 'd', nullptr, nullptr, can_forget(i), true};
        if (can_forget(i)) {
            BtDevice d = rows_[size_t(i)].dev;
            f.run = [this, d] { forget(d); };
            f.confirm = "Forget " + d.name + "?";
        } else {
            f.run = [] {};
        }
        a.push_back(f);
        shell_set_actions(std::move(a));
    }

    void rebuild(int focus_index) {
        lv_group_remove_all_objs(group);
        rows_.clear();
        // The list lives in a box of its own, so building it again replaces
        // the old one and leaves the panel be.
        if (box_) lv_obj_delete(box_);
        box_ = lv_obj_create(root_);
        style_plain(box_);
        lv_obj_set_pos(box_, 2, 4);
        lv_obj_set_size(box_, kListW + 2, kBodyH - 8);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
        list_.build(box_, 0, 0, kListW + 2, kBodyH - 8);
        add(LV_SYMBOL_USB, "USB stick mode", usb_value(), {Row::Usb, {}});
        for (auto& d : devices_) {
            bool ph = d.paired && is_phone(d.address);
            add(ph ? ZS_ICON_PHONE : d.connected ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_BLUETOOTH, d.name,
                d.connected ? (d.address == phone_addr_ ? phone_value() : "On") : d.paired ? "" : "new",
                {Row::Device, d});
        }
        add(LV_SYMBOL_PLUS, "Add a phone", "", {Row::AddPhone, {}});
        add(ZS_ICON_SEARCH, "Scan for devices", "", {Row::Scan, {}});
        add(LV_SYMBOL_EYE_OPEN, "Let the car find me", "", {Row::Visible, {}});
        list_.finish();
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].kind == Row::Device && rows_[i].dev.connected) list_.set_value_color(int(i), kGood);
        if (focus_index >= int(rows_.size())) focus_index = int(rows_.size()) - 1;
        if (focus_index >= 0) list_.focus(focus_index);
        show_panel(focused());
        set_actions();
    }

    void add(const char* symbol, const std::string& text, const std::string& value, Row row) {
        int i = int(rows_.size());
        rows_.push_back(std::move(row));
        // Deferred by the list: activating rebuilds it.
        list_.add(symbol, text, value, [this, i] { activate(i); });
    }

    int focused() const { return list_.focused(); }

    // The panel on the right explains the row in focus.
    void show_panel(int i) {
        if (!head_ || i < 0 || i >= int(rows_.size())) return;
        const Row& r = rows_[i];
        std::string head, help;
        uint32_t color = kText;
        switch (r.kind) {
            case Row::Usb:
                switch (stick_.state) {
                    case UsbStickStatus::Off:
                        head = "USB stick";
                        help = "The car plays ~/Music as if from a USB stick.\nSwitch on the left: Slave. Cable in the "
                               "right USB-C port.";
                        break;
                    case UsbStickStatus::Building:
                        head = fmt("Copying %d%%", stick_.percent);
                        help = "Making the stick from ~/Music. Only needed when the music changed.";
                        color = kWarn;
                        break;
                    case UsbStickStatus::Ready:
                        head = "Ready";
                        help = "Plug the right USB-C port into the car. Switch on the left: Slave.";
                        color = kGood;
                        break;
                    case UsbStickStatus::Attached:
                        head = "In use";
                        help = "The car is reading the stick. Use the car's buttons.";
                        color = kGood;
                        break;
                    case UsbStickStatus::Error:
                        head = "Failed";
                        help = stick_.error;
                        color = kBad;
                        break;
                }
                break;
            case Row::Device:
                head = r.dev.name;
                color = r.dev.connected ? kGood : kText;
                if (is_connected_phone(i)) {
                    std::string to = sound_goes_to();
                    std::string out = to.empty() ? "To the headphone jack." : "To " + to + ".";
                    if (!track_.title.empty()) {
                        head = track_.title;
                        help = (track_.artist.empty() ? "" : track_.artist + "\n") + "From " + r.dev.name + ". " + out +
                               "\nTab: skip.";
                    } else {
                        help = "Play music on the phone. " + out;
                    }
                } else if (r.dev.paired && is_phone(r.dev.address)) {
                    help = r.dev.connected ? "Connected." : "Your phone. Connect, then play music on it.";
                } else {
                    help = r.dev.connected  ? "Connected: sound goes here. Its play and skip buttons work."
                           : r.dev.paired   ? "Paired. Connect to send the sound here."
                                            : "Found nearby. Pairing connects it too.";
                }
                break;
            case Row::AddPhone:
                head = "Add a phone";
                help = "Shows this Cardputer for 2 min. On the phone: Bluetooth, pick cardputer.";
                break;
            case Row::Scan:
                head = "Scan";
                help = "Put the car, speaker or headphones in pairing mode first. Takes 8 seconds.";
                break;
            case Row::Visible:
                head = "Be found";
                help = "For cars that search for a phone: shows this Cardputer for 2 minutes. Pick it on the car's screen.";
                break;
        }
        lv_label_set_text(head_, head.c_str());
        lv_obj_set_style_text_color(head_, c(color), 0);
        lv_label_set_text(help_, help.c_str());
        lv_obj_update_layout(head_);
        lv_obj_set_pos(help_, 0, lv_obj_get_height(head_) + 4);
    }

    void refresh_devices() {
        std::vector<BtDevice> now = hal().bt_devices();
        bool same = now.size() == devices_.size();
        for (size_t i = 0; same && i < now.size(); ++i)
            same = now[i].address == devices_[i].address && now[i].connected == devices_[i].connected &&
                   now[i].paired == devices_[i].paired;
        if (same) return;
        devices_ = std::move(now);
        find_phone();
        poll_phone();
        rebuild(focused());
    }

    // These wait on Bluetooth; say so before the screen stops updating.
    void busy(const std::string& what) {
        shell_toast(what);
        lv_refr_now(nullptr);
    }

    void activate(int i) {
        if (i < 0 || i >= int(rows_.size())) return;
        const Row r = rows_[i];   // a copy: rebuilding replaces rows_
        switch (r.kind) {
            case Row::Usb:
                if (stick_.state == UsbStickStatus::Off || stick_.state == UsbStickStatus::Error) {
                    hal().usb_stick_start();
                    shell_toast("USB stick mode on");
                } else {
                    hal().usb_stick_stop();
                    shell_toast("USB stick mode off");
                }
                n_ = 5;   // look at the result soon
                return;
            case Row::Device:
                if (is_connected_phone(i)) {
                    phone_key(phone::Key::PlayPause);
                    return;
                }
                if (r.dev.connected) {
                    busy("Disconnecting...");
                    hal().bt_disconnect(r.dev.address);
                    shell_toast("Disconnected " + r.dev.name);
                } else {
                    busy(r.dev.paired ? "Connecting..." : "Pairing...");
                    std::string error;
                    if (!hal().bt_connect(r.dev.address, error)) shell_toast(error);
                    else if (is_phone(r.dev.address)) shell_toast("Connected. Play music on " + r.dev.name);
                    else shell_toast("Sound now goes to " + r.dev.name);
                }
                break;
            case Row::AddPhone:
                hal().bt_set_visible(120);
                shell_toast("Visible as \"cardputer\" for 2 minutes");
                return;
            case Row::Scan:
                busy("Scanning for 8 seconds...");
                hal().bt_scan();
                break;
            case Row::Visible:
                hal().bt_set_visible(120);
                shell_toast("Visible as \"cardputer\" for 2 minutes");
                return;
        }
        devices_ = hal().bt_devices();
        find_phone();
        poll_phone();
        rebuild(i);
    }

    void disconnect(const BtDevice& d) {
        int i = focused();
        busy("Disconnecting...");
        hal().bt_disconnect(d.address);
        shell_toast("Disconnected " + d.name);
        devices_ = hal().bt_devices();
        find_phone();
        rebuild(i);
    }

    lv_obj_t* root_ = nullptr;
    ListView list_;
    lv_obj_t* box_ = nullptr;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* head_ = nullptr;
    lv_obj_t* help_ = nullptr;
    std::vector<Row> rows_;
    std::vector<BtDevice> devices_;
    std::map<std::string, bool> phone_;   // address: sends music (a phone)
    std::string phone_addr_;              // the connected phone, or ""
    std::string player_;                  // its player object, or ""
    phone::Info track_;                   // what it plays
    UsbStickStatus stick_;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_car_app() { return std::make_unique<CarApp>(); }

}  // namespace zs
