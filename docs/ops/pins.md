# Pins

Source: R0-003, upstream/ps2recomp/README.md, tools/ob.sh; 2026-10-21.

- OS: Windows 11. MSVC 19.44 / VS 2022 Community (vcvars64 via `OB_VCVARS`), CMake 3.30, Ninja.
- MSYS2 GCC 16.1 + gdb 17.2 (C:\ob\g, probes only).
- Python: `C:/Users/johnn/AppData/Local/Python/pythoncore-3.14-64/python.exe` (3.14.5).
- PS2Recomp upstream `eb8688b` + `upstream/ps2recomp/0001-0020` (`git am`). Decomp: Outbreak-decomp `eb6af38`. Policy: track upstream HEAD; check at each phase start, rebase our patches when it moves; pin only for a bug that breaks it (developer, 2026-10-22). Checked 2026-10-22: HEAD is still eb8688b.
- Runner configure (tools/ob.sh): RelWithDebInfo `/O2 /Zi /DNDEBUG`, STUDIO/TEST/ANALYZER/RECOMP/FFMPEG/DEBUG_UI OFF,
  `-DPS2X_ENABLE_AGRESSIVE_LOGS=OFF` always (upstream default ON; std::cerr traces halved boot speed:
  reports/2026-10-21-pa3-groundwork-and-boot-speed.md struggle 5).
- Oracle: PCSX2 2.9.134 portable in `ps2_emu/` (git-ignored) with BIOS `ps2-0200j-20040614` and the English v2.1 iso.
- Game: SLPM-65428 v1.04 + English patch v2.1.
- Linux/Android (not re-run this generation): GCC 13.3, Clang 18.1 (last ran at 0006, 450/450), Ubuntu 24.04, NDK r27c API 29.
