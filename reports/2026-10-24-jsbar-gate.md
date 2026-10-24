# J's Bar golden frames for the gate (M4)

- **Date:** 2026-10-24
- **Who:** the developer (PCSX2 reference shot), one Claude Code session
- **Goal:** M4's "first room matches PCSX2 golden frames": a J's Bar reference from PCSX2 and the gate extended to
  compare our runner's J's Bar against it under the frozen tolerance.
- **Outcome:** the gate has sets (`gate/sets.json`: `menus`, `jsbar`), per-screen excluded rectangles (the two window
  zombies, approved by the developer), `refs.py`/`gate.py --set`, and `ob.sh gate` runs the J's Bar set once its
  references exist; menus unchanged (selftest 9/9, synthetic cases as before). One PCSX2 shot so far: the room
  matches our runner almost pixel for pixel. **Not gate-ready:** the runner's scene does not replay the same way run
  to run (dialogue and a character's entrance move by seconds), so the references are not taken in yet.
- **Versions:** PCSX2 2.9.134 (Software renderer, the reference settings, `refs.py check-settings` OK); runner from
  `C:\ob\up` HEAD 330abdc (patches to 0053), `PS2X_GS=pcsx2-sw PS2X_VU1=mvu`, paced.

## Summary

The developer's shot (`ps2_emu/snaps/...20261024171855.png`, 512x448, taken ~4 s into the scene) against our runner
frames of the same scene, gate metric (pixels with any channel differing by more than 8): best whole-frame share
0.23% (limit 0.01%); with the two window rectangles excluded, 12 pixels differ (share 0.0072-0.0077%) in two of the
three older runs (`frames-jsbar1`, `frames-jsbar4`), the room, Kevin, the wounded man, gun and tin identical
(`C:\ob\ref-jsbar-compare.png`). Two fresh runs on today's build (`frames-jsdet1`, `frames-jsdet2`) do not match: in
jsdet1 the first subtitle is already typing at the matching instant, in jsdet2 Cindy walks in and two lines play in
the first 3-5 s (`C:\ob\logs\jsdet2-strip.png`). The scene's events run at a different pace each run, which no
excluded rectangle can absorb.

## Struggles

### 1. The old exclusion boxes hid Kevin

- **Seen:** the boxes from the earlier determinism study ((110,50)-(330,175) for the centre window) cover Kevin's head.
- **Found by:** a motion map over 3 runs (pixels whose value varies > 8 across frames 70-500): the centre-window zombie
  moves in x 184-238, y 100-160, Kevin's idle in x 244-318 from y 130.
- **Fix:** tight rectangles (0,95)-(35,175) and (175,100)-(245,170) (3.4% of the frame); Kevin stays compared.
- **Evidence:** `gate/sets.json` `exclude_why`; with them jsbar1/jsbar4 best share 0.0072%/0.0077%.

### 2. The runner's J's Bar scene is not deterministic (open)

- **Seen:** against the PCSX2 shot, with the windows excluded: jsbar1, jsbar4 match (0.007%); jsbar2 does not (the
  wounded man in another pose, 815 pixels, 0.37%); fresh jsdet1 (0.11%: a subtitle typing) and jsdet2 (0.27%: Cindy
  already in the room, lines "Maybe I had too much to drink..." and "I'm worried about Bob." within +200..+300 ticks).
- **Found by:** `gate.metric` over every frame of each run (scratch scripts), then looking at the frames.
- **What is known:** Kevin's idle animation is locked to the scene start (the PCSX2 shot matches the frame ~+250
  ticks after mode 2 in jsbar1, jsbar4 and jsdet1); the anchor (GAME mode 2) holds. The dialogue start and the
  entrance vary (first line at +248, ~+700, +1102, none by +2136 in older runs; before +251 in jsdet1; before +200 in
  jsdet2). Entry 28 of `reports/2026-10-23-gameplay.md` guessed the voice ADX streams (IOP, read in host time) pace
  the dialogue. **Unverified**; the entrance in jsdet2 suggests the event script itself waits on something
  host-timed.
- **Not done:** root cause and fix; until then the J's Bar references are not taken in (intake would make the gate
  red at random).

### 3. Editing ob.sh while a boot ran under it

- **Seen:** `tools/ob.sh: line 511: rt: unbound variable` after `boot jsdet2: exit 0`.
- **Cause:** bash reads a script as it runs; `cmd_gate` was edited while the jsdet loop's `ob.sh boot` was running,
  so after the boot returned bash continued at a shifted offset. The boot itself was complete (frames intact).
- **Take-away:** do not edit `tools/ob.sh` while a run of it is in flight.

### 4. Backslashes through heredocs (again)

- **Seen:** `C:\ob\ref` written as `C:\ob<CR>ef` in `docs/ops/reference-capture.md`.
- **Fix:** a byte replace from a script file. Same trap as in `reports/2026-10-24-vu1-aot-emitter.md` struggle 4.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| (to the developer) with the windows excluded, a boot-b shot is all that is missing | the fresh runs show the scene's pacing itself varies | jsdet1/jsdet2 against the shot | this report, struggle 2 |

## Still open

- Make the J's Bar scene replay identically (struggle 2), then take boot b (a shot as soon as the fade-in completes),
  run `refs.py intake --set jsbar`, and run `ob.sh gate`.
- Android and Linux runs of the scene (M4's "both platforms").

## Useful for other developers

- `gate/sets.json` + `gate.py/refs.py --set`: a new golden-frame set is a script, a reference folder and an entry.
- A motion map (per-pixel max-min over a run's frames) shows exactly what moves in a fixed-camera scene.
