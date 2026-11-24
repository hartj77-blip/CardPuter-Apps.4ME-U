# stb_image (public domain / MIT): decodes the pictures on pages (picture.cpp).
include(FetchContent)
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 6b5799bae8a1fe3792691ef4b72ae26434c3e799
  SOURCE_SUBDIR none)
FetchContent_MakeAvailable(stb)
# picture.cpp on its own, so the app (a module on the device) and the test share it.
add_library(browser_picture STATIC ${CMAKE_CURRENT_LIST_DIR}/picture.cpp)
target_include_directories(browser_picture PUBLIC ${CMAKE_SOURCE_DIR} PRIVATE ${stb_SOURCE_DIR})
set_target_properties(browser_picture PROPERTIES POSITION_INDEPENDENT_CODE ON)

zs_app(browser "Browser" ZS_ICON_GLOBE 0x3584e4 make_browser_app ORDER 65
       SOURCES browser.cpp page.cpp LIBS browser_picture)

# The page engine: Markdown, HTML, forms, layout, addresses and pictures (no network, no LVGL).
zs_app_test(browser_test SOURCES tests/page_test.cpp page.cpp)
if(TARGET browser_test)
  target_link_libraries(browser_test PRIVATE browser_picture)
endif()
