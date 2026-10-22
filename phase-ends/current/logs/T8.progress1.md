# T8 progress (attempt 1, expert handed back early on harness enforcement)

Done:
- Read plan Context/Interfaces/Rationale/Risks/T8, tasks/T7.md, docs/ops/gs.md, R1.3-005, R1.3-006.
- Retriever: R1.3-010 (PCSX2 GS entry semantics) adopted.
- Coder T8.c1 briefed and RUNNING in background (agent a5de0934e6b02047d): PS2Recomp patch 0021, hook `PS2ExternalGS` in `include/runtime/gs/gs_external.h` (gifPacket(path,data,size), privWrite(offset,value), vsync(tick, const GSRegisters&), readLocalToHost(dst,max), reset(), snapshot(rgba,w,h)); `ps2ExternalGS()/ps2SetExternalGS()`; `ps2CreateExternalGS(mode)` under `PS2X_EXTERNAL_GS`; `PS2X_GS` selector; CMake `PS2X_EXTERNAL_GS_DIR` -> target `ps2x_external_gs`, FATAL with DEBUG_UI=ON; snapshot published via `GS::publishExternalFrame`, latch uses it when hook set; ps2x_tests recording-fake test; export 0021 + README + pins. Log: logs/T8.c1.md. Do NOT start c2 until its report is read.

Decisions (carry):
- Snapshot every vsync on the game thread, published into the frontend latch; UploadFrame/dump/capture unchanged (raylib still shows the snapshot under pcsx2-sw; deviation from Context "skip upload" deferred to T9).
- CPU backend kept running under the hook (no null backend; speed out of scope).
- Readback = GSInitAndReadFIFO(dst, bytes/16): uses m_env regs set by the GIF stream (R1.3-010 §3).
- GIF: use GSgifTransfer (Transfer<3>) for ALL paths: MTGS sends whole packets that way (R1.3-010 caveat, MTGS.cpp:386-392/455/473); path id kept for logs.
- Field: PCSX2 passes PRE-swap CSR (R1.3-010 §4); our hook vsync is after FIELD update, so field = (csr&0x2000)?1:0 inverted relative to MTGS formula; SW VSync does not use field (GSRendererSW.cpp:71-95), low impact. Flip parity = hypothesis 1 if shares high.
- registers_written = true always. Re-mirror whole GSRegisters into 8 KB basemem at each vsync (offsets 0x0..0xE0 step 0x10, CSR 0x1000, IMR 0x1010, BUSDIR 0x1040, SIGLBLID 0x1080); stubs may bypass the write path.
- Snapshot args (0,0,false,true) = PCSX2 ScreenshotSize=2 (R1.3-010 §5). Size check: gate.py compare refuses a frame whose size != its ref.

Next five steps (c2 brief):
1. native/pcsx2_gs/adapter: target `ps2x_external_gs` implementing the hook; lazy GSopen on first game-thread call with own hidden Win32 window (smoke main.cpp:23-47), MakeOptions (smoke :50-67), EmuFolders (smoke :152-153); thread-id assert.
2. tools/ob.sh cmd_recompile: `-DPS2X_EXTERNAL_GS_DIR=<repo>/native/pcsx2_gs`, reconfigure when cache lacks it, stage resources/shaders + shaderc_shared.dll + zstd.dll beside ps2EntryRunner.exe; cmd_boot strips vulkansdk PATH entries.
3. gate.py stamp (:319-388): add C:\ob\pcsx2 HEAD + diff and native/pcsx2_gs tree; size check in compare.
4. Background `PS2X_GS=cpu bash tools/run.sh ob-gate -- bash tools/ob.sh gate` (stale) must equal baseline-1.2 shares; then `PS2X_GS=pcsx2-sw ...` foreground, savedata/access/capcom <= baseline-1.2.
5. Docs gate.md/gs.md, struggle report, cookbook entry (cookbook_add.sh), logs/T8.md, tasks/T8.md, task_log finish, commit.

Gotchas: runner configure only runs if no build.ninja (ob.sh:120); Vulkan SDK PATH shadows shaderc (C0001).
