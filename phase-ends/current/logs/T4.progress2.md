# TASK_PROGRESS — T4 attempt 2

Task: T4 scripted input and hidden window, patch 0020
Attempt: 2   Agent: pa-expert   Commit: (this handoff commit)
Reason: harness forced hand-back while coder T4.c2 runs (2nd time; see progress1).

## Done so far
- progress1 archived (logs/T4.progress1.md). T4.c1 done: up ee22992, repo 2058e9a, ps2x_tests 470/470, iop 4/4, gs 45/45, runner linked. Grammar/exit codes: logs/T4.c1.md.
- Hidden verified by code (ps2_runtime.cpp ~786-797: SetConfigFlags(RESIZABLE|HIDDEN) before InitWindow, SetExitKey(KEY_NULL)) AND log: C:\ob\logs\boot-t4disc.log `[run] window hidden=1 flags=0x84`.
- Discovery boot t4disc (hidden, .run/1.1/t4/disc.script from logs/T4.gen_disc.py, c1 runner): exit 0 by itself at quit (tick 16139); 256 captures C:\ob\frames-t4disc; contact sheets .run/1.1/t4/s1.png s2.png (sheet.py).
- Screen map (watch log, `grep "\[script\] watch" C:\ob\logs\boot-t4disc.log`):
  - boot task u16 @0x002F85D8 (byte0 step, byte1 sub): 0x401 ticks 56-750 = SAVE-DATA / memory-card prompt ("no data ... start anyway? Yes/No") — comes FIRST, before logos (plan order wrong, note it); 0x701 779-1035 = content warning (JP); 0x801 1035-1291 = black; 0x901 1291-1547 = ACCESS (full at ~1424); 0xa01 then 0 at 1549.
  - TitleWork *0x0023BE34 (ptr 0x90d180 set 1556) +0 u32: 0x10000 1572-1892 = CAPCOM; 0x30200 1982-2358 = PRESS START; 0x50501 2530-2738 = main menu (input wait); 0x2 2738, 0x20202 2758-3232 = "SCENARIO" screen; 0x10502 3296+ constant through char select (~3792-4040) and Difficulty (~4293).
  - char select predicate: SelectWork* *0x003065FC (title.c:1232,1303), state s8 @+4 (title.c:1150-1160), 9 = char select, 10 difficulty, 11 (title.c:1464-1480). UNVERIFIED in a boot yet.
  - overlay id 0x00239F30 1->2 at 4815.
- gate/boot.script written (uncommitted; in TASK_PROGRESS.patch with ob.sh): savedata (left, circle), access, capcom, title (start), mainmenu (circle), scenario (circle), charselect, quit; timeout 420; 3 frames per capture.

## In flight
- Coder T4.c2 (agentId ab136aa63f7b7ef4b): Claude's correction — game thread ending before `quit` -> stderr `[script] game ended before quit at step <i> line <L>: <text>`, exit 4, + test; amend ee22992 in C:\ob\up (allowed, local), re-export 0020 + README; sync src; tools/test/recompile (relinks runner). Wait for its report/commit `T4.c2` before booting.
- Uncommitted: tools/ob.sh (boot --script) + gate/boot.script -> TASK_PROGRESS.patch (`git apply`, delete).

## Hypotheses rejected
- Plan order "title, then save-data screens": wrong; memory-card prompt is boot task sub 4, before content warning/ACCESS.
- TitleWork step 9 = char select (R1.1-008): the "step 9" is SelectWork.state, not TitleWork.

## Next 5 steps
1. After T4.c2 done: verify exit-4 code by reading; git apply patch.
2. `bash tools/ob.sh boot --script gate/boot.script --tag t4a` (background shell); check hidden=1 + exit 0; inspect captures with sheet.py (6 screens right? esp. charselect, access full brightness). Adjust settle waits if needed (if savedata Left/Circle does not advance -> timeout names step).
3. Second run --tag t4b; per-screen capture begin ticks (`grep "\[script\] capture\|step .* begin" boot-t4a.log`) and `cmp` frames-t4a vs frames-t4b per file.
4. Card HOW_WE_WORK.md Build/run/test Run line (boot --script hidden; plain boot has no auto-press); suites (ob.sh test); reports/2026-10-21-pa3-groundwork-and-boot-speed.md: update struggle 3 (hidden, evidence), add struggle 7 (finding screens: decomp partial -> watch discovery boot), Mistakes rows (exit 0 on early game end — Claude caught; plan screen order; R1.1-008 step 9 field). No game data.
5. logs/T4.md, tasks/T4.md, task_log finish, commit (gate/boot.script, tools/ob.sh, HOW_WE_WORK.md, report), plan_edit set T4 done.

## Gotchas
- `watch` baseline log line for 0x00239F30 interleaved with raylib INFO output (stdout shared); grep carefully.
- Captures' tick is a few vsyncs after step begin; report both.

## State to carry verbatim
- c2 agentId ab136aa63f7b7ef4b; reports R1.1-006..008; disc log C:\ob\logs\boot-t4disc.log.
