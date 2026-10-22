# TASK_PROGRESS — T4 attempt 1

Task: T4 scripted input and hidden window, patch 0020
Attempt: 1   Agent: pa-expert   Ctx at handoff: ~70k   Commit: (this handoff commit)
Reason: harness forced hand-back while coder T4.c1 still runs in background (as in T3).

## Done so far
- Research adopted: R1.1-006 (wrong: said decomp-src missing), R1.1-007 (title/boot task), R1.1-008 (offsets, overlay ids, select steps).
- Own asm reads (C:\ob\splat\SLPM_654.28\en\main\text.s): task pool D_002F85D0, 0x20/slot (func_001B65D0 text.s ~B67D0); boot task func_001BAC60 created in slot 0 (text.s:225466-225469) -> struct 0x002F85D0; step u8 @+8 (func_001BAC60 `lbu a1,0x8(a0)` text.s:227523), sub u8 @+9 (R1.1-008). Logo phases: step==1 && sub 7/8/9 (boottask.c:118-159, func_001BAD50 arg 3,1,2; each 0x80 vsyncs). Which logo = ACCESS/CAPCOM unknown -> discovery boot.
- ob.sh `boot --script F` written (hidden, PS2X_INPUT_SCRIPT, frames-<tag>, returns runner exit code, PS2X_AUTO_PRESS removed), bash -n ok, NOT committed: saved as TASK_PROGRESS.patch.
- Discovery script generator: phase-ends/current/logs/T4.gen_disc.py (copy of .run/1.1/t4/gen_disc.py).

## In flight
- Coder T4.c1 (pa-coder, agentId abd4de0ed16569771) running: patch 0020 in C:\ob\up (parser/stepper ps2_input_script.{h,cpp}, readState script-only, vsync-stepped, capture gating via new ps2DumpPresentedFrameNamed, hidden flag + log `[run] window hidden=`, quit exit 0, timeout exit 3 `[script] TIMEOUT step i line L`, parse error exit 2, `watch` step, ps2xTest tests), format-patch 0020 + README row, sync src==up, ob.sh tools/test/recompile. Log phase-ends/current/logs/T4.c1.md. Respawn: do NOT touch C:\ob\up/src or upstream/ps2recomp until its report/commit `T4.c1` appears (`git log --grep T4.c1`).
- tools/ob.sh: patch in TASK_PROGRESS.patch (git apply, delete patch).

## Hypotheses rejected
- "C:\ob\decomp-src missing" (R1.1-006) — it exists as a flat folder of .c files (boot.c, boottask.c, title.c, hddwork.c, overlay.c...), no git.

## Current hypothesis
Screens map to: ACCESS/CAPCOM = boot task 0x002F85D8 (u8 step=1) / 0x002F85D9 sub in {7,8,9}; title PRESS START = *0x0023BE34+0 state 0 sub 2; save-data/memory-card prompt = some TitleWork state/sub/step between PRESS START and main menu; main menu = state 1 sub 5; char select = title.c:2382 "step 9", scenario select step 11, difficulty step 10 (field/state unconfirmed). One discovery boot with watches confirms.

## Next 5 steps
1. Wait for T4.c1 report; verify readState/hidden code by reading it (§5); check grammar matches T4.gen_disc.py syntax.
2. `git apply` TASK_PROGRESS.patch; generate disc script (`PY .run/1.1/t4/gen_disc.py .run/1.1/t4/disc.script`); hidden boot `bash tools/ob.sh boot --script .run/1.1/t4/disc.script --tag t4disc` (background); confirm log `[run] window hidden=1` early, else kill by pid and return question.
3. Map watch transitions (grep `[script] watch` in C:\ob\logs\boot-t4disc.log) to captures d#### ticks; view a few PNGs to identify screens; write gate/boot.script (waits on guest state + `wait vsync` settle, captures access capcom title savedata mainmenu charselect, n=3 each, Left then Circle at memory-card prompt, quit after charselect).
4. Two runs from a rebuilt runner: tags t4a, t4b; per-screen capture begin ticks from `[script] capture` lines; byte-compare frames-t4a vs frames-t4b (cmp per file).
5. Card Run line (HOW_WE_WORK.md Build/run/test: boot --script, hidden; auto-press gone), README 0020 row check, suites, log+summary, plan_edit set T4 done.

## Gotchas
- Native frame 512x448; first ~2 frames 640x448 blank: captures only after a guest-state wait.
- Boot timing (T2 t2d): char select ~+1:39, Difficulty +2:11; first ~1200 vsyncs slow. gate script timeout ~420 s.
- TitleWork* never cleared at task end (title.c:95-98): pointer predicates valid only while DEMO title task live.
- R1.1-008 caution: func_00540BB0 sets state 2 sub 5, not main menu.
- Any VISIBLE boot needs developer ask -> return question.

## State to carry verbatim
- Coder agentId abd4de0ed16569771. Reports: R1.1-006, R1.1-007, R1.1-008.
- Addresses: boot task 0x002F85D0 (step @+8, sub @+9); TitleWork* @0x0023BE34 (state s8 +0, sub +1, step +2, unkC cursor +0x0C, sel s16 +0x204); overlay id 0x00239F30 s8; D_0023BBB0 update-check; pad pressed 0x003064D0.

## Coder runs so far
- T4.c1 opus55 running (patch 0020 + tests + recompile).
