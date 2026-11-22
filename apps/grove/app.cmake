# Grove (README.md): things on the Grove port. For now a NeoPixel LED strip,
# lit by zeroos-grove-ledd, a small root service, through SMI and DMA.
zs_app(grove "Grove" LV_SYMBOL_CHARGE 0xd4a017 make_grove_app ORDER 75
       SOURCES grove.cpp)

# The LED strip service, built next to the modules; grove.pkg packs it
# (tools/build.sh sets ZEROOS_APP_BIN).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(zeroos-grove-ledd ${CMAKE_CURRENT_LIST_DIR}/ledd/main.cpp
                 ${CMAKE_CURRENT_LIST_DIR}/ledd/smi_ws2812.cpp)
  target_compile_options(zeroos-grove-ledd PRIVATE -Wall -Wextra)
  set_target_properties(zeroos-grove-ledd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/apps)
endif()

# The effects and settings line (no screen), and the service with --dry-run.
zs_app_test(leds_test SOURCES tests/leds_test.cpp)
if(NOT CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME ledd_test COMMAND ${CMAKE_CURRENT_LIST_DIR}/tests/ledd_check.sh $<TARGET_FILE:zeroos-grove-ledd>)
endif()
