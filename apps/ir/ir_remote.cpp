// IR Remote: a universal remote built on Flipper-IRDB (docs/IR.md).
//
//   My remotes   ~/IR/*.ir: saved from the database, learned from your own
//                remotes (L relearns a button, N adds one, D deletes), or
//                .ir files copied from a Flipper Zero
//   Universal    every Power (Input, Vol +, Mute, ...) code for TVs,
//                projectors, displays, screens, audio, players, set-top
//                boxes, conferencing, ACs, fans and lights, most common
//                first: Enter sends them one after another; press any key
//                when the device reacts, then O opens that brand's remotes
//   Browse       4,500 remotes by kind, brand and model; type to search
//
// The lists are List screens (docs/UX.md in the OS): typing filters them and
// their actions are in the Tab menu. A remote's keypad and the universal
// sender are Control screens: their letters are shortcuts, each one listed
// in the screen's actions so the soft-key bar shows it.
#include <algorithm>
#include <functional>

#include "apps/common.h"
#include "ir/db.h"
#include "platform/keys.h"
#include "ui/list_view.h"
#include "apps/kit/look.h"

namespace zs {
namespace {
using namespace theme;

std::string thousands(size_t n) {
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

bool printable(uint32_t key) { return key >= 32 && key < 127 && key != '`'; }
bool digit(uint32_t key) { return key >= '0' && key <= '9'; }

class IrApp : public App {
public:
    void build(lv_obj_t* root) override {
        root_ = root;
        db_ = ir::db_root();
        index_ = ir::load_index(db_);
        universals_ = ir::universal_names(db_);
        // The AV kinds first, in the order a room is set up; anything else after, as the OS sorts it.
        static const char* order[] = {"TVs", "Projectors", "Displays", "Screens", "Audio", "Players",
                                      "Set_top_boxes", "Conferencing", "ACs", "Fans", "Lights"};
        auto rank = [](const std::string& n) {
            int i = 0;
            for (const char* o : order) {
                if (n == o) return i;
                ++i;
            }
            return i;
        };
        std::stable_sort(universals_.begin(), universals_.end(),
                         [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
        show();
    }

    void tick() override {
        if (answered_) {   // a text box's answer, acted on outside its key event
            answered_ = false;
            Done d = std::move(done_);
            if (d) d(answer_);
            return;
        }
        if (page_.kind != Page::Universal || run_ != Run::Sending) return;
        // One code every 150 ms: time for the device to react, and for you
        // to press a key while the code that worked is still the last one.
        if (lv_tick_elaps(last_send_) < 150) return;
        last_send_ = lv_tick_get();
        auto& codes = uni_.codes[size_t(page_.focus)];
        if (pos_ >= int(codes.size())) {
            run_ = Run::Done;
            update_universal();
            return;
        }
        send(codes[size_t(pos_)]);
        update_universal();
        ++pos_;
    }

    bool on_key(uint32_t key) override {
        if (input_) return false;   // the text box has the keys
        switch (page_.kind) {
            case Page::Remote: return remote_key(key);
            case Page::Universal: return universal_key(key);
            case Page::Kinds:
                // The first Browse page searches every remote, not just its
                // own rows, so it keeps its own search.
                if (printable(key) && !digit(key) && (key != ' ' || !filter_.empty())) {
                    filter_ += char(key);
                    show();
                    return true;
                }
                if ((key == LV_KEY_BACKSPACE || key == LV_KEY_DEL) && !filter_.empty()) {
                    filter_.pop_back();
                    show();
                    return true;
                }
                return list_.key(key);
            default: return list_.key(key);   // typing filters the list
        }
    }

    bool on_back() override {
        if (input_) {
            close_input();
            return true;
        }
        if (page_.kind == Page::Universal && run_ == Run::Sending) {
            run_ = Run::Stopped;
            update_universal();
            return true;
        }
        if (!filter_.empty()) {
            filter_.clear();
            show();
            return true;
        }
        if (stack_.empty()) return false;
        page_ = stack_.back();
        stack_.pop_back();
        show();
        return true;
    }

private:
    enum class Page { Home, Kinds, Brands, Models, Remote, Universal };
    enum class Run { Idle, Sending, Stopped, Done };
    struct Nav {
        Page kind = Page::Home;
        std::string category, brand;
        std::string path;        // Remote: full path; Universal: kind name
        bool mine = false;       // Remote: in ~/IR, so it can be changed
        int focus = 0;           // focused row or button, restored on Back
    };

    // --- navigation -------------------------------------------------------
    static Nav nav(Page kind, std::string category = "", std::string brand = "", std::string path = "",
                   bool mine = false) {
        Nav n;
        n.kind = kind;
        n.category = std::move(category);
        n.brand = std::move(brand);
        n.path = std::move(path);
        n.mine = mine;
        return n;
    }

    void go(Nav next) {
        page_.focus = focused_index();
        stack_.push_back(page_);
        page_ = std::move(next);
        filter_.clear();
        show();
    }

    // The focused row, or button on a remote's keypad.
    int focused_index() {
        if (page_.kind != Page::Remote) return std::max(0, list_.focused());
        lv_obj_t* f = group ? lv_group_get_focused(group) : nullptr;
        for (size_t i = 0; i < focusables_.size(); ++i)
            if (focusables_[i] == f) return int(i);
        return 0;
    }

    void show() {
        run_ = Run::Idle;
        int want = page_.focus;   // building fires focus events that change it
        if (group) lv_group_set_editing(group, false);
        lv_obj_clean(root_);
        lv_group_remove_all_objs(group);
        list_.reset();
        focusables_.clear();
        info_ = status_ = bar_ = nullptr;
        input_ = ta_ = nullptr;   // deleted with the page
        switch (page_.kind) {
            case Page::Home: build_home(); break;
            case Page::Kinds: build_kinds(); break;
            case Page::Brands: build_brands(); break;
            case Page::Models: build_models(); break;
            case Page::Remote: build_remote(); break;
            case Page::Universal: build_universal(); break;
        }
        page_.focus = want;
        if (!focusables_.empty()) {
            int f = std::clamp(want, 0, int(focusables_.size()) - 1);
            lv_group_focus_obj(focusables_[size_t(f)]);
        } else if (list_.size() > 0) {
            list_.focus(std::clamp(want, 0, list_.size() - 1));
        }
        set_actions();
        if (page_.kind == Page::Remote) show_info(std::clamp(want, 0, std::max(0, int(sigs_.size()) - 1)));
        if (page_.kind == Page::Universal) update_universal();
    }

    // A header line and a list under it.
    void list_page(const std::string& title, const std::string& right) {
        lv_obj_t* t = label(root_, title.c_str(), &zs_font_12, kText);
        lv_obj_set_pos(t, 6, 3);
        lv_obj_set_width(t, right.empty() ? 308 : 200);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        if (!right.empty()) {
            lv_obj_t* r = label(root_, right.c_str(), &zs_font_12, kMuted);
            lv_obj_align(r, LV_ALIGN_TOP_RIGHT, -6, 3);
        }
        list_.build(root_, 2, 20, 316, kBodyH - 20);
    }

    void row(const char* symbol, const std::string& text, const std::string& value, std::function<void()> action) {
        list_.add(symbol, text, value, std::move(action));
    }

    // Each screen's actions: Enter's first, then the menu's.
    void set_actions() {
        if (input_) return;
        std::vector<Action> a;
        switch (page_.kind) {
            case Page::Home:
                a.push_back({"Open", 0, "Enter", nullptr});
                a.push_back({"New remote", 'n', nullptr, [this] { new_remote(); }});
                break;
            case Page::Kinds:
            case Page::Brands:
            case Page::Models:
                a.push_back({"Open", 0, "Enter", nullptr});
                a.push_back({"Back", 0, "Esc", nullptr});
                break;
            case Page::Remote: {
                bool any = !sigs_.empty();
                a.push_back({"Send", 0, "Enter", nullptr});
                if (page_.mine) {
                    Action learn_it{"Learn", 'l', "L", [this] { learn_focused(); }};
                    learn_it.enabled = any;
                    a.push_back(learn_it);
                    a.push_back({"New button", 'n', "N", [this] { new_button(); }});
                    Action del{"Delete", 'd', "D", [this] { delete_button(); }};
                    del.enabled = any;
                    del.danger = true;
                    del.confirm = "Delete this button?";
                    a.push_back(del);
                } else {
                    a.push_back({"Save to My remotes", 's', "S", [this] { save_copy(); }});
                }
                break;
            }
            case Page::Universal:
                switch (run_) {
                    case Run::Idle:
                    case Run::Done: a.push_back({"Send all", 0, "Enter", nullptr}); break;
                    case Run::Sending: a.push_back({"Stop", 0, "Any key", nullptr}); break;
                    case Run::Stopped:
                        a.push_back({"Send again", 0, "Enter", nullptr});
                        a.push_back({"Try nearby", 0, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT, nullptr});
                        a.push_back({"Open brand", 'o', "O", [this] { open_brand(); }});
                        break;
                }
                break;
        }
        actions_run_ = run_;
        shell_set_actions(std::move(a));
    }


    // --- pages ------------------------------------------------------------
    void build_home() {
        list_page("IR Remote", index_.empty() ? "No code database (docs/IR.md)" : thousands(index_.size()) + " remotes");
        for (auto& p : ir::user_remotes())
            row(LV_SYMBOL_POWER, ir::stem(p), "Mine", [this, p] { go(nav(Page::Remote, "", "", p, true)); });
        row(LV_SYMBOL_PLUS, "New remote (learn buttons)", "", [this] { new_remote(); });
        for (auto& u : universals_)
            row(LV_SYMBOL_SHUFFLE, ir::display(u), "Universal", [this, u] { go(nav(Page::Universal, "", "", u, false)); });
        if (!index_.empty())
            row(ZS_ICON_SEARCH, "Browse or search", thousands(index_.size()), [this] { go(nav(Page::Kinds)); });
        list_.finish();
    }

    void new_remote() {
        ask("Name for the new remote", "", [this](const std::string& name) {
            std::string p = ir::user_remote_path(name);
            if (!ir::save(p, {})) {
                shell_toast("Couldn't save to ~/IR");
                return;
            }
            go(nav(Page::Remote, "", "", p, true));
        });
    }

    void build_kinds() {
        if (!filter_.empty()) {   // search everything: brand, model or kind
            int found = 0;
            list_page(std::string(ZS_ICON_SEARCH " ") + filter_ + "_", "");
            list_.set_empty_text("Nothing found");
            for (auto& r : index_) {
                if (!ir::contains(r.brand + " " + r.model, filter_) && !ir::contains(ir::display(r.category), filter_))
                    continue;
                if (++found > 150) break;
                row(nullptr, r.brand + "  " + r.model, ir::display(r.category),
                    [this, r] { go(nav(Page::Remote, r.category, r.brand, db_ + "/" + r.path, false)); });
            }
            list_.finish();
            return;
        }
        list_page("Browse: type to search", "");
        std::vector<std::pair<std::string, int>> kinds;
        for (auto& r : index_) {
            if (kinds.empty() || kinds.back().first != r.category) kinds.push_back({r.category, 0});
            kinds.back().second++;
        }
        for (auto& k : kinds) {
            std::string cat = k.first;
            row(LV_SYMBOL_DIRECTORY, ir::display(cat), std::to_string(k.second), [this, cat] { go(nav(Page::Brands, cat)); });
        }
        list_.finish();
    }

    void build_brands() {
        std::vector<std::pair<std::string, int>> brands;
        for (auto& r : index_) {
            if (r.category != page_.category) continue;
            if (brands.empty() || brands.back().first != r.brand) brands.push_back({r.brand, 0});
            brands.back().second++;
        }
        list_page(ir::display(page_.category), std::to_string(brands.size()) + " brands");
        list_.set_empty_text("No brand matches");
        for (auto& b : brands) {
            std::string brand = b.first;
            row(nullptr, brand, b.second > 1 ? std::to_string(b.second) : "",
                [this, brand] { go(nav(Page::Models, page_.category, brand)); });
        }
        list_.finish();
    }

    void build_models() {
        list_page(ir::display(page_.category) + "  " LV_SYMBOL_RIGHT "  " + page_.brand, "");
        list_.set_empty_text("No model matches");
        for (auto& r : index_) {
            if (r.category != page_.category || r.brand != page_.brand) continue;
            row(nullptr, r.model, std::to_string(r.buttons) + " buttons",
                [this, r] { go(nav(Page::Remote, r.category, r.brand, db_ + "/" + r.path, false)); });
        }
        list_.finish();
    }

    // The remote: a pad of its buttons, and a side panel.
    void build_remote() {
        sigs_.clear();
        ir::load(page_.path, sigs_);
        lv_obj_t* grid = lv_obj_create(root_);
        style_plain(grid);
        lv_obj_set_size(grid, 220, kBodyH);
        lv_obj_set_pos(grid, 0, 0);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_all(grid, 4, 0);
        lv_obj_set_style_pad_gap(grid, 4, 0);
        lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_ACTIVE);
        for (size_t i = 0; i < sigs_.size(); ++i) {
            lv_obj_t* b = lv_button_create(grid);
            lv_obj_set_size(b, 68, 30);
            look::button(b);
            // Power keys: red, the colour of a key you can't take back.
            bool power = ir::contains(sigs_[i].name, "power") || ir::contains(sigs_[i].name, "off");
            if (power) lv_obj_set_style_text_color(b, c(kBad), 0);
            lv_obj_set_style_pad_hor(b, 3, 0);
            lv_obj_t* l = lv_label_create(b);
            lv_label_set_text(l, ir::pretty_name(sigs_[i].name).c_str());
            lv_obj_set_width(l, 62);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
            lv_obj_center(l);
            lv_obj_set_user_data(b, (void*)(intptr_t)i);
            lv_obj_add_event_cb(b, on_pad, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(b, on_pad_focus, LV_EVENT_FOCUSED, this);
            shell_add_focus(b);
            focusables_.push_back(b);
        }
        if (sigs_.empty()) {
            lv_obj_t* e = label(grid, page_.mine ? "No buttons yet.\n\nAdd one with New\nbutton: name it, then\npoint your old remote\nat the Cardputer and\npress its button."
                                                 : "This remote has no\nbuttons ZeroOS can send.",
                                &zs_font_12, kMuted);
            lv_obj_set_width(e, 200);
        }

        lv_obj_t* side = lv_obj_create(root_);
        style_panel(side);
        lv_obj_set_pos(side, 222, 4);
        lv_obj_set_size(side, 94, kBodyH - 8);
        lv_obj_remove_flag(side, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(side, 5, 0);
        lv_obj_t* t = label(side, page_.mine ? ir::stem(page_.path).c_str() : page_.brand.c_str(), &zs_font_12, kMuted);
        lv_obj_set_width(t, 84);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        info_ = label(side, "", &zs_font_12, kText);
        lv_obj_set_width(info_, 84);
        lv_obj_set_pos(info_, 0, 16);
        lv_label_set_long_mode(info_, LV_LABEL_LONG_WRAP);
        status_ = label(side, "", &zs_font_12, kGood);
        lv_obj_set_pos(status_, 0, 64);
        show_info(std::clamp(page_.focus, 0, std::max(0, int(sigs_.size()) - 1)));
    }

    void show_info(int i) {
        if (!info_) return;   // focus events fire while the pad is being built
        if (i < 0 || i >= int(sigs_.size())) {
            lv_label_set_text(info_, "");
            return;
        }
        auto& s = sigs_[size_t(i)];
        lv_label_set_text(info_, (ir::pretty_name(s.name) + "\n" + ir::describe(s)).c_str());
        if (status_) lv_label_set_text(status_, "");
    }

    bool remote_key(uint32_t key) {
        int n = int(focusables_.size());
        int i = focused_index();
        const int cols = 3;
        switch (key) {
            case LV_KEY_RIGHT: if (n) lv_group_focus_obj(focusables_[size_t((i + 1) % n)]); return true;
            case LV_KEY_LEFT: if (n) lv_group_focus_obj(focusables_[size_t((i + n - 1) % n)]); return true;
            case LV_KEY_DOWN: if (n) lv_group_focus_obj(focusables_[size_t(std::min(n - 1, i + cols))]); return true;
            case LV_KEY_UP: if (n) lv_group_focus_obj(focusables_[size_t(std::max(0, i - cols))]); return true;
        }
        if (!page_.mine) {
            if (key == 's' || key == 'S') {
                save_copy();
                return true;
            }
            return false;
        }
        switch (key) {
            case 'l':
            case 'L': learn_focused(); return true;
            case 'n':
            case 'N': new_button(); return true;
            case 'd':
            case 'D':
            case LV_KEY_DEL:
            case LV_KEY_BACKSPACE:
                if (!n) return true;
                shell_confirm("Delete " + ir::pretty_name(sigs_[size_t(i)].name) + "?", "From " + ir::stem(page_.path),
                              "Delete", [this] { delete_button(); });
                return true;
        }
        return false;
    }

    void save_copy() {
        std::string name = page_.brand + " " + ir::stem(page_.path);
        std::string p = ir::user_remote_path(name);
        std::string rel = page_.path.rfind(db_ + "/", 0) == 0 ? page_.path.substr(db_.size() + 1) : page_.path;
        if (ir::save(p, sigs_, "From Flipper-IRDB: " + rel)) shell_toast("Saved to My remotes: " + ir::stem(p));
        else shell_toast("Couldn't save to ~/IR");
    }

    void learn_focused() {
        int i = focused_index();
        if (i < int(sigs_.size())) learn(i, sigs_[size_t(i)].name);
    }

    void new_button() {
        ask("Name for the new button", "", [this](const std::string& name) {
            if (!name.empty()) learn(int(sigs_.size()), name);
        });
    }

    // The focused button goes (the confirm box has asked).
    void delete_button() {
        int i = focused_index();
        if (i >= int(sigs_.size())) return;
        sigs_.erase(sigs_.begin() + i);
        save_mine(std::max(0, i - 1));
    }

    // Learn into button i (a new one when i == size).
    void learn(int i, const std::string& name) {
        shell_toast("Point your remote at the Cardputer and press " + ir::pretty_name(name));
        lv_refr_now(nullptr);
        std::vector<uint32_t> t;
        if (!hal().ir_learn(t, 5000)) {
            shell_toast("Nothing received");
            return;
        }
        ir::Signal s = ir::from_capture(t);
        s.name = name;
        if (i < int(sigs_.size())) sigs_[size_t(i)] = s;
        else sigs_.push_back(s);
        save_mine(i);
        shell_toast("Learned " + ir::pretty_name(name) + (s.raw ? "" : " (" + s.protocol + ")"));
    }

    void save_mine(int focus) {
        if (!ir::save(page_.path, sigs_)) shell_toast("Couldn't save " + page_.path);
        page_.focus = focus;
        show();
    }

    bool send(const ir::Signal& s) {
        ir::Wave w;
        return ir::encode(s, w) && hal().ir_send(w);
    }

    // Universal: the buttons on the left, what is being sent on the right.
    void build_universal() {
        ir::load_universal(db_, page_.path, uni_);
        list_.build(root_, 2, 2, 132, kBodyH - 2);
        for (size_t i = 0; i < uni_.buttons.size(); ++i) {
            row(nullptr, ir::pretty_name(uni_.buttons[i]), std::to_string(uni_.codes[i].size()), [this, i] {
                if (page_.focus != int(i) || run_ == Run::Idle || run_ == Run::Done) {
                    page_.focus = int(i);
                    pos_ = 0;
                    run_ = Run::Sending;
                } else if (run_ == Run::Stopped) {   // Enter again: resend the one it stopped on
                    send(uni_.codes[i][size_t(pos_)]);
                } else {
                    run_ = Run::Stopped;
                }
                update_universal();
            });
        }
        list_.finish();
        lv_obj_t* side = lv_obj_create(root_);
        style_panel(side);
        lv_obj_set_pos(side, 136, 4);
        lv_obj_set_size(side, 180, kBodyH - 8);
        lv_obj_remove_flag(side, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(side, 6, 0);
        info_ = label(side, "", &zs_font_16, kText);
        lv_obj_set_width(info_, 166);
        lv_label_set_long_mode(info_, LV_LABEL_LONG_DOT);
        bar_ = thin_bar(side, 166, 6, kAccent);
        lv_obj_set_pos(bar_, 0, 24);
        status_ = label(side, "", &zs_font_12, kMuted);
        lv_obj_set_pos(status_, 0, 36);
        lv_obj_set_width(status_, 166);
        lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);
        update_universal();
    }

    void update_universal() {
        if (!info_ || uni_.buttons.empty()) return;
        size_t b = size_t(std::clamp(page_.focus, 0, int(uni_.buttons.size()) - 1));
        auto& codes = uni_.codes[b];
        std::string button = ir::pretty_name(uni_.buttons[b]);
        int n = int(codes.size());
        int at = n ? std::clamp(pos_, 0, n - 1) : 0;
        std::string brand = n ? ir::source_brand(codes[size_t(at)].source) : "";
        lv_bar_set_range(bar_, 0, std::max(1, n));
        switch (run_) {
            case Run::Idle:
                lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
                lv_label_set_text(info_, (ir::display(uni_.name) + ": " + button).c_str());
                lv_label_set_text(status_, fmt("Point at the device and send: %d codes go out, most common "
                                               "first. Stop as soon as it reacts.", n).c_str());
                break;
            case Run::Sending:
                lv_bar_set_value(bar_, pos_ + 1, LV_ANIM_OFF);
                lv_label_set_text(info_, fmt("%s  %d / %d", button.c_str(), at + 1, n).c_str());
                lv_label_set_text(status_, brand.c_str());
                break;
            case Run::Stopped:
                lv_bar_set_value(bar_, at + 1, LV_ANIM_OFF);
                lv_label_set_text(info_, fmt("Stopped at %d / %d", at + 1, n).c_str());
                lv_label_set_text(status_, (brand + "\nNot quite? Try the codes either side of it, or open " + brand +
                                            "'s remotes.").c_str());
                break;
            case Run::Done:
                lv_bar_set_value(bar_, n, LV_ANIM_OFF);
                lv_label_set_text(info_, fmt("Sent all %d", n).c_str());
                lv_label_set_text(status_, "No reaction? Go closer and aim at the device's front, or find its "
                                           "remote under Browse.");
                break;
        }
        if (run_ != actions_run_) set_actions();
    }

    bool universal_key(uint32_t key) {
        if (run_ == Run::Sending) {   // any key stops; the last code sent is probably the one
            if (key == LV_KEY_UP || key == LV_KEY_DOWN || printable(key) || key == LV_KEY_ENTER ||
                key == LV_KEY_LEFT || key == LV_KEY_RIGHT) {
                pos_ = std::max(0, pos_ - 1);
                run_ = Run::Stopped;
                update_universal();
                return true;
            }
            return false;
        }
        if (run_ == Run::Stopped && !uni_.buttons.empty()) {
            auto& codes = uni_.codes[size_t(page_.focus)];
            int n = int(codes.size());
            if ((key == LV_KEY_LEFT || key == LV_KEY_RIGHT) && n) {
                pos_ = std::clamp(pos_ + (key == LV_KEY_RIGHT ? 1 : -1), 0, n - 1);
                send(codes[size_t(pos_)]);
                update_universal();
                return true;
            }
            if (key == 'o' || key == 'O') {
                open_brand();
                return true;
            }
        }
        // A Control screen: letters are its own, not a filter for the list.
        if (printable(key) && !digit(key)) return false;
        bool used = list_.key(key);
        // Moving to another button starts it afresh.
        int i = std::max(0, list_.focused());
        if (i != page_.focus) {
            page_.focus = i;
            run_ = Run::Idle;
            pos_ = 0;
            update_universal();
        }
        return used;
    }

    // The brand of the code it stopped on: its remotes.
    void open_brand() {
        if (run_ != Run::Stopped || uni_.buttons.empty()) return;
        auto& codes = uni_.codes[size_t(page_.focus)];
        if (codes.empty()) return;
        const std::string& src = codes[size_t(std::clamp(pos_, 0, int(codes.size()) - 1))].source;
        std::string cat = src.substr(0, src.find('/'));
        std::string brand = ir::source_brand(src);
        if (!brand.empty()) go(nav(Page::Models, cat, brand));
    }

    // --- text input (as in Media) -----------------------------------------
    using Done = std::function<void(const std::string&)>;
    void ask(const char* title, const std::string& initial, Done done) {
        done_ = std::move(done);
        // A Text screen while it's open: the bar says so.
        shell_set_actions({{"OK", 0, "Enter", nullptr}, {"Cancel", 0, "Esc", nullptr}});
        input_ = lv_obj_create(root_);
        look::raised(input_);
        lv_obj_set_size(input_, 300, 74);
        lv_obj_center(input_);
        label(input_, title, &zs_font_12, kMuted);
        lv_obj_t* ta = ta_ = lv_textarea_create(input_);
        look::field(ta_);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_max_length(ta, 40);
        lv_textarea_set_text(ta, initial.c_str());
        lv_obj_set_width(ta, 280);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(ta, [](lv_event_t* e) {
            auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
            self->answer_ = lv_textarea_get_text(lv_event_get_target_obj(e));
            self->answered_ = true;   // tick() acts on it: that may rebuild the page,
                                      // which can't happen inside this key event
            // Enter is still down: without this its release would also
            // "click" whichever button gets the focus next.
            if (lv_indev_t* in = lv_indev_active()) lv_indev_wait_release(in);
            self->close_input();
        }, LV_EVENT_READY, this);
        lv_group_add_obj(group, ta);
        lv_group_focus_obj(ta);
        lv_group_set_editing(group, true);
    }

    // The box is hidden, not deleted, as its own Enter event is still
    // running; the next page rebuild deletes it with the rest.
    void close_input() {
        if (!input_) return;
        if (!answered_) done_ = nullptr;   // cancelled
        lv_group_set_editing(group, false);
        if (ta_) lv_group_remove_obj(ta_);
        lv_obj_add_flag(input_, LV_OBJ_FLAG_HIDDEN);
        input_ = ta_ = nullptr;
        set_actions();
        if (!focusables_.empty())
            lv_group_focus_obj(focusables_[size_t(std::clamp(page_.focus, 0, int(focusables_.size()) - 1))]);
        else if (list_.size() > 0)
            list_.focus(std::clamp(page_.focus, 0, list_.size() - 1));
    }

    // --- events -----------------------------------------------------------
    static void on_pad(lv_event_t* e) {
        auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
        size_t i = size_t((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        if (i >= self->sigs_.size()) return;
        bool ok = self->send(self->sigs_[i]);
        if (self->status_) {
            lv_label_set_text(self->status_, ok ? LV_SYMBOL_OK " Sent" : LV_SYMBOL_WARNING " Not sent");
            lv_obj_set_style_text_color(self->status_, c(ok ? kGood : kBad), 0);
        }
    }
    static void on_pad_focus(lv_event_t* e) {
        auto* self = static_cast<IrApp*>(lv_event_get_user_data(e));
        self->page_.focus = int((intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)));
        self->show_info(self->page_.focus);
    }
    lv_obj_t* root_ = nullptr;
    std::string db_;
    std::vector<ir::RemoteRef> index_;
    std::vector<std::string> universals_;

    Nav page_;
    std::vector<Nav> stack_;
    std::string filter_;

    ListView list_;
    std::vector<lv_obj_t*> focusables_;   // a remote's keypad
    lv_obj_t* info_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* bar_ = nullptr;

    std::vector<ir::Signal> sigs_;   // the open remote

    ir::Universal uni_;
    Run run_ = Run::Idle;
    Run actions_run_ = Run::Idle;   // what the soft-key bar was last set for
    int pos_ = 0;

    uint32_t last_send_ = 0;

    lv_obj_t* input_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    Done done_;
    std::string answer_;
    bool answered_ = false;
};
}  // namespace

std::unique_ptr<App> make_ir_app() { return std::make_unique<IrApp>(); }

}  // namespace zs
