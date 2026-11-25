// AI: a pocket AI assistant. Chat with Claude, or with ChatGPT, Gemini,
// OpenRouter, Groq, Mistral, an Ollama server on your PC, or any
// OpenAI-compatible server. Bring your own API key. See docs/AI.md.
//
//   Chat page      a Text screen (docs/UX.md in the OS): type, Enter sends.
//                  Up/Down scroll the conversation; Esc while it's thinking
//                  stops the request. The Tab menu has a new chat, the
//                  model, saving to Notes and the settings.
//   Settings       a list: provider, API key, model, server, personality.
//                  Esc: back to the chat.
//
// The request runs on a worker thread (curl), so the screen never freezes;
// leaving the app stops it.
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "apps/common.h"
#include "apps/ai/chat.h"
#include "ui/fileio.h"
#include "ui/list_view.h"
#include "apps/kit/look.h"

namespace zs {
namespace {
using namespace theme;

// The conversation outlives the app screen: leave and come back to it.
std::vector<ai::Message>& history() {
    static std::vector<ai::Message> h;
    return h;
}

// Keys saved under the app's old name (ZClaw) move to the new file once.
std::string conf_path() {
    std::string dir = home_dir() + "/.config/zeroos";
    std::string path = dir + "/ai.conf";
    if (access(path.c_str(), F_OK) != 0) rename((dir + "/zclaw.conf").c_str(), path.c_str());
    return path;
}

// Shared with the worker thread.
struct Job {
    std::mutex m;
    bool done = false, ok = false;
    std::string reply, error;
    std::atomic<int> pid{0};
};

class AiApp : public App {
public:
    ~AiApp() override {
        lv_async_call_cancel(rebuild_cb, this);
        stop();
    }

    void build(lv_obj_t* root) override {
        root_ = root;
        cfg_ = ai::load(conf_path());
        const ai::Provider* p = ai::find_provider(cfg_.provider);
        // First run: straight to settings to pick a provider and add a key.
        if (p && p->needs_key && cfg_.key.empty() && history().empty()) page_ = Page::Settings;
        rebuild();
    }

    void tick() override {
        if (!job_) return;
        bool done;
        {
            std::lock_guard<std::mutex> l(job_->m);
            done = job_->done;
        }
        if (!done) {
            if (thinking_ && ++dots_ % 5 == 0) {
                static const char* const d[] = {"Thinking", "Thinking.", "Thinking..", "Thinking..."};
                lv_label_set_text(thinking_, d[(dots_ / 5) % 4]);
            }
            return;
        }
        if (worker_.joinable()) worker_.join();
        if (job_->ok) {
            history().push_back({false, job_->reply});
        } else {
            // The question stays, so pressing Enter on an empty box retries it.
            shell_toast(job_->error);
            failed_ = true;
        }
        job_.reset();
        if (page_ == Page::Chat) {
            fill_messages();
            set_chat_actions();   // Esc no longer stops; Try again may apply
        }
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;
        if (page_ == Page::Chat) {
            if (chat_ && (key == LV_KEY_UP || key == LV_KEY_DOWN)) {
                lv_obj_scroll_by_bounded(chat_, 0, key == LV_KEY_UP ? 48 : -48, LV_ANIM_OFF);
                return true;
            }
            return false;
        }
        return list_.key(key);   // Settings and Providers: typing filters
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (page_ == Page::Providers) {
            page_ = Page::Settings;
            defer_rebuild();
            return true;
        }
        if (page_ == Page::Settings) {
            page_ = Page::Chat;
            defer_rebuild();
            return true;
        }
        if (job_) {
            stop();
            shell_toast("Stopped");
            fill_messages();
            set_chat_actions();
            return true;
        }
        return false;
    }

private:
    enum class Page { Chat, Settings, Providers };

    void defer_rebuild() { lv_async_call(rebuild_cb, this); }
    static void rebuild_cb(void* p) { static_cast<AiApp*>(p)->rebuild(); }

    void rebuild() {
        if (ta_) draft_ = lv_textarea_get_text(ta_);
        if (group) lv_group_set_editing(group, false);
        lv_obj_clean(root_);
        chat_ = ta_ = thinking_ = input_ = nullptr;
        list_.reset();
        if (group) lv_group_remove_all_objs(group);
        switch (page_) {
            case Page::Chat: build_chat(); break;
            case Page::Settings: build_settings(); break;
            case Page::Providers: build_providers(); break;
        }
    }

    // ---- chat ----------------------------------------------------------------

    std::string provider_name() const {
        const ai::Provider* p = ai::find_provider(cfg_.provider);
        return p ? p->name : "?";
    }

    void build_chat() {
        lv_obj_t* head = label(root_, (provider_name() + "  |  " + ai::effective_model(cfg_)).c_str(),
                               &zs_font_12, kMuted);
        lv_obj_set_pos(head, 8, 2);
        lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
        lv_obj_set_width(head, kScreenW - 16);
        set_chat_actions();

        chat_ = lv_obj_create(root_);
        style_panel(chat_);
        lv_obj_set_pos(chat_, 4, 18);
        lv_obj_set_size(chat_, kScreenW - 8, kBodyH - 58);
        lv_obj_set_flex_flow(chat_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(chat_, 4, 0);
        lv_obj_set_style_pad_row(chat_, 4, 0);
        fill_messages();

        ta_ = lv_textarea_create(root_);
        look::field(ta_);
        lv_textarea_set_one_line(ta_, true);
        lv_textarea_set_placeholder_text(ta_, "Ask anything");
        lv_textarea_set_max_length(ta_, 2000);
        lv_textarea_set_text(ta_, draft_.c_str());
        lv_obj_set_width(ta_, kScreenW - 8);
        lv_obj_set_pos(ta_, 4, kBodyH - 37);
        lv_obj_add_event_cb(ta_, on_send, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta_);
        lv_group_focus_obj(ta_);
        lv_group_set_editing(group, true);
    }

    // The chat's actions. Enter and Esc are the text box's and the shell's;
    // the rest are in the menu.
    void set_chat_actions() {
        if (page_ != Page::Chat || input_) return;
        std::vector<Action> a;
        a.push_back({"Send", 0, "Enter", nullptr});
        if (job_) a.push_back({"Stop", 0, "Esc", nullptr});
        a.push_back({"Scroll", 0, LV_SYMBOL_UP LV_SYMBOL_DOWN, nullptr});
        a.push_back({"New chat", 'n', nullptr, [this] { new_chat(); }});
        Action again{"Try again", 't', nullptr, [this] { retry(); }};
        again.enabled = can_retry();
        a.push_back(again);
        a.push_back({"Model", 'm', nullptr, [this] { ask_model(); }});
        a.push_back({"Save chat to Notes", 's', nullptr, [this] { save_to_notes(); }});
        a.push_back({"Settings", 'p', nullptr, [this] {
                         page_ = Page::Settings;
                         defer_rebuild();
                     }});
        shell_set_actions(std::move(a));
    }

    // The last question went unanswered (and no request is running).
    bool can_retry() const { return !job_ && failed_ && !history().empty() && history().back().user; }
    void retry() {
        if (can_retry()) send();
    }

    void new_chat() {
        stop();
        history().clear();
        failed_ = false;
        page_ = Page::Chat;
        defer_rebuild();
        shell_toast("New chat");
    }

    void ask_model() {
        ask("Model (empty: the default)", ai::effective_model(cfg_), false, [this](const std::string& v) {
            cfg_.model = v;
            store("Model saved");
        });
    }

    static void on_send(lv_event_t* e) {
        auto* self = static_cast<AiApp*>(lv_event_get_user_data(e));
        std::string text = lv_textarea_get_text(self->ta_);
        if (self->job_) {
            shell_toast("Still thinking");
            return;
        }
        bool blank = text.find_first_not_of(" \t") == std::string::npos;
        if (blank) {
            // Enter on an empty box after a failure: ask again.
            self->retry();
            return;
        }
        // A question that failed and wasn't retried is replaced, so the
        // conversation keeps taking turns.
        if (!history().empty() && history().back().user) history().pop_back();
        history().push_back({true, text});
        lv_textarea_set_text(self->ta_, "");
        self->draft_.clear();
        self->send();
    }

    // The provider needs a key and none is set: sending can only fail.
    bool key_missing() const {
        const ai::Provider* p = ai::find_provider(cfg_.provider);
        return p && p->needs_key && cfg_.key.empty();
    }

    void send() {
        if (key_missing()) {   // say what's wrong and go where it's fixed
            failed_ = true;
            shell_toast("Add your API key first");
            page_ = Page::Settings;
            defer_rebuild();
            return;
        }
        failed_ = false;
        job_ = std::make_shared<Job>();
        auto job = job_;
        ai::Settings cfg = cfg_;
        std::vector<ai::Message> h = history();
        auto run = [job, cfg, h] {
            std::string reply, error;
            bool ok = ai::chat(cfg, h, reply, error, &job->pid);
            std::lock_guard<std::mutex> l(job->m);
            job->ok = ok;
            job->reply = std::move(reply);
            job->error = std::move(error);
            job->done = true;
        };
#ifdef __EMSCRIPTEN__
        run();   // the browser demo has no threads; it answers at once
#else
        worker_ = std::thread(run);
#endif
        fill_messages();
        set_chat_actions();
    }

    // Kill curl and wait for the worker: it must be gone before the app
    // (and its module) is.
    void stop() {
        if (job_) {
            if (int pid = job_->pid.load()) kill(pid, SIGTERM);
        }
        if (worker_.joinable()) worker_.join();
        if (job_ && !history().empty() && history().back().user) failed_ = true;
        job_.reset();
    }

    void fill_messages() {
        if (!chat_) return;
        lv_obj_clean(chat_);
        thinking_ = nullptr;
        auto& h = history();
        size_t first = h.size() > 30 ? h.size() - 30 : 0;   // keep the screen light
        if (h.empty()) {
            lv_obj_t* l = label(chat_, "Ask me anything: facts, ideas, code, a quick translation...",
                                &zs_font_12, kMuted);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, kScreenW - 24);
        }
        for (size_t i = first; i < h.size(); ++i) {
            lv_obj_t* l = lv_label_create(chat_);
            lv_label_set_text(l, ((h[i].user ? "You: " : "") + h[i].text).c_str());
            lv_obj_set_style_text_font(l, &zs_font_12, 0);
            lv_obj_set_style_text_color(l, c(h[i].user ? kMuted : kText), 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, kScreenW - 24);
        }
        if (job_) {
            thinking_ = label(chat_, "Thinking", &zs_font_12, kMuted);
        } else if (failed_ && !h.empty() && h.back().user) {
            lv_obj_t* l = label(chat_,
                                key_missing() ? "Not answered: no API key. Add one in Settings (P), then press Enter."
                                              : "Not answered. Send it again with Try again in the menu.",
                                &zs_font_12, kBad);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, kScreenW - 24);
        }
        lv_obj_update_layout(chat_);
        lv_obj_scroll_to_y(chat_, LV_COORD_MAX, LV_ANIM_OFF);
    }

    // ---- settings ------------------------------------------------------------

    void build_settings() {
        const ai::Provider* p = ai::find_provider(cfg_.provider);
        lv_obj_t* t = label(root_, LV_SYMBOL_SETTINGS " AI settings", &zs_font_12, kAccent);
        lv_obj_set_pos(t, 8, 2);
        lv_obj_t* hint = label(root_, p ? p->key_hint : "", &zs_font_12, kMuted);
        lv_obj_set_pos(hint, 8, kBodyH - 16);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_DOT);
        lv_obj_set_width(hint, kScreenW - 16);
        set_settings_actions();

        list_.build(root_, 4, 18, kScreenW - 8, kBodyH - 36);
        std::string key = cfg_.key.empty()
                              ? std::string(p && !p->needs_key ? "not needed" : "not set")
                              : "set, ends " + (cfg_.key.size() > 4 ? cfg_.key.substr(cfg_.key.size() - 4) : "");
        std::string url = cfg_.url.empty() ? "default" : cfg_.url;
        if (p && !*p->url && cfg_.url.empty()) url = "not set";
        list_.add(LV_SYMBOL_LIST, "Provider", provider_name(), [this] {
            page_ = Page::Providers;
            rebuild();
        });
        list_.add(LV_SYMBOL_KEYBOARD, "API key", key, [this] {
            ask("API key (paste or type it; empty removes it)", "", true, [this](const std::string& v) {
                cfg_.key = v;
                store("Key saved");
            });
        });
        list_.add(LV_SYMBOL_EDIT, "Model", ai::effective_model(cfg_), [this] { ask_model(); });
        list_.add(LV_SYMBOL_WIFI, "Server", url, [this] {
            ask("Server address (empty: the default)", cfg_.url, false, [this](const std::string& v) {
                cfg_.url = ai::normalize_url(cfg_.provider, v);
                store("Server saved");
            });
        });
        list_.add(LV_SYMBOL_EDIT, "Personality", cfg_.system.empty() ? "default" : "your own", [this] {
            ask("How AI should answer (empty: the default)", cfg_.system, false, [this](const std::string& v) {
                cfg_.system = v;
                store("Saved");
            });
        });
        list_.finish();
    }

    // Settings is a List screen: its other actions are in the menu.
    void set_settings_actions() {
        std::vector<Action> a;
        a.push_back({"Change", 0, "Enter", nullptr});
        a.push_back({"Chat", 0, "Esc", nullptr});
        a.push_back({"New chat", 'n', nullptr, [this] { new_chat(); }});
        a.push_back({"Save chat to Notes", 's', nullptr, [this] { save_to_notes(); }});
        shell_set_actions(std::move(a));
    }

    void build_providers() {
        lv_obj_t* t = label(root_, "Pick an AI provider", &zs_font_12, kAccent);
        lv_obj_set_pos(t, 8, 2);
        shell_set_actions({{"Use", 0, "Enter", nullptr}, {"Back", 0, "Esc", nullptr}});
        list_.build(root_, 4, 18, kScreenW - 8, kBodyH - 22);
        const auto& all = ai::providers();
        int current = -1;
        for (size_t i = 0; i < all.size(); ++i) {
            bool on = cfg_.provider == all[i].id;
            int row = list_.add(on ? LV_SYMBOL_OK : LV_SYMBOL_RIGHT, all[i].name, on ? "in use" : "", [this, i] {
                stop();
                ai::save(conf_path(), cfg_);
                ai::switch_provider(conf_path(), cfg_, ai::providers()[i].id);
                page_ = Page::Settings;
                store(std::string("Using ") + ai::providers()[i].name);
            });
            if (on) current = row;
        }
        list_.finish();
        if (current >= 0) list_.focus(current);
    }

    void store(const std::string& msg) {
        if (ai::save(conf_path(), cfg_)) shell_toast(msg);
        else shell_toast("Couldn't save the settings");
        defer_rebuild();   // the chat's header shows the model too
    }

    void save_to_notes() {
        if (history().empty()) {
            shell_toast("Nothing to save yet");
            return;
        }
        std::string dir = home_dir() + "/Notes";
        mkdir(dir.c_str(), 0755);
        char stamp[32];
        std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M", std::localtime(&now));
        std::string path = dir + "/AI " + stamp + ".txt";
        std::string out = "AI chat, " + provider_name() + " (" + ai::effective_model(cfg_) + ")\n\n";
        for (auto& m : history()) out += (m.user ? "You: " : "AI: ") + m.text + "\n\n";
        shell_toast(write_file_atomic(path, out) ? "Saved to Notes/AI " + std::string(stamp) + ".txt"
                                                 : "Couldn't save (card full?)");
    }

    // ---- one-line text box over the screen ------------------------------------

    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, bool password, Done done) {
        done_ = std::move(done);
        // A Text screen while it's open: the bar says so.
        shell_set_actions({{"OK", 0, "Enter", nullptr}, {"Cancel", 0, "Esc", nullptr}});
        input_ = lv_obj_create(root_);
        look::raised(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        lv_obj_remove_flag(input_, LV_OBJ_FLAG_SCROLLABLE);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = lv_textarea_create(input_);
        look::field(ta);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_password_mode(ta, password);
        lv_textarea_set_max_length(ta, 1000);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<AiApp*>(lv_event_get_user_data(e));
            std::string v = lv_textarea_get_text(lv_event_get_target_obj(e));
            Done d = std::move(self->done_);
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
            self->close_input();
            if (d) d(v);
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    void close_input() {
        if (!input_) return;
        lv_group_set_editing(group, false);
        lv_obj_t* in = input_;
        input_ = nullptr;
        lv_obj_delete_async(in);
        switch (page_) {
            case Page::Chat:
                set_chat_actions();
                if (ta_) {
                    lv_group_focus_obj(ta_);
                    lv_group_set_editing(group, true);
                }
                break;
            case Page::Settings:
                set_settings_actions();
                if (list_.focused() >= 0) list_.focus(list_.focused());
                break;
            case Page::Providers: break;
        }
    }

    lv_obj_t* root_ = nullptr;
    lv_obj_t* chat_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    lv_obj_t* thinking_ = nullptr;
    lv_obj_t* input_ = nullptr;
    ListView list_;
    Page page_ = Page::Chat;
    ai::Settings cfg_;
    std::string draft_;
    std::shared_ptr<Job> job_;
    std::thread worker_;
    bool failed_ = false;
    int dots_ = 0;
    Done done_;
};
}  // namespace

std::unique_ptr<App> make_ai_app() { return std::make_unique<AiApp>(); }

}  // namespace zs
