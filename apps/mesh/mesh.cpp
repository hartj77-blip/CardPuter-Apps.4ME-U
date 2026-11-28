// Mesh: the Cardputer as a Meshtastic node (Cap LoRa-1262), for messages
// with no phone signal and no internet. meshtasticd runs the radio; our own
// small daemon, zeroos-meshd, sits in front of it so that the app, ZeroShell's
// notes and a phone can all use it at once, and keeps the message history
// while the app is closed (core/hub.h). README.md has the tour.
//
// This file: what's on the screen (a stack of pages; Esc goes back one), the
// radio service's state, the first-run setup, and the pieces every page uses.
// app.h says which file has which page.
#include "apps/mesh/app.h"

#include <cctype>
#include <cstdlib>
#include <ctime>

#include "apps/mesh/core/text.h"
#include "ui/fileio.h"
#include "ui/power.h"
#include "ui/prefs.h"
#include "ui/tracker.h"

namespace zs {
using namespace mesh_ui;

namespace {
constexpr int kTabH = 14;

bool simulated() {
    static int s = -1;
    if (s < 0) s = hal().system().simulated ? 1 : 0;
    return s == 1;
}
}  // namespace

MeshApp::MeshApp() {
    node_sort_ = prefs::get_int("mesh_sort", 0);
    node_filter_ = prefs::get_int("mesh_filter", 0);
    setup_skipped_ = prefs::get_int("mesh_setup_skipped", 0) != 0;
    // For pictures of the app (tools/screenshots.sh): the simulated mesh, on.
    if (simulated() && std::getenv("ZS_MESH_DEMO")) hal().mesh_start();
}

MeshApp::~MeshApp() {
    if (scanning_) hal().scan_stop();
    if (ta_ && !stack_.empty() && stack_.back().page == Page::Chat) drafts_[stack_.back().key] = lv_textarea_get_text(ta_);
    session().save_marks();
    session().suspend();
    // The GPS was ours to watch only while the app was open; the daemon keeps
    // sharing the position by itself. A track being recorded still needs it.
    if (gps_used_ && !track_state().recording) hal().gps_stop();
}

mesh::Session& MeshApp::session() {
    // One for as long as ZeroShell runs: opening the app again shows what it
    // knew at once, then brings it up to date.
    static std::unique_ptr<mesh::Session> s;
    if (!s) {
        mesh::Session::Options o;
        o.simulated = simulated();
        if (!o.simulated) o.data_dir = home_dir() + "/.local/share/zeroos/mesh";
        s = std::make_unique<mesh::Session>(o);
    }
    return *s;
}

long MeshApp::now_ms() const {
    static uint32_t last = lv_tick_get();
    static long acc = 0;
    uint32_t t = lv_tick_get();
    acc += long(uint32_t(t - last));
    last = t;
    return acc;
}

long MeshApp::now() const {
    if (!simulated()) return long(std::time(nullptr));
    // The simulator's clock is the screen's (it runs fast under a script).
    static long t0 = long(std::time(nullptr));
    static long ms0 = now_ms();
    return t0 + (now_ms() - ms0) / 1000;
}

// ---- Pages

void MeshApp::push(const View& v) {
    if (ta_ && !stack_.empty() && stack_.back().page == Page::Chat) drafts_[stack_.back().key] = lv_textarea_get_text(ta_);
    stack_.push_back(v);
    dirty_ = true;
}

void MeshApp::pop() {
    if (stack_.size() > 1) stack_.pop_back();
    dirty_ = true;
}

void MeshApp::replace_top(const View& v) {
    if (!stack_.empty()) stack_.pop_back();
    stack_.push_back(v);
    dirty_ = true;
}

void MeshApp::reset_to(Page p) {
    stack_.clear();
    View v;
    v.page = p;
    stack_.push_back(v);
    dirty_ = true;
}

void MeshApp::clear_screen() {
    if (scanning_) {
        hal().scan_stop();
        scanning_ = false;
    }
    lv_obj_clean(root_);
    list_.reset();
    if (group) lv_group_remove_all_objs(group);
    chat_box_ = chat_head_ = ta_ = nullptr;
    map_ = map_note_ = nullptr;
    live_label_ = nullptr;
    scan_img_ = scan_note_ = nullptr;
    tab_status_ = nullptr;
    for (auto& t : tab_labels_) t = nullptr;
    msg_objs_.clear();
    msg_index_.clear();
    value_rows_.clear();
    sel_ = -1;
}

void MeshApp::build(lv_obj_t* root) {
    root_ = root;
    st_ = hal().mesh();
    bool on = st_.running && !st_.failing;
    if (on) session().resume();
    reset_to(on ? Page::Main : Page::Off);
    std::string arg = shell_app_arg();
    if (on && arg == "radio") tab_ = kRadio;
    if (on && arg == "nodes") tab_ = kNodes;
    dirty_ = false;
    rebuild();
}

void MeshApp::rebuild() {
    clear_screen();
    seen_chats_ = client().chats_rev();
    seen_nodes_ = client().nodes_rev();
    seen_config_ = client().config_rev();
    seen_log_ = client().log_rev();
    if (stack_.empty()) reset_to(Page::Off);
    dirty_ = false;
    switch (stack_.back().page) {
    case Page::Off: build_off(); break;
    case Page::Setup: build_setup(); break;
    case Page::Main:
        build_tabs();
        if (tab_ == kChats) build_chats_tab();
        else if (tab_ == kNodes) build_nodes_tab();
        else if (tab_ == kMap) build_map_tab();
        else build_radio_tab();
        break;
    case Page::Chat: build_chat(); break;
    case Page::Quick: build_quick(); break;
    case Page::React: build_react(); break;
    case Page::MessageInfo: build_message_info(); break;
    case Page::Search: build_search(); break;
    case Page::NodePage: build_node_page(); break;
    case Page::Trace: build_trace(); break;
    case Page::Channels: build_channels(); break;
    case Page::ChannelPage: build_channel_page(); break;
    case Page::ShowQr: build_show_qr(); break;
    case Page::Scan: build_scan(); break;
    case Page::Log: build_log(); break;
    case Page::About: build_about(); break;
    case Page::Pick: build_pick(); break;
    case Page::Prompt: build_prompt(); break;
    }
}

bool MeshApp::on_back() {
    if (stack_.empty()) return false;
    Page p = stack_.back().page;
    if (p == Page::Chat) {
        // Esc peels before it leaves: the picked message, the reply, then the page.
        if (sel_ >= 0) {
            select_message(-1);
            return true;
        }
        if (reply_to_ || bell_) {
            reply_to_ = 0;
            bell_ = false;
            if (ta_) lv_textarea_set_placeholder_text(ta_, "Message");
            update_chat_head();
            return true;
        }
    }
    if (p == Page::Setup) {
        // Not now: the tabs, with a reminder that nothing can be sent yet.
        setup_skipped_ = true;
        prefs::set_int("mesh_setup_skipped", 1);
        reset_to(Page::Main);
        return true;
    }
    if (stack_.size() > 1) {
        pop();
        return true;
    }
    return false;   // leave the app
}

bool MeshApp::on_key(uint32_t key) {
    if (stack_.empty()) return false;
    switch (stack_.back().page) {
    case Page::Main:
        if (key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
            if (tab_ == kRadio && list_.focused_is_choice()) return list_.key(key);
            switch_tab(tab_ + (key == LV_KEY_RIGHT ? 1 : kTabs - 1));
            return true;
        }
        if (tab_ == kMap) return map_key(key);
        return list_.key(key);
    case Page::Chat: return chat_key(key);
    case Page::Prompt: return false;   // typing
    case Page::ShowQr: return false;
    case Page::Scan: return key != LV_KEY_ESC;
    case Page::MessageInfo:
    case Page::Trace:
    case Page::About:
    case Page::Log:
        if (live_label_ && (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
            lv_obj_scroll_by_bounded(lv_obj_get_parent(live_label_), 0, key == LV_KEY_UP ? 36 : -36, LV_ANIM_OFF);
            return true;
        }
        return false;
    default: return list_.key(key);
    }
}

// ---- The radio service, and keeping the screen up to date

void MeshApp::start_mesh() {
    hal().mesh_start();
    starting_ = true;
    starting_at_ = now();
    dirty_ = true;
}

void MeshApp::stop_mesh() {
    session().suspend();
    hal().mesh_stop();
    if (!track_state().recording) hal().set_ext_power(false);   // a track still needs the GPS
    starting_ = restarting_ = false;
    reset_to(Page::Off);
}

// The radio was running when Mesh was updated from 1.x: it's still on its old
// port, with no daemon in front of it. Starting it again puts both right.
void MeshApp::restart_mesh_service() {
    shell_toast("Restarting the radio to finish updating Mesh...");
    session().suspend();
    hal().mesh_stop();
    restarting_ = true;
}

void MeshApp::poll_radio() {
    MeshStatus m = hal().mesh();
    bool was_on = st_.running && !st_.failing, on = m.running && !m.failing;
    std::string before = std::to_string(st_.installed) + std::to_string(st_.running) + std::to_string(st_.no_radio) +
                         std::to_string(st_.failing) + st_.error + st_.web_url;
    std::string after = std::to_string(m.installed) + std::to_string(m.running) + std::to_string(m.no_radio) +
                        std::to_string(m.failing) + m.error + m.web_url;
    st_ = m;
    if (restarting_ && !m.running) {
        restarting_ = false;
        start_mesh();
        return;
    }
    if (m.running || m.failing) starting_ = false;
    if (on && !was_on) {
        session().resume();
        reset_to(Page::Main);
        return;
    }
    if (!on && was_on && !restarting_) {
        session().suspend();
        reset_to(Page::Off);
        return;
    }
    if (before != after && !stack_.empty() && (stack_.back().page == Page::Off || stack_.back().page == Page::Main)) dirty_ = true;
    if (!on || simulated()) return;
    // Connected to nothing for a while: is the daemon there at all?
    long waited = session().waiting(now());
    if (waited >= 8 && !checked_daemon_at_) {
        checked_daemon_at_ = now();
        if (std::system("systemctl is-active --quiet zeroos-meshd.service") != 0) restart_mesh_service();
    }
}

void MeshApp::use_gps() {
    long ms = now_ms();
    if (ms - gps_at_ms_ < 2000 && gps_at_ms_) return;
    gps_at_ms_ = ms;
    if (share_level() == 0 || !session().running()) {
        client().set_here(false, 0, 0);
        return;
    }
    gps_used_ = true;
    GpsFix g = hal().gps();
    bool fix = g.mode >= 2 && !(g.lat == 0 && g.lon == 0);
    client().set_here(fix, g.lat, g.lon);
    // On the device the daemon gives the radio our position, app open or
    // not. The simulator has no daemon: the app does it.
    if (simulated() && fix && client().ready()) client().send_own_position(g.lat, g.lon, true, int(g.alt_m), now(), 120);
}

void MeshApp::pump() {
    mesh::Session& s = session();
    if (!s.running()) return;
    s.tick(now(), now_ms());
    for (const std::string& n : client().take_notices()) shell_toast(n);
    use_gps();
    // First run: no region yet means nothing can be sent or heard. Offer the
    // setup once the radio's settings are in.
    if (client().needs_region() && !setup_offered_ && !setup_skipped_ && !stack_.empty() && stack_.back().page == Page::Main) {
        setup_offered_ = true;
        setup_region_ = 0;
        const mesh::Node* me = client().me();
        setup_name_ = me ? me->long_name : "";
        View v;
        v.page = Page::Setup;
        push(v);
    }
}

void MeshApp::tick() {
    ++n_;
    if (n_ % 10 == 1) poll_radio();
    pump();
    if (dirty_) {
        rebuild();
        return;
    }
    if (stack_.empty()) return;
    Page p = stack_.back().page;
    if (p == Page::Scan) {
        scan_tick();
        return;
    }
    if (n_ % 5) return;
    mesh::Client& c = client();
    bool chats = c.chats_rev() != seen_chats_, nodes = c.nodes_rev() != seen_nodes_, config = c.config_rev() != seen_config_;
    switch (p) {
    case Page::Main:
        if (tab_ == kChats && (chats || nodes || config)) refresh_chats_tab();
        else if (tab_ == kNodes && (nodes || n_ % 100 == 0)) refresh_nodes_tab();
        else if (tab_ == kMap && (nodes || n_ % 20 == 0)) draw_map();
        else if (tab_ == kRadio && (config || nodes)) refresh_radio_tab();
        if (chats || nodes || config || n_ % 20 == 0) {
            // The strip: unread on the Chats tab, the state on the right.
            if (tab_labels_[kChats]) {
                int unread = c.total_unread();
                lv_label_set_text(tab_labels_[kChats], unread ? fmt("Chats %d", unread).c_str() : "Chats");
            }
            if (tab_status_) {
                lv_label_set_text(tab_status_, status_line().c_str());
                lv_obj_set_style_text_color(tab_status_, lv_color_hex(status_color()), 0);
            }
        }
        break;
    case Page::Chat:
        if (chats) fill_chat();
        if (chats || nodes || config || n_ % 20 == 0) update_chat_head();
        break;
    case Page::NodePage:
        if (nodes || n_ % 20 == 0) refresh_node_page();
        break;
    case Page::Trace:
        if (nodes) dirty_ = true;
        break;
    case Page::Log:
        if (c.log_rev() != seen_log_) refresh_log();
        break;
    case Page::About:
        if (live_label_ && n_ % 20 == 0) lv_label_set_text(live_label_, about_text().c_str());
        break;
    case Page::Channels:
        if (config) dirty_ = true;
        break;
    case Page::Off:
        if (starting_ && now() - starting_at_ > 40) {   // it never came up
            starting_ = false;
            dirty_ = true;
        }
        break;
    default: break;
    }
    seen_chats_ = c.chats_rev();
    seen_nodes_ = c.nodes_rev();
    seen_config_ = c.config_rev();
}

// ---- Pieces every page uses

void MeshApp::set_actions(std::vector<Action> a) { shell_set_actions(std::move(a)); }

Action MeshApp::hint(const char* label, const char* key) {
    Action a;
    a.label = label;
    a.key = key;
    return a;
}
Action MeshApp::menu(const char* label, char letter, std::function<void()> run, bool enabled) {
    Action a;
    a.label = label;
    a.letter = letter;
    a.run = std::move(run);
    a.enabled = enabled;
    return a;
}
Action MeshApp::danger(const char* label, char letter, const std::string& confirm, std::function<void()> run) {
    Action a;
    a.label = label;
    a.letter = letter;
    a.run = std::move(run);
    a.danger = true;
    a.confirm = confirm;
    return a;
}

lv_obj_t* MeshApp::title_row(const std::string& title, const std::string& note, uint32_t note_color) {
    lv_obj_t* n = label(root_, note.c_str(), &zs_font_12, note_color ? note_color : kMuted);
    lv_obj_align(n, LV_ALIGN_TOP_RIGHT, -6, 2);
    lv_obj_update_layout(n);
    lv_obj_t* t = label(root_, title.c_str(), &zs_font_14, kAccent);
    lv_obj_set_pos(t, 6, 0);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, kScreenW - 18 - int(lv_obj_get_width(n)));
    return n;
}

std::string MeshApp::status_line() {
    mesh::Client& c = client();
    if (st_.no_radio) return "No radio found";
    if (!c.linked()) return "Connecting...";
    if (!c.ready()) return c.ever_ready() ? "Radio restarting..." : "Reading the radio...";
    if (c.needs_region()) return "No region: nothing is sent";
    if (c.has_lora() && !c.lora().tx_enabled) return "Transmit is off";
    const mesh::Node* me = c.me();
    int online = c.online_count(now());
    return (me ? me->name() : std::string("Mesh")) + fmt(" | %d online", online);
}

uint32_t MeshApp::status_color() {
    mesh::Client& c = client();
    if (st_.no_radio) return kBad;
    if (!c.ready()) return kInfo;
    if (c.needs_region() || (c.has_lora() && !c.lora().tx_enabled)) return kWarn;
    return kMuted;
}

void MeshApp::build_tabs() {
    static const char* const kNames[kTabs] = {"Chats", "Nodes", "Map", "Radio"};
    int x = 6;
    for (int i = 0; i < kTabs; ++i) {
        std::string text = kNames[i];
        if (i == kChats && client().total_unread()) text += fmt(" %d", client().total_unread());
        lv_obj_t* l = label(root_, text.c_str(), &zs_font_12, i == tab_ ? kText : kMuted);
        lv_obj_set_pos(l, x, 0);
        tab_labels_[i] = l;
        if (i == tab_) {
            // The tab you're on: white, with a line under it.
            lv_obj_update_layout(l);
            lv_obj_t* bar = lv_obj_create(root_);
            style_plain(bar);
            lv_obj_set_style_bg_color(bar, c(kFocus), 0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_size(bar, int(lv_obj_get_width(l)), 1);
            lv_obj_set_pos(bar, x, kTabH - 1);
        }
        x += i == kChats ? 56 : 46;
    }
    tab_status_ = label(root_, status_line().c_str(), &zs_font_12, status_color());
    lv_label_set_long_mode(tab_status_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(tab_status_, kScreenW - x - 8);
    lv_obj_set_style_text_align(tab_status_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(tab_status_, x, 0);
}

void MeshApp::switch_tab(int tab) {
    tab_ = ((tab % kTabs) + kTabs) % kTabs;
    dirty_ = true;
}

// The mesh isn't running: turn it on, or what's wrong with it.
void MeshApp::build_off() {
    set_actions({hint("Select", "Enter")});
    lv_obj_t* panel = lv_obj_create(root_);
    style_panel(panel);
    lv_obj_set_pos(panel, 6, 4);
    lv_obj_set_size(panel, 150, kBodyH - 10);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 3, 0);

    bool bad = st_.failing || (st_.running && st_.no_radio);
    const char* head = !st_.installed ? "Not installed"
                       : (st_.running || st_.failing) && st_.no_radio ? "No radio"
                       : st_.failing ? "Radio error"
                       : (starting_ || restarting_ || st_.running) ? "Starting..."
                                                                   : "Mesh off";
    label(panel, head, &zs_font_16, bad ? kBad : kText);
    std::string err = st_.error.empty() ? "The radio service keeps stopping. Power off, reseat the Cap LoRa-1262, try again."
                                        : st_.error;
    const char* text = !st_.installed ? "meshtasticd is missing: reinstall Mesh in Apps."
                       : st_.no_radio ? "Plug in the Cap LoRa-1262 with the power off, then turn the mesh on."
                       : st_.failing ? err.c_str()
                       : (starting_ || restarting_ || st_.running) ? "Powering the radio. A few seconds."
                                                                   : "Messages over LoRa radio, miles away, with no signal or internet.";
    lv_obj_t* h = label(panel, text, &zs_font_12, kMuted);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(h, 136);

    list_.build(root_, 162, 4, 152, kBodyH - 10);
    if (st_.installed) {
        if (!st_.running && !st_.failing && !starting_ && !restarting_) {
            list_.add(LV_SYMBOL_POWER, "Turn on mesh", "", [this] {
                start_mesh();
                shell_toast("Starting the radio...");
            });
        } else {
            list_.add(LV_SYMBOL_POWER, "Turn off mesh", "", [this] {
                stop_mesh();
                shell_toast("Mesh off");
            });
        }
    }
    list_.add(LV_SYMBOL_GPS, "GPS", "", [] { shell_open_app("maps", "gps"); });
    list_.finish();
}

// ---- First run: a region and a name, and you're on the air.

std::string MeshApp::short_from(const std::string& long_name) {
    // "Trail Runner" -> "TR", "Anna" -> "ANNA": initials of several words,
    // else the first four letters or digits.
    std::string initials, firsts;
    bool start = true;
    for (unsigned char ch : long_name) {
        if (std::isalnum(ch)) {
            if (start) initials.push_back(char(std::toupper(ch)));
            if (firsts.size() < 4) firsts.push_back(char(std::toupper(ch)));
            start = false;
        } else {
            start = true;
        }
    }
    if (initials.size() >= 2) return initials.substr(0, 4);
    return firsts.empty() ? "ZERO" : firsts;
}

void MeshApp::build_setup() {
    set_actions({hint("Select", "Enter"), hint("Not now", "Esc")});
    title_row("Set up your radio", "Nothing is sent until you start");
    list_.build(root_, 4, 17, kScreenW - 8, kBodyH - 18);
    list_.add(ZS_ICON_GLOBE, "Where you are", setup_region_ ? mesh::region_name(setup_region_) : "Choose", [this] { pick_region(true); });
    if (!setup_region_) list_.set_value_color(0, kWarn);
    list_.add(LV_SYMBOL_EDIT, "Your name", setup_name_.empty() ? "Choose" : mesh::one_line(setup_name_, 22), [this] {
        prompt("Your name on the mesh", "Trail Runner", setup_name_,
               "Others see this name, and a short one made from it. Up to 39 letters.", 39, [this](const std::string& s) {
                   if (!s.empty()) setup_name_ = s;
               });
    });
    list_.add(LV_SYMBOL_OK, "Start", setup_region_ ? "" : "Choose where you are first", [this] {
        if (!setup_region_) {
            pick_region(true);
            return;
        }
        mesh::Client& c = client();
        if (!c.ready()) {
            shell_toast("The radio isn't connected yet");
            return;
        }
        mesh::LoRaConfig l = c.lora();
        l.region = setup_region_;
        l.use_preset = true;
        l.tx_enabled = true;
        if (l.hop_limit == 0) l.hop_limit = 3;
        // One save, one restart.
        c.begin_edit();
        if (!setup_name_.empty()) c.set_owner(setup_name_, short_from(setup_name_));
        c.set_lora(l);
        c.commit_edit();
        shell_toast("Saved. The radio restarts: a few seconds");
        reset_to(Page::Main);
    });
    list_.finish();
    if (setup_region_) list_.focus(2);
}

// ---- Pick one of a list; type one line

void MeshApp::pick(const std::string& title, std::vector<Option> options, int focus, const std::string& note) {
    pick_title_ = title;
    pick_note_ = note;
    pick_options_ = std::move(options);
    pick_focus_ = focus;
    View v;
    v.page = Page::Pick;
    push(v);
}

void MeshApp::build_pick() {
    set_actions({hint("Choose", "Enter"), hint("Back", "Esc")});
    title_row(pick_title_, pick_note_);
    list_.build(root_, 4, 17, kScreenW - 8, kBodyH - 18);
    for (size_t i = 0; i < pick_options_.size(); ++i) {
        list_.add(nullptr, pick_options_[i].text, pick_options_[i].value, [this, i] {
            if (i >= pick_options_.size()) return;
            auto fn = pick_options_[i].choose;   // the page goes first: fn may open another
            pop();
            if (fn) fn();
        });
    }
    list_.finish();
    if (pick_focus_ >= 0 && pick_focus_ < int(pick_options_.size())) list_.focus(pick_focus_);
}

void MeshApp::prompt(const std::string& title, const std::string& placeholder, const std::string& initial,
                     const std::string& help, int max_len, std::function<void(const std::string&)> done) {
    prompt_title_ = title;
    prompt_placeholder_ = placeholder;
    prompt_initial_ = initial;
    prompt_help_ = help;
    prompt_max_ = max_len;
    prompt_done_ = std::move(done);
    View v;
    v.page = Page::Prompt;
    push(v);
}

void MeshApp::build_prompt() {
    set_actions({hint("OK", "Enter"), hint("Back", "Esc")});
    lv_obj_t* t = label(root_, prompt_title_.c_str(), &zs_font_14, kText);
    lv_obj_set_pos(t, 10, 10);
    ta_ = lv_textarea_create(root_);
    lv_textarea_set_one_line(ta_, true);
    lv_textarea_set_max_length(ta_, uint32_t(prompt_max_));
    lv_textarea_set_text(ta_, prompt_initial_.c_str());
    lv_textarea_set_placeholder_text(ta_, prompt_placeholder_.c_str());
    lv_obj_set_size(ta_, kScreenW - 20, 36);
    lv_obj_set_pos(ta_, 10, 38);
    kit::look::field(ta_);
    lv_obj_add_event_cb(ta_, [](lv_event_t* e) {
        auto* self = static_cast<MeshApp*>(lv_event_get_user_data(e));
        std::string text = lv_textarea_get_text(self->ta_);
        // Trim: a name with a space at the end isn't what anyone meant.
        while (!text.empty() && text.back() == ' ') text.pop_back();
        while (!text.empty() && text.front() == ' ') text.erase(0, 1);
        auto done = self->prompt_done_;
        self->pop();
        if (done) done(text);
    }, LV_EVENT_READY, this);
    if (!prompt_help_.empty()) {
        lv_obj_t* h = label(root_, prompt_help_.c_str(), &zs_font_12, kMuted);
        lv_obj_set_pos(h, 10, 82);
        lv_obj_set_width(h, kScreenW - 20);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    }
    shell_add_focus(ta_);
    lv_group_focus_obj(ta_);
    if (group) lv_group_set_editing(group, true);
}

// A page of text that Up and Down scroll.
void MeshApp::build_text_page(const std::string& title, const std::string& body) {
    title_row(title);
    lv_obj_t* panel = lv_obj_create(root_);
    style_panel(panel);
    lv_obj_set_pos(panel, 4, 17);
    lv_obj_set_size(panel, kScreenW - 8, kBodyH - 19);
    lv_obj_set_style_pad_all(panel, 5, 0);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    live_label_ = label(panel, body.c_str(), &zs_font_12, kText);
    lv_label_set_long_mode(live_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(live_label_, kScreenW - 26);
}

void MeshApp::write_daemon_conf() {
    if (simulated()) return;
    std::string dir = home_dir() + "/.config/zeroos";
    std::string text = "# Written by Mesh (Radio > Share my position); read by zeroos-meshd.\n";
    text += std::string("share_position=") + (share_level() > 0 ? "1" : "0") + "\n";
    write_file_atomic(dir + "/mesh.conf", text, 0600);
}

std::unique_ptr<App> make_mesh_app() { return std::make_unique<MeshApp>(); }

}  // namespace zs
