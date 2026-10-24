# ob_pcsx2_gs: PCSX2 v2.9.134 (a81f1aa) GS + common as one static library, plus a smoke exe.
# Source lists are read at configure time from PCSX2's own CMakeLists text (never hand-copied),
# then trimmed by the explicit exclusion list below. Host-side symbols come from stubs/*.cpp.
# Single ISA (isa_native, /arch:AVX2), USE_VULKAN only: no DX11/DX12/OpenGL/Metal sources or defines.
# Android arm64 (NDK clang, Stage A1): PCSX2's ARCH_ARM64 lists (SW JIT on vixl, GSVector*_arm64.h), the Linux
# common files minus LnxMisc.cpp (X11/D-Bus; stubs/android/ has its other functions), Vulkan through
# VK_KHR_android_surface (upstream/pcsx2/0001), presenting into the NativeActivity's ANativeWindow.
cmake_minimum_required(VERSION 3.22)
if(ANDROID)
    project(ob_pcsx2_gs C CXX)
else()
    project(ob_pcsx2_gs C CXX ASM_MASM)
endif()

set(OB_PCSX2 "C:/ob/pcsx2" CACHE PATH "PCSX2 v2.9.134 source tree")
set(OB_PCSX2_DEPS "C:/ob/pcsx2-deps" CACHE PATH "zstd + shaderc install prefix")
set(P "${OB_PCSX2}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
# PCSX2 builds without exceptions (BuildParameters.cmake).
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# ob_extract(<var> <file> <anchor>): the source files (.cpp/.cc/.asm) of the first parenthesised
# list that follows <anchor> (up to the next ')') in <file>. Fails loudly if the anchor is gone (upstream changed).
function(ob_extract var file anchor)
    file(READ "${file}" text)
    string(REPLACE "\r" "" text "${text}")
    string(FIND "${text}" "${anchor}" pos)
    if(pos LESS 0)
        message(FATAL_ERROR "ob_extract: anchor not found in ${file}: ${anchor}")
    endif()
    string(LENGTH "${anchor}" alen)
    math(EXPR pos "${pos} + ${alen}")
    string(SUBSTRING "${text}" ${pos} -1 rest)
    string(FIND "${rest}" ")" end)
    string(SUBSTRING "${rest}" 0 ${end} block)
    string(REGEX MATCHALL "[A-Za-z0-9_./-]+\\.(cpp|cc|asm)" files "${block}")
    set(${var} ${files} PARENT_SCOPE)
endfunction()

# ---- PCSX2 GS lists (pcsx2/CMakeLists.txt) ----
set(PC "${P}/pcsx2/CMakeLists.txt")
ob_extract(gs_shared   "${PC}" "set(pcsx2GSSources\n")
ob_extract(gs_unshared "${PC}" "set(pcsx2GSSourcesUnshared")
if(ANDROID)
    ob_extract(gs_arch "${PC}" "elseif(ARCH_ARM64)\n\tlist(APPEND pcsx2GSSourcesUnshared")
else()
    ob_extract(gs_arch "${PC}" "if(ARCH_X86)\n\tlist(APPEND pcsx2GSSourcesUnshared")
endif()
ob_extract(gs_vulkan   "${PC}" "if(USE_VULKAN)\n\tlist(APPEND pcsx2GSSources")
set(gs_all ${gs_shared} ${gs_unshared} ${gs_arch} ${gs_vulkan})
list(REMOVE_ITEM gs_all
    GS/GSCapture.cpp                              # FFmpeg video/audio capture; stubbed (stubs/gs_stubs.cpp)
    GS/GSDump.cpp                                 # GS dump writer (zstd/xz); stubbed
    GS/GSLzma.cpp                                 # GS dump reader (zstd + 3rdparty/lzma); only GSDumpReplayer uses it
    GS/GSPng.cpp                                  # libpng/zlib writer for dumps/snapshots; GSPng::Save stubbed
)
list(TRANSFORM gs_all PREPEND "${P}/pcsx2/")

# ---- common (common/CMakeLists.txt) ----
set(CC "${P}/common/CMakeLists.txt")
ob_extract(common_base "${CC}" "add_library(common)\n\ntarget_sources(common PRIVATE")
if(ANDROID)
    # The Linux branch (FastJmp.cpp's inline asm covers arm64). LnxMisc.cpp needs X11 and D-Bus.
    ob_extract(common_lnx "${CC}" "else()\n\ttarget_sources(common PRIVATE")
    list(REMOVE_ITEM common_lnx Linux/LnxMisc.cpp)
    set(common_all ${common_base} ${common_lnx})
else()
    ob_extract(common_x86  "${CC}" "if(ARCH_X86)\n\ttarget_sources(common PRIVATE")
    ob_extract(common_win  "${CC}" "if(WIN32)\n\ttarget_sources(common PRIVATE")
    ob_extract(common_asm  "${CC}" "enable_language(ASM_MASM)\n\t\ttarget_sources(common PRIVATE")
    set(common_all ${common_base} ${common_x86} ${common_win} ${common_asm})
endif()
list(REMOVE_ITEM common_all
    PrecompiledHeader.cpp                         # HEADER_FILE_ONLY upstream
    Image.cpp                                     # libpng/libjpeg/libwebp; RGBA8Image I/O stubbed
    YAML.cpp                                      # rapidyaml (ryml); stubbed
    HTTPDownloader.cpp                            # updater/achievements HTTP; nothing in GS uses it
    HTTPDownloaderWinHTTP.cpp                     # WinHTTP backend of the above
)
list(TRANSFORM common_all PREPEND "${P}/common/")

# ---- 3rdparty ----
# imgui: ONLY PCSX2's 3rdparty imgui (1.93.0) is compiled, into ob_pcsx2_gs. PS2Recomp's imgui (1.92.7)
# is fetched only when PS2X_ENABLE_DEBUG_UI=ON, which our runner keeps OFF, so exactly one imgui
# reaches the final runner link.
ob_extract(imgui_src "${P}/3rdparty/imgui/CMakeLists.txt" "add_library(imgui")
list(REMOVE_ITEM imgui_src
    src/imgui_demo.cpp                            # demo windows (IMGUI_DISABLE_DEMO_WINDOWS upstream)
    src/imgui_freetype.cpp                        # FreeType + plutosvg not in our deps; font loader stubbed
)
list(TRANSFORM imgui_src PREPEND "${P}/3rdparty/imgui/")
ob_extract(fmt_src "${P}/3rdparty/fmt/CMakeLists.txt" "set(FMT_SOURCES")
list(TRANSFORM fmt_src PREPEND "${P}/3rdparty/fmt/")

set(CPUINFO_LIBRARY_TYPE static CACHE STRING "" FORCE)
set(CPUINFO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(CPUINFO_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(CPUINFO_BUILD_MOCK_TESTS OFF CACHE BOOL "" FORCE)
set(CPUINFO_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
add_subdirectory("${P}/3rdparty/cpuinfo" "${CMAKE_BINARY_DIR}/cpuinfo" EXCLUDE_FROM_ALL)

foreach(l gs_all common_all imgui_src fmt_src)
    list(LENGTH ${l} n)
    message(STATUS "ob_pcsx2_gs: ${l} = ${n} files")
endforeach()

file(GLOB ob_stubs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.cpp")
if(ANDROID)
    file(GLOB ob_stubs_android CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stubs/android/*.cpp")
    list(APPEND ob_stubs ${ob_stubs_android})
    # vixl: the arm64 assembler behind the SW renderer's JIT (GSDrawScanlineCodeGenerator.arm64.cpp).
    add_subdirectory("${P}/3rdparty/vixl" "${CMAKE_BINARY_DIR}/vixl" EXCLUDE_FROM_ALL)
    target_compile_options(vixl PRIVATE -w)
endif()

add_library(ob_pcsx2_gs STATIC ${gs_all} ${common_all} ${imgui_src} ${fmt_src} ${ob_stubs})
# include/ first: its GS/Renderers/DX11|DX12 headers shadow PCSX2's D3D headers (reasons in each shim).
target_include_directories(ob_pcsx2_gs BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(ob_pcsx2_gs PUBLIC
    "${P}" "${P}/pcsx2" "${P}/common" "${P}/3rdparty/include"
    "${P}/3rdparty/imgui/include" "${P}/3rdparty/fmt/include" "${P}/3rdparty/fast_float/include"
    "${P}/3rdparty/winwil/include" "${P}/3rdparty/xbyak" "${P}/3rdparty/vulkan/include"
    "${OB_PCSX2_DEPS}/include")
target_include_directories(ob_pcsx2_gs PRIVATE "${P}/3rdparty/imgui/src")
if(ANDROID)
    # PCSX2_DEFS for a POSIX arm64 build (BuildParameters.cmake: __POSIX__, -march=armv8.1-a for the atomics),
    # PCSX2's -fno-exceptions/-fvisibility=hidden; the Android shims (shm_open) force-included where needed.
    target_include_directories(ob_pcsx2_gs PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/stubs/android"
        "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
    target_compile_definitions(ob_pcsx2_gs PUBLIC
        __POSIX__ ENABLE_VULKAN FMT_EXCEPTIONS=0
        IMGUI_ENABLE_STB_TRUETYPE)
    target_compile_options(ob_pcsx2_gs PUBLIC
        "$<$<COMPILE_LANGUAGE:C,CXX>:-march=armv8.1-a>" "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:-fvisibility=hidden>" "$<$<COMPILE_LANGUAGE:C,CXX>:-w>")
    # LnxHostSys.cpp and GS.cpp (GSAllocateWrappedMemory) call shm_open/shm_unlink.
    target_compile_options(ob_pcsx2_gs PRIVATE
        "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/stubs/android/ob_android_compat.h")
    target_link_libraries(ob_pcsx2_gs PUBLIC cpuinfo vixl android log dl)
    # Texture replacement (GSTextureReplacements*.cpp), as on Windows: libpng 1.6.58 and zstd 1.5.7 built static
    # from the sources tools/ob/pcsx2-deps*.bat unpacked, zlib from the NDK, PCSX2's 3rdparty libzip.
    set(OB_DEPS_SRC "${OB_PCSX2_DEPS}-build")
    find_package(ZLIB REQUIRED)
    set(PNG_SHARED OFF CACHE BOOL "" FORCE)
    set(PNG_STATIC ON CACHE BOOL "" FORCE)
    set(PNG_TESTS OFF CACHE BOOL "" FORCE)
    set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${OB_DEPS_SRC}/lpng1658" "${CMAKE_BINARY_DIR}/libpng" EXCLUDE_FROM_ALL)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    add_subdirectory("${OB_DEPS_SRC}/zstd-1.5.7/build/cmake" "${CMAKE_BINARY_DIR}/zstd" EXCLUDE_FROM_ALL)
    add_library(Zstd::Zstd ALIAS libzstd_static)
    add_subdirectory("${P}/3rdparty/libzip" "${CMAKE_BINARY_DIR}/libzip" EXCLUDE_FROM_ALL)
    target_compile_options(zip PRIVATE -w)
    # This build's png.h/pnglibconf.h ahead of the Windows install's copies in ${OB_PCSX2_DEPS}/include.
    target_include_directories(ob_pcsx2_gs BEFORE PRIVATE
        "${OB_DEPS_SRC}/lpng1658" "${CMAKE_BINARY_DIR}/libpng")
    target_compile_definitions(ob_pcsx2_gs PUBLIC OB_TEXTURE_REPLACEMENTS)
    target_link_libraries(ob_pcsx2_gs PUBLIC zip png_static)
else()
    # PCSX2_DEFS (BuildParameters.cmake, Release, x86, WIN32) + pcsx2/CMakeLists WIN32 block; single ISA.
    target_compile_definitions(ob_pcsx2_gs PUBLIC
        _M_X86=1 __SSE4_1__=1 ENABLE_VULKAN
        _ITERATOR_DEBUG_LEVEL=0 _HAS_EXCEPTIONS=0
        _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS CRT_SECURE_NO_DEPRECATE _SCL_SECURE_NO_WARNINGS
        _UNICODE UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN WIL_SUPPRESS_EXCEPTIONS
        XBYAK_NO_EXCEPTION FMT_EXCEPTIONS=0
        IMGUI_ENABLE_STB_TRUETYPE) # no FreeType: stubs/ui_stubs.cpp routes the font loader to stb_truetype
    target_compile_options(ob_pcsx2_gs PUBLIC
        "$<$<COMPILE_LANGUAGE:C,CXX>:/arch:AVX2>" "$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>"
        "$<$<COMPILE_LANGUAGE:CXX>:/Zc:externConstexpr;/Zc:__cplusplus;/permissive-;/Zc:preprocessor>"
        "$<$<COMPILE_LANGUAGE:C,CXX>:/fp:contract;/GS-;/W0>")
    # OneCore: VirtualAlloc2/MapViewOfFile3 (GS.cpp GSAllocateWrappedMemory), as pcsx2/CMakeLists.txt links it.
    target_link_libraries(ob_pcsx2_gs PUBLIC cpuinfo winmm pathcch dwmapi OneCore)
    # Texture replacement (GSTextureReplacements*.cpp): PNG loader on libpng, <serial>.zip packs on PCSX2's
    # 3rdparty libzip (zlib + zstd), deps from tools/ob/pcsx2-deps-png.bat and pcsx2-deps.bat.
    list(APPEND CMAKE_PREFIX_PATH "${OB_PCSX2_DEPS}")
    find_package(ZLIB REQUIRED)
    find_package(PNG REQUIRED)
    if(NOT TARGET Zstd::Zstd)
        add_library(Zstd::Zstd SHARED IMPORTED)
        set_target_properties(Zstd::Zstd PROPERTIES
            IMPORTED_IMPLIB "${OB_PCSX2_DEPS}/lib/zstd.lib"
            IMPORTED_LOCATION "${OB_PCSX2_DEPS}/bin/zstd.dll"
            INTERFACE_INCLUDE_DIRECTORIES "${OB_PCSX2_DEPS}/include")
    endif()
    add_subdirectory("${P}/3rdparty/libzip" "${CMAKE_BINARY_DIR}/libzip" EXCLUDE_FROM_ALL)
    target_compile_options(zip PRIVATE /W0)
    target_compile_definitions(ob_pcsx2_gs PUBLIC OB_TEXTURE_REPLACEMENTS)
    target_link_libraries(ob_pcsx2_gs PUBLIC zip PNG::PNG)

    add_executable(ob_pcsx2_gs_smoke smoke/main.cpp)
    target_link_libraries(ob_pcsx2_gs_smoke PRIVATE ob_pcsx2_gs)

    # GS thread tests (ob.sh test): the command ring alone (header-only, no PCSX2), and the adapter (pcsx2_side.cpp over
    # ob_pcsx2_gs, SW renderer) end to end through the ring. Both run from this build dir (resources/ staged by ob.sh pcsx2-gs).
    add_executable(ob_gs_ring_tests tests/gs_ring_tests.cpp)
    target_include_directories(ob_gs_ring_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
    target_compile_options(ob_gs_ring_tests PRIVATE /EHsc) # <chrono>/<thread>; this directory strips /EHsc for PCSX2
    add_executable(ob_pcsx2_gs_adapter_tests tests/adapter_tests.cpp adapter/pcsx2_side.cpp adapter/gpu_driver.cpp)
    target_include_directories(ob_pcsx2_gs_adapter_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
    target_link_libraries(ob_pcsx2_gs_adapter_tests PRIVATE ob_pcsx2_gs)
    # Android GPU driver choice, pure part (adapter/gpu_driver.cpp): setting, meta.json, fall-back decision, log lines.
    add_executable(ob_gpu_driver_tests tests/gpu_driver_tests.cpp adapter/gpu_driver.cpp)
    target_include_directories(ob_gpu_driver_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
endif()

# Runner hookup (PS2Recomp PS2X_EXTERNAL_GS_DIR add_subdirectory's us): ps2x_external_gs implements
# PS2ExternalGS with PS2Recomp's flags; it reaches PCSX2 only through the plain-types adapter/ob_gs_bridge.h,
# and links ob_pcsx2_gs_adapter PRIVATE so PCSX2's PUBLIC defines/options never reach ps2_runtime.
if(TARGET ps2_runtime)
    # gpu_driver.cpp: the pure GPU-driver decisions (driver choice on Android, gs_self_read everywhere).
    add_library(ob_pcsx2_gs_adapter STATIC adapter/pcsx2_side.cpp adapter/gpu_driver.cpp)
    target_include_directories(ob_pcsx2_gs_adapter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
    target_link_libraries(ob_pcsx2_gs_adapter PUBLIC ob_pcsx2_gs)
    if(ANDROID)
        # pcsx2_side.cpp: the activity's window and assets (android_app via raylib), raylib's EGL surface release.
        target_include_directories(ob_pcsx2_gs_adapter PRIVATE "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
        target_link_libraries(ob_pcsx2_gs_adapter PUBLIC EGL android)
        # Custom Vulkan driver (outbreak.ini gpu_driver; docs/ops/android.md "GPU driver"): libadrenotools
        # (BSD-2-Clause, github.com/bylaws/libadrenotools, pin in docs/ops/pins.md) from source. adrenotools and
        # linkernsbypass link in statically; its hooks libhook_impl.so + libmain_hook.so must sit in the APK's lib/
        # (tools/ob/android.sh package takes them from <build>/adrenotools/src/hook).
        set(OB_ADRENOTOOLS "C:/ob/adrenotools" CACHE PATH "libadrenotools source tree (pinned commit)")
        add_subdirectory("${OB_ADRENOTOOLS}" "${CMAKE_BINARY_DIR}/adrenotools" EXCLUDE_FROM_ALL)
        target_sources(ob_pcsx2_gs_adapter PRIVATE adapter/gpu_driver_android.cpp)
        target_link_libraries(ob_pcsx2_gs_adapter PRIVATE adrenotools)
        add_dependencies(ob_pcsx2_gs_adapter main_hook hook_impl)
    endif()

    add_library(ps2x_external_gs STATIC adapter/hook.cpp)
    if(MSVC)
        target_compile_options(ps2x_external_gs PRIVATE /EHsc) # this directory strips /EHsc for PCSX2 (above)
    else()
        target_compile_options(ps2x_external_gs PRIVATE -fexceptions) # after ob_pcsx2_gs's -fno-exceptions
    endif()
    target_link_libraries(ps2x_external_gs PRIVATE ps2_runtime ob_pcsx2_gs_adapter)
endif()
