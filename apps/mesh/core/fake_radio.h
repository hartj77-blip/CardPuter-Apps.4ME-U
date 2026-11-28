// A stand-in for meshtasticd's client API: it answers the same bytes a real
// node does (the settings download, queue status, acks, traceroutes,
// positions, telemetry, admin changes), with a small made-up mesh behind
// it. The simulator and the browser emulator run Mesh against it, and the
// tests drive the client and the daemon with it.
//
// Its behaviour follows the firmware (src/mesh/PhoneAPI.cpp and the modules):
// one text in 2 s and one traceroute in 30 or the packet is refused; no
// region means nothing is sent (queue status 34, no ack); an implicit ack
// comes from our own node number, a real one from the node addressed; a
// change to the radio settings restarts it.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "apps/mesh/core/proto.h"

namespace zs::mesh {

class FakeRadio {
public:
    struct Options {
        bool region_set = true;
        bool chatter = true;        // the made-up nodes talk now and then
        bool neighbours = true;     // false: a radio alone (nothing acks)
        long epoch = 1795000000;    // unix time at 0 ms
        uint32_t seed = 7;
    };
    FakeRadio();
    explicit FakeRadio(const Options& o);

    static constexpr uint32_t kMe = 0x0ce20001;

    // Bytes from the client; bytes for the client.
    void feed(const char* data, size_t n);
    void feed(const std::string& s) { feed(s.data(), s.size()); }
    std::string take_out();
    // Time passes. Milliseconds since it was made, never backwards.
    void advance(long now_ms);
    long now_ms() const { return now_ms_; }
    long unix_now() const { return opt_.epoch + now_ms_ / 1000; }

    // The radio restarted itself (after a settings change): the client's
    // connection is gone and it must connect again. True once per restart.
    bool take_restart();
    // A new connection: what was queued for the old one is dropped.
    void reconnect();

    // ---- For tests: make things happen, and see what the radio holds.
    void inject_text(uint32_t from, uint32_t to, uint32_t channel, const std::string& text, uint32_t reply_id = 0,
                     uint32_t emoji = 0);
    void inject(const MeshPacket& p);
    uint32_t last_packet_id() const { return last_id_; }
    const std::string& long_name() const { return long_name_; }
    const std::string& short_name() const { return short_name_; }
    LoRaConfig lora() const;
    DeviceConfig device() const;
    const std::vector<Channel>& channels() const { return channels_; }
    int texts_taken() const { return texts_taken_; }
    int restarts() const { return restarts_; }
    int config_downloads() const { return downloads_; }
    bool has_node(uint32_t num) const;
    bool node_favorite(uint32_t num) const;
    bool node_ignored(uint32_t num) const;
    bool own_position(double& lat, double& lon) const;
    const std::vector<MeshPacket>& sent_to_mesh() const { return air_; }   // what it would have transmitted
    // A node of the made-up mesh by short name ("HILL"), 0 if none.
    uint32_t node_by_short(const std::string& short_name) const;

private:
    struct SimNode {
        uint32_t num = 0;
        std::string long_name, short_name;
        uint32_t hw = 0, role = 0;
        int hops = 0;               // 0 direct
        float snr = 0;
        int rssi = 0;
        long heard_ago = 0;         // seconds before the epoch
        bool has_pos = false;
        double north_m = 0, east_m = 0;
        int alt = 0;
        int battery = -1;           // -1: says nothing
        bool has_env = false;
        float temp = 0, humidity = 0, pressure = 0;
        bool mqtt = false, named = true, reachable = true, has_key = true;
        bool favorite = false, ignored = false;
        bool shares_position = true;
    };
    struct Event {
        long due = 0;
        std::function<void()> run;
    };
    void build_mesh();
    void emit(const FromRadio& f);
    void emit_raw(const std::string& payload);
    void at(long delay_ms, std::function<void()> fn);
    void on_to_radio(const ToRadio& t);
    void on_packet(MeshPacket p);
    void on_admin(const MeshPacket& p);
    void send_config(uint32_t nonce);
    void queue_status(int res, uint32_t id);
    void routing(uint32_t from, uint32_t request_id, uint32_t error);
    void deliver(MeshPacket p, const SimNode* via);
    NodeInfo own_info() const;
    NodeInfo info_of(const SimNode& n) const;
    Position position_of(const SimNode& n) const;
    SimNode* find(uint32_t num);
    const SimNode* find(uint32_t num) const;
    void chatter();
    void schedule_restart();
    uint32_t rnd();
    uint32_t new_id();

    Options opt_;
    Deframer in_;
    std::string out_;
    long now_ms_ = 0;
    std::vector<Event> events_;
    std::vector<SimNode> nodes_;
    std::vector<Channel> channels_;
    std::string long_name_ = "Cardputer Zero", short_name_ = "ZERO";
    std::string lora_raw_, device_raw_, position_raw_;
    bool own_pos_ = false;
    double own_lat_ = 0, own_lon_ = 0;
    int own_alt_ = 0;
    bool in_edit_ = false, restart_wanted_ = false, restarted_ = false, down_ = false;
    long last_text_ms_ = -100000, last_trace_ms_ = -100000, next_chatter_ms_ = 6000;
    uint32_t from_id_ = 1, last_id_ = 0, rng_ = 1, id_ = 0x40000000;
    int texts_taken_ = 0, restarts_ = 0, downloads_ = 0, chat_turn_ = 0;
    std::vector<MeshPacket> air_;
};

}  // namespace zs::mesh
