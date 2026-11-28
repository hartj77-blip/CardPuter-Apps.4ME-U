# Mesh (README.md): a Meshtastic client for the Cap LoRa-1262. core/ is
# everything that needs no screen: the protocol, the message history, the
# client, a simulated radio, and zeroos-meshd, the daemon that shares
# meshtasticd's one API connection (core/hub.h). The daemon is built into the
# build's bin/; mesh.pkg packs it.
set(MESH_CORE
  ${CMAKE_CURRENT_LIST_DIR}/core/proto.cpp ${CMAKE_CURRENT_LIST_DIR}/core/store.cpp
  ${CMAKE_CURRENT_LIST_DIR}/core/text.cpp ${CMAKE_CURRENT_LIST_DIR}/core/recorder.cpp
  ${CMAKE_CURRENT_LIST_DIR}/core/client.cpp ${CMAKE_CURRENT_LIST_DIR}/core/fake_radio.cpp
  ${CMAKE_CURRENT_LIST_DIR}/core/session.cpp)
add_library(mesh_core STATIC ${MESH_CORE})
target_include_directories(mesh_core PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(mesh_core PRIVATE -Wall -Wextra)
set_target_properties(mesh_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(zeroos-meshd ${CMAKE_CURRENT_LIST_DIR}/core/meshd.cpp ${CMAKE_CURRENT_LIST_DIR}/core/hub.cpp)
target_link_libraries(zeroos-meshd PRIVATE mesh_core)
target_compile_options(zeroos-meshd PRIVATE -Wall -Wextra)
set_target_properties(zeroos-meshd PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

zs_app(mesh "Mesh" ZS_ICON_CHAT 0x12a182 make_mesh_app ORDER 140
       SOURCES mesh.cpp chats.cpp nodes.cpp radio.cpp
       LIBS mesh_core)

# The codec against Meshtastic's own definitions (tests/vectors.h).
zs_app_test(mesh_proto_test SOURCES tests/proto_test.cpp)
# The client against the simulated radio.
zs_app_test(mesh_client_test SOURCES tests/client_test.cpp)
# The daemon end to end, over sockets on this machine.
zs_app_test(mesh_hub_test SOURCES tests/hub_test.cpp core/hub.cpp)
foreach(_t mesh_proto_test mesh_client_test mesh_hub_test)
  if(TARGET ${_t})
    target_link_libraries(${_t} PRIVATE mesh_core)
  endif()
endforeach()
