# Maps (README.md): an offline moving map with directions. The map is a
# .zmap file built from OpenStreetMap data by tools/zmap_build.cpp, with a
# search index, the house numbers and a routing map for Navit beside it
# (prepare.sh makes all four when the package is made; maps.pkg packs them).
zs_app(maps "Maps" ZS_ICON_GLOBE 0x2e8b57 make_maps_app ORDER 135
       SOURCES maps.cpp render.cpp zmap.cpp nav.cpp nav_parse.cpp search.cpp addr.cpp)

# The map builder, for the machine that makes the package (it needs zlib,
# and far more memory than a Cardputer has). Built into the build's bin/.
if(NOT CMAKE_CROSSCOMPILING AND NOT ZS_BACKEND STREQUAL "web")
  find_package(ZLIB)
  if(ZLIB_FOUND)
    add_executable(zmap-build ${CMAKE_CURRENT_LIST_DIR}/tools/zmap_build.cpp)
    target_compile_options(zmap-build PRIVATE -Wall -Wextra)
    target_link_libraries(zmap-build PRIVATE ZLIB::ZLIB)
    set_target_properties(zmap-build PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # The builder on a made-up town (tests/make-town.py), then the reader,
    # the renderer, Navit's answers and the search on what it wrote, and a
    # real route where Navit is installed (tests/maps_check.sh).
    set(_m ${CMAKE_CURRENT_LIST_DIR})
    add_executable(maps_test ${_m}/tests/maps_test.cpp ${_m}/render.cpp ${_m}/zmap.cpp)
    add_executable(nav_test ${_m}/tests/nav_test.cpp ${_m}/nav.cpp ${_m}/nav_parse.cpp ${_m}/search.cpp ${_m}/addr.cpp)
    add_executable(navit_test ${_m}/tests/navit_test.cpp ${_m}/nav.cpp ${_m}/nav_parse.cpp)
    foreach(_t maps_test nav_test navit_test)
      target_include_directories(${_t} PRIVATE ${CMAKE_SOURCE_DIR})
      target_compile_options(${_t} PRIVATE -Wall -Wextra)
      target_link_libraries(${_t} PRIVATE pthread)
    endforeach()
    add_test(NAME maps_test COMMAND ${_m}/tests/maps_check.sh $<TARGET_FILE:zmap-build>
             $<TARGET_FILE:maps_test> $<TARGET_FILE:nav_test> $<TARGET_FILE:navit_test>)
  endif()
endif()
