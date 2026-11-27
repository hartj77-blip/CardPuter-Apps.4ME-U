// Maps: an offline moving map (README.md). The map is a .zmap file
// (zmap.h) made from OpenStreetMap data; the position comes from the GPS
// module through gpsd, as in the GPS app. A Control screen (docs/UX.md in
// the OS); the soft-key bar shows its keys:
//   Enter   follow my position again (after panning)
//   - =     zoom out, zoom in
//   arrows  pan (W A S D too)
//   N       north up / heading up
//   T       day / night colours        U  miles / kilometres
//   I       about this map
// Opened with a word "lat,lon" (shell_open_app("maps", "40.7357,-74.1724"))
// it shows that place.
#include <dirent.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/maps/render.h"
#include "apps/maps/zmap.h"
#include "ui/power.h"
#include "ui/prefs.h"
#include "ui/tracker.h"

namespace zs {
namespace {
using namespace theme;

constexpr int kW = kScreenW, kH = kBodyH;
// Metres a pixel at each zoom step.
const double kZoom[] = {0.75, 1.2, 2, 3, 5, 8, 12, 20, 30, 50, 80, 120, 200, 300, 500, 800, 1200, 2000};
constexpr int kZooms = int(sizeof kZoom / sizeof kZoom[0]);
constexpr int kDriveZoom = 2;

double angle_towards(double from, double to, double share) {
    double d = std::fmod(to - from + 540.0, 360.0) - 180.0;
    return std::fmod(from + d * share + 360.0, 360.0);
}

class MapsApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        opened_ = std::time(nullptr);
        hal().set_ext_power(true);
        load_maps();
        if (!map_) {
            shell_set_actions({});
            lv_obj_t* t = label(root, "No map installed", &zs_font_16, kText);
            lv_obj_align(t, LV_ALIGN_CENTER, 0, -22);
            lv_obj_t* d = label(root, problem_.empty() ? "Reinstall Maps from Apps, or put a\n.zmap file in the Maps folder."
                                                       : problem_.c_str(),
                                &zs_font_12, kMuted);
            lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(d, LV_ALIGN_CENTER, 0, 10);
            return;
        }
        night_ = prefs::get_int("maps.night", 1) != 0;
        north_up_ = prefs::get_int("maps.north", 0) != 0;
        std::string units = prefs::get_str("maps.units", "");
        imperial_ = units.empty() ? map_->imperial() : units == "mi";
        drive_zoom_ = std::clamp(prefs::get_int("maps.zoom", kDriveZoom), 0, kZooms - 1);

        pixels_.assign(size_t(kW) * kH, 0);
        canvas_ = lv_canvas_create(root);
        lv_canvas_set_buffer(canvas_, pixels_.data(), kW, kH, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(canvas_, 0, 0);

        status_ = chip(root, &zs_font_12);
        lv_obj_set_pos(status_, 4, 4);
        speed_box_ = lv_obj_create(root);
        lv_obj_remove_style_all(speed_box_);
        lv_obj_set_size(speed_box_, 52, 34);
        lv_obj_set_pos(speed_box_, 4, kH - 38);
        lv_obj_set_style_bg_color(speed_box_, c(kBg), 0);
        lv_obj_set_style_bg_opa(speed_box_, LV_OPA_90, 0);
        lv_obj_set_style_radius(speed_box_, 4, 0);
        speed_ = label(speed_box_, "", &zs_font_20, kText);
        lv_obj_align(speed_, LV_ALIGN_TOP_MID, 0, 0);
        speed_unit_ = label(speed_box_, "", &zs_font_12, kMuted);
        lv_obj_align(speed_unit_, LV_ALIGN_BOTTOM_MID, 0, 1);
        // The limit, as on a US sign: black on white.
        limit_ = label(root, "", &zs_font_16, 0x000000);
        lv_obj_set_style_bg_color(limit_, c(0xffffff), 0);
        lv_obj_set_style_bg_opa(limit_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(limit_, c(0x000000), 0);
        lv_obj_set_style_border_width(limit_, 1, 0);
        lv_obj_set_style_radius(limit_, 3, 0);
        lv_obj_set_style_pad_hor(limit_, 4, 0);
        lv_obj_set_style_pad_ver(limit_, 3, 0);
        lv_obj_set_pos(limit_, 60, kH - 30);
        road_ = chip(root, &zs_font_14);
        lv_label_set_long_mode(road_, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(road_, LV_TEXT_ALIGN_CENTER, 0);
        scale_text_ = chip(root, &zs_font_12);
        scale_bar_ = lv_obj_create(root);
        lv_obj_remove_style_all(scale_bar_);
        lv_obj_set_style_bg_opa(scale_bar_, LV_OPA_COVER, 0);

        // Where to start: the place asked for, the last place, or the whole map.
        const zmap::Header& h = map_->header();
        cx_ = (double(h.min_x) + h.max_x) / 2, cy_ = (double(h.min_y) + h.max_y) / 2;
        zoom_ = overview_zoom();
        overview_ = true;
        double lat = 0, lon = 0;
        std::string arg = shell_app_arg();
        bool asked = std::sscanf(arg.c_str(), "%lf,%lf", &lat, &lon) == 2;
        bool known = asked || std::sscanf(prefs::get_str("maps.last", "").c_str(), "%lf,%lf", &lat, &lon) == 2;
        if (known && std::fabs(lat) < 85 && map_->contains(zmap::lon_to_x(lon), zmap::lat_to_y(lat))) {
            cx_ = zmap::lon_to_x(lon), cy_ = zmap::lat_to_y(lat);
            zoom_ = drive_zoom_;
            overview_ = false;
        }
        follow_ = !asked;
        set_actions();
        update_overlays();
        redraw();
        shell_toast(map_->name() + "  \xC2\xA9 OpenStreetMap contributors");
    }

    ~MapsApp() override {
        if (!map_) {
            release_gps();
            return;
        }
        if (have_pos_ && !outside_) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "%.5f,%.5f", zmap::y_to_lat(pos_y_), zmap::x_to_lon(pos_x_));
            prefs::set_str("maps.last", buf);
        }
        release_gps();
    }

    void tick() override {
        if (!map_) return;
        uint32_t now = lv_tick_get();
        read_gps(now);
        if (follow_ && have_pos_ && !outside_) {
            cx_ = pos_x_, cy_ = pos_y_;
            double want = north_up_ ? 0 : heading_;
            up_ = std::fabs(std::fmod(want - up_ + 540.0, 360.0) - 180.0) < 0.5 ? want : angle_towards(up_, want, 0.3);
        }
        if (++n_ % 10 == 0) update_overlays();
        if (power().screen_off()) return;   // nobody is looking
        // Redraw when the picture would change by a pixel or so, but never
        // spend more than about a third of the time drawing.
        double moved = zmap::metres_between(cx_, cy_, drawn_cx_, drawn_cy_) / kZoom[zoom_];
        double turned = std::fabs(std::fmod(up_ - drawn_up_ + 540.0, 360.0) - 180.0);
        double marker = have_pos_ ? zmap::metres_between(pos_x_, pos_y_, drawn_px_, drawn_py_) / kZoom[zoom_] : 0;
        bool changed = dirty_ || moved >= 0.7 || turned >= 1.0 || marker >= 0.7 || have_pos_ != drawn_pos_;
        if (changed && now - drawn_at_ >= std::clamp<uint32_t>(draw_ms_ * 3, 90, 1500)) redraw();
    }

    bool on_key(uint32_t key) override {
        if (!map_) return false;
        switch (key) {
            case LV_KEY_ENTER: centre(); return true;
            case '=': case '+': zoom_by(-1); return true;
            case '-': case '_': zoom_by(1); return true;
            case LV_KEY_UP: case 'w': case 'W': pan(0, -1); return true;
            case LV_KEY_DOWN: case 's': case 'S': pan(0, 1); return true;
            case LV_KEY_LEFT: case 'a': case 'A': pan(-1, 0); return true;
            case LV_KEY_RIGHT: case 'd': case 'D': pan(1, 0); return true;
            case 'n': case 'N': toggle_north(); return true;
            case 't': case 'T': toggle_night(); return true;
            case 'u': case 'U': toggle_units(); return true;
            case 'i': case 'I': about(); return true;
            default: return false;
        }
    }

private:
    static lv_obj_t* chip(lv_obj_t* parent, const lv_font_t* font) {
        lv_obj_t* l = label(parent, "", font, kText);
        lv_obj_set_style_bg_color(l, c(kBg), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_90, 0);
        lv_obj_set_style_radius(l, 3, 0);
        lv_obj_set_style_pad_hor(l, 4, 0);
        lv_obj_set_style_pad_ver(l, 1, 0);
        return l;
    }

    // Every .zmap in ZS_MAPS_DIR (tests, the simulator), ~/Maps and the
    // one the package brings. The first that opens is used.
    void load_maps() {
        std::vector<std::string> dirs;
        if (const char* e = std::getenv("ZS_MAPS_DIR")) dirs.push_back(e);
        dirs.push_back(home_dir() + "/Maps");
        dirs.push_back("/usr/share/zeroos/maps");
        for (const std::string& dir : dirs) {
            std::vector<std::string> files;
            if (DIR* d = opendir(dir.c_str())) {
                while (dirent* e = readdir(d)) {
                    std::string n = e->d_name;
                    if (n.size() > 5 && n.compare(n.size() - 5, 5, ".zmap") == 0) files.push_back(dir + "/" + n);
                }
                closedir(d);
            }
            std::sort(files.begin(), files.end());
            for (const std::string& f : files) {
                auto m = std::make_unique<zmap::Map>();
                if (m->open(f)) {
                    map_ = std::move(m);
                    return;
                }
                problem_ = f.substr(f.rfind('/') + 1) + ": " + m->error();
            }
        }
    }

    void release_gps() {
        if (track_state().recording) return;   // the track still needs the GPS
        hal().gps_stop();
        if (!hal().mesh().running) hal().set_ext_power(false);
    }

    int overview_zoom() const {
        const zmap::Header& h = map_->header();
        double upm = zmap::units_per_metre(zmap::y_to_lat((double(h.min_y) + h.max_y) / 2));
        double need = std::max((double(h.max_y) - h.min_y) / upm / (kH - 8), (double(h.max_x) - h.min_x) / upm / (kW - 8));
        for (int i = 0; i < kZooms; i++)
            if (kZoom[i] >= need) return i;
        return kZooms - 1;
    }

    // The position between fixes: a GPS reports once a second, the screen
    // redraws more often, so the last fix is carried forward at its speed.
    void read_gps(uint32_t now) {
        fix_ = hal().gps();
        if (!fix_.device || fix_.mode < 2 || std::fabs(fix_.lat) > 85) {
            if (have_pos_) dirty_ = true;
            have_pos_ = false;
            return;
        }
        double fx = zmap::lon_to_x(fix_.lon), fy = zmap::lat_to_y(fix_.lat);
        if (fx != fix_x_ || fy != fix_y_ || fix_.time != fix_time_) {
            fix_x_ = fx, fix_y_ = fy, fix_time_ = fix_.time, fix_at_ = now;
        }
        double tx = fix_x_, ty = fix_y_;
        moving_ = fix_.speed_mps > 1.0;   // slower than a walk is the receiver wandering
        if (moving_) {
            double m = fix_.speed_mps * std::min<uint32_t>(now - fix_at_, 1500) / 1000.0;
            double upm = zmap::units_per_metre(fix_.lat), rad = fix_.track_deg * zmap::kPi / 180;
            tx += std::sin(rad) * m * upm, ty -= std::cos(rad) * m * upm;
            heading_ = have_heading_ ? angle_towards(heading_, fix_.track_deg, 0.3) : fix_.track_deg;
            have_heading_ = true;
        }
        bool jump = !have_pos_ || zmap::metres_between(pos_x_, pos_y_, tx, ty) > 150;
        pos_x_ = jump ? tx : pos_x_ + (tx - pos_x_) * 0.4;
        pos_y_ = jump ? ty : pos_y_ + (ty - pos_y_) * 0.4;
        bool was_outside = outside_;
        outside_ = !map_->contains(pos_x_, pos_y_);
        if (!have_pos_ || was_outside != outside_) {
            // A first fix on the map: go there, at driving scale.
            if (!outside_ && follow_ && overview_) {
                zoom_ = drive_zoom_;
                overview_ = false;
            }
            dirty_ = true;
            have_pos_ = true;
            update_overlays();
        }
        have_pos_ = true;
    }

    zmap::View view() const {
        zmap::View v;
        v.cx = cx_, v.cy = cy_, v.mpp = kZoom[zoom_], v.up = up_;
        v.w = kW, v.h = kH;
        v.px = kW / 2;
        // Following with the heading up: more of the road ahead than behind.
        v.py = follow_ && have_pos_ && !outside_ && !north_up_ ? kH * 2 / 3 : kH / 2;
        return v;
    }

    void redraw() {
        auto t0 = std::chrono::steady_clock::now();
        zmap::View v = view();
        const zmap::Theme& t = night_ ? zmap::night_theme() : zmap::day_theme();
        std::vector<zmap::Rect> keep_out = {{kW - 26, 0, kW, 26}, {kW - 84, kH - 20, kW, kH}};
        if (*lv_label_get_text(status_)) keep_out.push_back({0, 0, 170, 22});
        if (have_pos_) keep_out.push_back({0, kH - 40, 96, kH});
        if (*lv_label_get_text(road_)) keep_out.push_back({96, kH - 24, 252, kH});
        renderer_.draw(*map_, v, t, pixels_.data(), labels_, keep_out);
        if (have_pos_) {
            double sx, sy;
            zmap::Renderer::to_screen(v, pos_x_, pos_y_, sx, sy);
            if (sx > -12 && sy > -12 && sx < kW + 12 && sy < kH + 12)
                renderer_.marker(v, t, pixels_.data(), sx, sy, heading_ - up_, have_heading_);
        }
        renderer_.compass(v, t, pixels_.data(), kW - 13, 13);
        draw_labels(t);
        lv_obj_invalidate(canvas_);
        update_scale();
        drawn_cx_ = cx_, drawn_cy_ = cy_, drawn_up_ = up_, drawn_px_ = pos_x_, drawn_py_ = pos_y_, drawn_pos_ = have_pos_;
        dirty_ = false;
        drawn_at_ = lv_tick_get();
        draw_ms_ = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    }

    // The names, upright whichever way the map is turned: road numbers in
    // a box, the rest with an outline so they read over anything.
    void draw_labels(const zmap::Theme& t) {
        lv_layer_t layer;
        lv_canvas_init_layer(canvas_, &layer);
        for (const zmap::Label& l : labels_) {
            const lv_font_t* font = l.kind == zmap::kCity ? &zs_font_16 : l.kind == zmap::kTown ? &zs_font_14 : &zs_font_12;
            lv_point_t size;
            lv_text_get_size(&size, l.text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_area_t a = {l.x - size.x / 2, l.y - size.y / 2, l.x - size.x / 2 + size.x, l.y - size.y / 2 + size.y};
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = font;
            d.text = l.text;
            if (l.shield) {
                lv_draw_rect_dsc_t r;
                lv_draw_rect_dsc_init(&r);
                r.bg_color = c(l.kind == zmap::kMotorway ? t.motorway_shield : t.shield);
                r.radius = 3;
                r.border_color = c(t.shield_text);
                r.border_width = 1;
                lv_area_t box = {a.x1 - 4, a.y1 - 1, a.x2 + 3, a.y2};
                lv_draw_rect(&layer, &r, &box);
                d.color = c(t.shield_text);
                lv_draw_label(&layer, &d, &a);
                continue;
            }
            d.color = c(t.halo);
            static const int kAround[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
            for (auto& o : kAround) {
                lv_area_t b = {a.x1 + o[0], a.y1 + o[1], a.x2 + o[0], a.y2 + o[1]};
                lv_draw_label(&layer, &d, &b);
            }
            d.color = c(t.text);
            lv_draw_label(&layer, &d, &a);
        }
        lv_canvas_finish_layer(canvas_, &layer);
    }

    // A round distance that is at most 70 pixels long.
    void update_scale() {
        static const double kMetric[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000};
        static const double kFeet[] = {50, 100, 200, 500, 1000, 2640, 5280, 10560, 26400, 52800, 105600, 264000, 528000};
        double mpp = kZoom[zoom_], best = 0;
        for (double v : imperial_ ? kFeet : kMetric) {
            double m = imperial_ ? v * 0.3048 : v;
            if (m / mpp <= 70) best = v;
        }
        if (best == 0) best = imperial_ ? 50 : 10;
        int px = std::max(4, int(std::lround((imperial_ ? best * 0.3048 : best) / mpp)));
        std::string text = imperial_ ? (best < 2640 ? fmt("%.0f ft", best) : best < 5280 ? "1/2 mi" : fmt("%.0f mi", best / 5280))
                                     : (best < 1000 ? fmt("%.0f m", best) : fmt("%.0f km", best / 1000));
        if (text != scale_shown_) {
            scale_shown_ = text;
            lv_label_set_text(scale_text_, text.c_str());
        }
        lv_obj_set_style_bg_color(scale_bar_, c(night_ ? 0xffffff : 0x202020), 0);
        lv_obj_set_size(scale_bar_, px, 2);
        lv_obj_set_pos(scale_bar_, kW - 6 - px, kH - 5);
        lv_obj_align(scale_text_, LV_ALIGN_BOTTOM_RIGHT, -6, -8);
    }

    // The boxes over the map: speed, limit, the road's name, and what the
    // GPS is doing when it has no position.
    void update_overlays() {
        long waited = long(std::time(nullptr) - opened_);
        std::string status;
        if (!fix_.device) status = waited < 6 ? "Starting GPS..." : "No GPS module found";
        else if (fix_.mode < 2) status = fmt("Looking for satellites (%d)", fix_.sats_seen);
        else if (outside_) status = "You're outside this map";
        set_text(status_, status);
        bool on_map = have_pos_ && !outside_;
        zmap::RoadHit road;
        if (on_map) {
            road = zmap::nearest_road(*map_, pos_x_, pos_y_, moving_ ? heading_ : -1, 30 + std::min(fix_.error_m, 40.0));
            double shown = fix_.speed_mps * (imperial_ ? 2.23694 : 3.6);
            int limit = road.speed & 0x7f;
            bool mph = road.speed & 0x80;
            if (limit && mph != imperial_) limit = int(std::lround(limit * (imperial_ ? 0.621371 : 1.609344)));
            set_text(speed_, fmt("%.0f", shown));
            set_text(speed_unit_, imperial_ ? "mph" : "km/h");
            lv_obj_set_style_text_color(speed_, c(limit && shown > limit + (imperial_ ? 5 : 8) ? kBad : kText), 0);
            set_text(limit_, limit ? fmt("%d", limit) : "");
            // Keep the screen on while driving; parked, it may sleep as usual.
            if (fix_.speed_mps > 2.5) power().stay_awake(lv_tick_get());
        }
        set_hidden(speed_box_, !on_map);
        set_hidden(limit_, !on_map || !*lv_label_get_text(limit_));
        std::string name = !on_map || !road.found ? "" : *road.name ? road.name : road.ref;
        if (name != lv_label_get_text(road_)) {
            // As wide as the name, up to the room between the speed and the scale.
            lv_point_t size;
            lv_text_get_size(&size, name.c_str(), &zs_font_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_obj_set_width(road_, std::min<int>(size.x, 142) + 10);
            set_text(road_, name);
            lv_obj_align(road_, LV_ALIGN_BOTTOM_MID, 14, -3);
        }
        set_hidden(road_, name.empty());
        set_hidden(status_, status.empty());
    }

    void set_text(lv_obj_t* l, const std::string& text) {
        if (text == lv_label_get_text(l)) return;
        lv_label_set_text(l, text.c_str());
        dirty_ = true;   // the names on the map keep clear of these boxes
    }
    static void set_hidden(lv_obj_t* o, bool hidden) {
        if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }

    void set_actions() {
        shell_set_actions({{"Centre", 'c', "Enter", [this] { centre(); }},
                           {"Zoom", 0, "- =", nullptr},
                           {north_up_ ? "Heading up" : "North up", 'n', "N", [this] { toggle_north(); }},
                           {night_ ? "Day colours" : "Night colours", 't', nullptr, [this] { toggle_night(); }},
                           {imperial_ ? "Kilometres" : "Miles", 'u', nullptr, [this] { toggle_units(); }},
                           {"About this map", 'i', nullptr, [this] { about(); }}});
    }

    void centre() {
        if (!have_pos_) {
            shell_toast(fix_.device ? "No GPS fix yet" : "No GPS module found");
            return;
        }
        if (outside_) {
            shell_toast("You're outside this map");
            return;
        }
        follow_ = true;
        if (overview_) zoom_ = drive_zoom_;
        overview_ = false;
        dirty_ = true;
    }

    void zoom_by(int steps) {
        int z = std::clamp(zoom_ + steps, 0, kZooms - 1);
        if (z == zoom_) return;
        zoom_ = z;
        overview_ = false;
        if (follow_ && have_pos_ && !outside_) {   // the scale to come back to
            drive_zoom_ = zoom_;
            prefs::set_int("maps.zoom", zoom_);
        }
        redraw();
    }

    void pan(int dx, int dy) {
        zmap::View v = view();
        double wx, wy;
        zmap::Renderer::to_world(v, v.px + dx * 48, v.py + dy * 40, wx, wy);
        const zmap::Header& h = map_->header();
        cx_ = std::clamp(wx, double(h.min_x), double(h.max_x));
        cy_ = std::clamp(wy, double(h.min_y), double(h.max_y));
        follow_ = false;
        redraw();
    }

    void toggle_north() {
        north_up_ = !north_up_;
        prefs::set_int("maps.north", north_up_);
        if (north_up_) up_ = 0;
        set_actions();
        redraw();
    }
    void toggle_night() {
        night_ = !night_;
        prefs::set_int("maps.night", night_);
        set_actions();
        redraw();
    }
    void toggle_units() {
        imperial_ = !imperial_;
        prefs::set_str("maps.units", imperial_ ? "mi" : "km");
        set_actions();
        update_overlays();
        redraw();
    }
    void about() {
        shell_toast(map_->name() + ", data of " + map_->date() + ". \xC2\xA9 OpenStreetMap contributors");
    }

    lv_obj_t *root_ = nullptr, *canvas_ = nullptr;
    lv_obj_t *status_ = nullptr, *speed_box_ = nullptr, *speed_ = nullptr, *speed_unit_ = nullptr;
    lv_obj_t *limit_ = nullptr, *road_ = nullptr, *scale_text_ = nullptr, *scale_bar_ = nullptr;
    std::unique_ptr<zmap::Map> map_;
    std::string problem_;
    std::vector<uint16_t> pixels_;
    zmap::Renderer renderer_;
    std::vector<zmap::Label> labels_;
    std::string scale_shown_;

    // The view
    double cx_ = 0, cy_ = 0, up_ = 0;
    int zoom_ = kDriveZoom, drive_zoom_ = kDriveZoom;
    bool follow_ = true, overview_ = false, north_up_ = false, night_ = true, imperial_ = false;
    // What was last drawn
    double drawn_cx_ = 0, drawn_cy_ = 0, drawn_up_ = 0, drawn_px_ = 0, drawn_py_ = 0;
    bool drawn_pos_ = false, dirty_ = true;
    uint32_t drawn_at_ = 0, draw_ms_ = 0;
    // The position
    GpsFix fix_;
    std::string fix_time_;
    double fix_x_ = 0, fix_y_ = 0, pos_x_ = 0, pos_y_ = 0, heading_ = 0;
    uint32_t fix_at_ = 0;
    bool have_pos_ = false, have_heading_ = false, moving_ = false, outside_ = false;
    std::time_t opened_ = 0;
    int n_ = 0;
};
}  // namespace

std::unique_ptr<App> make_maps_app() { return std::make_unique<MapsApp>(); }

}  // namespace zs
