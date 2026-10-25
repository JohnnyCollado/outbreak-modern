# PCSX2's IPU (the EE's MPEG-2 decoder, with its DMA channels 3/4) as a static library, included by
# native/pcsx2_gs/CMakeLists.txt after ob_pcsx2_gs (it shares that library's common/, config stubs, compile options,
# include paths, and the eeHw / cpuRegs globals of stubs/host_stubs.cpp).
# Sources: pcsx2/CMakeLists.txt pcsx2IPUSources + pcsx2IPUSourcesUnshared (one ISA, as the GS).
set(S "${P}/pcsx2")
set(ipu_src
    IPU/IPU.cpp IPU/IPU_Fifo.cpp IPU/IPUdma.cpp
    IPU/IPU_MultiISA.cpp IPU/IPUdither.cpp IPU/yuv2rgb.cpp)
list(TRANSFORM ipu_src PREPEND "${S}/")
# IPU.cpp defines FMVstarted, which ob_pcsx2_gs's host_stubs.cpp already defines for the GS (texture dumping);
# the IPU's copy is renamed so both link (nothing reads the IPU's).
set_source_files_properties("${S}/IPU/IPU.cpp" PROPERTIES
    COMPILE_DEFINITIONS "FMVstarted=ob_ipu_FMVstarted")

add_library(ob_pcsx2_ipu STATIC ${ipu_src}
    "${CMAKE_CURRENT_SOURCE_DIR}/stubs/ipu_stubs.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/adapter/ipu_side.cpp")
target_include_directories(ob_pcsx2_ipu PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
target_link_libraries(ob_pcsx2_ipu PUBLIC ob_pcsx2_gs)

if(NOT ANDROID)
    add_executable(ob_pcsx2_ipu_tests tests/ipu_tests.cpp)
    target_include_directories(ob_pcsx2_ipu_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/adapter")
    target_link_libraries(ob_pcsx2_ipu_tests PRIVATE ob_pcsx2_ipu)
endif()
