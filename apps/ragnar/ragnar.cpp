// Ragnar: a Bjorn-family network reconnaissance tool, run headless on ZeroOS.
// The real work is a background service (headlessRagnar.py) and its web
// dashboard on port 8000; this screen is a control panel over it.
//
//   Off       -> Turn on Ragnar (asks first: it actively scans the network)
//   Starting  -> the service is coming up
//   Running   -> Open dashboard (in the Browser app, on this device),
//                Open on monitor (Dock), Allow on this network / Block,
//                Turn off Ragnar. When the dashboard is allowed on the LAN,
//                the left panel shows a QR code and link to it.
//
// The dashboard answers this device and your Tailscale network as soon as
// Ragnar is on; "Allow on this network" opens it to the LAN too (the service is
// otherwise firewalled, files/zeroos-ragnar-firewall, because Ragnar serves an
// unauthenticated endpoint until you set a login in it). Only use Ragnar on
// networks you own or are allowed to test. See README.md.
//
// There is no hal support for Ragnar: the app drives systemd directly, and
// fakes its state in the simulator so it opens on a PC.
#include "apps/common.h"
#include "ui/list_view.h"
#include "ui/power.h"
#include "apps/kit/look.h"

#include <cstdlib>
#include <string>

namespace zs {
namespace {
using namespace theme;

constexpr int kPort = 8000;

bool simulated() {
    static int s = -1;
    if (s < 0) s = hal().system().simulated ? 1 : 0;
    return s == 1;
}

// systemctl on a real device; true on exit 0.
bool sysctl(const char* verb, const char* unit) {
    std::string cmd = std::string("systemctl ") + verb + " " + unit + " >/dev/null 2>&1";
    return std::system(cmd.c_str()) == 0;
}

class RagnarApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        poll();
        rebuild();
    }

    void tick() override {
        if (dirty_) { dirty_ = false; rebuild(); return; }
        if (++n_ % 10) return;   // about once a second
        std::string was = sig_;
        poll();
        if (sig_ != was) rebuild();
    }

    bool on_key(uint32_t key) override { return list_.key(key); }

private:
    struct Status { bool running = false, failed = false, lan = false; std::string ip; };

    void poll() {
        if (simulated()) {
            st_.running = sim_running_;
            st_.failed = false;
            st_.lan = sim_lan_;
            st_.ip = sim_running_ ? "192.168.1.42" : "";
        } else {
            st_.running = sysctl("is-active --quiet", "ragnar.service");
            st_.failed = !st_.running && sysctl("is-failed --quiet", "ragnar.service");
            st_.lan = st_.running && sysctl("is-active --quiet", "zeroos-ragnar-lan.service");
            st_.ip = st_.running ? hal().net().ip : std::string();
        }
        if (st_.running || st_.failed) starting_ = false;
        sig_ = std::to_string(st_.running) + std::to_string(st_.failed) +
               std::to_string(st_.lan) + std::to_string(starting_) + st_.ip;
    }

    std::string local_url() const { return fmt("http://localhost:%d", kPort); }
    std::string lan_url() const {
        return st_.ip.empty() ? std::string() : fmt("http://%s:%d", st_.ip.c_str(), kPort);
    }

    void start() {
        shell_confirm("Start Ragnar?",
                      "It scans and probes every device on the network you are on. "
                      "Only use it on networks you own or are allowed to test.",
                      "Start", [this] {
            starting_ = true;
            if (simulated()) {
                sim_running_ = true;
            } else if (!sysctl("start", "ragnar.service")) {
                starting_ = false;
                shell_toast("Couldn't start. See journalctl -u ragnar");
            }
            if (starting_) shell_toast("Starting Ragnar...");
            dirty_ = true;
        });
    }

    void stop() {
        if (simulated()) { sim_running_ = false; sim_lan_ = false; }
        else sysctl("stop", "ragnar.service");
        starting_ = false;
        shell_toast("Ragnar off");
        dirty_ = true;
    }

    void set_lan(bool on) {
        if (simulated()) {
            sim_lan_ = on;
        } else if (!sysctl(on ? "start" : "stop", "zeroos-ragnar-lan.service")) {
            shell_toast("Couldn't change it. Update ZeroOS and try again");
            return;
        }
        shell_toast(on ? "The dashboard is open on this network"
                       : "Only this device and Tailscale can reach the dashboard");
        dirty_ = true;
    }

    void open_dashboard() {
        if (!st_.running) { shell_toast("Turn Ragnar on first"); return; }
        if (!app_find("browser")) { shell_toast("Needs the Browser app: get it from Apps"); return; }
        shell_open_app("browser", local_url());
    }

    void open_on_monitor() {
        if (!st_.running) { shell_toast("Turn Ragnar on first"); return; }
        if (!power().docked()) { shell_toast("Needs Dock mode (a monitor)"); return; }
        hal().dock_open("browser", local_url());
        shell_toast("Opening the dashboard on the monitor");
    }

    void rebuild() {
        lv_obj_clean(root_);
        list_.reset();
        if (group) lv_group_remove_all_objs(group);
        shell_set_actions({{"Select", 0, "Enter", nullptr}});

        // Left: status, or the dashboard QR code once it is allowed on the LAN.
        lv_obj_t* panel = lv_obj_create(root_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 4);
        lv_obj_set_size(panel, 150, kBodyH - 8);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(panel, 3, 0);

        std::string url = lan_url();
        if (st_.running && st_.lan && !url.empty()) {
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_t* qr = lv_qrcode_create(panel);
            lv_qrcode_set_size(qr, 92);
            lv_qrcode_set_dark_color(qr, c(0x000000));
            lv_qrcode_set_light_color(qr, c(0xffffff));
            lv_qrcode_update(qr, url.c_str(), uint32_t(url.size()));
            lv_obj_set_style_border_color(qr, c(0xffffff), 0);
            lv_obj_set_style_border_width(qr, 4, 0);
            label(panel, "Scan for the dashboard", &zs_font_12, kText);
            lv_obj_t* u = label(panel, url.c_str(), &zs_font_12, kMuted);
            lv_label_set_long_mode(u, LV_LABEL_LONG_DOT);
            lv_obj_set_width(u, 136);
        } else {
            lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            const char* head = st_.failed   ? "Stopped with an error"
                               : st_.running ? LV_SYMBOL_OK " Running"
                               : starting_   ? "Starting..."
                                             : "Ragnar off";
            label(panel, head, &zs_font_16, st_.failed ? kBad : st_.running ? kGood : kText);
            const char* hint =
                st_.failed    ? "It stopped soon after starting. Check journalctl -u ragnar; some Python parts may be missing (see the app's README)."
                : st_.running ? "The dashboard is at http://localhost:8000, and on your Tailscale network. Allow on this network to reach it from other devices here."
                : starting_   ? "Bringing up the service and the web dashboard..."
                              : "Autonomous network recon. Turn it on, then drive it from the web dashboard. Only on networks you own or may test.";
            lv_obj_t* h = label(panel, hint, &zs_font_12, kMuted);
            lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(h, 136);
        }

        // Right: actions.
        list_.build(root_, 162, 4, 152, kBodyH - 8);
        if (!st_.running && !starting_) {
            list_.add(LV_SYMBOL_POWER, "Turn on Ragnar", "", [this] { start(); });
        } else {
            list_.add(LV_SYMBOL_POWER, "Turn off Ragnar", "", [this] { stop(); });
        }
        if (st_.running) {
            list_.add(LV_SYMBOL_HOME, "Open dashboard", "", [this] { open_dashboard(); });
            list_.add(LV_SYMBOL_IMAGE, "Open on monitor", "", [this] { open_on_monitor(); });
            if (st_.lan)
                list_.add(LV_SYMBOL_CLOSE, "Block on network", "", [this] { set_lan(false); });
            else
                list_.add(LV_SYMBOL_WIFI, "Allow on this network", "", [this] { set_lan(true); });
        }
        list_.finish();
    }

    lv_obj_t* root_ = nullptr;
    ListView list_;
    Status st_;
    std::string sig_;
    bool starting_ = false, dirty_ = false;
    bool sim_running_ = false, sim_lan_ = false;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_ragnar_app() { return std::make_unique<RagnarApp>(); }

}  // namespace zs
