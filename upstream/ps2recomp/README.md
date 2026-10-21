# Changes for PS2Recomp

Fixes made while building on [PS2Recomp](https://github.com/ran-j/PS2Recomp) (GPL-3.0), kept here
as patches until they live in a fork. Each one is meant to go upstream as its own pull request,
and only with your OK. Apply with `git am` on PS2Recomp `eb8688b` or later.

| Patch | What | Tested |
|---|---|---|
| `0001-Enable-SSE4.1-for-GCC-Clang-on-x86-64.patch` | Linux (and any GCC/Clang x86-64) builds failed: SSE4.1 intrinsics without the target enabled. Adds `-msse4.1` next to the existing ARM flags. | 2026-10-21, Ubuntu 24.04: GCC 13.3 and Clang 18.1, Release. `ps2x_tests` 445/445, GS cache tests and `runELFtests` pass on both. Android arm64 (NDK r27c) unaffected. |
