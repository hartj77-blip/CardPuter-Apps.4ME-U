# Games: plays games for classic consoles with libretro cores, on the
# built-in screen.
zs_app(games "Games" LV_SYMBOL_PLAY 0xd9480f make_games_app ORDER 400
       SOURCES games.cpp emu.cpp pad.cpp systems.cpp ../kit/run.cpp ../kit/ui.cpp)

# A test-pattern libretro core (tests/testcore.c): with
# ZS_GAMES_CORES=<build>/test-cores the app plays ~/Games/test/*.zstest.
if(NOT CMAKE_CROSSCOMPILING)
  add_library(zstest_libretro MODULE ${CMAKE_CURRENT_LIST_DIR}/tests/testcore.c)
  set_target_properties(zstest_libretro PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
                        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-cores")
  target_link_libraries(zstest_libretro PRIVATE m)
endif()
