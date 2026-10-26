# PCSX2's SIO2 (pads' and memory cards' serial interface) and its PS2 memory cards as a static library, included by
# native/pcsx2_gs/CMakeLists.txt after ob_pcsx2_gs (it shares that library's common/, config stubs, compile options
# and include paths). Sources: pcsx2/CMakeLists.txt pcsx2SIOSources (the SIO2, memory card and multitap files) and
# the two pad classes Sio2::Pad needs for a port with no pad.
set(S "${P}/pcsx2")
set(sio2_src
    SIO/Sio.cpp SIO/Sio2.cpp
    SIO/Memcard/MemoryCardFile.cpp SIO/Memcard/MemoryCardProtocol.cpp
    SIO/Multitap/MultitapProtocol.cpp
    SIO/Pad/PadBase.cpp SIO/Pad/PadNotConnected.cpp)
# Left out: SIO/Sio0.cpp (PS1 serial port), SIO/Memcard/MemoryCardFolder.cpp (folder cards: rapidyaml),
# SIO/Pad/Pad.cpp and the other pad types (input bindings: pads come through PS2Recomp's libpad2 HLE).
list(TRANSFORM sio2_src PREPEND "${S}/")

add_library(ob_pcsx2_sio2 STATIC ${sio2_src}
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs/sio2_stubs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter/sio2_side.cpp")
target_include_directories(ob_pcsx2_sio2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
target_link_libraries(ob_pcsx2_sio2 PUBLIC ob_pcsx2_gs)

if(NOT ANDROID)
    add_executable(ob_pcsx2_sio2_tests tests/sio2_tests.cpp)
    target_include_directories(ob_pcsx2_sio2_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
    target_link_libraries(ob_pcsx2_sio2_tests PRIVATE ob_pcsx2_sio2)
endif()
