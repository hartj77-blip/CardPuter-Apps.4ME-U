#include "apps/mesh/core/recorder.h"

#include <cmath>
#include <cstdio>

namespace zs::mesh {

bool is_chat_port(uint32_t portnum) {
    return portnum == kPortText || portnum == kPortAlert || portnum == kPortWaypoint;
}

Record chat_record(const MeshPacket& p, uint32_t me, bool mine, long now) {
    Record r;
    r.kind = Record::Msg;
    r.time = now;
    r.id = p.id;
    r.from = mine ? me : p.from;
    r.to = p.to;
    r.channel = p.channel;
    r.mine = mine;
    r.pki = p.pki_encrypted;
    r.via_mqtt = p.via_mqtt;
    r.state = mine ? Delivery::Queued : Delivery::Received;
    r.snr_q = mine ? 0 : int(std::lround(p.rx_snr * 4));
    r.rssi = mine ? 0 : p.rx_rssi;
    r.hops = mine ? -1 : p.hops();
    r.reply_id = p.decoded.reply_id;
    const std::string& body = p.decoded.payload;
    if (p.decoded.portnum == kPortWaypoint) {
        Waypoint w;
        decode(body, w);
        r.waypoint = true;
        r.text = w.name.empty() ? "Waypoint" : w.name;
        if (!w.description.empty()) r.text += ": " + w.description;
        if (w.has_latlon) {
            char b[48];
            std::snprintf(b, sizeof b, " (%.5f, %.5f)", w.lat_i * 1e-7, w.lon_i * 1e-7);
            r.text += b;
        }
        return r;
    }
    r.reaction = p.decoded.emoji != 0 && p.decoded.reply_id != 0;
    r.bell = p.decoded.portnum == kPortAlert || body.find('\a') != std::string::npos;
    r.text = body;
    return r;
}

Recorder::Sent* Recorder::find(uint32_t id) {
    for (Sent& s : sent_)
        if (s.id == id) return &s;
    return nullptr;
}

bool Recorder::seen(uint32_t from, uint32_t id) {
    uint64_t k = (uint64_t(from) << 32) | id;
    for (uint64_t x : seen_)
        if (x == k) return true;
    seen_.push_back(k);
    if (seen_.size() > 256) seen_.pop_front();
    return false;
}

void Recorder::news(Sent& s, Delivery state, uint32_t error, uint32_t acker, long now, std::vector<Record>& out) {
    if (!advance_delivery(s.state, s.error, state, error)) return;
    Record r;
    r.kind = Record::Ack;
    r.time = now;
    r.id = s.id;
    r.state = s.state;
    r.error = s.error;
    r.acker = acker;
    out.push_back(std::move(r));
}

void Recorder::on_to_radio(const ToRadio& t, long now, std::vector<Record>& out) {
    if (t.which != ToRadio::Packet || !t.packet.has_decoded || !is_chat_port(t.packet.decoded.portnum)) return;
    const MeshPacket& p = t.packet;
    if (p.id == 0 || find(p.id)) return;   // no id: nothing to match an ack to; twice: a resend
    out.push_back(chat_record(p, me_, true, now));
    Sent s;
    s.id = p.id;
    s.to = p.to;
    s.at = now;
    sent_.push_back(s);
    if (sent_.size() > 128) sent_.pop_front();
}

void Recorder::on_from_radio(const FromRadio& f, long now, std::vector<Record>& out) {
    if (f.which == FromRadio::MyInfo) {
        me_ = f.my_info.my_node_num;
        return;
    }
    if (f.which == FromRadio::QueueStatusMsg) {
        const QueueStatus& q = f.queue_status;
        Sent* s = q.mesh_packet_id ? find(q.mesh_packet_id) : nullptr;
        if (!s) return;
        if (q.res == 0) {
            news(*s, Delivery::Sent, 0, 0, now, out);
        } else {
            // The radio's own error numbers, which overlap Routing.Error's:
            // 34 here is "transmit disabled" (no region), 33 "no radio".
            uint32_t err = q.res == 34 ? kErrLocalRadioOff : q.res == 33 ? kErrLocalNoRadio : kErrLocalQueue;
            news(*s, Delivery::Failed, err, 0, now, out);
        }
        return;
    }
    if (f.which != FromRadio::Packet || !f.packet.has_decoded) return;
    const MeshPacket& p = f.packet;
    if (p.decoded.portnum == kPortRouting) {
        Sent* s = p.decoded.request_id ? find(p.decoded.request_id) : nullptr;
        if (!s) return;
        Routing r;
        if (!decode(p.decoded.payload, r) || r.which != Routing::ErrorReason) return;
        if (r.error_reason != kErrNone) {
            news(*s, Delivery::Failed, r.error_reason, p.from, now, out);
        } else if (s->to != kBroadcast && p.from == s->to) {
            news(*s, Delivery::Delivered, 0, p.from, now, out);
        } else {
            // From ourselves: the radio heard another node repeat it. On a
            // channel that's all the confirmation there is.
            news(*s, Delivery::Relayed, 0, p.from, now, out);
        }
        return;
    }
    if (!is_chat_port(p.decoded.portnum)) return;
    // A copy of something a client of ours sent (the daemon shows each
    // client what the others send) is already recorded.
    if ((me_ && p.from == me_) || p.from == 0) {
        if (p.id && find(p.id)) return;
        if (p.id && seen(me_, p.id)) return;
        out.push_back(chat_record(p, me_, true, now));
        out.back().state = Delivery::Sent;   // sent from the radio itself (its own canned messages)
        return;
    }
    if (p.id && seen(p.from, p.id)) return;
    out.push_back(chat_record(p, me_, false, now));
}

void Recorder::tick(long now, std::vector<Record>& out) {
    for (Sent& s : sent_) {
        if ((s.state == Delivery::Queued || s.state == Delivery::Sent) && now - s.at > kNoAnswerAfter)
            news(s, Delivery::Failed, kErrLocalNoAnswer, 0, now, out);
    }
}

void Recorder::link_lost(long now, std::vector<Record>& out) {
    for (Sent& s : sent_) {
        if (s.state == Delivery::Queued) news(s, Delivery::Failed, kErrLocalQueue, 0, now, out);
    }
}

}  // namespace zs::mesh
