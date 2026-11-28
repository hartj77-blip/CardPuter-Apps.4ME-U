// The hand-written codec (core/proto.cpp) against bytes made by Google's
// protobuf library from Meshtastic's own definitions (vectors.h). Each way:
// their bytes decode to the right values, and ours encode to their bytes.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "apps/mesh/core/pb.h"
#include "apps/mesh/core/proto.h"
#include "apps/mesh/tests/vectors.h"

using namespace zs::mesh;

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static std::string hex(const std::string& s) {
    std::string o;
    char b[4];
    for (unsigned char ch : s) {
        std::snprintf(b, sizeof b, "%02x", ch);
        o += b;
    }
    return o;
}
// Fields sorted by number (the order within a number kept), so a message we
// write "named fields, then the ones we don't know" compares equal to theirs.
static std::string canon(const std::string& s) {
    std::vector<std::pair<uint32_t, std::string>> fields;
    pb::Reader r(s);
    pb::Field f;
    while (r.next(f)) fields.emplace_back(f.num, std::string(f.raw));
    std::stable_sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string out;
    for (auto& x : fields) out += x.second;
    return out;
}
#define SAME(ours, theirs)                                                                      \
    do {                                                                                        \
        std::string a_ = (ours), b_ = (theirs);                                                 \
        if (a_ != b_) {                                                                         \
            std::printf("FAIL %s:%d: bytes differ\n  ours   %s\n  theirs %s\n", __FILE__, __LINE__, \
                        hex(a_).c_str(), hex(b_).c_str());                                      \
            ++failures;                                                                         \
        }                                                                                       \
    } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static void test_to_radio() {
    ToRadio t;
    t.which = ToRadio::WantConfig;
    t.want_config_id = 0x1234abcd;
    SAME(encode(t), vectors::to_want_config);

    ToRadio d;
    CHECK(decode(vectors::to_want_config, d));
    CHECK(d.which == ToRadio::WantConfig && d.want_config_id == 0x1234abcd);

    t = ToRadio{};
    t.which = ToRadio::Packet;
    t.packet.to = kBroadcast;
    t.packet.channel = 1;
    t.packet.id = 0x11223344;
    t.packet.want_ack = true;
    t.packet.hop_limit = 3;
    t.packet.has_decoded = true;
    t.packet.decoded.portnum = kPortText;
    t.packet.decoded.payload = "hello mesh";
    SAME(encode(t), vectors::to_text);
    CHECK(decode(vectors::to_text, d));
    CHECK(d.which == ToRadio::Packet && d.packet.to == kBroadcast && d.packet.channel == 1);
    CHECK(d.packet.id == 0x11223344 && d.packet.want_ack && d.packet.hop_limit == 3);
    CHECK(d.packet.has_decoded && d.packet.decoded.portnum == kPortText && d.packet.decoded.payload == "hello mesh");

    t = ToRadio{};
    t.which = ToRadio::Heartbeat;
    SAME(encode(t), vectors::to_heartbeat);
    CHECK(decode(vectors::to_heartbeat, d) && d.which == ToRadio::Heartbeat);

    t = ToRadio{};
    t.which = ToRadio::Disconnect;
    SAME(encode(t), vectors::to_disconnect);
    CHECK(decode(vectors::to_disconnect, d) && d.which == ToRadio::Disconnect);
}

static void test_config_download() {
    FromRadio f;
    CHECK(decode(vectors::from_my_info, f));
    CHECK(f.id == 7 && f.which == FromRadio::MyInfo && f.is_config());
    CHECK(f.my_info.my_node_num == 0xcafe0001 && f.my_info.reboot_count == 12);
    CHECK(f.my_info.min_app_version == 30200 && f.my_info.pio_env == "native" && f.my_info.nodedb_count == 9);
    SAME(encode(f), vectors::from_my_info);

    CHECK(decode(vectors::from_node_info, f));
    CHECK(f.which == FromRadio::NodeInfoMsg && f.is_config());
    const NodeInfo& n = f.node_info;
    CHECK(n.num == 0xa1b2c3d4 && n.has_user && n.has_position && n.has_metrics);
    CHECK(n.user.id == "!a1b2c3d4" && n.user.long_name == "Hilltop Relay" && n.user.short_name == "HILL");
    CHECK(n.user.hw_model == 43 && n.user.role == 2 && n.user.public_key.size() == 32);
    CHECK(n.user.has_unmessagable && n.user.is_unmessagable);
    CHECK(n.position.has_latlon && n.position.lat_i == -338688000 && n.position.lon_i == 1512093000);
    CHECK(n.position.has_alt && n.position.alt == -12 && n.position.time == 1795000000);
    CHECK(n.position.has_speed && n.position.ground_speed == 0 && n.position.has_track);
    CHECK(n.position.ground_track == 9000000 && n.position.sats_in_view == 11 && n.position.precision_bits == 13);
    CHECK(std::fabs(n.position.lat() + 33.8688) < 1e-6 && std::fabs(n.position.lon() - 151.2093) < 1e-6);
    CHECK(near(n.snr, -7.25f) && n.last_heard == 1795000100);
    CHECK(n.metrics.has_battery && n.metrics.battery_level == 101 && near(n.metrics.voltage, 4.12f));
    CHECK(near(n.metrics.channel_utilization, 11.5f) && near(n.metrics.air_util_tx, 0.75f));
    CHECK(n.metrics.uptime_seconds == 86400);
    CHECK(n.channel == 1 && n.via_mqtt && n.has_hops && n.hops_away == 0);
    CHECK(n.is_favorite && n.is_ignored && n.is_muted);
    SAME(encode(f), vectors::from_node_info);

    CHECK(decode(vectors::from_metadata, f));
    CHECK(f.which == FromRadio::Metadata && f.metadata.firmware_version == "2.7.26.61");
    CHECK(f.metadata.device_state_version == 24 && f.metadata.can_shutdown && f.metadata.has_wifi);
    CHECK(f.metadata.has_bluetooth && f.metadata.has_ethernet && f.metadata.role == 1);
    CHECK(f.metadata.hw_model == 37 && f.metadata.has_pkc);
    SAME(encode(f), vectors::from_metadata);

    CHECK(decode(vectors::from_channel_primary, f));
    CHECK(f.which == FromRadio::ChannelMsg && f.channel.index == 0 && f.channel.role == Channel::Primary);
    CHECK(f.channel.settings.psk == std::string("\x01", 1) && f.channel.settings.name.empty());
    SAME(encode(f), vectors::from_channel_primary);

    CHECK(decode(vectors::from_channel_secondary, f));
    const Channel& c = f.channel;
    CHECK(c.index == 2 && c.role == Channel::Secondary && c.settings.name == "Friends");
    CHECK(c.settings.psk.size() == 32 && c.settings.id == 0xdeadbeef);
    CHECK(c.settings.uplink_enabled && c.settings.downlink_enabled);
    CHECK(c.settings.position_precision == 32 && c.settings.is_muted);
    SAME(encode(f), vectors::from_channel_secondary);

    CHECK(decode(vectors::from_channel_disabled, f));
    CHECK(f.channel.index == 5 && f.channel.role == Channel::Disabled);
    SAME(encode(f), vectors::from_channel_disabled);

    CHECK(decode(vectors::from_config_complete, f));
    CHECK(f.which == FromRadio::ConfigComplete && f.config_complete_id == 0x1234abcd && f.is_config());
    SAME(encode(f), vectors::from_config_complete);

    CHECK(decode(vectors::from_rebooted, f));
    CHECK(f.which == FromRadio::Rebooted && !f.is_config());
    SAME(encode(f), vectors::from_rebooted);

    CHECK(decode(vectors::from_log_record, f));
    CHECK(f.which == FromRadio::LogRecord && !f.is_config());
}

static void test_config() {
    FromRadio f;
    CHECK(decode(vectors::from_config_lora, f));
    CHECK(f.which == FromRadio::ConfigMsg && f.config.which == Config::LoRa);
    LoRaConfig l;
    CHECK(decode(f.config.raw, l));
    CHECK(l.use_preset && l.modem_preset == 4 && l.region == 3 && l.hop_limit == 3 && l.tx_enabled);
    CHECK(l.tx_power == 22 && l.channel_num == 4 && l.override_duty_cycle && l.rx_boosted_gain);
    CHECK(l.ignore_mqtt && l.ok_to_mqtt);
    // Written back untouched: the same message, with the fields we don't
    // name (bandwidth, frequency offset, the ignore list) still there.
    SAME(canon(encode(l)), canon(f.config.raw));
    // Change the region and preset: only those two fields differ.
    l.region = 1;
    l.modem_preset = 0;
    LoRaConfig back;
    CHECK(decode(encode(l), back));
    CHECK(back.region == 1 && back.modem_preset == 0 && back.hop_limit == 3 && back.tx_power == 22);
    CHECK(pb::find_varint(encode(l), 3) == 250 && pb::find_varint(encode(l), 4) == 9);
    CHECK(pb::has_field(encode(l), 6) && pb::has_field(encode(l), 14) && pb::has_field(encode(l), 103));
    CHECK(!pb::has_field(encode(l), 2));   // LONG_FAST is 0: left out, as proto3 does

    CHECK(decode(vectors::from_config_lora_empty, f));
    CHECK(f.config.which == Config::LoRa && f.config.raw.empty());
    CHECK(decode(f.config.raw, l) && l.region == 0 && !l.tx_enabled);
    SAME(encode(f), vectors::from_config_lora_empty);

    CHECK(decode(vectors::from_config_device, f));
    CHECK(f.config.which == Config::Device);
    DeviceConfig d;
    CHECK(decode(f.config.raw, d));
    CHECK(d.role == 1 && d.rebroadcast_mode == 2 && d.node_info_broadcast_secs == 10800);
    CHECK(d.tzdef == "CET-1CEST,M3.5.0,M10.5.0/3");
    SAME(canon(encode(d)), canon(f.config.raw));
    d.role = 0;
    CHECK(pb::find_varint(encode(d), 4) == 4 && pb::find_varint(encode(d), 12) == 1);

    CHECK(decode(vectors::from_config_position, f));
    CHECK(f.config.which == Config::PositionCfg);
    PositionConfig p;
    CHECK(decode(f.config.raw, p));
    CHECK(p.position_broadcast_secs == 900 && p.smart_enabled && p.fixed_position && p.gps_mode == 2);
    SAME(canon(encode(p)), canon(f.config.raw));
}

static void test_traffic() {
    FromRadio f;
    CHECK(decode(vectors::from_text, f));
    CHECK(f.id == 99 && f.which == FromRadio::Packet && !f.is_config());
    const MeshPacket& p = f.packet;
    CHECK(p.from == 0xa1b2c3d4 && p.to == 0xcafe0001 && p.channel == 2 && p.id == 0x55667788);
    CHECK(p.rx_time == 1795000200 && near(p.rx_snr, -7.25f) && p.rx_rssi == -101);
    CHECK(p.hop_limit == 1 && p.hop_start == 3 && p.hops() == 2 && p.want_ack && p.priority == 70);
    CHECK(p.via_mqtt && p.pki_encrypted && p.public_key.size() == 32 && p.next_hop == 0xd4 && p.relay_node == 0x7f);
    CHECK(p.has_decoded && p.decoded.portnum == kPortText && p.decoded.payload == "On my way, 10 min");
    CHECK(p.decoded.want_response && p.decoded.dest == 0xcafe0001 && p.decoded.source == 0xa1b2c3d4);
    CHECK(p.decoded.request_id == 0x01020304 && p.decoded.reply_id == 0x0a0b0c0d && p.decoded.emoji == 1);
    CHECK(p.decoded.has_bitfield && p.decoded.bitfield == 1);
    SAME(encode(f), vectors::from_text);

    CHECK(decode(vectors::from_encrypted, f));
    CHECK(!f.packet.has_decoded && f.packet.encrypted.size() == 5 && f.packet.hops() == -1);
    SAME(encode(f), vectors::from_encrypted);

    CHECK(decode(vectors::from_ack, f));
    CHECK(f.packet.decoded.portnum == kPortRouting && f.packet.decoded.request_id == 0x11223344);
    Routing r;
    CHECK(decode(f.packet.decoded.payload, r));
    CHECK(r.which == Routing::ErrorReason && r.error_reason == kErrNone);
    SAME(encode(r), vectors::routing_none);
    SAME(encode(f), vectors::from_ack);

    CHECK(decode(vectors::from_nak, f));
    CHECK(decode(f.packet.decoded.payload, r) && r.error_reason == kErrMaxRetransmit);
    CHECK(decode(vectors::routing_pki, r) && r.error_reason == kErrPkiUnknownPubkey);
    SAME(encode(r), vectors::routing_pki);
    CHECK(decode("", r) && r.which == Routing::ErrorReason && r.error_reason == kErrNone);

    RouteDiscovery d;
    CHECK(decode(vectors::route_discovery, d));
    CHECK(d.route.size() == 2 && d.route[0] == 0xaaaa && d.route[1] == 0xffffffff);
    CHECK(d.snr_towards.size() == 3 && d.snr_towards[0] == -20 && d.snr_towards[1] == 12 && d.snr_towards[2] == -128);
    CHECK(d.route_back.size() == 1 && d.route_back[0] == 0xbbbb);
    CHECK(d.snr_back.size() == 2 && d.snr_back[0] == 5 && d.snr_back[1] == 6);
    SAME(encode(d), vectors::route_discovery);
    SAME(encode(RouteDiscovery{}), vectors::route_discovery_empty);
    // Not packed (one field per value): older encoders write it that way.
    pb::Writer w;
    w.fixed32_always(1, 0x1111);
    w.fixed32_always(1, 0x2222);
    w.int32_always(2, -8);
    CHECK(decode(w.str(), d) && d.route.size() == 2 && d.route[1] == 0x2222 && d.snr_towards[0] == -8);

    FromRadio q;
    CHECK(decode(vectors::from_queue_status, q));
    CHECK(q.which == FromRadio::QueueStatusMsg && q.queue_status.res == 34 && q.queue_status.free == 15);
    CHECK(q.queue_status.maxlen == 16 && q.queue_status.mesh_packet_id == 0x11223344);
    SAME(encode(q), vectors::from_queue_status);

    CHECK(decode(vectors::from_notification, q));
    CHECK(q.which == FromRadio::Notification && q.notification.has_reply_id);
    CHECK(q.notification.reply_id == 0x11223344 && q.notification.level == 30);
    CHECK(q.notification.message == "TraceRoute can only be sent once every 30 seconds");
    SAME(encode(q), vectors::from_notification);
}

static void test_payloads() {
    Telemetry t;
    CHECK(decode(vectors::telemetry_device, t));
    CHECK(t.which == Telemetry::Device && t.time == 1795000300 && t.device.battery_level == 87);
    CHECK(near(t.device.voltage, 3.98f) && near(t.device.channel_utilization, 4.5f) && t.device.uptime_seconds == 3600);
    SAME(encode(t), vectors::telemetry_device);

    CHECK(decode(vectors::telemetry_environment, t));
    CHECK(t.which == Telemetry::Environment && near(t.environment.temperature, -3.5f));
    CHECK(near(t.environment.relative_humidity, 71.0f) && near(t.environment.barometric_pressure, 1013.25f));
    CHECK(t.environment.has_iaq && t.environment.iaq == 42);

    CHECK(decode(vectors::telemetry_local, t));
    CHECK(t.which == Telemetry::Local && t.local.uptime_seconds == 7200 && t.local.num_packets_rx == 900);
    CHECK(t.local.num_online_nodes == 7 && t.local.num_total_nodes == 31 && t.local.noise_floor == -113);
    CHECK(t.local.num_tx_relay == 15 && t.local.num_tx_relay_canceled == 6 && t.local.num_rx_dupe == 120);
    SAME(encode(t), vectors::telemetry_local);

    // Asking for telemetry: the kind wanted, empty.
    Telemetry ask;
    ask.which = Telemetry::Device;
    SAME(encode(ask), vectors::telemetry_request_device);
    ask.which = Telemetry::Environment;
    SAME(encode(ask), vectors::telemetry_request_environment);

    Position p;
    CHECK(decode(vectors::position_time_only, p));
    CHECK(!p.has_latlon && !p.has_alt && p.time == 1795000400);
    SAME(encode(p), vectors::position_time_only);
    CHECK(decode(vectors::position_full, p));
    CHECK(p.has_latlon && p.lat_i == 515074000 && p.lon_i == -1278000 && p.alt == 35 && p.location_source == 3);
    CHECK(p.ground_speed == 2 && p.ground_track == 12300000 && p.sats_in_view == 9);
    SAME(encode(p), vectors::position_full);

    Waypoint w;
    CHECK(decode(vectors::waypoint, w));
    CHECK(w.id == 0xc0ffee && w.has_latlon && w.lat_i == 515074000 && w.lon_i == -1278000);
    CHECK(w.expire == 1795003600 && w.name == "Camp" && w.description == "Water here");
    SAME(encode(w), vectors::waypoint);

    User u;
    CHECK(decode(vectors::user, u));
    CHECK(u.id == "!cafe0001" && u.long_name == "Cardputer Zero" && u.short_name == "ZERO" && u.is_licensed);
    SAME(canon(encode(u)), canon(vectors::user));
    u.long_name = "Renamed";
    User back;
    CHECK(decode(encode(u), back) && back.long_name == "Renamed" && back.public_key.size() == 32 && back.hw_model == 37);
}

static AdminMessage scalar(uint32_t which, uint64_t value) {
    AdminMessage a;
    a.which = which;
    a.value = value;
    return a;
}

static void test_admin() {
    SAME(encode(scalar(AdminMessage::GetOwnerRequest, 1)), vectors::admin_get_owner_request);
    SAME(encode(scalar(AdminMessage::GetChannelRequest, 3)), vectors::admin_get_channel_request);
    // DEVICE_CONFIG is 0 and must still be on the wire: it's a oneof.
    SAME(encode(scalar(AdminMessage::GetConfigRequest, 0)), vectors::admin_get_config_request_device);
    SAME(encode(scalar(AdminMessage::GetConfigRequest, 5)), vectors::admin_get_config_request_lora);
    SAME(encode(scalar(AdminMessage::BeginEdit, 1)), vectors::admin_begin_edit);
    SAME(encode(scalar(AdminMessage::CommitEdit, 1)), vectors::admin_commit_edit);
    SAME(encode(scalar(AdminMessage::RebootSeconds, 5)), vectors::admin_reboot);
    SAME(encode(scalar(AdminMessage::ShutdownSeconds, 2)), vectors::admin_shutdown);
    SAME(encode(scalar(AdminMessage::NodedbReset, 1)), vectors::admin_nodedb_reset);
    SAME(encode(scalar(AdminMessage::SetFavorite, 0xa1b2c3d4)), vectors::admin_set_favorite);
    SAME(encode(scalar(AdminMessage::RemoveFavorite, 0xa1b2c3d4)), vectors::admin_remove_favorite);
    SAME(encode(scalar(AdminMessage::SetIgnored, 0xa1b2c3d4)), vectors::admin_set_ignored);
    SAME(encode(scalar(AdminMessage::RemoveIgnored, 0xa1b2c3d4)), vectors::admin_remove_ignored);
    SAME(encode(scalar(AdminMessage::RemoveByNodenum, 0xa1b2c3d4)), vectors::admin_remove_node);
    SAME(encode(scalar(AdminMessage::RemoveFixedPosition, 1)), vectors::admin_remove_fixed_position);

    AdminMessage a;
    a.which = AdminMessage::SetOwner;
    a.user.long_name = "Trail Runner";
    a.user.short_name = "TRUN";
    SAME(encode(a), vectors::admin_set_owner);

    a = AdminMessage{};
    a.which = AdminMessage::SetConfig;
    LoRaConfig l;
    l.use_preset = true;
    l.modem_preset = 0;
    l.region = 1;
    l.hop_limit = 3;
    l.tx_enabled = true;
    l.tx_power = 30;
    l.rx_boosted_gain = true;
    a.config.which = Config::LoRa;
    a.config.raw = encode(l);
    SAME(encode(a), vectors::admin_set_config_lora);

    a = AdminMessage{};
    a.which = AdminMessage::SetChannel;
    a.channel.index = 1;
    a.channel.role = Channel::Secondary;
    a.channel.settings.name = "Hikers";
    for (int i = 0; i < 16; ++i) a.channel.settings.psk.push_back(char(i));
    SAME(encode(a), vectors::admin_set_channel);

    a = AdminMessage{};
    a.which = AdminMessage::SetChannel;
    a.channel.index = 3;
    a.channel.role = Channel::Disabled;
    // Theirs carries an empty settings message; with or without it the
    // firmware reads the same thing. Decode ours with their reader's rules.
    Channel c;
    AdminMessage theirs;
    CHECK(decode(vectors::admin_set_channel_disabled, theirs));
    CHECK(theirs.which == AdminMessage::SetChannel && theirs.channel.index == 3 && theirs.channel.role == 0);
    AdminMessage ours;
    CHECK(decode(encode(a), ours) && ours.channel.index == 3 && ours.channel.role == 0);
    (void)c;

    a = AdminMessage{};
    a.which = AdminMessage::SetFixedPosition;
    a.position.has_latlon = true;
    a.position.lat_i = 515074000;
    a.position.lon_i = -1278000;
    a.position.has_alt = true;
    a.position.alt = 35;
    SAME(encode(a), vectors::admin_set_fixed_position);

    CHECK(decode(vectors::admin_get_owner_response, a));
    CHECK(a.which == AdminMessage::GetOwnerResponse && a.user.long_name == "Cardputer Zero" && a.user.short_name == "ZERO");
}

static void test_framing() {
    std::string a = frame("abc"), b = frame(std::string(300, 'x'));
    CHECK(a.size() == 7 && uint8_t(a[0]) == 0x94 && uint8_t(a[1]) == 0xc3 && a[2] == 0 && a[3] == 3);
    CHECK(uint8_t(b[2]) == 1 && uint8_t(b[3]) == 44);
    Deframer d;
    std::string all = "boot text\n" + a + "\x94junk" + b + frame("");
    std::string out;
    std::vector<std::string> got;
    for (char ch : all) {   // a byte at a time, as a slow socket would
        d.feed(&ch, 1);
        while (d.next(out)) got.push_back(out);
    }
    CHECK(got.size() == 3 && got[0] == "abc" && got[1] == std::string(300, 'x') && got[2].empty());
    // A length that can't be real is not a header.
    Deframer e;
    std::string bad = std::string("\x94\xc3\xff\xff", 4) + a;
    e.feed(bad.data(), bad.size());
    CHECK(e.next(out) && out == "abc");
    // Half a frame waits for the rest.
    Deframer h;
    h.feed(b.data(), 100);
    CHECK(!h.next(out));
    h.feed(b.data() + 100, b.size() - 100);
    CHECK(h.next(out) && out.size() == 300);
}

static void test_wire_edges() {
    pb::Field f;
    // Truncated and malformed input never reads past the end.
    for (const std::string* v : {&vectors::from_node_info, &vectors::from_text, &vectors::from_config_lora}) {
        for (size_t n = 0; n < v->size(); ++n) {
            FromRadio x;
            decode(std::string_view(v->data(), n), x);
        }
    }
    std::string junk = "\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff";
    FromRadio x;
    CHECK(!decode(junk, x));
    pb::Reader r(std::string_view("\x0a\x05hi", 4));   // says 5 bytes, has 2
    CHECK(!r.next(f) && !r.ok());
    // Zigzag.
    pb::Writer w;
    w.sint32_always(1, -3);
    w.sint32_always(2, 7);
    pb::Reader z(w.str());
    CHECK(z.next(f) && f.s32() == -3);
    CHECK(z.next(f) && f.s32() == 7);
    // A negative int32 is ten bytes on the wire and comes back negative.
    pb::Writer n;
    n.int32_always(1, -101);
    CHECK(n.str().size() == 11);
    pb::Reader nr(n.str());
    CHECK(nr.next(f) && f.i32() == -101);
    // Changing one field keeps the others, in any order.
    std::string msg = pb::with_varint(pb::with_bytes(pb::with_varint("", 9, 5), 2, "name"), 1, 7);
    CHECK(pb::find_varint(msg, 9) == 5 && pb::find_varint(msg, 1) == 7 && pb::find_bytes(msg, 2) == "name");
    msg = pb::with_varint(msg, 9, 0);
    CHECK(!pb::has_field(msg, 9) && pb::has_field(msg, 2));
}

static void test_names_and_links() {
    CHECK(node_id(0xa1b2c3d4) == "!a1b2c3d4" && node_id(1) == "!00000001");
    CHECK(parse_node_id("!a1b2c3d4") == 0xa1b2c3d4 && parse_node_id("A1B2C3D4") == 0xa1b2c3d4);
    CHECK(parse_node_id("!xyz") == 0 && parse_node_id("") == 0 && parse_node_id("!a1b2c3d") == 0);
    CHECK(std::string(region_name(3)) == "EU_868" && std::string(region_name(0)) == "Not set");
    CHECK(std::string(preset_name(0)) == "LongFast" && std::string(preset_name(4)) == "MediumFast");
    CHECK(std::string(role_name(0)) == "Client" && std::string(role_name(2)) == "Router");
    CHECK(hw_model_name(37) == "Linux (meshtasticd)" && hw_model_name(9999) == "Model 9999" && hw_model_name(0).empty());
    CHECK(std::string(routing_error_text(kErrMaxRetransmit)) == "No one confirmed it");
    for (const Named& n : regions()) CHECK(n.value != 0 && *n.name && *n.label);

    std::string raw;
    CHECK(base64("") == "" && base64("f") == "Zg==" && base64("fo") == "Zm8=" && base64("foo") == "Zm9v");
    CHECK(base64url(std::string("\xfb\xff", 2)) == "-_8" && base64(std::string("\xfb\xff", 2)) == "+/8=");
    CHECK(base64_decode("Zm9vYmFy", raw) && raw == "foobar");
    CHECK(base64_decode("-_8", raw) && raw == std::string("\xfb\xff", 2));
    CHECK(base64_decode("+/8=", raw) && raw == std::string("\xfb\xff", 2));
    CHECK(!base64_decode("not base64!", raw));

    ChannelSet set;
    CHECK(parse_channel_url(vectors::channel_url, set));
    CHECK(set.settings.size() == 2 && !set.add_only && set.has_lora);
    CHECK(set.settings[0].psk == std::string("\x01", 1) && set.settings[0].name.empty());
    CHECK(set.settings[1].name == "Hikers" && set.settings[1].psk.size() == 16 && set.settings[1].uplink_enabled);
    CHECK(set.lora.use_preset && set.lora.region == 3 && set.lora.hop_limit == 3 && set.lora.tx_enabled);
    CHECK(channel_url(set) == vectors::channel_url);

    std::string add = std::string(vectors::channel_url);
    add.replace(add.find("/e/#"), 4, "/e/?add=true#");
    CHECK(parse_channel_url(add, set) && set.add_only && set.settings.size() == 2);
    CHECK(channel_url(set) == add);
    // Just the code, as someone might type it; and things that aren't links.
    std::string code = std::string(vectors::channel_url).substr(std::string(vectors::channel_url).find('#') + 1);
    CHECK(parse_channel_url("  " + code + "\n", set) && set.settings.size() == 2);
    CHECK(!parse_channel_url("", set));
    CHECK(!parse_channel_url("https://example.com/#" + code, set));
    CHECK(!parse_channel_url("https://meshtastic.org/e/#", set));
    CHECK(!parse_channel_url("https://meshtastic.org/e/#!!!!", set));
    CHECK(!parse_channel_url("hello there", set));

    LoRaConfig lora;
    lora.raw = "x";
    lora.use_preset = true;
    lora.modem_preset = 4;
    ChannelSettings c;
    CHECK(channel_display_name(c, lora) == "MediumFast");
    c.name = "Friends";
    CHECK(channel_display_name(c, lora) == "Friends");
    CHECK(std::string(psk_kind("")) == "No encryption" && std::string(psk_kind(std::string("\x01", 1))) == "Default key");
    CHECK(std::string(psk_kind(std::string(16, 'k'))) == "Private key");
}

int main() {
    test_to_radio();
    test_config_download();
    test_config();
    test_traffic();
    test_payloads();
    test_admin();
    test_framing();
    test_wire_edges();
    test_names_and_links();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("mesh proto: ok\n");
    return 0;
}
