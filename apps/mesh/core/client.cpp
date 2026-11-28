#include "apps/mesh/core/client.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "apps/mesh/core/pb.h"
#include "apps/mesh/core/recorder.h"
#include "apps/mesh/core/text.h"

namespace zs::mesh {

namespace {
constexpr size_t kMaxMessagesPerChat = 2000;
constexpr int kTextGap = 3;         // seconds between texts: the radio refuses a second one within 2
constexpr int kTraceGap = 30;       // ... and a second traceroute within 30

const char* port_name(uint32_t port) {
    switch (port) {
    case kPortText: return "Text";
    case kPortPosition: return "Position";
    case kPortNodeInfo: return "Node info";
    case kPortRouting: return "Routing";
    case kPortAdmin: return "Admin";
    case kPortWaypoint: return "Waypoint";
    case kPortAlert: return "Alert";
    case kPortRangeTest: return "Range test";
    case kPortTelemetry: return "Telemetry";
    case kPortTraceroute: return "Traceroute";
    case kPortNeighborInfo: return "Neighbours";
    case 10: return "Detection";
    case 65: return "Store & forward";
    case 73: return "Map report";
    case 0: return "Encrypted";
    default: return "Other";
    }
}
}  // namespace

std::string Node::name() const {
    if (!short_name.empty()) return short_name;
    if (!long_name.empty()) return long_name;
    return node_id(num);
}
std::string Node::full_name() const { return long_name.empty() ? (short_name.empty() ? node_id(num) : short_name) : long_name; }

const char* delivery_text(const Message& m, bool dm) {
    switch (m.state) {
    case Delivery::Queued:
    case Delivery::Sent: return "Sending...";
    case Delivery::Relayed: return dm ? "Passed on, not confirmed" : "Heard by the mesh";
    case Delivery::Delivered: return "Delivered";
    case Delivery::Failed: return delivery_error_text(m.error);
    default: return "";
    }
}

Client::Client() {
    std::random_device rd;
    packet_id_ = (uint32_t(rd()) << 8) ^ uint32_t(rd());
    channels_.resize(8);
    for (int i = 0; i < 8; ++i) channels_[size_t(i)].index = i;
}

// ---- The link

uint32_t Client::next_id() {
    if (++packet_id_ == 0) ++packet_id_;
    return packet_id_;
}

void Client::want_config() {
    std::random_device rd;
    nonce_ = uint32_t(rd()) | 1u;
    if (nonce_ == 69420 || nonce_ == 69421) nonce_ += 2;   // those two mean "only part of it"
    ToRadio t;
    t.which = ToRadio::WantConfig;
    t.want_config_id = nonce_;
    out_ += frame(encode(t));
    config_asked_ = now_;
    fresh_.clear();
}

void Client::link_up(long now) {
    now_ = now;
    linked_ = true;
    ready_ = false;
    out_.clear();
    held_.clear();
    pending_.clear();
    last_tx_ = last_rx_ = now;
    want_config();
    ++config_rev_;
}

void Client::link_down() {
    if (!linked_) return;
    linked_ = false;
    ready_ = false;
    out_.clear();
    held_.clear();
    for (const Pending& p : pending_) {
        if (p.kind == Pending::Trace) {
            for (TraceResult& t : traces_)
                if (t.dest == p.node && !t.answered && !t.failed) t.failed = true, t.error = kErrLocalQueue;
        }
    }
    pending_.clear();
    ++config_rev_;
}

std::string Client::take_out() {
    std::string o;
    o.swap(out_);
    if (!o.empty()) last_tx_ = now_;
    return o;
}

void Client::tick(long now) {
    now_ = now;
    if (!linked_) return;
    if (!ready_ && now - config_asked_ > 30) want_config();   // the answer never finished: ask again
    if (ready_ && now - last_tx_ >= 60 && out_.empty()) {
        ToRadio t;
        t.which = ToRadio::Heartbeat;
        out_ += frame(encode(t));
    }
    if (!held_.empty() && now - last_text_at_ >= kTextGap) {
        out_ += held_.front();
        held_.pop_front();
        last_text_at_ = now;
    }
    for (size_t i = 0; i < pending_.size();) {
        Pending& p = pending_[i];
        long limit = p.kind == Pending::Trace ? 120 : 60;
        if (now - p.at <= limit) {
            ++i;
            continue;
        }
        if (p.kind == Pending::Trace) {
            for (TraceResult& t : traces_)
                if (t.dest == p.node && t.asked == p.at && !t.answered) t.failed = true, t.error = kErrLocalNoAnswer;
            notice("No route found to " + node_name(p.node));
            ++nodes_rev_;
        } else if (p.kind != Pending::Admin) {
            notice("No answer from " + node_name(p.node));
        }
        pending_.erase(pending_.begin() + long(i));
    }
}

void Client::on_from_radio(const std::string& payload, long now) {
    now_ = now;
    last_rx_ = now;
    FromRadio f;
    if (!decode(payload, f)) return;
    switch (f.which) {
    case FromRadio::MyInfo:
        if (me_ != f.my_info.my_node_num) {
            // Another radio, or this one got a new number (it does when its
            // region is first set): what we knew belongs to the old one.
            if (me_) nodes_.clear();
            me_ = f.my_info.my_node_num;
        }
        ++nodes_rev_;
        break;
    case FromRadio::NodeInfoMsg: on_node_info(f.node_info); break;
    case FromRadio::ConfigMsg: on_config(f.config); break;
    case FromRadio::ChannelMsg:
        if (f.channel.index >= 0 && f.channel.index < 8) {
            channels_[size_t(f.channel.index)] = f.channel;
            ++config_rev_;
        }
        break;
    case FromRadio::Metadata:
        metadata_ = f.metadata;
        ++config_rev_;
        break;
    case FromRadio::ConfigComplete:
        if (f.config_complete_id != nonce_) break;
        // Nodes the radio no longer lists are gone from its memory.
        if (!fresh_.empty()) {
            for (auto it = nodes_.begin(); it != nodes_.end();) {
                if (!fresh_.count(it->first)) it = nodes_.erase(it);
                else ++it;
            }
        }
        fresh_.clear();
        ready_ = ever_ready_ = true;
        ensure_channel_chats();
        ++config_rev_;
        ++nodes_rev_;
        ++chats_rev_;
        break;
    case FromRadio::Packet: on_packet(f.packet, now); break;
    case FromRadio::QueueStatusMsg:
        queue_ = f.queue_status;
        break;
    case FromRadio::Rebooted:
        // It restarted under us: everything may have changed.
        ready_ = false;
        want_config();
        ++config_rev_;
        break;
    case FromRadio::Notification:
        if (!f.notification.message.empty() && f.notification.level >= 30) notice(f.notification.message);
        break;
    default: break;
    }
}

// ---- What arrives

Node& Client::touch(uint32_t num) {
    Node& n = nodes_[num];
    n.num = num;
    return n;
}

void Client::on_node_info(const NodeInfo& i) {
    if (!i.num) return;
    Node& n = touch(i.num);
    fresh_[i.num] = true;
    if (i.has_user) {
        n.has_user = true;
        n.long_name = i.user.long_name;
        n.short_name = i.user.short_name;
        n.hw_model = i.user.hw_model;
        n.role = i.user.role;
        n.has_key = !i.user.public_key.empty();
        n.licensed = i.user.is_licensed;
        n.has_unmessagable = i.user.has_unmessagable;
        n.unmessagable = i.user.is_unmessagable;
    }
    if (i.has_position && i.position.has_latlon) {
        n.has_pos = true;
        n.lat = i.position.lat();
        n.lon = i.position.lon();
        n.has_alt = i.position.has_alt;
        n.alt = i.position.alt;
        n.pos_time = i.position.time;
        n.precision_bits = i.position.precision_bits;
    }
    if (i.has_metrics) {
        n.has_metrics = true;
        n.metrics = i.metrics;
    }
    n.has_snr = i.snr != 0;
    n.snr = i.snr;
    n.last_heard = i.last_heard;
    n.hops = i.has_hops ? int(i.hops_away) : -1;
    n.channel = i.channel;
    n.via_mqtt = i.via_mqtt;
    n.favorite = i.is_favorite;
    n.ignored = i.is_ignored;
    ++nodes_rev_;
}

void Client::on_config(const Config& c) {
    switch (c.which) {
    case Config::LoRa:
        decode(c.raw, lora_);
        has_lora_ = true;
        break;
    case Config::Device: decode(c.raw, device_); break;
    case Config::PositionCfg: decode(c.raw, position_); break;
    default: return;
    }
    ++config_rev_;
}

void Client::heard(Node& n, const MeshPacket& p, long now) {
    n.last_heard = now;
    n.via_mqtt = p.via_mqtt;
    int hops = p.hops();
    if (hops >= 0) n.hops = hops;
    // The signal figures are for the last hop: they are this node's only
    // when it was heard directly.
    if (hops == 0 && !p.via_mqtt && (p.rx_snr != 0 || p.rx_rssi != 0)) {
        n.has_snr = true;
        n.snr = p.rx_snr;
        n.rssi = p.rx_rssi;
    }
}

void Client::log_packet(const MeshPacket& p, bool out, long now) {
    LogLine l;
    l.time = now;
    l.out = out;
    l.from = out ? me_ : p.from;
    l.to = p.to;
    l.port = p.has_decoded ? p.decoded.portnum : 0;
    l.snr = p.rx_snr;
    l.rssi = p.rx_rssi;
    l.hops = out ? -1 : p.hops();
    l.what = port_name(l.port);
    log_.push_back(std::move(l));
    if (log_.size() > 200) log_.pop_front();
    ++log_rev_;
}

void Client::on_packet(const MeshPacket& p, long now) {
    log_packet(p, false, now);
    bool from_other = p.from != 0 && p.from != me_;
    if (from_other) {
        heard(touch(p.from), p, now);
        ++nodes_rev_;
    }
    if (!p.has_decoded) return;
    const Data& d = p.decoded;
    uint32_t src = p.from ? p.from : me_;
    Pending* req = d.request_id ? pending(d.request_id) : nullptr;
    switch (d.portnum) {
    case kPortPosition: {
        Position pos;
        if (!decode(d.payload, pos)) break;
        if (pos.has_latlon) {
            Node& n = touch(src);
            n.has_pos = true;
            n.lat = pos.lat();
            n.lon = pos.lon();
            n.has_alt = pos.has_alt;
            n.alt = pos.alt;
            n.pos_time = pos.time ? long(pos.time) : now;
            n.precision_bits = pos.precision_bits;
            ++nodes_rev_;
        }
        if (req && req->kind == Pending::PositionReq) {
            notice(pos.has_latlon ? "Position from " + node_name(src) : node_name(src) + " has no position to share");
            drop_pending(d.request_id);
        }
        break;
    }
    case kPortNodeInfo: {
        User u;
        if (!decode(d.payload, u)) break;
        Node& n = touch(src);
        n.has_user = true;
        n.long_name = u.long_name;
        n.short_name = u.short_name;
        n.hw_model = u.hw_model;
        n.role = u.role;
        n.has_key = !u.public_key.empty();
        n.licensed = u.is_licensed;
        n.has_unmessagable = u.has_unmessagable;
        n.unmessagable = u.is_unmessagable;
        ++nodes_rev_;
        ++chats_rev_;   // a chat's title may have been a number until now
        if (req && req->kind == Pending::UserReq) {
            notice("Details from " + node_name(src));
            drop_pending(d.request_id);
        }
        break;
    }
    case kPortTelemetry: {
        Telemetry t;
        if (!decode(d.payload, t)) break;
        Node& n = touch(src);
        if (t.which == Telemetry::Device) {
            n.has_metrics = true;
            n.metrics = t.device;
            n.metrics_time = now;
        } else if (t.which == Telemetry::Environment) {
            n.has_env = true;
            n.env = t.environment;
            n.env_time = now;
        } else if (t.which == Telemetry::Local && src == me_) {
            stats_ = t.local;
            has_stats_ = true;
            ++config_rev_;
        }
        ++nodes_rev_;
        if (req && req->kind == Pending::TelemetryReq) {
            notice("Telemetry from " + node_name(src));
            drop_pending(d.request_id);
        }
        break;
    }
    case kPortRouting: on_routing(p, now); break;
    case kPortTraceroute: {
        if (!req || req->kind != Pending::Trace) break;
        RouteDiscovery r;
        if (!decode(d.payload, r)) break;
        for (TraceResult& t : traces_) {
            if (t.dest != req->node || t.asked != req->at) continue;
            t.answered = now;
            t.failed = false;
            t.route = r;
            t.has_back = p.hop_start != 0 && r.snr_back.size() == r.route_back.size() + 1;
            size_t n = r.route.size();
            notice("Route to " + node_name(t.dest) + ": " + (n == 0 ? "direct" : std::to_string(n) + (n == 1 ? " hop between" : " hops between")));
        }
        drop_pending(d.request_id);
        ++nodes_rev_;
        break;
    }
    case kPortAdmin: on_admin(p); break;
    default: break;
    }
}

void Client::on_routing(const MeshPacket& p, long) {
    Pending* req = pending(p.decoded.request_id);
    if (!req) return;
    Routing r;
    if (!decode(p.decoded.payload, r) || r.which != Routing::ErrorReason || r.error_reason == kErrNone) return;
    // An ack only says the request arrived; the answer is its own packet.
    std::string who = node_name(req->node);
    switch (req->kind) {
    case Pending::Trace:
        for (TraceResult& t : traces_)
            if (t.dest == req->node && t.asked == req->at && !t.answered) t.failed = true, t.error = r.error_reason;
        notice("Traceroute to " + who + ": " + routing_error_text(r.error_reason));
        ++nodes_rev_;
        break;
    case Pending::PositionReq:
        notice(r.error_reason == kErrNoResponse ? who + " isn't sharing its position"
                                                : "Position from " + who + ": " + routing_error_text(r.error_reason));
        break;
    case Pending::TelemetryReq:
        notice(r.error_reason == kErrNoResponse ? who + " has no telemetry to share"
                                                : "Telemetry from " + who + ": " + routing_error_text(r.error_reason));
        break;
    case Pending::UserReq: notice("Details from " + who + ": " + routing_error_text(r.error_reason)); break;
    case Pending::Admin: notice(std::string("The radio refused the change: ") + routing_error_text(r.error_reason)); break;
    }
    drop_pending(p.decoded.request_id);
}

void Client::on_admin(const MeshPacket& p) {
    AdminMessage a;
    if (!decode(p.decoded.payload, a)) return;
    // Answers to things other clients asked can arrive here too; take what's useful.
    if (a.which == AdminMessage::GetConfigResponse) on_config(a.config);
    if (a.which == AdminMessage::GetChannelResponse && a.channel.index >= 0 && a.channel.index < 8) {
        channels_[size_t(a.channel.index)] = a.channel;
        ++config_rev_;
    }
}

// ---- Nodes

const Node* Client::node(uint32_t num) const {
    auto it = nodes_.find(num);
    return it == nodes_.end() ? nullptr : &it->second;
}
std::string Client::node_name(uint32_t num) const {
    if (num == kBroadcast) return "everyone";
    const Node* n = node(num);
    return n ? n->name() : node_id(num);
}
std::string Client::node_full_name(uint32_t num) const {
    const Node* n = node(num);
    return n ? n->full_name() : node_id(num);
}
int Client::online_count(long now) const {
    int c = 0;
    for (const auto& kv : nodes_)
        if (kv.first != me_ && online(kv.second, now)) ++c;
    return c;
}
void Client::set_here(bool valid, double lat, double lon) {
    here_valid_ = valid;
    here_lat_ = lat;
    here_lon_ = lon;
}
bool Client::here(double& lat, double& lon) const {
    if (here_valid_) {
        lat = here_lat_;
        lon = here_lon_;
        return true;
    }
    const Node* n = me();
    if (n && n->has_pos) {
        lat = n->lat;
        lon = n->lon;
        return true;
    }
    return false;
}

// ---- Channels

const Channel* Client::channel(uint32_t index) const {
    if (index >= channels_.size() || channels_[index].role == Channel::Disabled) return nullptr;
    return &channels_[index];
}
std::string Client::channel_name(uint32_t index) const {
    const Channel* c = channel(index);
    if (!c) return "Channel " + std::to_string(index);
    return channel_display_name(c->settings, lora_);
}
int Client::free_channel_slot() const {
    for (size_t i = 1; i < channels_.size(); ++i)
        if (channels_[i].role == Channel::Disabled) return int(i);
    return -1;
}
std::string Client::share_url(int index) const {
    ChannelSet set;
    if (index >= 0) {
        const Channel* c = channel(uint32_t(index));
        if (!c) return "";
        set.settings.push_back(c->settings);
        // A channel with no name is "the preset's": say which, so it lands
        // the same on a radio set to another preset.
        if (set.settings[0].name.empty()) set.settings[0].name = channel_display_name(c->settings, lora_);
        set.add_only = true;
    } else {
        for (const Channel& c : channels_)
            if (c.role == Channel::Primary) set.settings.push_back(c.settings);
        for (const Channel& c : channels_)
            if (c.role == Channel::Secondary) set.settings.push_back(c.settings);
        if (set.settings.empty()) return "";
        set.has_lora = has_lora_;
        set.lora = lora_;
    }
    return channel_url(set);
}

// ---- Chats

Chat& Client::chat_for(const std::string& key) {
    for (Chat& c : chats_)
        if (c.key == key) return c;
    Chat c;
    c.key = key;
    c.dm = !key.empty() && key[0] == 'd';
    if (c.dm) c.peer = parse_node_id(std::string_view(key).substr(1));
    else c.channel = uint32_t(std::atoi(key.c_str() + 1));
    auto it = read_marks_.find(key);
    if (it != read_marks_.end()) c.read_upto = it->second;
    chats_.push_back(std::move(c));
    return chats_.back();
}
const Chat* Client::chat(const std::string& key) const {
    for (const Chat& c : chats_)
        if (c.key == key) return &c;
    return nullptr;
}
std::string Client::chat_title(const Chat& c) const {
    if (c.dm) return node_full_name(c.peer);
    return channel_name(c.channel);
}
int Client::total_unread() const {
    int n = 0;
    for (const Chat& c : chats_) n += c.unread;
    return n;
}
void Client::recount(Chat& c) {
    c.unread = 0;
    for (const Message& m : c.msgs)
        if (!m.mine && m.time > c.read_upto) ++c.unread;
}
void Client::sort_chats() {
    std::stable_sort(chats_.begin(), chats_.end(), [](const Chat& a, const Chat& b) {
        if (a.dm != b.dm) return !a.dm;
        if (!a.dm) return a.channel < b.channel;
        return a.last_time() > b.last_time();
    });
}
void Client::ensure_channel_chats() {
    for (const Channel& c : channels_)
        if (c.role != Channel::Disabled) chat_for(chat_key_channel(uint32_t(c.index)));
    // A channel that's gone and never said anything needs no row.
    chats_.erase(std::remove_if(chats_.begin(), chats_.end(),
                                [this](const Chat& c) { return !c.dm && c.msgs.empty() && !channel(c.channel); }),
                 chats_.end());
    sort_chats();
}
const Chat& Client::open_dm(uint32_t n) {
    std::string key = chat_key_dm(n);
    chat_for(key);
    sort_chats();
    ++chats_rev_;
    return *chat(key);
}

void Client::apply(const Record& r) {
    if (r.kind == Record::Deleted) {
        for (Chat& c : chats_) {
            if (r.key != "*" && c.key != r.key) continue;
            c.msgs.clear();
            c.unread = 0;
        }
        chats_.erase(std::remove_if(chats_.begin(), chats_.end(),
                                    [this](const Chat& c) { return c.msgs.empty() && (c.dm || !channel(c.channel)); }),
                     chats_.end());
        ++chats_rev_;
        return;
    }
    if (r.kind == Record::Ack) {
        for (Chat& c : chats_) {
            for (size_t i = c.msgs.size(); i-- > 0;) {
                Message& m = c.msgs[i];
                if (!m.mine || m.id != r.id) continue;
                if (advance_delivery(m.state, m.error, r.state, r.error)) ++chats_rev_;
                return;
            }
        }
        return;
    }
    if (r.kind != Record::Msg) return;
    Chat& c = chat_for(r.chat_key());
    if (r.reaction) {
        for (size_t i = c.msgs.size(); i-- > 0;) {
            Message& m = c.msgs[i];
            if (m.id != r.reply_id) continue;
            for (const Reaction& x : m.reactions)
                if (x.from == r.from && x.text == r.text) return;
            m.reactions.push_back({r.from, r.text});
            ++chats_rev_;
            return;
        }
        // The message it answers isn't here (too old): shown as a message.
    }
    if (r.id) {
        size_t look = std::min<size_t>(c.msgs.size(), 64);
        for (size_t i = c.msgs.size(); i-- > c.msgs.size() - look;)
            if (c.msgs[i].id == r.id && c.msgs[i].from == r.from) return;   // read twice
    }
    Message m;
    m.id = r.id;
    m.from = r.from;
    m.to = r.to;
    m.time = r.time;
    m.text = r.text;
    m.mine = r.mine;
    m.pki = r.pki;
    m.via_mqtt = r.via_mqtt;
    m.bell = r.bell;
    m.waypoint = r.waypoint;
    m.state = r.state;
    m.error = r.error;
    m.snr = r.snr_q / 4.0f;
    m.rssi = r.rssi;
    m.hops = r.hops;
    m.reply_id = r.reply_id;
    if (!m.mine && m.time > c.read_upto) ++c.unread;
    c.msgs.push_back(std::move(m));
    if (c.msgs.size() > kMaxMessagesPerChat) c.msgs.erase(c.msgs.begin(), c.msgs.begin() + 200);
    if (c.dm) sort_chats();
    ++chats_rev_;
}

void Client::clear_chats() {
    chats_.clear();
    if (ever_ready_) ensure_channel_chats();
    ++chats_rev_;
}

void Client::mark_read(const std::string& key, long now) {
    for (Chat& c : chats_) {
        if (c.key != key) continue;
        long upto = std::max(c.last_time(), c.read_upto);
        (void)now;
        if (upto == c.read_upto && c.unread == 0) return;
        c.read_upto = upto;
        read_marks_[key] = upto;
        c.unread = 0;
        ++chats_rev_;
        return;
    }
}
std::string Client::read_marks() const {
    std::string s;
    for (const auto& kv : read_marks_) s += kv.first + "\t" + std::to_string(kv.second) + "\n";
    return s;
}
void Client::set_read_marks(const std::string& text) {
    read_marks_.clear();
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(at, nl - at);
        at = nl + 1;
        size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        read_marks_[line.substr(0, tab)] = std::atol(line.c_str() + tab + 1);
    }
    for (Chat& c : chats_) {
        auto it = read_marks_.find(c.key);
        c.read_upto = it == read_marks_.end() ? 0 : it->second;
        recount(c);
    }
    ++chats_rev_;
}

// ---- Sending

uint32_t Client::hop_limit() const {
    uint32_t h = lora_.hop_limit;
    return h >= 1 && h <= 7 ? h : 3;
}

MeshPacket Client::make_packet(uint32_t to, uint32_t channel, uint32_t port, std::string payload, bool want_response) {
    MeshPacket p;
    p.to = to;
    p.channel = channel;
    p.id = next_id();
    p.hop_limit = hop_limit();
    p.has_decoded = true;
    p.decoded.portnum = port;
    p.decoded.payload = std::move(payload);
    p.decoded.want_response = want_response;
    return p;
}

uint32_t Client::queue_packet(MeshPacket p) {
    ToRadio t;
    t.which = ToRadio::Packet;
    t.packet = p;
    std::string f = frame(encode(t));
    log_packet(p, true, now_);
    if (p.decoded.portnum == kPortText || p.decoded.portnum == kPortAlert) {
        if (now_ - last_text_at_ < kTextGap || !held_.empty()) {
            held_.push_back(std::move(f));
            return p.id;
        }
        last_text_at_ = now_;
    }
    out_ += f;
    return p.id;
}

bool Client::chat_target(const std::string& key, uint32_t& to, uint32_t& channel) {
    last_error_.clear();
    if (!linked_ || !ready_) {
        last_error_ = "Not sent: the radio isn't connected yet";
        return false;
    }
    if (needs_region()) {
        last_error_ = "Not sent: choose your region first (Radio)";
        return false;
    }
    if (has_lora_ && !lora_.tx_enabled) {
        last_error_ = "Not sent: transmit is turned off (Radio)";
        return false;
    }
    if (key.size() < 2) {
        last_error_ = "Not sent";
        return false;
    }
    if (key[0] == 'd') {
        to = parse_node_id(std::string_view(key).substr(1));
        if (!to) {
            last_error_ = "Not sent";
            return false;
        }
        const Node* n = node(to);
        channel = n && this->channel(n->channel) ? n->channel : 0;
        return true;
    }
    to = kBroadcast;
    channel = uint32_t(std::atoi(key.c_str() + 1));
    if (!this->channel(channel)) {
        last_error_ = "Not sent: that channel isn't on this radio any more";
        return false;
    }
    return true;
}

uint32_t Client::send_text(const std::string& chat_key, const std::string& text, uint32_t reply_id, bool bell) {
    uint32_t to, ch;
    if (!chat_target(chat_key, to, ch)) return 0;
    std::string body = utf8_cut(text, kMaxText - (bell ? 1 : 0));
    if (body.empty()) {
        last_error_ = "Nothing to send";
        return 0;
    }
    if (bell) body.push_back('\a');   // rings the other radios' bells
    MeshPacket p = make_packet(to, ch, kPortText, body, false);
    p.want_ack = true;
    p.decoded.reply_id = reply_id;
    return queue_packet(p);
}

uint32_t Client::send_reaction(const std::string& chat_key, uint32_t reply_id, const std::string& emoji) {
    uint32_t to, ch;
    if (!reply_id || emoji.empty() || !chat_target(chat_key, to, ch)) return 0;
    MeshPacket p = make_packet(to, ch, kPortText, emoji, false);
    p.want_ack = true;
    p.decoded.reply_id = reply_id;
    p.decoded.emoji = 1;
    return queue_packet(p);
}

uint32_t Client::send_waypoint(const std::string& chat_key, const std::string& name, double lat, double lon, long now) {
    uint32_t to, ch;
    if (!chat_target(chat_key, to, ch)) return 0;
    Waypoint w;
    w.id = next_id();
    w.has_latlon = true;
    w.lat_i = int32_t(std::lround(lat * 1e7));
    w.lon_i = int32_t(std::lround(lon * 1e7));
    w.expire = uint32_t(now + 8 * 3600);   // as the phone apps do
    w.name = utf8_cut(name.empty() ? "Here" : name, 29);
    MeshPacket p = make_packet(to, ch, kPortWaypoint, encode(w), false);
    p.want_ack = true;
    return queue_packet(p);
}

// ---- Asking other nodes

Client::Pending* Client::pending(uint32_t id) {
    for (Pending& p : pending_)
        if (p.id == id) return &p;
    return nullptr;
}
void Client::drop_pending(uint32_t id) {
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [id](const Pending& p) { return p.id == id; }),
                   pending_.end());
}

int Client::traceroute_wait(long now) const {
    long left = kTraceGap - (now - last_trace_at_);
    return last_trace_at_ && left > 0 ? int(left) : 0;
}

uint32_t Client::traceroute(uint32_t n, long now) {
    now_ = now;
    last_error_.clear();
    if (!ready_ || !n || n == me_) {
        last_error_ = "The radio isn't connected yet";
        return 0;
    }
    if (int w = traceroute_wait(now)) {
        last_error_ = "The radio allows one traceroute in 30 s: wait " + std::to_string(w) + " s";
        return 0;
    }
    const Node* node = this->node(n);
    MeshPacket p = make_packet(n, node && channel(node->channel) ? node->channel : 0, kPortTraceroute,
                               encode(RouteDiscovery{}), true);
    p.want_ack = true;
    uint32_t id = queue_packet(p);
    pending_.push_back({Pending::Trace, id, n, now});
    TraceResult t;
    t.dest = n;
    t.asked = now;
    traces_.push_back(std::move(t));
    if (traces_.size() > 20) traces_.erase(traces_.begin());
    last_trace_at_ = now;
    ++nodes_rev_;
    return id;
}

const TraceResult* Client::last_trace(uint32_t n) const {
    for (size_t i = traces_.size(); i-- > 0;)
        if (traces_[i].dest == n) return &traces_[i];
    return nullptr;
}

uint32_t Client::request_position(uint32_t n, long now) {
    now_ = now;
    if (!ready_ || !n || n == me_) return 0;
    // Ours goes with the question (empty when we have none): it's an exchange.
    Position mine;
    if (const Node* self = me(); self && self->has_pos) {
        mine.has_latlon = true;
        mine.lat_i = int32_t(std::lround(self->lat * 1e7));
        mine.lon_i = int32_t(std::lround(self->lon * 1e7));
        mine.time = uint32_t(now);
    }
    const Node* node = this->node(n);
    uint32_t id = queue_packet(make_packet(n, node && channel(node->channel) ? node->channel : 0, kPortPosition,
                                           encode(mine), true));
    pending_.push_back({Pending::PositionReq, id, n, now});
    return id;
}

uint32_t Client::request_telemetry(uint32_t n, long now, bool environment) {
    now_ = now;
    if (!ready_ || !n || n == me_) return 0;
    Telemetry t;
    t.which = environment ? Telemetry::Environment : Telemetry::Device;
    const Node* node = this->node(n);
    uint32_t id = queue_packet(make_packet(n, node && channel(node->channel) ? node->channel : 0, kPortTelemetry,
                                           encode(t), true));
    pending_.push_back({Pending::TelemetryReq, id, n, now});
    return id;
}

uint32_t Client::request_user(uint32_t n, long now) {
    now_ = now;
    if (!ready_ || !n || n == me_) return 0;
    User u;
    if (const Node* self = me()) {
        u.id = node_id(me_);
        u.long_name = self->long_name;
        u.short_name = self->short_name;
        u.hw_model = self->hw_model;
        u.is_licensed = self->licensed;
    }
    const Node* node = this->node(n);
    uint32_t id = queue_packet(make_packet(n, node && channel(node->channel) ? node->channel : 0, kPortNodeInfo,
                                           encode(u), true));
    pending_.push_back({Pending::UserReq, id, n, now});
    return id;
}

// ---- Changing this radio

void Client::send_admin(const AdminMessage& a, bool want_response) {
    if (!linked_ || !me_) return;
    MeshPacket p = make_packet(me_, 0, kPortAdmin, encode(a), want_response);
    p.want_ack = true;
    uint32_t id = queue_packet(p);
    pending_.push_back({Pending::Admin, id, me_, now_});
}

static AdminMessage admin_scalar(uint32_t which, uint64_t value) {
    AdminMessage a;
    a.which = which;
    a.value = value;
    return a;
}

void Client::begin_edit() { send_admin(admin_scalar(AdminMessage::BeginEdit, 1)); }
void Client::commit_edit() { send_admin(admin_scalar(AdminMessage::CommitEdit, 1)); }

void Client::set_owner(const std::string& long_name, const std::string& short_name) {
    AdminMessage a;
    a.which = AdminMessage::SetOwner;
    a.user.long_name = utf8_cut(long_name, 39);
    a.user.short_name = utf8_cut(short_name, 4);
    Node& self = touch(me_);
    // The radio takes these two from what we send, so send them as they are.
    a.user.is_licensed = self.licensed;
    a.user.has_unmessagable = self.has_unmessagable;
    a.user.is_unmessagable = self.unmessagable;
    send_admin(a);
    self.long_name = a.user.long_name;
    self.short_name = a.user.short_name;
    self.has_user = true;
    ++nodes_rev_;
    ++config_rev_;
}

void Client::set_lora(const LoRaConfig& l) {
    AdminMessage a;
    a.which = AdminMessage::SetConfig;
    a.config.which = Config::LoRa;
    a.config.raw = encode(l);
    send_admin(a);
    decode(a.config.raw, lora_);
    has_lora_ = true;
    ++config_rev_;
    ++chats_rev_;   // the default channel is named after the preset
}

void Client::set_device(const DeviceConfig& d) {
    AdminMessage a;
    a.which = AdminMessage::SetConfig;
    a.config.which = Config::Device;
    a.config.raw = encode(d);
    send_admin(a);
    decode(a.config.raw, device_);
    ++config_rev_;
}

void Client::set_position_config(const PositionConfig& p) {
    AdminMessage a;
    a.which = AdminMessage::SetConfig;
    a.config.which = Config::PositionCfg;
    a.config.raw = encode(p);
    send_admin(a);
    decode(a.config.raw, position_);
    ++config_rev_;
}

void Client::set_channel(const Channel& c) {
    if (c.index < 0 || c.index >= 8) return;
    AdminMessage a;
    a.which = AdminMessage::SetChannel;
    a.channel = c;
    send_admin(a);
    channels_[size_t(c.index)] = c;
    if (c.role == Channel::Disabled) channels_[size_t(c.index)].settings = ChannelSettings{};
    ensure_channel_chats();
    ++config_rev_;
    ++chats_rev_;
}

void Client::delete_channel(uint32_t index) {
    if (index == 0 || index >= 8) return;   // the primary channel is changed, never removed
    // The slot is left empty rather than closing the gap: the radio reads
    // each slot on its own, and the chats here are kept by slot number.
    Channel c;
    c.index = int32_t(index);
    c.role = Channel::Disabled;
    set_channel(c);
}

bool Client::add_channels(const ChannelSet& set, std::string* why) {
    std::vector<ChannelSettings> fresh;
    for (const ChannelSettings& s : set.settings) {
        bool have = false;
        std::string name = s.name.empty() ? std::string(preset_name(set.has_lora ? set.lora.modem_preset : lora_.modem_preset)) : s.name;
        for (const Channel& c : channels_) {
            if (c.role == Channel::Disabled) continue;
            if (channel_display_name(c.settings, lora_) == name && c.settings.psk == s.psk) have = true;
        }
        for (const ChannelSettings& f : fresh)
            if (f.name == name && f.psk == s.psk) have = true;
        if (have) continue;
        ChannelSettings n = s;
        n.name = name;
        fresh.push_back(std::move(n));
    }
    if (fresh.empty()) {
        if (why) *why = "You already have " + std::string(set.settings.size() == 1 ? "that channel" : "those channels");
        return false;
    }
    size_t room = 0;
    for (size_t i = 1; i < channels_.size(); ++i)
        if (channels_[i].role == Channel::Disabled) ++room;
    if (room < fresh.size()) {
        if (why) *why = "No room: a radio holds 8 channels. Delete one first";
        return false;
    }
    if (fresh.size() > 1) begin_edit();
    for (const ChannelSettings& s : fresh) {
        Channel c;
        c.index = free_channel_slot();
        c.role = Channel::Secondary;
        c.settings = s;
        set_channel(c);
    }
    if (fresh.size() > 1) commit_edit();
    return true;
}

void Client::replace_channels(const ChannelSet& set) {
    begin_edit();
    for (size_t i = 0; i < 8; ++i) {
        Channel c;
        c.index = int32_t(i);
        if (i < set.settings.size()) {
            c.role = i == 0 ? Channel::Primary : Channel::Secondary;
            c.settings = set.settings[i];
        }
        if (c.role == Channel::Disabled && channels_[i].role == Channel::Disabled) continue;
        set_channel(c);
    }
    if (set.has_lora) {
        LoRaConfig l = lora_;
        l.use_preset = set.lora.use_preset;
        l.modem_preset = set.lora.modem_preset;
        if (set.lora.region) l.region = set.lora.region;   // a link without one keeps ours
        if (set.lora.hop_limit) l.hop_limit = set.lora.hop_limit;
        l.channel_num = set.lora.channel_num;
        l.tx_enabled = true;
        set_lora(l);
    }
    commit_edit();
}

void Client::set_favorite(uint32_t n, bool on) {
    send_admin(admin_scalar(on ? AdminMessage::SetFavorite : AdminMessage::RemoveFavorite, n));
    touch(n).favorite = on;
    ++nodes_rev_;
}
void Client::set_ignored(uint32_t n, bool on) {
    send_admin(admin_scalar(on ? AdminMessage::SetIgnored : AdminMessage::RemoveIgnored, n));
    touch(n).ignored = on;
    ++nodes_rev_;
}
void Client::remove_node(uint32_t n) {
    if (n == me_) return;
    send_admin(admin_scalar(AdminMessage::RemoveByNodenum, n));
    nodes_.erase(n);
    ++nodes_rev_;
}
void Client::reset_nodedb() {
    send_admin(admin_scalar(AdminMessage::NodedbReset, 1));
    for (auto it = nodes_.begin(); it != nodes_.end();) {
        if (it->first != me_ && !it->second.favorite) it = nodes_.erase(it);
        else ++it;
    }
    ++nodes_rev_;
}
void Client::reboot(int seconds) { send_admin(admin_scalar(AdminMessage::RebootSeconds, uint64_t(seconds))); }

void Client::set_fixed_position(double lat, double lon, int alt_m) {
    AdminMessage a;
    a.which = AdminMessage::SetFixedPosition;
    a.position.has_latlon = true;
    a.position.lat_i = int32_t(std::lround(lat * 1e7));
    a.position.lon_i = int32_t(std::lround(lon * 1e7));
    a.position.has_alt = true;
    a.position.alt = alt_m;
    a.position.time = uint32_t(now_);
    send_admin(a);
    Node& self = touch(me_);
    self.has_pos = true;
    self.lat = lat;
    self.lon = lon;
    self.has_alt = true;
    self.alt = alt_m;
    self.pos_time = now_;
    position_.fixed_position = true;
    ++nodes_rev_;
    ++config_rev_;
}
void Client::remove_fixed_position() {
    send_admin(admin_scalar(AdminMessage::RemoveFixedPosition, 1));
    touch(me_).has_pos = false;
    position_.fixed_position = false;
    ++nodes_rev_;
    ++config_rev_;
}

bool Client::send_own_position(double lat, double lon, bool has_alt, int alt_m, long now, int min_gap) {
    now_ = now;
    if (!ready_ || !me_ || (last_pos_sent_ && now - last_pos_sent_ < min_gap)) return false;
    Position p;
    p.has_latlon = true;
    p.lat_i = int32_t(std::lround(lat * 1e7));
    p.lon_i = int32_t(std::lround(lon * 1e7));
    p.has_alt = has_alt;
    p.alt = alt_m;
    p.time = uint32_t(now);
    p.location_source = 3;   // LOC_EXTERNAL: from outside the radio
    MeshPacket m = make_packet(me_, 0, kPortPosition, encode(p), false);
    m.priority = 10;         // BACKGROUND
    queue_packet(m);
    last_pos_sent_ = now;
    Node& self = touch(me_);
    self.has_pos = true;
    self.lat = lat;
    self.lon = lon;
    self.has_alt = has_alt;
    self.alt = alt_m;
    self.pos_time = now;
    ++nodes_rev_;
    return true;
}

// ---- News, the log

void Client::notice(const std::string& s) {
    if (notices_.size() < 8) notices_.push_back(s);
}
std::vector<std::string> Client::take_notices() {
    std::vector<std::string> n;
    n.swap(notices_);
    return n;
}

std::vector<std::string> Client::trace_lines(const TraceResult& t) const {
    std::vector<std::string> lines;
    auto hop = [this](uint32_t num) { return num == kBroadcast ? std::string("?") : node_name(num); };
    auto snr = [](const std::vector<int32_t>& v, size_t i) -> std::string {
        if (i >= v.size() || v[i] == -128) return "";
        char b[16];
        std::snprintf(b, sizeof b, " %+.1f dB", v[i] / 4.0);
        return b;
    };
    if (t.failed) {
        lines.push_back(std::string("Failed: ") + delivery_error_text(t.error));
        return lines;
    }
    if (!t.answered) {
        lines.push_back("Waiting for an answer...");
        return lines;
    }
    // Out: us, each node between, them; the figure is how well each heard the one before.
    std::string out = "Out: You";
    for (size_t i = 0; i < t.route.route.size(); ++i) out += " > " + hop(t.route.route[i]) + snr(t.route.snr_towards, i);
    out += " > " + node_name(t.dest) + snr(t.route.snr_towards, t.route.route.size());
    lines.push_back(out);
    if (t.has_back) {
        std::string back = "Back: " + node_name(t.dest);
        for (size_t i = 0; i < t.route.route_back.size(); ++i)
            back += " > " + hop(t.route.route_back[i]) + snr(t.route.snr_back, i);
        back += " > You" + snr(t.route.snr_back, t.route.route_back.size());
        lines.push_back(back);
    }
    return lines;
}

}  // namespace zs::mesh
