# OB_PROFILE=1 build mode (tools/ob.sh recompile): debug info for the host code only, so an external sampling
# profiler (tools/hostprof) can name functions in ps2_runtime, PCSX2's GS and microVU. Injected into PS2Recomp's
# configure through CMAKE_PROJECT_INCLUDE (no PS2Recomp or PCSX2 source edit).
#
# - Compile: /Z7 (debug info inside each .obj, no PDB server) on the non-game static libraries only. Their
#   optimisation flags are unchanged (/Z7 does not change code generation). The generated game units
#   (ps2EntryRunner's Unity/ objects and its PCH) keep their flags, so toggling the mode rebuilds none of them.
# - Link: ps2EntryRunner gets /DEBUG:FULL plus /OPT:REF /OPT:ICF, because /DEBUG alone turns those two off and the
#   exe's code layout would differ from a normal build. Game objects carry no debug info; the PDB still lists their
#   public (external) function names.
# - raylib is left out: its build tree is shared with the tools tree (FETCHCONTENT_BASE_DIR=$OB_T/_deps).
#
# CMAKE_PROJECT_INCLUDE runs after every project() call; the guard keeps one deferred call, which runs at the end
# of the top-level CMakeLists.txt, after every target exists.
include_guard(GLOBAL)

function(_ob_profile_apply)
    set(_done "")
    foreach(_t ps2_runtime ps2_iop ob_pcsx2_gs ob_pcsx2_gs_adapter ps2x_external_gs ob_pcsx2_vu ps2x_external_vu1)
        if(TARGET ${_t})
            # Not to ml64 (ASM_MASM sources in the PCSX2 libraries): it has no /Z7.
            set_property(TARGET ${_t} APPEND PROPERTY COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:C,CXX>:/Z7>")
            list(APPEND _done ${_t})
        endif()
    endforeach()
    if(TARGET ps2EntryRunner)
        set_property(TARGET ps2EntryRunner APPEND PROPERTY LINK_OPTIONS /DEBUG:FULL /OPT:REF /OPT:ICF)
    endif()
    message(STATUS "OB_PROFILE: /Z7 on ${_done}; ps2EntryRunner linked with /DEBUG:FULL /OPT:REF /OPT:ICF")
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _ob_profile_apply)
