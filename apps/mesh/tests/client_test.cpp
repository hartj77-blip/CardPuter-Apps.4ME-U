// The client against the simulated radio, through a Session as the app runs
// it in the simulator: the settings download, sending and what becomes of a
// message, asking nodes things, changing the radio, channels, history.
#include <cstdio>
#include <string>

#include "apps/mesh/core/session.h"
#include "apps/mesh/core/text.h"

using namespace zs::mesh;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

namespace {

struct Rig {
    Session::Options opt;
    std::unique_ptr<Session> s;
    long ms = 0;
    long epoch = 1795000000;

    explicit Rig(bool region = true, bool neighbours = true, bool chatter = false) {
        opt.simulated = true;
        opt.seed_history = false;
        opt.fake.region_set = region;
        opt.fake.neighbours = neighbours;
        opt.fake.chatter = chatter;
        s = std::make_unique<Session>(opt);
        s->resume();
        run(1000);
    }
    long now() const { return epoch + ms / 1000; }
    void run(long for_ms) {
        for (long end = ms + for_ms; ms < end; ms += 100) s->tick(now(), ms);
    }
    Client& c() { return s->client(); }
    FakeRadio& radio() { return *s->fake(); }
    const Message* last(const std::string& key) {
        const Chat* chat = c().chat(key);
        return chat && !chat->msgs.empty() ? &chat->msgs.back() : nullptr;
    }
    const Message* by_id(const std::string& key, uint32_t id) {
        const Chat* chat = c().chat(key);
        if (!chat) return nullptr;
        for (const Message& m : chat->msgs)
            if (m.id == id) return &m;
        return nullptr;
    }
    std::string notices() {
        std::string all;
        for (const std::string& n : c().take_notices()) all += n + "\n";
        return all;
    }
};

void test_download() {
    Rig r;
    Client& c = r.c();
    CHECK(c.linked() && c.ready() && c.ever_ready());
    CHECK(c.my_num() == FakeRadio::kMe);
    CHECK(c.me() && c.me()->long_name == "Cardputer Zero" && c.me()->short_name == "ZERO");
    CHECK(c.nodes().size() == 10);
    CHECK(c.has_lora() && c.lora().region == 3 && c.lora().use_preset && c.lora().hop_limit == 3);
    CHECK(!c.needs_region() && c.can_transmit());
    CHECK(c.metadata().firmware_version == "2.7.26.sim");
    CHECK(c.channel(0) && c.channel(1) && !c.channel(2));
    CHECK(c.channel_name(0) == "LongFast" && c.channel_name(1) == "Friends" && c.channel_name(5) == "Channel 5");
    CHECK(c.free_channel_slot() == 2);
    // A chat row for each channel, before anyone has said anything.
    CHECK(c.chats().size() == 2 && c.chats()[0].key == "c0" && c.chats()[1].key == "c1");
    CHECK(c.chat_title(c.chats()[0]) == "LongFast");

    uint32_t hill = r.radio().node_by_short("HILL"), mrk = r.radio().node_by_short("MRK");
    const Node* h = c.node(hill);
    CHECK(h && h->name() == "HILL" && h->full_name() == "Hilltop Relay" && h->hops == 0 && h->has_snr);
    CHECK(h && h->has_pos && h->has_metrics && h->metrics.battery_level == 101 && h->has_key);
    const Node* m = c.node(mrk);
    CHECK(m && m->hops == 2 && !m->has_snr);
    const Node* anon = c.node(0x9f3c21aa);
    CHECK(anon && !anon->has_user && anon->name() == "!9f3c21aa");
    CHECK(c.node_name(0x12345678) == "!12345678" && c.node_name(kBroadcast) == "everyone");
    CHECK(c.online_count(r.now()) == 7);   // not the two last heard hours and days ago, nor ourselves
    double lat, lon;
    CHECK(!c.here(lat, lon));
    c.set_here(true, 46.6, 8.6);
    CHECK(c.here(lat, lon) && lat == 46.6);
}

void test_channel_message() {
    Rig r;
    Client& c = r.c();
    uint32_t id = c.send_text("c0", "Radio check");
    CHECK(id != 0);
    r.run(200);
    const Message* m = r.last("c0");
    CHECK(m && m->mine && m->text == "Radio check" && m->id == id);
    CHECK(m && (m->state == Delivery::Queued || m->state == Delivery::Sent));
    CHECK(m && std::string(delivery_text(*m, false)) == "Sending...");
    r.run(1500);
    m = r.last("c0");
    CHECK(m && m->state == Delivery::Relayed && std::string(delivery_text(*m, false)) == "Heard by the mesh");
    CHECK(r.radio().texts_taken() == 1);
    CHECK(r.radio().sent_to_mesh().back().hop_limit == 3 && r.radio().sent_to_mesh().back().want_ack);
    CHECK(c.total_unread() == 0);

    // Someone else speaks: unread until the chat is looked at.
    uint32_t anna = r.radio().node_by_short("ANNA");
    r.radio().inject_text(anna, kBroadcast, 0, "Loud and clear");
    r.run(300);
    m = r.last("c0");
    CHECK(m && !m->mine && m->from == anna && m->text == "Loud and clear" && m->hops == 0 && m->rssi == -103);
    CHECK(c.chat("c0")->unread == 1 && c.total_unread() == 1);
    c.mark_read("c0", r.now());
    CHECK(c.total_unread() == 0);
    // The same packet again (the radio repeats itself after a reconnect) isn't a second message.
    size_t n = c.chat("c0")->msgs.size();
    MeshPacket again;
    again.from = anna;
    again.to = kBroadcast;
    again.id = 0x7777;
    again.has_decoded = true;
    again.decoded.portnum = kPortText;
    again.decoded.payload = "Once";
    r.radio().inject(again);
    r.radio().inject(again);
    r.run(300);
    CHECK(c.chat("c0")->msgs.size() == n + 1);

    // A reaction lands on its message, not as a message.
    r.radio().inject_text(anna, kBroadcast, 0, "\xF0\x9F\x91\x8D", id, 1);
    r.run(300);
    CHECK(c.chat("c0")->msgs.size() == n + 1);
    m = r.by_id("c0", id);
    CHECK(m && m->reactions.size() == 1 && m->reactions[0].from == anna);
    CHECK(m && reaction_text(m->reactions[0].text) == "+1");
    // Ours, on theirs.
    CHECK(c.send_reaction("c0", 0x7777, "\xE2\x9D\xA4") != 0);
    r.run(3500);
    m = r.by_id("c0", 0x7777);
    CHECK(m && m->reactions.size() == 1 && m->reactions[0].from == FakeRadio::kMe);
    CHECK(r.radio().sent_to_mesh().back().decoded.emoji == 1 && r.radio().sent_to_mesh().back().decoded.reply_id == 0x7777);

    // Other channel, with the bell.
    r.run(3000);
    CHECK(c.send_text("c1", "Dinner?", 0, true) != 0);
    r.run(300);
    CHECK(r.radio().sent_to_mesh().back().channel == 1 && r.radio().sent_to_mesh().back().decoded.payload == "Dinner?\a");
    m = r.last("c1");
    CHECK(m && m->bell && display_text(m->text) == "Dinner?");
    // A channel that isn't there.
    CHECK(c.send_text("c5", "x") == 0 && !c.last_error().empty());
    CHECK(c.send_text("c0", "") == 0);
}

void test_direct_message() {
    Rig r;
    Client& c = r.c();
    uint32_t anna = r.radio().node_by_short("ANNA"), mrk = r.radio().node_by_short("MRK"),
             sol = r.radio().node_by_short("SOL7");
    std::string ka = chat_key_dm(anna), km = chat_key_dm(mrk), ks = chat_key_dm(sol);
    CHECK(c.open_dm(anna).key == ka && c.chat_title(*c.chat(ka)) == "Anna's T-Deck");

    // Heard directly: the node itself confirms.
    uint32_t id = c.send_text(ka, "Are you up?");
    r.run(2000);
    const Message* m = r.by_id(ka, id);
    CHECK(m && m->state == Delivery::Delivered && std::string(delivery_text(*m, true)) == "Delivered");
    CHECK(r.radio().sent_to_mesh().back().to == anna);

    // Two hops away: first a relay is heard, then the node's own ack.
    r.run(3000);
    id = c.send_text(km, "Marek?");
    r.run(1000);
    m = r.by_id(km, id);
    CHECK(m && m->state == Delivery::Relayed && std::string(delivery_text(*m, true)) == "Passed on, not confirmed");
    r.run(3000);
    m = r.by_id(km, id);
    CHECK(m && m->state == Delivery::Delivered);

    // Out of reach: the radio gives up and says so.
    r.run(3000);
    id = c.send_text(ks, "Hello?");
    r.run(9000);
    m = r.by_id(ks, id);
    CHECK(m && m->state == Delivery::Failed && m->error == kErrMaxRetransmit);
    CHECK(m && std::string(delivery_text(*m, true)) == "No one confirmed it");

    // One to us opens a chat of its own, unread.
    r.radio().inject_text(mrk, FakeRadio::kMe, 0, "Back at 5");
    r.run(300);
    m = r.last(km);
    CHECK(m && !m->mine && m->text == "Back at 5" && m->hops == 2);
    CHECK(c.chat(km)->unread == 1);
    // DMs come after the channels, newest first.
    CHECK(c.chats().size() == 5 && !c.chats()[0].dm && !c.chats()[1].dm && c.chats()[2].key == km);
}

void test_send_queue() {
    // The radio refuses a second text within two seconds. Three typed at
    // once all get through, spaced out, in order.
    Rig r;
    Client& c = r.c();
    uint32_t a = c.send_text("c0", "one"), b = c.send_text("c0", "two"), d = c.send_text("c0", "three");
    CHECK(a && b && d);
    r.run(500);
    CHECK(r.radio().texts_taken() == 1);
    r.run(9000);
    CHECK(r.radio().texts_taken() == 3);
    const Chat* chat = c.chat("c0");
    CHECK(chat && chat->msgs.size() == 3);
    if (chat && chat->msgs.size() == 3) {
        CHECK(chat->msgs[0].text == "one" && chat->msgs[1].text == "two" && chat->msgs[2].text == "three");
        for (const Message& m : chat->msgs) CHECK(m.state == Delivery::Relayed);
    }
    // Too long: cut at a character, never mid-way.
    std::string big(190, 'x');
    big += "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";   // 204 bytes
    r.run(3000);
    CHECK(c.send_text("c0", big) != 0);
    r.run(300);
    const std::string& sent = r.radio().sent_to_mesh().back().decoded.payload;
    CHECK(sent.size() == 200 && (uint8_t(sent.back()) & 0xC0) == 0x80);
}

void test_alone() {
    // No one in range: nothing repeats it, the radio gives up.
    Rig r(true, false);
    Client& c = r.c();
    CHECK(c.nodes().size() == 1 && c.online_count(r.now()) == 0);
    uint32_t id = c.send_text("c0", "Anyone?");
    r.run(7000);
    const Message* m = r.by_id("c0", id);
    CHECK(m && m->state == Delivery::Failed && m->error == kErrMaxRetransmit);
}

void test_no_region() {
    Rig r(false);
    Client& c = r.c();
    CHECK(c.ready() && c.needs_region() && !c.can_transmit());
    CHECK(c.send_text("c0", "hi") == 0);
    CHECK(c.last_error().find("region") != std::string::npos);
    CHECK(r.radio().texts_taken() == 0);

    // First-run setup: a name and a region, one save, one restart.
    LoRaConfig l = c.lora();
    l.region = 1;
    l.use_preset = true;
    l.modem_preset = 4;
    c.begin_edit();
    c.set_owner("Trail Runner", "TRUN");
    c.set_lora(l);
    c.commit_edit();
    r.run(500);
    CHECK(r.radio().restarts() == 0 || r.radio().restarts() == 1);
    r.run(1000);
    CHECK(r.radio().restarts() == 1);
    CHECK(!c.linked() && !c.ready() && c.ever_ready());   // it went away...
    r.run(4000);
    CHECK(c.linked() && c.ready());                       // ... and came back with the new settings
    CHECK(c.lora().region == 1 && c.lora().modem_preset == 4 && c.lora().tx_enabled);
    CHECK(c.me() && c.me()->long_name == "Trail Runner" && c.me()->short_name == "TRUN");
    CHECK(r.radio().long_name() == "Trail Runner" && r.radio().lora().region == 1);
    CHECK(c.channel_name(0) == "MediumFast");             // the default channel follows the preset
    CHECK(!c.needs_region() && c.can_transmit());
    CHECK(c.send_text("c0", "hi") != 0);
    r.run(1500);
    CHECK(r.radio().texts_taken() == 1);
    CHECK(r.radio().config_downloads() == 2);
}

void test_region_refused_by_radio() {
    // A client that sends anyway (the phone app, through the daemon) with no
    // region: the radio's queue says no, and nothing else ever comes.
    FakeRadio::Options o;
    o.region_set = false;
    FakeRadio radio(o);
    Recorder rec;
    std::vector<Record> out;
    FromRadio mine;
    mine.which = FromRadio::MyInfo;
    mine.my_info.my_node_num = FakeRadio::kMe;
    rec.on_from_radio(mine, 100, out);
    ToRadio t;
    t.which = ToRadio::Packet;
    t.packet.to = kBroadcast;
    t.packet.id = 0x42;
    t.packet.want_ack = true;
    t.packet.has_decoded = true;
    t.packet.decoded.portnum = kPortText;
    t.packet.decoded.payload = "hello";
    rec.on_to_radio(t, 100, out);
    CHECK(out.size() == 1 && out[0].kind == Record::Msg && out[0].mine && out[0].state == Delivery::Queued);
    CHECK(out[0].from == FakeRadio::kMe && out[0].chat_key() == "c0");
    radio.feed(frame(encode(t)));
    Deframer d;
    std::string bytes = radio.take_out(), payload;
    d.feed(bytes.data(), bytes.size());
    while (d.next(payload)) {
        FromRadio f;
        CHECK(decode(payload, f));
        rec.on_from_radio(f, 101, out);
    }
    CHECK(out.size() == 2 && out[1].kind == Record::Ack && out[1].state == Delivery::Failed);
    CHECK(out[1].error == kErrLocalRadioOff);
    CHECK(std::string(delivery_error_text(out[1].error)).find("region") != std::string::npos);
    // The same message sent again by id isn't recorded twice.
    rec.on_to_radio(t, 102, out);
    CHECK(out.size() == 2);
    // Nothing at all for five minutes: it's given up on.
    ToRadio u = t;
    u.packet.id = 0x43;
    rec.on_to_radio(u, 200, out);
    rec.tick(200 + Recorder::kNoAnswerAfter - 1, out);
    CHECK(out.size() == 3);
    rec.tick(200 + Recorder::kNoAnswerAfter + 1, out);
    CHECK(out.size() == 4 && out[3].state == Delivery::Failed && out[3].error == kErrLocalNoAnswer);
}

void test_requests() {
    Rig r;
    Client& c = r.c();
    uint32_t anna = r.radio().node_by_short("ANNA"), mrk = r.radio().node_by_short("MRK"),
             trun = r.radio().node_by_short("TRUN"), wx = r.radio().node_by_short("WX1"),
             sol = r.radio().node_by_short("SOL7"), gate = r.radio().node_by_short("GATE");

    // Traceroute, two hops out.
    CHECK(c.traceroute_wait(r.now()) == 0);
    CHECK(c.traceroute(mrk, r.now()) != 0);
    CHECK(c.last_trace(mrk) && !c.last_trace(mrk)->answered && !c.last_trace(mrk)->failed);
    CHECK(c.trace_lines(*c.last_trace(mrk)).size() == 1);
    r.run(5000);
    const TraceResult* t = c.last_trace(mrk);
    CHECK(t && t->answered && !t->failed && t->route.route.size() == 2 && t->has_back);
    std::vector<std::string> lines = c.trace_lines(*t);
    CHECK(lines.size() == 2);
    if (lines.size() == 2) {
        CHECK(lines[0] == "Out: You > HILL +6.5 dB > BASE +3.0 dB > MRK -0.5 dB");
        CHECK(lines[1].rfind("Back: MRK > BASE", 0) == 0 && lines[1].find("> You +5.5 dB") != std::string::npos);
    }
    CHECK(r.notices().find("Route to MRK: 2 hops between") != std::string::npos);
    // Straight away again: we refuse before the radio has to.
    CHECK(c.traceroute_wait(r.now()) > 20);
    CHECK(c.traceroute(anna, r.now()) == 0 && c.last_error().find("30 s") != std::string::npos);
    // A node out of reach: the radio's NAK ends the wait.
    r.run(31000);
    CHECK(c.traceroute(sol, r.now()) != 0);
    r.run(10000);
    t = c.last_trace(sol);
    CHECK(t && t->failed && t->error == kErrMaxRetransmit);
    CHECK(r.notices().find("Traceroute to SOL7") != std::string::npos);
    CHECK(c.trace_lines(*t)[0].rfind("Failed:", 0) == 0);

    // Position: one that shares, one that doesn't.
    const Node* a = c.node(anna);
    double before = a ? a->lat : 0;
    CHECK(c.request_position(anna, r.now()) != 0);
    r.run(2500);
    CHECK(r.notices().find("Position from ANNA") != std::string::npos);
    CHECK(c.node(anna)->has_pos && c.node(anna)->lat == before);
    CHECK(c.request_position(trun, r.now()) != 0);
    r.run(12000);
    CHECK(r.notices().find("TRUN isn't sharing its position") != std::string::npos);

    // Telemetry: the device's, and the weather station's sensors.
    CHECK(c.request_telemetry(anna, r.now()) != 0);
    r.run(12000);
    CHECK(r.notices().find("Telemetry from ANNA") != std::string::npos);
    CHECK(c.node(anna)->has_metrics && c.node(anna)->metrics.battery_level == 82 && c.node(anna)->metrics_time > 0);
    CHECK(!c.node(wx)->has_env);
    CHECK(c.request_telemetry(wx, r.now(), true) != 0);
    r.run(12000);
    CHECK(c.node(wx)->has_env && c.node(wx)->env.has_temperature && c.node(wx)->env.temperature == 1.5f);
    r.notices();
    CHECK(c.request_telemetry(gate, r.now()) != 0);   // says nothing about itself
    r.run(12000);
    CHECK(r.notices().find("GATE has no telemetry to share") != std::string::npos);

    // Who are you?
    CHECK(c.request_user(anna, r.now()) != 0);
    r.run(3000);
    CHECK(r.notices().find("Details from ANNA") != std::string::npos);
    // Never asks itself.
    CHECK(c.traceroute(c.my_num(), r.now()) == 0 && c.request_position(c.my_num(), r.now()) == 0);
    // The log saw all of it.
    CHECK(c.log().size() > 10 && c.log_rev() > 0);
}

void test_radio_settings() {
    Rig r;
    Client& c = r.c();
    // The name alone: no restart.
    c.set_owner("Zero One", "Z1");
    r.run(1500);
    CHECK(r.radio().long_name() == "Zero One" && r.radio().short_name() == "Z1" && r.radio().restarts() == 0);
    CHECK(c.me()->long_name == "Zero One");
    // Too long: cut to what the radio holds.
    c.set_owner(std::string(60, 'n'), "TOOLONG");
    r.run(500);
    CHECK(r.radio().long_name().size() == 39 && r.radio().short_name() == "TOOL");

    // The role: a restart, and it's there afterwards.
    DeviceConfig d = c.device();
    d.role = 1;
    c.set_device(d);
    r.run(5000);
    CHECK(r.radio().restarts() == 1 && c.ready() && c.device().role == 1 && r.radio().device().role == 1);
    CHECK(r.radio().device().node_info_broadcast_secs == 10800);   // what we didn't touch is as it was

    // Hop limit and power.
    LoRaConfig l = c.lora();
    l.hop_limit = 5;
    l.tx_power = 10;
    c.set_lora(l);
    r.run(5000);
    CHECK(r.radio().restarts() == 2 && c.lora().hop_limit == 5 && r.radio().lora().tx_power == 10);
    CHECK(c.send_text("c0", "five hops") != 0);
    r.run(500);
    CHECK(r.radio().sent_to_mesh().back().hop_limit == 5);

    // Transmit off: we say so instead of sending into nothing.
    l = c.lora();
    l.tx_enabled = false;
    c.set_lora(l);
    r.run(5000);
    CHECK(!c.can_transmit() && c.send_text("c0", "x") == 0 && c.last_error().find("transmit") != std::string::npos);

    // Favourite, ignore, remove.
    uint32_t anna = r.radio().node_by_short("ANNA"), gate = r.radio().node_by_short("GATE");
    c.set_favorite(anna, true);
    c.set_ignored(gate, true);
    r.run(500);
    CHECK(r.radio().node_favorite(anna) && r.radio().node_ignored(gate) && c.node(anna)->favorite && c.node(gate)->ignored);
    c.set_favorite(anna, false);
    c.remove_node(gate);
    r.run(500);
    CHECK(!r.radio().node_favorite(anna) && !r.radio().has_node(gate) && !c.node(gate));
    c.set_favorite(anna, true);
    c.reset_nodedb();
    r.run(500);
    CHECK(c.nodes().size() == 2 && c.node(anna));   // favourites survive, as on the radio

    // Where we are.
    double lat = 0, lon = 0;
    CHECK(!r.radio().own_position(lat, lon));
    CHECK(c.send_own_position(46.5, 8.5, true, 1500, r.now()));
    CHECK(!c.send_own_position(46.5, 8.5, true, 1500, r.now()));   // not again so soon
    r.run(500);
    CHECK(r.radio().own_position(lat, lon) && lat > 46.49 && lat < 46.51);
    CHECK(c.here(lat, lon) && c.me()->has_pos);
    c.set_fixed_position(47.0, 9.0, 400);
    r.run(500);
    CHECK(r.radio().own_position(lat, lon) && lat > 46.99 && c.position_config().fixed_position);
    c.remove_fixed_position();
    r.run(500);
    CHECK(!r.radio().own_position(lat, lon) && !c.position_config().fixed_position);

    // Restart on request.
    int before = r.radio().restarts();
    c.reboot();
    r.run(5000);
    CHECK(r.radio().restarts() == before + 1 && c.ready());
}

void test_channels() {
    Rig r;
    Client& c = r.c();
    // Share one channel: a link that adds, carrying its name and key.
    std::string one = c.share_url(1);
    ChannelSet set;
    CHECK(parse_channel_url(one, set) && set.add_only && set.settings.size() == 1 && set.settings[0].name == "Friends");
    CHECK(!set.has_lora);
    // The default channel has no name of its own: the link says which preset.
    CHECK(parse_channel_url(c.share_url(0), set) && set.settings[0].name == "LongFast");
    // All of them, with the radio settings: a link that replaces.
    std::string all = c.share_url(-1);
    CHECK(parse_channel_url(all, set) && !set.add_only && set.settings.size() == 2 && set.has_lora && set.lora.region == 3);
    CHECK(c.share_url(4).empty());

    // Adding what we already have does nothing, and says so.
    std::string why;
    CHECK(parse_channel_url(one, set));
    CHECK(!c.add_channels(set, &why) && why.find("already") != std::string::npos);

    // A new one goes in the first free slot.
    ChannelSet fresh;
    ChannelSettings hikers;
    hikers.name = "Hikers";
    hikers.psk = std::string(32, 'k');
    fresh.settings.push_back(hikers);
    fresh.add_only = true;
    CHECK(c.add_channels(fresh, &why));
    r.run(500);
    CHECK(c.channel(2) && c.channel_name(2) == "Hikers" && c.channel(2)->role == Channel::Secondary);
    CHECK(r.radio().channels()[2].settings.name == "Hikers" && r.radio().channels()[2].settings.psk.size() == 32);
    CHECK(c.chat("c2") && c.chats().size() == 3);
    CHECK(c.send_text("c2", "First!") != 0);
    r.run(1500);
    CHECK(r.radio().sent_to_mesh().back().channel == 2);

    // Full: five more fit, a sixth doesn't.
    ChannelSet many;
    for (int i = 0; i < 5; ++i) {
        ChannelSettings s;
        s.name = "More" + std::to_string(i);
        s.psk = std::string(16, char('a' + i));
        many.settings.push_back(s);
    }
    CHECK(c.add_channels(many, &why));
    r.run(500);
    CHECK(c.free_channel_slot() == -1 && r.radio().channels()[7].role == Channel::Secondary);
    ChannelSet extra;
    ChannelSettings s;
    s.name = "Extra";
    s.psk = std::string(16, 'z');
    extra.settings.push_back(s);
    CHECK(!c.add_channels(extra, &why) && why.find("8 channels") != std::string::npos);

    // Delete one: its slot empties, the others stay where they were.
    c.delete_channel(3);
    r.run(500);
    CHECK(!c.channel(3) && c.channel(4) && c.channel_name(4) == "More1" && r.radio().channels()[3].role == Channel::Disabled);
    CHECK(c.free_channel_slot() == 3);
    c.delete_channel(0);   // never the primary
    r.run(300);
    CHECK(c.channel(0));

    // Rename one, and change its key.
    Channel ch = *c.channel(2);
    ch.settings.name = "Climbers";
    ch.settings.uplink_enabled = true;
    c.set_channel(ch);
    r.run(500);
    CHECK(r.radio().channels()[2].settings.name == "Climbers" && r.radio().channels()[2].settings.uplink_enabled);
    CHECK(r.radio().channels()[2].settings.psk.size() == 32);

    // Replace everything from a link: its channels, its preset, our region kept.
    ChannelSet theirs;
    ChannelSettings p;
    p.psk = std::string("\x01", 1);
    theirs.settings.push_back(p);
    ChannelSettings q;
    q.name = "Club";
    q.psk = std::string(16, 'c');
    theirs.settings.push_back(q);
    theirs.has_lora = true;
    theirs.lora.use_preset = true;
    theirs.lora.modem_preset = 4;
    theirs.lora.hop_limit = 4;
    c.replace_channels(theirs);
    r.run(6000);
    CHECK(c.ready() && c.channel(0) && c.channel(1) && !c.channel(2) && !c.channel(7));
    CHECK(c.channel_name(0) == "MediumFast" && c.channel_name(1) == "Club");
    CHECK(c.lora().region == 3 && c.lora().modem_preset == 4 && c.lora().hop_limit == 4);
    CHECK(r.radio().lora().modem_preset == 4 && r.radio().channels()[1].settings.name == "Club");
    CHECK(r.radio().channels()[2].role == Channel::Disabled);
}

void test_history() {
    Rig r;
    Client& c = r.c();
    uint32_t anna = r.radio().node_by_short("ANNA");
    std::string ka = chat_key_dm(anna);
    r.radio().inject_text(anna, kBroadcast, 0, "one");
    r.radio().inject_text(anna, FakeRadio::kMe, 0, "two");
    r.run(300);
    CHECK(c.total_unread() == 2);
    c.mark_read("c0", r.now());
    std::string marks = c.read_marks();
    CHECK(marks.find("c0\t") != std::string::npos && marks.find(ka) == std::string::npos);

    // The marks carry over to another run of the app.
    Client other;
    other.set_read_marks(marks);
    Record rec;
    rec.kind = Record::Msg;
    rec.time = r.now();
    rec.id = 9;
    rec.from = anna;
    rec.to = kBroadcast;
    rec.text = "one";
    other.apply(rec);
    CHECK(other.total_unread() == 0);
    rec.time = r.now() + 50;
    rec.id = 10;
    other.apply(rec);
    CHECK(other.total_unread() == 1);
    other.set_read_marks("garbage\nc0\tnot a number\n\tx\n");
    CHECK(other.total_unread() == 2);

    // Exported as text.
    c.send_text(ka, "three");
    r.run(2500);
    std::string text = r.s->export_text("*", r.now());
    CHECK(text.find("== LongFast (channel) ==") != std::string::npos);
    CHECK(text.find("== Anna's T-Deck (direct) ==") != std::string::npos);
    CHECK(text.find("Anna's T-Deck: two") != std::string::npos && text.find("You: three") != std::string::npos);
    CHECK(r.s->export_text(ka, r.now()).find("LongFast") == std::string::npos);

    // Deleting a chat: a DM's row goes, a channel's stays, empty.
    r.s->forget_chat(ka, r.now());
    CHECK(!c.chat(ka));
    r.s->forget_chat("c0", r.now());
    CHECK(c.chat("c0") && c.chat("c0")->msgs.empty() && c.total_unread() == 0);
    r.radio().inject_text(anna, FakeRadio::kMe, 0, "again");
    r.run(300);
    CHECK(c.chat(ka) && c.chat(ka)->msgs.size() == 1);
    r.s->forget_chat("*", r.now());
    CHECK(c.chats().size() == 2 && c.chat("c0")->msgs.empty());
}

void test_simulator_is_lively() {
    // What someone trying the app in the simulator sees: old messages to
    // read, and the made-up nodes talking and answering.
    Session::Options o;
    o.simulated = true;
    Session s(o);
    s.resume();
    long ms = 0, epoch = 1795000000;
    for (; ms < 2000; ms += 100) s.tick(epoch + ms / 1000, ms);
    Client& c = s.client();
    CHECK(c.ready() && c.chats().size() >= 3);
    CHECK(c.chat("c0") && c.chat("c0")->msgs.size() == 4 && c.chat("c0")->unread == 0);
    CHECK(c.total_unread() >= 2);
    bool reacted = false;
    for (const Message& m : c.chat("c0")->msgs) reacted = reacted || !m.reactions.empty();
    CHECK(reacted);
    size_t before = c.chat("c0")->msgs.size();
    c.send_text("c0", "Anyone copy?");
    for (; ms < 12000; ms += 100) s.tick(epoch + ms / 1000, ms);
    CHECK(c.chat("c0")->msgs.size() >= before + 2);   // ours, and an answer
    int log_before = c.log_rev();
    for (; ms < 200000; ms += 100) s.tick(epoch + ms / 1000, ms);
    CHECK(c.log_rev() > log_before + 2);
    // Leaving the app and coming back keeps everything.
    s.suspend();
    CHECK(!s.running());
    s.resume();
    for (long end = ms + 1000; ms < end; ms += 100) s.tick(epoch + ms / 1000, ms);
    CHECK(c.ready() && c.chat("c0")->msgs.size() >= before + 2);
}

void test_text() {
    CHECK(display_text("plain") == "plain");
    CHECK(display_text("caf\xC3\xA9 \xE2\x80\x94 ok") == "caf\xC3\xA9 \xE2\x80\x94 ok");
    CHECK(display_text("great \xF0\x9F\x91\x8D") == "great :thumbsup:");
    CHECK(display_text("\xE2\x9D\xA4\xEF\xB8\x8F") == "<3");                       // heart + variation selector
    CHECK(display_text("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == ":thumbsup:");       // with a skin tone
    CHECK(display_text("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82!") == "?!");   // a run we can't draw: one "?"
    CHECK(display_text("ring\a") == "ring" && display_text("a\tb\nc") == "a b\nc");
    CHECK(display_text("\xff\xfe broken") == "? broken");
    CHECK(reaction_text("\xF0\x9F\x98\x82") == ":'D" && reaction_text("") == "?" && reaction_text("\xF0\x9F\xA6\x84") == "?");
    CHECK(utf8_cut("h\xC3\xA9llo", 2) == "h" && utf8_cut("abc", 10) == "abc");
    CHECK(one_line("a\nb", 10) == "a b" && one_line("abcdefghij", 8) == "abcde...");
    CHECK(age_text(1000, 0).empty() && age_text(1000, 990) == "now" && age_text(10000, 9000) == "16m");
    CHECK(age_text(100000, 90000) == "2h" && age_text(1000000, 700000) == "3d" && age_text(5000000, 1000000) == "6w");
    CHECK(distance_text(850) == "850 m" && distance_text(3210) == "3.2 km" && distance_text(120400) == "120 km");
    CHECK(distance_text(1609.344, true) == "1.0 mi" && distance_text(30, true) == "98 ft");
    CHECK(duration_text(45) == "45s" && duration_text(3900) == "1h 5m" && duration_text(93600) == "1d 2h");
    double d = distance_m(46.6340, 8.5940, 46.6529, 8.5940);
    CHECK(d > 2090 && d < 2110);
    CHECK(bearing_deg(46.0, 8.0, 47.0, 8.0) < 0.01 && std::string(compass_point(0)) == "N");
    double east = bearing_deg(46.0, 8.0, 46.0, 9.0);
    CHECK(east > 89 && east < 91 && std::string(compass_point(east)) == "E");
    CHECK(std::string(compass_point(225)) == "SW" && std::string(compass_point(359)) == "N");
    CHECK(snr_bars(6.5f) == 4 && snr_bars(-3.25f) == 3 && snr_bars(-16.5f) == 1 && snr_bars(-25) == 0);
    CHECK(std::string(snr_quality(-3)) == "Good" && std::string(snr_quality(-10)) == "Fair" && std::string(snr_quality(-19)) == "Weak");
}

}  // namespace

int main() {
    test_download();
    test_channel_message();
    test_direct_message();
    test_send_queue();
    test_alone();
    test_no_region();
    test_region_refused_by_radio();
    test_requests();
    test_radio_settings();
    test_channels();
    test_history();
    test_simulator_is_lively();
    test_text();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("mesh client: ok\n");
    return 0;
}
