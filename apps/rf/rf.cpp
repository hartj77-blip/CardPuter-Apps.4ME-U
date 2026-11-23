// RF Tools (README.md): one app with nine tools for an RTL-SDR dongle, which
// share one connection to the radio daemon. It opens on a menu of the tools
// (1 to 9 pick one); Esc in a tool comes back to the menu.
#include "apps/kit/suite.h"

namespace zs {

std::unique_ptr<App> make_spectrum_app();
std::unique_ptr<App> make_scanner_app();
std::unique_ptr<App> make_sensors_app();
std::unique_ptr<App> make_aircraft_app();
std::unique_ptr<App> make_aprs_app();
std::unique_ptr<App> make_ft8_app();
std::unique_ptr<App> make_logbook_app();
std::unique_ptr<App> make_sdr_server_app();
std::unique_ptr<App> make_station_app();

std::unique_ptr<App> make_rf_app() {
    return std::make_unique<kit::Suite>("rf", std::vector<kit::Tool>{
        {"spectrum",  "Spectrum",   LV_SYMBOL_BARS,     "see and listen",           make_spectrum_app},
        {"scanner",   "Scanner",    LV_SYMBOL_REFRESH,  "find busy channels",       make_scanner_app},
        {"sensors",   "Sensors",    LV_SYMBOL_TINT,     "weather stations, 433 MHz", make_sensors_app},
        {"aircraft",  "Aircraft",   LV_SYMBOL_GPS,      "planes overhead, ADS-B",   make_aircraft_app},
        {"aprs",      "APRS",       LV_SYMBOL_ENVELOPE, "ham positions, messages",  make_aprs_app},
        {"ft8",       "FT8",        LV_SYMBOL_SHUFFLE,  "decode FT8",               make_ft8_app},
        {"logbook",   "Logbook",    LV_SYMBOL_LIST,     "your contacts",            make_logbook_app},
        {"sdrserver", "SDR Server", LV_SYMBOL_UPLOAD,   "share the radio",          make_sdr_server_app},
        {"station",   "Station",    LV_SYMBOL_HOME,     "callsign, rig, settings",  make_station_app},
    });
}

}  // namespace zs
