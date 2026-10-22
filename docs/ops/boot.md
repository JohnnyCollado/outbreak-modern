# Booting the runner

Source: phase 1.1 T4 summary (patch 0020), tools/ob.sh.

## Scripted boot (the normal form)
`bash tools/ob.sh boot --script gate/boot.script --tag T` — hidden window, the script drives the pad on guest vsyncs
(no host input), `capture` steps write `<screen>-<k>.png` to C:\ob\frames-<T>, log C:\ob\logs\boot-<T>.log.
Exit codes (ob.sh returns the runner's): 0 `quit` reached; 2 script parse error or capture without a dump dir;
3 wall-clock timeout (`[script] TIMEOUT step <i> line <L>`); 4 game ended before `quit`.
Grammar: header of `upstream/ps2recomp/0020-*.patch` and the T4 summary (phase 1.1): `timeout`, `wait vsync`,
`wait mem`, `press`, `capture`, `watch`, `log`, `quit`. Screen predicates for SLPM_654.28 en are in the T4 summary.
Standing proceed this generation (hidden boots that end by themselves).

## Plain boot
`bash tools/ob.sh boot [--secs N] [--tag T]` — no script: host pad, VISIBLE window, every presented frame dumped
(~33 PNG/s). Any ESC or close click ends it. Needs a specific developer proceed each time.
