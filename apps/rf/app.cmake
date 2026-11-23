# RF Tools (README.md): nine apps for an RTL-SDR dongle in one module, rf.so,
# since they share one connection to the radio daemon. core/ is the radio
# side: the daemon zrfd, its client library, and stand-ins for a simulated
# radio. Its programs are built into the build's bin/; rf.pkg packs them.
set(ZRF_SAVED_PIC ${CMAKE_POSITION_INDEPENDENT_CODE})
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # zrfclient goes into rf.so
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/core ${CMAKE_BINARY_DIR}/rf)
set(CMAKE_POSITION_INDEPENDENT_CODE ${ZRF_SAVED_PIC})
set_target_properties(zrfd zrf zrf-sim-sensors zrf-sim-aircraft zrf-sim-aprs PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# After the other apps on the home screen.
zs_app(spectrum "Spectrum" LV_SYMBOL_BARS 0x39c5cf make_spectrum_app MODULE rf ORDER 500
       SOURCES spectrum.cpp LIBS zrfclient)
zs_app(scanner "Scanner" LV_SYMBOL_REFRESH 0x3fb950 make_scanner_app MODULE rf ORDER 510
       SOURCES scanner.cpp LIBS zrfclient)
zs_app(sensors "Sensors" LV_SYMBOL_TINT 0x58a6ff make_sensors_app MODULE rf ORDER 520
       SOURCES sensors.cpp LIBS zrfclient)
zs_app(aircraft "Aircraft" LV_SYMBOL_GPS 0xe3b341 make_aircraft_app MODULE rf ORDER 530
       SOURCES aircraft.cpp LIBS zrfclient)
zs_app(aprs "APRS" LV_SYMBOL_ENVELOPE 0xff7eb6 make_aprs_app MODULE rf ORDER 540
       SOURCES aprs_app.cpp LIBS zrfclient)
zs_app(ft8 "FT8" LV_SYMBOL_SHUFFLE 0x3fb950 make_ft8_app MODULE rf ORDER 550
       SOURCES ft8_app.cpp LIBS zrfclient)
zs_app(logbook "Logbook" LV_SYMBOL_LIST 0xe3b341 make_logbook_app MODULE rf ORDER 560
       SOURCES logbook.cpp LIBS zrfclient)
zs_app(sdrserver "SDR Server" LV_SYMBOL_UPLOAD 0xa371f7 make_sdr_server_app MODULE rf ORDER 570
       SOURCES sdr_server.cpp LIBS zrfclient)
zs_app(station "Station" LV_SYMBOL_HOME 0xff8a1f make_station_app MODULE rf ORDER 580
       SOURCES station.cpp LIBS zrfclient)
