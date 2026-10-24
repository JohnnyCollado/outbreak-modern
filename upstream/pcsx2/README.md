# Changes for PCSX2

Our edits to [PCSX2](https://github.com/PCSX2/pcsx2) (GPL-3.0), used only for its GS, built in-process as the
static library `ob_pcsx2_gs` (`native/pcsx2_gs/`, docs/ops/gs.md). Base: tag `v2.9.134` =
`a81f1aa39fc56ef60dbc913637c74c49c1792033`, pinned (the gate refs were made by this GS; moving it means moving the
refs). Clone at `C:\ob\pcsx2` (`bash tools/ob.sh pcsx2-gs` clones it when missing). Apply in order with `git am` on
a81f1aa. Procedure as for PS2Recomp (docs/ops/workspace.md:31-37): commit on a local branch in `C:\ob\pcsx2`,
`git format-patch` here, add a row. A stub in our code is preferred over an edit here.

| Patch | What | Why it could not be a stub |
|---|---|---|
| 0001 | **GS/Vulkan: Android surface and unversioned shaderc.** `WindowInfo::Type::Android` (window_handle = `ANativeWindow*`, appended last); `VK_USE_PLATFORM_ANDROID_KHR` on `__ANDROID__`; `vkCreateAndroidSurfaceKHR` entry point; `VK_KHR_android_surface` required for an Android window; `VKSwapChain` creates the surface from the window; `libshaderc_shared.so` loaded without a version suffix on Android (an APK carries only `lib*.so`). All under `__ANDROID__`; Windows unchanged (gate 3/3 after it). | The surface type switch is inside `VKSwapChain::CreateVulkanSurface` and `GSDeviceVK::SelectInstanceExtensions`; the library name is a literal in `VKShaderCache.cpp:142-147` |
| 0002 | **GS/Vulkan: Android host-preloaded libvulkan handle.** `Vulkan::SetPreloadedVulkanLibrary(void*)` (VKLoader.h/.cpp, `__ANDROID__` only): when set, `LoadVulkanLibrary` takes the module entry points (`vkGetInstanceProcAddr` and friends) from the host's handle with `dlsym` instead of opening the system `libvulkan.so`; `UnloadVulkanLibrary` never closes it (the host owns it; PCSX2 re-loads Vulkan for every device it creates and for adapter enumeration); `nullptr` returns to the system library; a handle missing an entry point falls back to the system library. Used by `native/pcsx2_gs/adapter/gpu_driver_android.cpp` for a custom driver opened through libadrenotools (docs/ops/android.md "GPU driver"). Windows unchanged. | The library is opened inside `Vulkan::LoadVulkanLibrary` (`VKLoader.cpp:46-88`) from a file-static `DynamicLibrary`; nothing outside VKLoader.cpp can supply the handle. ARMSX2 (GPL-3.0) puts the whole adrenotools call in the same function; we keep it in our adapter and patch only the hand-off |
