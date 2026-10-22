# ob_pcsx2_gs: PCSX2 v2.9.134 (a81f1aa) GS + common as one static library, plus a smoke exe.
# Source lists are read at configure time from PCSX2's own CMakeLists text (never hand-copied),
# then trimmed by the explicit exclusion list below. Host-side symbols come from stubs/*.cpp.
# Single ISA (isa_native, /arch:AVX2), USE_VULKAN only: no DX11/DX12/OpenGL/Metal sources or defines.
cmake_minimum_required(VERSION 3.24)
project(ob_pcsx2_gs C CXX ASM_MASM)

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
ob_extract(gs_x86      "${PC}" "if(ARCH_X86)\n\tlist(APPEND pcsx2GSSourcesUnshared")
ob_extract(gs_vulkan   "${PC}" "if(USE_VULKAN)\n\tlist(APPEND pcsx2GSSources")
set(gs_all ${gs_shared} ${gs_unshared} ${gs_x86} ${gs_vulkan})
list(REMOVE_ITEM gs_all
    GS/GSCapture.cpp                              # FFmpeg video/audio capture; stubbed (stubs/gs_stubs.cpp)
    GS/GSDump.cpp                                 # GS dump writer (zstd/xz); stubbed
    GS/GSLzma.cpp                                 # GS dump reader (zstd + 3rdparty/lzma); only GSDumpReplayer uses it
    GS/GSPng.cpp                                  # libpng/zlib writer for dumps/snapshots; GSPng::Save stubbed
    GS/Renderers/HW/GSTextureReplacementLoaders.cpp # png/dds texture-pack loaders; replacements stubbed
    GS/Renderers/HW/GSTextureReplacements.cpp     # texture-pack dumping/replacement; stubbed
)
list(TRANSFORM gs_all PREPEND "${P}/pcsx2/")

# ---- common (common/CMakeLists.txt) ----
set(CC "${P}/common/CMakeLists.txt")
ob_extract(common_base "${CC}" "add_library(common)\n\ntarget_sources(common PRIVATE")
ob_extract(common_x86  "${CC}" "if(ARCH_X86)\n\ttarget_sources(common PRIVATE")
ob_extract(common_win  "${CC}" "if(WIN32)\n\ttarget_sources(common PRIVATE")
ob_extract(common_asm  "${CC}" "enable_language(ASM_MASM)\n\t\ttarget_sources(common PRIVATE")
set(common_all ${common_base} ${common_x86} ${common_win} ${common_asm})
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

add_library(ob_pcsx2_gs STATIC ${gs_all} ${common_all} ${imgui_src} ${fmt_src} ${ob_stubs})
# include/ first: its GS/Renderers/DX11|DX12 headers shadow PCSX2's D3D headers (reasons in each shim).
target_include_directories(ob_pcsx2_gs BEFORE PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(ob_pcsx2_gs PUBLIC
    "${P}" "${P}/pcsx2" "${P}/common" "${P}/3rdparty/include"
    "${P}/3rdparty/imgui/include" "${P}/3rdparty/fmt/include" "${P}/3rdparty/fast_float/include"
    "${P}/3rdparty/winwil/include" "${P}/3rdparty/xbyak" "${P}/3rdparty/vulkan/include"
    "${OB_PCSX2_DEPS}/include")
target_include_directories(ob_pcsx2_gs PRIVATE "${P}/3rdparty/imgui/src")
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

add_executable(ob_pcsx2_gs_smoke smoke/main.cpp)
target_link_libraries(ob_pcsx2_gs_smoke PRIVATE ob_pcsx2_gs)

# Runner hookup (PS2Recomp PS2X_EXTERNAL_GS_DIR add_subdirectory's us): ps2x_external_gs implements
# PS2ExternalGS with PS2Recomp's flags; it reaches PCSX2 only through the plain-types adapter/ob_gs_bridge.h,
# and links ob_pcsx2_gs_adapter PRIVATE so PCSX2's PUBLIC defines/options never reach ps2_runtime.
if(TARGET ps2_runtime)
    add_library(ob_pcsx2_gs_adapter STATIC adapter/pcsx2_side.cpp)
    target_include_directories(ob_pcsx2_gs_adapter PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
    target_link_libraries(ob_pcsx2_gs_adapter PUBLIC ob_pcsx2_gs)

    add_library(ps2x_external_gs STATIC adapter/hook.cpp)
    target_compile_options(ps2x_external_gs PRIVATE /EHsc) # this directory strips /EHsc for PCSX2 (above)
    target_link_libraries(ps2x_external_gs PRIVATE ps2_runtime ob_pcsx2_gs_adapter)
endif()
