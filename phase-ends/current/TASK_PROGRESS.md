# T2 progress (handoff 1, written by Claude from the expert's hand-back)

## Done
- splat asm copied to C:\ob\splat\SLPM_654.28 (developer accepted).
- Developer approved ONE visible boot, bounded 200 s, killed by pid: `bash tools/ob.sh boot --secs 200 --tag t2`. Not used yet.

## In flight
- Coder T2.c1 running: writes tools/ob.sh (tools/test/recompile/boot), tools/ob/vc.bat (vcvars64 + long paths), header line in tools/recomp_cycle.sh; runs tools/test/recompile via run.sh (ob-tools, ob-test, ob-recompile); commits T2.c1. Do NOT start a second coder; it holds those files.
- No T2 commits yet; HEAD 83b3628.

## Next
1. Wait for T2.c1 report + commit.
2. Check counts in .run/<phase>/logs/ ob-tools, ob-test, ob-recompile: ps2x_tests 461/461, IOP ctest 4/4, gs_cache 45/45; 0 recompile errors (main ELF + overlays demo, submain, netdnas).
3. Run the one approved boot; confirm main menu from C:\ob\logs\boot-t2.log or C:\ob\frames.
4. Card Build/Run/Test lines in HOW_WE_WORK.md → ob.sh commands.
5. logs/T2.md + tasks/T2.md; summary lists replaced scripts: build-tools.bat, tests.bat, ioptests.bat, build-runner.bat, recomp-cycle.sh, ovl-cycle.sh, boot-frames*.ps1.
6. `python tools/task_log.py finish T2`; `bash tools/commit_task.sh T2 "..." HOW_WE_WORK.md`.

## Gotchas
- gs_cache tests: `ctest --test-dir C:\ob\t\ps2xTest\gs_cache`.
- ps2x_tests cwd must be C:\ob\src\ps2xTest.
- old ovl-cycle.sh points at the scratchpad asm path; ob.sh uses OB_SPLAT.
- Runner has no hidden-window flag (ps2_runtime.cpp:768 sets only FLAG_WINDOW_RESIZABLE); T4 adds it.
