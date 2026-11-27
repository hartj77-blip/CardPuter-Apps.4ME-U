// Mesh: the Cardputer as a Meshtastic LoRa node (Cap LoRa-1262), for text
// messages with no phone signal or internet. meshtasticd runs the radio.
//   Chat page (as soon as the mesh is on): messages on the primary channel;
//     a Text screen (docs/UX.md in the OS): type and press Enter to send to
//     everyone. The text box takes typing while the radio is still
//     connecting.
//   Info page: on/off, Allow phones, QR code for the web client (chat
//     from a phone), open it on the monitor in Dock mode, GPS. Esc: back to
//     chat.
// meshtasticd's web client and API have no login, so they answer only this
// device and your tailnet until you choose Allow phones, which opens
// them to the network you're on until the mesh goes off
// (zeroos-mesh-lan.service, files/zeroos-mesh-firewall).
// The two pages are tabs: Left / Right switch (from the chat, while the box
// is empty, so they never take the cursor from what you're typing), and the
// Tab menu has Info and Chat.
// See docs/EXPANSION.md.
#include "apps/common.h"
#include "ui/list_view.h"
#include "ui/power.h"
#include "ui/tracker.h"
#include "apps/kit/look.h"

#include <cstdlib>

namespace zs {
namespace {
using namespace theme;

class MeshApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        rebuild();
    }

    void tick() override {
        if (dirty_) {
            dirty_ = false;
            rebuild();
            return;
        }
        if (++n_ % 5) return;
        if (chat_ && hal().mesh_seq() != seq_) fill_messages();
        if (head_ && !(n_ % 10)) update_head();
        if (n_ % 10) return;
        MeshStatus m = hal().mesh();
        if (sig(m) != sig_) rebuild();
    }

    bool on_key(uint32_t key) override {
        if ((key == LV_KEY_LEFT || key == LV_KEY_RIGHT) && can_chat()) {
            bool empty = !ta_ || !*lv_textarea_get_text(ta_);
            if (info_ || empty) {
                switch_page(!info_);
                return true;
            }
            return false;   // the cursor, in what's being typed
        }
        if (ta_) return false;   // typing
        return list_.key(key);
    }

    bool on_back() override {
        if (info_ && can_chat()) {
            info_ = false;
            dirty_ = true;
            return true;
        }
        return false;
    }

private:
    static std::string sig(const MeshStatus& m) {
        // Not "connected" or the node count: the chat page updates its own
        // header for those, so a rebuild doesn't interrupt typing.
        return std::to_string(m.running) + std::to_string(m.no_radio) + std::to_string(m.failing) + m.error +
               m.web_url;
    }
    // The chat page shows whenever the daemon is up, before ZeroShell has
    // finished connecting to it: typing never waits on the radio.
    bool can_chat() const { return st_.running && !st_.failing; }

    // Allow phones: is it on, and switch it.
    static bool simulated() {
        static int s = -1;
        if (s < 0) s = hal().system().simulated ? 1 : 0;
        return s == 1;
    }
    bool lan_on() const {
        if (simulated()) return sim_lan_;
        return std::system("systemctl is-active --quiet zeroos-mesh-lan.service") == 0;
    }
    void set_lan(bool on) {
        if (simulated()) {
            sim_lan_ = on;
        } else if (std::system(on ? "systemctl start zeroos-mesh-lan.service >/dev/null 2>&1"
                                  : "systemctl stop zeroos-mesh-lan.service >/dev/null 2>&1") != 0) {
            shell_toast("Couldn't change it. Update ZeroOS and try again");
            return;
        }
        shell_toast(on ? "Phones on this network can connect" : "Only this device and Tailscale can connect");
        dirty_ = true;
    }

    void switch_page(bool info) {
        info_ = info;
        dirty_ = true;
    }

    void rebuild() {
        st_ = hal().mesh();
        sig_ = sig(st_);
        if (st_.running || st_.failing) starting_ = false;
        if (ta_) draft_ = lv_textarea_get_text(ta_);
        lv_obj_clean(root_);
        chat_ = ta_ = head_ = nullptr;
        list_.reset();
        if (group) lv_group_remove_all_objs(group);
        if (can_chat() && !info_) {
            build_chat();
            return;
        }

        std::vector<Action> acts;
        acts.push_back({"Select", 0, "Enter", nullptr});
        if (can_chat()) {
            acts.push_back({"Chat", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
            acts.push_back({"Chat", 'c', nullptr, [this] { switch_page(false); }});
        }
        shell_set_actions(std::move(acts));

        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 150, kBodyH - 10);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(panel, 3, 0);

        lan_ = st_.running && lan_on();
        if (st_.running && lan_ && !st_.web_url.empty() && !st_.no_radio && !st_.failing) {
            lv_obj_t* qr = lv_qrcode_create(panel);
            lv_qrcode_set_size(qr, 84);
            lv_qrcode_set_dark_color(qr, c(0x000000));
            lv_qrcode_set_light_color(qr, c(0xffffff));
            lv_qrcode_update(qr, st_.web_url.c_str(), uint32_t(st_.web_url.size()));
            lv_obj_set_style_border_color(qr, c(0xffffff), 0);
            lv_obj_set_style_border_width(qr, 4, 0);
            label(panel, "Scan to chat", &zs_font_12, kText);
            lv_obj_t* a = label(panel, ("App: " + st_.api).c_str(), &zs_font_12, kMuted);
            lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
            lv_obj_set_width(a, 136);
        } else {
            const char* head = !st_.installed ? "Not installed"
                               : (st_.running || st_.failing) && st_.no_radio ? "No radio"
                               : st_.failing ? "Radio error"
                               : st_.running ? LV_SYMBOL_OK " Mesh on"
                               : starting_   ? "Starting..."
                                             : "Mesh off";
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            label(panel, head, &zs_font_16, st_.failing || (st_.running && st_.no_radio) ? kBad : kText);
            std::string err = st_.error.empty()
                                  ? "The radio service keeps stopping. Power off, reseat the Cap LoRa-1262, try again."
                                  : st_.error;
            const char* hint = !st_.installed ? "meshtasticd is missing: reinstall Mesh in Apps."
                               : st_.no_radio ? "Plug in the Cap LoRa-1262 with the power off, then turn the mesh on."
                               : st_.failing ? err.c_str()
                               : st_.running && !lan_ ? "To chat from a phone on this network, choose Allow phones. Tailscale always can."
                               : st_.running ? "Connect to Wi-Fi to chat from your phone."
                                             : "Text over LoRa radio, miles away, with no signal or internet.";
            lv_obj_t* h = label(panel, hint, &zs_font_12, kMuted);
            lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(h, 136);
        }

        list_.build(root_, 162, 4, 152, kBodyH - 10);
        if (st_.installed) {
            if (!st_.running && !starting_) {
                list_.add(LV_SYMBOL_POWER, "Turn on mesh", "", [this] {
                    hal().mesh_start();
                    starting_ = true;
                    dirty_ = true;
                    shell_toast("Starting the radio...");
                });
            } else {
                list_.add(LV_SYMBOL_POWER, "Turn off mesh", "", [this] {
                    hal().mesh_stop();
                    if (!track_state().recording) hal().set_ext_power(false);   // a track still needs the GPS
                    starting_ = false;
                    dirty_ = true;
                    shell_toast("Mesh off");
                });
            }
            if (st_.running) {
                if (lan_)
                    list_.add(LV_SYMBOL_CLOSE, "Block phones", "", [this] { set_lan(false); });
                else
                    list_.add(LV_SYMBOL_WIFI, "Allow phones", "", [this] { set_lan(true); });
                list_.add(LV_SYMBOL_IMAGE, "Open on monitor", "", [] {
                    if (!power().docked()) {
                        shell_toast("Needs Dock mode (a monitor)");
                        return;
                    }
                    hal().dock_open("browser", "https://localhost:9443");
                    shell_toast("Opening Meshtastic on the monitor");
                });
            }
        }
        if (can_chat()) list_.add(LV_SYMBOL_ENVELOPE, "Chat", "", [this] { switch_page(false); });
        list_.add(LV_SYMBOL_GPS, "GPS", "", [] { shell_open_app("maps", "gps"); });   // Maps has the GPS screen
        list_.finish();
    }

    void build_chat() {
        std::vector<Action> acts;
        acts.push_back({"Send", 0, "Enter", nullptr});
        acts.push_back({"Info", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
        acts.push_back({"Node info", 'i', nullptr, [this] { switch_page(true); }});
        shell_set_actions(std::move(acts));

        head_ = label(root_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(head_, 8, 2);
        lv_label_set_long_mode(head_, LV_LABEL_LONG_DOT);
        lv_obj_set_width(head_, kScreenW - 16);
        update_head();

        chat_ = lv_obj_create(root_);
        style_panel(chat_);
        lv_obj_set_pos(chat_, 4, 18);
        lv_obj_set_size(chat_, kScreenW - 8, kBodyH - 58);
        lv_obj_set_flex_flow(chat_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(chat_, 4, 0);
        lv_obj_set_style_pad_row(chat_, 2, 0);
        fill_messages();

        ta_ = lv_textarea_create(root_);
        look::field(ta_);
        lv_textarea_set_one_line(ta_, true);
        lv_textarea_set_placeholder_text(ta_, "Message everyone");
        lv_textarea_set_max_length(ta_, 200);
        lv_textarea_set_text(ta_, draft_.c_str());
        lv_obj_set_width(ta_, kScreenW - 8);
        lv_obj_set_pos(ta_, 4, kBodyH - 37);
        lv_obj_add_event_cb(ta_, [](lv_event_t* e) {
            auto* self = static_cast<MeshApp*>(lv_event_get_user_data(e));
            std::string text = lv_textarea_get_text(self->ta_);
            if (text.empty()) return;
            MeshStatus m = hal().mesh();
            if (m.no_radio) {
                shell_toast("Not sent: no radio found");
            } else if (!m.connected) {
                shell_toast("Not sent yet: radio still connecting");   // the text stays in the box
            } else if (hal().mesh_send(text)) {
                lv_textarea_set_text(self->ta_, "");
                self->fill_messages();
            } else {
                shell_toast("Not sent: lost the radio, try again");
            }
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta_);
        lv_group_focus_obj(ta_);
        lv_group_set_editing(group, true);
    }

    void update_head() {
        MeshStatus m = hal().mesh();
        std::string h;
        if (m.no_radio) {
            h = "No radio found";
            lv_obj_set_style_text_color(head_, c(kBad), 0);
        } else if (!m.connected) {
            h = "Connecting to the radio...";
            lv_obj_set_style_text_color(head_, c(kMuted), 0);
        } else {
            h = (m.my_name.empty() ? std::string("Mesh") : m.my_name) + fmt("  |  %d nodes", m.nodes);
            lv_obj_set_style_text_color(head_, c(kMuted), 0);
        }
        lv_label_set_text(head_, h.c_str());
    }

    void fill_messages() {
        if (!chat_) return;
        seq_ = hal().mesh_seq();
        lv_obj_clean(chat_);
        auto msgs = hal().mesh_messages();
        size_t first = msgs.size() > 30 ? msgs.size() - 30 : 0;   // keep the screen light
        if (msgs.empty()) {
            label(chat_, "No messages yet. Say hello!", &zs_font_12, kMuted);
        }
        for (size_t i = first; i < msgs.size(); ++i) {
            const MeshMessage& m = msgs[i];
            std::string who = m.mine ? "You" : m.from + (m.direct ? " (to you)" : "");
            lv_obj_t* l = lv_label_create(chat_);
            lv_label_set_text(l, (who + ": " + m.text).c_str());
            lv_obj_set_style_text_font(l, &zs_font_12, 0);
            lv_obj_set_style_text_color(l, c(m.mine ? kMuted : m.direct ? kWarn : kText), 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, kScreenW - 24);
        }
        lv_obj_update_layout(chat_);
        lv_obj_scroll_to_y(chat_, LV_COORD_MAX, LV_ANIM_OFF);
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* chat_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    lv_obj_t* head_ = nullptr;
    ListView list_;
    MeshStatus st_;
    std::string sig_, draft_;
    bool starting_ = false, dirty_ = false, info_ = false;
    bool lan_ = false;       // Allow phones is on
    bool sim_lan_ = false;   // the same, in the simulator
    int n_ = 0, seq_ = -1;
};
}  // namespace

std::unique_ptr<App> make_mesh_app() { return std::make_unique<MeshApp>(); }

}  // namespace zs
