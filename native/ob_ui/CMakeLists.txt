# The settings menu (docs/plans/2026-10-26-settings-menu.md), included by native/pcsx2_gs/CMakeLists.txt.
# ob_ui_core: the platform-free part (ini editing, the disc check, the menu trigger, memory card reading, the touch
# controls' pad), std only, so
# it builds and tests without PCSX2 or the runtime.
add_library(ob_ui_core STATIC
    core/ini_file.cpp
    core/disc_check.cpp
    core/game_screen.cpp
    core/menu_trigger.cpp
    core/memory_card.cpp
    core/touch_pad.cpp
    core/widescreen.cpp)
target_include_directories(ob_ui_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/core")
if(MSVC)
    target_compile_options(ob_ui_core PRIVATE /EHsc) # the including directory strips /EHsc for PCSX2
endif()

if(NOT ANDROID)
    add_executable(ob_ui_tests tests/ob_ui_tests.cpp)
    target_link_libraries(ob_ui_tests PRIVATE ob_ui_core)
    if(MSVC)
        target_compile_options(ob_ui_tests PRIVATE /EHsc)
    endif()
endif()

# UI screens (native/ob_ui/ui): the launcher and the in-game menu, Dear ImGui on PCSX2's GS (its imgui, in
# ob_pcsx2_gs) through ob_gs_bridge.h's ob_gs_ui_present; its own file/folder browser (controller-friendly). Linked into ps2x_external_gs (native/pcsx2_gs) with the hook
# that feeds it raylib's input (adapter/ui_hook.cpp). Only with the runner (ps2_runtime) in the same build.
if(TARGET ps2_runtime)
    add_library(ob_ui STATIC ui/settings_ui.cpp ui/file_dialog.cpp)
    target_include_directories(ob_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ui"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../pcsx2_gs/adapter")
    target_link_libraries(ob_ui PUBLIC ob_ui_core PRIVATE ob_pcsx2_gs_adapter)
    if(MSVC)
        target_compile_options(ob_ui PRIVATE /EHsc)
        target_link_libraries(ob_ui PRIVATE comdlg32 shell32 ole32) # file_dialog.cpp: Windows' Browse dialogs
    else()
        target_compile_options(ob_ui PRIVATE -fexceptions)
    endif()
    if(ANDROID)
        # The Storage Access Framework bridge (native/android/java OutbreakActivity; plan section 5.8).
        # The Kotlin side (native/android/kotlin, decision A14): the in-game menu bridge and the launcher JNI.
        target_sources(ob_ui PRIVATE android/android_picker.cpp android/android_menu.cpp)
        target_include_directories(ob_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/android"
            "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
        target_link_libraries(ob_ui PRIVATE zip android log)
    endif()
endif()
