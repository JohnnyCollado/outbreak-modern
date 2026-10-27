# PCSX2's DEV9 (the expansion bay) with its SMAP Ethernet and the "Sockets" network adapter (a user-mode NAT with its
# own DHCP and DNS servers) as a static library, included by native/pcsx2_gs/CMakeLists.txt after ob_pcsx2_gs (it
# shares that library's common/, config stubs, compile options and include paths), as native/pcsx2_sio2.
# Sources: pcsx2/CMakeLists.txt pcsx2DEV9Sources. Online play plan: docs/plans/2026-10-26-online-play.md (P1, P2).
set(S "${P}/pcsx2")
file(GLOB_RECURSE dev9_src RELATIVE "${S}" CONFIGURE_DEPENDS
    "${S}/DEV9/ATA/*.cpp" "${S}/DEV9/InternalServers/*.cpp" "${S}/DEV9/PacketReader/*.cpp" "${S}/DEV9/Sessions/*.cpp")
list(APPEND dev9_src DEV9/DEV9.cpp DEV9/smap.cpp DEV9/net.cpp DEV9/sockets.cpp DEV9/AdapterUtils.cpp DEV9/flash.cpp)
# Left out: DEV9/ATA/HddCreate.cpp (the HDD image maker, used by PCSX2's settings UI only), DEV9/pcap_io.cpp and
# DEV9/Win32/ (the PCAP and TAP adapters: stubbed in stubs/dev9_stubs.cpp; only Sockets is used). The ATA files stay:
# DEV9.cpp holds an ATA object, never opened (no HDD).
list(REMOVE_ITEM dev9_src DEV9/ATA/HddCreate.cpp)
list(TRANSFORM dev9_src PREPEND "${S}/")

add_library(ob_pcsx2_dev9 STATIC ${dev9_src}
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs/dev9_stubs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter/dev9_side.cpp")
target_include_directories(ob_pcsx2_dev9 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter" "${S}/DEV9")
target_link_libraries(ob_pcsx2_dev9 PUBLIC ob_pcsx2_gs)
if(WIN32)
    target_link_libraries(ob_pcsx2_dev9 PUBLIC ws2_32 iphlpapi)
endif()

if(NOT ANDROID)
    add_executable(ob_pcsx2_dev9_tests tests/dev9_tests.cpp)
    target_include_directories(ob_pcsx2_dev9_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
    target_link_libraries(ob_pcsx2_dev9_tests PRIVATE ob_pcsx2_dev9)
endif()
