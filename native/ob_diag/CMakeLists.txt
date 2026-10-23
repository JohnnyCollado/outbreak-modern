# ob_diag: the optional diagnostics plugin (a shared library) for the PS2Recomp runner, over the hook API in
# PS2Recomp's runtime/ps2_diag_hooks.h. The runner loads it only when PS2X_DIAG names it (or it sits beside the
# exe as ob_diag.dll); a play build ships without it. Built by tools/ob.sh (recompile, test) into
# $OB_ROOT/diag-build; headers only from the PS2Recomp built copy (OB_SRC), stb from the runner's raylib (OB_T).
cmake_minimum_required(VERSION 3.20)
project(ob_diag LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OB_SRC "C:/ob/src" CACHE PATH "PS2Recomp built copy (runtime headers, MiniTest)")
set(OB_T "C:/ob/t" CACHE PATH "tools build dir (raylib sources: stb_image, stb_image_write)")
set(OB_STB_DIR "${OB_T}/_deps/raylib-src/src/external" CACHE PATH "stb headers")

foreach(f "${OB_SRC}/ps2xRuntime/include/runtime/ps2_diag_hooks.h" "${OB_STB_DIR}/stb_image_write.h")
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "ob_diag: ${f} missing")
    endif()
endforeach()

add_library(ob_diag_core STATIC
    src/input_script.cpp
    src/frame_dump.cpp
    src/frame_time.cpp
    src/pc_sampler.cpp
    src/vu1_recorder.cpp
)
target_include_directories(ob_diag_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${OB_SRC}/ps2xRuntime/include"
    "${OB_STB_DIR}"
)
set_target_properties(ob_diag_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ob_diag SHARED src/plugin.cpp)
target_link_libraries(ob_diag PRIVATE ob_diag_core)
set_target_properties(ob_diag PROPERTIES PREFIX "")

add_executable(ob_diag_tests
    tests/main.cpp
    tests/input_script_tests.cpp
    tests/frame_dump_tests.cpp
    tests/diag_tests.cpp
    tests/stb_image_impl.cpp
    src/plugin.cpp
)
target_link_libraries(ob_diag_tests PRIVATE ob_diag_core)
target_include_directories(ob_diag_tests PRIVATE "${OB_SRC}/ps2xTest/include")
target_compile_definitions(ob_diag_tests PRIVATE OB_DIAG_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-out")
