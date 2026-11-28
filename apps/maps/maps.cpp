// Maps: an offline moving map (README.md). The map is a .zmap file
// (zmap.h) made from OpenStreetMap data; the position comes from the GPS
// module through gpsd (which the package sets up). A Control screen (docs/UX.md in
// the OS); the soft-key bar shows its keys:
//   Enter   follow my position again (after panning)
//   G       go to: find an address, a street, a town, gas, food or parking,
//           or a saved place; then a preview of the route (Enter starts,
//           F fastest / shortest, Esc cancels)
//   H       directions home (once Home is saved: Go to, Tab > H)
//   X       stop the directions            V  voice on / off
//   Z       automatic zoom on / off
//   - =     zoom out, zoom in
//   arrows  pan (W A S D too)
//   N       north up / heading up
//   T       colours: automatic (by the sun), day, night
//   U       miles / kilometres
//   I       about this map
//   P       the GPS screen: position, satellites, saving the spot you're
//           at, recording a track (what was the GPS app, which Maps replaces)
//   R       record a track / stop       S (menu)  save this spot
// Directions come from Navit, run by nav.cpp; the search from the map's
// .search file (search.h). Opened with a word "lat,lon"
// (shell_open_app("maps", "40.7357,-74.1724")) it shows that place; with
// "gps" it opens on the GPS screen (Mesh's GPS button).
#include <dirent.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/maps/nav.h"
#include "apps/maps/render.h"
#include "apps/maps/search.h"
#include "apps/maps/sun.h"
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
constexpr int kBannerH = 40;   // the next turn, along the top while there are directions
constexpr int kStripH = 22;    // speed, limit and arrival, along the bottom while there are directions
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
        if (!map_ && shell_app_arg() == "gps") {   // the GPS screen still works with no map
            open_gps();
            return;
        }
        if (!map_) {
            shell_set_actions({{"Position and tracks", 'p', "P", [this] { open_gps(); }}});
            lv_obj_t* t = label(root, "No map installed", &zs_font_16, kText);
            lv_obj_align(t, LV_ALIGN_CENTER, 0, -22);
            lv_obj_t* d = label(root, problem_.empty() ? "Reinstall Maps from Apps, or put a\n.zmap file in the Maps folder."
                                                       : problem_.c_str(),
                                &zs_font_12, kMuted);
            lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(d, LV_ALIGN_CENTER, 0, 10);
            return;
        }
        colours_ = prefs::get_str("maps.colours", "auto");
        auto_zoom_ = prefs::get_int("maps.autozoom", 1) != 0;
        shortest_ = prefs::get_int("maps.shortest", 0) != 0;
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
        lv_obj_set_pos(banner_dist_, 46, 0);
        banner_text_ = label(root, "", &zs_font_14, 0xffffff);
        lv_obj_set_width(banner_text_, kW - 52);
        lv_label_set_long_mode(banner_text_, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(banner_text_, 46, 22);
        banner_then_ = label(root, "", &zs_font_12, 0xb8c7dd);
        lv_obj_set_pos(banner_then_, kW - 50, 2);
        // Along the bottom with directions: speed, the limit, and the arrival.
        strip_speed_ = label(root, "", &zs_font_20, 0xffffff);
        strip_unit_ = label(root, "", &zs_font_12, 0xaab4c3);
        strip_eta_ = label(root, "", &zs_font_14, 0xffffff);
        for (lv_obj_t* o : {strip_speed_, strip_unit_, strip_eta_}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
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
        nc.shortest = shortest_;
        engine_ = nav::make_engine(nc);
        engine_->set_voice(voice_);
        // Directions that were going when Maps was last closed carry on.
        double dl = 0, dn = 0;
        std::string saved = prefs::get_str("maps.dest", "");
        if (std::sscanf(saved.c_str(), "%lf,%lf", &dl, &dn) == 2 && map_->contains(zmap::lon_to_x(dn), zmap::lat_to_y(dl))) {
            size_t name_at = saved.find(',', saved.find(',') + 1);
            start_route({dl, dn}, name_at == std::string::npos ? "" : saved.substr(name_at + 1), false, false);
        }
        update_colours();
        set_actions();
        update_overlays();
        redraw();
        if (arg == "gps") open_gps();
        else shell_toast(map_->name() + "  \xC2\xA9 OpenStreetMap contributors");
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
        if (gps_panel_ && ++gps_n_ % 5 == 0) update_gps();
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
        if (n_ % 600 == 0) update_colours();   // about once a minute
        auto_zoom(now);
        if (power().screen_off() || searching_ || gps_panel_) return;   // nobody is looking at the map
        // Redraw when the picture would change by a pixel or so, but never
        // spend more than about a third of the time drawing.
        double moved = zmap::metres_between(cx_, cy_, drawn_cx_, drawn_cy_) / kZoom[zoom_];
        double turned = std::fabs(std::fmod(up_ - drawn_up_ + 540.0, 360.0) - 180.0);
        double marker = have_pos_ ? zmap::metres_between(pos_x_, pos_y_, drawn_px_, drawn_py_) / kZoom[zoom_] : 0;
        bool changed = dirty_ || moved >= 0.7 || turned >= 1.0 || marker >= 0.7 || have_pos_ != drawn_pos_;
        if (changed && now - drawn_at_ >= std::clamp<uint32_t>(draw_ms_ * 3, 90, 1500)) redraw();
    }

    bool on_key(uint32_t key) override {
        if (gps_panel_) return gps_key(key);
        if (!map_) {
            if (key != 'p' && key != 'P') return false;
            open_gps();
            return true;
        }
        if (searching_) return search_key(key);
        if (previewing_) {   // the route preview: Enter starts, F changes the route, Esc (on_back) cancels
            if (key == LV_KEY_ENTER) {
                begin_route();
                return true;
            }
            if (key == 'f' || key == 'F') {
                toggle_shortest();
                return true;
            }
            if (key == 'x' || key == 'X') {
                stop_route();
                return true;
            }
        }
        switch (key) {
            case LV_KEY_ENTER: centre(); return true;
            case 'h': case 'H': go_saved('h'); return true;
            case 'z': case 'Z': toggle_auto_zoom(); return true;
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
            case 'p': case 'P': open_gps(); return true;
            case 'r': case 'R': toggle_track(); return true;
            default: return false;
        }
    }

    bool on_back() override {
        if (previewing_ && !searching_) {
            stop_route();
            return true;
        }
        if (gps_panel_) {
            if (!map_) return false;   // nothing to go back to: home
            close_gps();
            return true;
        }
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
        if (follow_ && have_pos_ && !outside_ && !north_up_)
            v.py = navigating_ && !previewing_ ? kBannerH + (kH - kBannerH - kStripH) * 72 / 100 : kH * 2 / 3;
        else
            v.py = navigating_ ? kBannerH + (kH - kBannerH - (previewing_ ? 0 : kStripH)) / 2 : kH / 2;
        return v;
    }

    void redraw() {
        auto t0 = std::chrono::steady_clock::now();
        zmap::View v = view();
        const zmap::Theme& t = night_ ? zmap::night_theme() : zmap::day_theme();
        int top = navigating_ ? kBannerH : 0;
        bool strip = navigating_ && !previewing_;
        std::vector<zmap::Rect> keep_out = {{kW - 26, top, kW, top + 26}, {kW - 84, kH - 20, kW, kH}};
        if (navigating_) keep_out.push_back({0, 0, kW, kBannerH + 2});
        if (strip) keep_out.push_back({0, kH - kStripH - 2, kW, kH});
        if (*lv_label_get_text(status_)) keep_out.push_back({0, top, 170, top + 22});
        if (have_pos_) keep_out.push_back({0, kH - 40, 96, kH});
        if (*lv_label_get_text(road_)) keep_out.push_back({96, kH - 24, 252, kH});
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
        if (strip) renderer_.box(pixels_.data(), kW, kH, 0, kH - kStripH, kW, kH, zmap::rgb565(0x161a20));
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
        const TrackState& track = track_state();
        if (status.empty() && track.recording) status = LV_SYMBOL_STOP " REC  " + short_distance(track.meters);
        if (track.recording != rec_shown_ && !gps_panel_ && !searching_) set_actions();
        set_text(status_, status);
        lv_obj_set_style_text_color(status_, c(have_pos_ && track.recording && !outside_ ? kBad : kText), 0);
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
        // With directions the speed, limit and arrival go in one strip along the bottom.
        bool strip = navigating_ && !previewing_;
        set_hidden(speed_box_, !on_map || strip);
        set_hidden(limit_, !on_map || !*lv_label_get_text(limit_));
        if (strip) {
            set_text(strip_speed_, on_map ? lv_label_get_text(speed_) : "--");
            set_text(strip_unit_, imperial_ ? "mph" : "km/h");
            lv_obj_set_style_text_color(strip_speed_, lv_obj_get_style_text_color(speed_, 0), 0);
            lv_obj_set_pos(strip_speed_, 6, kH - kStripH - 1);
            lv_obj_align_to(strip_unit_, strip_speed_, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -3);
            lv_obj_set_style_text_font(limit_, &zs_font_14, 0);
            lv_obj_set_style_pad_ver(limit_, 0, 0);
            lv_obj_set_style_pad_hor(limit_, 3, 0);
            lv_obj_align_to(limit_, strip_unit_, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, 1);
        } else {
            lv_obj_set_style_text_font(limit_, &zs_font_16, 0);
            lv_obj_set_style_pad_ver(limit_, 3, 0);
            lv_obj_set_style_pad_hor(limit_, 4, 0);
            lv_obj_set_pos(limit_, 60, kH - 30);
        }
        for (lv_obj_t* o : {strip_speed_, strip_unit_, strip_eta_}) set_hidden(o, !strip);
        set_hidden(scale_text_, strip);
        set_hidden(scale_bar_, strip);
        std::string name = !on_map || !road.found || navigating_ ? "" : *road.name ? road.name : road.ref;
        if (strip) lv_obj_move_foreground(limit_);
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
            shell_set_actions({{"Go", 0, "Enter", nullptr},
                               {"Back", 0, "Esc", nullptr},
                               {"Save as Home", 'h', nullptr, [this] { save_focused('h'); }},
                               {"Save as Work", 'w', nullptr, [this] { save_focused('w'); }},
                               {"Save this place", 's', nullptr, [this] { save_focused('s'); }},
                               {"Remove from saved", 'd', nullptr, [this] { unsave_focused(); }, true, true,
                                "Remove it from your saved places?"}});
            return;
        }
        if (previewing_) {
            shell_set_actions({{"Start", 0, "Enter", [this] { begin_route(); }},
                               {shortest_ ? "Fastest" : "Shortest", 'f', "F", [this] { toggle_shortest(); }},
                               {"Cancel", 'x', "Esc", [this] { stop_route(); }}});
            return;
        }
        bool rec = track_state().recording;
        rec_shown_ = rec;
        if (gps_panel_) {
            shell_set_actions({{"Save spot", 's', "Enter", [this] { mark_spot(); }},
                               {rec ? "Stop" : "Record", 'r', "R", [this] { toggle_track(); }},
                               {map_ ? "Map" : "Home", 0, "Esc", nullptr}});
            return;
        }
        std::vector<Action> a = {{"Centre", 'c', "Enter", [this] { centre(); }},
                                 {"Go to", 'g', "G", [this] { open_search(); }}};
        if (navigating_) a.push_back({"Stop", 'x', "X", [this] { ask_stop(); }});
        else a.push_back({"Zoom", 0, "- =", nullptr});
        a.push_back({north_up_ ? "Heading up" : "North up", 'n', navigating_ ? nullptr : "N", [this] { toggle_north(); }});
        a.push_back({"Directions home", 'h', nullptr, [this] { go_saved('h'); }, !saved_place('h').name.empty()});
        a.push_back({"Directions to work", 'k', nullptr, [this] { go_saved('w'); }, !saved_place('w').name.empty()});
        a.push_back({shortest_ ? "Fastest routes" : "Shortest routes", 'f', nullptr, [this] { toggle_shortest(); }});
        a.push_back({auto_zoom_ ? "Auto zoom off" : "Auto zoom on", 'z', nullptr, [this] { toggle_auto_zoom(); }});
        a.push_back({"Position and tracks", 'p', nullptr, [this] { open_gps(); }});
        a.push_back({rec ? "Stop track" : "Record track", 'r', nullptr, [this] { toggle_track(); }});
        a.push_back({"Save this spot", 's', nullptr, [this] { mark_spot(); }});
        a.push_back({voice_ ? "Voice off" : "Voice on", 'v', nullptr, [this] { toggle_voice(); }});
        a.push_back({colours_ == "auto" ? "Colours: automatic" : colours_ == "day" ? "Colours: day" : "Colours: night", 't', nullptr,
                     [this] { toggle_night(); }});
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
        manual_zoom_at_ = lv_tick_get();
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
    // Colours: automatic (day colours while the sun is up where the map is),
    // or always day, or always night. T goes round the three.
    void toggle_night() {
        colours_ = colours_ == "auto" ? "day" : colours_ == "day" ? "night" : "auto";
        prefs::set_str("maps.colours", colours_);
        update_colours();
        shell_toast(colours_ == "auto" ? "Colours change at sunrise and sunset" : colours_ == "day" ? "Day colours" : "Night colours");
        set_actions();
        redraw();
    }
    void update_colours() {
        bool night = colours_ == "night" ||
                     (colours_ == "auto" && !zmap::sun_up(zmap::y_to_lat(cy_), zmap::x_to_lon(cx_), std::time(nullptr)));
        if (night != night_) {
            night_ = night;
            dirty_ = true;
        }
    }

    // Automatic zoom while following: closer at low speed and before a
    // turn, further out on the highway. A zoom you choose holds for 20 s.
    void auto_zoom(uint32_t now) {
        if (!auto_zoom_ || !follow_ || !have_pos_ || outside_ || previewing_ || searching_) return;
        if (manual_zoom_at_ && now - manual_zoom_at_ < 20000) return;
        double v = fix_.speed_mps;
        if (v < 1.0 && !navigating_) return;   // parked: as it is
        double want = v < 5 ? 1.2 : v < 11 ? 2 : v < 18 ? 3 : v < 25 ? 5 : 8;
        if (navigating_ && route_ && next_turn_ >= 0 && next_turn_m_ < std::max(200.0, v * 12)) want = std::min(want, v < 11 ? 1.2 : 2.0);
        int z = 0;
        while (z + 1 < kZooms && kZoom[z + 1] <= want + 1e-6) z++;
        if (z == zoom_) {
            zoom_wanted_ = -1;
            return;
        }
        // The same wish for two seconds, so it doesn't pump at a junction.
        if (z != zoom_wanted_) {
            zoom_wanted_ = z;
            zoom_wanted_at_ = now;
            return;
        }
        if (now - zoom_wanted_at_ < 2000) return;
        zoom_ = z;
        zoom_wanted_ = -1;
        dirty_ = true;
    }
    void toggle_auto_zoom() {
        auto_zoom_ = !auto_zoom_;
        prefs::set_int("maps.autozoom", auto_zoom_);
        shell_toast(auto_zoom_ ? "Automatic zoom on" : "Automatic zoom off");
        set_actions();
    }
    void toggle_shortest() {
        shortest_ = !shortest_;
        prefs::set_int("maps.shortest", shortest_);
        engine_->set_shortest(shortest_);
        if (navigating_) {   // plan again: show the new route when it comes
            route_.reset();
            route_xy_.clear();
            fitted_ = false;
            dirty_ = true;
        }
        shell_toast(shortest_ ? "Shortest route" : "Fastest route");
        set_actions();
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
            search_.load_addresses(search_path_.substr(0, search_path_.size() - 7) + ".addr");
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
        results_.clear();
        roles_.clear();
        std::vector<std::string> headers;   // a section's title before row i ("" for none)
        if (query_.empty()) {
            // Home, Work and saved places, then the last places gone to.
            for (char role : {'h', 'w'}) {
                zmap::Place p = saved_place(role);
                if (p.name.empty()) continue;
                results_.push_back(p);
                roles_.push_back(role);
            }
            for (const zmap::Place& p : places_list("maps.saved")) {
                results_.push_back(p);
                roles_.push_back('s');
            }
            size_t saved = results_.size();
            for (const zmap::Place& p : places_list("maps.recent")) {
                results_.push_back(p);
                roles_.push_back('r');
            }
            headers.assign(results_.size(), "");
            if (saved) headers[0] = "Saved";
            if (results_.size() > saved) headers[saved] = "Recent";
            if (results_.empty())
                note = search_.ok() ? (search_.has_addresses() ? "Type an address, a street or a town:\n\"20 nassau st\", \"main st princeton\",\nor \"gas\", \"food\", \"parking\""
                                                               : "Type a town or a street.\nAdd the town to narrow it down: \"main st princeton\"")
                                    : "No search index for this map.";
            else if (saved_place('h').name.empty())
                note = "";
        } else if (!search_.ok()) {
            note = "No search index for this map.";
        } else {
            results_ = search_.find(query_, kMaxResults, true, clat, clon);
            // Grouped: addresses, then gas, food and parking, then towns, then streets.
            auto group = [](char k) { return k == 'a' ? 0 : k == 'g' || k == 'f' || k == 'p' ? 1 : k == 'r' ? 3 : 2; };
            std::stable_sort(results_.begin(), results_.end(), [&](const zmap::Place& a, const zmap::Place& b) { return group(a.kind) < group(b.kind); });
            roles_.assign(results_.size(), 0);
            headers.assign(results_.size(), "");
            static const char* kGroup[] = {"Addresses", "Nearby", "Places", "Streets"};
            int last = -1;
            for (size_t i = 0; i < results_.size(); i++) {
                int g = group(results_[i].kind);
                if (g == 1) headers[i] = results_[i].kind == 'g' ? "Gas" : results_[i].kind == 'f' ? "Food" : "Parking";
                else if (g != last) headers[i] = kGroup[g];
                if (g == 1 && i > 0 && results_[i - 1].kind == results_[i].kind) headers[i] = "";
                last = g;
            }
            if (results_.empty()) note = "Nothing called \"" + query_ + "\" on this map.";
        }
        lv_obj_t* first = nullptr;
        for (size_t i = 0; i < results_.size(); i++) {
            zmap::Place& p = results_[i];
            if (!headers[i].empty()) {
                lv_obj_t* h = lv_list_add_text(search_list_, headers[i].c_str());
                lv_obj_set_style_text_color(h, c(kMuted), 0);
                lv_obj_set_style_bg_opa(h, LV_OPA_TRANSP, 0);
                lv_obj_set_style_pad_ver(h, 1, 0);
            }
            if (p.metres < 0) p.metres = nav::metres({clat, clon}, {p.lat, p.lon});
            char role = roles_[i];
            std::string text = role == 'h' ? "Home   " + p.name : role == 'w' ? "Work   " + p.name : p.name;
            if (!p.town.empty() && role != 'h' && role != 'w') text += ", " + p.town;
            text += "   " + short_distance(p.metres);
            const char* icon = role == 'h' ? LV_SYMBOL_HOME : role == 'w' ? LV_SYMBOL_BELL : role == 'r' ? LV_SYMBOL_LOOP
                             : p.kind == 'a' ? LV_SYMBOL_HOME : p.kind == 'r' ? LV_SYMBOL_RIGHT : p.kind == 'g' ? LV_SYMBOL_TINT
                             : p.kind == 'f' ? LV_SYMBOL_BELL : p.kind == 'p' ? ZS_ICON_CAR : ZS_ICON_PIN;
            if (role == 's') icon = LV_SYMBOL_OK;
            lv_obj_t* b = list_button(search_list_, icon, text.c_str(),
                                      [](lv_event_t* e) {
                                          auto* app = static_cast<MapsApp*>(lv_event_get_user_data(e));
                                          auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
                                          app->pick(size_t(uintptr_t(lv_obj_get_user_data(btn))));
                                      },
                                      this);
            lv_obj_set_user_data(b, reinterpret_cast<void*>(uintptr_t(i)));
            lv_obj_t* l = lv_obj_get_child(b, -1);
            if (l) lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            if (!first) first = b;
        }
        finish_list(search_list_);
        if (first && group) lv_group_focus_obj(first);
        lv_label_set_text(search_note_, note.c_str());
        set_hidden(search_note_, note.empty());
    }

    // ---- Saved places --------------------------------------------------
    // Home and Work (maps.home, maps.work: one place each) and the saved
    // places (maps.saved), as "kind|name|town|lat|lon;...".

    static std::string place_text(const zmap::Place& p) {
        auto clean = [](std::string s) {
            for (char& ch : s)
                if (ch == '|' || ch == ';' || ch == '\n' || ch == '=') ch = ' ';
            return s;
        };
        return fmt("%c|%s|%s|%.6f|%.6f", p.kind, clean(p.name).c_str(), clean(p.town).c_str(), p.lat, p.lon);
    }
    static std::vector<zmap::Place> places_list(const char* key) {
        std::vector<zmap::Place> out;
        std::string all = prefs::get_str(key, "");
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
    static void set_places_list(const char* key, const std::vector<zmap::Place>& list, size_t most) {
        std::string all;
        for (size_t i = 0; i < list.size() && i < most; i++) all += (all.empty() ? "" : ";") + place_text(list[i]);
        prefs::set_str(key, all);
    }
    static zmap::Place saved_place(char role) {
        std::vector<zmap::Place> l = places_list(role == 'h' ? "maps.home" : "maps.work");
        return l.empty() ? zmap::Place{} : l[0];
    }

    // The row in focus in Go to, or -1.
    int focused_row() const {
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        if (!f || !search_list_ || lv_obj_get_parent(f) != search_list_) return -1;
        size_t i = size_t(uintptr_t(lv_obj_get_user_data(f)));
        return i < results_.size() ? int(i) : -1;
    }
    void save_focused(char role) {
        int i = focused_row();
        if (i < 0) {
            shell_toast("Pick a place in the list first");
            return;
        }
        zmap::Place p = results_[size_t(i)];
        if (role == 'h' || role == 'w') {
            set_places_list(role == 'h' ? "maps.home" : "maps.work", {p}, 1);
            shell_toast((role == 'h' ? "Home is " : "Work is ") + p.name);
        } else {
            std::vector<zmap::Place> l = places_list("maps.saved");
            for (const zmap::Place& o : l)
                if (o.name == p.name && o.town == p.town) {
                    shell_toast(p.name + " is already saved");
                    return;
                }
            l.insert(l.begin(), p);
            set_places_list("maps.saved", l, 20);
            shell_toast("Saved " + p.name);
        }
        refresh_search();
    }
    void unsave_focused() {
        int i = focused_row();
        if (i < 0 || roles_[size_t(i)] == 0 || roles_[size_t(i)] == 'r') {
            shell_toast("Only a saved place can be removed");
            return;
        }
        char role = roles_[size_t(i)];
        zmap::Place p = results_[size_t(i)];
        if (role == 'h' || role == 'w') {
            prefs::set_str(role == 'h' ? "maps.home" : "maps.work", "");
        } else {
            std::vector<zmap::Place> l = places_list("maps.saved"), keep;
            for (const zmap::Place& o : l)
                if (!(o.name == p.name && o.town == p.town)) keep.push_back(o);
            set_places_list("maps.saved", keep, 20);
        }
        shell_toast("Removed " + p.name);
        refresh_search();
    }
    // H on the map: straight to Home (or Work), with the preview first.
    void go_saved(char role) {
        zmap::Place p = saved_place(role);
        if (p.name.empty()) {
            shell_toast(role == 'h' ? "No Home yet: in Go to, pick a place and press Tab > H" : "No Work yet: in Go to, Tab > W on a place");
            return;
        }
        start_route({p.lat, p.lon}, role == 'h' ? "Home" : "Work", true, true);
    }

    // Where "near" means: you, or else the middle of the map on screen.
    void here(double& lat, double& lon) const {
        lat = zmap::y_to_lat(have_pos_ && !outside_ ? pos_y_ : cy_);
        lon = zmap::x_to_lon(have_pos_ && !outside_ ? pos_x_ : cx_);
    }

    void pick(size_t i) {
        if (i >= results_.size()) return;
        zmap::Place p = results_[i];
        char role = i < roles_.size() ? roles_[i] : 0;
        close_search();
        std::string name = role == 'h' ? "Home" : role == 'w' ? "Work"
                         : p.name + (p.town.empty() || p.kind == 'c' || p.kind == 't' || p.kind == 'v' ? "" : ", " + p.town);
        if (role != 'h' && role != 'w') remember_place(p);
        start_route({p.lat, p.lon}, name, true, true);
    }

    // The last places gone to, newest first (maps.recent).
    void remember_place(const zmap::Place& p) {
        std::vector<zmap::Place> l{p};
        for (const zmap::Place& o : places_list("maps.recent"))
            if (!(o.name == p.name && o.town == p.town)) l.push_back(o);
        set_places_list("maps.recent", l, 8);
    }

    // ---- Directions ----------------------------------------------------

    // Directions to dest. With `preview`, the whole route is shown first,
    // with its distance and time: Enter starts, F changes it, Esc cancels.
    void start_route(nav::LatLon dest, const std::string& name, bool say, bool preview) {
        double lat, lon;
        here(lat, lon);
        nav::Fix f;
        f.ok = have_pos_ && !outside_;
        f.lat = zmap::y_to_lat(pos_y_), f.lon = zmap::x_to_lon(pos_x_);
        f.speed_mps = fix_.speed_mps, f.track_deg = fix_.track_deg;
        engine_->position(f);
        engine_->go(dest, name, {lat, lon});
        navigating_ = true;
        previewing_ = preview;
        if (preview) follow_ = false;
        dest_name_ = name;
        route_.reset();
        route_xy_.clear();
        nav_gen_ = ~0u;
        fitted_ = false;
        next_turn_ = -1;
        announced_turn_ = -1;
        if (!preview) prefs::set_str("maps.dest", fmt("%.6f,%.6f,", dest.lat, dest.lon) + name);
        dest_ = dest;
        if (say && !preview) shell_toast("Directions to " + name);
        set_actions();
        update_overlays();
        dirty_ = true;
    }

    // From the preview to driving.
    void begin_route() {
        previewing_ = false;
        prefs::set_str("maps.dest", fmt("%.6f,%.6f,", dest_.lat, dest_.lon) + dest_name_);
        if (have_pos_ && !outside_) {
            follow_ = true;
            zoom_ = drive_zoom_;
            manual_zoom_at_ = 0;
            engine_->say("Starting the route to " + dest_name_);
        } else {
            shell_toast("No GPS fix yet: the directions start when there is one");
        }
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
        previewing_ = false;
        route_.reset();
        route_xy_.clear();
        prefs::set_str("maps.dest", "");
        set_hidden(eta_, true);
        lv_label_set_text(banner_dist_, "");
        lv_label_set_text(banner_text_, "");
        lv_label_set_text(banner_then_, "");
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
            if (!previewing_ && said.find("reached your destination") != std::string::npos) {
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
            est_s_ = shortest_ ? estimate_time(*route_) : -1;
            // The preview, or no position yet: show the whole route once.
            if (!fitted_ && (previewing_ || !(have_pos_ && !outside_))) fit_route();
            if (!fitted_ && !previewing_ && have_pos_ && !outside_) engine_->say("Starting the route to " + dest_name_);
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
        if (!previewing_ && next >= 0 && route_->turns[size_t(next)].kind == nav::kArrive && left < 25 && have_pos_) {
            arrived();
            return;
        }
        if (next != next_turn_ || std::fabs(left - next_turn_m_) > 5) dirty_ = true;
        next_turn_ = next;
        next_turn_m_ = left;
        if (!previewing_ && have_pos_ && !outside_ && next >= 0) announce(route_->turns[size_t(next)], next, left);
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
        double need = std::max((x1 - x0) / upm / (kW - 30), (y1 - y0) / upm / (kH - kBannerH - 24));
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

    // For the shortest route Navit's time counts every road at the same
    // speed, so it is worked out here instead: the route's roads, sampled
    // every 500 m on the map, at their speed limit or a usual speed for
    // their kind of road.
    double estimate_time(const nav::Route& r) const {
        static const double kKmh[] = {16, 40, 48, 56, 64, 80, 97};   // service .. motorway
        double total = 0;
        for (double at = 0, end = r.cum.empty() ? 0 : r.cum.back(); at < end; at += 500) {
            size_t i = size_t(std::upper_bound(r.cum.begin(), r.cum.end(), at) - r.cum.begin());
            const nav::LatLon& p = r.line[std::min(i, r.line.size() - 1)];
            zmap::RoadHit h = zmap::nearest_road(*map_, zmap::lon_to_x(p.lon), zmap::lat_to_y(p.lat), -1, 40);
            double kmh = h.found ? kKmh[std::clamp(int(h.kind) - zmap::kService, 0, 6)] : 40;
            int limit = h.speed & 0x7f;
            if (limit) kmh = (h.speed & 0x80) ? limit * 1.609344 : limit;
            total += std::min(500.0, end - at) / (kmh / 3.6);
        }
        return total;
    }

    // "5:32  23 min  12 mi": arrival time, time and distance left.
    std::string eta_text() const {
        if (!route_ || nav_.left_s < 0 || nav_.left_m < 0) return "";
        double left_s = nav_.left_s;
        if (shortest_ && est_s_ > 0 && !route_->cum.empty() && route_->cum.back() > 0)
            left_s = est_s_ * std::min(1.0, nav_.left_m / route_->cum.back());
        std::time_t when = std::time(nullptr) + std::time_t(left_s);
        char clock[16];
        std::strftime(clock, sizeof clock, imperial_ ? "%l:%M" : "%H:%M", std::localtime(&when));
        std::string c0 = clock;
        while (!c0.empty() && c0[0] == ' ') c0.erase(0, 1);
        long mins = std::lround(left_s / 60);
        std::string dur = mins < 60 ? fmt("%ld min", std::max(mins, 1L)) : fmt("%ld h %02ld", mins / 60, mins % 60);
        return c0 + "  " + dur + "  " + short_distance(nav_.left_m);
    }

    // The banner along the top: the next turn's arrow, how far, what, and
    // the turn after it if it comes soon. In the preview: where to, and how
    // far and long.
    void nav_banner() {
        renderer_.box(pixels_.data(), kW, kH, 0, 0, kW, kBannerH, zmap::rgb565(0x1c3d6e));
        std::string dist, text, then;
        int x = 46;
        if (previewing_) {
            x = 8;
            dist = "To " + dest_name_;
            std::string eta = eta_text();
            text = !route_ ? (nav_.state == nav::Snapshot::kStarting ? "Starting the route planner..." : "Planning the route...")
                 : eta.empty() ? "Working out the time..." : eta + (shortest_ ? "  (shortest)" : "");
        } else if (!route_) {
            x = 8;
            text = nav_.state == nav::Snapshot::kStarting ? "Starting the route planner..." : "Planning the route...";
        } else if (next_turn_ >= 0) {
            const nav::Maneuver& m = route_->turns[size_t(next_turn_)];
            renderer_.turn_icon(pixels_.data(), kW, kH, m.kind, 22, kBannerH / 2.0f, 32, zmap::rgb565(0xffffff));
            dist = short_distance(next_turn_m_);
            text = m.text;
            // "then" and the turn after, when it comes within 400 m of this one.
            size_t n2 = size_t(next_turn_) + 1;
            if (n2 < route_->turns.size() && route_->turn_along[n2] - route_->turn_along[size_t(next_turn_)] < 400) {
                renderer_.box(pixels_.data(), kW, kH, kW - 52, 3, kW - 3, kBannerH - 3, zmap::rgb565(0x2b5594));
                renderer_.turn_icon(pixels_.data(), kW, kH, route_->turns[n2].kind, kW - 27, 25, 18, zmap::rgb565(0xffffff));
                then = "then";
            }
        } else {
            x = 8;
            text = "Follow the route";
        }
        lv_obj_set_x(banner_dist_, x);
        lv_obj_set_style_text_font(banner_dist_, previewing_ ? &zs_font_16 : &zs_font_20, 0);
        lv_obj_set_width(banner_dist_, kW - x - (then.empty() ? 6 : 56));
        lv_label_set_long_mode(banner_dist_, LV_LABEL_LONG_DOT);
        if (dist != lv_label_get_text(banner_dist_)) lv_label_set_text(banner_dist_, dist.c_str());
        lv_obj_set_y(banner_text_, dist.empty() ? 11 : 22);
        lv_obj_set_x(banner_text_, x);
        lv_obj_set_width(banner_text_, kW - x - (then.empty() ? 6 : 56));
        if (text != lv_label_get_text(banner_text_)) lv_label_set_text(banner_text_, text.c_str());
        if (then != lv_label_get_text(banner_then_)) lv_label_set_text(banner_then_, then.c_str());
        lv_obj_set_pos(banner_then_, kW - 40, 3);
        // The arrival, in the strip along the bottom.
        std::string eta = previewing_ ? "" : eta_text();
        if (eta != lv_label_get_text(strip_eta_)) lv_label_set_text(strip_eta_, eta.c_str());
        lv_obj_align(strip_eta_, LV_ALIGN_BOTTOM_RIGHT, -6, -2);
        set_hidden(eta_, true);
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

    // ---- Position and tracks (was the GPS app) ----------------------------

    static const char* compass_point(double deg) {
        static const char* k[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        return k[int(std::fmod(deg + 22.5 + 360.0, 360.0) / 45.0) % 8];
    }

    void open_gps() {
        if (gps_panel_) return;
        gps_panel_ = lv_obj_create(root_);
        lv_obj_remove_style_all(gps_panel_);
        lv_obj_set_size(gps_panel_, kW, kH);
        lv_obj_set_style_bg_color(gps_panel_, c(kBg), 0);
        lv_obj_set_style_bg_opa(gps_panel_, LV_OPA_COVER, 0);
        gps_status_ = label(gps_panel_, "", &zs_font_12, kMuted);
        lv_obj_set_pos(gps_status_, 8, 4);
        gps_lat_ = label(gps_panel_, "", &zs_font_16, kText);
        lv_obj_set_pos(gps_lat_, 8, 20);
        gps_lon_ = label(gps_panel_, "", &zs_font_16, kText);
        lv_obj_set_pos(gps_lon_, 8, 38);
        lv_obj_t* panel = lv_obj_create(gps_panel_);
        style_panel(panel);
        lv_obj_set_pos(panel, 6, 58);
        lv_obj_set_size(panel, kScreenW - 12, kBodyH - 60);
        lv_obj_set_style_pad_ver(panel, 6, 0);
        lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN_WRAP);
        lv_obj_set_style_pad_row(panel, 2, 0);
        lv_obj_set_style_pad_column(panel, 14, 0);
        auto row = [&](const char* k) {
            lv_obj_t* r = lv_obj_create(panel);
            lv_obj_remove_style_all(r);
            lv_obj_set_size(r, 140, 18);
            label(r, k, &zs_font_12, kMuted);
            lv_obj_t* v = label(r, "-", &zs_font_12, kText);
            lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
            return v;
        };
        gps_alt_ = row("Altitude");
        gps_speed_ = row("Speed");
        gps_head_ = row("Heading");
        gps_acc_ = row("Accuracy");
        gps_sats_ = row("Satellites");
        gps_time_ = row("UTC");
        gps_hint_ = label(gps_panel_, "", &zs_font_12, kMuted);
        lv_obj_align(gps_hint_, LV_ALIGN_TOP_RIGHT, -8, 4);
        set_actions();
        update_gps();
    }

    void close_gps() {
        if (gps_panel_) lv_obj_delete(gps_panel_);
        gps_panel_ = nullptr;
        set_actions();
        dirty_ = true;
    }

    // A Control screen: Enter saves the spot, R records; the rest is the shell's.
    bool gps_key(uint32_t key) {
        if (key == LV_KEY_ENTER) {
            mark_spot();
            return true;
        }
        if (key == 'r' || key == 'R') {
            toggle_track();
            return true;
        }
        return false;
    }

    void update_gps() {
        if (!map_) fix_ = hal().gps();   // with a map, tick() reads it
        const TrackState& t = track_state();
        if (t.recording != rec_shown_) set_actions();
        if (t.recording) {
            std::string dur = t.seconds < 3600 ? fmt("%ld:%02ld", t.seconds / 60, t.seconds % 60)
                                               : fmt("%ld:%02ld:%02ld", t.seconds / 3600, t.seconds / 60 % 60, t.seconds % 60);
            lv_label_set_text(gps_hint_, (LV_SYMBOL_STOP " REC  " + short_distance(t.meters) + "  " + dur).c_str());
        } else {
            lv_label_set_text(gps_hint_, "");
        }
        lv_obj_set_style_text_color(gps_hint_, c(t.recording ? kBad : kMuted), 0);
        long waited = long(std::time(nullptr) - opened_);
        if (!fix_.device) {
            lv_label_set_text(gps_status_, waited < 6 ? "Starting GPS..." : "No GPS found");
            lv_label_set_text(gps_lat_, waited < 6 ? "" : "Plug in a Cap LoRa-1262, a Grove GPS");
            lv_label_set_text(gps_lon_, waited < 6 ? "" : "unit or a USB GPS (power off first)");
            lv_obj_set_style_text_font(gps_lat_, &zs_font_12, 0);
            lv_obj_set_style_text_font(gps_lon_, &zs_font_12, 0);
            return;
        }
        lv_obj_set_style_text_font(gps_lat_, &zs_font_16, 0);
        lv_obj_set_style_text_font(gps_lon_, &zs_font_16, 0);
        const char* st = fix_.mode >= 3 ? LV_SYMBOL_GPS " 3D fix" : fix_.mode == 2 ? LV_SYMBOL_GPS " 2D fix"
                                                                                    : "Looking for satellites...";
        lv_label_set_text(gps_status_, st);
        lv_obj_set_style_text_color(gps_status_, c(fix_.mode >= 2 ? kGood : kMuted), 0);
        if (fix_.mode >= 2) {
            lv_label_set_text(gps_lat_, fmt("%.6f %s", std::fabs(fix_.lat), fix_.lat >= 0 ? "N" : "S").c_str());
            lv_label_set_text(gps_lon_, fmt("%.6f %s", std::fabs(fix_.lon), fix_.lon >= 0 ? "E" : "W").c_str());
            lv_label_set_text(gps_alt_, fix_.mode >= 3 ? (imperial_ ? fmt("%.0f ft", fix_.alt_m * 3.28084) : fmt("%.0f m", fix_.alt_m)).c_str() : "-");
            lv_label_set_text(gps_speed_, (imperial_ ? fmt("%.1f mph", fix_.speed_mps * 2.23694) : fmt("%.1f km/h", fix_.speed_mps * 3.6)).c_str());
            lv_label_set_text(gps_head_, fix_.speed_mps > 0.5 ? fmt("%.0f %s", fix_.track_deg, compass_point(fix_.track_deg)).c_str() : "-");
            lv_label_set_text(gps_acc_, fix_.error_m > 0 ? (imperial_ ? fmt("%.0f ft", fix_.error_m * 3.28084) : fmt("%.0f m", fix_.error_m)).c_str() : "-");
        } else {
            lv_label_set_text(gps_lat_, "--.------");
            lv_label_set_text(gps_lon_, "--.------");
        }
        lv_label_set_text(gps_sats_, fmt("%d of %d", fix_.sats_used, fix_.sats_seen).c_str());
        lv_label_set_text(gps_time_, fix_.time.size() >= 19 ? fix_.time.substr(11, 8).c_str() : "-");
    }

    // Tracks go on recording with Maps closed and the screen off (ZeroShell's ui/tracker.h).
    void toggle_track() {
        if (track_state().recording) {
            track_stop();
            const TrackState& t = track_state();
            shell_toast(t.points ? "Saved " + t.name + " (" + short_distance(t.meters) + ")"
                                 : "Stopped. No fix, so the track is empty.");
        } else if (track_start()) {
            shell_toast("Recording to Documents/Tracks");
        } else {
            shell_toast("Couldn't save to ~/Documents");
        }
        set_actions();
        if (gps_panel_) update_gps();
        update_overlays();
    }

    // A line in ~/Documents/waypoints.txt: time, position and altitude.
    void mark_spot() {
        if (fix_.mode < 2) {
            shell_toast("No fix yet");
            return;
        }
        std::ofstream f(home_dir() + "/Documents/waypoints.txt", std::ios::app);
        if (!f) {
            shell_toast("Couldn't save to ~/Documents");
            return;
        }
        char line[160];
        std::snprintf(line, sizeof line, "%s  %.6f, %.6f  %.0f m\n",
                      fix_.time.empty() ? "?" : fix_.time.substr(0, 19).c_str(), fix_.lat, fix_.lon, fix_.alt_m);
        f << line;
        shell_toast("Saved to waypoints.txt");
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
    lv_obj_t *banner_dist_ = nullptr, *banner_text_ = nullptr, *banner_then_ = nullptr, *eta_ = nullptr;
    lv_obj_t *strip_speed_ = nullptr, *strip_unit_ = nullptr, *strip_eta_ = nullptr;
    bool previewing_ = false, shortest_ = false, auto_zoom_ = true;
    nav::LatLon dest_;
    std::string colours_ = "auto";
    uint32_t manual_zoom_at_ = 0, zoom_wanted_at_ = 0;
    int zoom_wanted_ = -1;
    double est_s_ = -1;   // the shortest route's time, worked out here (estimate_time)
    std::vector<char> roles_;   // Go to's rows: 'h' Home, 'w' Work, 's' saved, 'r' recent, 0 found
    // Position and tracks
    lv_obj_t *gps_panel_ = nullptr, *gps_status_ = nullptr, *gps_lat_ = nullptr, *gps_lon_ = nullptr, *gps_hint_ = nullptr;
    lv_obj_t *gps_alt_ = nullptr, *gps_speed_ = nullptr, *gps_head_ = nullptr, *gps_acc_ = nullptr, *gps_sats_ = nullptr,
             *gps_time_ = nullptr;
    int gps_n_ = 0;
    bool rec_shown_ = false;   // what the R action's label says

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
