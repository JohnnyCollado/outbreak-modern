# T5 progress (attempt 1, forced hand-back while coder T5.c1 runs)

Done so far:
- TOLERANCE.md sections 1-8: d=8, p=0.0001; gated savedata/access/capcom (proven); advisory title/mainmenu/charselect
  (Sofdec movies, M5); windows unchanged (64/32/6) with reasons; Q2 line-doubling = runner fault (1.3).
- Report struggles 6-8 with outcomes; docs/ops/gate.md Tolerance section; logs/T5.md resumes 1-2; tasks/T5.md.
- Research adopted: R1.2-007 (PCSX2 SW), R1.2-008 (decomp timers/movies).
In flight:
- Coder T5.c1 (pa-coder, background, agent id adf58f279339d706e): gate.py `advisory` list in tolerance file,
  `--tolerance PATH` on compare/selftest + ob.sh cmd_gate pass-through, ADVISORY line suffix, exit 0 iff gated green,
  selftest skips advisory screens' counts, synth frames-advisory + tol-advisory.json/tol-none.json, docs/ops/gate.md.
  Commits as T5.c1; log logs/T5.c1.md. Do not touch tools/gate/gate.py, tools/ob.sh, docs/ops/gate.md until it reports.
Hypotheses rejected (evidence in logs/T5.md): region split (no UI region agrees a vs b); loop measurable on our side
(no bg motion, t5_anim); decomp loop constants (movies only); name->stream map (t5_map noise).
Next five steps:
1. Read T5.c1 report (not its log); check its commit.
2. Write draft tolerance .run/1.2/tol-draft.json {d:8,p:0.0001,advisory:["title","mainmenu","charselect"]}.
3. `bash tools/run.sh t5_gate --timeout 280 -- bash tools/ob.sh gate --tolerance .run/1.2/tol-draft.json`
   (hidden boot, standing proceed); expect exit 1 (static screens red from line-doubling); keep per-screen lines.
4. Add the gate result to logs/T5.md, tasks/T5.md (Status done, coder run c1), report if struggles.
5. `PY tools/task_log.py finish T5`; `bash tools/commit_task.sh T5 "..." gate/TOLERANCE.md reports/2026-10-22-gate.md`;
   `PY tools/plan_edit.py set-status T5 done`; return done (≤ 12 lines).
Gotchas: gate/tolerance.json must NOT be created (T6 freezes it). Stamp hashes only cmd_recompile of ob.sh: no rebuild.
