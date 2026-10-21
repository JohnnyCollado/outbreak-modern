# Milestone 1: skeleton

## Goal

From the design: "CMake + SDL3 + Vulkan window on Windows and the Odin; guest memory; ISO/AFS
reader." No game code runs yet.

## Done when

- One CMake project builds a Windows `.exe` and an Android APK.
- Both open a window, clear it with Vulkan, and close cleanly.
- Guest memory is reserved and mapped the same way on both, and its tests pass.
- Given your SLPM-65428 image, both list its files and read `SLPM_654.28` and the AFS archives
  byte-identical to a reference reader (the decomp's `tools/iso_list.py`).

## Depends on M0

If M0 picks PS2Recomp or REO, part of this already exists in a different form. PS2Recomp has a
window (raylib, not SDL3), guest memory and an Android runner. M1 then becomes "port what we need
onto that base", and the SDL3/Vulkan choice (design Decision 3) gets reopened. Unknown until M0.

## Work

1. **Repository and build.** A new repo (design question 1, still open) or a folder here. CMake
   for everything; Gradle + the NDK wraps the CMake build for Android.
2. **Platform layer (SDL3).** Window, events, lifecycle (Android pause/resume, surface lost),
   logging to logcat and a file.
3. **Vulkan bring-up.** Instance, device, swapchain, a clear and present on both. Check the device
   has descriptor indexing (needed later for bindless textures, design Decision 6). Log the device
   features.
4. **Guest memory.** Reserve a 4 GiB range, commit 32 MiB of RAM at its base plus the 16 KiB
   scratchpad (0x70000000 in the guest; where it sits in the host range is to be decided), with
   guard pages. `VirtualAlloc` on Windows, `mmap` on Android. Tests: reads and writes in range,
   faults out of range.
5. **ISO 9660 + AFS reader.** Reads the user's image. The design names three AFS archives in the
   game's data (port map, Files). Their names and layout are in the decomp's `fsinit.c`.
6. **Android storage.** How the user picks their image: the Storage Access Framework (a picker
   that hands back a URI, not a file path) or a copy into the app's own storage.

## What's needed

- A Windows PC with a Vulkan driver, or a decision to test Windows only in CI. Whether you have
  one is **unknown**.
- The Odin 3, with your "proceed" for each install and run.
- Android SDK + NDK + CMake for the APK. Where it builds (this container, your PC) is **unknown**.
- Your disc image for the reader test.

## Limitations

- The 4 GiB reservation needs a 64-bit address space. 32-bit Windows and 32-bit Android are out,
  by design.
- SAF gives a content URI, not a path. Reading an ISO through it means random-access reads on a
  file descriptor from `ContentResolver`. Whether those are fast enough for streaming is
  **unknown**: measure.
- Nothing in M1 proves the Odin can run the game. It proves only the platform layer.

## Unknowns

- **SDL3 + Vulkan on the Odin 3's Adreno and its Android version.** Expected to work; untested.
  Which Vulkan version and extensions the Odin's driver reports is **unknown**. Answer:
  `vulkaninfo` or our own feature log on the device (needs your "proceed").
- **Whether Android lets the app reserve 4 GiB of address space.** 64-bit Android normally has
  far more virtual address space, but whether an OEM build limits it is **unknown**: test.
- **Whether the decomp's file list matches every file the game reads.** `cdfile.c`'s list is what
  the decomp found; whether anything is missed is unknown until the game runs.
- **SDL3's Android storage helpers**: the design says SDL3 "covers storage access". Exactly
  which SDL3 functions cover SAF URIs was **not checked**.
