// One app that holds several tools (Net Tools, RF Tools): a numbered menu of
// the tools, and the chosen tool running inside the same app. Each tool is an
// ordinary App; the suite builds it in its own root, passes it the keys and
// ticks, and takes it down again when Esc leaves it.
//
//   std::unique_ptr<App> make_net_app() {
//       return std::make_unique<kit::Suite>("net", std::vector<kit::Tool>{
//           {"discover", "Discover", ZS_ICON_SEARCH, "Devices on the network", make_discover_app},
//           ...});
//   }
//
// A tool opens another (Discover hands a host to Ports) with
// kit::suite_open("ports", ip), and reads what it was handed with
// kit::tool_arg() in build(). Esc at that tool's first screen goes back to
// the tool that opened it, and from there to the menu.
//
// The suite can be opened straight at a tool: shell_open_app("net", "ports")
// or ("net", "ports:192.168.1.10").
//
// Header-only, so a pack that uses it needs nothing added to its SOURCES.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "apps/common.h"
#include "ui/list_view.h"

namespace zs::kit {

struct Tool {
    const char* id;              // "ports"
    const char* name;            // "Ports"
    const char* symbol;          // LV_SYMBOL_* / ZS_ICON_*
    const char* about;           // one short line, shown in the menu
    std::unique_ptr<App> (*make)();
};

class Suite;

namespace detail {
inline Suite* g_suite = nullptr;        // the suite that is open
inline std::string g_tool_arg;          // handed to the tool being built
inline std::map<std::string, int> g_last;   // per suite: the menu row last opened
}  // namespace detail

// What the tool being built was handed (a host, a frequency), or "". Outside
// a suite it is the shell's word for the app (shell_app_arg()).
inline std::string tool_arg() {
    if (!detail::g_suite) return shell_app_arg();
    return detail::g_tool_arg;
}

void suite_open(const std::string& tool, const std::string& arg = "");

class Suite : public App {
public:
    Suite(std::string id, std::vector<Tool> tools) : id_(std::move(id)), tools_(std::move(tools)) {}

    ~Suite() override {
        close_tool();
        if (detail::g_suite == this) detail::g_suite = nullptr;
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        detail::g_suite = this;
        // "ports" or "ports:192.168.1.10": straight to that tool.
        std::string a = shell_app_arg();
        size_t colon = a.find(':');
        std::string tool = a.substr(0, colon);
        if (index_of(tool) >= 0) {
            open_tool(index_of(tool), colon == std::string::npos ? "" : a.substr(colon + 1));
            return;
        }
        show_menu();
    }

    void tick() override {
        apply_pending();
        if (tool_) tool_->tick();
        apply_pending();
    }

    bool on_key(uint32_t key) override {
        bool used = tool_ ? tool_->on_key(key) : list_.key(key);
        apply_pending();
        return used;
    }

    bool on_back() override {
        if (!tool_) return false;   // the menu: leave the app
        if (tool_->on_back()) {
            apply_pending();
            return true;
        }
        // Back to the tool that opened this one, else to the menu.
        if (!back_.empty()) {
            auto [i, arg] = back_.back();
            back_.pop_back();
            open_tool(i, arg, /*remember=*/false);
        } else {
            show_menu();
        }
        return true;
    }

    // Open a tool by id (deferred to after the calling key or tick returns:
    // the calling tool is destroyed by the switch).
    void request(const std::string& id, const std::string& arg) {
        pending_ = index_of(id);
        pending_arg_ = arg;
    }

private:
    int index_of(const std::string& id) const {
        for (size_t i = 0; i < tools_.size(); ++i)
            if (id == tools_[i].id) return int(i);
        return -1;
    }

    void apply_pending() {
        if (pending_ < 0) return;
        int i = pending_;
        pending_ = -1;
        open_tool(i, pending_arg_, /*remember=*/tool_ != nullptr);
    }

    // Take the open tool (or the menu) off the screen. The tool goes first,
    // while its widgets still exist: destructors read them and stop timers.
    // Its widgets leave the focus group before that: deleting the focused
    // row would otherwise focus the next one, and a FOCUSED handler would
    // run with the tool already destroyed (RF Tools' Station did, a
    // use-after-free that crashed the screenshot run).
    void close_tool() {
        if (group) lv_group_remove_all_objs(group);
        tool_.reset();
        list_.reset();
        if (root_) lv_obj_clean(root_);
        shell_set_fn_layer(false);
        shell_set_actions({});
    }

    void show_menu() {
        close_tool();
        back_.clear();
        current_ = -1;
        list_.build(root_, 4, 4, theme::kScreenW - 8, theme::kBodyH - 8);
        for (size_t i = 0; i < tools_.size(); ++i)
            list_.add(tools_[i].symbol, tools_[i].name, tools_[i].about, [this, i] { request_index(int(i)); });
        list_.finish();
        auto it = detail::g_last.find(id_);
        if (it != detail::g_last.end() && it->second < list_.size()) list_.focus(it->second);
        Action open, close;   // hints: the list and the shell handle the keys
        open.label = "Open";
        open.key = "Enter";
        close.label = "Close";
        close.key = "Esc";
        shell_set_actions({open, close});
    }

    void request_index(int i) {
        pending_ = i;
        pending_arg_.clear();
    }

    void open_tool(int i, const std::string& arg, bool remember = false) {
        if (remember && current_ >= 0) back_.push_back({current_, current_arg_});
        close_tool();
        detail::g_last[id_] = i;
        current_ = i;
        current_arg_ = arg;
        tool_ = tools_[size_t(i)].make();
        tool_->group = group;
        detail::g_tool_arg = arg;
        tool_->build(root_);
        detail::g_tool_arg.clear();
    }

    std::string id_;
    std::vector<Tool> tools_;
    lv_obj_t* root_ = nullptr;
    ListView list_;
    std::unique_ptr<App> tool_;
    int current_ = -1;
    std::string current_arg_;
    std::vector<std::pair<int, std::string>> back_;   // tools to go back to
    int pending_ = -1;
    std::string pending_arg_;
};

inline void suite_open(const std::string& tool, const std::string& arg) {
    if (detail::g_suite) detail::g_suite->request(tool, arg);
    else shell_open_app(tool, arg);
}

}  // namespace zs::kit
