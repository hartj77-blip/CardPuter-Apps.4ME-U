#!/usr/bin/env python3
"""Make tests/vectors.h: messages built with Google's protobuf library from
Meshtastic's own .proto files, as bytes for proto_test.cpp to check the
hand-written codec in core/proto.cpp against.

Only needed when a vector is added or Meshtastic changes a message:

    git clone --depth 1 https://github.com/meshtastic/protobufs
    pip install grpcio-tools
    python3 -m grpc_tools.protoc -Iprotobufs \
        -I"$(python3 -c 'import grpc_tools,os;print(os.path.dirname(grpc_tools.__file__)+"/_proto")')" \
        --python_out=gen protobufs/meshtastic/*.proto protobufs/nanopb.proto
    PYTHONPATH=gen python3 apps/mesh/tests/make_vectors.py > apps/mesh/tests/vectors.h
"""
import base64

from meshtastic import admin_pb2, apponly_pb2, channel_pb2, config_pb2, mesh_pb2, portnums_pb2, telemetry_pb2

V = []


def add(name, msg):
    V.append((name, msg.SerializeToString()))


# --- ToRadio
m = mesh_pb2.ToRadio()
m.want_config_id = 0x1234ABCD
add("to_want_config", m)

m = mesh_pb2.ToRadio()
m.packet.to = 0xFFFFFFFF
m.packet.channel = 1
m.packet.id = 0x11223344
m.packet.want_ack = True
m.packet.hop_limit = 3
m.packet.decoded.portnum = portnums_pb2.TEXT_MESSAGE_APP
m.packet.decoded.payload = b"hello mesh"
add("to_text", m)

m = mesh_pb2.ToRadio()
m.heartbeat.SetInParent()
add("to_heartbeat", m)

m = mesh_pb2.ToRadio()
m.disconnect = True
add("to_disconnect", m)

# --- FromRadio: the answer to want_config
m = mesh_pb2.FromRadio()
m.id = 7
m.my_info.my_node_num = 0xCAFE0001
m.my_info.reboot_count = 12
m.my_info.min_app_version = 30200
m.my_info.pio_env = "native"
m.my_info.nodedb_count = 9
add("from_my_info", m)

m = mesh_pb2.FromRadio()
m.id = 8
n = m.node_info
n.num = 0xA1B2C3D4
n.user.id = "!a1b2c3d4"
n.user.long_name = "Hilltop Relay"
n.user.short_name = "HILL"
n.user.hw_model = mesh_pb2.HELTEC_V3
n.user.role = config_pb2.Config.DeviceConfig.ROUTER
n.user.public_key = bytes(range(32))
n.user.is_unmessagable = True
n.position.latitude_i = -338688000
n.position.longitude_i = 1512093000
n.position.altitude = -12
n.position.time = 1795000000
n.position.location_source = mesh_pb2.Position.LOC_INTERNAL
n.position.ground_speed = 0
n.position.ground_track = 9000000
n.position.sats_in_view = 11
n.position.precision_bits = 13
n.snr = -7.25
n.last_heard = 1795000100
n.device_metrics.battery_level = 101
n.device_metrics.voltage = 4.12
n.device_metrics.channel_utilization = 11.5
n.device_metrics.air_util_tx = 0.75
n.device_metrics.uptime_seconds = 86400
n.channel = 1
n.via_mqtt = True
n.hops_away = 0
n.is_favorite = True
n.is_ignored = True
n.is_muted = True
add("from_node_info", m)

m = mesh_pb2.FromRadio()
m.metadata.firmware_version = "2.7.26.61"
m.metadata.device_state_version = 24
m.metadata.canShutdown = True
m.metadata.hasWifi = True
m.metadata.hasBluetooth = True
m.metadata.hasEthernet = True
m.metadata.role = config_pb2.Config.DeviceConfig.CLIENT_MUTE
m.metadata.hw_model = mesh_pb2.PORTDUINO
m.metadata.hasPKC = True
add("from_metadata", m)

m = mesh_pb2.FromRadio()
m.channel.index = 0
m.channel.role = channel_pb2.Channel.PRIMARY
m.channel.settings.psk = b"\x01"
add("from_channel_primary", m)

m = mesh_pb2.FromRadio()
c = m.channel
c.index = 2
c.role = channel_pb2.Channel.SECONDARY
c.settings.psk = bytes(range(16, 48))
c.settings.name = "Friends"
c.settings.id = 0xDEADBEEF
c.settings.uplink_enabled = True
c.settings.downlink_enabled = True
c.settings.module_settings.position_precision = 32
c.settings.module_settings.is_muted = True
add("from_channel_secondary", m)

m = mesh_pb2.FromRadio()
m.channel.index = 5
add("from_channel_disabled", m)

m = mesh_pb2.FromRadio()
l = m.config.lora
l.use_preset = True
l.modem_preset = config_pb2.Config.LoRaConfig.MEDIUM_FAST
l.bandwidth = 250
l.spread_factor = 9
l.coding_rate = 5
l.frequency_offset = 1.5
l.region = config_pb2.Config.LoRaConfig.EU_868
l.hop_limit = 3
l.tx_enabled = True
l.tx_power = 22
l.channel_num = 4
l.override_duty_cycle = True
l.sx126x_rx_boosted_gain = True
l.override_frequency = 869.525
l.ignore_incoming.extend([0x11111111, 0x22222222])
l.ignore_mqtt = True
l.config_ok_to_mqtt = True
add("from_config_lora", m)

m = mesh_pb2.FromRadio()
m.config.lora.SetInParent()
add("from_config_lora_empty", m)

m = mesh_pb2.FromRadio()
d = m.config.device
d.role = config_pb2.Config.DeviceConfig.CLIENT_MUTE
d.button_gpio = 4
d.rebroadcast_mode = config_pb2.Config.DeviceConfig.LOCAL_ONLY
d.node_info_broadcast_secs = 10800
d.tzdef = "CET-1CEST,M3.5.0,M10.5.0/3"
d.led_heartbeat_disabled = True
add("from_config_device", m)

m = mesh_pb2.FromRadio()
p = m.config.position
p.position_broadcast_secs = 900
p.position_broadcast_smart_enabled = True
p.fixed_position = True
p.gps_update_interval = 120
p.position_flags = 811
p.gps_mode = config_pb2.Config.PositionConfig.NOT_PRESENT
add("from_config_position", m)

m = mesh_pb2.FromRadio()
m.config_complete_id = 0x1234ABCD
add("from_config_complete", m)

m = mesh_pb2.FromRadio()
m.rebooted = True
add("from_rebooted", m)

# --- FromRadio: live traffic
m = mesh_pb2.FromRadio()
m.id = 99
p = m.packet
setattr(p, "from", 0xA1B2C3D4)
p.to = 0xCAFE0001
p.channel = 2
p.id = 0x55667788
p.rx_time = 1795000200
p.rx_snr = -7.25
p.rx_rssi = -101
p.hop_limit = 1
p.hop_start = 3
p.want_ack = True
p.priority = mesh_pb2.MeshPacket.RELIABLE
p.via_mqtt = True
p.pki_encrypted = True
p.public_key = bytes(range(32))
p.next_hop = 0xD4
p.relay_node = 0x7F
p.decoded.portnum = portnums_pb2.TEXT_MESSAGE_APP
p.decoded.payload = "On my way, 10 min".encode()
p.decoded.want_response = True
p.decoded.dest = 0xCAFE0001
p.decoded.source = 0xA1B2C3D4
p.decoded.request_id = 0x01020304
p.decoded.reply_id = 0x0A0B0C0D
p.decoded.emoji = 1
p.decoded.bitfield = 1
add("from_text", m)

m = mesh_pb2.FromRadio()
p = m.packet
setattr(p, "from", 0xA1B2C3D4)
p.to = 0xFFFFFFFF
p.id = 0x1000
p.encrypted = b"\x01\x02\x03\x04\x05"
add("from_encrypted", m)


def routing_packet(err, frm):
    m = mesh_pb2.FromRadio()
    p = m.packet
    setattr(p, "from", frm)
    p.to = 0xCAFE0001
    p.id = 0x2000
    p.decoded.portnum = portnums_pb2.ROUTING_APP
    r = mesh_pb2.Routing()
    r.error_reason = err
    p.decoded.payload = r.SerializeToString()
    p.decoded.request_id = 0x11223344
    return m


add("from_ack", routing_packet(mesh_pb2.Routing.NONE, 0xA1B2C3D4))
add("from_nak", routing_packet(mesh_pb2.Routing.MAX_RETRANSMIT, 0xCAFE0001))

r = mesh_pb2.Routing()
r.error_reason = mesh_pb2.Routing.NONE
add("routing_none", r)
r = mesh_pb2.Routing()
r.error_reason = mesh_pb2.Routing.PKI_UNKNOWN_PUBKEY
add("routing_pki", r)

r = mesh_pb2.RouteDiscovery()
r.route.extend([0x0000AAAA, 0xFFFFFFFF])
r.snr_towards.extend([-20, 12, -128])
r.route_back.extend([0x0000BBBB])
r.snr_back.extend([5, 6])
add("route_discovery", r)
add("route_discovery_empty", mesh_pb2.RouteDiscovery())

t = telemetry_pb2.Telemetry()
t.time = 1795000300
t.device_metrics.battery_level = 87
t.device_metrics.voltage = 3.98
t.device_metrics.channel_utilization = 4.5
t.device_metrics.air_util_tx = 0.25
t.device_metrics.uptime_seconds = 3600
add("telemetry_device", t)

t = telemetry_pb2.Telemetry()
t.time = 1795000301
t.environment_metrics.temperature = -3.5
t.environment_metrics.relative_humidity = 71.0
t.environment_metrics.barometric_pressure = 1013.25
t.environment_metrics.gas_resistance = 12.0
t.environment_metrics.iaq = 42
add("telemetry_environment", t)

t = telemetry_pb2.Telemetry()
t.time = 1795000302
s = t.local_stats
s.uptime_seconds = 7200
s.channel_utilization = 9.5
s.air_util_tx = 1.25
s.num_packets_tx = 40
s.num_packets_rx = 900
s.num_packets_rx_bad = 3
s.num_online_nodes = 7
s.num_total_nodes = 31
s.num_rx_dupe = 120
s.num_tx_relay = 15
s.num_tx_relay_canceled = 6
s.noise_floor = -113
add("telemetry_local", t)

# A request: the variant wanted, empty.
t = telemetry_pb2.Telemetry()
t.device_metrics.SetInParent()
add("telemetry_request_device", t)
t = telemetry_pb2.Telemetry()
t.environment_metrics.SetInParent()
add("telemetry_request_environment", t)

p = mesh_pb2.Position()
p.time = 1795000400
add("position_time_only", p)

p = mesh_pb2.Position()
p.latitude_i = 515074000
p.longitude_i = -1278000
p.altitude = 35
p.time = 1795000401
p.location_source = mesh_pb2.Position.LOC_EXTERNAL
p.ground_speed = 2
p.ground_track = 12300000
p.sats_in_view = 9
add("position_full", p)

w = mesh_pb2.Waypoint()
w.id = 0x00C0FFEE
w.latitude_i = 515074000
w.longitude_i = -1278000
w.expire = 1795003600
w.name = "Camp"
w.description = "Water here"
add("waypoint", w)

u = mesh_pb2.User()
u.id = "!cafe0001"
u.long_name = "Cardputer Zero"
u.short_name = "ZERO"
u.hw_model = mesh_pb2.PORTDUINO
u.is_licensed = True
u.public_key = bytes(range(32, 64))
add("user", u)

m = mesh_pb2.FromRadio()
m.queueStatus.res = 34
m.queueStatus.free = 15
m.queueStatus.maxlen = 16
m.queueStatus.mesh_packet_id = 0x11223344
add("from_queue_status", m)

m = mesh_pb2.FromRadio()
m.clientNotification.reply_id = 0x11223344
m.clientNotification.time = 1795000500
m.clientNotification.level = mesh_pb2.LogRecord.WARNING
m.clientNotification.message = "TraceRoute can only be sent once every 30 seconds"
add("from_notification", m)

m = mesh_pb2.FromRadio()
m.log_record.message = "boot"
add("from_log_record", m)

# --- Admin


def admin(name, fill):
    a = admin_pb2.AdminMessage()
    fill(a)
    add("admin_" + name, a)


admin("get_owner_request", lambda a: setattr(a, "get_owner_request", True))
admin("get_channel_request", lambda a: setattr(a, "get_channel_request", 3))
admin("get_config_request_device", lambda a: setattr(a, "get_config_request", admin_pb2.AdminMessage.DEVICE_CONFIG))
admin("get_config_request_lora", lambda a: setattr(a, "get_config_request", admin_pb2.AdminMessage.LORA_CONFIG))
admin("begin_edit", lambda a: setattr(a, "begin_edit_settings", True))
admin("commit_edit", lambda a: setattr(a, "commit_edit_settings", True))
admin("reboot", lambda a: setattr(a, "reboot_seconds", 5))
admin("shutdown", lambda a: setattr(a, "shutdown_seconds", 2))
admin("nodedb_reset", lambda a: setattr(a, "nodedb_reset", True))
admin("set_favorite", lambda a: setattr(a, "set_favorite_node", 0xA1B2C3D4))
admin("remove_favorite", lambda a: setattr(a, "remove_favorite_node", 0xA1B2C3D4))
admin("set_ignored", lambda a: setattr(a, "set_ignored_node", 0xA1B2C3D4))
admin("remove_ignored", lambda a: setattr(a, "remove_ignored_node", 0xA1B2C3D4))
admin("remove_node", lambda a: setattr(a, "remove_by_nodenum", 0xA1B2C3D4))
admin("remove_fixed_position", lambda a: setattr(a, "remove_fixed_position", True))


def set_owner(a):
    a.set_owner.long_name = "Trail Runner"
    a.set_owner.short_name = "TRUN"


admin("set_owner", set_owner)


def set_lora(a):
    l = a.set_config.lora
    l.use_preset = True
    l.modem_preset = config_pb2.Config.LoRaConfig.LONG_FAST
    l.region = config_pb2.Config.LoRaConfig.US
    l.hop_limit = 3
    l.tx_enabled = True
    l.tx_power = 30
    l.sx126x_rx_boosted_gain = True


admin("set_config_lora", set_lora)


def set_channel(a):
    a.set_channel.index = 1
    a.set_channel.role = channel_pb2.Channel.SECONDARY
    a.set_channel.settings.name = "Hikers"
    a.set_channel.settings.psk = bytes(range(16))


admin("set_channel", set_channel)


def del_channel(a):
    a.set_channel.index = 3
    a.set_channel.role = channel_pb2.Channel.DISABLED


admin("set_channel_disabled", del_channel)


def fixed(a):
    a.set_fixed_position.latitude_i = 515074000
    a.set_fixed_position.longitude_i = -1278000
    a.set_fixed_position.altitude = 35


admin("set_fixed_position", fixed)


def owner_response(a):
    a.get_owner_response.id = "!cafe0001"
    a.get_owner_response.long_name = "Cardputer Zero"
    a.get_owner_response.short_name = "ZERO"
    a.session_passkey = b"12345678"


admin("get_owner_response", owner_response)

# --- Channel links
cs = apponly_pb2.ChannelSet()
s = cs.settings.add()
s.psk = b"\x01"
s = cs.settings.add()
s.psk = bytes(range(16))
s.name = "Hikers"
s.uplink_enabled = True
cs.lora_config.use_preset = True
cs.lora_config.modem_preset = config_pb2.Config.LoRaConfig.LONG_FAST
cs.lora_config.region = config_pb2.Config.LoRaConfig.EU_868
cs.lora_config.hop_limit = 3
cs.lora_config.tx_enabled = True
add("channel_set", cs)
URL = "https://meshtastic.org/e/#" + base64.urlsafe_b64encode(cs.SerializeToString()).decode().rstrip("=")

print("// Made by make_vectors.py from meshtastic/protobufs with Google's protobuf")
print("// library. Don't edit: change make_vectors.py and run it again.")
print("#pragma once")
print("#include <string>")
print()
print("namespace vectors {")
for name, data in V:
    body = "".join("\\x%02x" % b for b in data)
    print('const std::string %s("%s", %d);' % (name, body, len(data)))
print('const char* const channel_url = "%s";' % URL)
print("}  // namespace vectors")
