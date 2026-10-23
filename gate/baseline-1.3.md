# Gate baseline, phase 1.3 (2026-10-22)

Today's build scored by the frozen gate, with both GS backends, plus frame time per backend. This is the starting
point for the next phase. Previous baseline: `gate/baseline-1.2.md`.

## Build and run
- Runner source: `C:\ob\up` HEAD `00e03cd` = upstream `eb8688b` + `upstream/ps2recomp/0001`-`0029`.
- PCSX2 GS: v2.9.134 (`a81f1aa`), in-process via `native/pcsx2_gs` (no PCSX2 patches).
- Runner exe: `C:\ob\r\ps2xRuntime\ps2EntryRunner.exe`; gate stamp
  `0c0395ea24a50562d7eb239891b4780ef8d363174bcccea4207cce60ccbb0a15` (both runs: `gate: not recompiled`).
- Repo HEAD at run: `14b1fde`. Tolerance unchanged since its freeze (`gate/tolerance.json`, d 8, p 0.0001).
- Commands: `PS2X_GS=pcsx2-vk bash tools/ob.sh gate`, `PS2X_GS=cpu bash tools/ob.sh gate`. Selftest 9/9 ok both.
  Logs: `.run/logs/b13-gate-pcsx2-vk.log`, `.run/logs/b13-gate-cpu.log`.

## Gate result

Share = fraction of pixels outside the tolerance against the PCSX2 reference (lower is better).

| Screen | 1.2 baseline (cpu) | 1.3 cpu | 1.3 pcsx2-vk | Change (vk vs 1.2) |
|---|---|---|---|---|
| savedata | 0.043597 | 0.042668 | 0.003353 | -92% |
| access | 0.248753 | 0.050319 | **0.000000 GREEN** | green |
| capcom | 0.082450 | 0.018507 | 0.001251 | -98% |
| title (advisory) | 0.999865 | 0.998744 | 0.995361 | movie background |
| mainmenu (advisory) | 0.998910 | 0.996238 | 0.989694 | movie background |
| charselect (advisory) | 0.891462 | 0.694545 | 0.606197 | movie background |

Gated green: cpu 0/3, pcsx2-vk 1/3, **pcsx2-sw 3/3 (savedata, access, capcom all 0.000000, max delta 0)**
(`.run/logs/sc-gate-pcsx2-sw.log`, same build plus the shader cache). The vk remainder is PCSX2's hardware
renderer differing from its own software renderer, which made the references. Decision (2026-10-23): the gate
judges correctness with `PS2X_GS=pcsx2-sw`; `pcsx2-vk` is the display path. What changed since 1.2: the PCSX2 GS (patches 0021-0024, 0026) and the VIF1
IMAGE-continuation fix (0025), which corrected glyph and CLUT uploads for both backends
(`reports/2026-10-22-pcsx2-gs.md`, T12). The advisory screens are dominated by Sofdec movie backgrounds (out of
scope until the movies milestone).

## Frame time

Hidden `gate/boot.script` boots with `PS2X_FRAME_TIME` (patches 0027 + 0029). Busy = work per frame, excluding the
scheduler's idle wait. Budget 16.68 ms (59.94 Hz). CSVs: `C:\ob\logs\ft29-<gs>[-2].csv`.

| Run | vsyncs | boot s | busy median ms | p95 | p99 | max | frames over budget |
|---|---|---|---|---|---|---|---|
| pcsx2-vk run 1 | 2711 | 45.2 | 4.89 | 14.42 | 33.86 | 1042.7 | 3.4% |
| pcsx2-vk run 2 | 2713 | 45.2 | 4.81 | 13.55 | 38.02 | 3070.0 | 4.1% |
| cpu run 1 | 2740 | 47.1 | 9.77 | 41.88 | 49.30 | 73.1 | 35.2% |
| cpu run 2 | 2758 | 47.7 | 10.57 | 45.05 | 53.16 | 80.7 | 42.1% |

- PCSX2 Vulkan does about half the work per frame of the CPU GS and goes over budget about a tenth as often.
- The Vulkan max is one frame per boot, tick 2: PCSX2's GS opening and compiling its shaders on first use
  (`native/pcsx2_gs/adapter/pcsx2_side.cpp:88`, `DisableShaderCache = true`). It varied 1.0-3.1 s between runs.
  Start-up cost, not a stall. Since 2026-10-23 the shader cache is on (`C:\ob\ps2xRuntime\cache`): tick 2
  drops from 1,083 ms (cold) to 325 ms (warm) (`C:\ob\logstsc-*.csv`).
- Since patch 0026, the CPU GS does no drawing when PCSX2 is selected (decode only), so these numbers are PCSX2's
  real cost, not both renderers added together.

## Build times (same day, for reference)
- `ob.sh recompile` with unchanged generated code: ~16-23 s. After a runtime-internal change: 8-10 s (patch 0028;
  `bash tools/ob.sh depcheck`: 0 game units rebuilt). Full rebuild (recompiler or game-facing header change): ~6 min.
- `ob.sh test`: 49 s after a runtime change (was 841 s when it rebuilt the whole tools tree).
