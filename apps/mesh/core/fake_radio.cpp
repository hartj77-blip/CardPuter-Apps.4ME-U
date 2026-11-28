#include "apps/mesh/core/fake_radio.h"

#include <algorithm>
#include <cmath>

#include "apps/mesh/core/pb.h"

namespace zs::mesh {

namespace {
// Where the made-up mesh lives: a valley in the Alps.
constexpr double kBaseLat = 46.6340, kBaseLon = 8.5940;
constexpr int kBaseAlt = 1440;

const char* const kReplies[] = {
    "Copy that.", "Loud and clear here.", "On my way, 10 min.", "Got it, thanks!", "Say again? Bad signal up here.",
};
const char* const kChatter[] = {
    "Anyone near the north trailhead?", "Weather is turning, heading down.", "Radio check from the ridge.",
    "Coffee at the hut in 20?",         "Snow line is at 2100 m today.",     "Battery at half, going quiet for a bit.",
};
}  // namespace

FakeRadio::FakeRadio() : FakeRadio(Options{}) {}

FakeRadio::FakeRadio(const Options& o) : opt_(o) {
    rng_ = o.seed ? o.seed : 1;
    build_mesh();
}

uint32_t FakeRadio::rnd() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}
uint32_t FakeRadio::new_id() { return ++id_; }

void FakeRadio::build_mesh() {
    channels_.resize(8);
    for (int i = 0; i < 8; ++i) channels_[size_t(i)].index = i;
    channels_[0].role = Channel::Primary;
    channels_[0].settings.psk = std::string("\x01", 1);
    channels_[1].role = Channel::Secondary;
    channels_[1].settings.name = "Friends";
    for (int i = 0; i < 16; ++i) channels_[1].settings.psk.push_back(char(0x40 + i * 7));

    LoRaConfig l;
    l.use_preset = true;
    l.modem_preset = 0;
    l.region = opt_.region_set ? 3 : 0;   // EU_868
    l.hop_limit = 3;
    l.tx_enabled = true;
    l.tx_power = 22;
    lora_raw_ = encode(l);
    DeviceConfig d;
    d.node_info_broadcast_secs = 10800;
    device_raw_ = encode(d);
    PositionConfig p;
    p.position_broadcast_secs = 900;
    p.smart_enabled = true;
    p.gps_mode = 2;
    position_raw_ = encode(p);

    if (!opt_.neighbours) return;
    auto add = [this](uint32_t num, const char* ln, const char* sn, uint32_t hw, uint32_t role, int hops, float snr,
                      int rssi, long ago, double north, double east, int alt, int batt) -> SimNode& {
        SimNode n;
        n.num = num;
        n.long_name = ln;
        n.short_name = sn;
        n.hw = hw;
        n.role = role;
        n.hops = hops;
        n.snr = snr;
        n.rssi = rssi;
        n.heard_ago = ago;
        n.has_pos = !(north == 0 && east == 0);
        n.north_m = north;
        n.east_m = east;
        n.alt = alt;
        n.battery = batt;
        nodes_.push_back(n);
        return nodes_.back();
    };
    SimNode& hill = add(0x7a3f10c4, "Hilltop Relay", "HILL", 9, 2, 0, 6.5f, -84, 40, 2100, 350, 2310, 101);
    hill.has_env = true;
    hill.temp = -2.5f;
    hill.humidity = 68;
    hill.pressure = 772.4f;
    add(0x5be02a91, "Anna's T-Deck", "ANNA", 50, 0, 0, -3.25f, -103, 180, -620, 580, 1455, 82);
    add(0x2d9c44e7, "Base Camp", "BASE", 43, 12, 1, 0, 0, 300, -3900, -2600, 1210, 100);
    add(0x91c6b3d2, "Marek Mobile", "MRK", 67, 5, 2, 0, 0, 1500, 6400, 4100, 1890, 64);
    SimNode& wx = add(0x33aa7e15, "Weather Station", "WX1", 65, 6, 1, 0, 0, 700, 1300, -5200, 2040, 91);
    wx.has_env = true;
    wx.temp = 1.5f;
    wx.humidity = 81;
    wx.pressure = 798.1f;
    SimNode& sol = add(0x48d1f6a0, "Solar Node 7", "SOL7", 9, 11, 3, 0, 0, 5 * 3600, 14800, -9300, 2650, 37);
    sol.reachable = false;
    SimNode& trun = add(0x6e52c08b, "Trail Runner", "TRUN", 67, 5, 0, -16.5f, -121, 95, 380, -240, 1470, 48);
    trun.shares_position = false;
    SimNode& mq = add(0x1f07b9ce, "Valley Gateway", "GATE", 37, 0, 0, 0, 0, 900, 0, 0, 0, -1);
    mq.mqtt = true;
    SimNode& anon = add(0x9f3c21aa, "", "", 0, 0, 2, 0, 0, 2 * 86400, 0, 0, 0, -1);
    anon.named = false;
    anon.reachable = false;
    anon.has_key = false;
}

// ---- Out

void FakeRadio::emit_raw(const std::string& payload) {
    if (down_) return;
    out_ += frame(payload);
}
void FakeRadio::emit(const FromRadio& f) {
    FromRadio x = f;
    x.id = from_id_++;
    emit_raw(encode(x));
}
std::string FakeRadio::take_out() {
    std::string o;
    o.swap(out_);
    return o;
}
void FakeRadio::at(long delay_ms, std::function<void()> fn) { events_.push_back({now_ms_ + delay_ms, std::move(fn)}); }

void FakeRadio::advance(long now_ms) {
    if (now_ms < now_ms_) return;
    for (;;) {
        // The earliest event that's due, one at a time: one may add another.
        size_t pick = events_.size();
        for (size_t i = 0; i < events_.size(); ++i)
            if (events_[i].due <= now_ms && (pick == events_.size() || events_[i].due < events_[pick].due)) pick = i;
        if (pick == events_.size()) break;
        Event e = std::move(events_[pick]);
        events_.erase(events_.begin() + long(pick));
        now_ms_ = std::max(now_ms_, e.due);
        e.run();
    }
    now_ms_ = now_ms;
    if (opt_.chatter && opt_.neighbours && !down_ && now_ms_ >= next_chatter_ms_) {
        chatter();
        next_chatter_ms_ = now_ms_ + 25000 + long(rnd() % 30000);
    }
}

bool FakeRadio::take_restart() {
    bool r = restarted_;
    restarted_ = false;
    return r;
}
void FakeRadio::reconnect() {
    down_ = false;
    out_.clear();
    in_.clear();
}

// ---- The mesh behind it

FakeRadio::SimNode* FakeRadio::find(uint32_t num) {
    for (SimNode& n : nodes_)
        if (n.num == num) return &n;
    return nullptr;
}
const FakeRadio::SimNode* FakeRadio::find(uint32_t num) const {
    for (const SimNode& n : nodes_)
        if (n.num == num) return &n;
    return nullptr;
}
bool FakeRadio::has_node(uint32_t num) const { return find(num) != nullptr; }
bool FakeRadio::node_favorite(uint32_t num) const {
    const SimNode* n = find(num);
    return n && n->favorite;
}
bool FakeRadio::node_ignored(uint32_t num) const {
    const SimNode* n = find(num);
    return n && n->ignored;
}
bool FakeRadio::own_position(double& lat, double& lon) const {
    lat = own_lat_;
    lon = own_lon_;
    return own_pos_;
}
uint32_t FakeRadio::node_by_short(const std::string& s) const {
    for (const SimNode& n : nodes_)
        if (n.short_name == s) return n.num;
    return 0;
}
LoRaConfig FakeRadio::lora() const {
    LoRaConfig l;
    decode(lora_raw_, l);
    return l;
}
DeviceConfig FakeRadio::device() const {
    DeviceConfig d;
    decode(device_raw_, d);
    return d;
}

Position FakeRadio::position_of(const SimNode& n) const {
    Position p;
    if (!n.has_pos) return p;
    double lat = kBaseLat + n.north_m / 111320.0;
    double lon = kBaseLon + n.east_m / (111320.0 * std::cos(kBaseLat * 3.14159265358979 / 180.0));
    p.has_latlon = true;
    p.lat_i = int32_t(std::lround(lat * 1e7));
    p.lon_i = int32_t(std::lround(lon * 1e7));
    p.has_alt = true;
    p.alt = n.alt;
    p.time = uint32_t(unix_now() - std::min<long>(n.heard_ago, 600));
    p.precision_bits = 32;
    return p;
}

NodeInfo FakeRadio::info_of(const SimNode& n) const {
    NodeInfo i;
    i.num = n.num;
    if (n.named) {
        i.has_user = true;
        i.user.id = node_id(n.num);
        i.user.long_name = n.long_name;
        i.user.short_name = n.short_name;
        i.user.hw_model = n.hw;
        i.user.role = n.role;
        if (n.has_key) i.user.public_key = std::string(32, char(n.num & 0xff));
    }
    if (n.has_pos) {
        i.has_position = true;
        i.position = position_of(n);
    }
    i.snr = n.hops == 0 && !n.mqtt ? n.snr : 0;
    i.last_heard = uint32_t(unix_now() - n.heard_ago);
    if (n.battery >= 0) {
        i.has_metrics = true;
        i.metrics.has_battery = true;
        i.metrics.battery_level = uint32_t(n.battery);
        i.metrics.has_voltage = true;
        i.metrics.voltage = n.battery > 100 ? 4.18f : 3.3f + 0.009f * float(n.battery);
        i.metrics.has_ch_util = true;
        i.metrics.channel_utilization = 6.5f + float(n.num % 7);
        i.metrics.has_air_util = true;
        i.metrics.air_util_tx = 0.4f + float(n.num % 5) * 0.3f;
        i.metrics.has_uptime = true;
        i.metrics.uptime_seconds = 3600 * (4 + n.num % 90);
    }
    i.via_mqtt = n.mqtt;
    i.has_hops = true;
    i.hops_away = uint32_t(n.hops);
    i.is_favorite = n.favorite;
    i.is_ignored = n.ignored;
    return i;
}

NodeInfo FakeRadio::own_info() const {
    NodeInfo i;
    i.num = kMe;
    i.has_user = true;
    i.user.id = node_id(kMe);
    i.user.long_name = long_name_;
    i.user.short_name = short_name_;
    i.user.hw_model = 37;
    i.user.public_key = std::string(32, '\x5a');
    if (own_pos_) {
        i.has_position = true;
        i.position.has_latlon = true;
        i.position.lat_i = int32_t(std::lround(own_lat_ * 1e7));
        i.position.lon_i = int32_t(std::lround(own_lon_ * 1e7));
        i.position.has_alt = true;
        i.position.alt = own_alt_;
        i.position.time = uint32_t(unix_now());
    }
    i.last_heard = uint32_t(unix_now());
    i.has_metrics = true;
    i.metrics.has_battery = true;
    i.metrics.battery_level = 101;
    i.metrics.has_ch_util = true;
    i.metrics.channel_utilization = 8.4f;
    i.metrics.has_air_util = true;
    i.metrics.air_util_tx = 0.6f;
    i.metrics.has_uptime = true;
    i.metrics.uptime_seconds = uint32_t(600 + now_ms_ / 1000);
    i.has_hops = true;
    i.is_favorite = true;
    return i;
}

// ---- The settings download (PhoneAPI's states, in its order)

void FakeRadio::send_config(uint32_t nonce) {
    ++downloads_;
    bool only_config = nonce == 69420, only_nodes = nonce == 69421;
    FromRadio f;
    if (!only_nodes) {
        f.which = FromRadio::MyInfo;
        f.my_info.my_node_num = kMe;
        f.my_info.reboot_count = uint32_t(3 + restarts_);
        f.my_info.min_app_version = 30200;
        f.my_info.pio_env = "native";
        f.my_info.nodedb_count = uint32_t(nodes_.size() + 1);
        emit(f);
    }
    f = FromRadio{};
    f.which = FromRadio::NodeInfoMsg;
    f.node_info = own_info();
    emit(f);
    if (!only_nodes) {
        f = FromRadio{};
        f.which = FromRadio::Metadata;
        f.metadata.firmware_version = "2.7.26.sim";
        f.metadata.device_state_version = 24;
        f.metadata.can_shutdown = true;
        f.metadata.has_wifi = true;
        f.metadata.has_ethernet = true;
        f.metadata.hw_model = 37;
        f.metadata.has_pkc = true;
        emit(f);
        for (const Channel& c : channels_) {
            f = FromRadio{};
            f.which = FromRadio::ChannelMsg;
            f.channel = c;
            emit(f);
        }
        auto config = [this](uint32_t which, const std::string& raw) {
            FromRadio c;
            c.which = FromRadio::ConfigMsg;
            c.config.which = which;
            c.config.raw = raw;
            emit(c);
        };
        config(Config::Device, device_raw_);
        config(Config::PositionCfg, position_raw_);
        config(Config::Power, "");
        config(Config::Network, "");
        config(Config::Display, "");
        config(Config::LoRa, lora_raw_);
        config(Config::Bluetooth, "");
        config(Config::Security, "");
        // Module settings: Mesh reads none of them, but they are part of the
        // stream and a client must step over them. MQTT and telemetry, empty.
        for (uint32_t module : {1u, 6u}) {
            pb::Writer w, m, inner;
            m.sub(module, inner);
            w.varint(1, from_id_++);
            w.sub(9, m);
            emit_raw(w.str());
        }
    }
    if (!only_config) {
        for (const SimNode& n : nodes_) {
            f = FromRadio{};
            f.which = FromRadio::NodeInfoMsg;
            f.node_info = info_of(n);
            emit(f);
        }
    }
    f = FromRadio{};
    f.which = FromRadio::ConfigComplete;
    f.config_complete_id = nonce;
    emit(f);
}

// ---- In

void FakeRadio::feed(const char* data, size_t n) {
    if (down_) return;
    in_.feed(data, n);
    std::string payload;
    while (in_.next(payload)) {
        ToRadio t;
        if (decode(payload, t)) on_to_radio(t);
        if (down_) break;
    }
}

void FakeRadio::queue_status(int res, uint32_t id) {
    FromRadio f;
    f.which = FromRadio::QueueStatusMsg;
    f.queue_status.res = res;
    f.queue_status.free = 15;
    f.queue_status.maxlen = 16;
    f.queue_status.mesh_packet_id = id;
    emit(f);
}

void FakeRadio::routing(uint32_t from, uint32_t request_id, uint32_t error) {
    FromRadio f;
    f.which = FromRadio::Packet;
    MeshPacket& p = f.packet;
    p.from = from;
    p.to = kMe;
    p.id = new_id();
    p.rx_time = uint32_t(unix_now());
    p.has_decoded = true;
    p.decoded.portnum = kPortRouting;
    Routing r;
    r.which = Routing::ErrorReason;
    r.error_reason = error;
    p.decoded.payload = encode(r);
    p.decoded.request_id = request_id;
    if (const SimNode* n = from != kMe ? find(from) : nullptr) {
        p.hop_start = 3;
        p.hop_limit = uint32_t(3 - n->hops);
        if (n->hops == 0) {
            p.rx_snr = n->snr;
            p.rx_rssi = n->rssi;
        }
    }
    emit(f);
}

void FakeRadio::on_to_radio(const ToRadio& t) {
    switch (t.which) {
    case ToRadio::WantConfig: send_config(t.want_config_id); break;
    case ToRadio::Heartbeat: queue_status(0, 0); break;
    case ToRadio::Disconnect: out_.clear(); break;
    case ToRadio::Packet: on_packet(t.packet); break;
    default: break;
    }
}

// A packet from a node of the mesh, as the radio hands it to its client.
void FakeRadio::deliver(MeshPacket p, const SimNode* via) {
    if (!p.id) p.id = new_id();
    p.rx_time = uint32_t(unix_now());
    if (via) {
        p.hop_start = 3;
        p.hop_limit = uint32_t(std::max(0, 3 - via->hops));
        p.via_mqtt = via->mqtt;
        if (via->hops == 0 && !via->mqtt) {
            p.rx_snr = via->snr;
            p.rx_rssi = via->rssi;
        }
    }
    FromRadio f;
    f.which = FromRadio::Packet;
    f.packet = std::move(p);
    emit(f);
}

void FakeRadio::inject(const MeshPacket& p) {
    SimNode* n = find(p.from);
    if (n) n->heard_ago = 0;
    deliver(p, n);
}

void FakeRadio::inject_text(uint32_t from, uint32_t to, uint32_t channel, const std::string& text, uint32_t reply_id,
                            uint32_t emoji) {
    MeshPacket p;
    p.from = from;
    p.to = to;
    p.channel = channel;
    p.has_decoded = true;
    p.decoded.portnum = kPortText;
    p.decoded.payload = text;
    p.decoded.reply_id = reply_id;
    p.decoded.emoji = emoji;
    inject(p);
}

void FakeRadio::on_packet(MeshPacket p) {
    if (!p.has_decoded) return;
    if (!p.id) p.id = new_id();
    last_id_ = p.id;
    uint32_t id = p.id, port = p.decoded.portnum;
    bool local = p.to == kMe || p.to == 0;

    // What the firmware refuses before it queues anything (PhoneAPI::handleToRadioPacket).
    if (port == kPortTraceroute) {
        if (now_ms_ - last_trace_ms_ < 30000) {
            FromRadio f;
            f.which = FromRadio::Notification;
            f.notification.has_reply_id = true;
            f.notification.reply_id = id;
            f.notification.level = 30;
            f.notification.time = uint32_t(unix_now());
            f.notification.message = "TraceRoute can only be sent once every 30 seconds";
            f.notification.type = 2;
            emit(f);
            queue_status(0, id);
            routing(kMe, id, kErrRateLimit);
            return;
        }
        last_trace_ms_ = now_ms_;
    } else if (port == kPortText) {
        if (now_ms_ - last_text_ms_ < 2000) {
            queue_status(0, id);
            routing(kMe, id, kErrRateLimit);
            return;
        }
        last_text_ms_ = now_ms_;
    }

    if (local) {
        queue_status(0, id);
        if (port == kPortAdmin) {
            on_admin(p);
        } else if (port == kPortPosition) {
            Position pos;
            if (decode(p.decoded.payload, pos) && pos.has_latlon) {
                own_pos_ = true;
                own_lat_ = pos.lat();
                own_lon_ = pos.lon();
                own_alt_ = pos.alt;
            }
        }
        return;
    }

    LoRaConfig l = lora();
    if (l.region == 0 || !l.tx_enabled) {
        queue_status(34, id);   // ERRNO_DISABLED, and no ack follows
        return;
    }
    queue_status(0, id);
    air_.push_back(p);
    if (air_.size() > 64) air_.erase(air_.begin());

    SimNode* dest = p.to == kBroadcast ? nullptr : find(p.to);
    bool anyone = false;
    for (const SimNode& n : nodes_)
        if (n.hops == 0 && !n.mqtt) anyone = true;
    uint32_t chan = p.channel;

    if (p.to == kBroadcast) {
        if (port == kPortText) ++texts_taken_;
        if (!p.want_ack) return;
        if (!anyone) {
            at(6000, [this, id] { routing(kMe, id, kErrMaxRetransmit); });
            return;
        }
        at(800, [this, id] { routing(kMe, id, kErrNone); });   // someone repeated it
        if (port == kPortText && opt_.chatter && p.decoded.emoji == 0) {
            at(3500 + long(rnd() % 2000), [this, chan, id] {
                std::vector<const SimNode*> who;
                for (const SimNode& n : nodes_)
                    if (n.reachable && n.named && !n.mqtt && n.role != 2 && n.role != 6 && n.role != 11) who.push_back(&n);
                if (who.empty()) return;
                const SimNode* n = who[size_t(chat_turn_++) % who.size()];
                MeshPacket r;
                r.from = n->num;
                r.to = kBroadcast;
                r.channel = chan;
                r.has_decoded = true;
                r.decoded.portnum = kPortText;
                r.decoded.payload = kReplies[size_t(chat_turn_) % (sizeof kReplies / sizeof *kReplies)];
                (void)id;
                deliver(r, n);
            });
        }
        return;
    }

    // To one node.
    if (port == kPortText) ++texts_taken_;
    if (!dest || !dest->reachable) {
        at(8000, [this, id] { routing(kMe, id, kErrMaxRetransmit); });
        return;
    }
    uint32_t to = dest->num;
    int hops = dest->hops;
    long there = 1200 + 900L * hops;
    if (hops > 0 && p.want_ack) at(700, [this, id] { routing(kMe, id, kErrNone); });   // heard the next hop repeat it
    switch (port) {
    case kPortText:
    case kPortWaypoint:
    case kPortAlert:
        if (p.want_ack) at(there, [this, to, id] { routing(to, id, kErrNone); });
        if (port == kPortText && opt_.chatter && p.decoded.emoji == 0) {
            at(there + 3500, [this, to, id] {
                const SimNode* n = find(to);
                if (!n) return;
                MeshPacket r;
                r.from = to;
                r.to = kMe;
                r.has_decoded = true;
                r.decoded.portnum = kPortText;
                r.decoded.payload = kReplies[size_t(chat_turn_++) % (sizeof kReplies / sizeof *kReplies)];
                r.pki_encrypted = n->has_key;
                (void)id;
                deliver(r, n);
            });
        }
        break;
    case kPortTraceroute:
        at(there + 800, [this, to, id, hops] {
            const SimNode* n = find(to);
            if (!n) return;
            RouteDiscovery d;
            // Through the relay on the hill, then whoever else is in between.
            std::vector<uint32_t> between;
            if (hops >= 1) between.push_back(0x7a3f10c4);
            if (hops >= 2) between.push_back(0x2d9c44e7);
            if (hops >= 3) between.push_back(kBroadcast);   // a node that wouldn't say who it is
            d.route = between;
            for (size_t i = 0; i <= between.size(); ++i) d.snr_towards.push_back(int32_t(26 - 14 * int(i)));
            d.route_back.assign(between.rbegin(), between.rend());
            for (size_t i = 0; i <= between.size(); ++i) d.snr_back.push_back(i == between.size() ? 22 : int32_t(18 - 10 * int(i)));
            MeshPacket r;
            r.from = to;
            r.to = kMe;
            r.has_decoded = true;
            r.decoded.portnum = kPortTraceroute;
            r.decoded.payload = encode(d);
            r.decoded.request_id = id;
            deliver(r, n);
        });
        break;
    case kPortPosition:
        if (!p.decoded.want_response) break;
        at(there, [this, to, id] {
            const SimNode* n = find(to);
            if (!n) return;
            if (!n->has_pos || !n->shares_position) {
                routing(to, id, kErrNoResponse);
                return;
            }
            MeshPacket r;
            r.from = to;
            r.to = kMe;
            r.has_decoded = true;
            r.decoded.portnum = kPortPosition;
            r.decoded.payload = encode(position_of(*n));
            r.decoded.request_id = id;
            deliver(r, n);
        });
        break;
    case kPortTelemetry: {
        if (!p.decoded.want_response) break;
        Telemetry ask;
        decode(p.decoded.payload, ask);
        bool env = ask.which == Telemetry::Environment;
        at(there, [this, to, id, env] {
            const SimNode* n = find(to);
            if (!n) return;
            if ((env && !n->has_env) || (!env && n->battery < 0)) {
                routing(to, id, kErrNoResponse);
                return;
            }
            Telemetry t;
            t.time = uint32_t(unix_now());
            if (env) {
                t.which = Telemetry::Environment;
                t.environment.has_temperature = t.environment.has_humidity = t.environment.has_pressure = true;
                t.environment.temperature = n->temp;
                t.environment.relative_humidity = n->humidity;
                t.environment.barometric_pressure = n->pressure;
            } else {
                t.which = Telemetry::Device;
                t.device = info_of(*n).metrics;
            }
            MeshPacket r;
            r.from = to;
            r.to = kMe;
            r.has_decoded = true;
            r.decoded.portnum = kPortTelemetry;
            r.decoded.payload = encode(t);
            r.decoded.request_id = id;
            deliver(r, n);
        });
        break;
    }
    case kPortNodeInfo:
        if (!p.decoded.want_response) break;
        at(there, [this, to, id] {
            const SimNode* n = find(to);
            if (!n) return;
            MeshPacket r;
            r.from = to;
            r.to = kMe;
            r.has_decoded = true;
            r.decoded.portnum = kPortNodeInfo;
            r.decoded.payload = encode(info_of(*n).user);
            r.decoded.request_id = id;
            deliver(r, n);
        });
        break;
    default: break;
    }
}

void FakeRadio::schedule_restart() {
    if (in_edit_) {
        restart_wanted_ = true;
        return;
    }
    restart_wanted_ = false;
    at(600, [this] {
        ++restarts_;
        restarted_ = true;
        down_ = true;      // until the client connects again
        out_.clear();
        events_.clear();
        next_chatter_ms_ = now_ms_ + 8000;
    });
}

void FakeRadio::on_admin(const MeshPacket& p) {
    AdminMessage a;
    if (!decode(p.decoded.payload, a)) return;
    uint32_t id = p.id;
    auto reply = [this, id](const AdminMessage& r) {
        MeshPacket m;
        m.from = kMe;
        m.to = kMe;
        m.id = new_id();
        m.has_decoded = true;
        m.decoded.portnum = kPortAdmin;
        m.decoded.payload = encode(r);
        m.decoded.request_id = id;
        FromRadio f;
        f.which = FromRadio::Packet;
        f.packet = m;
        emit(f);
    };
    switch (a.which) {
    case AdminMessage::BeginEdit: in_edit_ = true; break;
    case AdminMessage::CommitEdit:
        in_edit_ = false;
        if (restart_wanted_) schedule_restart();
        break;
    case AdminMessage::SetOwner:
        // As the firmware: an empty name leaves the old one.
        if (!a.user.long_name.empty()) long_name_ = a.user.long_name;
        if (!a.user.short_name.empty()) short_name_ = a.user.short_name;
        break;
    case AdminMessage::SetConfig:
        if (a.config.which == Config::LoRa) {
            LoRaConfig before = lora(), now;
            decode(a.config.raw, now);
            // Choosing a region turns transmit on, as the firmware does.
            if (before.region == 0 && now.region != 0) now.tx_enabled = true;
            lora_raw_ = encode(now);
            schedule_restart();
        } else if (a.config.which == Config::Device) {
            device_raw_ = a.config.raw;
            schedule_restart();
        } else if (a.config.which == Config::PositionCfg) {
            position_raw_ = a.config.raw;
        }
        break;
    case AdminMessage::SetChannel:
        if (a.channel.index >= 0 && a.channel.index < 8) {
            channels_[size_t(a.channel.index)] = a.channel;
            if (a.channel.role == Channel::Disabled) channels_[size_t(a.channel.index)].settings = ChannelSettings{};
        }
        break;
    case AdminMessage::SetFavorite:
    case AdminMessage::RemoveFavorite:
        if (SimNode* n = find(uint32_t(a.value))) n->favorite = a.which == AdminMessage::SetFavorite;
        break;
    case AdminMessage::SetIgnored:
    case AdminMessage::RemoveIgnored:
        if (SimNode* n = find(uint32_t(a.value))) n->ignored = a.which == AdminMessage::SetIgnored;
        break;
    case AdminMessage::RemoveByNodenum:
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&a](const SimNode& n) { return n.num == uint32_t(a.value); }),
                     nodes_.end());
        break;
    case AdminMessage::NodedbReset:
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [](const SimNode& n) { return !n.favorite; }), nodes_.end());
        break;
    case AdminMessage::SetFixedPosition:
        own_pos_ = a.position.has_latlon;
        own_lat_ = a.position.lat();
        own_lon_ = a.position.lon();
        own_alt_ = a.position.alt;
        {
            PositionConfig pc;
            decode(position_raw_, pc);
            pc.fixed_position = true;
            position_raw_ = encode(pc);
        }
        break;
    case AdminMessage::RemoveFixedPosition: {
        own_pos_ = false;
        PositionConfig pc;
        decode(position_raw_, pc);
        pc.fixed_position = false;
        position_raw_ = encode(pc);
        break;
    }
    case AdminMessage::RebootSeconds: schedule_restart(); break;
    case AdminMessage::GetOwnerRequest: {
        AdminMessage r;
        r.which = AdminMessage::GetOwnerResponse;
        r.user = own_info().user;
        reply(r);
        break;
    }
    case AdminMessage::GetChannelRequest: {
        AdminMessage r;
        r.which = AdminMessage::GetChannelResponse;
        if (a.value >= 1 && a.value <= 8) r.channel = channels_[size_t(a.value - 1)];   // asked for by index + 1
        reply(r);
        break;
    }
    case AdminMessage::GetConfigRequest: {
        AdminMessage r;
        r.which = AdminMessage::GetConfigResponse;
        r.config.which = uint32_t(a.value) + 1;
        r.config.raw = a.value == 5 ? lora_raw_ : a.value == 0 ? device_raw_ : a.value == 1 ? position_raw_ : "";
        reply(r);
        break;
    }
    default: break;
    }
}

// The made-up nodes talk among themselves, so there's something to see.
void FakeRadio::chatter() {
    if (nodes_.empty()) return;
    std::vector<SimNode*> who;
    for (SimNode& n : nodes_)
        if (n.reachable && n.named) who.push_back(&n);
    if (who.empty()) return;
    SimNode* n = who[rnd() % who.size()];
    n->heard_ago = 0;
    MeshPacket p;
    p.from = n->num;
    p.to = kBroadcast;
    p.has_decoded = true;
    uint32_t kind = rnd() % 4;
    if (kind == 0 && n->role != 2 && n->role != 6 && n->role != 11 && !n->mqtt) {
        p.decoded.portnum = kPortText;
        p.channel = rnd() % 3 == 0 && channels_[1].role != Channel::Disabled ? 1 : 0;
        p.decoded.payload = kChatter[size_t(chat_turn_++) % (sizeof kChatter / sizeof *kChatter)];
    } else if (kind == 1 && n->has_pos && n->shares_position) {
        n->north_m += double(int(rnd() % 61) - 30);   // it moves a little
        n->east_m += double(int(rnd() % 61) - 30);
        p.decoded.portnum = kPortPosition;
        p.decoded.payload = encode(position_of(*n));
    } else if (n->battery >= 0) {
        Telemetry t;
        t.time = uint32_t(unix_now());
        t.which = Telemetry::Device;
        t.device = info_of(*n).metrics;
        p.decoded.portnum = kPortTelemetry;
        p.decoded.payload = encode(t);
    } else {
        p.decoded.portnum = kPortNodeInfo;
        p.decoded.payload = encode(info_of(*n).user);
    }
    deliver(p, n);
}

}  // namespace zs::mesh
