# T8 progress (attempt 2, expert handed back on harness enforcement while coder c2 runs)

Done:
- Archived attempt-1 progress to logs/T8.progress1.md (commit 49fbc2f).
- Read T7 summary, docs/ops/gs.md, R1.3-010, T8.c1 log tail, gs_external.h, native/pcsx2_gs CMake + smoke, ob.sh :83-231, gate.py :319-388, GSRegisters (ps2_memory.h:197-220).
- Coder T8.c2 briefed, RUNNING in background (agent a0d96c0bf7cc23292). Log: logs/T8.c2.md. Do NOT touch native/pcsx2_gs, tools/ob.sh, tools/gate/gate.py until its report.

c2 scope: two targets in native/pcsx2_gs under `if(TARGET ps2_runtime)`: `ps2x_external_gs` (adapter/hook.cpp, PS2Recomp flags) PRIVATE-links `ob_pcsx2_gs_adapter` (adapter/pcsx2_side.cpp, PCSX2 flags) via plain header adapter/ob_gs_bridge.h, so PCSX2 PUBLIC defines (_HAS_EXCEPTIONS=0 etc.) never reach ps2_runtime. Lazy GSopen + mutex; GSgifTransfer for all paths; basemem mirror; field=(csr&0x2000)?1:0; GSInitAndReadFIFO readback; snapshot (0,0,false,true) alpha forced 0xFF. ob.sh recompile reconfigures when cache lacks PS2X_EXTERNAL_GS_DIR, stages shaders+DLLs; cmd_boot strips vulkansdk PATH; gate.py stamp adds pcsx2:HEAD/diff + repo/native/pcsx2_gs tree. c2 runs: pcsx2-gs smoke, ob.sh test, recompile, PS2X_GS=pcsx2-sw gate, PS2X_GS=cpu gate.

Decisions (why): in-tree add_subdirectory build (ninja tracks PCSX2 + adapter) over importing C:\ob\pcsx2-gs-build; GSgifTransfer for all = PCSX2 MTGS behaviour; field inverted vs MTGS formula because our hook fires post-swap; lazy open keeps all PCSX2 calls on one thread.

Next five steps:
1. Read c2 report (not its log); verify gate lines in .run/1.3/logs/ob-gate-sw*.log and ob-gate-cpu*.log tails.
2. cpu gate shares must equal gate/baseline-1.2.md (savedata 0.043597, access 0.248753, capcom 0.082450, title 0.999865, mainmenu 0.998910, charselect 0.891462); pcsx2-sw savedata/access/capcom <= those. If high: adapter faults first on savedata frame, one hypothesis at a time (field parity first, then path mapping, then snapshot size/crop).
3. Docs: docs/ops/gs.md (adapter section, PS2X_GS, staging), docs/ops/gate.md (stamp entries), HOW_WE_WORK.md if paths/tools changed.
4. Struggle report reports/ + index row; cookbook entry if generalizable (flag isolation via PRIVATE link split).
5. logs/T8.md, tasks/T8.md (both gates' lines; next-task-needs: CPU backend still runs under the hook and skews frame time for T10, say whether it can be switched off), task_log finish, commit_task.sh T8.
