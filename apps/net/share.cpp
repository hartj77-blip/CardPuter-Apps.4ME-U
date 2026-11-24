// Share: this Cardputer as a router for a laptop on the Ethernet cable. The
// laptop gets an address by DHCP and goes online through whatever the
// Cardputer is online through (Wi-Fi, a tethered phone). With nothing to share
// the laptop still gets an address, so it can reach the Cardputer (SSH).
//
//   Enter  sharing on / off      R  refresh
//
// The work is done by /usr/bin/zeroos-share (files/zeroos-share), a
// NetworkManager "shared" connection; this screen runs it and shows its
// status, refreshed every few seconds.
#include <string>
#include <vector>

#include "apps/common.h"
#include "net_run.h"
#include "net_sim.h"
#include "net_ui.h"

namespace zs {
namespace {
using namespace theme;

constexpr uint32_t kRefreshMs = 3000;

struct Client {
    std::string ip, mac, host;
};

struct ShareStatus {
    std::string state = "off";   // on, off, starting
    bool carrier = false;
    std::string iface = "eth0";
    std::string address;         // 10.42.0.1/24 while on
    std::string up_iface, up_kind, up_name;
    std::vector<Client> clients;
    bool cable_online = false;   // (off) online through this port right now
    std::string error;
};

std::vector<std::string> words(const std::string& s, size_t max) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size() && out.size() + 1 < max) {
        while (i < s.size() && s[i] == ' ') ++i;
        size_t j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    while (i < s.size() && s[i] == ' ') ++i;
    if (i < s.size()) out.push_back(s.substr(i));   // the rest, spaces and all
    while (out.size() < max) out.push_back("");
    return out;
}

class ShareApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        head_ = label(root_, "", &zs_font_14, kMuted);
        lv_obj_set_pos(head_, 4, 1);
        lv_obj_t* hint = label(root_, "Enter on/off  R refresh", &zs_font_12, kMuted);
        lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -4, 3);
        out_.build(root_, 18);
        run("status");
        render();
    }

    ~ShareApp() override { job_.cancel(); }

    void tick() override {
        if (!job_.started()) {
            if (lv_tick_elaps(last_) > kRefreshMs) run("status");
            return;
        }
        std::vector<std::string> lines;
        if (job_.drain(lines)) for (auto& l : lines) raw_.push_back(l);
        if (job_.running()) return;
        net::Done d = job_.status();
        job_.reset();
        bool was_action = action_ != "status";
        if (d == net::Done::Missing) {
            st_ = ShareStatus{};
            st_.error = "zeroos-share is missing: reinstall Net Tools";
        } else {
            parse();
        }
        if (was_action) {
            err_ = st_.error;   // kept on screen until the next Enter
            shell_toast(!err_.empty() ? err_ : st_.state == "on" ? "Sharing on the Ethernet port" : "Sharing off");
        } else if (!st_.error.empty()) {
            err_ = st_.error;
        }
        action_.clear();
        last_ = lv_tick_get();
        render();
    }

    bool on_key(uint32_t key) override {
        if (key == LV_KEY_ENTER) {
            if (busy()) return true;
            bool on = st_.state != "off";
            if (net::simulated()) sim_on_ = !on;
            err_.clear();
            run(on ? "off" : "on");
            render();
            return true;
        }
        if (key == 'r' || key == 'R') {
            if (!busy()) { run("status"); render(); }
            return true;
        }
        return false;
    }

private:
    bool busy() const { return action_ == "on" || action_ == "off"; }

    void run(const std::string& what) {
        job_.cancel();
        job_.reset();
        raw_.clear();
        action_ = what;
        job_.start({"zeroos-share", what}, net::sim::share(sim_on_), 0);
    }

    void parse() {
        ShareStatus s;
        for (auto& line : raw_) {
            size_t t = line.find('\t');
            if (t == std::string::npos) continue;
            std::string k = line.substr(0, t), v = line.substr(t + 1);
            if (k == "state") s.state = v;
            else if (k == "carrier") s.carrier = v == "1";
            else if (k == "iface") s.iface = v;
            else if (k == "address") s.address = v;
            else if (k == "error") s.error = v;
            else if (k == "cable_online") s.cable_online = v == "1";
            else if (k == "uplink") {
                auto f = words(v, 3);
                s.up_iface = f[0];
                s.up_kind = f[1];
                s.up_name = f[2];
            } else if (k == "client") {
                auto f = words(v, 3);
                s.clients.push_back({f[0], f[1], f[2]});
            }
        }
        st_ = s;
    }

    std::string uplink_text() const {
        if (st_.up_iface.empty()) return "nothing: the laptop gets an address only";
        std::string kind = st_.up_kind == "wifi"        ? "Wi-Fi"
                           : st_.up_kind == "usb"       ? "phone (USB)"
                           : st_.up_kind == "bluetooth" ? "phone (Bluetooth)"
                           : st_.up_kind == "ethernet"  ? "wired"
                                                        : st_.up_iface;
        std::string name = st_.up_name == st_.up_iface ? "" : " " + st_.up_name;
        return kind + name + " (" + st_.up_iface + ")";
    }

    void render() {
        bool on = st_.state == "on";
        const char* head = action_ == "on"         ? "Turning sharing on..."
                           : action_ == "off"      ? "Turning sharing off..."
                           : st_.state == "starting" ? "Sharing: starting"
                           : on                    ? "Sharing: on"
                                                   : "Sharing: off";
        lv_label_set_text(head_, head);
        lv_obj_set_style_text_color(head_, lv_color_hex(busy() ? kInfo : on ? kGood : kMuted), 0);

        out_.clear();
        if (!err_.empty()) {
            out_.append("! " + err_);
            out_.append("");
        }
        out_.append(std::string("Cable (") + st_.iface + "): " + (st_.carrier ? "plugged in" : "not plugged in"));
        out_.append("Internet from: " + uplink_text());
        if (on) {
            std::string ip = st_.address.substr(0, st_.address.find('/'));
            if (!ip.empty()) out_.append("This Cardputer: " + ip);
            out_.append("");
            if (st_.clients.empty()) {
                out_.append("Waiting for the laptop to ask for an address.");
                out_.append("Its wired network should be set to DHCP (automatic).");
            } else {
                out_.append("Connected:");
                for (auto& c : st_.clients)
                    out_.append("  " + c.ip + "  " + (c.host.empty() ? c.mac : c.host));
            }
            if (!ip.empty()) out_.append("From the laptop: ssh pi@" + ip);
        } else {
            out_.append("");
            out_.append("Plug a laptop into the Ethernet port and press");
            out_.append("Enter: it gets an address and goes online through");
            out_.append("this Cardputer.");
            if (st_.cable_online) {
                out_.append("");
                out_.append("Note: this Cardputer is online through the cable");
                out_.append("now. Sharing takes the port over for the laptop.");
            }
        }
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* head_ = nullptr;
    net::OutputView out_;
    net::Job job_;
    std::vector<std::string> raw_;
    ShareStatus st_;
    std::string action_;
    std::string err_;
    uint32_t last_ = 0;
    bool sim_on_ = false;
};
}  // namespace

std::unique_ptr<App> make_share_app() { return std::make_unique<ShareApp>(); }

}  // namespace zs
