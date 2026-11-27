# Maps (README.md): an offline moving map. The map itself is a .zmap file
# built from OpenStreetMap data by tools/zmap_build.cpp (prepare.sh runs it
# when the package is made; maps.pkg packs the result).
zs_app(maps "Maps" ZS_ICON_GLOBE 0x2e8b57 make_maps_app ORDER 135
       SOURCES maps.cpp render.cpp zmap.cpp)

# The map builder, for the machine that makes the package (it needs zlib,
# and far more memory than a Cardputer has). Built into the build's bin/.
if(NOT CMAKE_CROSSCOMPILING AND NOT ZS_BACKEND STREQUAL "web")
  find_package(ZLIB)
  if(ZLIB_FOUND)
    add_executable(zmap-build ${CMAKE_CURRENT_LIST_DIR}/tools/zmap_build.cpp)
    target_compile_options(zmap-build PRIVATE -Wall -Wextra)
    target_link_libraries(zmap-build PRIVATE ZLIB::ZLIB)
    set_target_properties(zmap-build PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # The builder on a made-up town (tests/make-town.py), then the reader
    # and the renderer on the map it wrote.
    add_executable(maps_test ${CMAKE_CURRENT_LIST_DIR}/tests/maps_test.cpp
                   ${CMAKE_CURRENT_LIST_DIR}/render.cpp ${CMAKE_CURRENT_LIST_DIR}/zmap.cpp)
    target_include_directories(maps_test PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_options(maps_test PRIVATE -Wall -Wextra)
    add_test(NAME maps_test COMMAND ${CMAKE_CURRENT_LIST_DIR}/tests/maps_check.sh
             $<TARGET_FILE:zmap-build> $<TARGET_FILE:maps_test>)
  endif()
endif()
