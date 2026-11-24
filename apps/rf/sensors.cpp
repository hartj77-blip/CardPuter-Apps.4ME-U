// Sensors: wireless weather stations, tyre sensors, doorbells and remotes
// decoded by rtl_433, one row per device with its latest readings.
//
// A List screen: Enter shows everything the selected device sent, typing
// filters the devices, and the menu (Tab) changes band (433.92, 315, 868.3,
// 915 MHz) and clears the list.
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "background_radio.h"
#include "apps/common.h"
#include "radio_link.h"
#include "json_flat.h"
#include "ui/list_view.h"
#include "apps/kit/look.h"

namespace zs {
namespace {
using namespace theme;

const double kBands[] = {433.92e6, 315e6, 868.3e6, 915e6};
const int kTopH = 18;
const size_t kMaxDevices = 40;

struct Device {
    std::string key, name;
    zrf::JsonFields fields;   // the latest message
    uint32_t last_seen = 0;   // lv_tick
    int count = 0;
    int row = -1;             // in the ListView
};

std::string age_text(uint32_t ms) {
    if (ms < 2000) return "now";
    if (ms < 60000) return fmt("%us", ms / 1000);
    if (ms < 3600000) return fmt("%um", ms / 60000);
    return fmt("%uh", ms / 3600000);
}

// The readings worth a place on the row, most useful first.
std::string summary(const zrf::JsonFields& f) {
    std::string out;
    auto add = [&](const std::string& s) { out += (out.empty() ? "" : "  ") + s; };
    auto num = [&](const char* key) -> const std::string* { return zrf::json_get(f, key); };
    if (auto v = num("temperature_C")) add(fmt("%.1f°C", std::atof(v->c_str())));
    if (auto v = num("humidity")) add(fmt("%.0f%%", std::atof(v->c_str())));
    if (auto v = num("pressure_kPa")) add(fmt("%.0f kPa", std::atof(v->c_str())));
    if (auto v = num("pressure_hPa")) add(fmt("%.0f hPa", std::atof(v->c_str())));
    if (auto v = num("wind_avg_m_s")) add(fmt("%.1f m/s", std::atof(v->c_str())));
    if (auto v = num("wind_avg_km_h")) add(fmt("%.0f km/h", std::atof(v->c_str())));
    if (out.empty()) {
        for (const char* key : {"button", "cmd", "code", "event", "state", "alarm", "data"})
            if (auto v = num(key)) { add(std::string(key) + " " + v->substr(0, 12)); break; }
    }
    if (auto v = num("battery_ok"); v && *v == "0") add("low batt");
    return out;
}

class SensorsApp : public App {
public:
    ~SensorsApp() override {
        if (timer_) lv_timer_delete(timer_);
    }

    void build(lv_obj_t* root) override {
        BackgroundRadio::get().stop();   // this app needs the receiver
        root_ = root;
        set_actions();
        title_ = label(root, "", &zs_font_14, kAccent);
        lv_obj_set_pos(title_, 4, 0);
        note_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_align(note_, LV_ALIGN_TOP_RIGHT, -4, 2);

        // The list lives in a box of its own, built afresh as devices appear.
        box_ = lv_obj_create(root);
        style_plain(box_);
        lv_obj_set_pos(box_, 0, kTopH);
        lv_obj_set_size(box_, kScreenW, kBodyH - kTopH);
        lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
        rebuild();

        detail_ = lv_obj_create(root);
        style_panel(detail_);
        lv_obj_set_pos(detail_, 4, kTopH);
        lv_obj_set_size(detail_, kScreenW - 8, kBodyH - kTopH - 2);
        detail_text_ = label(detail_, "", &zs_font_12, kText);
        lv_obj_set_width(detail_text_, kScreenW - 30);
        lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

        message_ = label(root, "", &zs_font_14, kMuted);
        lv_obj_set_style_text_align(message_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(message_, LV_ALIGN_CENTER, 0, 8);

        set_message("Starting radio...");
        update_title();
        timer_ = lv_timer_create([](lv_timer_t* t) { static_cast<SensorsApp*>(lv_timer_get_user_data(t))->pump(); },
                                 40, this);
        pump();
    }

    void tick() override {
        // Ages only need refreshing about once a second.
        if (++ticks_ % 10 != 0) return;
        for (auto& d : devices_) update_row(d);
    }

    bool on_key(uint32_t key) override {
        if (showing_ >= 0) {
            // Up/Down scroll the detail page.
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
        a.push_back({"Details", 0, "Enter", nullptr});
        a.push_back({fmt("Band: %g MHz (next)", kBands[band_] / 1e6), 'b', nullptr, [this] {
                         band_ = (band_ + 1) % int(std::size(kBands));
                         link_.client.send(fmt("tune %.0f", kBands[band_]));
                         clear();
                         update_title();
                         set_actions();
                     }});
        a.push_back({"Clear the list", 'c', nullptr, [this] {
                         clear();
                         update_title();
                     }});
        shell_set_actions(std::move(a));
    }

    // The rows again, keeping the focus where it was. Only when a device is
    // first heard: readings on known rows are changed in place.
    void rebuild() {
        int at = list_.focused();
        if (group) lv_group_remove_all_objs(group);
        lv_obj_clean(box_);
        list_.build(box_, 4, 2, kScreenW - 8, kBodyH - kTopH - 4);
        list_.set_empty_text("");   // the message in the middle says why
        for (size_t i = 0; i < devices_.size(); ++i) {
            devices_[i].row = list_.add(nullptr, devices_[i].name, "", [this, i] { open_detail(int(i)); });
            update_row(devices_[i]);
        }
        list_.finish();
        if (at >= 0 && at < list_.size()) list_.focus(at);
    }

    void pump() {
        bool fresh, unavailable;
        if (!link_.ensure(fresh, unavailable)) {
            if (unavailable) set_message("Radio service not available");
            return;
        }
        if (fresh) {
            link_.client.send(fmt("tune %.0f", kBands[band_]));
            link_.client.send("decode sensors");
            if (devices_.empty()) set_message("Listening for sensors...\nWeather stations, tyre sensors, remotes");
        }
        zrf::RadioEvents ev;
        if (!link_.client.poll(RadioLink::poll_wait_ms(), ev)) {
            link_.lost();
            set_message("Radio stopped. Restarting...");
            return;
        }
        if (ev.has_error) {
            if (ev.error == "no dongle") set_message("No radio found.\nPlug in an SDR dongle.");
            else if (ev.error == "program not installed") set_message("The sensor decoder (rtl_433)\nis not installed.");
            else if (devices_.empty()) set_message(("Radio: " + ev.error).c_str());
        }
        for (auto& line : ev.decoded) add_message(line);
        if (!ev.decoded.empty()) update_title();
    }

    void add_message(const std::string& line) {
        zrf::JsonFields f = zrf::parse_flat_json(line);
        const std::string* model = zrf::json_get(f, "model");
        if (!model) return;
        const std::string* id = zrf::json_get(f, "id");
        const std::string* channel = zrf::json_get(f, "channel");
        std::string key = *model + "|" + (id ? *id : "") + "|" + (channel ? *channel : "");
        ++messages_;

        auto it = std::find_if(devices_.begin(), devices_.end(), [&](const Device& d) { return d.key == key; });
        if (it == devices_.end()) {
            if (devices_.size() >= kMaxDevices) return;
            Device d;
            d.key = key;
            d.name = *model + (id ? " " + *id : "");
            devices_.push_back(std::move(d));
            rebuild();
            it = devices_.end() - 1;
            set_message("");
        }
        it->fields = std::move(f);
        it->last_seen = lv_tick_get();
        ++it->count;
        update_row(*it);
        if (showing_ == int(it - devices_.begin())) fill_detail();
    }

    void update_row(Device& d) {
        if (d.row < 0) return;
        std::string s = summary(d.fields);
        list_.set_value(d.row, s + (s.empty() ? "" : "   ") + age_text(lv_tick_elaps(d.last_seen)));
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
        const Device& d = devices_[size_t(showing_)];
        std::string text = fmt("%d messages\n", d.count);
        for (auto& kv : d.fields) {
            if (kv.first == "time") continue;
            text += kv.first + ":  " + kv.second.substr(0, 40) + "\n";
        }
        lv_label_set_text(detail_text_, text.c_str());
    }

    void clear() {
        if (showing_ >= 0) close_detail();
        devices_.clear();
        rebuild();
        messages_ = 0;
        set_message("Listening for sensors...\nWeather stations, tyre sensors, remotes");
    }

    void set_message(const char* text) {
        lv_label_set_text(message_, text);
        if (*text) lv_obj_remove_flag(message_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(message_, LV_OBJ_FLAG_HIDDEN);
    }

    void update_title() {
        if (showing_ >= 0) {
            lv_label_set_text(title_, devices_[size_t(showing_)].name.c_str());
            lv_label_set_text(note_, "");
            return;
        }
        lv_label_set_text(title_, fmt("%g MHz", kBands[band_] / 1e6).c_str());
        lv_label_set_text(note_, devices_.empty() ? "" : fmt("%d devices", int(devices_.size())).c_str());
    }

    RadioLink link_;
    lv_timer_t* timer_ = nullptr;
    lv_obj_t *root_ = nullptr, *title_ = nullptr, *note_ = nullptr, *box_ = nullptr;
    ListView list_;
    lv_obj_t *detail_ = nullptr, *detail_text_ = nullptr, *message_ = nullptr;
    std::vector<Device> devices_;
    int band_ = 0, showing_ = -1, messages_ = 0, ticks_ = 0;
};
}  // namespace

std::unique_ptr<App> make_sensors_app() { return std::make_unique<SensorsApp>(); }

}  // namespace zs
