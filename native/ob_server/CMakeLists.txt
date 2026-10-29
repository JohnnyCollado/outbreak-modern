# SPDX-License-Identifier: AGPL-3.0-or-later
# ob_server: the server built into the app for same-network play (docs/plans/2026-10-27-ob-server.md): login pages,
# lobby and relay for Biohazard Outbreak File #1 (protocol: research/2026-10-27-lobby-protocol.md). AGPL-3.0, own
# licence (LICENSE.md). Standalone project (no PCSX2, no PS2Recomp): ob.sh test builds it on its own; the runner will
# add it with add_subdirectory for Host game.
cmake_minimum_required(VERSION 3.20)
project(ob_server C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# mbedTLS 2.28.10 (docs/ops/pins.md): the last line that still has 3DES and SSL 3.0 for the PS2's login browser
# (plan P4 result). Source from its GitHub tag, cloned once into C:\ob\mbedtls (outside the repo, as PCSX2 is).
set(OB_MBEDTLS "C:/ob/mbedtls/v2.28.10" CACHE PATH "mbedTLS 2.28.10 source")
if(NOT EXISTS "${OB_MBEDTLS}/include/mbedtls/version.h")
    message(FATAL_ERROR "ob_server: mbedTLS 2.28.10 source not found at OB_MBEDTLS=${OB_MBEDTLS}")
endif()
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0057 NEW) # mbedTLS 2.28's old cmake_minimum_required vs the NDK toolchain (report entry 5)
# SSL 3.0 and the SSLv2-format greeting, for 2003 clients (src/mbedtls_config_ob.h), in every mbedTLS target and ours.
add_compile_definitions("MBEDTLS_USER_CONFIG_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/src/mbedtls_config_ob.h\"")
add_subdirectory("${OB_MBEDTLS}" "${CMAKE_CURRENT_BINARY_DIR}/mbedtls" EXCLUDE_FROM_ALL)

add_library(ob_server STATIC src/datafile.cpp src/protocol.cpp src/web.cpp src/tls_server.cpp src/net.cpp src/accounts.cpp src/lobby.cpp src/relay.cpp
    src/ob_server.cpp src/guard.cpp src/beacon.cpp src/host.cpp)
target_include_directories(ob_server PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(ob_server PUBLIC mbedtls mbedx509 mbedcrypto)
if(WIN32)
    target_link_libraries(ob_server PUBLIC ws2_32)
endif()
if(MSVC)
    target_compile_options(ob_server PRIVATE /W4 /permissive-)
else()
    target_compile_options(ob_server PRIVATE -Wall -Wextra)
endif()

if(NOT ANDROID)
    add_executable(ob_server_tests tests/ob_server_tests.cpp tests/lobby_tests.cpp tests/relay_tests.cpp tests/guard_tests.cpp tests/beacon_tests.cpp tests/host_tests.cpp)
    target_link_libraries(ob_server_tests PRIVATE ob_server)
    # ob_server.exe: the same server as a console program for a dedicated PC (plan N6).
    add_executable(ob_server_exe tools/ob_server_main.cpp)
    set_target_properties(ob_server_exe PROPERTIES OUTPUT_NAME ob_server)
    target_link_libraries(ob_server_exe PRIVATE ob_server)
endif()
