// Capture: watch the traffic on the wire and save it. Runs tcpdump, showing a
// live one-line-per-packet summary and, at the same time, writing a .pcap to
// ~/Recordings that you can open in Wireshark on a PC (Sync copies it off).
//
// A Control screen: Space starts and stops, F picks the interface (auto,
// eth0, wlan0) and R clears the view; the soft-key bar shows them.
#include <sys/stat.h>

#include <ctime>
#include <string>
#include <vector>

#include "apps/common.h"
#include "net_run.h"
#include "net_sim.h"
#include "apps/kit/ui.h"
#include "apps/kit/look.h"

namespace zs {
namespace {
using namespace theme;

class CaptureApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        set_actions();
        header_ = label(root_, "", &zs_font_14, kText);
        lv_obj_set_pos(header_, 4, 1);
        out_.build(root_, 18);
        out_.set_placeholder("Not capturing.\nPackets are saved to ~/Recordings.");
        update_header();
    }

    ~CaptureApp() override { job_.cancel(); }

    void tick() override {
        if (!capturing_) return;
        std::vector<std::string> lines;
        if (job_.drain(lines)) {
            for (auto& l : lines) { out_.append(l); ++packets_; }
            update_header();
        }
        if (!job_.running()) {   // tcpdump died (missing, or no permission)
            using net::Done;
            Done d = job_.status();
            if (d == Done::Missing) out_.append("tcpdump is not installed.");
            else if (packets_ == 0) out_.append("Could not start capture (no permission, or no interface).");
            stop();
        }
    }

    bool on_key(uint32_t key) override {
        if (key == ' ') { toggle(); return true; }
        if (key == 'r' || key == 'R') { clear(); return true; }
        if (key == 'f' || key == 'F') { next_iface(); return true; }
        return false;
    }

private:
    void set_actions() {
        std::vector<Action> a;
        a.push_back({capturing_ ? "Stop" : "Start", 's', "Space", [this] { toggle(); }});
        Action f{fmt("Interface: %s", iface_name()), 'f', "F", [this] { next_iface(); }};
        f.enabled = !capturing_;
        a.push_back(f);
        a.push_back({"Clear", 'r', "R", [this] { clear(); }});
        shell_set_actions(std::move(a));
    }

    void toggle() { capturing_ ? stop() : start(); }

    void clear() {
        out_.clear();
        packets_ = 0;
        update_header();
    }

    // Not while capturing: tcpdump is already listening on one.
    void next_iface() {
        if (capturing_) return;
        iface_ = (iface_ + 1) % 3;
        set_actions();
        update_header();
    }

    const char* iface_name() const { return iface_ == 1 ? "eth0" : iface_ == 2 ? "wlan0" : "auto"; }

    std::string new_pcap_path() {
        std::string dir = home_dir() + "/Recordings";
        ::mkdir(dir.c_str(), 0755);
        std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
        return dir + "/capture-" + stamp + ".pcap";
    }

    void start() {
        out_.clear();
        packets_ = 0;
        pcap_ = net::simulated() ? "~/Recordings/capture-sim.pcap" : new_pcap_path();
        job_.reset();
        capturing_ = true;

        // One tcpdump both prints a summary (--print) and writes the pcap (-w),
        // flushing each packet (-U) so a stop leaves a complete file.
        std::vector<std::string> argv = {"tcpdump", "-n", "-U", "-l", "--print", "-w", pcap_};
        if (iface_ != 0) { argv.push_back("-i"); argv.push_back(iface_name()); }
        job_.start(argv, net::sim::tcpdump());
        set_actions();
        update_header();
    }

    void stop() {
        job_.cancel();
        capturing_ = false;
        set_actions();
        update_header();
        if (packets_ > 0) shell_toast(fmt("Saved %d packets", packets_));
    }

    void update_header() {
        if (capturing_)
            lv_label_set_text(header_, fmt(LV_SYMBOL_PLAY " %d  %s", packets_, iface_name()).c_str());
        else if (packets_ > 0)
            lv_label_set_text(header_, fmt("%d packets  saved", packets_).c_str());
        else
            lv_label_set_text(header_, fmt("Capture  (%s)", iface_name()).c_str());
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* header_ = nullptr;
    kit::OutputView out_;
    net::Job job_;
    std::string pcap_;
    int packets_ = 0;
    int iface_ = 0;
    bool capturing_ = false;
};
}  // namespace

std::unique_ptr<App> make_capture_app() { return std::make_unique<CaptureApp>(); }

}  // namespace zs
