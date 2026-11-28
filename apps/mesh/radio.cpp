// Mesh: the Radio tab. Your name, channels (make, join by QR code or link,
// share), where you are, phones, the radio's own settings, what it's doing.
// (app.h has the map of the files.)
#include <cstdlib>
#include <ctime>
#include <random>

#include "apps/mesh/app.h"
#include "apps/mesh/core/text.h"
#include "hal/qr.h"
#include "ui/power.h"
#include "ui/prefs.h"

namespace zs {
using namespace mesh_ui;

namespace {
constexpr int kTabH = 14;
// Share my position: how exactly. The figure is the channel's
// "position precision" (bits of latitude and longitude kept).
const char* const kShareNames[] = {"Off", "About 3 km", "About 350 m", "Exact"};
constexpr uint32_t kShareBits[] = {0, 13, 16, 32};

bool simulated() {
    static int s = -1;
    if (s < 0) s = hal().system().simulated ? 1 : 0;
    return s == 1;
}
bool g_sim_lan = false;

std::string random_key() {
    std::random_device rd;
    std::string k;
    for (int i = 0; i < 32; ++i) k.push_back(char(rd() & 0xff));   // AES-256
    return k;
}
}  // namespace

void MeshApp::saved_note(bool restarts) {
    shell_toast(restarts ? "Saved. The radio restarts: a few seconds" : "Saved");
    dirty_ = true;
}

// ---- Allow phones (the web client and the Meshtastic app, on this network)

bool MeshApp::lan_on() {
    if (simulated()) return g_sim_lan;
    return std::system("systemctl is-active --quiet zeroos-mesh-lan.service") == 0;
}

void MeshApp::set_lan(bool on) {
    if (simulated()) {
        g_sim_lan = on;
    } else if (std::system(on ? "systemctl start zeroos-mesh-lan.service >/dev/null 2>&1"
                              : "systemctl stop zeroos-mesh-lan.service >/dev/null 2>&1") != 0) {
        shell_toast("Couldn't change it. Update ZeroOS and try again");
        return;
    }
    shell_toast(on ? "Phones on this network can connect" : "Only this device and Tailscale can connect");
    dirty_ = true;
}

// ---- Share my position

int MeshApp::share_level() {
    int v = prefs::get_int("mesh_share", 0);
    return v < 0 || v > 3 ? 0 : v;
}

void MeshApp::set_share_level(int level) {
    mesh::Client& c = client();
    if (!c.ready()) {
        shell_toast("The radio isn't connected yet");
        dirty_ = true;
        return;
    }
    prefs::set_int("mesh_share", level);
    write_daemon_conf();
    // How exact is a setting of the channel positions go out on: the first.
    if (const mesh::Channel* ch = c.channel(0)) {
        if (ch->settings.position_precision != kShareBits[level]) {
            mesh::Channel copy = *ch;
            copy.settings.position_precision = kShareBits[level];
            c.set_channel(copy);
        }
    }
    // A position typed into the radio by hand would win over the GPS.
    if (level > 0 && c.position_config().fixed_position) c.remove_fixed_position();
    gps_at_ms_ = 0;
    if (level == 0) shell_toast("Your position stays here");
    else shell_toast(std::string("Sharing where you are on ") + c.channel_name(0) + (level == 3 ? "" : ", roughly") + ", once the GPS has a fix");
}

// ---- The tab

void MeshApp::build_radio_tab() {
    mesh::Client& c = client();
    set_actions({hint("Select", "Enter"),
                 menu("About this radio", 'i', [this] {
                     View v;
                     v.page = Page::About;
                     push(v);
                 }),
                 menu("What it's hearing", 'l', [this] {
                     View v;
                     v.page = Page::Log;
                     push(v);
                 }),
                 menu("Restart the radio", 'r', [this] {
                     client().reboot();
                     shell_toast("Restarting the radio...");
                 }),
                 danger("Forget every node", 'n', "Forget every node but your favourites?", [this] {
                     client().reset_nodedb();
                     shell_toast("Nodes forgotten. They come back as they're heard");
                 }),
                 danger("Delete every message", 'd', "Delete every message in every chat?", [this] { delete_chat("*"); })});
    list_.build(root_, 4, kTabH + 1, kScreenW - 8, kBodyH - kTabH - 2);
    const mesh::Node* me = c.me();
    bool ready = c.ever_ready();
    value_rows_.clear();

    list_.add(LV_SYMBOL_EDIT, "Your name", me && me->has_user ? mesh::one_line(me->long_name + " (" + me->short_name + ")", 26) : "", [this] { edit_name(); });
    int n_channels = 0;
    std::string names;
    for (const mesh::Channel& ch : c.channels()) {
        if (ch.role == mesh::Channel::Disabled) continue;
        names += (n_channels++ ? ", " : "") + c.channel_name(uint32_t(ch.index));
    }
    list_.add(ZS_ICON_CHAT, "Channels", mesh::one_line(names, 26), [this] {
        View v;
        v.page = Page::Channels;
        push(v);
    });
    list_.add_choice(LV_SYMBOL_GPS, "Share my position", {kShareNames[0], kShareNames[1], kShareNames[2], kShareNames[3]}, share_level(),
                     [this](int at) { set_share_level(at); });
    bool lan = lan_on();
    list_.add_choice(ZS_ICON_PHONE, "Phones on this network", {"Blocked", "Allowed"}, lan ? 1 : 0, [this](int at) { set_lan(at == 1); });
    list_.add(LV_SYMBOL_IMAGE, "Use it from a phone", lan ? "QR code" : "Allow phones first", [this, lan] {
        if (!lan) {
            shell_toast("Set Phones on this network to Allowed first. Tailscale always can");
            return;
        }
        if (st_.web_url.empty()) {
            shell_toast("Connect to Wi-Fi first");
            return;
        }
        show_qr("Chat from a phone",
                "On the same Wi-Fi, scan to open Meshtastic's web page. Or in the Meshtastic app: add a network device, " + st_.api,
                st_.web_url);
    });
    list_.add(ZS_ICON_GLOBE, "Region", ready ? mesh::region_name(c.lora().region) : "", [this] { pick_region(false); });
    if (ready && c.needs_region()) list_.set_value_color(5, kWarn);
    list_.add(ZS_ICON_TOWER, "Range and speed", ready ? (c.lora().use_preset ? mesh::preset_name(c.lora().modem_preset) : "Custom") : "", [this] { pick_preset(); });
    list_.add(ZS_ICON_NETWORK, "Hops", ready ? fmt("%u", c.lora().hop_limit ? c.lora().hop_limit : 3) : "", [this] { pick_hops(); });
    list_.add(ZS_ICON_SLIDERS, "Role", ready ? mesh::role_name(c.device().role) : "", [this] { pick_role(); });
    list_.add_choice(LV_SYMBOL_UPLOAD, "Transmit", {"Off", "On"}, !ready || c.lora().tx_enabled ? 1 : 0, [this](int at) {
        mesh::Client& cl = client();
        if (!cl.ready()) {
            shell_toast("The radio isn't connected yet");
            return;
        }
        mesh::LoRaConfig l = cl.lora();
        if (l.tx_enabled == (at == 1)) return;
        l.tx_enabled = at == 1;
        cl.set_lora(l);
        saved_note(true);
    });
    list_.add(LV_SYMBOL_IMAGE, "Open on monitor", "", [] {
        if (!power().docked()) {
            shell_toast("Needs Dock mode (a monitor)");
            return;
        }
        hal().dock_open("browser", "https://localhost:9443");
        shell_toast("Opening Meshtastic on the monitor");
    });
    list_.add(LV_SYMBOL_LIST, "What it's hearing", "", [this] {
        View v;
        v.page = Page::Log;
        push(v);
    });
    list_.add(LV_SYMBOL_SETTINGS, "About this radio", ready ? c.metadata().firmware_version : "", [this] {
        View v;
        v.page = Page::About;
        push(v);
    });
    list_.add(LV_SYMBOL_GPS, "GPS", "", [] { shell_open_app("maps", "gps"); });
    list_.add(LV_SYMBOL_POWER, "Turn off mesh", "", [this] {
        stop_mesh();
        shell_toast("Mesh off");
    });
    list_.finish();
    if (!focus_key_.empty()) list_.focus(std::atoi(focus_key_.c_str()));
    focus_key_.clear();
    seen_sig_ = std::to_string(ready) + (me ? me->long_name + me->short_name : "") + names + std::to_string(lan) +
                std::to_string(c.lora().region) + std::to_string(c.lora().modem_preset) + std::to_string(c.lora().hop_limit) +
                std::to_string(c.device().role) + std::to_string(c.lora().tx_enabled) + c.metadata().firmware_version;
}

void MeshApp::refresh_radio_tab() {
    mesh::Client& c = client();
    const mesh::Node* me = c.me();
    std::string names;
    int n_channels = 0;
    for (const mesh::Channel& ch : c.channels()) {
        if (ch.role == mesh::Channel::Disabled) continue;
        names += (n_channels++ ? ", " : "") + c.channel_name(uint32_t(ch.index));
    }
    // (Allow phones isn't looked at again here: asking systemd costs too much
    // to do on every change. Its row is right from when the tab was drawn.)
    std::string lan = seen_sig_.empty() ? "" : std::string(1, seen_sig_.find(names) != std::string::npos && !names.empty() ? seen_sig_[seen_sig_.find(names) + names.size()] : '0');
    std::string sig = std::to_string(c.ever_ready()) + (me ? me->long_name + me->short_name : "") + names + lan +
                      std::to_string(c.lora().region) + std::to_string(c.lora().modem_preset) + std::to_string(c.lora().hop_limit) +
                      std::to_string(c.device().role) + std::to_string(c.lora().tx_enabled) + c.metadata().firmware_version;
    if (sig == seen_sig_ || !list_.filter().empty()) return;
    focus_key_ = std::to_string(list_.focused());   // the same row, after the redraw
    dirty_ = true;
}

// ---- Your name

void MeshApp::edit_name() {
    mesh::Client& c = client();
    if (!c.ready()) {
        shell_toast("The radio isn't connected yet");
        return;
    }
    const mesh::Node* me = c.me();
    std::string long_name = me ? me->long_name : "", short_name = me ? me->short_name : "";
    prompt("Your name on the mesh", "Trail Runner", long_name, "Up to 39 letters. Everyone who hears your radio sees it.", 39,
           [this, long_name, short_name](const std::string& name) {
               if (name.empty()) return;
               // The short one: theirs if the long one didn't change, else made from the new name.
               std::string suggest = name == long_name && !short_name.empty() ? short_name : short_from(name);
               prompt("Short name", "TRUN", suggest, "Up to 4 letters: what's shown beside your messages and on small screens.", 4,
                      [this, name](const std::string& brief) {
                          client().set_owner(name, brief.empty() ? short_from(name) : brief);
                          saved_note(false);
                      });
           });
}

// ---- Region, preset, hops, role

void MeshApp::pick_region(bool setup) {
    mesh::Client& c = client();
    std::vector<Option> opts;
    int focus = -1, i = 0;
    uint32_t current = setup ? setup_region_ : c.lora().region;
    for (const mesh::Named& r : mesh::regions()) {
        Option o;
        o.text = r.label;
        o.value = r.name;
        uint32_t v = r.value;
        if (v == current) focus = i;
        o.choose = [this, setup, v] {
            if (setup) {
                setup_region_ = v;
                dirty_ = true;
                return;
            }
            mesh::Client& cl = client();
            if (!cl.ready()) {
                shell_toast("The radio isn't connected yet");
                return;
            }
            mesh::LoRaConfig l = cl.lora();
            if (l.region == v) return;
            if (l.region == 0) l.tx_enabled = true;   // the first time: on the air
            l.region = v;
            cl.set_lora(l);
            saved_note(true);
        };
        opts.push_back(std::move(o));
        ++i;
    }
    pick("Where are you?", std::move(opts), focus, "Sets the legal frequency");
}

void MeshApp::pick_preset() {
    mesh::Client& c = client();
    if (!c.ready()) {
        shell_toast("The radio isn't connected yet");
        return;
    }
    std::vector<Option> opts;
    int focus = -1, i = 0;
    for (const mesh::Named& p : mesh::modem_presets()) {
        Option o;
        o.text = p.name;
        o.value = p.label;
        uint32_t v = p.value;
        if (c.lora().use_preset && v == c.lora().modem_preset) focus = i;
        o.choose = [this, v] {
            mesh::LoRaConfig l = client().lora();
            if (l.use_preset && l.modem_preset == v) return;
            l.use_preset = true;
            l.modem_preset = v;
            client().set_lora(l);
            saved_note(true);
        };
        opts.push_back(std::move(o));
        ++i;
    }
    pick("Range and speed", std::move(opts), focus, "Must match the others'");
}

void MeshApp::pick_hops() {
    mesh::Client& c = client();
    if (!c.ready()) {
        shell_toast("The radio isn't connected yet");
        return;
    }
    static const char* const kNotes[] = {"", "neighbours only", "", "the default", "", "", "", "the most: use with care"};
    std::vector<Option> opts;
    uint32_t current = c.lora().hop_limit ? c.lora().hop_limit : 3;
    for (uint32_t h = 1; h <= 7; ++h) {
        Option o;
        o.text = fmt("%u hop%s", h, h == 1 ? "" : "s");
        o.value = kNotes[h];
        o.choose = [this, h] {
            mesh::LoRaConfig l = client().lora();
            if (l.hop_limit == h) return;
            l.hop_limit = h;
            client().set_lora(l);
            saved_note(true);
        };
        opts.push_back(std::move(o));
    }
    pick("How many nodes may pass a message on", std::move(opts), int(current) - 1);
}

void MeshApp::pick_role() {
    mesh::Client& c = client();
    if (!c.ready()) {
        shell_toast("The radio isn't connected yet");
        return;
    }
    std::vector<Option> opts;
    int focus = -1, i = 0;
    for (const mesh::Named& r : mesh::device_roles()) {
        Option o;
        o.text = r.name;
        o.value = r.label;
        uint32_t v = r.value;
        if (v == c.device().role) focus = i;
        o.choose = [this, v] {
            auto apply = [this, v] {
                mesh::DeviceConfig d = client().device();
                if (d.role == v) return;
                d.role = v;
                client().set_device(d);
                saved_note(true);
            };
            // A router in a pocket makes the mesh worse for everyone near it.
            if (v == 2 || v == 11) shell_confirm("Make this a router?", "Routers are for fixed nodes on high ground with a good antenna.", "Router", apply);
            else apply();
        };
        opts.push_back(std::move(o));
        ++i;
    }
    pick("Role", std::move(opts), focus, "Client suits a handheld");
}

// ---- Channels

void MeshApp::new_channel() {
    mesh::Client& c = client();
    if (!c.ready()) {
        shell_toast("The radio isn't connected yet");
        return;
    }
    if (c.free_channel_slot() < 0) {
        shell_toast("A radio holds 8 channels. Delete one first");
        return;
    }
    prompt("Name the new channel", "Friends", "", "Up to 11 letters. It gets a key of its own: share it with a QR code.", 11,
           [this](const std::string& name) {
               if (name.empty()) return;
               mesh::Client& cl = client();
               int slot = cl.free_channel_slot();
               if (slot < 0) return;
               mesh::Channel ch;
               ch.index = slot;
               ch.role = mesh::Channel::Secondary;
               ch.settings.name = name;
               ch.settings.psk = random_key();
               cl.set_channel(ch);
               shell_toast("Channel made. Share it from its page");
               View v;
               v.page = Page::ChannelPage;
               v.index = slot;
               push(v);
           });
}

void MeshApp::build_channels() {
    mesh::Client& c = client();
    set_actions({hint("Open", "Enter"), hint("Back", "Esc"),
                 menu("New channel", 'n', [this] { new_channel(); }),
                 menu("Join with a QR code", 'q', [this] {
                     View v;
                     v.page = Page::Scan;
                     push(v);
                 }),
                 menu("Join with a link", 'j', [this] {
                     prompt("Paste or type the link", "https://meshtastic.org/e/#...", "", "The part after # is enough. A QR code is easier: Tab, Q.", 400,
                            [this](const std::string& link) {
                                if (!link.empty()) join_from_link(link);
                            });
                 }),
                 menu("Share all of them", 's', [this] {
                     std::string url = client().share_url(-1);
                     if (url.empty()) return;
                     show_qr("All channels", "Whoever scans this gets every channel here and your radio's preset, in place of theirs.", url);
                 })});
    title_row("Channels", fmt("%d free", [&c] {
                  int n = 0;
                  for (size_t i = 1; i < c.channels().size(); ++i)
                      if (c.channels()[i].role == mesh::Channel::Disabled) ++n;
                  return n;
              }()));
    list_.build(root_, 4, 17, kScreenW - 8, kBodyH - 18);
    for (const mesh::Channel& ch : c.channels()) {
        if (ch.role == mesh::Channel::Disabled) continue;
        int index = ch.index;
        std::string value = mesh::psk_kind(ch.settings.psk);
        if (ch.role == mesh::Channel::Primary) value += ", first";
        list_.add(ZS_ICON_CHAT, c.channel_name(uint32_t(index)), value, [this, index] {
            View v;
            v.page = Page::ChannelPage;
            v.index = index;
            push(v);
        });
    }
    list_.add(LV_SYMBOL_PLUS, "New channel...", "", [this] { new_channel(); });
    list_.add(ZS_ICON_CAMERA, "Join with a QR code...", "", [this] {
        View v;
        v.page = Page::Scan;
        push(v);
    });
    list_.finish();
}

void MeshApp::build_channel_page() {
    mesh::Client& c = client();
    int index = stack_.back().index;
    const mesh::Channel* ch = c.channel(uint32_t(index));
    if (!ch) {
        // Deleted (here or from a phone): nothing to show.
        set_actions({hint("Back", "Esc")});
        title_row("Channel");
        lv_obj_t* l = label(root_, "This channel isn't on the radio any more.", &zs_font_12, kMuted);
        lv_obj_set_pos(l, 8, 24);
        return;
    }
    bool primary = ch->role == mesh::Channel::Primary;
    std::string name = c.channel_name(uint32_t(index));
    std::vector<Action> acts = {hint("Select", "Enter"), hint("Back", "Esc")};
    if (!primary) {
        acts.push_back(danger("Delete this channel", 'd', "Delete " + name + " and its messages here?", [this, index] {
            delete_chat(mesh::chat_key_channel(uint32_t(index)));
            client().delete_channel(uint32_t(index));
            pop();
        }));
    }
    set_actions(std::move(acts));
    title_row(name, primary ? "The first channel" : fmt("Channel %d", index + 1));
    list_.build(root_, 4, 17, kScreenW - 8, kBodyH - 18);
    list_.add(LV_SYMBOL_IMAGE, "Share with a QR code", "", [this, index, name] {
        std::string url = client().share_url(index);
        if (url.empty()) return;
        show_qr(name, "Scan with a phone's camera, or Mesh on another Cardputer (Radio, Channels, Join with a QR code). It adds this channel.", url);
    });
    list_.add(ZS_ICON_CHAT, "Open its chat", "", [this, index] { open_chat(mesh::chat_key_channel(uint32_t(index))); });
    list_.add(LV_SYMBOL_EDIT, "Name", ch->settings.name.empty() ? "the preset's" : ch->settings.name, [this, index] {
        const mesh::Channel* cur = client().channel(uint32_t(index));
        if (!cur) return;
        prompt("Channel name", "Friends", cur->settings.name,
               cur->role == mesh::Channel::Primary ? "Leave it empty to stay on the public channel named after the preset."
                                                   : "Up to 11 letters. Everyone on it must use the same name and key.",
               11, [this, index](const std::string& text) {
                   const mesh::Channel* now_ch = client().channel(uint32_t(index));
                   if (!now_ch || (text.empty() && now_ch->role != mesh::Channel::Primary)) return;
                   mesh::Channel copy = *now_ch;
                   copy.settings.name = text;
                   client().set_channel(copy);
                   saved_note(false);
               });
    });
    list_.add(LV_SYMBOL_SETTINGS, "Key", mesh::psk_kind(ch->settings.psk), [this, index, primary] {
        std::vector<Option> opts;
        Option fresh;
        fresh.text = "A new key";
        fresh.value = "private: share it again";
        fresh.choose = [this, index] {
            const mesh::Channel* cur = client().channel(uint32_t(index));
            if (!cur) return;
            mesh::Channel copy = *cur;
            copy.settings.psk = random_key();
            client().set_channel(copy);
            shell_toast("New key. Share the channel again: the old QR code no longer works");
            dirty_ = true;
        };
        opts.push_back(fresh);
        Option def;
        def.text = "The default key";
        def.value = "public: anyone can read it";
        def.choose = [this, index] {
            const mesh::Channel* cur = client().channel(uint32_t(index));
            if (!cur) return;
            mesh::Channel copy = *cur;
            copy.settings.psk = std::string("\x01", 1);
            client().set_channel(copy);
            saved_note(false);
        };
        opts.push_back(def);
        pick(primary ? "Key (changing it leaves the public mesh)" : "Key", std::move(opts));
    });
    list_.add_choice(LV_SYMBOL_UPLOAD, "Send to the internet (MQTT)", {"No", "Yes"}, ch->settings.uplink_enabled ? 1 : 0, [this, index](int at) {
        const mesh::Channel* cur = client().channel(uint32_t(index));
        if (!cur) return;
        mesh::Channel copy = *cur;
        copy.settings.uplink_enabled = at == 1;
        client().set_channel(copy);
    });
    list_.finish();
}

// A shared link, from a QR code or typed: add its channels, or take them all.
void MeshApp::join_from_link(const std::string& link) {
    mesh::ChannelSet set;
    if (!mesh::parse_channel_url(link, set)) {
        shell_toast("That isn't a Meshtastic channel link");
        return;
    }
    mesh::Client& c = client();
    if (!c.ready()) {
        shell_toast("The radio isn't connected yet");
        return;
    }
    std::string names;
    for (const mesh::ChannelSettings& s : set.settings) {
        std::string n = s.name.empty() ? std::string(mesh::preset_name(set.has_lora ? set.lora.modem_preset : c.lora().modem_preset)) : s.name;
        names += (names.empty() ? "" : ", ") + n;
    }
    std::vector<Option> opts;
    Option add;
    add.text = set.settings.size() == 1 ? "Add it" : "Add them";
    add.value = "keeps your channels";
    add.choose = [this, set] {
        std::string why;
        if (client().add_channels(set, &why)) shell_toast(set.settings.size() == 1 ? "Channel added" : "Channels added");
        else shell_toast(why);
        dirty_ = true;
    };
    opts.push_back(add);
    if (!set.add_only) {
        Option all;
        all.text = "Replace all of mine";
        all.value = "and the radio's preset";
        all.choose = [this, set] {
            shell_confirm("Replace every channel?", "Your channels and their messages here go. The radio takes the link's settings and restarts.",
                          "Replace", [this, set] {
                              for (const mesh::Channel& ch : client().channels())
                                  if (ch.role != mesh::Channel::Disabled) session().forget_chat(mesh::chat_key_channel(uint32_t(ch.index)), now());
                              client().replace_channels(set);
                              saved_note(true);
                          });
        };
        opts.push_back(all);
    }
    Option no;
    no.text = "Don't";
    no.choose = [] {};
    opts.push_back(no);
    pick(mesh::one_line("Join " + names + "?", 40), std::move(opts));
}

// ---- A QR code to show, and reading one

void MeshApp::show_qr(const std::string& title, const std::string& note, const std::string& text) {
    qr_title_ = title;
    qr_note_ = note;
    View v;
    v.page = Page::ShowQr;
    v.key = text;
    push(v);
}

void MeshApp::build_show_qr() {
    set_actions({hint("Back", "Esc")});
    const std::string& text = stack_.back().key;
    lv_obj_t* qr = lv_qrcode_create(root_);
    lv_qrcode_set_size(qr, 124);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xffffff));
    lv_qrcode_update(qr, text.c_str(), uint32_t(text.size()));
    lv_obj_set_style_border_color(qr, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(qr, 4, 0);
    lv_obj_set_pos(qr, 4, 2);
    lv_obj_t* t = label(root_, qr_title_.c_str(), &zs_font_14, kAccent);
    lv_obj_set_pos(t, 142, 2);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, kScreenW - 148);
    lv_obj_t* n = label(root_, qr_note_.c_str(), &zs_font_12, kMuted);
    lv_obj_set_pos(n, 142, 22);
    lv_obj_set_width(n, kScreenW - 148);
    lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);
}

namespace {
constexpr int kViewW = 160, kViewH = 120;
}

void MeshApp::build_scan() {
    set_actions({hint("Back", "Esc")});
    scan_pix_.assign(size_t(kViewW) * kViewH, 0);
    scan_dsc_ = lv_image_dsc_t{};
    scan_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    scan_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    scan_dsc_.header.w = kViewW;
    scan_dsc_.header.h = kViewH;
    scan_dsc_.header.stride = kViewW * 2;
    scan_dsc_.data = reinterpret_cast<const uint8_t*>(scan_pix_.data());
    scan_dsc_.data_size = uint32_t(scan_pix_.size() * 2);
    scan_img_ = lv_image_create(root_);
    lv_image_set_src(scan_img_, &scan_dsc_);
    lv_obj_set_pos(scan_img_, 4, 8);
    lv_obj_t* t = label(root_, "Join a channel", &zs_font_14, kAccent);
    lv_obj_set_pos(t, kViewW + 12, 8);
    scan_note_ = label(root_, "Starting the camera...", &zs_font_12, kMuted);
    lv_obj_set_pos(scan_note_, kViewW + 12, 30);
    lv_obj_set_width(scan_note_, kScreenW - kViewW - 18);
    lv_label_set_long_mode(scan_note_, LV_LABEL_LONG_WRAP);
    scan_seq_ = 0;
    scan_frames_ = 0;
    scan_done_ = false;
    scanning_ = true;
    hal().scan_start();
}

void MeshApp::scan_tick() {
    if (!scanning_ || scan_done_ || !scan_img_) return;
    CameraFrame f;
    bool got = hal().scan_frame(f);
    CamState st = hal().scan_state();
    if (st == CamState::NoCamera) {
        lv_label_set_text(scan_note_, "No camera found. Join with a link instead: Esc, then Tab, J.");
        return;
    }
    if (!got || f.seq == scan_seq_ || !f.y || f.w <= 0 || f.h <= 0) return;
    if (!scan_seq_) lv_label_set_text(scan_note_, "Point it at the channel's QR code, on a phone (Meshtastic: Share channel) or another Cardputer.");
    scan_seq_ = f.seq;
    for (int y = 0; y < kViewH; ++y) {
        const uint8_t* line = f.y + size_t(y * f.h / kViewH) * size_t(f.w);
        uint16_t* out = &scan_pix_[size_t(y) * kViewW];
        for (int x = 0; x < kViewW; ++x) {
            uint8_t g = line[x * f.w / kViewW];   // the brightness, as grey
            out[x] = uint16_t(((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
        }
    }
    lv_obj_invalidate(scan_img_);
    if (++scan_frames_ % 3) return;   // reading costs more than drawing
    QrResult r = qr_decode(f.y, f.w, f.h);
    if (!r.found || r.text.empty()) return;
    mesh::ChannelSet set;
    if (!mesh::parse_channel_url(r.text, set)) {
        lv_label_set_text(scan_note_, "That's a QR code, but not a Meshtastic channel.");
        return;
    }
    scan_done_ = true;
    hal().beep(1800, 40);
    std::string link = r.text;
    pop();   // the camera page; clear_screen() lets the camera go
    join_from_link(link);
}

// ---- What the radio is hearing, and about it

std::string MeshApp::log_text() {
    mesh::Client& c = client();
    std::string s;
    size_t n = c.log().size(), from = n > 60 ? n - 60 : 0;
    for (size_t i = from; i < n; ++i) {
        const mesh::LogLine& l = c.log()[i];
        time_t t = time_t(l.time);
        struct tm tmv {};
        localtime_r(&t, &tmv);
        char when[16];
        std::strftime(when, sizeof when, "%H:%M:%S", &tmv);
        s += when;
        if (l.out) {
            s += "  You > " + (l.to == c.my_num() ? std::string("radio") : c.node_name(l.to)) + "  " + l.what;
        } else {
            s += "  " + (l.from == 0 || l.from == c.my_num() ? std::string("radio") : c.node_name(l.from)) + "  " + l.what;
            if (l.hops == 0 && (l.snr != 0 || l.rssi != 0)) s += fmt("  %+.1f dB", double(l.snr));
            else if (l.hops > 0) s += fmt("  %d hop%s", l.hops, l.hops == 1 ? "" : "s");
        }
        s += "\n";
    }
    if (s.empty()) s = "Nothing yet. Each packet the radio hears or sends shows here.";
    return s;
}

void MeshApp::build_log() {
    set_actions({hint("Back", "Esc"), hint("Scroll", LV_SYMBOL_UP LV_SYMBOL_DOWN)});
    build_text_page("What the radio is hearing", log_text());
    lv_obj_t* panel = lv_obj_get_parent(live_label_);
    lv_obj_update_layout(panel);
    lv_obj_scroll_to_y(panel, LV_COORD_MAX, LV_ANIM_OFF);
}

void MeshApp::refresh_log() {
    seen_log_ = client().log_rev();
    if (!live_label_) return;
    lv_obj_t* panel = lv_obj_get_parent(live_label_);
    bool at_end = lv_obj_get_scroll_bottom(panel) <= 4;   // scrolled back to read: leave it there
    lv_label_set_text(live_label_, log_text().c_str());
    if (at_end) {
        lv_obj_update_layout(panel);
        lv_obj_scroll_to_y(panel, LV_COORD_MAX, LV_ANIM_OFF);
    }
}

std::string MeshApp::about_text() {
    mesh::Client& c = client();
    std::string s;
    auto row = [&s](const std::string& k, const std::string& v) {
        if (!v.empty()) s += k + ": " + v + "\n";
    };
    const mesh::Node* me = c.me();
    if (!c.ever_ready()) return "Waiting for the radio...";
    row("Node", me ? me->full_name() + " (" + me->name() + ")" : "");
    row("ID", mesh::node_id(c.my_num()));
    row("Firmware", "meshtasticd " + c.metadata().firmware_version);
    row("Region", mesh::region_name(c.lora().region));
    row("Preset", c.lora().use_preset ? mesh::preset_name(c.lora().modem_preset) : "Custom");
    row("Transmit", c.lora().tx_enabled ? (c.lora().tx_power ? fmt("on, %d dBm", c.lora().tx_power) : "on, the most the region allows") : "off");
    if (me && me->has_metrics) {
        if (me->metrics.has_ch_util) row("Channel in use", fmt("%.1f%% (busy above 25%%)", double(me->metrics.channel_utilization)));
        if (me->metrics.has_air_util) row("Your airtime", fmt("%.1f%% of the last hour", double(me->metrics.air_util_tx)));
        if (me->metrics.has_uptime) row("Radio up for", mesh::duration_text(long(me->metrics.uptime_seconds)));
    }
    if (c.has_stats()) {
        const mesh::LocalStats& st = c.stats();
        row("Packets", fmt("%u sent, %u heard, %u bad", st.num_packets_tx, st.num_packets_rx, st.num_packets_rx_bad));
        row("Passed on", fmt("%u (%u not needed)", st.num_tx_relay, st.num_tx_relay_canceled));
        if (st.noise_floor) row("Noise floor", fmt("%d dBm", st.noise_floor));
    }
    int total = int(c.nodes().size()) - 1;
    row("Nodes", fmt("%d heard in 2 hours, %d known", c.online_count(now()), total < 0 ? 0 : total));
    if (c.queue().maxlen) row("Send queue", fmt("%u of %u free", c.queue().free, c.queue().maxlen));
    row("Position", share_level() ? std::string("shared, ") + kShareNames[share_level()] : "not shared");
    s += "\nMessages are kept in ~/.local/share/zeroos/mesh, on this Cardputer only.";
    if (!st_.api.empty()) s += "\nMeshtastic app or command line: " + st_.api + ", port 4403.";
    return s;
}

void MeshApp::build_about() {
    set_actions({hint("Back", "Esc"), hint("Scroll", LV_SYMBOL_UP LV_SYMBOL_DOWN)});
    build_text_page("About this radio", about_text());
}

}  // namespace zs
