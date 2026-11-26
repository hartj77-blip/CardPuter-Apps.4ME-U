# stb_image (public domain / MIT): decodes the covers the server sends (art.cpp).
include(FetchContent)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 6b5799bae8a1fe3792691ef4b72ae26434c3e799
  SOURCE_SUBDIR none)
FetchContent_MakeAvailable(stb)
# What needs no screen (the server's replies, the pictures), on its own, so
# the app (a module on the device) and the tests share it.
add_library(jellyfin_core STATIC ${CMAKE_CURRENT_LIST_DIR}/api.cpp ${CMAKE_CURRENT_LIST_DIR}/art.cpp)
target_include_directories(jellyfin_core PUBLIC ${CMAKE_SOURCE_DIR} PRIVATE ${stb_SOURCE_DIR})
target_compile_options(jellyfin_core PRIVATE -Wall -Wextra)
set_target_properties(jellyfin_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

zs_app(jellyfin "Jellyfin" LV_SYMBOL_VIDEO 0x00a4dc make_jellyfin_app ORDER 72
       SOURCES jellyfin.cpp ui.cpp LIBS jellyfin_core)

# The server's replies and the addresses asked for (no network, no screen).
zs_app_test(jellyfin_api_test SOURCES tests/api_test.cpp)
# Covers: decoding, cropping to a card, the files they are kept in.
zs_app_test(jellyfin_art_test SOURCES tests/art_test.cpp)
foreach(_t jellyfin_api_test jellyfin_art_test)
  if(TARGET ${_t})
    target_link_libraries(${_t} PRIVATE jellyfin_core)
  endif()
endforeach()
# The background threads: what runs first, what is dropped, stopping.
zs_app_test(jellyfin_work_test SOURCES tests/work_test.cpp)
if(TARGET jellyfin_work_test)
  target_link_libraries(jellyfin_work_test PRIVATE pthread)
endif()
