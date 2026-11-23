# Net Tools (README.md): one app, module "net", with seven tools that drive
# the standard Linux network tools: Discover, Ports, Trace, DNS, Capture,
# Netinfo and Share. net.cpp lists them; the menu is apps/kit/suite.h.
zs_app(net "Net Tools" ZS_ICON_GLOBE 0x0aa5d6 make_net_app ORDER 220
       SOURCES net.cpp discover.cpp ports.cpp trace.cpp dns.cpp capture.cpp netinfo.cpp share.cpp
               net_run.cpp net_sim.cpp net_ui.cpp)
