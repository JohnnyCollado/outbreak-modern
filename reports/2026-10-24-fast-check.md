# The fast J's Bar check (patch 0051, `ob.sh check`)

- **Date:** 2026-10-24
- **Who:** the developer, one Claude Code session
- **Goal:** cut a J's Bar check from ~150 s to under a minute or so, so every later item verifies faster.
- **Outcome:** `bash tools/ob.sh check` runs `gate/check.script` (play.script up to the `load90s` capture) unpaced
  in **63 s** (paced: 140 s). Capture ticks agree with the paced run within 1 (6449 / 8253 vs 6448 / 8252) and
  repeat exactly unpaced; gate 3/3. PS2Recomp 0051 (`C:\ob\up` `f5ffc3a`); ob_diag capture hold; scripted boots
  use `gate/defaults.ini`.
- **Versions:** PS2Recomp `C:\ob\up` 0050 + 0051; Windows 11, MSVC 19.44.

## Summary

Most of a J's Bar run was the runtime pacing vsyncs to the wall clock while the game sat idle (logos, menus,
loading). `PS2X_PACING=0` lets guest cycles alone decide when vsyncs and timers fire and skips the idle sleep;
menus and loading then run at 150-1400 vsync/s, J's Bar at 65-75 (game thread bound). The first unpaced run
drifted +66 ticks; the cause was the script's capture steps, not the game, and a plugin-side hold fixed it.
A save state (jumping straight into J's Bar) was not attempted: the recompiled game runs on host threads with
host stacks, so its state cannot be written out and restored without a large redesign (unverified estimate).

## Struggles

### 1. Unpaced, the captures drifted +66 ticks

- **Seen:** `fast1`: charselect at 2579 as paced, then +21 at charsel-after, +43 at gameload, +66 at load60s and
  load90s (paced `iopl-after2`: 6454 / 8259).
- **Found by:** per-capture ticks: the drift grew only at script steps with captures and stayed flat through long
  `wait vsync 1800` stretches, so the game's timing was not changing. `capture charselect 6` (six frames) gave +21.
- **Cause:** a capture needs a frame per tick, taken by the main thread at host-frame rate; unpaced, the guest ran
  several vsyncs per host frame meanwhile, so each capture frame cost several ticks.
- **Fix:** ob_diag holds the guest in `onVsync` while a capture step still needs a frame (2 s cap,
  `Ps2InputScript::captureNeedsTick`).

### 2. The hold waited for a frame that could only come after it

- **Seen:** `fast2`/`fast4`: 110 s and 84 s, every capture frame after the first a 2 s timeout
  (`[script] capture hold at tick N timed out`).
- **Cause:** the scheduler publishes vsync N's snapshot after the plugin's `onVsync(N)`, on the same thread
  (`EeScheduler.cpp` VBlankStart: `m_diag->onVsync`, then `snapshot`/`publishExternalFrame`); holding at N for frame N
  blocks its own frame.
- **Fix:** at vsync N, wait for frame N-1 (never at the step's first vsync). `fast5`: 63 s, no timeout, captures
  on consecutive ticks (paced captures skip a tick now and then: 2580, 2581, 2582, 2584...).
- **Take-away:** read the producer's order before adding a wait on it.

### 3. Play settings would leak into scripted runs

- **Seen:** the developer's `outbreak.ini` beside the exe (upscale, textures, mods) applies to every run of that
  exe, gates included.
- **Fix:** `ob.sh boot --script` sets `PS2X_SETTINGS=gate/defaults.ini` (no keys) unless it is already set.

## Measurements (hidden, pcsx2-vk, mvu)

| run | pacing | wall | load60s / load90s tick | capture hold timeouts |
|---|---|---|---|---|
| `iopl-after2` (play.script) | paced | ~150 s to load90s | 6454 / 8259 | (no hold yet) |
| `fast1` | off | 62 s | 6520 / 8325 | (no hold yet) |
| `fast2` | off | 110 s | 6443 / 8246 | every frame (held at N) |
| `fast5`, `fast6` | off | 63 s, 63 s | 6449 / 8253 both | 0 |
| `paced1` (`check --paced`) | paced | 140 s | 6448 / 8252 | 0 |

`load90s` images: paced1 vs fast6 differ in 0.9% of bytes, fast5 vs fast6 in 0.6% (the scene is not deterministic
run to run, `reports/2026-10-23-gameplay.md` entry 28); fast6 shows the same J's Bar frame. Gate `pcsx2-sw` 3/3 with
the hold, 0 timeouts. Tests: ps2x 530/530 (+1), ob_diag 23/23 (+1).

## Still open

- Under a minute needs more: J's Bar itself is game-thread bound unpaced (~70 vsync/s), so the 1800-vsync waits
  between load30s and load90s cost ~50 s. Shorter checks for a given change can stop at `load60s`.
- A real save state was not attempted (see Summary).
