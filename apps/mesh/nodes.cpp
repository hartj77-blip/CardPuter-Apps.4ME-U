// Mesh: the Nodes tab, a node's page, traceroute, and the Map tab (who is
// where, from where you are). (app.h has the map of the files.)
#include <algorithm>
#include <cmath>

#include "apps/mesh/app.h"
#include "apps/mesh/core/text.h"
#include "ui/prefs.h"

namespace zs {
using namespace mesh_ui;

namespace {
constexpr int kTabH = 14;
const char* const kSorts[] = {"last heard", "name", "how close on the mesh", "distance"};
const char* const kFilters[] = {"everyone", "heard in 2 hours", "heard directly", "favourites"};
constexpr int kNumSorts = 4, kNumFilters = 4;

std::string hex_key(uint32_t num) { return mesh::node_id(num); }
}  // namespace

// ---- The Nodes tab

std::vector<uint32_t> MeshApp::sorted_nodes() {
    mesh::Client& c = client();
    long t = now();
    double lat = 0, lon = 0;
    bool here = c.here(lat, lon);
    std::vector<uint32_t> v;
    for (const auto& kv : c.nodes()) {
        const mesh::Node& n = kv.second;
        if (kv.first == c.my_num()) continue;
        if (node_filter_ == 1 && !mesh::Client::online(n, t)) continue;
        if (node_filter_ == 2 && !(n.hops == 0 && !n.via_mqtt)) continue;
        if (node_filter_ == 3 && !n.favorite) continue;
        v.push_back(kv.first);
    }
    auto dist = [&](const mesh::Node& n) { return here && n.has_pos ? mesh::distance_m(lat, lon, n.lat, n.lon) : 1e12; };
    auto hops = [](const mesh::Node& n) { return n.via_mqtt ? 90 : n.hops < 0 ? 80 : n.hops; };
    std::stable_sort(v.begin(), v.end(), [&](uint32_t x, uint32_t y) {
        const mesh::Node& a = *c.node(x);
        const mesh::Node& b = *c.node(y);
        switch (node_sort_) {
        case 1: {
            std::string an = a.name(), bn = b.name();
            for (char& ch : an) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            for (char& ch : bn) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            return an < bn;
        }
        case 2:
            if (hops(a) != hops(b)) return hops(a) < hops(b);
            if (a.snr != b.snr) return a.snr > b.snr;
            return a.last_heard > b.last_heard;
        case 3:
            if (dist(a) != dist(b)) return dist(a) < dist(b);
            return a.last_heard > b.last_heard;
        default:
            if (a.favorite != b.favorite) return a.favorite;   // the ones you starred, first
            return a.last_heard > b.last_heard;
        }
    });
    return v;
}

std::string MeshApp::node_row_text(const mesh::Node& n) {
    std::string s = n.favorite ? "* " : "";
    if (!n.has_user || (n.short_name.empty() && n.long_name.empty())) return s + mesh::node_id(n.num);
    if (n.short_name.empty()) return s + n.long_name;
    return s + n.short_name + (n.long_name.empty() ? "" : "  " + n.long_name);
}

std::string MeshApp::node_row_value(const mesh::Node& n) {
    mesh::Client& c = client();
    std::string link;
    double lat = 0, lon = 0;
    if (node_sort_ == 3 && n.has_pos && c.here(lat, lon)) {
        link = mesh::distance_text(mesh::distance_m(lat, lon, n.lat, n.lon)) + " " +
               mesh::compass_point(mesh::bearing_deg(lat, lon, n.lat, n.lon));
    } else if (n.via_mqtt) {
        link = "MQTT";
    } else if (n.hops == 0) {
        link = n.has_snr ? fmt("%+.0f dB", double(n.snr)) : "direct";
    } else if (n.hops > 0) {
        link = fmt("%d hop%s", n.hops, n.hops == 1 ? "" : "s");
    }
    std::string age = mesh::age_text(now(), n.last_heard);
    if (n.ignored) link = "ignored";
    return link + (link.empty() || age.empty() ? "" : "  ") + age;
}

void MeshApp::build_nodes_tab() {
    mesh::Client& c = client();
    set_actions({hint("Open", "Enter"),
                 menu((std::string("Sorted by ") + kSorts[node_sort_]).c_str(), 's', [this] {
                     node_sort_ = (node_sort_ + 1) % kNumSorts;
                     prefs::set_int("mesh_sort", node_sort_);
                     double lat, lon;
                     if (node_sort_ == 3 && !client().here(lat, lon)) shell_toast("By distance: needs your position (Radio > Share my position)");
                     else shell_toast(std::string("Sorted by ") + kSorts[node_sort_]);
                     dirty_ = true;
                 }),
                 menu((std::string("Showing ") + kFilters[node_filter_]).c_str(), 'v', [this] {
                     node_filter_ = (node_filter_ + 1) % kNumFilters;
                     prefs::set_int("mesh_filter", node_filter_);
                     shell_toast(std::string("Showing ") + kFilters[node_filter_]);
                     dirty_ = true;
                 }),
                 menu("Refresh the order", 'r', [this] { dirty_ = true; })});
    list_.build(root_, 4, kTabH + 1, kScreenW - 8, kBodyH - kTabH - 2);
    list_.set_empty_text(!c.ever_ready()      ? "Waiting for the radio..."
                         : node_filter_ == 0 ? "No one heard yet. Nodes appear here as they're heard."
                                             : std::string("No nodes: showing ") + kFilters[node_filter_] + " (Tab, V changes it)");
    node_order_ = sorted_nodes();
    long t = now();
    int row = 0, focus = -1;
    for (uint32_t num : node_order_) {
        const mesh::Node& n = *c.node(num);
        list_.add(nullptr, node_row_text(n), node_row_value(n), [this, num] {
            View v;
            v.page = Page::NodePage;
            v.num = num;
            push(v);
        });
        if (!mesh::Client::online(n, t)) list_.set_value_color(row, kDisabled);
        if (hex_key(num) == focus_key_) focus = row;
        ++row;
    }
    list_.finish();
    if (focus >= 0) list_.focus(focus);
    focus_key_.clear();
}

// The order is the one the list was drawn with: a list that reshuffles under
// the cursor each time a node is heard is no list to pick from. Values keep
// up; a node that appears or goes redraws it, with the focus where it was.
void MeshApp::refresh_nodes_tab() {
    mesh::Client& c = client();
    std::vector<uint32_t> want = sorted_nodes();
    std::vector<uint32_t> a = want, b = node_order_;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a != b) {
        if (!list_.filter().empty()) return;
        int f = list_.focused();
        if (f >= 0 && f < int(node_order_.size())) focus_key_ = hex_key(node_order_[size_t(f)]);
        dirty_ = true;
        return;
    }
    long t = now();
    int row = 0;
    for (uint32_t num : node_order_) {
        const mesh::Node* n = c.node(num);
        if (n) {
            list_.set_value(row, node_row_value(*n));
            list_.set_value_color(row, mesh::Client::online(*n, t) ? kMuted : kDisabled);
        }
        ++row;
    }
}

// ---- A node's page: what to do with it, then everything known about it.

void MeshApp::node_rows(const mesh::Node& n, std::vector<std::pair<std::string, std::string>>& rows) {
    mesh::Client& c = client();
    long t = now();
    bool self = n.num == c.my_num();
    if (!self) {
        std::string age = mesh::age_text(t, n.last_heard);
        rows.push_back({"Heard", age.empty() ? "never" : age == "now" ? "just now" : age + " ago"});
        rows.push_back({"Link", n.via_mqtt ? "by MQTT (the internet)" : n.hops == 0 ? "direct" : n.hops > 0 ? fmt("%d hop%s away", n.hops, n.hops == 1 ? "" : "s") : "not known"});
        if (n.hops == 0 && n.has_snr && !n.via_mqtt)
            rows.push_back({"Signal", n.rssi ? fmt("%s  %.1f dB  %d dBm", mesh::snr_quality(n.snr), double(n.snr), n.rssi)
                                             : fmt("%s  %.1f dB", mesh::snr_quality(n.snr), double(n.snr))});
    }
    double lat = 0, lon = 0;
    if (n.has_pos) {
        if (!self && c.here(lat, lon)) {
            double d = mesh::distance_m(lat, lon, n.lat, n.lon), b = mesh::bearing_deg(lat, lon, n.lat, n.lon);
            rows.push_back({"Distance", mesh::distance_text(d) + fmt(" %s (%.0f deg)", mesh::compass_point(b), b)});
        }
        rows.push_back({"Position", fmt("%.5f, %.5f", n.lat, n.lon)});
        if (n.has_alt) rows.push_back({"Altitude", fmt("%d m", n.alt)});
        std::string pa = mesh::age_text(t, n.pos_time);
        if (!pa.empty() && !self) rows.push_back({"Position from", pa == "now" ? "just now" : pa + " ago"});
    } else if (self) {
        rows.push_back({"Position", "none (Share my position)"});
    }
    if (n.has_metrics) {
        const mesh::DeviceMetrics& m = n.metrics;
        if (m.has_battery) rows.push_back({"Battery", m.battery_level > 100 ? "plugged in" : fmt("%u%%", m.battery_level)});
        if (m.has_voltage && m.voltage > 0) rows.push_back({"Voltage", fmt("%.2f V", double(m.voltage))});
        if (m.has_ch_util) rows.push_back({"Channel in use", fmt("%.1f%%", double(m.channel_utilization))});
        if (m.has_air_util) rows.push_back({"Its airtime", fmt("%.1f%%", double(m.air_util_tx))});
        if (m.has_uptime) rows.push_back({"Up for", mesh::duration_text(long(m.uptime_seconds))});
    }
    if (n.has_env) {
        if (n.env.has_temperature) rows.push_back({"Temperature", fmt("%.1f C", double(n.env.temperature))});
        if (n.env.has_humidity) rows.push_back({"Humidity", fmt("%.0f%%", double(n.env.relative_humidity))});
        if (n.env.has_pressure) rows.push_back({"Pressure", fmt("%.1f hPa", double(n.env.barometric_pressure))});
        if (n.env.has_iaq) rows.push_back({"Air quality", fmt("IAQ %u", n.env.iaq)});
    }
    if (n.has_user) {
        rows.push_back({"Role", mesh::role_name(n.role)});
        std::string hw = mesh::hw_model_name(n.hw_model);
        if (!hw.empty()) rows.push_back({"Hardware", hw});
        if (!self) rows.push_back({"Private messages", n.has_key ? "yes, has a key" : "no key heard yet"});
        if (n.licensed) rows.push_back({"Licensed", "ham radio operator"});
    }
    rows.push_back({"ID", mesh::node_id(n.num)});
}

void MeshApp::build_node_page() {
    mesh::Client& c = client();
    uint32_t num = stack_.back().num;
    const mesh::Node* n = c.node(num);
    if (!n) {
        set_actions({hint("Back", "Esc")});
        title_row(mesh::node_id(num));
        label(root_, "This node is no longer in the radio's list.", &zs_font_12, kMuted);
        return;
    }
    bool self = num == c.my_num();
    std::vector<Action> acts = {hint("Select", "Enter"), hint("Back", "Esc")};
    if (!self) {
        acts.push_back(menu("Ask who it is", 'u', [this, num] {
            if (client().request_user(num, now())) shell_toast("Asked. The answer can take a minute");
        }));
        acts.push_back(danger("Remove from the list", 'd', "Remove " + n->name() + " from the radio's list?", [this, num] {
            client().remove_node(num);
            pop();
        }));
    }
    set_actions(std::move(acts));
    title_row(n->full_name(), n->short_name.empty() ? "" : n->short_name);
    list_.build(root_, 4, 17, kScreenW - 8, kBodyH - 18);
    int rows = 0;
    if (!self) {
        if (!n->unmessagable) {
            list_.add(LV_SYMBOL_ENVELOPE, "Message", n->has_key ? "private" : "", [this, num] {
                client().open_dm(num);
                open_chat(mesh::chat_key_dm(num));
            });
            ++rows;
        }
        const mesh::TraceResult* t = c.last_trace(num);
        std::string tv = !t ? "" : t->failed ? "failed" : !t->answered ? "waiting..." : t->route.route.empty() ? "direct" : fmt("%d between", int(t->route.route.size()));
        list_.add(ZS_ICON_NETWORK, "Trace the route", tv, [this, num] { run_traceroute(num); });
        list_.add(LV_SYMBOL_GPS, "Ask where it is", "", [this, num] {
            if (client().request_position(num, now())) shell_toast("Asked. The answer can take a minute");
        });
        list_.add(LV_SYMBOL_BATTERY_3, "Ask how it's doing", "", [this, num] {
            // The device's own figures, and its sensors' if it has any.
            if (client().request_telemetry(num, now(), false)) shell_toast("Asked. The answer can take a minute");
        });
        list_.add_choice(nullptr, "Favourite", {"No", "Yes"}, n->favorite ? 1 : 0, [this, num](int at) { client().set_favorite(num, at == 1); });
        list_.add_choice(nullptr, "Ignore its messages", {"No", "Yes"}, n->ignored ? 1 : 0, [this, num](int at) { client().set_ignored(num, at == 1); });
        rows += 5;
    }
    std::vector<std::pair<std::string, std::string>> info;
    node_rows(*n, info);
    value_rows_.clear();
    for (const auto& r : info) {
        std::string k = r.first, v = r.second;
        value_rows_.push_back(list_.add(nullptr, r.first, r.second, [k, v] { shell_toast(k + ": " + v); }));
    }
    list_.finish();
    stack_.back().index = int(info.size());
}

void MeshApp::refresh_node_page() {
    const mesh::Node* n = client().node(stack_.back().num);
    if (!n) {
        dirty_ = true;
        return;
    }
    std::vector<std::pair<std::string, std::string>> info;
    node_rows(*n, info);
    if (info.size() != value_rows_.size()) {
        if (list_.filter().empty()) dirty_ = true;   // something new is known: draw the rows again
        return;
    }
    for (size_t i = 0; i < info.size(); ++i) list_.set_value(value_rows_[i], info[i].second);
}

// ---- Traceroute: which nodes a message passes through, each way.

void MeshApp::run_traceroute(uint32_t node) {
    mesh::Client& c = client();
    const mesh::TraceResult* last = c.last_trace(node);
    bool waiting = last && !last->answered && !last->failed;
    if (!waiting && !c.traceroute(node, now())) {
        // The radio takes one every 30 s: show the last answer meanwhile.
        shell_toast(c.last_error());
        if (!last) return;
    }
    View v;
    v.page = Page::Trace;
    v.num = node;
    push(v);
}

void MeshApp::build_trace() {
    mesh::Client& c = client();
    uint32_t node = stack_.back().num;
    set_actions({hint("Back", "Esc"), hint("Scroll", LV_SYMBOL_UP LV_SYMBOL_DOWN), menu("Trace again", 't', [this, node] {
                     if (!client().traceroute(node, now())) shell_toast(client().last_error());
                     dirty_ = true;
                 })});
    std::string body;
    int shown = 0;
    for (size_t i = c.traces().size(); i-- > 0 && shown < 5;) {
        const mesh::TraceResult& t = c.traces()[i];
        if (t.dest != node) continue;
        if (shown) body += "\n";
        std::string when = mesh::age_text(now(), t.asked);
        body += (when == "now" ? std::string("Just now") : when + " ago");
        if (t.answered) body += fmt(", answered in %ld s", t.answered - t.asked);
        body += "\n";
        for (const std::string& line : c.trace_lines(t)) body += line + "\n";
        ++shown;
    }
    if (!shown) body = "Not traced yet.";
    else body += "\nThe figure after a node is how well it heard the one before (SNR). \"?\" is a node that didn't say who it is.";
    build_text_page("Route to " + c.node_name(node), body);
}

// ---- The Map tab: nodes that share a position, around where you are.

void MeshApp::map_collect(double& lat0, double& lon0, bool& have_me) {
    mesh::Client& c = client();
    have_me = c.here(lat0, lon0);
    std::vector<uint32_t> with_pos;
    for (const auto& kv : c.nodes())
        if (kv.first != c.my_num() && kv.second.has_pos) with_pos.push_back(kv.first);
    if (!have_me && !with_pos.empty()) {
        // No position of our own: the middle of everyone else.
        double la = 0, lo = 0;
        for (uint32_t n : with_pos) {
            la += c.node(n)->lat;
            lo += c.node(n)->lon;
        }
        lat0 = la / double(with_pos.size());
        lon0 = lo / double(with_pos.size());
    }
    double la = lat0, lo = lon0;
    std::sort(with_pos.begin(), with_pos.end(), [&c, la, lo](uint32_t x, uint32_t y) {
        const mesh::Node& a = *c.node(x);
        const mesh::Node& b = *c.node(y);
        return mesh::distance_m(la, lo, a.lat, a.lon) < mesh::distance_m(la, lo, b.lat, b.lon);
    });
    // Keep the pick on the same node while the order changes under it.
    uint32_t picked = map_sel_ >= 0 && map_sel_ < int(map_nodes_.size()) ? map_nodes_[size_t(map_sel_)] : 0;
    map_nodes_ = with_pos;
    map_sel_ = -1;
    for (size_t i = 0; i < map_nodes_.size(); ++i)
        if (map_nodes_[i] == picked) map_sel_ = int(i);
    if (map_sel_ < 0 && !map_nodes_.empty()) map_sel_ = 0;
}

void MeshApp::build_map_tab() {
    set_actions({hint("Open", "Enter"), hint("Next", LV_SYMBOL_UP LV_SYMBOL_DOWN), hint("Zoom", "+ -"),
                 menu("Fit everyone", 'f', [this] {
                     map_range_ = 0;
                     draw_map();
                 })});
    map_ = lv_obj_create(root_);
    style_plain(map_);
    lv_obj_set_pos(map_, 0, kTabH + 1);
    lv_obj_set_size(map_, kScreenW, kBodyH - kTabH - 1);
    lv_obj_remove_flag(map_, LV_OBJ_FLAG_SCROLLABLE);
    draw_map();
}

void MeshApp::draw_map() {
    if (!map_) return;
    mesh::Client& c = client();
    lv_obj_clean(map_);
    const int W = 196, H = kBodyH - kTabH - 1, cx = W / 2, cy = H / 2, R = H / 2 - 3;
    double lat0 = 0, lon0 = 0;
    bool have_me = false;
    map_collect(lat0, lon0, have_me);
    if (map_nodes_.empty()) {
        lv_obj_t* l = label(map_, !c.ever_ready() ? "Waiting for the radio..."
                                                  : "No one is sharing a position yet.\n\nNodes that do appear here, around where you are. "
                                                    "To be on other people's maps, turn on Share my position (Radio).",
                            &zs_font_12, kMuted);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, kScreenW - 24);
        lv_obj_set_pos(l, 12, 8);
        return;
    }
    // How far the edge of the picture is: fit everyone, unless zoomed.
    double far = 100;
    for (uint32_t n : map_nodes_) far = std::max(far, mesh::distance_m(lat0, lon0, c.node(n)->lat, c.node(n)->lon));
    double range = map_range_ > 0 ? map_range_ : far * 1.08;
    double scale = double(R) / range;   // pixels per metre

    auto ring = [&](int r, lv_opa_t opa) {
        lv_obj_t* o = lv_obj_create(map_);
        style_plain(o);
        lv_obj_set_size(o, 2 * r, 2 * r);
        lv_obj_set_pos(o, cx - r, cy - r);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(o, col(kBorder), 0);
        lv_obj_set_style_border_width(o, 1, 0);
        lv_obj_set_style_border_opa(o, opa, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    };
    ring(R, LV_OPA_COVER);
    ring(R / 2, LV_OPA_70);
    lv_obj_t* north = label(map_, "N", &zs_font_12, kMuted);
    lv_obj_set_pos(north, cx - 4, 0);
    lv_obj_t* edge = label(map_, mesh::distance_text(range).c_str(), &zs_font_12, kDisabled);
    lv_obj_set_pos(edge, 2, H - 14);

    auto dot = [&](int x, int y, int size, uint32_t color) {
        lv_obj_t* o = lv_obj_create(map_);
        style_plain(o);
        lv_obj_set_size(o, size, size);
        lv_obj_set_pos(o, x - size / 2, y - size / 2);
        lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(o, col(color), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
        return o;
    };
    if (have_me) {
        // You: a small cross in the middle.
        for (int k = 0; k < 2; ++k) {
            lv_obj_t* o = lv_obj_create(map_);
            style_plain(o);
            lv_obj_set_size(o, k ? 1 : 9, k ? 9 : 1);
            lv_obj_set_pos(o, cx - (k ? 0 : 4), cy - (k ? 4 : 0));
            lv_obj_set_style_bg_color(o, col(kText), 0);
            lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        }
    }
    long t = now();
    const double kPi = 3.14159265358979323846;
    struct Spot {
        int x, y;
        size_t i;
        bool outside;
    };
    std::vector<Spot> spots;
    for (size_t i = 0; i < map_nodes_.size(); ++i) {
        const mesh::Node& n = *c.node(map_nodes_[i]);
        double d = mesh::distance_m(lat0, lon0, n.lat, n.lon);
        double b = mesh::bearing_deg(lat0, lon0, n.lat, n.lon) * kPi / 180.0;
        bool outside = d > range;
        double r = outside ? double(R) : d * scale;   // off the picture: on its edge, in its direction
        spots.push_back({cx + int(std::lround(r * std::sin(b))), cy - int(std::lround(r * std::cos(b))), i, outside});
    }
    // Dots first, the picked one last so it's on top; then names, the picked
    // one first, and only where they don't cover another name.
    std::vector<lv_area_t> taken;
    auto place = [&](const Spot& s, bool picked) {
        const mesh::Node& n = *c.node(map_nodes_[s.i]);
        lv_obj_t* l = label(map_, n.name().c_str(), &zs_font_12, picked ? kText : kMuted);
        lv_obj_update_layout(l);
        int lw = int(lv_obj_get_width(l)), lh = 13;
        int lx = s.x + 6 + lw > W ? s.x - 6 - lw : s.x + 6;
        lx = std::max(0, lx);
        int ly = std::min(std::max(0, s.y - 7), H - 14);
        lv_area_t a{lx, ly, lx + lw, ly + lh};
        for (const lv_area_t& o : taken) {
            if (!picked && a.x1 < o.x2 && o.x1 < a.x2 && a.y1 < o.y2 && o.y1 < a.y2) {
                lv_obj_delete(l);   // crowded: the dot says enough, Up and Down name it
                return;
            }
        }
        taken.push_back(a);
        lv_obj_set_pos(l, lx, ly);
    };
    for (const Spot& s : spots) {
        if (int(s.i) == map_sel_) continue;
        const mesh::Node& n = *c.node(map_nodes_[s.i]);
        dot(s.x, s.y, 6, s.outside ? kDisabled : mesh::Client::online(n, t) ? kGood : kMuted);
    }
    for (const Spot& s : spots)
        if (int(s.i) == map_sel_) {
            dot(s.x, s.y, 9, kFocus);
            place(s, true);
        }
    for (const Spot& s : spots)
        if (int(s.i) != map_sel_) place(s, false);
    if (!have_me) {
        // Without our own position the middle is everyone's middle, and the
        // distances are from there.
        lv_obj_t* w = label(map_, "You: no position", &zs_font_12, kWarn);
        lv_obj_set_pos(w, 2, H - 14);
        lv_obj_set_y(edge, H - 28);
    }

    // The picked node, in words, on the right.
    lv_obj_t* side = lv_obj_create(map_);
    style_panel(side);
    lv_obj_set_pos(side, W + 2, 1);
    lv_obj_set_size(side, kScreenW - W - 6, H - 3);
    lv_obj_set_style_pad_all(side, 4, 0);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 1, 0);
    lv_obj_remove_flag(side, LV_OBJ_FLAG_SCROLLABLE);
    int sw = kScreenW - W - 16;
    auto line = [&](const std::string& s, const lv_font_t* font, uint32_t color) {
        lv_obj_t* l = label(side, s.c_str(), font, color);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, sw);
    };
    if (map_sel_ >= 0) {
        const mesh::Node& n = *c.node(map_nodes_[size_t(map_sel_)]);
        double d = mesh::distance_m(lat0, lon0, n.lat, n.lon), b = mesh::bearing_deg(lat0, lon0, n.lat, n.lon);
        line(n.name() + fmt("  %d/%d", map_sel_ + 1, int(map_nodes_.size())), &zs_font_14, kText);
        if (n.long_name != n.name() && !n.long_name.empty()) line(n.long_name, &zs_font_12, kMuted);
        line(mesh::distance_text(d) + " " + mesh::compass_point(b), &zs_font_14, kText);
        line(fmt(have_me ? "%.0f\xC2\xB0 from you" : "%.0f\xC2\xB0", b), &zs_font_12, kMuted);
        if (n.has_alt) line(fmt("%d m up", n.alt), &zs_font_12, kMuted);
        std::string age = mesh::age_text(t, n.last_heard);
        line(age.empty() ? "not heard" : age == "now" ? "heard just now" : "heard " + age + " ago", &zs_font_12,
             mesh::Client::online(n, t) ? kGood : kMuted);
    }
}

bool MeshApp::map_key(uint32_t key) {
    if (map_nodes_.empty()) return false;
    int n = int(map_nodes_.size());
    if (key == LV_KEY_DOWN || key == LV_KEY_UP) {
        map_sel_ = (map_sel_ + (key == LV_KEY_DOWN ? 1 : n - 1)) % n;   // nearest first
        draw_map();
        return true;
    }
    if (key == LV_KEY_ENTER && map_sel_ >= 0) {
        View v;
        v.page = Page::NodePage;
        v.num = map_nodes_[size_t(map_sel_)];
        push(v);
        return true;
    }
    if (key == '+' || key == '=' || key == '-' || key == '_') {
        mesh::Client& c = client();
        double lat0 = 0, lon0 = 0, far = 100;
        bool have = false;
        map_collect(lat0, lon0, have);
        for (uint32_t m : map_nodes_) far = std::max(far, mesh::distance_m(lat0, lon0, c.node(m)->lat, c.node(m)->lon));
        double range = map_range_ > 0 ? map_range_ : far * 1.08;
        range = key == '+' || key == '=' ? range / 2 : range * 2;
        map_range_ = std::min(std::max(range, 50.0), 2000000.0);
        draw_map();
        return true;
    }
    return false;
}

}  // namespace zs
