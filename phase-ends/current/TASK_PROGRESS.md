# TASK_PROGRESS — T9 attempt 2 (review round 1 follow-up; waiting on the developer's visible check)

Task: T9 Vulkan presentation in the runner window
Attempt: 2   Agent: pa-expert   Commit: see `git log --oneline -3` (T9: adapter diagnostics + visible deinterlace)

## Done so far
- c1 (repo 589334d, C:\ob\up d192a70): patch 0024 attachWindow/windowResized hooks; adapter child HWND. Developer's
  visible boot: picture shows, but "still tearing inside" and "moving or resizing makes no difference inside".
- Adapter `native/pcsx2_gs/adapter/pcsx2_side.cpp` (no runtime change, so no patch 0025):
  - diagnostics: `attach child ... parent clipchildren= clipsiblings=`, `open ... vsync=<req> actual=<GetVSyncMode>
    surface=WxH`, `display mode pmode= smode2 int= ffmd= display1/2= dispfb1/2=` on change (cap 64),
    `resize request WxH child=WxH` (main thread), `resize applied WxH surface=WxH` (game thread);
    PCSX2's own log -> `pcsx2-gs.log` in the working dir (C:\ob\logs) via `Log::SetFileOutputLevel(LOGLEVEL_DEV)`.
  - fix: `MakeOptions(r, visible)`: InterlaceMode Automatic for a visible child, Off for hidden (gate unchanged).
- Verify: ob.sh test 475/475, 4/4, 45/45 (.run/1.3/logs/t9-test.log); pcsx2-vk gate savedata 0.042241, access
  0.247877, capcom 0.081552 (identical to c1; t9-gate-vk.log); cpu gate = baseline-1.2 exactly (t9-gate-cpu.log);
  vk frames .run/1.3/frames-vk2 viewed (capcom as before).

## Hypotheses rejected
- H1 parent GL present over the child (no WS_CLIPCHILDREN): GLFW `getWindowStyle` sets WS_CLIPSIBLINGS|WS_CLIPCHILDREN
  (C:\ob\g\_deps\raylib-src\src\external\glfw\src\win32_window.c:43); log `parent clipchildren=1 clipsiblings=1`.
- H2 no Mailbox: RTX 3070, vulkaninfo lists FIFO, IMMEDIATE, MAILBOX for the surface; visible requests Mailbox(2).
  Actual mode on a visible window is printed by the next visible boot (`actual=` and pcsx2-gs.log "present mode").
- Content tearing (vsync tick async to EE): visible-boot dump (9914 files, 810 distinct; moved to
  .run/1.3/frames-visible-dev) scanned row-by-row vs neighbours: 0 torn frames (3 weak candidates are fades).
- Resize chain broken: hidden boot + external SetWindowPos (scratch resize_hidden.py) -> `resize request 1280x720
  child=1280x720`, `resize applied 1280x720 surface=1280x720`, swap chain recreated (pcsx2-gs.log), same for 800x600.

## Current hypothesis
H3 interlace: game runs SMODE2 INT=1 FFMD=0, PMODE 0x7f27 (EN1+EN2, MMOD=1, ALP=0x7f), DISPLAY1 DY=51 vs DISPLAY2
DY=50: both circuits read the same buffer one line apart and blend (anti-flicker). InterlaceMode Off presents that
with a per-field one-line shake. Automatic (PCSX2 default) detects game deinterlacing and bobs. Confirm: visible boot.
"Resizing makes no difference" may mean the tearing did not change on resize; resize logs will show it works.

## Next 5 steps
1. Developer runs `PS2X_GS=pcsx2-vk bash tools/ob.sh boot --tag visible`, checks: no shake/tear; picture scales
   (letterboxed 4:3) when resized/maximised.
2. Read C:\ob\logs\boot-visible.err `[pcsx2-gs]` lines + C:\ob\logs\pcsx2-gs.log "present mode".
3. If still tearing: compare `actual=`; try FIFO for visible; if still, check whether the parent's raylib EndDrawing
   swap interacts (DWM) by skipping BeginDrawing/EndDrawing body only.
4. On confirm: docs/ops/gs.md (visible present, diagnostics, interlace), struggle report entry, logs/T9.md, tasks/T9.md.
5. task_log.py finish T9; commit; return review/done per developer.

## Gotchas
- `ob.sh boot` without --script dumps every frame into C:\ob\frames (never cleared): move it before a diagnostic boot.
- gate exit 1 is normal (RED shares at baseline).
