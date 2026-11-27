// Car: ways to play music through a car stereo (README.md).
//
//   USB stick mode   the Cardputer shows up as a USB stick holding ~/Music;
//                    the car plays the files and its own buttons work.
//   USB live sound   a USB stick whose songs are whatever plays here now
//                    (a phone's music, Media, Jellyfin), a few seconds
//                    behind; the car's skip and pause reach the player.
//   iPod mode        the same sound without the delay, to a stereo that
//                    takes an iPod on its USB socket.
//   Bluetooth        the Cardputer plays (Jellyfin too) and the car, a
//                    speaker or headphones is the loudspeaker; the car's
//                    play and skip buttons drive the play queue.
//   From a phone     a phone sends its music to the Cardputer over
//                    Bluetooth, which plays it out of the headphone jack
//                    (into the car's aux) or on to a Bluetooth car. Calls
//                    stay on the phone (phone_player.h).
//
// A List screen (docs/UX.md in the OS) with the focused row explained on the
// right. While a phone is connected, a "now playing" card runs across the
// top (the song, the artist and the phone, how far in), the phone's row
// comes first, and Enter on it plays and pauses; the menu skips.
// Otherwise Enter turns a USB mode on or off (one at a time: they share the
// one USB device port), or connects and disconnects a device; the menu (Tab) has Forget, which asks first (Del
// does the same).
#include "apps/common.h"
#include "ui/list_view.h"
#include "apps/kit/look.h"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <map>

#include "phone_player.h"

namespace zs {
namespace {
using namespace theme;

constexpr int kListW = 184;
constexpr int kNowH = 40;   // the now playing card
namespace phone = car_phone;

// The USB modes. zeroos-usb-stick.service (files/zeroos-usb-stick) sets up
// the one named in ~/.config/zeroos/car-usb and says which is on in
// /run/zeroos/usb-stick.mode.
enum class UsbMode { Files, Live, Ipod };
constexpr UsbMode kUsbModes[] = {UsbMode::Files, UsbMode::Live, UsbMode::Ipod};
const char* usb_word(UsbMode m) { return m == UsbMode::Live ? "live" : m == UsbMode::Ipod ? "ipod" : "files"; }
const char* usb_name(UsbMode m) {
    return m == UsbMode::Live ? "USB live sound" : m == UsbMode::Ipod ? "iPod mode" : "USB stick mode";
}
// Its row: short, so "In use" fits beside it.
const char* usb_row(UsbMode m) { return m == UsbMode::Live ? "USB live" : m == UsbMode::Ipod ? "USB iPod" : "USB stick"; }
UsbMode usb_from(const std::string& word) {
    for (UsbMode m : kUsbModes)
        if (word == usb_word(m)) return m;
    return UsbMode::Files;
}

std::string first_word(const std::string& path) {
    std::ifstream f(path);
    std::string w;
    f >> w;
    return w;
}

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
        build_now(root);

        // Car's WirePlumber settings, if it hasn't loaded them yet (just
        // installed or updated): restarts it once.
        phone::apply_sound_settings();
        devices_ = hal().bt_devices();
        asked_ = usb_from(first_word(usb_mode_path()));
        stick_ = hal().usb_stick();
        mode_ = usb_mode();
        find_phone();
        poll_phone();
        rebuild(0);
    }

    void tick() override {
        if (++n_ % 10 == 0) {   // once a second
            UsbStickStatus st = hal().usb_stick();
            UsbMode mode = usb_mode();
            bool changed = st.state != stick_.state || st.percent != stick_.percent || mode != mode_;
            stick_ = st;
            mode_ = mode;
            if (changed) {
                for (size_t i = 0; i < rows_.size(); ++i)
                    if (rows_[i].kind == Row::Usb) list_.set_value(int(i), usb_value(rows_[i].usb));
                show_now();   // where the sound goes
                show_panel(focused());
                set_actions();
            }
            show_time();   // the song's clock runs between polls
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
        UsbMode usb = UsbMode::Files;   // Kind::Usb: which mode
    };

    // --- The USB modes. One is on at a time.
    static std::string usb_mode_path() { return home_dir() + "/.config/zeroos/car-usb"; }

    // The mode the service is in; without it (the simulator), the one last asked for.
    UsbMode usb_mode() const {
        std::string on = first_word("/run/zeroos/usb-stick.mode");
        return on.empty() ? asked_ : usb_from(on);
    }

    // The service's state as far as this mode goes: Off unless it is the one that is on.
    UsbStickStatus::State usb_state(UsbMode m) const { return m == mode_ ? stick_.state : UsbStickStatus::Off; }

    std::string usb_value(UsbMode m) const {
        switch (usb_state(m)) {
            case UsbStickStatus::Off: return "Off";
            case UsbStickStatus::Building: return m == UsbMode::Files ? fmt("%d%%", stick_.percent) : "...";
            case UsbStickStatus::Ready: return "On";
            case UsbStickStatus::Attached: return "In use";
            case UsbStickStatus::Error: return "Failed";
        }
        return "";
    }

    bool usb_on(UsbMode m) const {
        UsbStickStatus::State s = usb_state(m);
        return s != UsbStickStatus::Off && s != UsbStickStatus::Error;
    }

    // Turns this mode on (and whichever was on, off), or off if it is on.
    void usb_toggle(UsbMode m) {
        if (usb_on(m)) {
            hal().usb_stick_stop();
            shell_toast(std::string(usb_name(m)) + " off");
        } else {
            if (stick_.state != UsbStickStatus::Off) hal().usb_stick_stop();
            std::string dir = home_dir() + "/.config";
            mkdir(dir.c_str(), 0755);
            mkdir((dir + "/zeroos").c_str(), 0755);
            std::ofstream(usb_mode_path()) << usb_word(m) << "\n";
            asked_ = m;
            hal().usb_stick_start();
            shell_toast(std::string(usb_name(m)) + " on");
        }
        n_ = 5;   // look at the result soon
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
        track_at_ = std::chrono::steady_clock::now();
        return changed;
    }

    // Short, so the phone's name keeps its room: the card says the rest.
    std::string phone_value() const {
        if (track_.playing()) return LV_SYMBOL_PLAY;
        if (track_.status == "paused") return LV_SYMBOL_PAUSE;
        return "On";
    }

    // The phone's row, the card and the panel after a poll.
    void show_phone() {
        show_now();
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

    // Where a phone's music comes out: the car over USB while USB live
    // sound is on, a Bluetooth car or speaker that is connected too, else
    // the headphone jack (or the speaker).
    std::string sound_goes_to() const {
        if (usb_on(UsbMode::Live) || usb_on(UsbMode::Ipod)) return "USB";
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
                case Row::Usb: enter = usb_on(r.usb) ? "Turn off" : "Turn on"; break;
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

    // Rows move (a phone that connects goes to the top), so focus follows
    // the row that had it, or the phone when it has just connected.
    void rebuild(int focus_index) {
        std::string keep;
        int f = focused();
        if (f >= 0 && f < int(rows_.size())) keep = row_key(rows_[size_t(f)]);
        if (!phone_addr_.empty() && phone_addr_ != shown_phone_) keep = "d" + phone_addr_;
        shown_phone_ = phone_addr_;
        lv_group_remove_all_objs(group);
        rows_.clear();
        // The list lives in a box of its own, so building it again replaces
        // the old one and leaves the panel be.
        if (box_) lv_obj_delete(box_);
        // With a phone connected the now playing card takes the top.
        int top = phone_addr_.empty() ? 4 : 4 + kNowH + 4;
        box_ = lv_obj_create(root_);
        style_plain(box_);
        lv_obj_set_pos(box_, 2, top);
        lv_obj_set_size(box_, kListW + 2, kBodyH - top - 4);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
        list_.build(box_, 0, 0, kListW + 2, kBodyH - top - 4);
        lv_obj_set_pos(panel_, kListW + 8, top + 2);
        lv_obj_set_size(panel_, kScreenW - kListW - 14, kBodyH - top - 8);
        auto add_device = [this](const BtDevice& d) {
            bool ph = d.paired && is_phone(d.address);
            add(ph ? ZS_ICON_PHONE : d.connected ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_BLUETOOTH, d.name,
                d.connected ? (d.address == phone_addr_ ? phone_value() : "On") : d.paired ? "" : "new",
                {Row::Device, d});
        };
        // The connected phone first: it's what you came for.
        for (auto& d : devices_)
            if (d.connected && d.address == phone_addr_) add_device(d);
        for (UsbMode m : kUsbModes) add(LV_SYMBOL_USB, usb_row(m), usb_value(m), {Row::Usb, {}, m});
        for (auto& d : devices_)
            if (!(d.connected && d.address == phone_addr_)) add_device(d);
        add(LV_SYMBOL_PLUS, "Add a phone", "", {Row::AddPhone, {}});
        add(ZS_ICON_SEARCH, "Scan for devices", "", {Row::Scan, {}});
        add(LV_SYMBOL_EYE_OPEN, "Let the car find me", "", {Row::Visible, {}});
        list_.finish();
        for (size_t i = 0; i < rows_.size(); ++i)
            if (rows_[i].kind == Row::Device && rows_[i].dev.connected) list_.set_value_color(int(i), kGood);
        for (size_t i = 0; i < rows_.size(); ++i)
            if (!keep.empty() && row_key(rows_[i]) == keep) focus_index = int(i);
        if (focus_index >= int(rows_.size())) focus_index = int(rows_.size()) - 1;
        if (focus_index >= 0) list_.focus(focus_index);
        show_now();
        show_panel(focused());
        set_actions();
    }

    // --- The now playing card: a row across the top while a phone is
    // connected. Its state on the left, the song in the big font, then the
    // artist and the phone, how far in, and a thin bar for that.
    void build_now(lv_obj_t* root) {
        now_ = lv_obj_create(root);
        style_panel(now_);
        lv_obj_set_style_pad_all(now_, 0, 0);
        lv_obj_set_pos(now_, 2, 4);
        lv_obj_set_size(now_, kScreenW - 4, kNowH);
        lv_obj_remove_flag(now_, LV_OBJ_FLAG_SCROLLABLE);
        now_icon_ = label(now_, LV_SYMBOL_PAUSE, &zs_font_20, kMuted);
        lv_obj_set_pos(now_icon_, 8, 9);
        constexpr int x = 34, w = kScreenW - 4 - x - 8;
        now_title_ = label(now_, "", &zs_font_14, kText);
        lv_obj_set_pos(now_title_, x, 3);
        lv_obj_set_width(now_title_, w - 70);
        lv_label_set_long_mode(now_title_, LV_LABEL_LONG_DOT);
        now_time_ = label(now_, "", &zs_font_12, kMuted);
        lv_obj_set_width(now_time_, 68);
        lv_obj_set_style_text_align(now_time_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(now_time_, x + w - 68, 4);
        now_sub_ = label(now_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(now_sub_, x, 20);
        lv_obj_set_width(now_sub_, w);
        lv_label_set_long_mode(now_sub_, LV_LABEL_LONG_DOT);
        now_bar_ = lv_bar_create(now_);   // along the card's bottom edge
        lv_obj_set_pos(now_bar_, 0, kNowH - 2);
        lv_obj_set_size(now_bar_, kScreenW - 4, 2);
        lv_bar_set_range(now_bar_, 0, 1000);
        lv_obj_set_style_radius(now_bar_, 0, 0);
        lv_obj_set_style_radius(now_bar_, 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(now_bar_, c(kit::look::kGrey3), 0);
        lv_obj_set_style_bg_color(now_bar_, c(kGood), LV_PART_INDICATOR);
        lv_obj_add_flag(now_, LV_OBJ_FLAG_HIDDEN);
    }

    std::string phone_name() const {
        for (auto& d : devices_)
            if (d.address == phone_addr_) return d.name;
        return "Phone";
    }

    void show_now() {
        if (!now_) return;
        if (phone_addr_.empty()) {
            lv_obj_add_flag(now_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        lv_obj_remove_flag(now_, LV_OBJ_FLAG_HIDDEN);
        bool playing = track_.playing();
        lv_label_set_text(now_icon_, playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
        lv_obj_set_style_text_color(now_icon_, c(playing ? kGood : kMuted), 0);
        std::string sub;
        if (track_.title.empty()) {
            lv_label_set_text(now_title_, phone_name().c_str());
            sub = "Connected. Play music on the phone.";
        } else {
            lv_label_set_text(now_title_, track_.title.c_str());
            sub = (track_.artist.empty() ? "" : track_.artist + "  \u00b7  ") + phone_name();
        }
        std::string to = sound_goes_to();
        sub += to.empty() ? "" : "  " LV_SYMBOL_RIGHT " " + to;
        lv_label_set_text(now_sub_, sub.c_str());
        show_time();
    }

    static std::string clock(int ms) {
        int s = ms / 1000;
        return fmt("%d:%02d", s / 60, s % 60);
    }

    // How far into the song: where the phone last said, plus the time since
    // if it's playing. Hidden when the phone doesn't say.
    void show_time() {
        if (!now_ || phone_addr_.empty()) return;
        int pos = track_.position_ms, len = track_.duration_ms;
        if (pos >= 0 && track_.playing())
            pos += int(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - track_at_)
                           .count());
        if (len > 0 && pos > len) pos = len;
        bool known = pos >= 0 && len > 0 && !track_.title.empty();
        lv_label_set_text(now_time_, known ? (clock(pos) + " / " + clock(len)).c_str() : "");
        if (known) {
            lv_obj_remove_flag(now_bar_, LV_OBJ_FLAG_HIDDEN);
            lv_bar_set_value(now_bar_, int(int64_t(pos) * 1000 / len), LV_ANIM_OFF);
        } else {
            lv_obj_add_flag(now_bar_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void add(const char* symbol, const std::string& text, const std::string& value, Row row) {
        int i = int(rows_.size());
        rows_.push_back(std::move(row));
        // Deferred by the list: activating rebuilds it.
        list_.add(symbol, text, value, [this, i] { activate(i); });
    }

    int focused() const { return list_.focused(); }

    static std::string row_key(const Row& r) {
        if (r.kind == Row::Usb) return std::string("u") + usb_word(r.usb);
        return r.kind == Row::Device ? "d" + r.dev.address : std::to_string(int(r.kind));
    }

    // The panel on the right explains the row in focus.
    void show_panel(int i) {
        if (!head_ || i < 0 || i >= int(rows_.size())) return;
        const Row& r = rows_[i];
        std::string head, help;
        uint32_t color = kText;
        switch (r.kind) {
            case Row::Usb: {
                // What the panel says in each state, for each mode.
                bool live = r.usb == UsbMode::Live, ipod = r.usb == UsbMode::Ipod;
                switch (usb_state(r.usb)) {
                    case UsbStickStatus::Off:
                        head = usb_name(r.usb);
                        help = ipod   ? "The car takes this for an iPod: no delay."
                               : live ? "The car plays what plays here, a few seconds late."
                                      : "The car plays ~/Music as if from a USB stick.";
                        help += "\nSwitch on the left: Slave. Cable in the right USB-C port.";
                        break;
                    case UsbStickStatus::Building:
                        head = live || ipod ? "Starting" : fmt("Copying %d%%", stick_.percent);
                        help = live || ipod ? "Getting the sound ready."
                                            : "Making the stick from ~/Music. Only needed when the music changed.";
                        color = kWarn;
                        break;
                    case UsbStickStatus::Ready:
                        head = "Ready";
                        help = ipod   ? "Plug the right USB-C port into the car and pick iPod or USB on it. Switch on the "
                                        "left: Slave."
                               : live ? "Plug the right USB-C port into the car and pick USB on it. Switch on the left: "
                                        "Slave."
                                      : "Plug the right USB-C port into the car. Switch on the left: Slave.";
                        color = kGood;
                        break;
                    case UsbStickStatus::Attached:
                        head = "In use";
                        help = ipod   ? "The car has taken it up. Sound here goes to the car, and its buttons work."
                               : live ? "Play \"Live sound\" on the car. Its skip and pause buttons work; sound here "
                                        "goes to the car."
                                      : "The car is reading the stick. Use the car's buttons.";
                        color = kGood;
                        break;
                    case UsbStickStatus::Error:
                        head = "Failed";
                        help = stick_.error;
                        color = kBad;
                        break;
                }
                break;
            }
            case Row::Device:
                head = r.dev.name;
                color = r.dev.connected ? kGood : kText;
                if (is_connected_phone(i)) {
                    // The card above has the song and where it goes; this
                    // says what the keys do.
                    help = "Enter  play, pause\nCtrl+N  next\nCtrl+B  back";
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
                usb_toggle(r.usb);
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
    lv_obj_t* now_ = nullptr;
    lv_obj_t* now_icon_ = nullptr;
    lv_obj_t* now_title_ = nullptr;
    lv_obj_t* now_sub_ = nullptr;
    lv_obj_t* now_time_ = nullptr;
    lv_obj_t* now_bar_ = nullptr;
    std::chrono::steady_clock::time_point track_at_;
    std::string shown_phone_;   // the phone the list was last built with
    std::vector<Row> rows_;
    std::vector<BtDevice> devices_;
    std::map<std::string, bool> phone_;   // address: sends music (a phone)
    std::string phone_addr_;              // the connected phone, or ""
    std::string player_;                  // its player object, or ""
    phone::Info track_;                   // what it plays
    UsbStickStatus stick_;       // what zeroos-usb-stick.service says
    UsbMode mode_ = UsbMode::Files;    // the mode it is in
    UsbMode asked_ = UsbMode::Files;   // the mode last asked for here
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_car_app() { return std::make_unique<CarApp>(); }

}  // namespace zs
