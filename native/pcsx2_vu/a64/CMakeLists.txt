# libob_a64vu.so: ARMSX2's ARM64 microVU (VU1 recompiler) behind the plain-types ../include/ob_pcsx2_vu.h, Android
# arm64 only. Study: research/2026-10-24-armsx2-microvu-feasibility.md.
#
# Its own shared library because it is compiled against ARMSX2's headers (pinned commit 90126bd), whose VU.h,
# Config.h, Gif_Unit.h, MTVU.h differ from our PCSX2 v2.9.134 pin (study 1.3): same-named types with different
# layouts must never meet in one link. Hidden visibility, -Wl,--exclude-libs,ALL and exports.map leave only
# ObPcsx2Vu::* in the dynamic symbol table. vixl comes from the same pinned sources the GS builds
# (${OB_PCSX2}/3rdparty/vixl, identical to ARMSX2's apart from two warning flags, study 1.4): the existing `vixl`
# target when the GS already added it to this build, else added here. fmt's format.cc is compiled in (same tree in
# both repos). Nothing of ARMSX2 is edited or copied into git: its sources compile from ${OB_ARMSX2}.
#
# Used two ways:
#   - from ../CMakeLists.txt (PS2Recomp's PS2X_EXTERNAL_VU1_DIR in the Android tree): target ob_a64vu only;
#   - standalone (tools: configure this directory with the NDK toolchain): also ob_vucompare_a64, the arm64
#     vucompare (../compare/main.cpp), when OB_A64_RUNTIME_LIB (the Android tree's libps2_runtime.a) exists.
cmake_minimum_required(VERSION 3.22)
project(ob_a64vu C CXX)
if(NOT ANDROID OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(FATAL_ERROR "native/pcsx2_vu/a64 is Android arm64 only")
endif()

set(OB_ARMSX2 "C:/ob/armsx2" CACHE PATH "ARMSX2 source tree (sparse clone, pinned 90126bd)")
set(OB_PCSX2 "C:/ob/pcsx2" CACHE PATH "PCSX2 v2.9.134 source tree (vixl, fmt)")
set(OB_PCSX2_DEPS "C:/ob/pcsx2-deps" CACHE PATH "PCSX2 deps install prefix (headers only here)")
set(OB_ARMSX2_COMMIT "90126bdfea7a3ffb1fd288b2f7f0ae0b033af2d6")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(A "${OB_ARMSX2}")

# The pin is checked at configure time: a different ARMSX2 commit needs a deliberate bump (re-copy the defaults
# in config_defaults_a64.cpp and GetGSPacketSize in ob_a64mvu_fakes.h).
if(NOT EXISTS "${A}/pcsx2/arm64/microVU-arm64.cpp")
    message(FATAL_ERROR "ob_a64vu: ${A}/pcsx2/arm64/microVU-arm64.cpp missing (ARMSX2 sparse clone, docs/ops/pins.md)")
endif()
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${A}" rev-parse HEAD OUTPUT_VARIABLE ob_armsx2_head
        OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE ob_rc)
    if(ob_rc EQUAL 0 AND NOT ob_armsx2_head STREQUAL OB_ARMSX2_COMMIT)
        message(FATAL_ERROR "ob_a64vu: ${A} is at ${ob_armsx2_head}, pinned ${OB_ARMSX2_COMMIT}")
    endif()
endif()

if(NOT TARGET vixl)
    add_subdirectory("${OB_PCSX2}/3rdparty/vixl" "${CMAKE_BINARY_DIR}/vixl" EXCLUDE_FROM_ALL)
    target_compile_options(vixl PRIVATE -w)
endif()

# Compiled verbatim from ARMSX2: AsmHelpers (vixl glue) and the vu-aot tier's TUs (vu-aot is off at run time,
# but microVU-arm64.cpp includes its text, so its symbols must link: study 1.4). microVU-arm64.cpp itself is
# #included by ob_a64mvu_jit.cpp (the fakes need a macro around it).
file(GLOB ob_vuaot_src "${A}/pcsx2/arm64/vuaot/*.cpp")
list(LENGTH ob_vuaot_src ob_n)
if(NOT ob_n EQUAL 10)
    message(FATAL_ERROR "ob_a64vu: expected 10 vuaot/*.cpp at 90126bd, found ${ob_n}")
endif()

add_library(ob_a64vu SHARED
    ob_a64mvu_jit.cpp
    a64_side.cpp
    config_defaults_a64.cpp
    link_stubs_a64.cpp
    common_shims_a64.cpp
    "${A}/pcsx2/arm64/AsmHelpers.cpp"
    ${ob_vuaot_src}
    "${OB_PCSX2}/3rdparty/fmt/src/format.cc")
target_include_directories(ob_a64vu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_include_directories(ob_a64vu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${A}" "${A}/pcsx2" "${A}/pcsx2/arm64" "${A}/common" "${A}/3rdparty/include"
    "${OB_PCSX2}/3rdparty/fmt/include" "${OB_PCSX2}/3rdparty/fast_float/include"
    "${OB_PCSX2}/3rdparty/imgui/include" "${OB_PCSX2}/3rdparty/cpuinfo/include"
    "${OB_PCSX2}/3rdparty/vulkan/include" "${OB_PCSX2}/3rdparty/rapidyaml/include"
    "${OB_PCSX2}/3rdparty/simpleini/include" "${OB_PCSX2_DEPS}/include")
# PCSX2_DEFS for a POSIX arm64 build, as the GS's Android branch (native/pcsx2_gs/CMakeLists.txt): __POSIX__,
# -march=armv8.1-a, no exceptions, hidden visibility. -w: third-party code, warnings are not ours to fix.
target_compile_definitions(ob_a64vu PRIVATE __POSIX__ FMT_EXCEPTIONS=0)
target_compile_options(ob_a64vu PRIVATE -march=armv8.1-a -fno-exceptions -fvisibility=hidden
    -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -w)
target_link_libraries(ob_a64vu PRIVATE vixl)
target_link_options(ob_a64vu PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
    -Wl,--exclude-libs,ALL -Wl,--no-undefined -Wl,--gc-sections)
set_target_properties(ob_a64vu PROPERTIES LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")

# ---- arm64 vucompare (standalone configure only): the Android tree's libps2_runtime.a for the interpreter ----
if(NOT TARGET ps2_runtime)
    set(OB_SRC "C:/ob/src" CACHE PATH "PS2Recomp built copy (headers)")
    set(OB_ANDROID_R "C:/ob/android-r" CACHE PATH "Android runner build tree")
    set(OB_T "C:/ob/t" CACHE PATH "tools build dir (raylib sources)")
    set(OB_ROOT_FETCH "C:/ob/android-fetch" CACHE PATH "Android FetchContent dir (sse2neon)")
    set(OB_A64_RUNTIME_LIB "${OB_ANDROID_R}/ps2xRuntime/libps2_runtime.a" CACHE FILEPATH "Android libps2_runtime.a")
    if(EXISTS "${OB_A64_RUNTIME_LIB}")
        add_executable(ob_vucompare_a64 ../compare/main.cpp)
        # Same defines as the Android tree's ps2_runtime objects (C:\ob\android-r compile_commands.json):
        # class layouts must agree. OB_A64_RUNTIME_DEFS overrides them if that tree's flags change.
        set(OB_A64_RUNTIME_DEFS "GRAPHICS_API_OPENGL_ES2;PLATFORM_ANDROID;PS2X_ENABLE_IOP_RPC_TRACE=1;PS2X_EXTERNAL_GS=1;PS2X_HAS_FFMPEG=0;PS2_RUNTIME_LOGS=1;USE_SSE2NEON"
            CACHE STRING "compile definitions of the Android libps2_runtime.a objects")
        target_compile_definitions(ob_vucompare_a64 PRIVATE ${OB_A64_RUNTIME_DEFS})
        target_include_directories(ob_vucompare_a64 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../ob_diag/include"
            "${OB_SRC}/ps2xRuntime/include" "${OB_SRC}/ps2xRuntime/src/lib/Kernel" "${OB_SRC}/ps2xIOP/include"
            "${OB_T}/_deps/raylib-src/src" "${OB_ROOT_FETCH}/sse2neon-src" ${OB_A64_RUNTIME_INCLUDES})
        target_link_libraries(ob_vucompare_a64 PRIVATE ob_a64vu "${OB_A64_RUNTIME_LIB}" ${OB_A64_RUNTIME_EXTRA_LIBS} log m)
        # Runs from /data/local/tmp/obvu with libob_a64vu.so beside it.
        set_target_properties(ob_vucompare_a64 PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
    else()
        message(STATUS "ob_a64vu: ${OB_A64_RUNTIME_LIB} missing, ob_vucompare_a64 not configured (ob.sh android build first)")
    endif()
endif()
