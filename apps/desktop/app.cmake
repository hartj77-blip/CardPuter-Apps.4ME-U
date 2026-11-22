# Desktop (README.md): remote desktop (VNC and RDP) on the built-in screen,
# or on the monitor in Dock mode.
zs_app(desktop "Desktop" ZS_ICON_LAPTOP 0x5e5c64 make_desktop_app ORDER 25
       SOURCES desktop.cpp hosts.cpp)

zs_app_test(desktop_hosts_test SOURCES tests/hosts_test.cpp hosts.cpp)

# The connection itself runs in its own process, zeroos-desktop-view
# (view/view.cpp), linked with libvncclient and FreeRDP 3. The package needs
# it; the simulator and screenshot builds use it when the libraries are
# there (apt install libvncserver-dev freerdp3-dev), and otherwise show the
# app without connecting.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(ZS_DESKTOP_LIBS QUIET IMPORTED_TARGET libvncclient freerdp3 winpr3)
endif()
if(ZS_DESKTOP_LIBS_FOUND)
  add_executable(zeroos-desktop-view
    ${CMAKE_CURRENT_LIST_DIR}/view/view.cpp ${CMAKE_CURRENT_LIST_DIR}/hosts.cpp)
  set_target_properties(zeroos-desktop-view PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  target_include_directories(zeroos-desktop-view PRIVATE ${CMAKE_SOURCE_DIR})
  target_compile_options(zeroos-desktop-view PRIVATE -Wall -Wextra -Wno-unused-parameter
                         -Wno-deprecated-declarations)
  target_link_libraries(zeroos-desktop-view PRIVATE PkgConfig::ZS_DESKTOP_LIBS)
  # Outside the device, the app finds it in the build tree.
  if(NOT ZS_BACKEND STREQUAL "device")
    set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/desktop.cpp PROPERTIES
      COMPILE_DEFINITIONS "ZS_DESKTOP_VIEW_DEV=\"${CMAKE_BINARY_DIR}/bin/zeroos-desktop-view\"")
  endif()
elseif(ZS_BACKEND STREQUAL "device")
  message(WARNING "Desktop: libvncclient, freerdp3 or winpr3 not found, so zeroos-desktop-view "
                  "won't be built and the desktop package can't be made. "
                  "apt install libvncserver-dev freerdp3-dev")
endif()
