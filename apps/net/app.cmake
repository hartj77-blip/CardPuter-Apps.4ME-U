# Net Tools (README.md): one pack, module "net", seven apps that drive the
# standard Linux network tools. The shared code (net_run, net_sim, net_ui)
# is listed once, on the first app.
zs_app(discover "Discover"  ZS_ICON_SEARCH     0x0aa5d6 make_discover_app MODULE net ORDER 220
       SOURCES discover.cpp net_run.cpp net_sim.cpp net_ui.cpp)
zs_app(ports    "Ports"     ZS_ICON_CHIP       0x7c4dff make_ports_app    MODULE net ORDER 230
       SOURCES ports.cpp)
zs_app(trace    "Trace"     ZS_ICON_SATELLITE  0xe3b341 make_trace_app    MODULE net ORDER 240
       SOURCES trace.cpp)
zs_app(dns      "DNS"       ZS_ICON_GLOBE      0x00a884 make_dns_app      MODULE net ORDER 250
       SOURCES dns.cpp)
zs_app(capture  "Capture"   LV_SYMBOL_DOWNLOAD 0xd6336c make_capture_app  MODULE net ORDER 260
       SOURCES capture.cpp)
zs_app(netinfo  "Netinfo"   ZS_ICON_NETWORK    0x5e6c7b make_netinfo_app  MODULE net ORDER 270
       SOURCES netinfo.cpp)
zs_app(share    "Share"     ZS_ICON_LAPTOP     0x26a269 make_share_app    MODULE net ORDER 280
       SOURCES share.cpp)
