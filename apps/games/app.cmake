# Games: plays games for classic consoles with libretro cores, on the
# built-in screen.
zs_app(games "Games" LV_SYMBOL_PLAY 0xd9480f make_games_app ORDER 400
       SOURCES games.cpp emu.cpp pad.cpp tv.cpp systems.cpp border.cpp ../kit/run.cpp ../kit/ui.cpp)

# A test-pattern libretro core (tests/testcore.c): with
# ZS_GAMES_CORES=<build>/test-cores the app plays ~/Games/test/*.zstest.
if(NOT CMAKE_CROSSCOMPILING)
  add_library(zstest_libretro MODULE ${CMAKE_CURRENT_LIST_DIR}/tests/testcore.c)
  set_target_properties(zstest_libretro PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
                        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-cores")
  target_link_libraries(zstest_libretro PRIVATE m)
endif()

# The borders around the picture (no screen needed).
zs_app_test(games_border_test SOURCES tests/border_test.cpp border.cpp)
# Players (which gamepad is which player) and the TV picture's place (no devices needed).
zs_app_test(games_players_tv_test SOURCES tests/players_tv_test.cpp pad.cpp tv.cpp)
if(TARGET games_players_tv_test)
  # lvgl.h and its settings; the test has its own lv_tick_get.
  target_include_directories(games_players_tv_test PRIVATE $<TARGET_PROPERTY:lvgl,INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_definitions(games_players_tv_test PRIVATE $<TARGET_PROPERTY:lvgl,INTERFACE_COMPILE_DEFINITIONS>)
endif()
