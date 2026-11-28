// Mesh: the Chats tab, a chat, quick replies, reactions, a message's
// details, searching every message. (app.h has the map of the files.)
#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>

#include <sys/stat.h>

#include "apps/mesh/app.h"
#include "apps/mesh/core/text.h"
#include "ui/fileio.h"

namespace zs {
using namespace mesh_ui;

namespace {
constexpr int kTabH = 14;
constexpr int kShowAtFirst = 40;   // messages drawn when a chat opens; Up brings more
constexpr int kShowMore = 30;

const char* const kDefaultQuick[] = {
    "On my way", "Yes", "No", "Where are you?", "All OK here", "Need help", "Call me when you can", "Thanks!",
};

struct Reaction {
    const char* emoji;   // what's sent
    const char* shown;   // what our font can draw
    const char* name;
};
const Reaction kReactions[] = {
    {"\xF0\x9F\x91\x8D", "+1", "Thumbs up"},      {"\xE2\x9D\xA4\xEF\xB8\x8F", "<3", "Heart"},
    {"\xF0\x9F\x98\x82", ":'D", "Laughing"},      {"\xF0\x9F\x91\x8E", "-1", "Thumbs down"},
    {"\xE2\x9D\x93", "?", "Question"},            {"\xE2\x80\xBC\xEF\xB8\x8F", "!!", "Important"},
    {"\xF0\x9F\x91\x8B", "hi", "Wave"},           {"\xE2\x9C\x85", "yes", "Done"},
};

// A message picked in a chat, or not.
void light(lv_obj_t* row, bool on) {
    lv_obj_set_style_bg_opa(row, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, on ? 2 : 0, 0);
    lv_obj_set_style_pad_left(row, on ? 4 : 2, 0);
}

std::string quick_path() { return home_dir() + "/.config/zeroos/mesh-quick.txt"; }

// A file name from a chat's title: letters, digits, - and _.
std::string file_stem(const std::string& title) {
    std::string out;
    for (unsigned char ch : title) out.push_back(std::isalnum(ch) || ch == '-' || ch == '_' ? char(ch) : '_');
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "chat" : out.substr(0, 40);
}
}  // namespace

// ---- The Chats tab: every channel, then every person you've written with.

std::string MeshApp::chat_row_value(const mesh::Chat& c) {
    if (c.unread) return fmt("%d new", c.unread);
    if (c.msgs.empty()) return c.dm ? "" : mesh::psk_kind(client().channel(c.channel) ? client().channel(c.channel)->settings.psk : std::string());
    const mesh::Message& m = c.msgs.back();
    std::string who = m.mine ? "You" : client().node_name(m.from);
    return mesh::one_line(who + ": " + mesh::display_text(m.text), 26);
}

void MeshApp::build_chats_tab() {
    mesh::Client& c = client();
    set_actions({hint("Open", "Enter"),
                 menu("New message", 'n', [this] { new_message(); }),
                 menu("Find in messages", 'f', [this] {
                     View v;
                     v.page = Page::Search;
                     push(v);
                 }),
                 menu("Mark all read", 'm', [this] {
                     for (const mesh::Chat& ch : client().chats()) client().mark_read(ch.key, now());
                     session().save_marks();
                 }),
                 menu("Save all to Documents", 's', [this] { export_chat("*"); })});
    list_.build(root_, 4, kTabH + 1, kScreenW - 8, kBodyH - kTabH - 2);
    list_.set_empty_text(c.ever_ready() ? "No chats yet" : "Waiting for the radio...");
    seen_sig_.clear();
    int focus = -1, row = 0;
    for (const mesh::Chat& ch : c.chats()) {
        std::string key = ch.key;
        list_.add(ch.dm ? LV_SYMBOL_ENVELOPE : ZS_ICON_CHAT, c.chat_title(ch), chat_row_value(ch), [this, key] { open_chat(key); });
        if (ch.unread) list_.set_value_color(row, kGood);
        if (key == focus_key_) focus = row;
        seen_sig_ += key + "\x1f" + c.chat_title(ch) + "\x1e";
        ++row;
    }
    if (c.ever_ready()) list_.add(LV_SYMBOL_PLUS, "Message someone...", "", [this] { new_message(); });
    list_.finish();
    if (focus >= 0) list_.focus(focus);
    focus_key_.clear();
}

void MeshApp::refresh_chats_tab() {
    mesh::Client& c = client();
    std::string sig;
    for (const mesh::Chat& ch : c.chats()) sig += ch.key + "\x1f" + c.chat_title(ch) + "\x1e";
    if (sig != seen_sig_) {
        // The rows themselves changed (a new chat, a name learnt): draw the
        // list again, with the focus on the chat it was on. Not while a
        // filter is being typed: that would wipe it.
        if (!list_.filter().empty()) return;
        int f = list_.focused();
        if (f >= 0 && f < int(c.chats().size())) {
            // The row's key, from the order the list was built in.
            size_t at = 0;
            for (int i = 0; i < f; ++i) at = seen_sig_.find('\x1e', at) + 1;
            size_t end = seen_sig_.find('\x1f', at);
            if (end != std::string::npos) focus_key_ = seen_sig_.substr(at, end - at);
        }
        dirty_ = true;
        return;
    }
    int row = 0;
    for (const mesh::Chat& ch : c.chats()) {
        list_.set_value(row, chat_row_value(ch));
        list_.set_value_color(row, ch.unread ? kGood : kMuted);
        ++row;
    }
}

void MeshApp::new_message() {
    mesh::Client& c = client();
    std::vector<uint32_t> nodes;
    for (const auto& kv : c.nodes())
        if (kv.first != c.my_num() && !kv.second.unmessagable && !kv.second.ignored) nodes.push_back(kv.first);
    std::sort(nodes.begin(), nodes.end(), [&c](uint32_t a, uint32_t b) { return c.node(a)->last_heard > c.node(b)->last_heard; });
    std::vector<Option> opts;
    for (uint32_t n : nodes) {
        const mesh::Node* node = c.node(n);
        Option o;
        o.text = node->long_name.empty() ? node->name() : node->name() + "  " + node->long_name;
        o.value = mesh::age_text(now(), node->last_heard);
        o.choose = [this, n] {
            client().open_dm(n);
            open_chat(mesh::chat_key_dm(n));
        };
        opts.push_back(std::move(o));
    }
    if (opts.empty()) {
        shell_toast("No one has been heard yet");
        return;
    }
    pick("Message who?", std::move(opts), -1, "Type to find");
}

// ---- A chat

void MeshApp::open_chat(const std::string& key) {
    reply_to_ = 0;
    bell_ = false;
    shown_from_ = -1;
    View v;
    v.page = Page::Chat;
    v.key = key;
    push(v);
}

const mesh::Message* MeshApp::selected_message() {
    if (stack_.empty() || sel_ < 0 || sel_ >= int(msg_index_.size())) return nullptr;
    // The chat page, or a page opened from it (React, the message's details).
    for (size_t i = stack_.size(); i-- > 0;) {
        if (stack_[i].page != Page::Chat) continue;
        const mesh::Chat* chat = client().chat(stack_[i].key);
        if (!chat || msg_index_[size_t(sel_)] >= int(chat->msgs.size())) return nullptr;
        return &chat->msgs[size_t(msg_index_[size_t(sel_)])];
    }
    return nullptr;
}

void MeshApp::set_chat_actions() {
    const std::string key = stack_.back().key;
    const mesh::Chat* chat = client().chat(key);
    const mesh::Message* m = selected_message();
    bool dm = chat && chat->dm;
    uint32_t peer = chat ? chat->peer : 0;
    std::vector<Action> a;
    a.push_back(hint(m ? "Details" : "Send", "Enter"));
    a.push_back(hint(m ? "Other" : "Pick", LV_SYMBOL_UP LV_SYMBOL_DOWN));
    a.push_back(menu("Quick reply", 'q', [this] {
        View v;
        v.page = Page::Quick;
        push(v);
    }));
    a.push_back(menu("Reply to it", 'r', [this] {
        const mesh::Message* msg = selected_message();
        if (!msg) return;
        reply_to_ = msg->id;
        select_message(-1);
        update_chat_head();
    }, m && !m->mine && m->id));
    a.push_back(menu("React to it", 'e', [this] { build_react(); }, m && m->id));
    a.push_back(menu("Send again", 'a', [this] {
        const mesh::Message* msg = selected_message();
        if (!msg) return;
        std::string text = msg->text;
        while (!text.empty() && text.back() == '\a') text.pop_back();
        if (!client().send_text(stack_.back().key, text, msg->reply_id, msg->bell)) shell_toast(client().last_error());
        select_message(-1);
    }, m && m->mine && m->state == mesh::Delivery::Failed));
    a.push_back(menu(bell_ ? "No bell" : "Ring their bell", 'b', [this] {
        bell_ = !bell_;
        update_chat_head();
        set_chat_actions();
    }));
    a.push_back(menu("Where I am", 'w', [this] { send_waypoint(); }));
    if (dm) {
        a.push_back(menu("About this node", 'i', [this, peer] {
            View v;
            v.page = Page::NodePage;
            v.num = peer;
            push(v);
        }));
    }
    a.push_back(menu("Save to Documents", 's', [this, key] { export_chat(key); }));
    a.push_back(danger("Delete this chat's messages", 'd', "Delete every message in this chat?", [this, key] {
        bool dm_chat = !key.empty() && key[0] == 'd';
        delete_chat(key);
        if (dm_chat) pop();   // the chat itself is gone
        else dirty_ = true;
    }));
    set_actions(std::move(a));
}

void MeshApp::build_chat() {
    const std::string key = stack_.back().key;
    set_chat_actions();

    chat_head_ = label(root_, "", &zs_font_12, kMuted);
    lv_obj_set_pos(chat_head_, 8, 2);
    lv_label_set_long_mode(chat_head_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(chat_head_, kScreenW - 16);

    chat_box_ = lv_obj_create(root_);
    style_panel(chat_box_);
    lv_obj_set_pos(chat_box_, 4, 18);
    lv_obj_set_size(chat_box_, kScreenW - 8, kBodyH - 58);
    lv_obj_set_flex_flow(chat_box_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(chat_box_, 3, 0);
    lv_obj_set_style_pad_row(chat_box_, 1, 0);
    lv_obj_set_scroll_dir(chat_box_, LV_DIR_VER);

    ta_ = lv_textarea_create(root_);
    kit::look::field(ta_);
    lv_textarea_set_one_line(ta_, true);
    lv_textarea_set_placeholder_text(ta_, "Message");
    lv_textarea_set_max_length(ta_, uint32_t(mesh::Client::kMaxText));
    auto draft = drafts_.find(key);
    lv_textarea_set_text(ta_, draft == drafts_.end() ? "" : draft->second.c_str());
    lv_obj_set_width(ta_, kScreenW - 8);
    lv_obj_set_pos(ta_, 4, kBodyH - 37);
    lv_obj_add_event_cb(ta_, [](lv_event_t* e) { static_cast<MeshApp*>(lv_event_get_user_data(e))->send_from_box(); },
                        LV_EVENT_READY, this);
    lv_group_add_obj(group, ta_);
    lv_group_focus_obj(ta_);
    lv_group_set_editing(group, true);

    fill_chat();
    update_chat_head();
}

void MeshApp::update_chat_head() {
    if (!chat_head_ || stack_.empty()) return;
    mesh::Client& c = client();
    const mesh::Chat* chat = c.chat(stack_.back().key);
    if (!chat) return;
    std::string text = c.chat_title(*chat);
    uint32_t color = kMuted;
    std::string problem = status_color() != kMuted ? status_line() : "";
    if (!problem.empty()) {
        text += "  |  " + problem;
        color = status_color();
    } else if (reply_to_) {
        std::string who = "a message";
        for (const mesh::Message& m : chat->msgs)
            if (m.id == reply_to_) who = c.node_name(m.from);
        text += "  |  Replying to " + who + " (Esc: don't)";
        color = kInfo;
    } else if (bell_) {
        text += "  |  The next one rings their bell";
        color = kInfo;
    } else if (chat->dm) {
        const mesh::Node* n = c.node(chat->peer);
        if (n) {
            std::string how = n->via_mqtt ? "by MQTT" : n->hops == 0 ? "direct" : n->hops > 0 ? fmt("%d hops", n->hops) : "";
            std::string age = mesh::age_text(now(), n->last_heard);
            if (!how.empty()) text += "  |  " + how;
            if (!age.empty()) text += std::string(how.empty() ? "  |  " : ", ") + (age == "now" ? "heard just now" : "heard " + age + " ago");
            if (!n->has_key) text += ", no key yet";
        }
    } else {
        const mesh::Channel* ch = c.channel(chat->channel);
        text += std::string("  |  ") + (ch ? mesh::psk_kind(ch->settings.psk) : "not on this radio any more");
        text += fmt(", %d online", c.online_count(now()));
    }
    lv_label_set_text(chat_head_, text.c_str());
    lv_obj_set_style_text_color(chat_head_, col(color), 0);
    if (ta_) {
        const char* ph = reply_to_ ? "Your reply" : bell_ ? "Message (with the bell)" : chat->dm ? "Message" : "Message everyone on it";
        lv_textarea_set_placeholder_text(ta_, ph);
    }
}

void MeshApp::fill_chat() {
    if (!chat_box_ || stack_.empty()) return;
    mesh::Client& c = client();
    const std::string key = stack_.back().key;
    const mesh::Chat* chat = c.chat(key);
    // Keep the pick on the same message while the list is drawn again.
    uint32_t picked_id = stack_.back().num;   // coming back from a message's details
    int picked_index = -1;
    stack_.back().num = 0;
    if (const mesh::Message* m = selected_message()) {
        picked_id = m->id;
        picked_index = msg_index_[size_t(sel_)];
    }
    lv_obj_clean(chat_box_);
    msg_objs_.clear();
    msg_index_.clear();
    sel_ = -1;
    if (!chat || chat->msgs.empty()) {
        label(chat_box_, chat && chat->dm ? "No messages yet. Only the two of you can read what you write here."
                                          : "No messages yet. Say hello!",
              &zs_font_12, kMuted);
        if (chat) c.mark_read(key, now());
        return;
    }
    int n = int(chat->msgs.size());
    if (shown_from_ < 0 || shown_from_ > n) shown_from_ = std::max(0, n - kShowAtFirst);
    int w = kScreenW - 26;
    long last_time = 0;
    if (shown_from_ > 0) label(chat_box_, fmt("%d older: Up to see them", shown_from_).c_str(), &zs_font_12, kDisabled);
    for (int i = shown_from_; i < n; ++i) {
        const mesh::Message& m = chat->msgs[size_t(i)];
        // The time, where there's a gap: not on every line.
        if (!last_time || m.time - last_time > 1800) {
            lv_obj_t* t = label(chat_box_, mesh::clock_text(now(), m.time).c_str(), &zs_font_12, kDisabled);
            lv_obj_set_width(t, w);
            lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        }
        last_time = m.time;

        lv_obj_t* row = lv_obj_create(chat_box_);
        style_plain(row);
        lv_obj_set_width(row, w + 4);
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_hor(row, 2, 0);
        lv_obj_set_style_pad_row(row, 0, 0);
        // The picked message: a lit bar down its left side on a raised ground.
        lv_obj_set_style_bg_color(row, col(kSurface2), 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(row, col(kFocus), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        if (m.reply_id) {
            for (const mesh::Message& t : chat->msgs) {
                if (t.id != m.reply_id) continue;
                std::string q = "> " + (t.mine ? std::string("You") : c.node_name(t.from)) + ": " + mesh::display_text(t.text);
                label(row, mesh::one_line(q, 44).c_str(), &zs_font_12, kDisabled);
                break;
            }
        }
        std::string text = (m.mine ? std::string("You") : c.node_name(m.from)) + ": ";
        if (m.bell) text += LV_SYMBOL_BELL " ";
        if (m.waypoint) text += LV_SYMBOL_GPS " ";
        text += mesh::display_text(m.text);
        if (m.mine) {
            switch (m.state) {
            case mesh::Delivery::Queued:
            case mesh::Delivery::Sent: text += " ..."; break;
            case mesh::Delivery::Relayed: text += " " LV_SYMBOL_OK; break;
            case mesh::Delivery::Delivered: text += " " LV_SYMBOL_OK LV_SYMBOL_OK; break;
            default: break;
            }
        }
        lv_obj_t* l = label(row, text.c_str(), &zs_font_12, m.mine ? kMuted : kText);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, w);
        if (m.mine && m.state == mesh::Delivery::Failed) {
            std::string why = std::string(LV_SYMBOL_WARNING " Not sent: ") + mesh::delivery_error_text(m.error);
            lv_obj_t* f = label(row, why.c_str(), &zs_font_12, kBad);
            lv_label_set_long_mode(f, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(f, w);
        }
        if (!m.reactions.empty()) {
            std::string r;
            for (const mesh::Reaction& x : m.reactions) {
                if (!r.empty()) r += "   ";
                r += mesh::reaction_text(x.text) + " " + (x.from == c.my_num() ? std::string("You") : c.node_name(x.from));
            }
            lv_obj_t* rl = label(row, mesh::one_line(r, 60).c_str(), &zs_font_12, kInfo);
            lv_obj_set_style_pad_left(rl, 10, 0);
        }
        if ((picked_id && m.id == picked_id) || (!picked_id && picked_index == i)) sel_ = int(msg_objs_.size());
        msg_objs_.push_back(row);
        msg_index_.push_back(i);
    }
    lv_obj_update_layout(chat_box_);
    if (sel_ >= 0 && picked_index < 0) set_chat_actions();   // the pick came back with the page
    if (sel_ >= 0) {
        light(msg_objs_[size_t(sel_)], true);
        lv_obj_scroll_to_view(msg_objs_[size_t(sel_)], LV_ANIM_OFF);
    } else {
        lv_obj_scroll_to_y(chat_box_, LV_COORD_MAX, LV_ANIM_OFF);
    }
    // Being looked at is being read.
    c.mark_read(key, now());
}

void MeshApp::select_message(int index) {
    if (sel_ >= 0 && sel_ < int(msg_objs_.size())) light(msg_objs_[size_t(sel_)], false);
    sel_ = index >= 0 && index < int(msg_objs_.size()) ? index : -1;
    if (sel_ >= 0) {
        light(msg_objs_[size_t(sel_)], true);
        lv_obj_scroll_to_view(msg_objs_[size_t(sel_)], LV_ANIM_OFF);
    } else if (chat_box_) {
        lv_obj_scroll_to_y(chat_box_, LV_COORD_MAX, LV_ANIM_OFF);
    }
    set_chat_actions();
}

bool MeshApp::chat_key(uint32_t key) {
    bool empty = !ta_ || !*lv_textarea_get_text(ta_);
    if (key == LV_KEY_UP || key == LV_KEY_DOWN) {
        // Up and Down pick a message (to reply to, react to, look at, send
        // again). Only with nothing typed: they never take the cursor away.
        if (!empty && sel_ < 0) return false;
        if (msg_objs_.empty()) return true;
        if (key == LV_KEY_UP) {
            if (sel_ < 0) {
                select_message(int(msg_objs_.size()) - 1);
            } else if (sel_ > 0) {
                select_message(sel_ - 1);
            } else if (shown_from_ > 0) {
                shown_from_ = std::max(0, shown_from_ - kShowMore);   // older ones, the pick staying put
                fill_chat();
                if (sel_ > 0) select_message(sel_ - 1);
            }
        } else {
            select_message(sel_ < 0 || sel_ + 1 >= int(msg_objs_.size()) ? -1 : sel_ + 1);
        }
        return true;
    }
    if (key == LV_KEY_ENTER && sel_ >= 0 && empty) {
        if (const mesh::Message* m = selected_message()) {
            stack_.back().num = m->id;   // picked again on the way back
            View v;
            v.page = Page::MessageInfo;
            v.num = m->id;
            v.index = int(m->time);
            push(v);
        }
        return true;
    }
    if (sel_ >= 0 && key >= 32 && key < 127) select_message(-1);   // typing: back to writing
    return false;
}

void MeshApp::send_from_box() {
    if (!ta_ || stack_.empty()) return;
    std::string text = lv_textarea_get_text(ta_);
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (text.empty()) return;
    if (!client().send_text(stack_.back().key, text, reply_to_, bell_)) {
        shell_toast(client().last_error());   // what was typed stays in the box
        return;
    }
    lv_textarea_set_text(ta_, "");
    drafts_.erase(stack_.back().key);
    reply_to_ = 0;
    bell_ = false;
    shown_from_ = -1;
    update_chat_head();
    set_chat_actions();
}

// ---- Quick replies: a list of your own, sent with one key.

std::vector<std::string> MeshApp::quick_replies() {
    std::vector<std::string> q;
    std::ifstream in(quick_path());
    if (in) {
        for (std::string line; std::getline(in, line);)
            if (!line.empty()) q.push_back(mesh::utf8_cut(line, mesh::Client::kMaxText));
        return q;
    }
    for (const char* s : kDefaultQuick) q.push_back(s);
    return q;
}

void MeshApp::save_quick(const std::vector<std::string>& q) {
    std::string text;
    for (const std::string& s : q) text += mesh::one_line(s, mesh::Client::kMaxText) + "\n";
    ::mkdir((home_dir() + "/.config").c_str(), 0755);
    ::mkdir((home_dir() + "/.config/zeroos").c_str(), 0755);
    if (!write_file_atomic(quick_path(), text, 0644)) shell_toast("Couldn't save the quick replies");
}

void MeshApp::build_quick() {
    std::string key;
    for (size_t i = stack_.size(); i-- > 0;)
        if (stack_[i].page == Page::Chat) {
            key = stack_[i].key;
            break;
        }
    set_actions({hint("Send", "Enter"), hint("Back", "Esc"),
                 menu("New", 'n', [this] {
                     prompt("New quick reply", "On my way", "", "Kept in the list. Enter on it sends it.", int(mesh::Client::kMaxText),
                            [this](const std::string& s) {
                                if (s.empty()) return;
                                auto q = quick_replies();
                                q.push_back(s);
                                save_quick(q);
                            });
                 }),
                 menu("Edit", 'e', [this] {
                     int at = list_.focused();
                     auto q = quick_replies();
                     if (at < 0 || at >= int(q.size())) return;
                     prompt("Edit quick reply", "", q[size_t(at)], "", int(mesh::Client::kMaxText), [this, at](const std::string& s) {
                         auto all = quick_replies();
                         if (s.empty() || at >= int(all.size())) return;
                         all[size_t(at)] = s;
                         save_quick(all);
                     });
                 }),
                 danger("Delete", 'd', "Delete this quick reply?", [this] {
                     int at = list_.focused();
                     auto q = quick_replies();
                     if (at < 0 || at >= int(q.size())) return;
                     q.erase(q.begin() + at);
                     save_quick(q);
                     dirty_ = true;
                 })});
    title_row("Quick reply", "1 to 9 send");
    list_.build(root_, 4, 17, kScreenW - 8, kBodyH - 18);
    list_.set_empty_text("None. Tab, then N, makes one.");
    for (const std::string& s : quick_replies()) {
        list_.add(nullptr, s, "", [this, key, s] {
            if (!client().send_text(key, s, reply_to_, bell_)) {
                shell_toast(client().last_error());
                return;
            }
            reply_to_ = 0;
            bell_ = false;
            shown_from_ = -1;
            pop();
        });
    }
    list_.finish();
}

// Not a page of its own: a list to pick from, over the chat.
void MeshApp::build_react() {
    const mesh::Message* m = selected_message();
    if (!m || !m->id) return;
    uint32_t id = m->id;
    std::string key = stack_.back().key;
    std::vector<Option> opts;
    for (const Reaction& r : kReactions) {
        Option o;
        o.text = std::string(r.shown) + "   " + r.name;
        std::string emoji = r.emoji;
        o.choose = [this, key, id, emoji] {
            if (!client().send_reaction(key, id, emoji)) shell_toast(client().last_error());
        };
        opts.push_back(std::move(o));
    }
    pick("React with", std::move(opts), -1, "Phones show the emoji");
}

void MeshApp::build_message_info() {
    set_actions({hint("Back", "Esc"), hint("Scroll", LV_SYMBOL_UP LV_SYMBOL_DOWN)});
    // The pick was made on the chat page, which this one covers: find it again.
    std::string key;
    for (size_t i = stack_.size(); i-- > 0;)
        if (stack_[i].page == Page::Chat) {
            key = stack_[i].key;
            break;
        }
    mesh::Client& c = client();
    const mesh::Chat* chat = c.chat(key);
    uint32_t id = stack_.back().num;
    const mesh::Message* m = nullptr;
    if (chat) {
        for (const mesh::Message& x : chat->msgs)
            if (x.id == id && (stack_.back().index == 0 || x.time == long(stack_.back().index))) m = &x;
    }
    if (!m) {
        build_text_page("Message", "It's no longer here.");
        return;
    }
    std::string body;
    auto row = [&body](const std::string& k, const std::string& v) {
        if (!v.empty()) body += k + ": " + v + "\n";
    };
    time_t t = time_t(m->time);
    struct tm tmv {};
    localtime_r(&t, &tmv);
    char when[40];
    std::strftime(when, sizeof when, "%a %e %b %Y, %H:%M:%S", &tmv);
    row("From", m->mine ? "You" : c.node_full_name(m->from) + " (" + mesh::node_id(m->from) + ")");
    row("To", chat->dm ? (m->mine ? c.node_full_name(m->to) : "You") : c.chat_title(*chat) + " (everyone on it)");
    row(m->mine ? "Sent" : "Received", when);
    if (m->mine) {
        row("Delivery", mesh::delivery_text(*m, chat->dm));
        if (m->state == mesh::Delivery::Relayed && chat->dm)
            body += "Another node was heard passing it on, but the person it's for hasn't confirmed it.\n";
        if (m->state == mesh::Delivery::Relayed && !chat->dm)
            body += "A channel message is never confirmed by its readers: another node passing it on is all there is.\n";
    } else {
        row("Came", m->via_mqtt ? "by MQTT (the internet)" : m->hops == 0 ? "direct, by radio" : m->hops > 0 ? fmt("through %d other node%s", m->hops, m->hops == 1 ? "" : "s") : "by radio");
        if (m->hops == 0 && !m->via_mqtt && (m->rssi != 0 || m->snr != 0))
            row("Signal", fmt("%s: SNR %.1f dB, RSSI %d dBm", mesh::snr_quality(m->snr), double(m->snr), m->rssi));
    }
    row("Encrypted", m->pki ? "to their key: only the two of you can read it" : chat->dm ? "with the channel's key" : mesh::psk_kind(c.channel(chat->channel) ? c.channel(chat->channel)->settings.psk : std::string()));
    if (!m->reactions.empty()) {
        std::string r;
        for (const mesh::Reaction& x : m->reactions)
            r += (r.empty() ? "" : ", ") + mesh::reaction_text(x.text) + " " + (x.from == c.my_num() ? std::string("You") : c.node_name(x.from));
        row("Reactions", r);
    }
    row("Length", fmt("%d of %d bytes", int(m->text.size()), int(mesh::Client::kMaxText)));
    row("Packet", fmt("%08x", m->id));
    body += "\n" + mesh::display_text(m->text);
    build_text_page("Message", body);
}

// ---- Find: every message, newest first. Typing filters, as in any list.

void MeshApp::build_search() {
    mesh::Client& c = client();
    set_actions({hint("Open its chat", "Enter"), hint("Back", "Esc")});
    title_row("Find in messages", "Type to find");
    list_.build(root_, 4, 17, kScreenW - 8, kBodyH - 18);
    list_.set_empty_text("No messages");
    struct Hit {
        long time;
        std::string key, text, where;
    };
    std::vector<Hit> hits;
    for (const mesh::Chat& chat : c.chats()) {
        for (const mesh::Message& m : chat.msgs) {
            Hit h;
            h.time = m.time;
            h.key = chat.key;
            h.text = (m.mine ? std::string("You") : c.node_name(m.from)) + ": " + mesh::one_line(mesh::display_text(m.text), 120);
            h.where = mesh::one_line(c.chat_title(chat), 12) + " " + mesh::age_text(now(), m.time);
            hits.push_back(std::move(h));
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.time > b.time; });
    if (hits.size() > 600) hits.resize(600);
    for (const Hit& h : hits) {
        std::string key = h.key;
        list_.add(nullptr, h.text, h.where, [this, key] {
            pop();
            open_chat(key);
        });
    }
    list_.finish();
}

// ---- Keeping and deleting

void MeshApp::export_chat(const std::string& key) {
    std::string text = session().export_text(key, now());
    if (text.empty()) {
        shell_toast("Nothing to save yet");
        return;
    }
    std::string title = "all chats";
    if (key != "*") {
        const mesh::Chat* chat = client().chat(key);
        if (chat) title = client().chat_title(*chat);
    }
    time_t t = time_t(now());
    struct tm tmv {};
    localtime_r(&t, &tmv);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M", &tmv);
    std::string dir = home_dir() + "/Documents";
    ::mkdir(dir.c_str(), 0755);
    dir += "/Mesh";
    ::mkdir(dir.c_str(), 0755);
    std::string name = file_stem(title) + "-" + stamp + ".txt";
    if (write_file_atomic(dir + "/" + name, text, 0644)) shell_toast("Saved: Documents/Mesh/" + name);
    else shell_toast("Couldn't save it: is the card full?");
}

void MeshApp::delete_chat(const std::string& key) {
    session().forget_chat(key, now());
    drafts_.erase(key);
    shell_toast("Messages deleted");
}

void MeshApp::send_waypoint() {
    double lat, lon;
    if (!client().here(lat, lon)) {
        shell_toast("No position yet: turn on Share my position (Radio) and wait for a fix");
        return;
    }
    std::string key = stack_.back().key;
    prompt("Name this spot", "Camp", "", "Sends where you are now, with this name. It shows on their maps for 8 hours.", 29,
           [this, key, lat, lon](const std::string& name) {
               if (!client().send_waypoint(key, name, lat, lon, now())) shell_toast(client().last_error());
               else shell_toast("Sent where you are");
           });
}

}  // namespace zs
