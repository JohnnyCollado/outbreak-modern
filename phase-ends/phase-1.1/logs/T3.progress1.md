# T3 progress (attempt 1, expert forced to hand back while coder T3.c1 still runs)

Done:
- Classified src vs up (diff: .run/1.1/t3/src-vs-up.diff; saved to logs/T3.src-vs-up.diff, logs/T3.TempProfiler.cpp, C:\ob\stash\t3\).
- Classification: keep = ps2_runtime.cpp frame dump (rewritten as 0019); replace-later = ps2_pad.cpp auto-press (T4); drop = iop_kernel.cpp thread/SendMbx traces, iop_emulator.cpp PS2X_IOP_TRACE_IMPORTS, iop_rpc.cpp rpc traces, Pad2.cpp socket/read/peek traces, gs_cpu_backend.cpp PS2X_GS_SUMMARY, ps2_runtime.cpp ps2TempProfilerStart + Kernel/TempProfiler.cpp (preserved; recommend own opt-in patch later), CR-only diffs (analyzer, ps2_runtime.h, ps2_memory.h, Thread.cpp). None of the dropped hunks changes guest behaviour (print/read only; profiler gated by PS2X_PROFILE).
In flight:
- pa-coder T3.c1 (background): clean src, ps2_frame_dump.{h,cpp} in up, UploadFrame hook after copyLatchedHostPresentationFrame, ps2x_tests test, up commit, 0019 patch + README row, suites + runner link. Log: logs/T3.c1.md.
Next:
1. Read T3.c1 report / logs/T3.c1.md; verify diff check (only ps2_pad.cpp) and suite counts.
2. Remaining clause needs a visible boot (`bash tools/ob.sh boot --secs 60`) to show PNG size = presented frame: return `question` for developer proceed.
3. Write logs/T3.md, tasks/T3.md; task_log.py finish T3; commit_task.sh T3 (include logs/T3.src-vs-up.diff, T3.TempProfiler.cpp).
