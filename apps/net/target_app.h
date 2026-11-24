// Shared shape for the Net Tools apps that take a target and stream one
// program's output: Ports (nmap), Trace (mtr), DNS (dig). A subclass fills in
// the title, how to build the command and its simulated output, and gets the
// input screen, the scrolling output, R to re-run and Esc to go back for free.
#pragma once

#include <string>
#include <vector>

#include "apps/common.h"
#include "net_run.h"
#include "apps/kit/suite.h"
#include "apps/kit/ui.h"
#include "apps/kit/look.h"

namespace zs {

class TargetApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        std::string arg = kit::tool_arg();   // a host handed over by Discover
        if (!arg.empty()) {
            target_ = arg;
            show_output();
        } else {
            show_input();
        }
    }

    void tick() override {
        if (pending_run_) {
            pending_run_ = false;
            show_output();
            return;
        }
        if (!running_ || !job_.started()) return;
        std::vector<std::string> lines;
        if (job_.drain(lines)) for (auto& l : lines) out_.append(l);
        if (!job_.running()) {
            running_ = false;
            finished();
        }
    }

    bool on_key(uint32_t key) override {
        if (!have_output_) return extra_key(key);
        if (key == 'r' || key == 'R') { rerun(); return true; }
        return extra_key(key);
    }

    bool on_back() override {
        if (have_output_) { show_input(); return true; }
        return false;
    }

protected:
    // What a subclass provides.
    virtual const char* title() const = 0;
    virtual const char* input_title() const = 0;
    virtual const char* placeholder() const { return "host or address"; }
    virtual std::vector<std::string> command(const std::string& target) const = 0;
    virtual std::vector<std::string> sim(const std::string& target) const = 0;
    virtual const char* waiting_text() const { return "Working..."; }
    // Optional extra keys (e.g. DNS record type). Return true if handled.
    virtual bool extra_key(uint32_t) { return false; }
    // Those keys, for the soft-key bar and the menu.
    virtual void extra_actions(std::vector<Action>&) {}
    // Called when the job ends (subclass may inspect job_.status()).
    virtual void finished() {}

    void rerun() {
        if (target_.empty()) { show_input(); return; }
        show_output();
    }

    const std::string& target() const { return target_; }
    net::Job& job() { return job_; }
    kit::OutputView& out() { return out_; }
    lv_obj_t* header_label() { return header_; }

    void show_input() {
        job_.cancel();
        running_ = false;
        have_output_ = false;
        lv_obj_clean(root_);
        if (group) lv_group_remove_all_objs(group);
        kit::input_screen(root_, group, input_title(), placeholder(), target_,
                          [this](const std::string& v) {
                              std::string t = v;
                              while (!t.empty() && (t.front() == ' ')) t.erase(t.begin());
                              while (!t.empty() && (t.back() == ' ')) t.pop_back();
                              if (t.empty()) return;
                              target_ = t;
                              pending_run_ = true;   // rebuild on the next tick
                          },
                          nullptr, "Run");
    }

    void show_output() {
        job_.cancel();
        job_.reset();
        lv_obj_clean(root_);
        if (group) lv_group_remove_all_objs(group);
        have_output_ = true;
        // A Control screen: R is its own key, and Esc goes back to the prompt.
        // Set before building: the output fills the area above the bar.
        std::vector<Action> a;
        a.push_back({"Run again", 'r', "R", [this] { rerun(); }});
        extra_actions(a);
        a.push_back({"New target", 0, "Esc", nullptr});
        shell_set_actions(std::move(a));

        header_ = theme::label(root_, "", &zs_font_14, kit::look::kAccent);
        lv_obj_set_pos(header_, 4, 1);
        lv_label_set_text(header_, fmt("%s  %s", title(), target_.c_str()).c_str());

        out_.build(root_, 18);
        out_.set_placeholder(waiting_text());

        running_ = true;
        job_.start(command(target_), sim(target_));
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* header_ = nullptr;
    kit::OutputView out_;
    net::Job job_;
    std::string target_;
    bool running_ = false;
    bool have_output_ = false;
    bool pending_run_ = false;
};

}  // namespace zs
