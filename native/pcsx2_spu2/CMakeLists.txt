# PCSX2's SPU2 (sound chip) as a static library, included by native/pcsx2_gs/CMakeLists.txt after ob_pcsx2_gs
# (it shares that library's common/, config stubs, compile options and include paths).
# Sources: pcsx2/CMakeLists.txt pcsx2SPU2Sources + pcsx2SPU2SourcesUnshared (one ISA, as the GS), Host/AudioStream.cpp,
# 3rdparty soundtouch (time stretch) and freesurround (AudioStream's expansion modes).
set(S "${P}/pcsx2")
set(spu2_src
    SPU2/ADSR.cpp SPU2/Debug.cpp SPU2/Dma.cpp SPU2/spu2.cpp SPU2/ReadInput.cpp SPU2/RegTable.cpp SPU2/Reverb.cpp
    SPU2/spu2sys.cpp SPU2/Mixer.cpp SPU2/ReverbResample.cpp
    Host/AudioStream.cpp)
# Left out: SPU2/spu2freeze.cpp (save states), SPU2/Wavedump_wav.cpp (dev-build WAV logs),
# Host/CubebAudioStream.cpp and Host/SDLAudioStream.cpp (the output is adapter/spu2_side.cpp's pull stream).
list(TRANSFORM spu2_src PREPEND "${S}/")

add_subdirectory("${P}/3rdparty/soundtouch" "${CMAKE_BINARY_DIR}/soundtouch" EXCLUDE_FROM_ALL)
add_subdirectory("${P}/3rdparty/freesurround" "${CMAKE_BINARY_DIR}/freesurround" EXCLUDE_FROM_ALL)
# Built with their own include paths: PCSX2's pcsx2/Memory.h would shadow <memory.h> (case-insensitive on Windows).
if(ANDROID)
    target_compile_options(pcsx2-soundtouch PRIVATE -w)
    target_compile_options(freesurround PRIVATE -w)
endif()

add_library(ob_pcsx2_spu2 STATIC ${spu2_src}
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs/spu2_stubs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter/spu2_side.cpp")
target_include_directories(ob_pcsx2_spu2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
target_link_libraries(ob_pcsx2_spu2 PUBLIC ob_pcsx2_gs pcsx2-soundtouch freesurround)

if(NOT ANDROID)
    add_executable(ob_pcsx2_spu2_tests tests/spu2_tests.cpp)
    target_include_directories(ob_pcsx2_spu2_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
    target_link_libraries(ob_pcsx2_spu2_tests PRIVATE ob_pcsx2_spu2)
endif()
