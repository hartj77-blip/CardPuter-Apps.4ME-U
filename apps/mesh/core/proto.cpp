#include "apps/mesh/core/proto.h"

#include <cstdio>

#include "apps/mesh/core/pb.h"

namespace zs::mesh {

using pb::Field;
using pb::Reader;
using pb::Writer;

const char* routing_error_text(uint32_t err) {
    switch (err) {
    case kErrNone: return "Delivered";
    case kErrNoRoute: return "No route to that node";
    case kErrGotNak: return "Refused on the way";
    case kErrTimeout: return "Timed out";
    case kErrNoInterface: return "No radio to send on";
    case kErrMaxRetransmit: return "No one confirmed it";
    case kErrNoChannel: return "They don't have this channel";
    case kErrTooLarge: return "Too long to send";
    case kErrNoResponse: return "No answer";
    case kErrDutyCycle: return "Airtime limit reached, try later";
    case kErrBadRequest: return "The radio rejected it";
    case kErrNotAuthorized: return "Not allowed";
    case kErrPkiFailed: return "Couldn't encrypt it for them";
    case kErrPkiUnknownPubkey: return "Their key isn't known yet";
    case kErrAdminBadSessionKey: return "Admin session expired";
    case kErrAdminKeyUnauthorized: return "Admin key not accepted";
    case kErrRateLimit: return "Sent too fast, wait a moment";
    case kErrPkiSendFail: return "They don't know our key yet";
    default: return "Not sent";
    }
}

// ---- Position
std::string encode(const Position& p) {
    Writer w;
    if (p.has_latlon) {
        w.sfixed32_always(1, p.lat_i);
        w.sfixed32_always(2, p.lon_i);
    }
    if (p.has_alt) w.int32_always(3, p.alt);
    w.fixed32(4, p.time);
    w.varint(5, p.location_source);
    if (p.has_speed) w.varint_always(15, p.ground_speed);
    if (p.has_track) w.varint_always(16, p.ground_track);
    w.varint(19, p.sats_in_view);
    w.varint(23, p.precision_bits);
    return w.take();
}
bool decode(std::string_view s, Position& p) {
    p = Position{};
    Reader r(s);
    Field f;
    bool lat = false, lon = false;
    while (r.next(f)) {
        switch (f.num) {
        case 1: p.lat_i = f.i32(); lat = true; break;
        case 2: p.lon_i = f.i32(); lon = true; break;
        case 3: p.alt = f.i32(); p.has_alt = true; break;
        case 4: p.time = f.u32(); break;
        case 5: p.location_source = f.u32(); break;
        case 15: p.ground_speed = f.u32(); p.has_speed = true; break;
        case 16: p.ground_track = f.u32(); p.has_track = true; break;
        case 19: p.sats_in_view = f.u32(); break;
        case 23: p.precision_bits = f.u32(); break;
        default: break;
        }
    }
    // 0, 0 is "no position" from older firmware, not a place in the Atlantic.
    p.has_latlon = lat && lon && !(p.lat_i == 0 && p.lon_i == 0);
    return r.ok();
}

// ---- User
std::string encode(const User& u) {
    std::string base = u.raw;
    for (uint32_t n : {1u, 2u, 3u, 5u, 6u, 7u, 8u, 9u}) base = pb::without(base, n);
    Writer w;
    w.bytes(1, u.id);
    w.bytes(2, u.long_name);
    w.bytes(3, u.short_name);
    w.varint(5, u.hw_model);
    w.boolean(6, u.is_licensed);
    w.varint(7, u.role);
    w.bytes(8, u.public_key);
    if (u.has_unmessagable) w.varint_always(9, u.is_unmessagable ? 1 : 0);
    w.raw(base);
    return w.take();
}
bool decode(std::string_view s, User& u) {
    u = User{};
    u.raw = std::string(s);
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: u.id = f.str(); break;
        case 2: u.long_name = f.str(); break;
        case 3: u.short_name = f.str(); break;
        case 5: u.hw_model = f.u32(); break;
        case 6: u.is_licensed = f.b(); break;
        case 7: u.role = f.u32(); break;
        case 8: u.public_key = f.str(); break;
        case 9: u.has_unmessagable = true; u.is_unmessagable = f.b(); break;
        default: break;
        }
    }
    return r.ok();
}

// ---- Telemetry
std::string encode(const DeviceMetrics& m) {
    Writer w;
    if (m.has_battery) w.varint_always(1, m.battery_level);
    if (m.has_voltage) w.f32_always(2, m.voltage);
    if (m.has_ch_util) w.f32_always(3, m.channel_utilization);
    if (m.has_air_util) w.f32_always(4, m.air_util_tx);
    if (m.has_uptime) w.varint_always(5, m.uptime_seconds);
    return w.take();
}
bool decode(std::string_view s, DeviceMetrics& m) {
    m = DeviceMetrics{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: m.battery_level = f.u32(); m.has_battery = true; break;
        case 2: m.voltage = f.f(); m.has_voltage = true; break;
        case 3: m.channel_utilization = f.f(); m.has_ch_util = true; break;
        case 4: m.air_util_tx = f.f(); m.has_air_util = true; break;
        case 5: m.uptime_seconds = f.u32(); m.has_uptime = true; break;
        default: break;
        }
    }
    return r.ok();
}
static std::string encode_env(const EnvironmentMetrics& e) {
    Writer w;
    if (e.has_temperature) w.f32_always(1, e.temperature);
    if (e.has_humidity) w.f32_always(2, e.relative_humidity);
    if (e.has_pressure) w.f32_always(3, e.barometric_pressure);
    if (e.has_iaq) w.varint_always(7, e.iaq);
    return w.take();
}
static bool decode_env(std::string_view s, EnvironmentMetrics& e) {
    e = EnvironmentMetrics{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: e.temperature = f.f(); e.has_temperature = true; break;
        case 2: e.relative_humidity = f.f(); e.has_humidity = true; break;
        case 3: e.barometric_pressure = f.f(); e.has_pressure = true; break;
        case 7: e.iaq = f.u32(); e.has_iaq = true; break;
        default: break;
        }
    }
    return r.ok();
}
static std::string encode_local(const LocalStats& l) {
    Writer w;
    w.varint(1, l.uptime_seconds);
    w.f32(2, l.channel_utilization);
    w.f32(3, l.air_util_tx);
    w.varint(4, l.num_packets_tx);
    w.varint(5, l.num_packets_rx);
    w.varint(6, l.num_packets_rx_bad);
    w.varint(7, l.num_online_nodes);
    w.varint(8, l.num_total_nodes);
    w.varint(9, l.num_rx_dupe);
    w.varint(10, l.num_tx_relay);
    w.varint(11, l.num_tx_relay_canceled);
    w.int32(15, l.noise_floor);
    return w.take();
}
static bool decode_local(std::string_view s, LocalStats& l) {
    l = LocalStats{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: l.uptime_seconds = f.u32(); break;
        case 2: l.channel_utilization = f.f(); break;
        case 3: l.air_util_tx = f.f(); break;
        case 4: l.num_packets_tx = f.u32(); break;
        case 5: l.num_packets_rx = f.u32(); break;
        case 6: l.num_packets_rx_bad = f.u32(); break;
        case 7: l.num_online_nodes = f.u32(); break;
        case 8: l.num_total_nodes = f.u32(); break;
        case 9: l.num_rx_dupe = f.u32(); break;
        case 10: l.num_tx_relay = f.u32(); break;
        case 11: l.num_tx_relay_canceled = f.u32(); break;
        case 15: l.noise_floor = f.i32(); break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const Telemetry& t) {
    Writer w;
    w.fixed32(1, t.time);
    Writer sub;
    switch (t.which) {
    case Telemetry::Device: sub.raw(encode(t.device)); w.sub(2, sub); break;
    case Telemetry::Environment: sub.raw(encode_env(t.environment)); w.sub(3, sub); break;
    case Telemetry::Local: sub.raw(encode_local(t.local)); w.sub(6, sub); break;
    default: break;
    }
    return w.take();
}
bool decode(std::string_view s, Telemetry& t) {
    t = Telemetry{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: t.time = f.u32(); break;
        case 2: t.which = Telemetry::Device; decode(f.data, t.device); break;
        case 3: t.which = Telemetry::Environment; decode_env(f.data, t.environment); break;
        case 6: t.which = Telemetry::Local; decode_local(f.data, t.local); break;
        case 4: case 5: case 7: case 8: case 9: case 11: t.which = Telemetry::Other; break;
        default: break;
        }
    }
    return r.ok();
}

// ---- Routing, traceroute
std::string encode(const RouteDiscovery& d) {
    Writer w;
    auto packed_fixed = [&w](uint32_t field, const std::vector<uint32_t>& v) {
        if (v.empty()) return;
        Writer p;
        for (uint32_t x : v) p.raw_fixed32(x);
        w.bytes_always(field, p.str());
    };
    auto packed_int = [&w](uint32_t field, const std::vector<int32_t>& v) {
        if (v.empty()) return;
        Writer p;
        for (int32_t x : v) p.raw_varint(uint64_t(int64_t(x)));
        w.bytes_always(field, p.str());
    };
    packed_fixed(1, d.route);
    packed_int(2, d.snr_towards);
    packed_fixed(3, d.route_back);
    packed_int(4, d.snr_back);
    return w.take();
}
bool decode(std::string_view s, RouteDiscovery& d) {
    d = RouteDiscovery{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: pb::each_fixed32(f, [&d](uint32_t v) { d.route.push_back(v); }); break;
        case 2: pb::each_varint(f, [&d](uint64_t v) { d.snr_towards.push_back(int32_t(uint32_t(v))); }); break;
        case 3: pb::each_fixed32(f, [&d](uint32_t v) { d.route_back.push_back(v); }); break;
        case 4: pb::each_varint(f, [&d](uint64_t v) { d.snr_back.push_back(int32_t(uint32_t(v))); }); break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const Routing& x) {
    Writer w;
    // The variant is a oneof: an error of NONE is still written, as the firmware does.
    if (x.which == Routing::ErrorReason) w.varint_always(3, x.error_reason);
    return w.take();
}
bool decode(std::string_view s, Routing& x, RouteDiscovery* route) {
    x = Routing{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: x.which = Routing::RouteRequest; if (route) decode(f.data, *route); break;
        case 2: x.which = Routing::RouteReply; if (route) decode(f.data, *route); break;
        case 3: x.which = Routing::ErrorReason; x.error_reason = f.u32(); break;
        default: break;
        }
    }
    // An empty Routing is how older firmware says "no error".
    if (x.which == Routing::None) x.which = Routing::ErrorReason;
    return r.ok();
}

// ---- Waypoint
std::string encode(const Waypoint& x) {
    Writer w;
    w.varint(1, x.id);
    if (x.has_latlon) {
        w.sfixed32_always(2, x.lat_i);
        w.sfixed32_always(3, x.lon_i);
    }
    w.varint(4, x.expire);
    w.bytes(6, x.name);
    w.bytes(7, x.description);
    return w.take();
}
bool decode(std::string_view s, Waypoint& x) {
    x = Waypoint{};
    Reader r(s);
    Field f;
    bool lat = false, lon = false;
    while (r.next(f)) {
        switch (f.num) {
        case 1: x.id = f.u32(); break;
        case 2: x.lat_i = f.i32(); lat = true; break;
        case 3: x.lon_i = f.i32(); lon = true; break;
        case 4: x.expire = f.u32(); break;
        case 6: x.name = f.str(); break;
        case 7: x.description = f.str(); break;
        default: break;
        }
    }
    x.has_latlon = lat && lon;
    return r.ok();
}

// ---- Data, MeshPacket
std::string encode(const Data& d) {
    Writer w;
    w.varint(1, d.portnum);
    w.bytes(2, d.payload);
    w.boolean(3, d.want_response);
    w.fixed32(4, d.dest);
    w.fixed32(5, d.source);
    w.fixed32(6, d.request_id);
    w.fixed32(7, d.reply_id);
    w.fixed32(8, d.emoji);
    if (d.has_bitfield) w.varint_always(9, d.bitfield);
    return w.take();
}
bool decode(std::string_view s, Data& d) {
    d = Data{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: d.portnum = f.u32(); break;
        case 2: d.payload = f.str(); break;
        case 3: d.want_response = f.b(); break;
        case 4: d.dest = f.u32(); break;
        case 5: d.source = f.u32(); break;
        case 6: d.request_id = f.u32(); break;
        case 7: d.reply_id = f.u32(); break;
        case 8: d.emoji = f.u32(); break;
        case 9: d.bitfield = f.u32(); d.has_bitfield = true; break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const MeshPacket& p) {
    Writer w;
    w.fixed32(1, p.from);
    w.fixed32(2, p.to);
    w.varint(3, p.channel);
    if (p.has_decoded) {
        Writer d;
        d.raw(encode(p.decoded));
        w.sub(4, d);
    } else {
        w.bytes(5, p.encrypted);
    }
    w.fixed32(6, p.id);
    if (p.rx_time) w.fixed32_always(7, p.rx_time);
    w.f32(8, p.rx_snr);
    w.varint(9, p.hop_limit);
    w.boolean(10, p.want_ack);
    w.varint(11, p.priority);
    if (p.rx_rssi) w.int32_always(12, p.rx_rssi);
    w.boolean(14, p.via_mqtt);
    w.varint(15, p.hop_start);
    w.bytes(16, p.public_key);
    w.boolean(17, p.pki_encrypted);
    w.varint(18, p.next_hop);
    w.varint(19, p.relay_node);
    return w.take();
}
bool decode(std::string_view s, MeshPacket& p) {
    p = MeshPacket{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: p.from = f.u32(); break;
        case 2: p.to = f.u32(); break;
        case 3: p.channel = f.u32(); break;
        case 4: p.has_decoded = decode(f.data, p.decoded); break;
        case 5: p.encrypted = f.str(); break;
        case 6: p.id = f.u32(); break;
        case 7: p.rx_time = f.u32(); break;
        case 8: p.rx_snr = f.f(); break;
        case 9: p.hop_limit = f.u32(); break;
        case 10: p.want_ack = f.b(); break;
        case 11: p.priority = f.u32(); break;
        case 12: p.rx_rssi = f.i32(); break;
        case 14: p.via_mqtt = f.b(); break;
        case 15: p.hop_start = f.u32(); break;
        case 16: p.public_key = f.str(); break;
        case 17: p.pki_encrypted = f.b(); break;
        case 18: p.next_hop = f.u32(); break;
        case 19: p.relay_node = f.u32(); break;
        default: break;
        }
    }
    return r.ok();
}

// ---- NodeInfo, MyNodeInfo, DeviceMetadata
std::string encode(const NodeInfo& n) {
    Writer w;
    w.varint(1, n.num);
    if (n.has_user) {
        Writer s;
        s.raw(encode(n.user));
        w.sub(2, s);
    }
    if (n.has_position) {
        Writer s;
        s.raw(encode(n.position));
        w.sub(3, s);
    }
    w.f32(4, n.snr);
    w.fixed32(5, n.last_heard);
    if (n.has_metrics) {
        Writer s;
        s.raw(encode(n.metrics));
        w.sub(6, s);
    }
    w.varint(7, n.channel);
    w.boolean(8, n.via_mqtt);
    if (n.has_hops) w.varint_always(9, n.hops_away);
    w.boolean(10, n.is_favorite);
    w.boolean(11, n.is_ignored);
    w.boolean(13, n.is_muted);
    return w.take();
}
bool decode(std::string_view s, NodeInfo& n) {
    n = NodeInfo{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: n.num = f.u32(); break;
        case 2: n.has_user = decode(f.data, n.user); break;
        case 3: n.has_position = decode(f.data, n.position); break;
        case 4: n.snr = f.f(); break;
        case 5: n.last_heard = f.u32(); break;
        case 6: n.has_metrics = decode(f.data, n.metrics); break;
        case 7: n.channel = f.u32(); break;
        case 8: n.via_mqtt = f.b(); break;
        case 9: n.hops_away = f.u32(); n.has_hops = true; break;
        case 10: n.is_favorite = f.b(); break;
        case 11: n.is_ignored = f.b(); break;
        case 13: n.is_muted = f.b(); break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const MyNodeInfo& m) {
    Writer w;
    w.varint(1, m.my_node_num);
    w.varint(8, m.reboot_count);
    w.varint(11, m.min_app_version);
    w.bytes(13, m.pio_env);
    w.varint(15, m.nodedb_count);
    return w.take();
}
bool decode(std::string_view s, MyNodeInfo& m) {
    m = MyNodeInfo{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: m.my_node_num = f.u32(); break;
        case 8: m.reboot_count = f.u32(); break;
        case 11: m.min_app_version = f.u32(); break;
        case 13: m.pio_env = f.str(); break;
        case 15: m.nodedb_count = f.u32(); break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const DeviceMetadata& m) {
    Writer w;
    w.bytes(1, m.firmware_version);
    w.varint(2, m.device_state_version);
    w.boolean(3, m.can_shutdown);
    w.boolean(4, m.has_wifi);
    w.boolean(5, m.has_bluetooth);
    w.boolean(6, m.has_ethernet);
    w.varint(7, m.role);
    w.varint(9, m.hw_model);
    w.boolean(11, m.has_pkc);
    return w.take();
}
bool decode(std::string_view s, DeviceMetadata& m) {
    m = DeviceMetadata{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: m.firmware_version = f.str(); break;
        case 2: m.device_state_version = f.u32(); break;
        case 3: m.can_shutdown = f.b(); break;
        case 4: m.has_wifi = f.b(); break;
        case 5: m.has_bluetooth = f.b(); break;
        case 6: m.has_ethernet = f.b(); break;
        case 7: m.role = f.u32(); break;
        case 9: m.hw_model = f.u32(); break;
        case 11: m.has_pkc = f.b(); break;
        default: break;
        }
    }
    return r.ok();
}

// ---- Channels
std::string encode(const ChannelSettings& c) {
    std::string base = c.raw;
    std::string module = std::string(pb::find_bytes(base, 7));
    for (uint32_t n : {2u, 3u, 4u, 5u, 6u, 7u}) base = pb::without(base, n);
    module = pb::with_varint(module, 1, c.position_precision);
    module = pb::with_varint(module, 2, c.is_muted ? 1 : 0);
    Writer w;
    w.bytes(2, c.psk);
    w.bytes(3, c.name);
    w.fixed32(4, c.id);
    w.boolean(5, c.uplink_enabled);
    w.boolean(6, c.downlink_enabled);
    w.bytes(7, module);
    w.raw(base);
    return w.take();
}
bool decode(std::string_view s, ChannelSettings& c) {
    c = ChannelSettings{};
    c.raw = std::string(s);
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 2: c.psk = f.str(); break;
        case 3: c.name = f.str(); break;
        case 4: c.id = f.u32(); break;
        case 5: c.uplink_enabled = f.b(); break;
        case 6: c.downlink_enabled = f.b(); break;
        case 7:
            c.position_precision = uint32_t(pb::find_varint(f.data, 1));
            c.is_muted = pb::find_varint(f.data, 2) != 0;
            break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const Channel& c) {
    Writer w;
    w.int32(1, c.index);
    // A disabled channel has no settings.
    if (c.role != Channel::Disabled) {
        Writer s;
        s.raw(encode(c.settings));
        w.sub(2, s);
    }
    w.varint(3, c.role);
    return w.take();
}
bool decode(std::string_view s, Channel& c) {
    c = Channel{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: c.index = f.i32(); break;
        case 2: decode(f.data, c.settings); break;
        case 3: c.role = f.u32(); break;
        default: break;
        }
    }
    return r.ok();
}

// ---- Config
std::string encode(const Config& c) {
    Writer w;
    if (c.which) w.bytes_always(c.which, c.raw);
    return w.take();
}
bool decode(std::string_view s, Config& c) {
    c = Config{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        if (f.wire == pb::kBytes) {
            c.which = f.num;
            c.raw = f.str();
        }
    }
    return r.ok();
}
std::string encode(const LoRaConfig& c) {
    std::string base = c.raw;
    for (uint32_t n : {1u, 2u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 104u, 105u}) base = pb::without(base, n);
    Writer w;
    w.boolean(1, c.use_preset);
    w.varint(2, c.modem_preset);
    w.varint(7, c.region);
    w.varint(8, c.hop_limit);
    w.boolean(9, c.tx_enabled);
    w.int32(10, c.tx_power);
    w.varint(11, c.channel_num);
    w.boolean(12, c.override_duty_cycle);
    w.boolean(13, c.rx_boosted_gain);
    w.boolean(104, c.ignore_mqtt);
    w.boolean(105, c.ok_to_mqtt);
    w.raw(base);
    return w.take();
}
bool decode(std::string_view s, LoRaConfig& c) {
    c = LoRaConfig{};
    c.raw = std::string(s);
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: c.use_preset = f.b(); break;
        case 2: c.modem_preset = f.u32(); break;
        case 7: c.region = f.u32(); break;
        case 8: c.hop_limit = f.u32(); break;
        case 9: c.tx_enabled = f.b(); break;
        case 10: c.tx_power = f.i32(); break;
        case 11: c.channel_num = f.u32(); break;
        case 12: c.override_duty_cycle = f.b(); break;
        case 13: c.rx_boosted_gain = f.b(); break;
        case 104: c.ignore_mqtt = f.b(); break;
        case 105: c.ok_to_mqtt = f.b(); break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const DeviceConfig& c) {
    std::string base = c.raw;
    for (uint32_t n : {1u, 6u, 7u, 11u}) base = pb::without(base, n);
    Writer w;
    w.varint(1, c.role);
    w.varint(6, c.rebroadcast_mode);
    w.varint(7, c.node_info_broadcast_secs);
    w.bytes(11, c.tzdef);
    w.raw(base);
    return w.take();
}
bool decode(std::string_view s, DeviceConfig& c) {
    c = DeviceConfig{};
    c.raw = std::string(s);
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: c.role = f.u32(); break;
        case 6: c.rebroadcast_mode = f.u32(); break;
        case 7: c.node_info_broadcast_secs = f.u32(); break;
        case 11: c.tzdef = f.str(); break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const PositionConfig& c) {
    std::string base = c.raw;
    for (uint32_t n : {1u, 2u, 3u, 13u}) base = pb::without(base, n);
    Writer w;
    w.varint(1, c.position_broadcast_secs);
    w.boolean(2, c.smart_enabled);
    w.boolean(3, c.fixed_position);
    w.varint(13, c.gps_mode);
    w.raw(base);
    return w.take();
}
bool decode(std::string_view s, PositionConfig& c) {
    c = PositionConfig{};
    c.raw = std::string(s);
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: c.position_broadcast_secs = f.u32(); break;
        case 2: c.smart_enabled = f.b(); break;
        case 3: c.fixed_position = f.b(); break;
        case 13: c.gps_mode = f.u32(); break;
        default: break;
        }
    }
    return r.ok();
}

// ---- QueueStatus, ClientNotification
std::string encode(const QueueStatus& q) {
    Writer w;
    w.int32(1, q.res);
    w.varint(2, q.free);
    w.varint(3, q.maxlen);
    w.varint(4, q.mesh_packet_id);
    return w.take();
}
bool decode(std::string_view s, QueueStatus& q) {
    q = QueueStatus{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: q.res = f.i32(); break;
        case 2: q.free = f.u32(); break;
        case 3: q.maxlen = f.u32(); break;
        case 4: q.mesh_packet_id = f.u32(); break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const ClientNotification& n) {
    Writer w;
    if (n.has_reply_id) w.varint_always(1, n.reply_id);
    w.fixed32(2, n.time);
    w.varint(3, n.level);
    w.bytes(4, n.message);
    w.varint(5, n.type);
    return w.take();
}
bool decode(std::string_view s, ClientNotification& n) {
    n = ClientNotification{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        switch (f.num) {
        case 1: n.reply_id = f.u32(); n.has_reply_id = true; break;
        case 2: n.time = f.u32(); break;
        case 3: n.level = f.u32(); break;
        case 4: n.message = f.str(); break;
        case 5: n.type = f.u32(); break;
        default: break;
        }
    }
    return r.ok();
}

// ---- FromRadio, ToRadio
std::string encode(const FromRadio& m) {
    Writer w;
    w.varint(1, m.id);
    Writer s;
    switch (m.which) {
    case FromRadio::Packet: s.raw(encode(m.packet)); w.sub(2, s); break;
    case FromRadio::MyInfo: s.raw(encode(m.my_info)); w.sub(3, s); break;
    case FromRadio::NodeInfoMsg: s.raw(encode(m.node_info)); w.sub(4, s); break;
    case FromRadio::ConfigMsg: s.raw(encode(m.config)); w.sub(5, s); break;
    case FromRadio::ConfigComplete: w.varint_always(7, m.config_complete_id); break;
    case FromRadio::Rebooted: w.varint_always(8, 1); break;
    case FromRadio::ChannelMsg: s.raw(encode(m.channel)); w.sub(10, s); break;
    case FromRadio::QueueStatusMsg: s.raw(encode(m.queue_status)); w.sub(11, s); break;
    case FromRadio::Metadata: s.raw(encode(m.metadata)); w.sub(13, s); break;
    case FromRadio::Notification: s.raw(encode(m.notification)); w.sub(16, s); break;
    default: break;
    }
    return w.take();
}
bool decode(std::string_view s, FromRadio& m) {
    m = FromRadio{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        if (f.num == 1 && f.wire == pb::kVarint) {
            m.id = f.u32();
            continue;
        }
        if (f.num < 2) continue;
        m.which = f.num;
        switch (f.num) {
        case 2: decode(f.data, m.packet); break;
        case 3: decode(f.data, m.my_info); break;
        case 4: decode(f.data, m.node_info); break;
        case 5: decode(f.data, m.config); break;
        case 7: m.config_complete_id = f.u32(); break;
        case 10: decode(f.data, m.channel); break;
        case 11: decode(f.data, m.queue_status); break;
        case 13: decode(f.data, m.metadata); break;
        case 16: decode(f.data, m.notification); break;
        default: break;
        }
    }
    return r.ok();
}
std::string encode(const ToRadio& m) {
    Writer w;
    Writer s;
    switch (m.which) {
    case ToRadio::Packet: s.raw(encode(m.packet)); w.sub(1, s); break;
    case ToRadio::WantConfig: w.varint_always(3, m.want_config_id); break;
    case ToRadio::Disconnect: w.varint_always(4, 1); break;
    case ToRadio::Heartbeat: s.varint(1, m.heartbeat_nonce); w.sub(7, s); break;
    default: break;
    }
    return w.take();
}
bool decode(std::string_view s, ToRadio& m) {
    m = ToRadio{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        m.which = f.num;
        switch (f.num) {
        case 1: decode(f.data, m.packet); break;
        case 3: m.want_config_id = f.u32(); break;
        case 7: m.heartbeat_nonce = uint32_t(pb::find_varint(f.data, 1)); break;
        default: break;
        }
    }
    return r.ok();
}

// ---- AdminMessage
std::string encode(const AdminMessage& a) {
    Writer w;
    Writer s;
    switch (a.which) {
    case AdminMessage::GetOwnerResponse:
    case AdminMessage::SetOwner: s.raw(encode(a.user)); w.sub(a.which, s); break;
    case AdminMessage::GetChannelResponse:
    case AdminMessage::SetChannel: s.raw(encode(a.channel)); w.sub(a.which, s); break;
    case AdminMessage::GetConfigResponse:
    case AdminMessage::SetConfig: s.raw(encode(a.config)); w.sub(a.which, s); break;
    case AdminMessage::SetFixedPosition: s.raw(encode(a.position)); w.sub(a.which, s); break;
    case AdminMessage::GetMetadataResponse: s.raw(encode(a.metadata)); w.sub(a.which, s); break;
    case AdminMessage::None: break;
    default:
        // A scalar: a node number, seconds, a channel index, or just "true".
        w.varint_always(a.which, a.value);
        break;
    }
    return w.take();
}
bool decode(std::string_view s, AdminMessage& a) {
    a = AdminMessage{};
    Reader r(s);
    Field f;
    while (r.next(f)) {
        if (f.num == 101) continue;   // session_passkey: not needed on a local connection
        a.which = f.num;
        if (f.wire == pb::kVarint) a.value = f.v;
        switch (f.num) {
        case AdminMessage::GetOwnerResponse:
        case AdminMessage::SetOwner: decode(f.data, a.user); break;
        case AdminMessage::GetChannelResponse:
        case AdminMessage::SetChannel: decode(f.data, a.channel); break;
        case AdminMessage::GetConfigResponse:
        case AdminMessage::SetConfig: decode(f.data, a.config); break;
        case AdminMessage::SetFixedPosition: decode(f.data, a.position); break;
        case AdminMessage::GetMetadataResponse: decode(f.data, a.metadata); break;
        default: break;
        }
    }
    return r.ok();
}

// ---- Framing
std::string frame(std::string_view payload) {
    std::string out;
    out.reserve(payload.size() + 4);
    out.push_back(char(0x94));
    out.push_back(char(0xC3));
    out.push_back(char((payload.size() >> 8) & 0xff));
    out.push_back(char(payload.size() & 0xff));
    out.append(payload);
    return out;
}
bool Deframer::next(std::string& payload) {
    for (;;) {
        // Anything before the two start bytes is the daemon's console text.
        size_t i = 0;
        while (i < buf_.size() && uint8_t(buf_[i]) != 0x94) ++i;
        if (i) buf_.erase(0, i);
        if (buf_.size() < 4) return false;
        if (uint8_t(buf_[1]) != 0xC3) {
            buf_.erase(0, 1);
            continue;
        }
        size_t len = (size_t(uint8_t(buf_[2])) << 8) | uint8_t(buf_[3]);
        if (len > kMaxFrame) {   // not a real header
            buf_.erase(0, 1);
            continue;
        }
        if (buf_.size() < 4 + len) return false;
        payload.assign(buf_, 4, len);
        buf_.erase(0, 4 + len);
        return true;
    }
}

// ---- Names
const std::vector<Named>& regions() {
    static const std::vector<Named> v = {
        {1, "US", "United States 915 MHz"},
        {3, "EU_868", "Europe 868 MHz"},
        {2, "EU_433", "Europe 433 MHz"},
        {6, "ANZ", "Australia / NZ 915 MHz"},
        {22, "ANZ_433", "Australia / NZ 433 MHz"},
        {11, "NZ_865", "New Zealand 865 MHz"},
        {26, "BR_902", "Brazil 902 MHz"},
        {4, "CN", "China 470 MHz"},
        {10, "IN", "India 865 MHz"},
        {5, "JP", "Japan 920 MHz"},
        {7, "KR", "Korea 920 MHz"},
        {23, "KZ_433", "Kazakhstan 433 MHz"},
        {24, "KZ_863", "Kazakhstan 863 MHz"},
        {16, "MY_433", "Malaysia 433 MHz"},
        {17, "MY_919", "Malaysia 919 MHz"},
        {25, "NP_865", "Nepal 865 MHz"},
        {19, "PH_433", "Philippines 433 MHz"},
        {20, "PH_868", "Philippines 868 MHz"},
        {21, "PH_915", "Philippines 915 MHz"},
        {9, "RU", "Russia 868 MHz"},
        {18, "SG_923", "Singapore 923 MHz"},
        {12, "TH", "Thailand 920 MHz"},
        {8, "TW", "Taiwan 923 MHz"},
        {14, "UA_433", "Ukraine 433 MHz"},
        {15, "UA_868", "Ukraine 868 MHz"},
        {13, "LORA_24", "2.4 GHz worldwide"},
    };
    return v;
}
const std::vector<Named>& modem_presets() {
    static const std::vector<Named> v = {
        {0, "LongFast", "Long range, fast (the default)"},
        {7, "LongModerate", "Long range, moderate"},
        {1, "LongSlow", "Long range, slow"},
        {9, "LongTurbo", "Long range, turbo"},
        {4, "MediumFast", "Medium range, fast"},
        {3, "MediumSlow", "Medium range, slow"},
        {6, "ShortFast", "Short range, fast"},
        {5, "ShortSlow", "Short range, slow"},
        {8, "ShortTurbo", "Short range, turbo"},
    };
    return v;
}
const std::vector<Named>& device_roles() {
    static const std::vector<Named> v = {
        {0, "Client", "Sends, receives and passes messages on"},
        {1, "Client Mute", "Doesn't pass messages on"},
        {8, "Client Hidden", "Only talks when it has to"},
        {12, "Client Base", "A fixed station for your own nodes"},
        {5, "Tracker", "Sends its position first"},
        {6, "Sensor", "Sends telemetry first"},
        {2, "Router", "A relay on high ground"},
        {11, "Router Late", "A relay that fills gaps"},
    };
    return v;
}
static const char* find_name(const std::vector<Named>& v, uint32_t x, const char* fallback) {
    for (const Named& n : v)
        if (n.value == x) return n.name;
    return fallback;
}
const char* region_name(uint32_t r) {
    if (r == 0) return "Not set";
    switch (r) {
    case 27: return "ITU1_2M";
    case 28: return "ITU2_2M";
    case 29: return "EU_866";
    case 30: return "EU_874";
    case 31: return "EU_917";
    case 32: return "EU_N_868";
    default: return find_name(regions(), r, "Other");
    }
}
const char* preset_name(uint32_t p) {
    switch (p) {
    case 2: return "VLongSlow";
    case 10: return "LiteFast";
    case 11: return "LiteSlow";
    case 12: return "NarrowFast";
    case 13: return "NarrowSlow";
    case 14: return "TinyFast";
    case 15: return "TinySlow";
    case 16: return "MediumTurbo";
    default: return find_name(modem_presets(), p, "Custom");
    }
}
const char* role_name(uint32_t r) {
    switch (r) {
    case 3: return "Router Client";
    case 4: return "Repeater";
    case 7: return "TAK";
    case 9: return "Lost and Found";
    case 10: return "TAK Tracker";
    default: return find_name(device_roles(), r, "Other");
    }
}
std::string hw_model_name(uint32_t m) {
    // mesh.proto, HardwareModel: the ones people carry. The rest show a number.
    switch (m) {
    case 0: return "";
    case 4: return "T-Beam";
    case 9: return "RAK4631";
    case 10: return "Heltec V2.1";
    case 12: return "T-Beam S3 Core";
    case 13: return "RAK11200";
    case 21: return "T-Lora T3-S3";
    case 25: return "Station G1";
    case 26: return "RAK11310";
    case 31: return "Station G2";
    case 37: return "Linux (meshtasticd)";
    case 39: return "DIY";
    case 43: return "Heltec V3";
    case 44: return "Heltec WSL V3";
    case 47: return "Raspberry Pi Pico";
    case 48: return "Heltec Wireless Tracker";
    case 49: return "Heltec Wireless Paper";
    case 50: return "T-Deck";
    case 51: return "T-Watch S3";
    case 58: return "Heltec Capsule Sensor V3";
    case 62: return "CardKB";
    case 64: return "Seeed XIAO S3";
    case 65: return "Heltec Mesh Node T114";
    case 66: return "SenseCAP Indicator";
    case 67: return "Tracker T1000-E";
    case 69: return "Station";
    case 71: return "Seeed Wio Tracker";
    case 84: return "Seeed XIAO nRF52840";
    case 255: return "Private hardware";
    default: {
        char b[24];
        std::snprintf(b, sizeof b, "Model %u", m);
        return b;
    }
    }
}

std::string node_id(uint32_t num) {
    char b[16];
    std::snprintf(b, sizeof b, "!%08x", num);
    return b;
}
uint32_t parse_node_id(std::string_view s) {
    if (!s.empty() && s[0] == '!') s.remove_prefix(1);
    if (s.size() != 8) return 0;
    uint32_t v = 0;
    for (char ch : s) {
        int d = ch >= '0' && ch <= '9' ? ch - '0'
              : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
              : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
        if (d < 0) return 0;
        v = (v << 4) | uint32_t(d);
    }
    return v;
}

// ---- Base64 and channel links
static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static std::string b64(std::string_view in, bool url) {
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = (uint32_t(uint8_t(in[i])) << 16) | (uint32_t(uint8_t(in[i + 1])) << 8) | uint8_t(in[i + 2]);
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        out.push_back(kB64[(v >> 6) & 63]);
        out.push_back(kB64[v & 63]);
    }
    if (i < in.size()) {
        uint32_t v = uint32_t(uint8_t(in[i])) << 16;
        if (i + 1 < in.size()) v |= uint32_t(uint8_t(in[i + 1])) << 8;
        out.push_back(kB64[(v >> 18) & 63]);
        out.push_back(kB64[(v >> 12) & 63]);
        if (i + 1 < in.size()) out.push_back(kB64[(v >> 6) & 63]);
        if (!url) out.append(i + 1 < in.size() ? "=" : "==");
    }
    if (url)
        for (char& ch : out) ch = ch == '+' ? '-' : ch == '/' ? '_' : ch;
    return out;
}
std::string base64(std::string_view in) { return b64(in, false); }
std::string base64url(std::string_view in) { return b64(in, true); }
bool base64_decode(std::string_view in, std::string& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : in) {
        int d;
        if (ch >= 'A' && ch <= 'Z') d = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') d = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') d = ch - '0' + 52;
        else if (ch == '+' || ch == '-') d = 62;
        else if (ch == '/' || ch == '_') d = 63;
        else if (ch == '=') break;
        else if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') continue;
        else return false;
        acc = (acc << 6) | uint32_t(d);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(char((acc >> bits) & 0xff));
        }
    }
    return true;
}

std::string channel_url(const ChannelSet& set) {
    Writer w;
    for (const ChannelSettings& c : set.settings) {
        Writer s;
        s.raw(encode(c));
        w.sub(1, s);
    }
    if (set.has_lora) {
        Writer s;
        s.raw(encode(set.lora));
        w.sub(2, s);
    }
    return std::string("https://meshtastic.org/e/") + (set.add_only ? "?add=true" : "") + "#" + base64url(w.str());
}
bool parse_channel_url(std::string_view url, ChannelSet& set) {
    set = ChannelSet{};
    while (!url.empty() && (url.back() == ' ' || url.back() == '\n' || url.back() == '\r')) url.remove_suffix(1);
    while (!url.empty() && url.front() == ' ') url.remove_prefix(1);
    size_t hash = url.rfind('#');
    std::string_view code = url;
    if (hash != std::string_view::npos) {
        std::string_view head = url.substr(0, hash);
        if (head.find("/e/") == std::string_view::npos && head.find("/e?") == std::string_view::npos) return false;
        set.add_only = head.find("add=true") != std::string_view::npos;
        code = url.substr(hash + 1);
    } else if (url.find("://") != std::string_view::npos || url.find('/') != std::string_view::npos) {
        return false;   // a link, but not a channel link
    }
    std::string bytes;
    if (code.empty() || !base64_decode(code, bytes) || bytes.empty()) return false;
    Reader r(bytes);
    Field f;
    while (r.next(f)) {
        if (f.num == 1 && f.wire == pb::kBytes) {
            ChannelSettings c;
            if (!decode(f.data, c)) return false;
            set.settings.push_back(std::move(c));
        } else if (f.num == 2 && f.wire == pb::kBytes) {
            set.has_lora = decode(f.data, set.lora);
        }
    }
    return r.ok() && !set.settings.empty() && set.settings.size() <= 8;
}

std::string channel_display_name(const ChannelSettings& c, const LoRaConfig& lora) {
    if (!c.name.empty()) return c.name;
    return lora.use_preset || lora.raw.empty() ? preset_name(lora.modem_preset) : "Custom";
}
const char* psk_kind(const std::string& psk) {
    if (psk.empty()) return "No encryption";
    if (psk.size() == 1) return uint8_t(psk[0]) == 0 ? "No encryption" : "Default key";
    return "Private key";
}

}  // namespace zs::mesh
