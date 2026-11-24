// APRS: amateur radio stations reporting their position, weather, status and
// messages on the packet frequency. One row per station, newest first in the
// order they were first heard.
//
// A List screen: Enter shows the station's last packet, typing filters the
// stations, and the menu (Tab) changes frequency and clears the list.
// Distances need your grid locator, set in Station.
#include <algorithm>
#include <string>
#include <vector>

#include "background_radio.h"
#include "apps/common.h"
#include "radio_link.h"
#include "aprs.h"
#include "ui/list_view.h"
#include "ui/prefs.h"
#include "apps/kit/look.h"

namespace zs {
namespace {
using namespace theme;

// kHz. The Station setting comes first; the menu steps through the rest.
const int kChannels[] = {144390, 144800, 145175, 144575, 145825};
const char* const kChannelNames[] = {"Americas", "Europe", "Australia", "New Zealand", "ISS and satellites"};
const size_t kMaxStations = 60;

struct Station {
    zrf::AprsPacket last;
    uint32_t last_seen = 0;
    int count = 0;
    // The most recent position survives later packets that carry none.
    bool has_position = false;
    double lat = 0, lon = 0;
    int row = -1;   // in the ListView
};

const char* compass(double bearing) {
    static const char* const names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return names[int((bearing + 22.5) / 45) % 8];
}

class AprsApp : public App {
public:
    ~AprsApp() override {
        if (timer_) lv_timer_delete(timer_);
    }

    void build(lv_obj_t* root) override {
        BackgroundRadio::get().stop();   // this app needs the receiver
        khz_ = prefs::get_int("rf.aprs_khz", 144390);
        have_home_ = zrf::grid_to_latlon(prefs::get_str("rf.grid", ""), home_lat_, home_lon_);

        set_actions();
        title_ = label(root, "", &zs_font_14, kAccent);
        lv_obj_set_pos(title_, 4, 0);
        note_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_align(note_, LV_ALIGN_TOP_RIGHT, -4, 2);
        // The list lives in a box of its own, built afresh as stations appear.
        box_ = lv_obj_create(root);
        style_plain(box_);
        lv_obj_set_pos(box_, 0, 18);
        lv_obj_set_size(box_, kScreenW, kBodyH - 18);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
        rebuild();

        detail_ = lv_obj_create(root);
        style_panel(detail_);
        lv_obj_set_pos(detail_, 4, 18);
        lv_obj_set_size(detail_, kScreenW - 8, kBodyH - 20);
        detail_text_ = label(detail_, "", &zs_font_12, kText);
        lv_obj_set_width(detail_text_, kScreenW - 30);
        lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

        message_ = label(root, "", &zs_font_14, kMuted);
        lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(message_, LV_ALIGN_CENTER, 0, 8);
        set_message("Starting radio...");
        update_title();
        timer_ = lv_timer_create([](lv_timer_t* t) { static_cast<AprsApp*>(lv_timer_get_user_data(t))->pump(); }, 40, this);
        pump();
    }

    void tick() override {
        if (++ticks_ % 10 != 0) return;
        for (auto& s : stations_) update_row(s);
    }

    bool on_key(uint32_t key) override {
        if (showing_ >= 0) {
            if (key == LV_KEY_DOWN) { lv_obj_scroll_by_bounded(detail_, 0, -24, LV_ANIM_OFF); return true; }
            if (key == LV_KEY_UP) { lv_obj_scroll_by_bounded(detail_, 0, 24, LV_ANIM_OFF); return true; }
            if (key == LV_KEY_ENTER) { close_detail(); return true; }
            return key == LV_KEY_LEFT || key == LV_KEY_RIGHT;
        }
        return list_.key(key);
    }

    bool on_back() override {
        if (showing_ < 0) return false;
        close_detail();
        return true;
    }

private:
    void set_actions() {
        if (showing_ >= 0) {
            shell_set_actions({{"Back", 0, "Enter", nullptr}, {"Scroll", 0, LV_SYMBOL_UP LV_SYMBOL_DOWN, nullptr}});
            return;
        }
        std::vector<Action> a;
        a.push_back({"Last packet", 0, "Enter", nullptr});
        a.push_back({fmt("Frequency: %.3f MHz (next)", khz_ / 1000.0), 'f', nullptr, [this] {
                         size_t i = 0;
                         while (i < std::size(kChannels) && kChannels[i] != khz_) ++i;
                         khz_ = kChannels[(i + 1) % std::size(kChannels)];
                         link_.client.send(fmt("tune %d000", khz_));
                         clear();
                         set_actions();
                     }});
        a.push_back({"Clear the list", 'c', nullptr, [this] { clear(); }});
        shell_set_actions(std::move(a));
    }

    // The rows again, keeping the focus where it was. Only when a station is
    // first heard: what known ones send is changed in place.
    void rebuild() {
        int at = list_.focused();
        if (group) lv_group_remove_all_objs(group);
        lv_obj_clean(box_);
        list_.build(box_, 4, 2, kScreenW - 8, kBodyH - 18 - 4);
        list_.set_empty_text("");   // the message in the middle says why
        for (size_t i = 0; i < stations_.size(); ++i) {
            stations_[i].row = list_.add(nullptr, stations_[i].last.source, "", [this, i] { open_detail(int(i)); });
            update_row(stations_[i]);
        }
        list_.finish();
        if (at >= 0 && at < list_.size()) list_.focus(at);
    }

    void pump() {
        bool fresh, unavailable;
        if (!link_.ensure(fresh, unavailable)) {
            if (unavailable) set_message(RadioLink::unavailable_text());
            return;
        }
        if (fresh) {
            link_.client.send(fmt("ppm %d", prefs::get_int("rf.ppm", 0)));
            link_.client.send(fmt("tune %d000", khz_));
            link_.client.send("decode aprs");
            if (stations_.empty()) set_message(waiting());
        }
        zrf::RadioEvents ev;
        if (!link_.client.poll(RadioLink::poll_wait_ms(), ev)) {
            link_.lost();
            set_message("Radio stopped. Restarting...");
            return;
        }
        if (ev.has_error) {
            if (ev.error == "no dongle") set_message("No radio found.\nPlug in an SDR dongle.");
            else if (ev.error == "program not installed") set_message("The packet decoder (multimon-ng)\nis not installed.");
            else if (stations_.empty()) set_message(("Radio: " + ev.error).c_str());
        }
        for (auto& line : ev.decoded) add_packet(line);
        if (!ev.decoded.empty()) update_title();
    }

    const char* waiting() const {
        return have_home_ ? "Listening for stations...\nPackets come every few minutes"
                          : "Listening for stations...\nSet your grid in Station for distances";
    }

    void add_packet(const std::string& line) {
        zrf::AprsPacket p;
        if (!zrf::parse_aprs(line, p)) return;
        auto it = std::find_if(stations_.begin(), stations_.end(), [&](const Station& s) { return s.last.source == p.source; });
        if (it == stations_.end()) {
            if (stations_.size() >= kMaxStations) return;
            Station s;
            s.last = p;   // its name, for the row
            stations_.push_back(std::move(s));
            rebuild();
            it = stations_.end() - 1;
            set_message("");
        }
        if (p.has_position) { it->has_position = true; it->lat = p.lat; it->lon = p.lon; }
        it->last = std::move(p);
        it->last_seen = lv_tick_get();
        ++it->count;
        update_row(*it);
        if (showing_ == int(it - stations_.begin())) fill_detail();
    }

    std::string where(const Station& s) const {
        if (!s.has_position) return "";
        if (!have_home_) return fmt("%.2f, %.2f", s.lat, s.lon);
        double km, bearing;
        zrf::distance_bearing(home_lat_, home_lon_, s.lat, s.lon, km, bearing);
        return km < 10 ? fmt("%.1f km %s", km, compass(bearing)) : fmt("%.0f km %s", km, compass(bearing));
    }

    void update_row(Station& s) {
        if (s.row < 0) return;
        uint32_t age = lv_tick_elaps(s.last_seen) / 1000;
        std::string when = age < 2 ? "now" : age < 60 ? fmt("%us", age) : age < 3600 ? fmt("%um", age / 60) : fmt("%uh", age / 3600);
        std::string w = where(s);
        std::string what = s.last.kind == "message" ? "msg" : s.last.kind == "position" ? "" : s.last.kind;
        std::string text = w + (w.empty() || what.empty() ? "" : "  ") + what;
        list_.set_value(s.row, text + (text.empty() ? "" : "   ") + when);
    }

    void open_detail(int index) {
        showing_ = index;
        fill_detail();
        lv_obj_scroll_to_y(detail_, 0, LV_ANIM_OFF);
        lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(detail_, LV_OBJ_FLAG_HIDDEN);
        set_actions();
        update_title();
    }

    void close_detail() {
        showing_ = -1;
        lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_HIDDEN);
        set_actions();
        update_title();
    }

    void fill_detail() {
        const Station& s = stations_[size_t(showing_)];
        const zrf::AprsPacket& p = s.last;
        std::string t = fmt("%d packets, last: %s\n", s.count, p.kind.c_str());
        if (p.kind == "message") t += "To " + p.addressee + ": " + p.text + "\n";
        else if (!p.text.empty()) t += p.text + "\n";
        if (s.has_position) {
            t += fmt("Position %.4f, %.4f", s.lat, s.lon);
            std::string w = where(s);
            if (have_home_) t += "  (" + w + ")";
            t += "\n";
        } else if (p.kind == "Mic-E") {
            t += "Position is in a compact form not decoded yet\n";
        }
        if (!p.path.empty()) t += "Via " + p.path + "\n";
        t += "To " + p.destination + "\n";
        lv_label_set_text(detail_text_, t.c_str());
    }

    void clear() {
        if (showing_ >= 0) close_detail();
        stations_.clear();
        rebuild();
        set_message(waiting());
        update_title();
    }

    void set_message(const char* text) {
        lv_label_set_text(message_, text);
        if (*text) lv_obj_remove_flag(message_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(message_, LV_OBJ_FLAG_HIDDEN);
    }

    void update_title() {
        if (showing_ >= 0) {
            lv_label_set_text(title_, stations_[size_t(showing_)].last.source.c_str());
            lv_label_set_text(note_, "");
            return;
        }
        lv_label_set_text(title_, fmt("%.3f MHz", khz_ / 1000.0).c_str());
        const char* region = "";
        for (size_t i = 0; i < std::size(kChannels); ++i)
            if (kChannels[i] == khz_) region = kChannelNames[i];
        if (stations_.empty()) lv_label_set_text(note_, region);
        else lv_label_set_text(note_, fmt("%d stations", int(stations_.size())).c_str());
    }

    RadioLink link_;
    lv_timer_t* timer_ = nullptr;
    ListView list_;
    lv_obj_t *title_ = nullptr, *note_ = nullptr, *box_ = nullptr, *detail_ = nullptr, *detail_text_ = nullptr, *message_ = nullptr;
    std::vector<Station> stations_;
    int khz_ = 144390, showing_ = -1, ticks_ = 0;
    bool have_home_ = false;
    double home_lat_ = 0, home_lon_ = 0;
};
}  // namespace

std::unique_ptr<App> make_aprs_app() { return std::make_unique<AprsApp>(); }

}  // namespace zs
