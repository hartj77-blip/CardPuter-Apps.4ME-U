// A Meshtastic client: everything Mesh knows about the radio and the mesh,
// and everything it can ask of them. No sockets and no screen. Bytes from
// the radio go in (on_from_radio), frames for the radio come out (take_out),
// history records go in (apply), and the screens read the rest.
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "apps/mesh/core/proto.h"
#include "apps/mesh/core/store.h"

namespace zs::mesh {

struct Node {
    uint32_t num = 0;
    std::string long_name, short_name;
    uint32_t hw_model = 0, role = 0;
    bool has_user = false, has_key = false, licensed = false;
    bool has_unmessagable = false, unmessagable = false;
    bool has_snr = false;
    float snr = 0;
    int rssi = 0;                      // 0: not known
    long last_heard = 0;
    int hops = -1;                     // -1: not known; 0: heard directly
    uint32_t channel = 0;
    bool via_mqtt = false, favorite = false, ignored = false;
    bool has_pos = false, has_alt = false;
    double lat = 0, lon = 0;
    int alt = 0;
    long pos_time = 0;
    uint32_t precision_bits = 0;
    bool has_metrics = false, has_env = false;
    DeviceMetrics metrics;
    EnvironmentMetrics env;
    long metrics_time = 0, env_time = 0;

    // "HILL", else the long name, else "!a1b2c3d4".
    std::string name() const;
    // The long name if it has one: "Hilltop Relay", else "!a1b2c3d4".
    std::string full_name() const;
};

struct Reaction {
    uint32_t from = 0;
    std::string text;                  // the emoji as sent
};

struct Message {
    uint32_t id = 0, from = 0, to = 0;
    long time = 0;
    std::string text;
    bool mine = false, pki = false, via_mqtt = false, bell = false, waypoint = false;
    Delivery state = Delivery::Received;
    uint32_t error = 0;
    float snr = 0;
    int rssi = 0, hops = -1;
    uint32_t reply_id = 0;
    std::vector<Reaction> reactions;
};

struct Chat {
    std::string key;                   // "c0", "d1a2b3c4d"
    bool dm = false;
    uint32_t peer = 0;                 // dm: the other node
    uint32_t channel = 0;              // not dm: the channel index
    std::vector<Message> msgs;
    long read_upto = 0;                // messages up to this time have been seen
    int unread = 0;
    long last_time() const { return msgs.empty() ? 0 : msgs.back().time; }
};

struct TraceResult {
    uint32_t dest = 0;
    long asked = 0, answered = 0;      // answered 0: still waiting, or failed
    bool failed = false;
    uint32_t error = 0;
    RouteDiscovery route;
    bool has_back = false;
};

struct LogLine {
    long time = 0;
    bool out = false;
    uint32_t from = 0, to = 0, port = 0;
    float snr = 0;
    int rssi = 0, hops = -1;
    std::string what;                  // "Text", "Position", "Telemetry" ...
};

class Client {
public:
    Client();

    // ---- The link
    // The connection came up: forget the old session, ask for everything.
    void link_up(long now);
    void link_down();
    void on_from_radio(const std::string& payload, long now);
    // Framed bytes to write to the radio.
    std::string take_out();
    // Heartbeats, timeouts, the send queue. About once a second is enough.
    void tick(long now);

    bool linked() const { return linked_; }
    bool ready() const { return ready_; }           // the radio's settings and nodes are all here
    bool ever_ready() const { return ever_ready_; } // ... or were, before the link dropped (shown, greyed)
    // Counts that go up when something a screen shows has changed.
    int nodes_rev() const { return nodes_rev_; }
    int chats_rev() const { return chats_rev_; }
    int config_rev() const { return config_rev_; }
    int log_rev() const { return log_rev_; }

    // ---- This node and its radio
    uint32_t my_num() const { return me_; }
    const Node* me() const { return node(me_); }
    const DeviceMetadata& metadata() const { return metadata_; }
    const LoRaConfig& lora() const { return lora_; }
    const DeviceConfig& device() const { return device_; }
    const PositionConfig& position_config() const { return position_; }
    bool has_lora() const { return has_lora_; }
    // No region chosen yet: the radio neither sends nor hears.
    bool needs_region() const { return ready_ && has_lora_ && lora_.region == 0; }
    bool can_transmit() const { return ready_ && has_lora_ && lora_.region != 0 && lora_.tx_enabled; }
    const LocalStats& stats() const { return stats_; }
    bool has_stats() const { return has_stats_; }
    const QueueStatus& queue() const { return queue_; }

    // ---- Nodes
    const Node* node(uint32_t num) const;
    const std::map<uint32_t, Node>& nodes() const { return nodes_; }
    // The name to show for a node number, known or not.
    std::string node_name(uint32_t num) const;
    std::string node_full_name(uint32_t num) const;
    // Heard within two hours.
    static bool online(const Node& n, long now) { return n.last_heard > 0 && now - n.last_heard < 7200; }
    int online_count(long now) const;

    // Where we are, for distances: the app's GPS if it gave one, else this
    // node's own position.
    void set_here(bool valid, double lat, double lon);
    bool here(double& lat, double& lon) const;

    // ---- Channels
    const std::vector<Channel>& channels() const { return channels_; }   // all 8 slots, by index
    const Channel* channel(uint32_t index) const;
    std::string channel_name(uint32_t index) const;
    int free_channel_slot() const;                                         // -1 when all 8 are used
    // The link that shares one channel (to add) or all of them (to replace).
    std::string share_url(int index) const;

    // ---- Chats
    const std::vector<Chat>& chats() const { return chats_; }             // channels first, then DMs, newest first
    const Chat* chat(const std::string& key) const;
    std::string chat_title(const Chat&) const;
    int total_unread() const;
    // History records (from the journal, or our own recorder in the simulator).
    void apply(const Record&);
    void clear_chats();
    void mark_read(const std::string& key, long now);
    // Unread marks to keep between runs: "key<TAB>time" lines.
    std::string read_marks() const;
    void set_read_marks(const std::string& text);
    // Make sure a chat exists (a DM to a node you haven't talked to yet).
    const Chat& open_dm(uint32_t node);

    // ---- Sending. Each returns the packet id, or 0 if it couldn't be sent
    // (see last_error()).
    uint32_t send_text(const std::string& chat_key, const std::string& text, uint32_t reply_id = 0, bool bell = false);
    uint32_t send_reaction(const std::string& chat_key, uint32_t reply_id, const std::string& emoji);
    uint32_t send_waypoint(const std::string& chat_key, const std::string& name, double lat, double lon, long now);
    const std::string& last_error() const { return last_error_; }
    // Bytes a message may have.
    static constexpr size_t kMaxText = 200;

    // ---- Asking other nodes
    uint32_t traceroute(uint32_t node, long now);
    uint32_t request_position(uint32_t node, long now);
    uint32_t request_telemetry(uint32_t node, long now, bool environment = false);
    uint32_t request_user(uint32_t node, long now);
    // Seconds until the radio will take another traceroute (it allows one in 30).
    int traceroute_wait(long now) const;
    const std::vector<TraceResult>& traces() const { return traces_; }    // oldest first, at most 20
    const TraceResult* last_trace(uint32_t node) const;

    // ---- Changing this radio. Each is sent at once; the radio saves, and
    // for radio settings restarts (the link drops and comes back).
    void set_owner(const std::string& long_name, const std::string& short_name);
    void set_lora(const LoRaConfig& lora);
    void set_device(const DeviceConfig& device);
    void set_position_config(const PositionConfig& position);
    // Several changes, one save and one restart.
    void begin_edit();
    void commit_edit();
    void set_channel(const Channel& channel);
    void delete_channel(uint32_t index);
    // A shared link: add its channels to free slots (false if they don't
    // fit), or replace every channel and the radio settings with the link's.
    bool add_channels(const ChannelSet& set, std::string* why = nullptr);
    void replace_channels(const ChannelSet& set);
    void set_favorite(uint32_t node, bool on);
    void set_ignored(uint32_t node, bool on);
    void remove_node(uint32_t node);
    void reset_nodedb();
    void reboot(int seconds = 2);
    void set_fixed_position(double lat, double lon, int alt_m);
    void remove_fixed_position();
    // Tell the radio where we are (a phone does the same with its GPS): the
    // node shares it as its own settings say. False if sent less than
    // `min_gap` seconds ago.
    bool send_own_position(double lat, double lon, bool has_alt, int alt_m, long now, int min_gap = 30);

    // ---- News for a toast: "Traceroute to HILL: no answer".
    std::vector<std::string> take_notices();

    // ---- Recent packets, newest last (at most 200).
    const std::deque<LogLine>& log() const { return log_; }

    // What a traceroute found, in words, a line per direction.
    std::vector<std::string> trace_lines(const TraceResult&) const;

private:
    struct Pending {
        enum Kind { Trace, PositionReq, TelemetryReq, UserReq, Admin } kind = Trace;
        uint32_t id = 0, node = 0;
        long at = 0;
    };
    uint32_t next_id();
    uint32_t hop_limit() const;
    MeshPacket make_packet(uint32_t to, uint32_t channel, uint32_t port, std::string payload, bool want_response);
    uint32_t queue_packet(MeshPacket p);
    void send_admin(const AdminMessage& a, bool want_response = false);
    void want_config();
    void on_packet(const MeshPacket& p, long now);
    void on_node_info(const NodeInfo& n);
    void on_config(const Config& c);
    void on_routing(const MeshPacket& p, long now);
    void on_admin(const MeshPacket& p);
    Node& touch(uint32_t num);
    void heard(Node& n, const MeshPacket& p, long now);
    void log_packet(const MeshPacket& p, bool out, long now);
    Chat& chat_for(const std::string& key);
    void sort_chats();
    void ensure_channel_chats();
    void recount(Chat& c);
    void notice(const std::string& s);
    Pending* pending(uint32_t id);
    void drop_pending(uint32_t id);
    bool chat_target(const std::string& key, uint32_t& to, uint32_t& channel);

    bool linked_ = false, ready_ = false, ever_ready_ = false;
    uint32_t nonce_ = 0;
    long last_tx_ = 0, last_rx_ = 0, config_asked_ = 0, now_ = 0;
    std::string out_;
    uint32_t me_ = 0;
    uint32_t packet_id_ = 0;
    DeviceMetadata metadata_;
    LoRaConfig lora_;
    DeviceConfig device_;
    PositionConfig position_;
    bool has_lora_ = false;
    LocalStats stats_;
    bool has_stats_ = false;
    QueueStatus queue_;
    std::map<uint32_t, Node> nodes_;
    std::vector<Channel> channels_;
    std::vector<Chat> chats_;
    std::map<std::string, long> read_marks_;
    std::vector<Pending> pending_;
    std::vector<TraceResult> traces_;
    std::vector<std::string> notices_;
    std::deque<LogLine> log_;
    std::string last_error_;
    long last_trace_at_ = 0, last_pos_sent_ = 0, last_text_at_ = 0;
    std::deque<std::string> held_;           // texts waiting their turn (the radio takes one in 2 s)
    std::map<uint32_t, bool> fresh_;         // nodes named in the download under way
    bool here_valid_ = false;
    double here_lat_ = 0, here_lon_ = 0;
    int nodes_rev_ = 0, chats_rev_ = 0, config_rev_ = 0, log_rev_ = 0;
};

// The delivery mark and words for a message of ours: "Sending...",
// "Heard by the mesh", "Delivered".
const char* delivery_text(const Message&, bool dm);

}  // namespace zs::mesh
