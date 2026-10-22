# T2 progress (attempt 1, forced hand-back while coder ran)
Done: read plan Context/Interfaces/T2, T1 + 1.1 T4 summaries, tools/ob.sh, refs.py outline, C:\ob layout.
In flight: coder T2.c1 (pa-coder, background) building tools/gate/gate.py (compare, selftest, stamp, synth), `ob.sh gate`, refs.py window fix, docs/ops/gate.md section; it commits T2.c1, then runs the real gate twice (run.sh log `ob-gate`), log phase-ends/current/logs/T2.c1.md.
Decisions (record as Deviations in T2 summary):
- stamp also hashes C:\ob\src hand-edited files (build reads src; up->src sync is manual) and only the cmd_recompile body of ob.sh.
- manifest `window` = int capture count from boot.script; T1's ISO time moves to `session` (no manifest exists yet).
- `gate.py synth` subcommand generates synthetic fixtures under .run/1.2/ (no game images).
- Best-frame tie-break: lowest share, then max, then k (plan Rationale :21).
Next steps:
1. Wait for/read T2.c1 report (`git log --grep T2.c1`, logs/T2.c1.md); verify the synth checks + `ob.sh gate --selftest` exit 2 yourself (one run).
2. grep .run/1.2/logs ob-gate logs for final lines: first run recompiled, boot exit 0, `no references` exit 2, wall; second run not recompiled; `[run] window hidden=1` in C:\ob\logs\boot-gate.log.
3. Append struggles to reports/2026-10-22-gate.md; wall time + recompile time into docs/ops/gate.md.
4. Write logs/T2.md + tasks/T2.md; `PY tools/task_log.py finish T2`; `bash tools/commit_task.sh T2 "..."`.
5. Set T2 done only if every done-when clause holds.
Gotchas: C:\ob\ref holds unrelated old files (pcsx2-*.png, gs-sum-25.txt); refs absent by design.
