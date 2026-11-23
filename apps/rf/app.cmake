# RF Tools (README.md): one app, rf.so, with nine tools for an RTL-SDR
# dongle, which share one connection to the radio daemon. core/ is the radio
# side: the daemon zrfd, its client library, and stand-ins for a simulated
# radio. Its programs are built into the build's bin/; rf.pkg packs them.
set(ZRF_SAVED_PIC ${CMAKE_POSITION_INDEPENDENT_CODE})
set(CMAKE_POSITION_INDEPENDENT_CODE ON)   # zrfclient goes into rf.so
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/core ${CMAKE_BINARY_DIR}/rf)
set(CMAKE_POSITION_INDEPENDENT_CODE ${ZRF_SAVED_PIC})
set_target_properties(zrfd zrf zrf-sim-sensors zrf-sim-aircraft zrf-sim-aprs PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# One app on the home screen, after the others, with the nine tools inside
# (rf.cpp lists them; the menu is apps/kit/suite.h).
zs_app(rf "RF Tools" ZS_ICON_SATELLITE 0x39c5cf make_rf_app ORDER 500
       SOURCES rf.cpp spectrum.cpp scanner.cpp sensors.cpp aircraft.cpp aprs_app.cpp ft8_app.cpp
               logbook.cpp sdr_server.cpp station.cpp
       LIBS zrfclient)
