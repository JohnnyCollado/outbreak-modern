# T8 progress (attempt 4, expert handed back on harness enforcement while coder c4 runs)

Done:
- Archived attempt-3 progress to logs/T8.progress3.md.
- Partner finding: c3's sw frames (.run/1.3/c3/frames-sw/) all solid black; shares not progress until frames show logos.
- Read: native/pcsx2_gs/adapter/pcsx2_side.cpp (all), hook.cpp; PCSX2 GSState.cpp:2280-2300 TRXDIR, :2387-2435 FlushWrite, :2905-2972 Write, :3356-3510 Transfer, :7030 Update; C:\ob\up ps2_memory.cpp:2030-2208 (0022 image tee correct: setup 80 B, tag, NLOOP*16 data); Kernel/Stubs/GS.cpp:95-125, :1140-1190; Stubs/Helpers/Support.h:1877-1899.
- Coder T8.c4 RUNNING in background (agent ab72f43eace29f5f8). Log: logs/T8.c4.md. Do NOT touch C:\ob\up, C:\ob\src, native/pcsx2_gs, upstream/ps2recomp, docs/ops/pins.md until its report.

Hypothesis (current): black = HLE libgraph stubs (sceGsPutDrawEnv :782, sceGsSwapDBuff :1165-1208 via applyGsRegPairs Support.h:1891; applyGsClearPacket GS.cpp:117-122) write draw-env/clear regs straight into the CPU GS via public GS::writeRegister (gs_frontend.cpp:1088); PCSX2 never sees FRAME/ZBUF/XYOFFSET/SCISSOR/TEST -> draws miss DISPFB 0x70. Public writeRegister has no other runtime callers (only stubs + gs_cache tests). Crash (PSMT4 WriteImage AV) possibly same stale state; c4 re-checks.
Fix (c4 scope): patch 0023 = writeRegister tees 32 B PATH3 A+D packet (tag NLOOP=1 EOP=1 PACKED NREG=1 REGS=0xE + {value, reg}) when hook set; test fails on 0022 alone; export + README + pins 0001-0023 + sync; then sw gate, view frames; adapter diag if still black (draw counts, local mem at DISPFB, s_carry[3] non-empty on reg packet).
Rejected: 0022 image tee malformed (read, correct). Open risk: reg packet appended into a partial PATH3 carry unit in adapter.

Next five steps:
1. Read c4 report (not log); view frames yourself (savedata/access/capcom vs C:\ob\ref\access-a.png, capcom-a.png).
2. Gate: cpu gated = savedata 0.043597, access 0.248753, capcom 0.082450; sw <= those, boot completes all steps.
3. Docs: docs/ops/gs.md (adapter, PS2X_GS, staging, 0022, 0023 HLE reg tee), docs/ops/gate.md (stamp entries).
4. reports/2026-10-22-pcsx2-gs.md + index row (PACKED overread, latch stride stall, HLE reg bypass black frames, AV); cookbook: generalizable "HLE stubs bypass the GIF stream: tee them for an external GS".
5. logs/T8.md, tasks/T8.md (all gate lines; T10 note: CPU backend still runs under the hook and skews frame time), task_log finish, commit_task.sh T8.
