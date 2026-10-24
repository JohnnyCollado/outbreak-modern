# ob_pcsx2_vu: PCSX2 v2.9.134 (a81f1aa) microVU (VU1 recompiler) as a static library, plus the offline
# compare tool. The x86 emitter, common, Perf/Log and the shared host stubs (eeHw, _cpuRegistersPack, EmuConfig)
# come from ob_pcsx2_gs (native/pcsx2_gs), never compiled twice in one link: this directory add_subdirectory's
# it only when no parent project already did. Study: research/2026-10-23-microvu-feasibility.md.
cmake_minimum_required(VERSION 3.22)

# Android arm64: ARMSX2's ARM64 microVU as its own shared library, libob_a64vu.so (a64/, built against ARMSX2's
# headers, exports only the ob_pcsx2_vu.h API and its table; study research/2026-10-24-armsx2-microvu-feasibility.md).
# The runner hookup is the same adapter/hook.cpp, which dlopens libob_a64vu.so at start-up (OB_PCSX2_VU_DLOPEN): it
# is built here but not linked, so a library that cannot load or start falls back to the interpreter (patch 0041)
# with the reason logged. tools/ob/android.sh packages it. Nothing below (x86 microVU, ob_pcsx2_gs) is used.
if(ANDROID)
    project(ob_pcsx2_vu C CXX)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/a64" "${CMAKE_CURRENT_BINARY_DIR}/a64")
    if(TARGET ps2_runtime)
        add_library(ps2x_external_vu1 STATIC adapter/hook.cpp)
        target_compile_definitions(ps2x_external_vu1 PRIVATE OB_PCSX2_VU_DLOPEN=1)
        target_include_directories(ps2x_external_vu1 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_link_libraries(ps2x_external_vu1 PRIVATE ps2_runtime dl)
        add_dependencies(ps2x_external_vu1 ob_a64vu) # built with the runner, loaded at run time
    endif()
    return()
endif()

cmake_minimum_required(VERSION 3.24)
project(ob_pcsx2_vu C CXX ASM_MASM)

set(OB_PCSX2 "C:/ob/pcsx2" CACHE PATH "PCSX2 v2.9.134 source tree")
set(OB_PCSX2_DEPS "C:/ob/pcsx2-deps" CACHE PATH "zstd + shaderc install prefix")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}") # PCSX2 builds without exceptions

if(NOT TARGET ob_pcsx2_gs)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../pcsx2_gs" "${CMAKE_BINARY_DIR}/pcsx2_gs" EXCLUDE_FROM_ALL)
endif()

# One TU holds all of microVU (microVU.h includes every .inl): src/ob_mvu_jit.cpp. It changes only on a PCSX2
# bump; the adapter, config and stubs are small TUs of their own.
add_library(ob_pcsx2_vu STATIC
    src/ob_mvu_jit.cpp
    src/pcsx2_side.cpp
    src/config_defaults.cpp
    src/link_stubs.cpp)
target_include_directories(ob_pcsx2_vu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(ob_pcsx2_vu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${OB_PCSX2}/pcsx2/x86")
# PRIVATE: PCSX2's defines/includes/flags (PUBLIC on ob_pcsx2_gs) reach only this library, not its users.
target_link_libraries(ob_pcsx2_vu PRIVATE ob_pcsx2_gs)

# ---- runner hookup (PS2Recomp PS2X_EXTERNAL_VU1_DIR add_subdirectory's us, after PS2X_EXTERNAL_GS_DIR) ----
# ps2x_external_vu1 implements PS2ExternalVu1 with PS2Recomp's flags; it reaches microVU only through the
# plain-types include/ob_pcsx2_vu.h, and ob_pcsx2_vu links ob_pcsx2_gs PRIVATE, so PCSX2's defines/options
# never reach ps2_runtime (same two-target split as native/pcsx2_gs/adapter).
if(TARGET ps2_runtime)
    add_library(ps2x_external_vu1 STATIC adapter/hook.cpp)
    target_compile_options(ps2x_external_vu1 PRIVATE /EHsc) # this directory strips /EHsc for PCSX2 (above)
    target_link_libraries(ps2x_external_vu1 PRIVATE ps2_runtime ob_pcsx2_vu)
    return() # the compare tool below is a standalone build (ob.sh vucompare), not part of the runner
endif()

# ---- compare tool (PS2Recomp side): links the runner's own ps2_runtime.lib for the interpreter ----
set(OB_SRC "C:/ob/src" CACHE PATH "PS2Recomp built copy (headers)")
set(OB_R "C:/ob/r" CACHE PATH "runner build dir (ps2_runtime.lib, ps2_iop.lib)")
set(OB_T "C:/ob/t" CACHE PATH "tools build dir (raylib)")
if(EXISTS "${OB_R}/ps2xRuntime/ps2_runtime.lib")
    add_executable(ob_pcsx2_vu_compare compare/main.cpp)
    target_compile_options(ob_pcsx2_vu_compare PRIVATE /EHsc)
    # Same defines as the runner's ps2_runtime objects (C:\ob\r build.ninja): class layouts must agree.
    target_compile_definitions(ob_pcsx2_vu_compare PRIVATE GRAPHICS_API_OPENGL_33 PLATFORM_DESKTOP
        PS2X_ENABLE_IOP_RPC_TRACE=1 PS2X_EXTERNAL_GS=1 PS2X_HAS_FFMPEG=0 PS2_RUNTIME_LOGS=1)
    target_include_directories(ob_pcsx2_vu_compare PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../ob_diag/include"
        "${OB_SRC}/ps2xRuntime/include"
        "${OB_SRC}/ps2xRuntime/src/lib/Kernel" "${OB_SRC}/ps2xIOP/include" "${OB_T}/_deps/raylib-src/src")
    target_link_libraries(ob_pcsx2_vu_compare PRIVATE ob_pcsx2_vu
        "${OB_R}/ps2xRuntime/ps2_runtime.lib" "${OB_R}/ps2xIOP/ps2_iop.lib"
        "${OB_T}/_deps/raylib-build/raylib/raylib.lib" opengl32 glu32 winmm)

    # Placement and start-up tests (ob.sh test): the runner's adapter + its ps2_runtime.lib (PS2Memory with
    # patch 0041, ps2InstallExternalVu1, VU1Interpreter) + microVU, under a crowded address space.
    add_executable(ob_pcsx2_vu_tests tests/vu_tests.cpp adapter/hook.cpp)
    target_compile_options(ob_pcsx2_vu_tests PRIVATE /EHsc)
    target_compile_definitions(ob_pcsx2_vu_tests PRIVATE GRAPHICS_API_OPENGL_33 PLATFORM_DESKTOP
        PS2X_ENABLE_IOP_RPC_TRACE=1 PS2X_EXTERNAL_GS=1 PS2X_HAS_FFMPEG=0 PS2_RUNTIME_LOGS=1 PS2X_EXTERNAL_VU1=1)
    target_include_directories(ob_pcsx2_vu_tests PRIVATE "${OB_SRC}/ps2xTest/include" "${OB_SRC}/ps2xRuntime/include"
        "${OB_SRC}/ps2xRuntime/src/lib/Kernel" "${OB_SRC}/ps2xIOP/include" "${OB_T}/_deps/raylib-src/src")
    target_link_libraries(ob_pcsx2_vu_tests PRIVATE ob_pcsx2_vu
        "${OB_R}/ps2xRuntime/ps2_runtime.lib" "${OB_R}/ps2xIOP/ps2_iop.lib"
        "${OB_T}/_deps/raylib-build/raylib/raylib.lib" opengl32 glu32 winmm)
else()
    message(STATUS "ob_pcsx2_vu: ${OB_R}/ps2xRuntime/ps2_runtime.lib missing, compare tool not configured (ob.sh recompile first)")
endif()
