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

## Live pad: a mock controller over a named pipe (ob_diag plugin)
`bash tools/ob.sh boot --pad <name> [--secs N] [--tag T]` — hidden window, plugin loaded, no script. The plugin
(`PS2X_PAD_PIPE=<name>`, `native/ob_diag/src/pad_control.h`) owns pad port 0 (buttons and sticks, hook API 2
`padSticks`, patch 0039) and serves `\\.\pipe\<name>`: local clients only, the current user only (protected DACL with
the user's SID), one client at a time (a second gets "pipe busy"). Shots go to `C:\ob\frames-<T>` (`PS2X_PAD_SHOTS`).
The run ends on `quit` (exit 0) or the `--secs` kill (default 200; give a long one, e.g. 900). Idle cost: one
atomic load per vsync; frames are read back only for a `shot` (one frame each, never per frame).

Client: `python tools/ob/pad.py [--pipe NAME] <command>` (one command), or commands on stdin, one per line (`#`
comments); prints one reply per command (`ok ...` / `err ...`); exit 0 all ok, 1 some err, 2 no pipe / no reply.
`--pipe` defaults to `$OB_PAD_PIPE` or `ob_pad`; `--wait S` for the pipe (10), `--timeout S` per reply (600).

| Command | Effect | Reply |
|---|---|---|
| `press <btn> [frames]` | held from the next vsync for frames (default 6), then released | when released: `ok press ... tick=N` |
| `hold <btn> <frames>` | held from the next vsync; stays held | after frames |
| `release <btn>\|all` | releases at once; `all` also centres both sticks | at once |
| `stick <left\|right> <x> <y> <frames>` | x, y in -1..1 (y -1 = up; byte `128 + v * 127`) for frames, then centred; frames 0 = until `release all` | when centred |
| `wait <frames>` | nothing | after frames |
| `tick` | | `ok tick=N` (guest vsync tick) |
| `shot [name]` | one frame through the on-demand frame path (re-asked after 120 vsyncs without one) | `ok shot <dir>\<name>.png tick=N WxH` (default name `shot-<tick>`) |
| `peek <addr\|*ptr+off> <1\|2\|4>` | guest RAM read; `*ptr+off` reads the u32 at ptr first (as the script's `wait mem`) | `ok peek 0xADDR W = 0xVALUE` |
| `quit` | ends the run (exit 0) | `ok quit` |

Buttons: up down left right cross circle square triangle l1 r1 l2 r2 l3 r3 start select, combined with `+`. One
command at a time (a second while one runs: `err busy`). The boot log shows each command as `[pad] <cmd> -> <reply>`.
Screen predicates for `peek` (task step, title/select states): `gate/play.script` comments. Not with `--script`
(both would drive the pad). If a command never completes (the guest stopped vsyncing) the client times out; the
server waits for that command before it reads the next.

## Input bindings (input.ini, patch 0038)
With a host pad (no plugin owning it), the runner maps keys and gamepad from `input.ini` beside the exe
(`C:\ob\r\ps2xRuntime\input.ini`) or `PS2X_INPUT_INI=<path>`; no file = the built-in defaults. `ob.sh` stages
nothing. Format and defaults: `native/input.ini.example`; a bad line is logged once as `[input] line N: ...` and
keeps its default; a read file logs `[input] bindings from <path> (N bad line(s))`.
