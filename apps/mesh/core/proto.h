// The Meshtastic client API (meshtastic/protobufs: mesh.proto, channel.proto,
// config.proto, admin.proto, telemetry.proto, portnums.proto), as plain
// structs with an encoder and a decoder each. Only the fields Mesh uses are
// named; settings messages also keep their bytes as they arrived (`raw`), so
// writing one back changes the field asked for and nothing else.
//
// The codec is checked against Meshtastic's own definitions: tests/vectors.h
// is made by tests/make_vectors.py with Google's protobuf library.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace zs::mesh {

constexpr uint32_t kBroadcast = 0xffffffffu;

// portnums.proto
enum Port : uint32_t {
    kPortText = 1,
    kPortPosition = 3,
    kPortNodeInfo = 4,
    kPortRouting = 5,
    kPortAdmin = 6,
    kPortWaypoint = 8,
    kPortAlert = 11,
    kPortRangeTest = 66,
    kPortTelemetry = 67,
    kPortTraceroute = 70,
    kPortNeighborInfo = 71,
};

// Routing.Error
enum RoutingError : uint32_t {
    kErrNone = 0,
    kErrNoRoute = 1,
    kErrGotNak = 2,
    kErrTimeout = 3,
    kErrNoInterface = 4,
    kErrMaxRetransmit = 5,
    kErrNoChannel = 6,
    kErrTooLarge = 7,
    kErrNoResponse = 8,
    kErrDutyCycle = 9,
    kErrBadRequest = 32,
    kErrNotAuthorized = 33,
    kErrPkiFailed = 34,
    kErrPkiUnknownPubkey = 35,
    kErrAdminBadSessionKey = 36,
    kErrAdminKeyUnauthorized = 37,
    kErrRateLimit = 38,
    kErrPkiSendFail = 39,
};
// What went wrong, for a person ("No reply", "Too long").
const char* routing_error_text(uint32_t err);

struct Position {
    bool has_latlon = false;
    int32_t lat_i = 0, lon_i = 0;      // degrees * 1e7
    bool has_alt = false;
    int32_t alt = 0;                   // metres above sea level
    uint32_t time = 0;                 // unix seconds
    uint32_t location_source = 0;
    bool has_speed = false, has_track = false;
    uint32_t ground_speed = 0;         // m/s
    uint32_t ground_track = 0;         // degrees * 1e5
    uint32_t sats_in_view = 0;
    uint32_t precision_bits = 0;

    double lat() const { return lat_i * 1e-7; }
    double lon() const { return lon_i * 1e-7; }
};

struct User {
    std::string id, long_name, short_name;
    uint32_t hw_model = 0;
    bool is_licensed = false;
    uint32_t role = 0;
    std::string public_key;
    bool has_unmessagable = false, is_unmessagable = false;
    std::string raw;
};

struct DeviceMetrics {
    bool has_battery = false, has_voltage = false, has_ch_util = false, has_air_util = false, has_uptime = false;
    uint32_t battery_level = 0;        // percent; 101 means powered
    float voltage = 0, channel_utilization = 0, air_util_tx = 0;
    uint32_t uptime_seconds = 0;
};

struct EnvironmentMetrics {
    bool has_temperature = false, has_humidity = false, has_pressure = false, has_iaq = false;
    float temperature = 0, relative_humidity = 0, barometric_pressure = 0;
    uint32_t iaq = 0;
};

struct LocalStats {
    uint32_t uptime_seconds = 0;
    float channel_utilization = 0, air_util_tx = 0;
    uint32_t num_packets_tx = 0, num_packets_rx = 0, num_packets_rx_bad = 0;
    uint32_t num_online_nodes = 0, num_total_nodes = 0;
    uint32_t num_rx_dupe = 0, num_tx_relay = 0, num_tx_relay_canceled = 0;
    int32_t noise_floor = 0;
};

struct Telemetry {
    uint32_t time = 0;
    enum Which { None = 0, Device = 2, Environment = 3, Local = 6, Other = 99 } which = None;
    DeviceMetrics device;
    EnvironmentMetrics environment;
    LocalStats local;
};

struct RouteDiscovery {
    std::vector<uint32_t> route, route_back;       // the nodes between, not the two ends
    std::vector<int32_t> snr_towards, snr_back;    // dB * 4 per hop; -128 unknown
};

struct Routing {
    enum Which { None = 0, RouteRequest = 1, RouteReply = 2, ErrorReason = 3 } which = None;
    uint32_t error_reason = 0;
};

struct Waypoint {
    uint32_t id = 0;
    bool has_latlon = false;
    int32_t lat_i = 0, lon_i = 0;
    uint32_t expire = 0;
    std::string name, description;
};

struct Data {
    uint32_t portnum = 0;
    std::string payload;
    bool want_response = false;
    uint32_t dest = 0, source = 0, request_id = 0, reply_id = 0, emoji = 0;
    bool has_bitfield = false;
    uint32_t bitfield = 0;
};

struct MeshPacket {
    uint32_t from = 0, to = 0, channel = 0;
    bool has_decoded = false;
    Data decoded;
    std::string encrypted;             // we can't read it (a channel we don't have)
    uint32_t id = 0;
    uint32_t rx_time = 0;
    float rx_snr = 0;
    uint32_t hop_limit = 0;
    bool want_ack = false;
    uint32_t priority = 0;
    int32_t rx_rssi = 0;
    bool via_mqtt = false;
    uint32_t hop_start = 0;
    std::string public_key;
    bool pki_encrypted = false;
    uint32_t next_hop = 0, relay_node = 0;

    // Hops this packet took to reach us, or -1 if it doesn't say.
    int hops() const {
        if (hop_start == 0 || hop_limit > hop_start) return -1;
        return int(hop_start - hop_limit);
    }
};

struct NodeInfo {
    uint32_t num = 0;
    bool has_user = false, has_position = false, has_metrics = false, has_hops = false;
    User user;
    Position position;
    float snr = 0;
    uint32_t last_heard = 0;
    DeviceMetrics metrics;
    uint32_t channel = 0;
    bool via_mqtt = false;
    uint32_t hops_away = 0;
    bool is_favorite = false, is_ignored = false, is_muted = false;
};

struct MyNodeInfo {
    uint32_t my_node_num = 0, reboot_count = 0, min_app_version = 0, nodedb_count = 0;
    std::string pio_env;
};

struct DeviceMetadata {
    std::string firmware_version;
    uint32_t device_state_version = 0;
    bool can_shutdown = false, has_wifi = false, has_bluetooth = false, has_ethernet = false, has_pkc = false;
    uint32_t role = 0, hw_model = 0;
};

struct ChannelSettings {
    std::string psk, name;
    uint32_t id = 0;
    bool uplink_enabled = false, downlink_enabled = false;
    uint32_t position_precision = 0;   // module_settings
    bool is_muted = false;             // module_settings
    std::string raw;
};

struct Channel {
    enum Role : uint32_t { Disabled = 0, Primary = 1, Secondary = 2 };
    int32_t index = 0;
    uint32_t role = Disabled;
    ChannelSettings settings;
};

// Config: which part, and that part's bytes. The parts Mesh changes have a
// struct; the rest pass through.
struct Config {
    enum Which : uint32_t {
        None = 0, Device = 1, PositionCfg = 2, Power = 3, Network = 4, Display = 5, LoRa = 6,
        Bluetooth = 7, Security = 8, SessionKey = 9, DeviceUi = 10,
    };
    uint32_t which = None;
    std::string raw;
};

struct LoRaConfig {
    bool use_preset = false;
    uint32_t modem_preset = 0;
    uint32_t region = 0;
    uint32_t hop_limit = 0;
    bool tx_enabled = false;
    int32_t tx_power = 0;
    uint32_t channel_num = 0;
    bool override_duty_cycle = false, rx_boosted_gain = false, ignore_mqtt = false, ok_to_mqtt = false;
    std::string raw;
};

struct DeviceConfig {
    uint32_t role = 0;
    uint32_t rebroadcast_mode = 0;
    uint32_t node_info_broadcast_secs = 0;
    std::string tzdef;
    std::string raw;
};

struct PositionConfig {
    uint32_t position_broadcast_secs = 0;
    bool smart_enabled = false, fixed_position = false;
    uint32_t gps_mode = 0;
    std::string raw;
};

struct QueueStatus {
    int32_t res = 0;
    uint32_t free = 0, maxlen = 0, mesh_packet_id = 0;
};

struct ClientNotification {
    bool has_reply_id = false;
    uint32_t reply_id = 0, time = 0, level = 0, type = 0;
    std::string message;
};

struct FromRadio {
    enum Which : uint32_t {
        None = 0, Packet = 2, MyInfo = 3, NodeInfoMsg = 4, ConfigMsg = 5, LogRecord = 6, ConfigComplete = 7,
        Rebooted = 8, ModuleConfig = 9, ChannelMsg = 10, QueueStatusMsg = 11, XModem = 12, Metadata = 13,
        MqttProxy = 14, FileInfo = 15, Notification = 16, DeviceUi = 17,
    };
    uint32_t id = 0;
    uint32_t which = None;             // the field number, also for ones not listed
    MeshPacket packet;
    MyNodeInfo my_info;
    NodeInfo node_info;
    Config config;
    uint32_t config_complete_id = 0;
    Channel channel;
    QueueStatus queue_status;
    DeviceMetadata metadata;
    ClientNotification notification;

    // Part of the answer to want_config (as opposed to live traffic).
    bool is_config() const {
        return which == MyInfo || which == NodeInfoMsg || which == ConfigMsg || which == ConfigComplete ||
               which == ModuleConfig || which == ChannelMsg || which == Metadata || which == FileInfo ||
               which == DeviceUi || which == 19 /* region_presets */;
    }
};

struct ToRadio {
    enum Which : uint32_t { None = 0, Packet = 1, WantConfig = 3, Disconnect = 4, XModem = 5, MqttProxy = 6, Heartbeat = 7 };
    uint32_t which = None;
    MeshPacket packet;
    uint32_t want_config_id = 0;
    uint32_t heartbeat_nonce = 0;
};

// AdminMessage: what we send and the answers we read.
struct AdminMessage {
    enum Which : uint32_t {
        None = 0,
        GetChannelRequest = 1, GetChannelResponse = 2, GetOwnerRequest = 3, GetOwnerResponse = 4,
        GetConfigRequest = 5, GetConfigResponse = 6, GetMetadataRequest = 12, GetMetadataResponse = 13,
        SetOwner = 32, SetChannel = 33, SetConfig = 34,
        RemoveByNodenum = 38, SetFavorite = 39, RemoveFavorite = 40, SetFixedPosition = 41,
        RemoveFixedPosition = 42, SetIgnored = 47, RemoveIgnored = 48,
        BeginEdit = 64, CommitEdit = 65,
        FactoryResetDevice = 94, RebootSeconds = 97, ShutdownSeconds = 98, FactoryResetConfig = 99,
        NodedbReset = 100,
    };
    uint32_t which = None;
    uint64_t value = 0;                // scalar variants (a node number, seconds, true)
    User user;
    Channel channel;
    Config config;
    Position position;
    DeviceMetadata metadata;
};

// ---- Encoding (a message's bytes, without framing).
std::string encode(const Position&);
std::string encode(const User&);                 // from `raw` plus the named fields
std::string encode(const DeviceMetrics&);
std::string encode(const Telemetry&);
std::string encode(const RouteDiscovery&);
std::string encode(const Routing&);
std::string encode(const Waypoint&);
std::string encode(const Data&);
std::string encode(const MeshPacket&);
std::string encode(const NodeInfo&);
std::string encode(const MyNodeInfo&);
std::string encode(const DeviceMetadata&);
std::string encode(const ChannelSettings&);      // from `raw` plus the named fields
std::string encode(const Channel&);
std::string encode(const Config&);
std::string encode(const LoRaConfig&);           // from `raw` plus the named fields
std::string encode(const DeviceConfig&);
std::string encode(const PositionConfig&);
std::string encode(const QueueStatus&);
std::string encode(const ClientNotification&);
std::string encode(const FromRadio&);
std::string encode(const ToRadio&);
std::string encode(const AdminMessage&);

// ---- Decoding. False if the bytes aren't a protobuf message at all;
// unknown fields are skipped.
bool decode(std::string_view, Position&);
bool decode(std::string_view, User&);
bool decode(std::string_view, DeviceMetrics&);
bool decode(std::string_view, Telemetry&);
bool decode(std::string_view, RouteDiscovery&);
bool decode(std::string_view, Routing&, RouteDiscovery* route = nullptr);
bool decode(std::string_view, Waypoint&);
bool decode(std::string_view, Data&);
bool decode(std::string_view, MeshPacket&);
bool decode(std::string_view, NodeInfo&);
bool decode(std::string_view, MyNodeInfo&);
bool decode(std::string_view, DeviceMetadata&);
bool decode(std::string_view, ChannelSettings&);
bool decode(std::string_view, Channel&);
bool decode(std::string_view, Config&);
bool decode(std::string_view, LoRaConfig&);
bool decode(std::string_view, DeviceConfig&);
bool decode(std::string_view, PositionConfig&);
bool decode(std::string_view, QueueStatus&);
bool decode(std::string_view, ClientNotification&);
bool decode(std::string_view, FromRadio&);
bool decode(std::string_view, ToRadio&);
bool decode(std::string_view, AdminMessage&);

// ---- The stream both ways on TCP port 4403: 0x94 0xC3, a 16-bit length
// (big-endian), then one ToRadio or FromRadio.
constexpr size_t kMaxFrame = 512;
std::string frame(std::string_view payload);

class Deframer {
public:
    void feed(const char* data, size_t n) { buf_.append(data, n); }
    bool next(std::string& payload);
    void clear() { buf_.clear(); }
    size_t pending() const { return buf_.size(); }

private:
    std::string buf_;
};

// ---- Names for the numbers.
struct Named {
    uint32_t value;
    const char* name;      // "EU_868": as Meshtastic writes it
    const char* label;     // "Europe 868 MHz": for a person
};
const std::vector<Named>& regions();
const std::vector<Named>& modem_presets();
const std::vector<Named>& device_roles();
const char* region_name(uint32_t);
const char* preset_name(uint32_t);          // "LongFast": also the default channel's name
const char* role_name(uint32_t);
std::string hw_model_name(uint32_t);

// "!a1b2c3d4"
std::string node_id(uint32_t num);
// The node number in "!a1b2c3d4" or "a1b2c3d4"; 0 if it isn't one.
uint32_t parse_node_id(std::string_view);

// ---- Channel sharing: https://meshtastic.org/e/#<base64url of a ChannelSet>.
struct ChannelSet {
    std::vector<ChannelSettings> settings;
    bool has_lora = false;
    LoRaConfig lora;
    bool add_only = false;     // the link said "/e/?add=true#": add, don't replace
};
std::string channel_url(const ChannelSet&);
bool parse_channel_url(std::string_view url, ChannelSet&);
std::string base64url(std::string_view);
bool base64_decode(std::string_view, std::string& out);   // either alphabet, padding optional
std::string base64(std::string_view);

// A channel's name as shown: its own, or the preset's ("LongFast") when it has
// none. The key in words: "Default key", "No encryption", "Private key".
std::string channel_display_name(const ChannelSettings&, const LoRaConfig&);
const char* psk_kind(const std::string& psk);

}  // namespace zs::mesh
