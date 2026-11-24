// Discover: who is on your network. Sweeps the local subnet with arp-scan
// (works the same on Ethernet or Wi-Fi) and lists every device with its IP,
// hardware vendor and MAC. Enter runs a port scan of the selected host.
// A List screen: typing filters the hosts, and the menu (Tab) has Rescan and
// the interface to scan on.
#include <algorithm>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/kit/ui.h"
#include "net_run.h"
#include "net_sim.h"
#include "apps/kit/suite.h"

namespace zs {
namespace {
using namespace theme;

struct Host {
    std::string ip, mac, vendor;
};

// 192.168.1.42 -> a sortable key (one byte per octet).
uint32_t ip_key(const std::string& ip) {
    uint32_t v = 0;
    int oct = 0;
    for (char ch : ip) {
        if (ch == '.') { v = (v << 8) | uint32_t(oct & 0xff); oct = 0; }
        else if (ch >= '0' && ch <= '9') oct = oct * 10 + (ch - '0');
    }
    return (v << 8) | uint32_t(oct & 0xff);
}

class DiscoverApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        set_actions();
        title_ = kit::title_row(root_, "");
        // The list lives in a box of its own, so it can be built afresh as
        // hosts answer (they are kept in address order).
        box_ = lv_obj_create(root_);
        style_plain(box_);
        lv_obj_set_pos(box_, 0, 18);
        lv_obj_set_size(box_, kScreenW, kit::body_h(root_) - 18);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
        scan();
    }

    void tick() override {
        if (!job_.started()) return;
        std::vector<std::string> lines;
        bool added = false;
        if (job_.drain(lines))
            for (auto& l : lines) added = add(l) || added;
        if (added) rebuild();
        update_title();
        if (!job_.running() && !done_) {
            done_ = true;
            if (hosts_.empty()) {
                using net::Done;
                net::Done d = job_.status();
                set_msg(d == Done::Missing   ? "arp-scan is not installed."
                        : d == Done::Failed  ? "Scan failed. Need a network connection?"
                                             : "No devices answered. Try another interface.");
            }
        }
    }

    bool on_key(uint32_t key) override { return list_.key(key); }

private:
    const char* iface_name() const { return iface_ == 1 ? "eth0" : iface_ == 2 ? "wlan0" : ""; }

    void set_actions() {
        std::vector<Action> a;
        a.push_back({"Scan ports", 0, "Enter", nullptr});
        a.push_back({"Rescan", 'r', nullptr, [this] { scan(); }});
        a.push_back({fmt("Interface: %s (next)", iface_ == 0 ? "auto" : iface_name()), 'f', nullptr, [this] {
                         iface_ = (iface_ + 1) % 3;   // auto, eth0, wlan0
                         set_actions();
                         scan();
                     }});
        shell_set_actions(std::move(a));
    }

    void scan() {
        job_.cancel();
        job_.reset();
        hosts_.clear();
        done_ = false;
        msg_ = "Scanning the network...";
        rebuild();
        update_title();

        std::vector<std::string> argv = {"arp-scan", "--quiet", "--ignoredups", "--retry=2"};
        if (iface_ == 0) argv.push_back("--localnet");
        else { argv.push_back("--localnet"); argv.push_back(std::string("--interface=") + iface_name()); }
        job_.start(argv, net::sim::arp_scan());
    }

    // True if the line was a new host.
    bool add(const std::string& line) {
        // arp-scan: "IP<TAB>MAC<TAB>Vendor". Ignore its header/footer lines.
        size_t t1 = line.find('\t');
        if (t1 == std::string::npos) return false;
        size_t t2 = line.find('\t', t1 + 1);
        Host h;
        h.ip = line.substr(0, t1);
        if (h.ip.find('.') == std::string::npos) return false;   // not an address line
        h.mac = line.substr(t1 + 1, (t2 == std::string::npos ? line.size() : t2) - t1 - 1);
        h.vendor = t2 == std::string::npos ? "" : line.substr(t2 + 1);
        if (std::any_of(hosts_.begin(), hosts_.end(), [&](const Host& o) { return o.ip == h.ip; })) return false;
        hosts_.push_back(h);
        // Keep the list in address order.
        std::sort(hosts_.begin(), hosts_.end(),
                  [](const Host& a, const Host& b) { return ip_key(a.ip) < ip_key(b.ip); });
        return true;
    }

    // The rows again, keeping the focus on the same host. A filter being
    // typed is lost, but hosts stop arriving within seconds of a scan.
    void rebuild() {
        int at = list_.focused();
        std::string keep = at >= 0 && at < int(shown_.size()) ? shown_[size_t(at)] : "";
        if (group) lv_group_remove_all_objs(group);
        lv_obj_clean(box_);
        list_.build(box_, 4, 2, kScreenW - 8, lv_obj_get_height(box_) - 4);
        list_.set_empty_text(msg_);
        shown_.clear();
        int focus = -1;
        for (const Host& h : hosts_) {
            // "192.168.1.10   Raspberry Pi"  (MAC when the vendor is unknown).
            std::string right = h.vendor.empty() ? h.mac : h.vendor;
            if (right.size() > 22) right = right.substr(0, 21) + "~";
            std::string ip = h.ip;
            int row = list_.add(LV_SYMBOL_WIFI, ip, right, [ip] { kit::suite_open("ports", ip); });
            if (ip == keep) focus = row;
            shown_.push_back(ip);
        }
        list_.finish();
        if (focus >= 0) list_.focus(focus);
    }

    void update_title() {
        const char* on = iface_ == 0 ? "network" : iface_name();
        lv_label_set_text(title_, fmt("%d device%s on %s", int(hosts_.size()),
                                      hosts_.size() == 1 ? "" : "s", on).c_str());
    }

    void set_msg(const char* m) {
        msg_ = m;
        rebuild();
    }

    lv_obj_t *root_ = nullptr, *title_ = nullptr, *box_ = nullptr;
    ListView list_;
    std::vector<Host> hosts_;
    std::vector<std::string> shown_;   // the hosts' addresses, in row order
    std::string msg_;
    net::Job job_;
    int iface_ = 0;
    bool done_ = false;
};
}  // namespace

std::unique_ptr<App> make_discover_app() { return std::make_unique<DiscoverApp>(); }

}  // namespace zs
