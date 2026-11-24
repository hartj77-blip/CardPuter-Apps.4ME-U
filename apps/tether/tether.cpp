// Tether: get online through your phone (docs/TETHER.md).
//
//   Wi-Fi hotspot   it's a Wi-Fi network: pick it in Wi-Fi, or scan the
//                   hotspot's QR code with Camera
//   USB             plug the phone in and turn on USB tethering; nothing to
//                   set up here
//   Bluetooth       slower, but far easier on both batteries. Pair once
//                   (Add a phone), then Enter on the phone goes online or
//                   offline. Forget is in the menu (Tab > D, or Del), and
//                   asks first.
#include <algorithm>
#include <vector>

#include "apps/common.h"
#include "ui/list_view.h"
#include "apps/kit/look.h"

namespace zs {

namespace {
using namespace theme;

class TetherApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        rebuild();
    }

    void tick() override {
        if (++n_ % 5 && !dirty_) return;   // twice a second
        TetherStatus s = hal().tether();
        if (!s.error.empty() && s.error != shown_error_ && page_ != Page::Pair) shell_toast(s.error);
        shown_error_ = s.error;
        // Pairing ended: say how it went and return to the list of phones.
        if (page_ == Page::Pair && s.busy.empty() && started_ && s.error.empty()) {
            for (auto& p : s.phones)
                if (p.mac == mac_) shell_toast(p.online ? "Online through " + p.name : "Paired with " + p.name);
            page_ = Page::Main;
            dirty_ = true;
        }
        if (!s.busy.empty()) started_ = true;
        if (dirty_ || signature(s) != sig_) rebuild();
    }

    bool on_key(uint32_t key) override {
        if (page_ == Page::Pair) return false;
        if (page_ == Page::Main && key == LV_KEY_DEL) {   // not a letter: letters filter the list
            if (const TetherPhone* p = focused_phone()) {
                std::string mac = p->mac, name = p->name;
                shell_confirm("Forget " + name + "?", "Pair it again to use it.", "Forget",
                              [this, mac, name] { forget(mac, name); });
            }
            return true;
        }
        if (list_.key(key)) {
            set_actions();   // Forget follows the focus
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (page_ == Page::Main) return false;
        page_ = Page::Main;
        dirty_ = true;
        return true;
    }

private:
    enum class Page { Main, Search, Pair };

    const TetherPhone* focused_phone() const {
        int i = page_ == Page::Main ? list_.focused() - first_phone_ : -1;
        return i >= 0 && i < int(st_.phones.size()) ? &st_.phones[size_t(i)] : nullptr;
    }

    void forget(const std::string& mac, const std::string& name) {
        hal().tether_forget(mac);
        shell_toast("Forgot " + name);
        dirty_ = true;
    }

    void set_actions() {
        std::vector<Action> a;
        switch (page_) {
            case Page::Main: {
                const TetherPhone* p = focused_phone();
                std::string enter = p ? (p->online ? "Go offline" : "Go online") : "Select";
                a.push_back({enter, 0, "Enter", nullptr});
                a.push_back({"Add a phone", 'n', nullptr, [this] { search(); }});
                Action f{"Forget", 'd', nullptr, [] {}, p != nullptr, true};
                if (p) {
                    std::string mac = p->mac, name = p->name;
                    f.run = [this, mac, name] { forget(mac, name); };
                    f.confirm = "Forget " + name + "?";
                }
                a.push_back(f);
                break;
            }
            case Page::Search:
                a.push_back({"Pair", 0, "Enter", nullptr});
                a.push_back({"Search again", 'r', nullptr, [this] { search(); }, st_.busy != "searching"});
                a.push_back({"Back", 0, "Esc", nullptr});
                break;
            case Page::Pair: a.push_back({"Back", 0, "Esc", nullptr}); break;
        }
        shell_set_actions(std::move(a));
    }

    void search() {
        hal().tether_search();
        page_ = Page::Search;
        dirty_ = true;
    }

    std::string signature(const TetherStatus& s) const {
        NetStatus n = hal().net();
        std::string k = std::to_string(int(page_)) + s.busy + s.busy_mac + s.passkey + s.error + s.usb_name + s.usb_ip +
                        n.wifi_ssid + n.tether;
        for (auto& p : s.phones) k += "|" + p.mac + (p.link ? "+" : "-") + (p.online ? "+" : "-");
        for (auto& p : s.found) k += "/" + p.mac;
        return k;
    }

    void rebuild() {
        st_ = hal().tether();
        sig_ = signature(st_);
        dirty_ = false;
        int keep = page_ == built_ ? list_.focused() : -1;
        lv_obj_clean(root_);
        list_.reset();
        if (group) lv_group_remove_all_objs(group);
        built_ = page_;
        set_actions();   // first: the screen is laid out above the bar
        switch (page_) {
        case Page::Main: build_main(); break;
        case Page::Search: build_search(); break;
        case Page::Pair: build_pair(); break;
        }
        if (page_ != Page::Pair) {
            list_.finish();
            if (keep >= 0) list_.focus(std::min(keep, list_.size() - 1));
            set_actions();   // again, for the row now in focus
        }
    }

    // What we're online through, for the top line.
    std::string headline(uint32_t& color) const {
        NetStatus n = hal().net();
        color = kGood;
        if (st_.usb && !st_.usb_ip.empty()) return "Online through " + st_.usb_name + " (USB)";
        for (auto& p : st_.phones)
            if (p.online) return "Online through " + p.name + " (Bluetooth)";
        color = kMuted;
        if (n.wifi_connected) return "On Wi-Fi: " + n.wifi_ssid;
        return "Not online";
    }

    void build_main() {
        uint32_t color;
        std::string head = headline(color);
        lv_obj_t* h = label(root_, head.c_str(), &zs_font_12, color);
        lv_obj_set_pos(h, 8, 4);
        lv_obj_set_width(h, kScreenW - 16);
        lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);

        list_.build(root_, 2, 20, kScreenW - 4, kBodyH - 21);
        list_.add(LV_SYMBOL_WIFI, "Wi-Fi hotspot: pick it in Wi-Fi", "", [] { shell_open_app("wifi"); });
        list_.add(ZS_ICON_CAMERA, "Wi-Fi hotspot: scan its QR code", "", [] { shell_open_app("camera", "scan"); });

        std::string usb = "USB: plug in the phone", usb_state;
        if (st_.usb) {
            usb = "USB: " + st_.usb_name;
            usb_state = st_.usb_ip.empty() ? "not sharing yet" : "online";
        }
        list_.add(LV_SYMBOL_USB, usb, usb_state, [this] {
            if (st_.usb && !st_.usb_ip.empty()) shell_toast("Online at " + st_.usb_ip);
            else shell_toast("On the phone: turn on USB tethering (iPhone: Personal Hotspot)");
        });

        first_phone_ = list_.size();
        for (size_t i = 0; i < st_.phones.size(); ++i) {
            const TetherPhone& p = st_.phones[i];
            bool busy = st_.busy == "connecting" && st_.busy_mac == p.mac;
            int row = list_.add(p.online ? LV_SYMBOL_OK : LV_SYMBOL_BLUETOOTH, p.name,
                                busy ? "connecting..." : p.online ? "online" : "off", [this, i] { on_phone(i); });
            if (p.online) list_.set_value_color(row, kGood);
        }
        list_.add(LV_SYMBOL_PLUS, "Add a phone over Bluetooth", "", [this] { search(); });
    }

    void on_phone(size_t i) {
        if (i >= st_.phones.size()) return;
        const TetherPhone& p = st_.phones[i];
        if (!st_.busy.empty()) {
            shell_toast("Busy: " + st_.busy);
            return;
        }
        if (p.online) {
            hal().tether_disconnect(p.mac);
            shell_toast("Offline");
        } else {
            hal().tether_connect(p.mac);
            shell_toast("Connecting through " + p.name + "...");
        }
        dirty_ = true;
    }

    void build_search() {
        bool searching = st_.busy == "searching";
        lv_obj_t* h = label(root_, searching ? "Looking for phones..." : st_.found.empty() ? "No phones found" : "Pick your phone",
                            &zs_font_12, searching ? kInfo : kText);
        lv_obj_set_pos(h, 8, 4);
        lv_obj_t* tip = label(root_, "On the phone, open Bluetooth settings and keep them open.", &zs_font_12, kMuted);
        lv_obj_set_pos(tip, 8, 20);
        lv_obj_set_width(tip, kScreenW - 16);
        lv_label_set_long_mode(tip, LV_LABEL_LONG_WRAP);

        list_.build(root_, 2, 52, kScreenW - 4, kBodyH - 53);
        list_.set_empty_text(searching ? "Searching..." : "Nothing found");
        for (size_t i = 0; i < st_.found.size(); ++i) {
            list_.add(ZS_ICON_PHONE, st_.found[i].name, "", [this, i] {
                if (i >= st_.found.size()) return;
                mac_ = st_.found[i].mac;
                name_ = st_.found[i].name;
                hal().tether_pair(mac_);
                started_ = true;
                page_ = Page::Pair;
                dirty_ = true;
            });
        }
        if (!searching) list_.add(LV_SYMBOL_REFRESH, "Search again", "", [this] { search(); });
    }

    void build_pair() {
        lv_obj_t* h = label(root_, ("Pairing with " + name_).c_str(), &zs_font_14, kText);
        lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 8);
        std::string big, text;
        uint32_t color = kMuted;
        if (!st_.error.empty() && st_.busy.empty()) {
            text = st_.error;
            color = kBad;
        } else if (st_.busy == "connecting") {
            text = "Paired. Going online...";
            color = kGood;
        } else if (!st_.passkey.empty()) {
            big = st_.passkey.size() == 6 ? st_.passkey.substr(0, 3) + " " + st_.passkey.substr(3) : st_.passkey;
            text = "If the phone shows the same number, tap Pair on the phone. If not, tap Cancel.";
        } else {
            text = "Waiting for the phone...";
        }
        if (!big.empty()) {
            lv_obj_t* n = label(root_, big.c_str(), &zs_font_28, kAccent);
            lv_obj_align(n, LV_ALIGN_TOP_MID, 0, 34);
        }
        lv_obj_t* t = label(root_, text.c_str(), &zs_font_12, color);
        lv_obj_set_width(t, kScreenW - 32);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, big.empty() ? 54 : 78);
    }

    lv_obj_t* root_ = nullptr;
    ListView list_;
    int first_phone_ = 0;   // the list's row of the first paired phone
    TetherStatus st_;
    std::string sig_, shown_error_, mac_, name_;
    Page page_ = Page::Main, built_ = Page::Main;
    bool dirty_ = false;
    bool started_ = false;   // the pairing we asked for has been seen running
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_tether_app() { return std::make_unique<TetherApp>(); }

}  // namespace zs
