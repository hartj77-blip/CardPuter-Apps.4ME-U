zs_app(car "Car" ZS_ICON_CAR 0x1e88a8 make_car_app ORDER 90
       SOURCES car.cpp)

# Reading a phone's player from busctl and bluetoothctl output (no Bluetooth).
zs_app_test(car_phone_test SOURCES tests/phone_player_test.cpp)

# USB live sound: zeroos-car-live serves a made-up USB stick whose songs are
# the sound playing now (live/main.cpp). Linux only: it speaks to /dev/fuse
# and /dev/uinput. car.pkg packs it from the build's bin/.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Threads REQUIRED)
  add_executable(zeroos-car-live ${CMAKE_CURRENT_LIST_DIR}/live/main.cpp)
  set_target_properties(zeroos-car-live PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  target_compile_options(zeroos-car-live PRIVATE -Wall -Wextra)
  target_link_libraries(zeroos-car-live PRIVATE Threads::Threads)
endif()
# The stick itself: its layout, its songs, and the car's buttons read from
# how it reads (no USB). tests/car-live-test.sh runs the program for real.
zs_app_test(car_live_stick_test SOURCES tests/live_stick_test.cpp)
