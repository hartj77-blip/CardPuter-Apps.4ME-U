// Spectrum: live spectrum trace over a waterfall, from the radio daemon.
//
// A Control screen: its letters are shortcuts, each in the soft-key bar and
// the menu (Tab). Left/Right tune by one step, Up/Down change the step, Z/X
// narrow or widen the view, G cycles gain, P toggles peak hold, C centres on
// the strongest signal, M cycles listening (off, broadcast FM, narrow FM, AM,
// upper and lower sideband, CW), S toggles squelch, B opens the band list, R
// records raw IQ to ~/Recordings. F follows a transceiver's dial (set the rig
// up in Station) and T sends this frequency to it. Enter, then a frequency in
// MHz and Enter again, jumps to it. Audio and recording stop on leaving.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "apps/common.h"
#include <sys/stat.h>

#include "background_radio.h"
#include "bands.h"
#include "radio_link.h"
#include "rf_state.h"
#include "rig_link.h"
#include "memories.h"
#include "ui/list_view.h"
#include "ui/prefs.h"
#include "apps/kit/look.h"

namespace zs {
namespace {
using namespace theme;

const int kW = kScreenW;
const int kTopH = 18;                      // frequency line
const int kTraceH = 44;
const int kFallH = 60;                     // down to the band edges, above the soft-key bar
const int kCanvasH = kTraceH + kFallH;
const int kRangeDb = 60;                   // shown above the noise floor

const double kMinHz = 0.5e6, kMaxHz = 1766e6;   // RTL-SDR Blog V4 range
const double kSteps[] = {10, 100, 1e3, 10e3, 100e3, 1e6, 10e6};
const double kSpans[] = {250e3, 1.024e6, 2.048e6};   // sample rates an RTL-SDR accepts
const int kGains[] = {-1, 0, 10, 20, 30, 40};       // -1 = automatic
const char* const kModes[] = {"off", "wfm", "nfm", "am", "usb", "lsb", "cw"};
const char* const kModeNames[] = {"", "FM", "NFM", "AM", "USB", "LSB", "CW"};
const int kSquelchDb = 10;   // above the noise floor

uint16_t rgb565(int r, int g, int b) { return uint16_t((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }
uint16_t rgb565(uint32_t hex) { return rgb565(hex >> 16 & 255, hex >> 8 & 255, hex & 255); }

std::string hz_text(double hz) {
    if (hz >= 1e6) return fmt("%g M", hz / 1e6);
    if (hz >= 1e3) return fmt("%g k", hz / 1e3);
    return fmt("%g Hz", hz);
}

class SpectrumApp : public App {
public:
    ~SpectrumApp() override {
        // Nothing but our own clean-up here: this also runs when the whole
        // program exits, after the settings store has gone.
        if (timer_) lv_timer_delete(timer_);
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        set_actions();
        freq_label_ = label(root, "", &zs_font_14, kAccent);
        lv_obj_set_pos(freq_label_, 4, 0);
        info_label_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_align(info_label_, LV_ALIGN_TOP_RIGHT, -4, 2);

        pixels_.assign(size_t(kW) * kCanvasH, rgb565(kBg));
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_buffer(canvas_, pixels_.data(), kW, kCanvasH, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(canvas_, 0, kTopH);

        message_ = label(root, "", &zs_font_14, kText);
        look::raised(message_);
        lv_obj_set_style_pad_all(message_, 6, 0);
        lv_obj_align(message_, LV_ALIGN_CENTER, 0, 10);

        low_label_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_align(low_label_, LV_ALIGN_BOTTOM_LEFT, 4, -1);
        high_label_ = label(root, "", &zs_font_12, kMuted);
        lv_obj_align(high_label_, LV_ALIGN_BOTTOM_RIGHT, -4, -1);
        hint_label_ = label(root, "", &zs_font_12, kMuted);
        // Between the two band-edge frequencies; long station text is cut short.
        lv_obj_set_width(hint_label_, 212);
        lv_label_set_long_mode(hint_label_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(hint_label_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(hint_label_, LV_ALIGN_BOTTOM_MID, 0, -1);

        // Blue -> cyan -> yellow -> white, weakest to strongest.
        for (int i = 0; i < 256; ++i) {
            float t = i / 255.0f;
            auto ramp = [t](float a, float b) { return int(255 * std::clamp((t - a) / (b - a), 0.0f, 1.0f)); };
            int blue = int(40 + 215 * std::clamp(t / 0.3f, 0.0f, 1.0f)) - ramp(0.5f, 0.8f);
            palette_[i] = rgb565(ramp(0.45f, 0.8f), ramp(0.15f, 0.65f), std::max(blue, ramp(0.85f, 1.0f)));
        }

        // Where we were last time.
        double saved = std::atof(prefs::get_str("rf.spec_hz", "0").c_str());
        if (saved >= kMinHz && saved <= kMaxHz) freq_ = saved;
        span_ = std::clamp(prefs::get_int("rf.spec_span", span_), 0, int(std::size(kSpans)) - 1);
        step_ = std::clamp(prefs::get_int("rf.spec_step", step_), 0, int(std::size(kSteps)) - 1);
        gain_ = std::clamp(prefs::get_int("rf.spec_gain", gain_), 0, int(std::size(kGains)) - 1);
        squelch_ = prefs::get_int("rf.spec_squelch", 0) != 0;
        keep_playing_ = prefs::get_int("rf.keep_playing", 0) != 0;
        // If the radio was left playing in the background, carry on from it.
        BackgroundRadio& bg = BackgroundRadio::get();
        if (bg.playing()) {
            freq_ = bg.hz();
            for (int i = 1; i < int(std::size(kModes)); ++i)
                if (bg.mode() == kModes[i]) mode_ = i;
            mode_picked_ = true;
            adopt_background_ = true;
        }

        saved_key_ = state_key();
        set_message("Starting radio...");
        update_labels();
        timer_ = lv_timer_create([](lv_timer_t* t) { static_cast<SpectrumApp*>(lv_timer_get_user_data(t))->pump(); },
                                 40, this);
        pump();
    }

    bool on_key(uint32_t key) override {
        // The band list or the saved channels: a List screen over the picture.
        if (bands_open_ || mems_open_) {
            if (pick_.key(key)) return true;
            // Enter goes to the row; Esc, Tab and the shell's keys go on.
            return key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT;
        }
        // Naming a channel being saved (a Text screen until Enter or Esc).
        if (naming_) {
            if (key == LV_KEY_ENTER) {
                zrf::Memory m;
                m.hz = freq_;
                m.mode = mode_ ? kModes[mode_] : suggested_mode(freq_);
                m.squelch = squelch_;
                m.name = name_.empty() ? fmt("%.4f MHz", freq_ / 1e6) : name_;
                memories_ = zrf::load_memories(memory_path());
                memories_.push_back(m);
                shell_toast(save_memories() ? "Saved " + m.name : "Could not save the channel");
                naming_ = false;
            } else if (key == LV_KEY_BACKSPACE) {
                if (!name_.empty()) name_.pop_back();
            } else if (key >= 32 && key < 127) {
                if (name_.size() < 20) name_ += char(key);
            } else if (key != LV_KEY_UP && key != LV_KEY_DOWN && key != LV_KEY_LEFT && key != LV_KEY_RIGHT) {
                return false;   // Esc (cancels), Tab and the shell's keys
            }
            update_labels();
            return true;
        }
        // Typing a frequency, after Enter.
        if (entering_) {
            if ((key >= '0' && key <= '9') || key == '.') {
                if (entry_.size() < 9) entry_ += char(key);
            } else if (key == LV_KEY_BACKSPACE) {
                if (!entry_.empty()) entry_.pop_back();
            } else if (key == LV_KEY_ENTER) {
                double mhz = std::atof(entry_.c_str());
                entering_ = false;
                if (!entry_.empty()) {
                    if (mhz * 1e6 >= kMinHz && mhz * 1e6 <= kMaxHz) tune(mhz * 1e6);
                    else shell_toast("Out of range (0.5 to 1766 MHz)");
                }
                entry_.clear();
            } else if (key < 32 || key >= 127) {
                // Esc (cancels), Tab and the shell's keys go on; arrows do nothing.
                if (key != LV_KEY_UP && key != LV_KEY_DOWN && key != LV_KEY_LEFT && key != LV_KEY_RIGHT) return false;
            }
            // Other letters do nothing while typing.
            update_labels();
            return true;
        }

        switch (key) {
        case LV_KEY_LEFT: tune(freq_ - kSteps[step_]); return true;
        case LV_KEY_RIGHT: tune(freq_ + kSteps[step_]); return true;
        case LV_KEY_UP: step_ = std::min(step_ + 1, int(std::size(kSteps)) - 1); break;
        case LV_KEY_DOWN: step_ = std::max(step_ - 1, 0); break;
        case 'z': case 'Z': set_span(span_ - 1); break;
        case 'x': case 'X': set_span(span_ + 1); break;
        case 'g': case 'G':
            gain_ = (gain_ + 1) % int(std::size(kGains));
            send_gain();
            break;
        case 'm': case 'M':
            // The audio recording is one mode; changing it ends the file.
            if (taping_) {
                taping_ = false;
                shell_toast("Saved " + tape_name_);
            }
            // The first press picks the mode that suits this frequency: the
            // right sideband on an amateur band, AM on the airband, and so on.
            if (mode_ == 0 && !mode_picked_) {
                const char* m = suggested_mode(freq_);
                for (int i = 1; i < int(std::size(kModes)); ++i)
                    if (std::strcmp(kModes[i], m) == 0) mode_ = i;
                mode_picked_ = true;
            } else {
                mode_ = (mode_ + 1) % int(std::size(kModes));
            }
            client_.send(std::string("mode ") + kModes[mode_]);
            break;
        case 's': case 'S':
            squelch_ = !squelch_;
            send_squelch();
            shell_toast(squelch_ ? "Squelch on" : "Squelch off");
            break;
        case 'b': case 'B': show_bands(true); return true;
        case 'a': case 'A':
            naming_ = true;
            name_.clear();
            break;
        case 'l': case 'L':
            memories_ = zrf::load_memories(memory_path());
            if (memories_.empty()) shell_toast("No saved channels yet. Save this one first.");
            else show_memories(true);
            return true;
        case 'k': case 'K':
            keep_playing_ = !keep_playing_;
            prefs::set_int("rf.keep_playing", keep_playing_ ? 1 : 0);
            shell_toast(keep_playing_ ? "Will keep playing when you leave" : "Will stop playing when you leave");
            break;
        case 'r': case 'R': toggle_record(); break;
        case 'w': case 'W':
            if (taping_) {
                taping_ = false;
                client_.send("recordaudio off");
                shell_toast("Saved " + tape_name_);
            } else if (!mode_) {
                shell_toast("Listen first (M)");
            } else {
                std::string dir = home_dir() + "/Recordings";
                mkdir(dir.c_str(), 0755);
                char when[32];
                time_t t = time(nullptr);
                tm lt{};
                localtime_r(&t, &lt);
                strftime(when, sizeof when, "%Y%m%d-%H%M%S", &lt);
                tape_name_ = fmt("radio_%.4fMHz_%s.wav", freq_ / 1e6, when);
                client_.send("recordaudio " + dir + "/" + tape_name_);
                taping_ = true;
                shell_toast("Recording the audio");
            }
            break;
        case 'f': case 'F':
            if (rig_.active()) {
                rig_.stop();
                shell_toast("Stopped following the rig");
            } else if (!RigLink::configured()) {
                shell_toast("Set up your rig in Station first");
            } else {
                rig_.start();
                shell_toast("Following the rig's dial");
            }
            break;
        case 't': case 'T':
            if (!rig_.active() || !rig_.connected()) shell_toast("Not connected to a rig (F follows it)");
            else { rig_.set_freq(freq_); shell_toast(fmt("Rig set to %.5f MHz", freq_ / 1e6)); }
            break;
        case LV_KEY_ENTER:
            entering_ = true;   // type a frequency
            entry_.clear();
            break;
        case 'p': case 'P':
            peak_hold_ = !peak_hold_;
            std::fill(peak_.begin(), peak_.end(), 0);
            shell_toast(peak_hold_ ? "Peak hold on" : "Peak hold off");
            break;
        case 'c': case 'C':
            if (!last_.empty()) {
                int best = int(std::max_element(last_.begin(), last_.end()) - last_.begin());
                tune(freq_ + (best + 0.5 - kW / 2.0) * kSpans[span_] / kW);
            }
            return true;
        default: return false;
        }
        update_labels();
        return true;
    }

    // Run a shortcut from the menu (Tab, or Ctrl + its letter) as its key.
    void press(uint32_t key) {
        on_key(key);
    }

    bool on_back() override {
        if (bands_open_) { show_bands(false); return true; }
        if (mems_open_) { show_memories(false); return true; }
        if (naming_) { naming_ = false; update_labels(); return true; }
        if (entering_) {
            entering_ = false;
            entry_.clear();
            update_labels();
            return true;
        }
        leaving();
        return false;
    }

private:
    // Runs every 40 ms: keep the connection up and draw what has arrived.
    void pump() {
        bool fresh, unavailable;
        // The rig's dial moves the display even while the radio is starting.
        double rig_hz = 0;
        bool rig_lost = false;
        if (rig_.poll(rig_hz, rig_lost) && std::fabs(rig_hz - freq_) >= 1) tune(rig_hz);
        if (rig_lost) {
            rig_.stop();
            shell_toast("Rig not answering. Check Station and the cable.");
            update_labels();
        }
        if (state_key() != saved_key_ && ++unsaved_ticks_ > 50) save_state();
        if (!link_.ensure(fresh, unavailable)) {
            if (unavailable) set_message(RadioLink::unavailable_text());
            return;
        }
        if (fresh) {
            // Station settings: crystal correction and power for an antenna amplifier.
            client_.send(fmt("ppm %d", prefs::get_int("rf.ppm", 0)));
            client_.send(prefs::get_int("rf.biastee", 0) ? "biastee on" : "biastee off");
            if (recording_) {
                // The radio service restarted; whatever was recording has stopped.
                recording_ = false;
                shell_toast("Recording stopped (radio restarted)");
                update_labels();
            }
            client_.send(fmt("rate %.0f", kSpans[span_]));
            send_gain();
            client_.send(fmt("tune %.0f", freq_));
            client_.send(fmt("spectrum %d 15", kW));
            send_squelch();
            if (mode_) client_.send(std::string("mode ") + kModes[mode_]);
            // We now hold the audio ourselves; the background connection can go
            // without the sound stopping.
            if (adopt_background_) {
                BackgroundRadio::get().release();
                adopt_background_ = false;
            }
        }

        zrf::RadioEvents ev;
        if (!client_.poll(RadioLink::poll_wait_ms(), ev)) {
            link_.lost();
            set_message("Radio stopped. Restarting...");
            return;
        }
        auto& frames = ev.frames;
        if (ev.has_station) {
            // Broadcast FM: stereo or not, and the station's name and text from RDS.
            stereo_ = ev.stereo;
            station_name_ = ev.station_name;
            while (!station_name_.empty() && station_name_.back() == ' ') station_name_.pop_back();
            station_text_ = ev.station_text;
            update_labels();
        }
        if (ev.has_error && ev.error != error_) {
            error_ = ev.error;
            if (error_.rfind("audio recording stopped", 0) == 0 || error_ == "can't write the audio recording") {
                taping_ = false;
                shell_toast(error_ == "can't write the audio recording" ? "Can't write the audio recording" : "Audio recording stopped: storage full");
                update_labels();
                error_.clear();
            } else if (error_.rfind("recording stopped", 0) == 0 || error_ == "can't write the recording") {
                recording_ = false;
                shell_toast(error_ == "recording stopped: storage full" ? "Recording stopped: storage full"
                            : error_ == "recording stopped: retuned" ? "Recording stopped (retuned)" : "Can't write the recording");
                update_labels();
                error_.clear();
            } else if (error_ == "audio output failed") shell_toast("No audio output");
            else if (error_ == "no dongle") set_message("No radio found.\nPlug in an SDR dongle.");
            else if (!error_.empty()) set_message(("Radio: " + error_).c_str());
        }
        bool drew = false;
        for (auto& f : frames) {
            // Frames still in flight from before a retune would smear the picture.
            if (int(f.cols.size()) != kW || std::fabs(f.freq - freq_) > 1 || std::fabs(f.rate - kSpans[span_]) > 1) continue;
            add_row(f.cols);
            drew = true;
        }
        if (drew) {
            // Strength of what is tuned in: the strongest of the middle few
            // columns against the noise floor (levels are 0.5 dB steps).
            uint8_t mid = *std::max_element(last_.begin() + kW / 2 - 3, last_.begin() + kW / 2 + 4);
            signal_db_ = (mid - floor_) / 2;
            if (mode_ && ++label_ticks_ % 8 == 0) update_labels();
            error_.clear();
            set_message("");
            draw_trace();
            lv_obj_invalidate(canvas_);
        }
    }

    void tune(double hz) {
        hz = std::clamp(hz, kMinHz, kMaxHz);
        if (hz == freq_) return;
        // Slide the picture so signals stay under their frequency.
        shift_columns(int(std::lround((freq_ - hz) / kSpans[span_] * kW)));
        freq_ = hz;
        station_name_.clear();   // a new frequency is a new station
        station_text_.clear();
        stereo_ = false;
        client_.send(fmt("tune %.0f", freq_));
        update_labels();
        lv_obj_invalidate(canvas_);
    }

    void set_span(int index) {
        index = std::clamp(index, 0, int(std::size(kSpans)) - 1);
        if (index == span_) return;
        span_ = index;
        shift_columns(kW);   // the old picture is at the wrong scale: clear it
        client_.send(fmt("rate %.0f", kSpans[span_]));
        lv_obj_invalidate(canvas_);
    }

    void toggle_record() {
        if (recording_) {
            recording_ = false;
            client_.send("record off");
            shell_toast("Saved " + record_name_);
            return;
        }
        std::string dir = home_dir() + "/Recordings";
        mkdir(dir.c_str(), 0755);
        char when[32];
        time_t t = time(nullptr);
        tm lt{};
        localtime_r(&t, &lt);
        strftime(when, sizeof when, "%Y%m%d-%H%M%S", &lt);
        record_name_ = fmt("iq_%.4fMHz_%s.cu8", freq_ / 1e6, when);
        client_.send("record " + dir + "/" + record_name_);
        recording_ = true;
        shell_toast(fmt("Recording, %.1f MB a second", kSpans[span_] * 2 / 1e6));
    }

    void send_squelch() {
        client_.send(squelch_ ? fmt("squelch %d", kSquelchDb) : std::string("squelch off"));
    }

    static std::string memory_path() {
        std::string dir = home_dir() + "/.config";
        mkdir(dir.c_str(), 0755);
        dir += "/zeroos";
        mkdir(dir.c_str(), 0755);
        return dir + "/rf-memories.tsv";
    }

    bool save_memories() { return zrf::save_memories(memory_path(), memories_); }

    // A list over the picture (the bands, or the saved channels), in a
    // bordered box. Its rows' actions run after the key, so they may close it.
    void open_pick(int w) {
        close_pick();
        pick_box_ = lv_obj_create(root_);
        style_plain(pick_box_);
        look::raised(pick_box_);
        int h = kBodyH - kTopH - 2;
        lv_obj_set_pos(pick_box_, (kW - w) / 2, kTopH);
        lv_obj_set_size(pick_box_, w, h);
        lv_obj_remove_flag(pick_box_, LV_OBJ_FLAG_SCROLLABLE);
        pick_.build(pick_box_, 2, 2, w - 6, h - 6);
    }

    void close_pick() {
        if (group) lv_group_remove_all_objs(group);
        pick_.reset();
        if (pick_box_) lv_obj_delete(pick_box_);
        pick_box_ = nullptr;
    }

    // The list is rebuilt each time it opens: channels come and go.
    void show_memories(bool on) {
        mems_open_ = on;
        close_pick();
        if (on) {
            open_pick(260);
            for (size_t i = 0; i < memories_.size(); ++i) {
                const zrf::Memory& m = memories_[i];
                std::string mode = m.mode == "off" ? "" : m.mode;
                for (char& ch : mode) ch = char(std::toupper(static_cast<unsigned char>(ch)));
                pick_.add(nullptr, m.name, fmt("%.4f %s", m.hz / 1e6, mode.c_str()), [this, i] { use_memory(i); });
            }
            pick_.finish();
        }
        set_actions();
    }

    // The saved channel in focus goes (the shell has asked first).
    void delete_memory() {
        int i = pick_.focused();
        if (!mems_open_ || i < 0 || i >= int(memories_.size())) return;
        shell_toast("Deleted " + memories_[size_t(i)].name);
        memories_.erase(memories_.begin() + i);
        save_memories();
        show_memories(!memories_.empty());
    }

    void use_memory(size_t i) {
        if (!mems_open_ || i >= memories_.size()) return;
        const zrf::Memory m = memories_[i];
        show_memories(false);
        tune(m.hz);
        squelch_ = m.squelch;
        send_squelch();
        // Listen the way the channel was saved, if already listening;
        // otherwise the next M press picks it.
        int wanted = 0;
        for (int k = 1; k < int(std::size(kModes)); ++k)
            if (m.mode == kModes[k]) wanted = k;
        if (mode_ && wanted) {
            mode_ = wanted;
            client_.send(std::string("mode ") + kModes[wanted]);
        }
        mode_picked_ = false;
        update_labels();
    }

    // Come back to the same place next time. Written a couple of seconds
    // after the last change, so holding an arrow key is one write, not fifty.
    void save_state() {
        prefs::set_str("rf.spec_hz", fmt("%.0f", freq_));
        prefs::set_int("rf.spec_span", span_);
        prefs::set_int("rf.spec_step", step_);
        prefs::set_int("rf.spec_gain", gain_);
        prefs::set_int("rf.spec_squelch", squelch_ ? 1 : 0);
        saved_key_ = state_key();
        unsaved_ticks_ = 0;
    }
    std::string state_key() const { return fmt("%.0f/%d/%d/%d/%d", freq_, span_, step_, gain_, squelch_ ? 1 : 0); }

    // Esc is about to take us home.
    void leaving() {
        if (state_key() != saved_key_) save_state();
        // Keep playing in the background if asked to and there is something on.
        if (mode_ && keep_playing_ && client_.connected()) {
            if (BackgroundRadio::get().start(freq_, kSpans[span_], kModes[mode_], squelch_))
                shell_toast(fmt("Still playing %.4f MHz. Open Spectrum to stop.", freq_ / 1e6));
        }
    }

    void show_bands(bool on) {
        bands_open_ = on;
        close_pick();
        if (on) {
            open_pick(200);
            for (size_t i = 0; i < std::size(kBands); ++i) pick_.add(nullptr, kBands[i].name, "", [this, i] { use_band(i); });
            pick_.finish();
        }
        set_actions();
    }

    void use_band(size_t i) {
        if (!bands_open_) return;
        const Band& b = kBands[i];
        show_bands(false);
        set_span(b.span);
        tune(b.home);
        // A new band: the next M press picks its mode afresh; if already
        // listening, switch to it at once.
        mode_picked_ = false;
        if (mode_)
            for (int k = 1; k < int(std::size(kModes)); ++k)
                if (std::strcmp(kModes[k], b.mode) == 0) {
                    mode_ = k;
                    client_.send(std::string("mode ") + kModes[k]);
                }
        // Sideband and Morse want fine tuning steps.
        step_ = b.mode[0] == 'u' || b.mode[0] == 'l' || b.mode[0] == 'c' ? 2 : 4;
        update_labels();
    }

    // What the keys do now, for the soft-key bar, the menu and the key sheet.
    // Set again only when something in it changed: update_labels() runs
    // several times a second while listening.
    void set_actions() {
        std::vector<Action> a;
        if (bands_open_ || mems_open_) {
            a.push_back({"Tune", 0, "Enter", nullptr});
            a.push_back({"Close", 0, "Esc", nullptr});
            if (mems_open_) {
                Action d{"Delete", 'd', nullptr, [this] { delete_memory(); }, true, true};
                d.confirm = "Delete this saved channel?";
                a.push_back(d);
            }
        } else if (naming_) {
            a.push_back({"Save channel", 0, "Enter", nullptr});
            a.push_back({"Cancel", 0, "Esc", nullptr});
        } else if (entering_) {
            a.push_back({"Go", 0, "Enter", nullptr});
            a.push_back({"Cancel", 0, "Esc", nullptr});
        } else {
            auto key = [this](uint32_t k) { return [this, k] { press(k); }; };
            a.push_back({"Go to MHz", 0, "Enter", key(LV_KEY_ENTER)});
            a.push_back({mode_ ? std::string("Listen: ") + kModeNames[mode_] : "Listen", 'm', "M", key('m')});
            a.push_back({"Bands", 'b', "B", key('b')});
            a.push_back({"Tune", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
            a.push_back({"Step", 0, LV_SYMBOL_UP LV_SYMBOL_DOWN, nullptr});
            a.push_back({squelch_ ? "Squelch off" : "Squelch on", 's', "S", key('s')});
            a.push_back({"Narrower view", 'z', "Z", key('z')});
            a.push_back({"Wider view", 'x', "X", key('x')});
            a.push_back({"Gain (next)", 'g', "G", key('g')});
            a.push_back({"Save channel", 'a', "A", key('a')});
            a.push_back({"Saved channels", 'l', "L", key('l')});
            a.push_back({keep_playing_ ? "Stop playing on leaving" : "Keep playing on leaving", 'k', "K", key('k')});
            a.push_back({peak_hold_ ? "Peak hold off" : "Peak hold on", 'p', "P", key('p')});
            a.push_back({"Centre on peak", 'c', "C", key('c')});
            a.push_back({taping_ ? "Stop audio recording" : "Record audio", 'w', "W", key('w')});
            a.push_back({recording_ ? "Stop IQ recording" : "Record IQ", 'r', "R", key('r')});
            a.push_back({rig_.active() ? "Stop following rig" : "Follow rig", 'f', "F", key('f')});
            a.push_back({"Set rig to this", 't', "T", key('t')});
        }
        std::string sig;
        for (auto& x : a) sig += x.label + "|";
        if (sig == actions_sig_) return;
        actions_sig_ = sig;
        shell_set_actions(std::move(a));
    }

    void send_gain() {
        client_.send(kGains[gain_] < 0 ? std::string("gain auto") : fmt("gain %d", kGains[gain_]));
    }

    // Move the waterfall and held peaks dx pixels right (negative = left).
    void shift_columns(int dx) {
        if (dx == 0) return;
        const uint16_t bg = rgb565(kBg);
        std::vector<uint8_t> peak(kW, 0);
        for (int x = 0; x < kW; ++x)
            if (x - dx >= 0 && x - dx < kW) peak[size_t(x)] = peak_[size_t(x - dx)];
        peak_ = peak;
        last_.clear();
        for (int y = kTraceH; y < kCanvasH; ++y) {
            uint16_t* row = &pixels_[size_t(y) * kW];
            if (std::abs(dx) >= kW) { std::fill(row, row + kW, bg); continue; }
            if (dx > 0) {
                std::memmove(row + dx, row, size_t(kW - dx) * 2);
                std::fill(row, row + dx, bg);
            } else {
                std::memmove(row, row - dx, size_t(kW + dx) * 2);
                std::fill(row + kW + dx, row + kW, bg);
            }
        }
        std::fill(pixels_.begin(), pixels_.begin() + long(kW) * kTraceH, bg);
    }

    void add_row(const std::vector<uint8_t>& cols) {
        // Follow the noise floor slowly so the colours don't pump.
        std::vector<uint8_t> sorted = cols;
        std::nth_element(sorted.begin(), sorted.begin() + kW / 4, sorted.end());
        float floor_now = sorted[kW / 4];
        floor_ = floor_ < 0 ? floor_now : floor_ * 0.9f + floor_now * 0.1f;

        uint16_t* fall = &pixels_[size_t(kTraceH) * kW];
        std::memmove(fall + kW, fall, size_t(kW) * (kFallH - 1) * 2);
        for (int x = 0; x < kW; ++x) fall[x] = palette_[size_t(level(cols[size_t(x)]) * 255)];
        last_ = cols;
        for (int x = 0; x < kW; ++x) peak_[size_t(x)] = std::max(peak_[size_t(x)], cols[size_t(x)]);
    }

    // 0..1 within the shown range above the noise floor. Levels are 0.5 dB steps.
    float level(uint8_t v) const {
        return std::clamp((v - (floor_ - 6)) / float(kRangeDb * 2), 0.0f, 1.0f);
    }

    void draw_trace() {
        const uint16_t bg = rgb565(kBg), fill = rgb565(0x12384a), line = rgb565(0x39c5cf);
        const uint16_t centre = rgb565(kSurface2), held = rgb565(kAccent);
        for (int x = 0; x < kW; ++x) {
            int top = int((1 - level(last_[size_t(x)])) * (kTraceH - 1));
            for (int y = 0; y < kTraceH; ++y)
                pixels_[size_t(y) * kW + x] = y < top ? (x == kW / 2 ? centre : bg) : y == top ? line : fill;
            if (peak_hold_) {
                int p = int((1 - level(peak_[size_t(x)])) * (kTraceH - 1));
                if (p < top) pixels_[size_t(p) * kW + x] = held;
            }
        }
    }

    void set_message(const char* text) {
        lv_label_set_text(message_, text);
        if (*text) lv_obj_remove_flag(message_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(message_, LV_OBJ_FLAG_HIDDEN);
    }

    void update_labels() {
        // Let the Logbook fill a new contact from where we are.
        g_rf_last_hz = freq_;
        g_rf_last_mode = mode_ ? kModes[mode_] : "";
        if (naming_) {
            lv_label_set_text(freq_label_, ("Name: " + name_ + "_").c_str());
        } else if (entering_) {
            lv_label_set_text(freq_label_, ("Go to " + entry_ + "_ MHz").c_str());
        } else {
            lv_label_set_text(freq_label_, fmt("%.5f MHz", freq_ / 1e6).c_str());
        }
        std::string gain = kGains[gain_] < 0 ? "auto" : fmt("%d dB", kGains[gain_]);
        std::string info = fmt("step %s", hz_text(kSteps[step_]).c_str());
        if (mode_) info += fmt("  " LV_SYMBOL_VOLUME_MAX " %s%s", kModeNames[mode_], squelch_ ? " SQ" : "");
        else info += fmt("  gain %s", gain.c_str());
        if (recording_) info = LV_SYMBOL_SAVE " REC  " + info;
        if (taping_) info = LV_SYMBOL_AUDIO " REC  " + info;
        if (rig_.active()) info = LV_SYMBOL_LOOP " " + info;
        lv_label_set_text(info_label_, info.c_str());
        double half = kSpans[span_] / 2;
        lv_label_set_text(low_label_, fmt("%.3f", (freq_ - half) / 1e6).c_str());
        lv_label_set_text(high_label_, fmt("%.3f", (freq_ + half) / 1e6).c_str());
        // Bottom line: the band we're in, then the signal strength while
        // listening (the soft-key bar has the keys).
        const Band* band = band_at(freq_);
        std::string hint = band ? std::string(band->name) + "   " : "";
        if (mode_ == 1 && !station_name_.empty())
            hint = station_name_ + (stereo_ ? "  stereo" : "") + (station_text_.empty() ? "" : "   " + station_text_);
        else if (mode_ && signal_db_ > -100) hint += fmt("signal %+.0f dB%s", signal_db_, mode_ == 1 && stereo_ ? "  stereo" : "");
        lv_label_set_text(hint_label_, hint.c_str());
        set_actions();
    }

    RadioLink link_;
    zrf::RadioClient& client_ = link_.client;
    lv_timer_t* timer_ = nullptr;
    lv_obj_t *canvas_ = nullptr, *freq_label_ = nullptr, *info_label_ = nullptr, *message_ = nullptr;
    lv_obj_t *low_label_ = nullptr, *high_label_ = nullptr, *hint_label_ = nullptr;
    std::vector<uint16_t> pixels_;
    uint16_t palette_[256] = {};
    std::vector<uint8_t> last_, peak_ = std::vector<uint8_t>(kW, 0);
    float floor_ = -1;
    double freq_ = 98.3e6;
    int step_ = 4, span_ = 1, gain_ = 0, mode_ = 0;
    bool mode_picked_ = false;
    float signal_db_ = -200;   // tuned signal above the noise floor
    bool stereo_ = false, taping_ = false;
    std::string station_name_, station_text_, tape_name_;
    int label_ticks_ = 0;
    RigLink rig_;
    bool squelch_ = false, bands_open_ = false, recording_ = false;
    std::string record_name_;
    lv_obj_t *root_ = nullptr, *pick_box_ = nullptr;
    ListView pick_;   // the bands or the saved channels, while one is open
    std::string actions_sig_;
    std::vector<zrf::Memory> memories_;
    bool mems_open_ = false, naming_ = false, entering_ = false;
    bool keep_playing_ = false, adopt_background_ = false;
    std::string name_, saved_key_;
    int unsaved_ticks_ = 0;
    bool peak_hold_ = false;
    std::string entry_, error_;
};
}  // namespace

std::unique_ptr<App> make_spectrum_app() { return std::make_unique<SpectrumApp>(); }

}  // namespace zs
