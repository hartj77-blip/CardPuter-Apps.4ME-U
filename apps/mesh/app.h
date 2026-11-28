// Mesh: the app. One class, its screens in four files:
//   mesh.cpp    the app itself: what's on screen, the radio's state, the
//               first-run setup, the pieces every screen shares
//   chats.cpp   the Chats tab, a chat, quick replies, searching
//   nodes.cpp   the Nodes tab, a node's page, traceroute, the Map tab
//   radio.cpp   the Radio tab: settings, channels, sharing, the activity log
// What it knows about the mesh is in core/ (no screen there).
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "apps/common.h"
#include "apps/kit/look.h"
#include "apps/mesh/core/session.h"
#include "ui/list_view.h"

namespace zs {

// The names the screens use, in one place: sizes and helpers from the OS's
// theme, colours from the apps' look (apps/kit/look.h).
namespace mesh_ui {
using kit::look::kAccent;
using kit::look::kBad;
using kit::look::kBg;
using kit::look::kBorder;
using kit::look::kDisabled;
using kit::look::kFocus;
using kit::look::kGood;
using kit::look::kInfo;
using kit::look::kMuted;
using kit::look::kOnFocus;
using kit::look::kSurface;
using kit::look::kSurface2;
using kit::look::kText;
using kit::look::kWarn;
using kit::look::style_panel;
using theme::c;
using theme::kBodyH;
using theme::kContentH;
using theme::kScreenW;
using theme::label;
using theme::style_plain;
inline lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }   // c(), where a local is called c
}  // namespace mesh_ui

class MeshApp : public App {
public:
    MeshApp();
    ~MeshApp() override;
    void build(lv_obj_t* root) override;
    void tick() override;
    bool on_key(uint32_t key) override;
    bool on_back() override;

private:
    enum class Page {
        Off,        // the mesh isn't running: turn it on, or what's wrong
        Setup,      // first run: region and name
        Main,       // the four tabs
        Chat,
        Quick,      // quick replies
        React,
        MessageInfo,
        Search,
        NodePage,
        Trace,
        Channels,
        ChannelPage,
        ShowQr,
        Scan,
        Log,
        About,
        Pick,       // choose one of a list
        Prompt,     // type one line
    };
    enum Tab { kChats, kNodes, kMap, kRadio, kTabs };
    struct View {
        Page page = Page::Main;
        std::string key;        // a chat's key, a link, a title
        uint32_t num = 0;       // a node, a message id
        int index = 0;          // a channel
    };
    struct Option {
        std::string text, value;
        std::function<void()> choose;
    };

    // ---- mesh.cpp
    mesh::Session& session();
    mesh::Client& client() { return session().client(); }
    long now() const;                       // unix seconds
    long now_ms() const;
    void push(const View& v);
    void pop();
    void replace_top(const View& v);
    void reset_to(Page p);
    void rebuild();
    void clear_screen();
    void poll_radio();
    void pump();
    void set_actions(std::vector<Action> a);
    static Action hint(const char* label, const char* key);
    static Action menu(const char* label, char letter, std::function<void()> run, bool enabled = true);
    static Action danger(const char* label, char letter, const std::string& confirm, std::function<void()> run);
    lv_obj_t* title_row(const std::string& title, const std::string& note = "", uint32_t note_color = 0);
    void build_tabs();
    void switch_tab(int tab);
    void build_off();
    void build_setup();
    void start_mesh();
    void stop_mesh();
    void restart_mesh_service();
    void pick(const std::string& title, std::vector<Option> options, int focus = -1, const std::string& note = "");
    void build_pick();
    void prompt(const std::string& title, const std::string& placeholder, const std::string& initial,
                const std::string& help, int max_len, std::function<void(const std::string&)> done);
    void build_prompt();
    void build_text_page(const std::string& title, const std::string& body);
    std::string status_line();              // "Connecting...", "No region", ""
    uint32_t status_color();
    void use_gps();
    void write_daemon_conf();
    static std::string short_from(const std::string& long_name);

    // ---- chats.cpp
    void build_chats_tab();
    void refresh_chats_tab();
    std::string chat_row_value(const mesh::Chat& c);
    void open_chat(const std::string& key);
    void build_chat();
    void fill_chat();
    void update_chat_head();
    void send_from_box();
    bool chat_key(uint32_t key);
    void select_message(int index);
    const mesh::Message* selected_message();
    void build_quick();
    std::vector<std::string> quick_replies();
    void save_quick(const std::vector<std::string>& q);
    void build_react();
    void build_message_info();
    void build_search();
    void export_chat(const std::string& key);
    void delete_chat(const std::string& key);
    void send_waypoint();
    void set_chat_actions();
    void new_message();
    std::string focus_key_;                 // the chat to put the list's focus on

    // ---- nodes.cpp
    void build_nodes_tab();
    void refresh_nodes_tab();
    std::string node_row_text(const mesh::Node& n);
    std::string node_row_value(const mesh::Node& n);
    std::vector<uint32_t> sorted_nodes();
    void build_node_page();
    void refresh_node_page();
    void node_rows(const mesh::Node& n, std::vector<std::pair<std::string, std::string>>& rows);
    void run_traceroute(uint32_t node);
    void map_collect(double& lat0, double& lon0, bool& have_me);
    void build_trace();
    void build_map_tab();
    void draw_map();
    bool map_key(uint32_t key);

    // ---- radio.cpp
    void build_radio_tab();
    void refresh_radio_tab();
    void build_channels();
    void build_channel_page();
    void build_show_qr();
    void build_scan();
    void scan_tick();
    void join_from_link(const std::string& link);
    void build_log();
    void refresh_log();
    void build_about();
    void pick_region(bool setup);
    void pick_preset();
    void pick_role();
    void pick_hops();
    void edit_name();
    void set_share_level(int level);
    int share_level();
    bool lan_on();
    void set_lan(bool on);
    void new_channel();
    void saved_note(bool restarts);
    void show_qr(const std::string& title, const std::string& note, const std::string& text);
    std::string about_text();
    std::string log_text();
    std::string qr_title_, qr_note_;
    bool scan_done_ = false;

    lv_obj_t* root_ = nullptr;
    std::vector<View> stack_;
    bool dirty_ = false;
    int n_ = 0;
    int tab_ = kChats;
    ListView list_;
    // The radio service, as ZeroShell sees it.
    MeshStatus st_;
    bool starting_ = false, restarting_ = false;
    long starting_at_ = 0, checked_daemon_at_ = 0;
    bool setup_offered_ = false, setup_skipped_ = false;
    std::string setup_name_;
    uint32_t setup_region_ = 0;
    // Counts the screens were last drawn at.
    int seen_chats_ = -1, seen_nodes_ = -1, seen_config_ = -1, seen_log_ = -1;
    std::string seen_sig_;
    // Tabs
    lv_obj_t* tab_labels_[kTabs] = {nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* tab_status_ = nullptr;
    // A chat
    lv_obj_t* chat_box_ = nullptr;
    lv_obj_t* chat_head_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    std::vector<lv_obj_t*> msg_objs_;
    std::vector<int> msg_index_;            // msg_objs_[i] is chat.msgs[msg_index_[i]]
    int sel_ = -1;                          // index into msg_objs_, -1 none
    int shown_from_ = 0;                    // first message drawn
    std::map<std::string, std::string> drafts_;
    uint32_t reply_to_ = 0;
    bool bell_ = false;
    // Nodes
    std::vector<uint32_t> node_order_;
    int node_sort_ = 0, node_filter_ = 0;
    // Map
    lv_obj_t* map_ = nullptr;
    lv_obj_t* map_note_ = nullptr;
    double map_range_ = 0;                  // metres from the middle to the edge; 0: fit everyone
    int map_sel_ = -1;
    std::vector<uint32_t> map_nodes_;
    // Pick, Prompt
    std::string pick_title_, pick_note_;
    std::vector<Option> pick_options_;
    int pick_focus_ = -1;
    std::string prompt_title_, prompt_placeholder_, prompt_initial_, prompt_help_;
    int prompt_max_ = 64;
    std::function<void(const std::string&)> prompt_done_;
    // Scan
    lv_obj_t* scan_img_ = nullptr;
    lv_obj_t* scan_note_ = nullptr;
    lv_image_dsc_t scan_dsc_{};
    std::vector<uint16_t> scan_pix_;
    uint32_t scan_seq_ = 0;
    int scan_frames_ = 0;
    bool scanning_ = false;
    // A node's page, the log, About: text that's brought up to date in place.
    lv_obj_t* live_label_ = nullptr;
    std::vector<int> value_rows_;
    // GPS
    long gps_at_ms_ = 0;
    bool gps_used_ = false;
};

}  // namespace zs
