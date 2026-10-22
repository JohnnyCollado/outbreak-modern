# Harness and build gotchas

## Windows / MSVC
- `<windows.h>` defines `min`/`max` macros: write `(std::max)(a, b)` or define `NOMINMAX` (T2, TempProfiler.cpp).
- Never allocate, lock or print between `SuspendThread` and `ResumeThread`: the suspended thread may hold the heap
  lock and the sampler deadlocks (reports/2026-10-21-pa3-groundwork-and-boot-speed.md, profiler struggle).
- MSVC 260-char path limit: build trees stay at C:\ob\t, C:\ob\r; `tools/ob/vc.bat` also sets git core.longpaths.
- `tools/ob/vc.bat`'s path must contain no spaces (`cmd /c` quoting from Git Bash).
- ps2x_tests must run with cwd `C:\ob\src\ps2xTest` (`ob.sh test` does this).
- PowerShell `Start-Process -PassThru`: touch `$p.Handle` before `WaitForExit`, or `ExitCode` is null (T4).

## Repo / PS2Recomp
- Upstream `PS2X_ENABLE_AGRESSIVE_LOGS` defaults ON: always configure the runner OFF (ob.sh does).

## Boots and frames
- Native presented frame is 512x448 (first two ticks 640x448, blank before the mode set).
- Plain `ob.sh boot` (no script) dumps every presented frame (~33 PNG/s, ~50 MB/min) and opens a visible window that
  any stray ESC or close click ends. Scripted boots (`--script`) are hidden and dump only in `capture` steps.
- Capture takes the next n distinct latched ticks; the host loop can skip a tick, so a capture may land one tick
  later between runs (17/18 identical in T4).

## PCSX2 debugger (ps2_emu/, memory note pcsx2-debugging-setup)
- Launch `ps2_emu/pcsx2-qt.exe` with PowerShell `Start-Process`; computer-use access is for process `pcsx2-qt.exe`
  (the name "PCSX2" resolves to the Program Files copy). Launching it needs a developer proceed per action.
- Clicks are refused while the agent's own window has focus: `open_application("Pcsx2-qt")` first.
- Breakpoints work only once a VM is running (boot, pause, Breakpoints tab -> New: Execute; "Log When Hit" +
  "Continue on Hit" to count).
- The HITS column lags; the system console (Tools -> Enable System Console) is the truth.
- IOP memory breakpoints on hardware registers (0x1F80xxxx) never fire; IOP Modules/Stack tabs are empty.
- Logging every hit of a hot loop slows emulation massively: disable such breakpoints before timing anything.
- Its EE Threads tab is the authority for kernel-behaviour questions (real BIOS, real IOP modules).

## SDK on PATH shadows a DLL the build forgot to stage
- A test exe that loads a DLL at run time (PCSX2's GS loads `shaderc_shared.dll`) can pass even when the DLL was
  never staged beside it, because the Vulkan SDK's `Bin` folder is on PATH and ships the same-named DLL. The test
  then proves nothing about the shipped layout and may load a different version.
- Run with the SDK stripped from PATH (`tools/ob.sh` does this for the smoke test and boots). Prove it once with a
  negative control: remove the staged DLL, expect "Failed to compile utility pipelines" and exit 1; restore it.
- Shipped runners stage the DLL beside the exe; never rely on PATH. (Found in phase 1.3 T7.)
