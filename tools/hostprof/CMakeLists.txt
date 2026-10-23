# hostprof: external sampling profiler (tools/hostprof/hostprof.cpp). Not part of the engine or the runner build;
# built by `tools/ob.sh hostprof-build` into $OB_ROOT/hostprof-build.
cmake_minimum_required(VERSION 3.20)
project(hostprof LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hostprof hostprof.cpp)
target_link_libraries(hostprof PRIVATE dbghelp winmm)
if(MSVC)
    target_compile_options(hostprof PRIVATE /W4 /permissive-)
    target_compile_definitions(hostprof PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
