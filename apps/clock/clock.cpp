// Clock with a stopwatch. Time comes from the RX8130CE RTC, so it is right
// even with no network. A Control screen (docs/UX.md in the OS): Space or
// Enter starts and stops the stopwatch, L takes a lap, R resets it; the
// soft-key bar says so.
#include <ctime>

#include "apps/common.h"
#include "apps/kit/look.h"

namespace zs {
namespace {
using namespace theme;

class ClockApp : public App {
public:
    void build(lv_obj_t* root) override {
        // Actions first: the shell then lays the app area out above the bar.
        shell_set_actions({{"Start/stop", 's', "Space", [this] { start_stop(); }},
                           {"Lap", 'l', "L", [this] { lap(); }},
                           {"Reset", 'r', "R", [this] { reset(); }}});
        time_ = label(root, "", &zs_font_28);
        lv_obj_align(time_, LV_ALIGN_TOP_MID, 0, 14);
        date_ = label(root, "", &zs_font_14, kMuted);
        lv_obj_align(date_, LV_ALIGN_TOP_MID, 0, 50);

        lv_obj_t* sw = lv_obj_create(root);
        style_panel(sw);
        lv_obj_set_size(sw, 220, 58);
        lv_obj_align(sw, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_remove_flag(sw, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* t = label(sw, LV_SYMBOL_BELL "  Stopwatch", &zs_font_12, kMuted);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
        sw_ = label(sw, "00:00.0", &zs_font_20, kAccent);
        lv_obj_align(sw_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lap_ = label(sw, "", &zs_font_12, kMuted);
        lv_obj_set_style_text_align(lap_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(lap_, LV_ALIGN_RIGHT_MID, 0, 0);
        tick();
    }

    void tick() override {
        time_t now = ::time(nullptr);
        tm lt{};
        localtime_r(&now, &lt);
        char buf[64];
        strftime(buf, sizeof buf, "%H:%M:%S", &lt);
        lv_label_set_text(time_, buf);
        strftime(buf, sizeof buf, "%A, %d %B %Y", &lt);
        lv_label_set_text(date_, buf);

        lv_label_set_text(sw_, stamp(now_ms()).c_str());
    }

    bool on_key(uint32_t key) override {
        switch (key) {
            case ' ':
            case LV_KEY_ENTER: start_stop(); return true;
            case 'l': case 'L': lap(); return true;
            case 'r': case 'R': reset(); return true;
        }
        return false;
    }

private:
    static std::string stamp(uint32_t ms) {
        return fmt("%02u:%02u.%u", ms / 60000, (ms / 1000) % 60, (ms / 100) % 10);
    }

    uint32_t now_ms() const { return elapsed_ + (running_ ? lv_tick_elaps(start_) : 0); }

    void start_stop() {
        if (running_) elapsed_ += lv_tick_elaps(start_);
        else start_ = lv_tick_get();
        running_ = !running_;
    }

    // A lap: the time since the last one, with the one before it underneath.
    // Only while the stopwatch runs.
    void lap() {
        if (!running_) return;
        uint32_t ms = now_ms();
        ++laps_;
        lines_ = fmt("Lap %d  %s", laps_, stamp(ms - lap_at_).c_str()) + (lines_.empty() ? "" : "\n") + lines_;
        if (size_t cut = lines_.find('\n', lines_.find('\n') + 1); cut != std::string::npos) lines_.resize(cut);
        lap_at_ = ms;
        lv_label_set_text(lap_, lines_.c_str());
    }

    void reset() {
        running_ = false;
        elapsed_ = lap_at_ = 0;
        laps_ = 0;
        lines_.clear();
        lv_label_set_text(lap_, "");
    }

    lv_obj_t *time_, *date_, *sw_, *lap_;
    bool running_ = false;
    uint32_t start_ = 0, elapsed_ = 0, lap_at_ = 0;
    int laps_ = 0;
    std::string lines_;
};
}  // namespace

std::unique_ptr<App> make_clock_app() { return std::make_unique<ClockApp>(); }

}  // namespace zs
