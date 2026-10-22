# T8 progress (attempt 3, expert handed back on harness enforcement while coder c3 runs)

Done:
- Archived attempt-2 progress to logs/T8.progress2.md.
- Read: ps2_runtime.cpp:2293-2329, ps2_memory.cpp:2030-2275 + :145-165, gs_frontend.cpp:160-185/:530-545/:1650-1660, ps2_gif_arbiter.cpp:40-65, EeScheduler.cpp:1873-1890, GSRenderer.cpp:1061-1140, T8.c2 log tail.
- Coder T8.c3 RUNNING in background (agent a5339a12fd318af48). Log: logs/T8.c3.md. Do NOT touch C:\ob\up, native/pcsx2_gs, upstream/ps2recomp, docs/ops/pins.md until its report.

Hypothesis (current): stall = 0021 skips native GIF fast paths (ps2_runtime.cpp:2315 nativeGifChains) under the hook; fallback CHCR/processPendingTransfers route handles those chains differently. Rejected/unlikely: readback (c2 count 0, never called), latch/priv notify (no guest effect under noop).
Fix (c3 scope): patch 0022 = native chains always run; on success tee raw GIF bytes to hook as Path3 (image: setup 80 B, IMAGE tag 16 B, data; packed: payload). Verified first on noop control boot (must reach savedata ~tick 175); else c3 returns blocked with guest PC/predicate evidence. Test in ps2_gs_external_tests.cpp fails on 0021 alone. Export 0022 + README + pins 0001-0022 + sync C:\ob\src. Adapter snapshot crop to DISPLAY2/DISPFB2 rect (512x448), mirroring CPU presentation rect.
Why tee over fixing fallback: keeps cpu mode byte-identical; adapter whole-unit buffer accepts split units.

Next five steps:
1. Read c3 report (not log); check gate tails .run/1.3/logs/ob-gate-cpu*.log, ob-gate-sw*.log.
2. cpu shares must equal baseline-1.2 (savedata 0.043597, access 0.248753, capcom 0.082450, title 0.999865, mainmenu 0.998910, charselect 0.891462); sw savedata/access/capcom <= those. If high: adapter faults one hypothesis at a time on savedata frame.
3. Docs: docs/ops/gs.md (adapter, PS2X_GS, staging, 0022), docs/ops/gate.md (stamp entries).
4. reports/2026-10-22-pcsx2-gs.md + index row (crash: PACKED overread fix; stall: native chain skip); cookbook if generalizable.
5. logs/T8.md, tasks/T8.md (all gate lines; next-task-needs: CPU backend still runs under the hook and skews T10 frame time), task_log finish, commit_task.sh T8.
