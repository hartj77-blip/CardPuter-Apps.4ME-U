// Remote: Tailscale and your other devices.
//
//   Off          -> Turn on Tailscale
//   Sign-in      -> QR code + link; approve on your phone
//   Connected    -> your devices (online first), a List screen (docs/UX.md
//                   in the OS). Enter = SSH (or mosh) in the terminal. The
//                   menu (Tab, or Ctrl + the letter): S SSH, D remote
//                   desktop (the Desktop app if it is installed, else RDP on
//                   the monitor in Dock mode), F send a file, I details.
//                   Files' Share sends a file here: Enter then sends it.
// Also: Connect to any host, the SSH user name, mosh on/off, sign out, and
// the control server: Tailscale's own or a Headscale server of your own,
// with an auth key instead of the sign-in link if you like.
#include <algorithm>
#include <vector>

#include "apps/common.h"
#include "ui/list_view.h"
#include "ui/power.h"
#include "ui/prefs.h"
#include "ui/remote.h"
#include "apps/kit/look.h"

namespace zs {

namespace {
using namespace theme;

// SSH and mosh run in the Terminal app, which is installed from Apps too.
void terminal_run(const std::string& cmd) {
    if (!app_find("terminal")) {
        shell_toast("SSH needs the Terminal app: get it from Apps");
        return;
    }
    remote_note_use();
    shell_open_app("terminal", cmd);
}

// Headscale: the shell reads these when it starts Tailscale ("tailscale up
// --login-server", and --authkey once). "" is Tailscale's own server.
std::string ts_server() { return prefs::get_str("tailscale.server", ""); }

// "https://hs.example.com:8080/" -> "hs.example.com:8080"; "" -> "Tailscale".
std::string server_name(std::string s) {
    size_t i = s.find("://");
    if (i != std::string::npos) s = s.substr(i + 3);
    while (!s.empty() && (s.back() == '/' || s.back() == ' ')) s.pop_back();
    return s.empty() ? "Tailscale" : s;
}

// A new server signs in again: now if Tailscale is on, else when it starts.
void set_server(const std::string& v) {
    std::string was = ts_server();
    prefs::set_str("tailscale.server", v);
    if (server_name(v) == server_name(was)) return;
    if (hal().tailscale().running) {
        hal().tailscale_start();
        shell_toast("Signing in to " + server_name(v) + "...");
    } else {
        shell_toast("Next time: " + server_name(v));
    }
}

const char* os_symbol(const std::string& os) {
    if (os == "windows") return LV_SYMBOL_IMAGE;
    if (os == "linux") return LV_SYMBOL_DRIVE;
    if (os == "iOS" || os == "android") return LV_SYMBOL_CALL;
    return LV_SYMBOL_HOME;
}

class RemoteApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        remote_note_use();
        if (tailscale_mode() != "off" && !hal().tailscale().running && hal().tailscale().installed) {
            hal().tailscale_start();
            starting_ = true;
        }
        rebuild();
    }

    void tick() override {
        remote_note_use();
        // Never rebuild under the open text box: it would vanish mid-typing.
        // Closing it sets dirty_, so the refresh comes then.
        if (input_) return;
        if (dirty_) {
            dirty_ = false;
            rebuild();
            return;
        }
        if (++n_ % 10) return;
        TsStatus t = hal().tailscale();
        if (signature(t) != sig_) rebuild();
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;   // typing in the text box
        if (list_.key(key)) {
            set_actions();   // the menu follows the row in focus
            return true;
        }
        return false;
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (!remote_pending_send().empty()) {
            remote_take_send();
            rebuild();
            return true;
        }
        return false;
    }

private:
    static std::string signature(const TsStatus& t) {
        std::string s = t.state + "|" + t.auth_url + "|" + std::to_string(t.running);
        for (auto& p : t.peers) s += "|" + p.name + (p.online ? "+" : "-");
        return s;
    }

    // Peers are the list's first rows, in ts_.peers' order.
    int focused_peer() const {
        int i = input_ ? -1 : list_.focused();
        return i >= 0 && i < peer_rows_ ? i : -1;
    }

    void set_actions() {
        int i = focused_peer();
        bool peer = i >= 0;
        bool sending = !remote_pending_send().empty();
        std::vector<Action> a;
        a.push_back({peer ? (sending ? "Send" : "SSH") : "Select", 0, "Enter", nullptr});
        if (!remote_pending_send().empty()) a.push_back({"Cancel sending", 0, "Esc", nullptr});
        a.push_back({"SSH", 's', nullptr, [this] { ssh(focused_peer()); }, peer});
        a.push_back({app_find("desktop") ? "Remote desktop" : "Desktop on monitor", 'd', nullptr,
                     [this] { desktop(focused_peer()); }, peer});
        a.push_back({"Send a file", 'f', nullptr, [this] { send_file(focused_peer()); }, peer});
        a.push_back({"Details", 'i', nullptr, [this] { details(focused_peer()); }, peer});
        shell_set_actions(std::move(a));
    }

    void ssh(int i) {
        if (i < 0 || i >= int(ts_.peers.size())) return;
        const TsPeer& p = ts_.peers[size_t(i)];
        if (!p.online) shell_toast(p.name + " looks offline; trying anyway");
        terminal_run(ssh_command(p.name));
    }

    // The Desktop app does VNC and RDP, on this screen or the monitor.
    void desktop(int i) {
        if (i < 0 || i >= int(ts_.peers.size())) return;
        const TsPeer& p = ts_.peers[size_t(i)];
        if (app_find("desktop")) {
            shell_open_app("desktop", "peer:" + p.name + ":" + p.os);
        } else if (!power().docked()) {
            shell_toast("Remote desktop needs Dock mode (a monitor)");
        } else {
            hal().dock_open("rdp", p.name);
            shell_toast("Opening " + p.name + " on the monitor");
        }
    }

    // The file Files handed over, or else pick one in Files first.
    void send_file(int i) {
        if (i < 0 || i >= int(ts_.peers.size())) return;
        if (remote_pending_send().empty()) {
            shell_toast("Pick the file in Files, then Share");
            shell_open_app("files");
            return;
        }
        const TsPeer& p = ts_.peers[size_t(i)];
        std::string path = remote_take_send();
        bool ok = hal().taildrop_send(path, p.name);
        shell_toast(ok ? "Sent to " + p.name : "Couldn't send to " + p.name);
        dirty_ = true;
    }

    void details(int i) {
        if (i < 0 || i >= int(ts_.peers.size())) return;
        const TsPeer& p = ts_.peers[size_t(i)];
        shell_toast(p.name + "  " + p.ip + "  " + p.os + (p.online ? "" : "  (offline)"));
    }

    void rebuild() {
        ts_ = hal().tailscale();
        sig_ = signature(ts_);
        if (ts_.state != "Stopped") starting_ = false;
        int keep = input_ ? -1 : list_.focused();
        lv_obj_clean(root_);
        input_ = nullptr;
        peer_rows_ = 0;
        if (group) lv_group_remove_all_objs(group);
        set_actions();   // first: the screen is laid out above the bar

        // Left: status, or the sign-in QR code.
        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 132, kBodyH - 8);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);

        bool signin = ts_.state == "NeedsLogin" && !ts_.auth_url.empty();
        if (signin) {
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t* qr = lv_qrcode_create(panel);
            lv_qrcode_set_size(qr, 96);
            lv_qrcode_set_dark_color(qr, c(0x000000));
            lv_qrcode_set_light_color(qr, c(0xffffff));
            lv_qrcode_update(qr, ts_.auth_url.c_str(), uint32_t(ts_.auth_url.size()));
            lv_obj_set_style_border_color(qr, c(0xffffff), 0);
            lv_obj_set_style_border_width(qr, 4, 0);
            // Headscale: an admin approves the device on the server
            // ("headscale nodes register"); the link's page says how.
            lv_obj_t* l = label(panel, ts_server().empty() ? "Scan to sign in" : "Approve on Headscale",
                                &zs_font_12, kText);
            (void)l;
        } else {
            const char* head = !ts_.installed            ? "Not installed"
                               : ts_.state == "Running"  ? LV_SYMBOL_OK " Connected"
                               : ts_.state == "Stopped" && !starting_ ? "Tailscale off"
                               : ts_.state == "NeedsLogin" ? "Signing in..."
                               : "Starting...";
            label(panel, head, &zs_font_16, ts_.state == "Running" ? kGood : kText);
            if (ts_.state == "Running") {
                kv_row(panel, "Name", ts_.self_name.c_str());
                kv_row(panel, "IP", ts_.self_ip.c_str());
                lv_obj_t* tn = label(panel, ts_.tailnet.c_str(), &zs_font_12, kMuted);
                lv_label_set_long_mode(tn, LV_LABEL_LONG_DOT);
                lv_obj_set_width(tn, 118);
                const std::string& send = remote_pending_send();
                if (!send.empty()) {
                    std::string hint = "Send " + send.substr(send.rfind('/') + 1) + ":\npick a device";
                    lv_obj_t* h = label(panel, hint.c_str(), &zs_font_12, kAccent);
                    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
                    lv_obj_set_width(h, 118);
                }
            } else {
                lv_obj_t* h = label(panel,
                                    ts_.installed ? "Reach your PC, phone and servers from anywhere."
                                                  : "Tailscale is missing: reinstall Remote in Apps.",
                                    &zs_font_12, kMuted);
                lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
                lv_obj_set_width(h, 118);
            }
        }

        // Right: devices, then settings and actions.
        list_.build(root_, 142, 4, kScreenW - 144, kBodyH - 8);
        if (ts_.state == "Running") {
            for (size_t i = 0; i < ts_.peers.size(); ++i) {
                const TsPeer& p = ts_.peers[i];
                int row = list_.add(os_symbol(p.os), p.name, p.online ? "" : "off", [this, i] { on_peer(i); });
                if (!p.online) list_.set_value_color(row, kMuted);
            }
            peer_rows_ = int(ts_.peers.size());
        } else if (ts_.installed && ts_.state == "Stopped" && !starting_) {
            list_.add(LV_SYMBOL_POWER, "Turn on Tailscale", "", [this] {
                hal().tailscale_start();
                starting_ = true;
                shell_toast("Starting Tailscale...");
            });
        }
        if (ts_.installed && ts_.state != "Running") {
            list_.add(LV_SYMBOL_EDIT, "Use auth key", "", [this] {
                ask("Auth key", "", [](const std::string& v) {
                    if (v.empty()) return;
                    prefs::set_str("tailscale.authkey", v);   // the shell uses it once, then clears it
                    hal().tailscale_start();
                    shell_toast("Signing in with the key...");
                });
            });
        }
        list_.add(LV_SYMBOL_KEYBOARD, "Connect to...", "", [this] {
            ask("Host (user@host)", "", [](const std::string& v) {
                if (!v.empty()) terminal_run((use_mosh() ? "mosh " : "ssh ") + v);
            });
        });
        list_.add(LV_SYMBOL_EDIT, "SSH as", ssh_user().empty() ? std::string("pi") : ssh_user(), [this] {
            ask("SSH user name", ssh_user(), [](const std::string& v) { set_ssh_user(v); });
        });
        list_.add_choice(LV_SYMBOL_SHUFFLE, "Use mosh", {"Off", "On"}, use_mosh() ? 1 : 0,
                         [](int i) { set_use_mosh(i == 1); });
        list_.add(LV_SYMBOL_HOME, "Server", server_name(ts_server()), [this] {
            ask("Headscale server (empty: Tailscale)", ts_server(), [](const std::string& v) { set_server(v); });
        });
        if (ts_.running) {
            list_.add(LV_SYMBOL_POWER, "Turn off Tailscale", "", [this] {
                hal().tailscale_stop();
                shell_toast("Tailscale off");
                dirty_ = true;
            });
        }
        if (ts_.state == "Running") {
            list_.add(LV_SYMBOL_CLOSE, "Sign out", "", [] {
                hal().tailscale_logout();
                shell_toast("Signed out of Tailscale");
            });
        }
        list_.finish();
        if (keep > 0 && list_.size()) list_.focus(std::min(keep, list_.size() - 1));
        set_actions();   // again, for the row now in focus
    }

    void on_peer(size_t i) {
        if (i >= ts_.peers.size()) return;
        if (!remote_pending_send().empty()) send_file(int(i));
        else ssh(int(i));
    }

    // One-line text box over the screen (a Text screen while it's open).
    using Done = void (*)(const std::string&);
    void ask(const char* title, const std::string& initial, Done done) {
        done_ = done;
        shell_set_actions({{"Save", 0, "Enter", nullptr}, {"Cancel", 0, "Esc", nullptr}});
        input_ = lv_obj_create(root_);
        look::raised(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(input_);
        look::field(ta);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<RemoteApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = self->done_;
            self->close_input();
            d(v);
            self->dirty_ = true;
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    void close_input() {
        if (!input_) return;
        lv_group_set_editing(group, false);
        lv_obj_t* in = input_;
        input_ = nullptr;
        lv_obj_delete_async(in);
        dirty_ = true;   // catch up on refreshes held back while it was open
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* input_ = nullptr;
    Done done_ = nullptr;
    TsStatus ts_;
    std::string sig_;
    ListView list_;
    int peer_rows_ = 0;   // the list's first rows are ts_.peers
    bool starting_ = false;
    bool dirty_ = false;   // rebuild on the next tick (never inside a widget's own event)
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_remote_app() { return std::make_unique<RemoteApp>(); }

}  // namespace zs
