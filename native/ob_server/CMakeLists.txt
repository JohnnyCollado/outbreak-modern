# SPDX-License-Identifier: AGPL-3.0-or-later
# ob_server: the server built into the app for same-network play (docs/plans/2026-10-27-ob-server.md): login pages,
# lobby and relay for Biohazard Outbreak File #1 (protocol: research/2026-10-27-lobby-protocol.md). AGPL-3.0, own
# licence (LICENSE.md). Standalone project (no PCSX2, no PS2Recomp): ob.sh test builds it on its own; the runner will
# add it with add_subdirectory for Host game.
cmake_minimum_required(VERSION 3.20)
project(ob_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ob_server STATIC src/protocol.cpp)
target_include_directories(ob_server PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(MSVC)
    target_compile_options(ob_server PRIVATE /W4 /permissive-)
else()
    target_compile_options(ob_server PRIVATE -Wall -Wextra)
endif()

if(NOT ANDROID)
    add_executable(ob_server_tests tests/ob_server_tests.cpp)
    target_link_libraries(ob_server_tests PRIVATE ob_server)
endif()
