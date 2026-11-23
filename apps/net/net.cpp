// Net Tools (README.md): one app with seven tools, front-ends for the
// standard Linux network tools. It opens on a menu of the tools (1 to 7 pick
// one); Esc in a tool comes back to the menu.
#include "apps/kit/suite.h"

namespace zs {

std::unique_ptr<App> make_discover_app();
std::unique_ptr<App> make_ports_app();
std::unique_ptr<App> make_trace_app();
std::unique_ptr<App> make_dns_app();
std::unique_ptr<App> make_capture_app();
std::unique_ptr<App> make_netinfo_app();
std::unique_ptr<App> make_share_app();

std::unique_ptr<App> make_net_app() {
    return std::make_unique<kit::Suite>("net", std::vector<kit::Tool>{
        {"discover", "Discover", ZS_ICON_SEARCH,     "devices on the network", make_discover_app},
        {"ports",    "Ports",    ZS_ICON_CHIP,       "open ports on a host",   make_ports_app},
        {"trace",    "Trace",    ZS_ICON_PIN,        "route to a host",        make_trace_app},
        {"dns",      "DNS",      ZS_ICON_GLOBE,      "look up a name",         make_dns_app},
        {"capture",  "Capture",  LV_SYMBOL_DOWNLOAD, "record packets",         make_capture_app},
        {"netinfo",  "Netinfo",  ZS_ICON_NETWORK,    "interfaces and ports",   make_netinfo_app},
        {"share",    "Share",    ZS_ICON_LAPTOP,     "Ethernet for a laptop",  make_share_app},
    });
}

}  // namespace zs
