// Maps: an offline moving map (README.md). The map is a .zmap file
// (zmap.h) made from OpenStreetMap data; the position comes from the GPS
// module through gpsd, as in the GPS app. A Control screen (docs/UX.md in
// the OS); the soft-key bar shows its keys:
//   Enter   follow my position again (after panning)
//   G       go to: find a place or street and get directions there
//   X       stop the directions            V  voice on / off
//   - =     zoom out, zoom in
//   arrows  pan (W A S D too)
//   N       north up / heading up
//   T       day / night colours        U  miles / kilometres
//   I       about this map
// Directions come from Navit, run by nav.cpp; the search from the map's
// .search file (search.h). Opened with a word "lat,lon"
// (shell_open_app("maps", "40.7357,-74.1724")) it shows that place.
#include <dirent.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/maps/nav.h"
#include "apps/maps/render.h"
#include "apps/maps/search.h"
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
constexpr int kBannerH = 36;   // the next turn, along the top while there are directions
constexpr size_t kMaxResults = 30;
namespace nav = zmap::nav;

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
        eta_ = chip(root, &zs_font_14);
        lv_obj_add_flag(eta_, LV_OBJ_FLAG_HIDDEN);
        // The next turn: drawn into the map (nav_banner()), with these over it.
        banner_dist_ = label(root, "", &zs_font_20, 0xffffff);
        lv_obj_set_pos(banner_dist_, 40, -1);
        banner_text_ = label(root, "", &zs_font_14, 0xffffff);
        lv_obj_set_width(banner_text_, kW - 46);
        lv_label_set_long_mode(banner_text_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(banner_text_, 40, 19);
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
        voice_ = prefs::get_int("maps.voice", 1) != 0;
        std::string base = map_->path().substr(0, map_->path().size() - 5);
        search_path_ = base + ".search";
        nav::Config nc;
        nc.navit_map = base + ".navit";
        nc.imperial = imperial_;
        nc.simulated = hal().system().simulated && !std::getenv("ZS_MAPS_NAVIT");
        engine_ = nav::make_engine(nc);
        engine_->set_voice(voice_);
        // Directions that were going when Maps was last closed carry on.
        double dl = 0, dn = 0;
        std::string saved = prefs::get_str("maps.dest", "");
        if (std::sscanf(saved.c_str(), "%lf,%lf", &dl, &dn) == 2 && map_->contains(zmap::lon_to_x(dn), zmap::lat_to_y(dl))) {
            size_t name_at = saved.find(',', saved.find(',') + 1);
            start_route({dl, dn}, name_at == std::string::npos ? "" : saved.substr(name_at + 1), false);
        }
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
        if (navigating_) update_nav();
        if (++n_ % 10 == 0) update_overlays();
        if (power().screen_off() || searching_) return;   // nobody is looking at the map
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
        if (searching_) return search_key(key);
        switch (key) {
            case LV_KEY_ENTER: centre(); return true;
            case 'g': case 'G': open_search(); return true;
            case 'x': case 'X':
                if (navigating_) ask_stop();
                return true;
            case 'v': case 'V': toggle_voice(); return true;
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

    bool on_back() override {
        if (!searching_) return false;
        if (!query_.empty()) {   // a box with text: Esc clears it first
            query_.clear();
            refresh_search();
        } else {
            close_search();
        }
        return true;
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
        int top = navigating_ ? kBannerH : 0;
        std::vector<zmap::Rect> keep_out = {{kW - 26, top, kW, top + 26}, {kW - 84, kH - 20, kW, kH}};
        if (navigating_) keep_out.push_back({0, 0, kW, kBannerH + 2});
        if (*lv_label_get_text(status_)) keep_out.push_back({0, top, 170, top + 22});
        if (have_pos_) keep_out.push_back({0, kH - 40, 96, kH});
        if (*lv_label_get_text(road_) || navigating_) keep_out.push_back({96, kH - 24, 252, kH});
        renderer_.draw(*map_, v, t, pixels_.data(), labels_, keep_out);
        if (route_) renderer_.route(v, t, pixels_.data(), route_xy_, route_seg_);
        if (have_pos_) {
            double sx, sy;
            zmap::Renderer::to_screen(v, pos_x_, pos_y_, sx, sy);
            if (sx > -12 && sy > -12 && sx < kW + 12 && sy < kH + 12)
                renderer_.marker(v, t, pixels_.data(), sx, sy, heading_ - up_, have_heading_);
        }
        renderer_.compass(v, t, pixels_.data(), kW - 13, top + 13);
        draw_labels(t);
        if (navigating_) nav_banner();
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
        std::string name = !on_map || !road.found || navigating_ ? "" : *road.name ? road.name : road.ref;
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
        lv_obj_set_y(status_, navigating_ ? kBannerH + 4 : 4);
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
        if (searching_) {
            shell_set_actions({{"Go", 0, "Enter", nullptr}, {"Back", 0, "Esc", nullptr}});
            return;
        }
        std::vector<Action> a = {{"Centre", 'c', "Enter", [this] { centre(); }},
                                 {"Go to", 'g', "G", [this] { open_search(); }}};
        if (navigating_) a.push_back({"Stop", 'x', "X", [this] { ask_stop(); }});
        else a.push_back({"Zoom", 0, "- =", nullptr});
        a.push_back({north_up_ ? "Heading up" : "North up", 'n', navigating_ ? nullptr : "N", [this] { toggle_north(); }});
        a.push_back({voice_ ? "Voice off" : "Voice on", 'v', nullptr, [this] { toggle_voice(); }});
        a.push_back({night_ ? "Day colours" : "Night colours", 't', nullptr, [this] { toggle_night(); }});
        a.push_back({imperial_ ? "Kilometres" : "Miles", 'u', nullptr, [this] { toggle_units(); }});
        a.push_back({"About this map", 'i', nullptr, [this] { about(); }});
        shell_set_actions(a);
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
    // ---- Go to ---------------------------------------------------------

    void open_search() {
        if (!search_loaded_) {
            search_loaded_ = true;
            search_.load(search_path_);
        }
        searching_ = true;
        query_.clear();
        search_panel_ = lv_obj_create(root_);
        lv_obj_remove_style_all(search_panel_);
        lv_obj_set_size(search_panel_, kW, kH);
        lv_obj_set_style_bg_color(search_panel_, c(kBg), 0);
        lv_obj_set_style_bg_opa(search_panel_, LV_OPA_COVER, 0);
        lv_obj_t* title = label(search_panel_, LV_SYMBOL_GPS " Go to", &zs_font_14, kAccent);
        lv_obj_set_pos(title, 6, 3);
        search_query_ = label(search_panel_, "", &zs_font_14, kText);
        lv_obj_set_pos(search_query_, 72, 3);
        lv_obj_set_width(search_query_, kW - 78);
        lv_label_set_long_mode(search_query_, LV_LABEL_LONG_CLIP);
        search_list_ = make_list(search_panel_, 0, 24, kW, kH - 24);
        search_note_ = label(search_panel_, "", &zs_font_12, kMuted);
        lv_obj_set_width(search_note_, kW - 24);
        lv_obj_set_style_text_align(search_note_, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(search_note_, LV_ALIGN_CENTER, 0, 12);
        set_actions();
        refresh_search();
    }

    void close_search() {
        searching_ = false;
        if (search_panel_) lv_obj_delete(search_panel_);
        search_panel_ = search_query_ = search_list_ = search_note_ = nullptr;
        results_.clear();
        set_actions();
        dirty_ = true;
    }

    // A List screen: letters type into the box, the arrows and Enter go to the list.
    bool search_key(uint32_t key) {
        if (key == LV_KEY_BACKSPACE) {
            if (query_.empty()) {
                close_search();
                return true;
            }
            do query_.pop_back();
            while (!query_.empty() && (uint8_t(query_.back()) & 0xC0) == 0x80);
            refresh_search();
            return true;
        }
        if (key >= 32 && key < 127 && query_.size() < 40) {
            query_ += char(key);
            refresh_search();
            return true;
        }
        return false;   // arrows, Enter, Tab and the shell's keys
    }

    void refresh_search() {
        lv_label_set_text(search_query_, (query_ + "_").c_str());
        lv_obj_clean(search_list_);
        std::string note;
        double clat = 0, clon = 0;
        here(clat, clon);
        if (query_.empty()) {
            results_ = recent_places();
            if (results_.empty())
                note = search_.ok() ? "Type a town or a street.\nAdd the town to narrow it down: \"main st princeton\""
                                    : "No search index for this map.";
        } else if (!search_.ok()) {
            results_.clear();
            note = "No search index for this map.";
        } else {
            results_ = search_.find(query_, kMaxResults, true, clat, clon);
            if (results_.empty()) note = "Nothing called \"" + query_ + "\" on this map.";
        }
        for (size_t i = 0; i < results_.size(); i++) {
            zmap::Place& p = results_[i];
            if (p.metres < 0) p.metres = nav::metres({clat, clon}, {p.lat, p.lon});
            std::string text = p.name + (p.town.empty() ? "" : ", " + p.town) + "   " + short_distance(p.metres);
            lv_obj_t* b = list_button(search_list_, p.kind == 'r' ? LV_SYMBOL_RIGHT : ZS_ICON_PIN, text.c_str(),
                                      [](lv_event_t* e) {
                                          auto* app = static_cast<MapsApp*>(lv_event_get_user_data(e));
                                          auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
                                          app->pick(size_t(uintptr_t(lv_obj_get_user_data(btn))));
                                      },
                                      this);
            lv_obj_set_user_data(b, reinterpret_cast<void*>(uintptr_t(i)));
            lv_obj_t* l = lv_obj_get_child(b, -1);
            if (l) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        }
        finish_list(search_list_);
        if (!results_.empty() && group) lv_group_focus_obj(lv_obj_get_child(search_list_, 0));
        lv_label_set_text(search_note_, note.c_str());
        set_hidden(search_note_, note.empty());
    }

    // Where "near" means: you, or else the middle of the map on screen.
    void here(double& lat, double& lon) const {
        lat = zmap::y_to_lat(have_pos_ && !outside_ ? pos_y_ : cy_);
        lon = zmap::x_to_lon(have_pos_ && !outside_ ? pos_x_ : cx_);
    }

    void pick(size_t i) {
        if (i >= results_.size()) return;
        zmap::Place p = results_[i];
        close_search();
        std::string name = p.name + (p.town.empty() || p.kind != 'r' ? "" : ", " + p.town);
        remember_place(p);
        start_route({p.lat, p.lon}, name, true);
    }

    // The last places gone to, newest first: "kind|name|town|lat|lon;..."
    std::vector<zmap::Place> recent_places() const {
        std::vector<zmap::Place> out;
        std::string all = prefs::get_str("maps.recent", "");
        size_t at = 0;
        while (at < all.size()) {
            size_t end = all.find(';', at);
            if (end == std::string::npos) end = all.size();
            std::string e = all.substr(at, end - at);
            at = end + 1;
            size_t a = e.find('|'), b = e.find('|', a + 1), cpos = e.find('|', b + 1);
            if (a != 1 || b == std::string::npos || cpos == std::string::npos) continue;
            zmap::Place p;
            p.kind = e[0];
            p.name = e.substr(2, b - 2);
            p.town = e.substr(b + 1, cpos - b - 1);
            if (std::sscanf(e.c_str() + cpos + 1, "%lf|%lf", &p.lat, &p.lon) == 2) out.push_back(p);
        }
        return out;
    }
    void remember_place(const zmap::Place& p) {
        auto clean = [](std::string s) {
            for (char& ch : s)
                if (ch == '|' || ch == ';' || ch == '\n' || ch == '=') ch = ' ';
            return s;
        };
        std::string entry = fmt("%c|%s|%s|%.6f|%.6f", p.kind, clean(p.name).c_str(), clean(p.town).c_str(), p.lat, p.lon);
        std::string all = entry;
        int kept = 1;
        for (const zmap::Place& o : recent_places()) {
            if (o.name == p.name && o.town == p.town) continue;
            if (++kept > 8) break;
            all += fmt(";%c|%s|%s|%.6f|%.6f", o.kind, clean(o.name).c_str(), clean(o.town).c_str(), o.lat, o.lon);
        }
        prefs::set_str("maps.recent", all);
    }

    // ---- Directions ----------------------------------------------------

    void start_route(nav::LatLon dest, const std::string& name, bool say) {
        double lat, lon;
        here(lat, lon);
        nav::Fix f;
        f.ok = have_pos_ && !outside_;
        f.lat = zmap::y_to_lat(pos_y_), f.lon = zmap::x_to_lon(pos_x_);
        f.speed_mps = fix_.speed_mps, f.track_deg = fix_.track_deg;
        engine_->position(f);
        engine_->go(dest, name, {lat, lon});
        navigating_ = true;
        dest_name_ = name;
        route_.reset();
        route_xy_.clear();
        nav_gen_ = ~0u;
        fitted_ = false;
        next_turn_ = -1;
        announced_turn_ = -1;
        prefs::set_str("maps.dest", fmt("%.6f,%.6f,", dest.lat, dest.lon) + name);
        if (say) shell_toast("Directions to " + name);
        set_actions();
        update_overlays();
        dirty_ = true;
    }

    void ask_stop() {
        shell_confirm("Stop the directions?", dest_name_.empty() ? "" : "To " + dest_name_, "Stop", [this] { stop_route(); });
    }

    void stop_route() {
        engine_->stop();
        navigating_ = false;
        route_.reset();
        route_xy_.clear();
        prefs::set_str("maps.dest", "");
        set_hidden(eta_, true);
        lv_label_set_text(banner_dist_, "");
        lv_label_set_text(banner_text_, "");
        set_actions();
        update_overlays();
        dirty_ = true;
    }

    void update_nav() {
        if (have_pos_ && !outside_) {
            nav::Fix f;
            f.ok = true;
            f.lat = zmap::y_to_lat(fix_y_), f.lon = zmap::x_to_lon(fix_x_);
            f.speed_mps = fix_.speed_mps, f.track_deg = fix_.track_deg;
            engine_->position(f);
        }
        nav_ = engine_->take();
        for (const std::string& said : nav_.said)
            if (said.find("reached your destination") != std::string::npos) {
                arrived();
                return;
            }
        if (nav_.state == nav::Snapshot::kFailed || nav_.state == nav::Snapshot::kNoRoute) {
            shell_toast(nav_.state == nav::Snapshot::kFailed ? nav_.error : "No route found to " + dest_name_);
            stop_route();
            return;
        }
        if (nav_.gen != nav_gen_ && nav_.route) {
            nav_gen_ = nav_.gen;
            route_ = nav_.route;
            route_xy_.clear();
            for (const nav::LatLon& p : route_->line) {
                route_xy_.push_back(zmap::lon_to_x(p.lon));
                route_xy_.push_back(zmap::lat_to_y(p.lat));
            }
            route_seg_ = 0;
            // With no position yet, show the whole route once.
            if (!fitted_ && !(have_pos_ && !outside_)) fit_route();
            if (!fitted_ && have_pos_ && !outside_) engine_->say("Starting the route to " + dest_name_);
            fitted_ = true;
            dirty_ = true;
        }
        if (!route_) return;
        // Where we are on it, and the next turn after that.
        nav::LatLon at{zmap::y_to_lat(pos_y_), zmap::x_to_lon(pos_x_)};
        double along = 0;
        if (have_pos_ && !outside_) {
            nav::Progress pr = nav::locate(route_->line, route_->cum, at, route_seg_);
            if (pr.seg != route_seg_) dirty_ = true;
            route_seg_ = pr.seg;
            along = pr.along_m;
        }
        int next = -1;
        for (size_t i = 0; i < route_->turns.size(); i++)
            if (route_->turn_along[i] > along + 3) {
                next = int(i);
                break;
            }
        double left = next >= 0 ? route_->turn_along[size_t(next)] - along : 0;
        if (next >= 0 && route_->turns[size_t(next)].kind == nav::kArrive && left < 25 && have_pos_) {
            arrived();
            return;
        }
        if (next != next_turn_ || std::fabs(left - next_turn_m_) > 5) dirty_ = true;
        next_turn_ = next;
        next_turn_m_ = left;
        if (have_pos_ && !outside_ && next >= 0) announce(route_->turns[size_t(next)], next, left);
    }

    // Spoken: once about half a minute before a turn ("In 500 feet, turn
    // right into Bridge Street"), and again as you reach it.
    void announce(const nav::Maneuver& m, int index, double left) {
        if (m.kind == nav::kArrive) return;   // arrived() says it
        if (index != announced_turn_) {
            announced_turn_ = index;
            announced_stage_ = 0;
        }
        double speed = std::max(fix_.speed_mps, 8.0);
        double early = std::clamp(speed * 30, 300.0, 3000.0), close = std::clamp(speed * 5, 40.0, 150.0);
        std::string text = m.text;
        if (announced_stage_ < 2 && left <= close) {
            engine_->say(text);
            announced_stage_ = 2;
        } else if (announced_stage_ < 1 && left <= early && left > close * 1.5) {
            if (!text.empty()) text[0] = char(std::tolower(uint8_t(text[0])));
            engine_->say("In " + spoken_distance(left) + ", " + text);
            announced_stage_ = 1;
        }
    }

    std::string spoken_distance(double m) const {
        std::string d = short_distance(m);
        size_t sp = d.find(' ');
        if (sp == std::string::npos) return d;
        std::string n = d.substr(0, sp), u = d.substr(sp + 1);
        if (u == "ft") u = "feet";
        else if (u == "mi") u = n == "1.0" || n == "1" ? "mile" : "miles";
        else if (u == "km") u = "kilometres";
        else if (u == "m") u = "metres";
        if (n.size() > 2 && n.compare(n.size() - 2, 2, ".0") == 0) n.erase(n.size() - 2);
        return n + " " + u;
    }

    void arrived() {
        std::string name = dest_name_;
        engine_->say("You have arrived");
        stop_route();
        shell_toast(name.empty() ? "You have arrived" : "You have arrived at " + name);
    }

    void fit_route() {
        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
        for (size_t i = 0; i + 1 < route_xy_.size(); i += 2) {
            x0 = std::min(x0, route_xy_[i]), x1 = std::max(x1, route_xy_[i]);
            y0 = std::min(y0, route_xy_[i + 1]), y1 = std::max(y1, route_xy_[i + 1]);
        }
        if (x0 > x1) return;
        cx_ = (x0 + x1) / 2, cy_ = (y0 + y1) / 2;
        double upm = zmap::units_per_metre(zmap::y_to_lat(cy_));
        double need = std::max((x1 - x0) / upm / (kW - 30), (y1 - y0) / upm / (kH - kBannerH - 30));
        zoom_ = kZooms - 1;
        for (int i = 0; i < kZooms; i++)
            if (kZoom[i] >= need) {
                zoom_ = i;
                break;
            }
        follow_ = false;
        overview_ = false;
        up_ = 0;
    }

    // The banner along the top: the next turn's arrow, how far, and what.
    void nav_banner() {
        const zmap::Theme& t = night_ ? zmap::night_theme() : zmap::day_theme();
        renderer_.box(pixels_.data(), kW, kH, 0, 0, kW, kBannerH, zmap::rgb565(0x1c3d6e));
        std::string dist, text;
        if (!route_) {
            text = nav_.state == nav::Snapshot::kStarting ? "Starting the route planner..." : "Planning the route...";
        } else if (next_turn_ >= 0) {
            const nav::Maneuver& m = route_->turns[size_t(next_turn_)];
            renderer_.turn_icon(pixels_.data(), kW, kH, m.kind, 19, kBannerH / 2.0f, 26, zmap::rgb565(0xffffff));
            dist = short_distance(next_turn_m_);
            text = m.text;
        } else {
            text = "Follow the route";
        }
        (void)t;
        lv_obj_set_x(banner_dist_, route_ ? 40 : 8);
        if (dist != lv_label_get_text(banner_dist_)) lv_label_set_text(banner_dist_, dist.c_str());
        lv_obj_set_y(banner_text_, dist.empty() ? 9 : 19);
        lv_obj_set_x(banner_text_, route_ ? 40 : 8);
        if (text != lv_label_get_text(banner_text_)) lv_label_set_text(banner_text_, text.c_str());
        // Arrival time, time and distance left.
        std::string eta;
        if (route_ && nav_.left_s >= 0 && nav_.left_m >= 0) {
            std::time_t when = std::time(nullptr) + std::time_t(nav_.left_s);
            char clock[16];
            std::strftime(clock, sizeof clock, imperial_ ? "%l:%M" : "%H:%M", std::localtime(&when));
            std::string c0 = clock;
            while (!c0.empty() && c0[0] == ' ') c0.erase(0, 1);
            long mins = std::lround(nav_.left_s / 60);
            std::string dur = mins < 60 ? fmt("%ld min", std::max(mins, 1L)) : fmt("%ld h %02ld", mins / 60, mins % 60);
            eta = c0 + "  " + dur + "  " + short_distance(nav_.left_m);
        }
        if (eta != lv_label_get_text(eta_)) {
            lv_label_set_text(eta_, eta.c_str());
            lv_obj_align(eta_, LV_ALIGN_BOTTOM_MID, 14, -3);
        }
        set_hidden(eta_, eta.empty());
    }

    // "450 ft", "0.3 mi", "12 mi"; "350 m", "2.4 km".
    std::string short_distance(double m) const {
        if (m < 0) return "";
        if (imperial_) {
            double ft = m * 3.28084;
            if (ft < 1000) return fmt("%.0f ft", std::max(50.0, std::round(ft / 50) * 50));
            double mi = m / 1609.344;
            return mi < 10 ? fmt("%.1f mi", mi) : fmt("%.0f mi", mi);
        }
        if (m < 1000) return fmt("%.0f m", std::max(10.0, std::round(m / 10) * 10));
        return m < 10000 ? fmt("%.1f km", m / 1000) : fmt("%.0f km", m / 1000);
    }

    void toggle_voice() {
        voice_ = !voice_;
        prefs::set_int("maps.voice", voice_);
        engine_->set_voice(voice_);
        shell_toast(voice_ ? "Voice on" : "Voice off");
        set_actions();
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

    // Go to
    zmap::SearchIndex search_;
    std::string search_path_;
    bool search_loaded_ = false, searching_ = false;
    std::string query_;
    std::vector<zmap::Place> results_;
    lv_obj_t *search_panel_ = nullptr, *search_query_ = nullptr, *search_list_ = nullptr, *search_note_ = nullptr;
    // Directions
    std::unique_ptr<nav::Engine> engine_;
    nav::Snapshot nav_;
    unsigned nav_gen_ = ~0u;
    std::shared_ptr<const nav::Route> route_;
    std::vector<double> route_xy_;
    size_t route_seg_ = 0;
    int next_turn_ = -1, announced_turn_ = -1, announced_stage_ = 0;
    double next_turn_m_ = 0;
    bool navigating_ = false, voice_ = true, fitted_ = false;
    std::string dest_name_;
    lv_obj_t *banner_dist_ = nullptr, *banner_text_ = nullptr, *eta_ = nullptr;

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
