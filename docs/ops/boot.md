# Booting the runner

Source: phase 1.1 T4 summary (patch 0020), tools/ob.sh. Since patch 0037 the script player, frame dumps and the
other diagnostics live in the ob_diag plugin (`native/ob_diag`); `ob.sh boot` loads it with `PS2X_DIAG` unless
`--no-diag`. Every boot logs `[fps] tick N | R fps` once per second when the window is hidden (patch 0035).

## Scripted boot (the normal form)
`bash tools/ob.sh boot --script gate/boot.script --tag T` — hidden window, the script drives the pad on guest vsyncs
(no host input), `capture` steps write `<screen>-<k>.png` to C:\ob\frames-<T>, log C:\ob\logs\boot-<T>.log.
Exit codes (ob.sh returns the runner's): 0 `quit` reached; 2 script parse error or capture without a dump dir;
3 wall-clock timeout (`[script] TIMEOUT step <i> line <L>`); 4 game ended before `quit`.
Grammar: header of `native/ob_diag/src/input_script.h` (was `upstream/ps2recomp/0020-*.patch`) and the T4 summary (phase 1.1): `timeout`, `wait vsync`,
`wait mem`, `press`, `capture`, `watch`, `log`, `quit`. Screen predicates for SLPM_654.28 en are in the T4 summary.
Standing proceed this generation (hidden boots that end by themselves).

## Engine-only boot (a play build)
`bash tools/ob.sh boot --no-diag --hidden --secs N --tag T` — no plugin, so no script, no dumps, no frame-time CSV;
nobody presses a button, so the game stays on its first screens. Speed comes from the `[fps]` lines in the log.

## Plain boot
`bash tools/ob.sh boot [--secs N] [--tag T]` — no script: host pad, VISIBLE window, every presented frame dumped
(~33 PNG/s). Any ESC or close click ends it. Needs a specific developer proceed each time.
