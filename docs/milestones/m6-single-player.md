# Milestone 6: single player end to end, then the beta

## Goal

From the design: "Single player end to end (File #1 scenario offline), Modern Camera built in."
The beta opens here. Interpolated rendering, resolution scale and widescreen also come here
(design Decision 6, "Order").

## Done when

- Every File #1 offline scenario can be played from start to end on both platforms. File #1's
  offline scenario list, and whether "end to end" means one scenario or all of them, is **to
  decide** (the design says "File #1 scenario", singular).
- It runs at full speed (30 Hz logic, no dropped ticks) on the Odin and on a reference PC.
  "Full speed" needs a measurement method: frame-time logs over a fixed route.
- The Modern Camera mod is built in and passes the checks `mod/test_mod.py` makes today.
- Beta builds reach testers **in a way that doesn't distribute game code** (below).

## Depends on M4, M5

## Work

1. **Play-through testing.** Scripted input (as REO does) for repeatable routes, plus manual play.
   Keep a list of every difference from PCSX2.
2. **Modern Camera.** The mod is written as reads and writes of game addresses (`MEM(type, addr)`
   in `mod/`). With guest memory at `base + addr` it should compile unchanged (design Decision 2;
   **unverified**). Today it hooks the game by patching MIPS code (`mod/stub.S`, `sites.json`). In
   a recompiled build those hooks become calls in the generated code, or overrides of whole
   functions. That needs a mechanism in the recompiler (M0/M2).
3. **Presentation options** (Decision 6): interpolation between 30 Hz ticks, internal resolution,
   widescreen, the 4K pack. All off by default, and off online unless shown to be visual only.
4. **The distribution problem.** The decomp's LEGAL.md rule 1 forbids sharing binaries that
   contain game code. Options, none tested:
   - **Users build it themselves**, as REO does on Windows (Visual Studio, CMake, Python; ~15 GB;
     20-40 minutes). That works for Windows. For Android:
     - build the APK on a PC with the NDK and sideload it (`adb install`). Needs a PC and some
       technical skill from each tester;
     - build **on the device**: ship the recompiler and a C compiler inside the app (clang for
       Android exists, e.g. in Termux). Whether a build of this size can finish on the Odin, and
       in what time, is **unknown**.
   - **Ship no game code, translate at run time.** That is a JIT: the emulator approach the design
     rejected.
   - **Ship it anyway.** Breaks the decomp's rule and goes past the tolerance the project relies
     on. Not proposed.
5. **The beta itself** (design, "Beta testing"): testers across hardware tiers, an in-game report
   (device, GPU/driver, settings, frame times, memory, the last seconds of log).

## What's needed

- A decision on point 4. Without it there's no beta.
- Testers with the right hardware. Who and how many is **unknown**.
- HeySnippy's position on a native Modern Camera. The feasibility report found the original
  camera logic "closed source", so a native port needs "the author's permission or collaboration,
  or a clean-room rewrite". `mod/` is described as our own code. Whether that clears it is a
  question for you, not a fact I can check.

## Limitations

- **The hardware tiers in the design are estimates.** M6 is where they get measured. Expect them
  to change.
- **The camera mod's online status**: Obsrv bans cheats (decomp NETWORK.md). The camera may be
  presentation only. Aiming from the camera direction may count as a gameplay change. To ask the
  Obsrv admins before M7.
- **File #2 is out of scope** for M6. Design and decomp both start with File #1.

## Unknowns

- **Full-speed performance on the Odin.** Nothing measured before M2. If it's too slow, the
  options (VU1 shaders, SIMD helpers) are in Decision 6, with unknown gains.
- **Android build on the device** (point 4). Untested anywhere in this project.
- **Thermal behaviour on the Odin** during long sessions. Measured only in M6 (ADPF and frame
  pacing are in the design).
- **Whether the design's interpolation is safe.** It needs the previous and current transform of
  every object and the camera. Where the game keeps them, and whether it keeps the previous one
  at all, is not known.
