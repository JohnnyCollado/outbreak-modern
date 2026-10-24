# High resolution, widescreen and the 4K texture pack (patch 0050)

- **Date:** 2026-10-24
- **Who:** the developer, one Claude Code session
- **Goal:** step 1 of the post-research plan: high resolution, widescreen and the 4K texture pack on today's PCSX2
  GS path, all in one day.
- **Outcome:** all three work on Windows with `PS2X_GS=pcsx2-vk`, off by default. PS2Recomp 0050 (`C:\ob\up`
  `af6e20d`): `upscale`/`window` settings, `.pnach` memory patches, the game serial; outbreak-modern: the adapter
  passes upscale, aspect, textures root and serial to PCSX2, which now builds its texture replacement (libpng,
  zlib, libzip). Hidden J's Bar run with all three: same scene timing, PCSX2 reports the pack found, 16:9 view
  confirmed in the captures; the developer's visible run looks good. The pack costs some frames (Still open).
- **Versions:** PS2Recomp `C:\ob\up` 0049 + 0050; PCSX2 v2.9.134; zlib 1.3.2, libpng 1.6.58; Windows 11, MSVC 19.44.

## Summary

Upscaling was a constant in the adapter (`UpscaleMultiplier = 1.0`). Widescreen for this exact build exists in
PCSX2's patch database (CRC 32088394, English-patched SLPM-65428): one data float (4/3 -> 16/9 at `0x00239D48`)
plus a movie fix that rewrites code, which a recompiled game cannot take at run time. So the runtime gained a small
`.pnach` reader for memory writes that drops writes into recompiled code. Texture replacement needed less than
the docs said: PNG/DDS loaders only (no jpeg/webp), libzip from PCSX2's own tree, plus the serial and textures
folder the stubs left empty. The developer's pack (4,074 PNGs, 3.2 GB, PCSX2 layout for SLPM-65428) is found.

## Struggles

### 1. The ELF can't tell code from data

- **Seen:** planning the "drop code patches" rule from the ELF: every segment is read-write-execute and `.text`
  runs to `0x23BA00`, so the widescreen float at `0x00239D48` (a `.data`-like value in `vu_data.data.s:154415`)
  sits inside the "code" section.
- **Found by:** reading the boot ELF's program and section headers.
- **Fix:** code = the range the recompiled function table covers (`g_ps2RecompiledFunctionTableBase..End`,
  `0x100000..0x1D9ABC`) plus every registered guest module's range, which is where code was actually recompiled.
- **Take-away:** in this game, the ELF's flags say nothing; the recompiler's tables are the truth.

### 2. The test settings file had a carriage return and a tab in the textures path

- **Seen:** first run with the pack: `replacements=none`, and the logged path read `D:\NEXTJJENepos\...	extures`.
- **Cause:** written with `printf`: `\r` and `\t` in `D:\NEXTJJEN\repos\...\textures` became control characters.
  The retry then failed too: the Write tool refused to overwrite an unread file and the run used the old file.
- **Fix:** read, then rewrite with the Write tool; third run `replacements=found`. ~11 minutes lost on two runs.
- **Take-away:** Windows paths never through `printf`/`echo -e`; check the `[settings] from` line before reading
  anything else of a run.

### 3. PCSX2 does not say how many replacements it found

- **Seen:** no log line from the texture replacement code on success (its messages go to the on-screen display,
  which is stubbed).
- **Fix:** the adapter's open line adds `replacements=found|none|not built` from
  `GSTextureReplacements::HasAnyReplacementTextures()`.

### 4. The pack costs frames

- **Seen:** J's Bar after `load60s`, hidden: widescreen + 3x mean 59.4 vsync/s (min 50); with the pack mean 57.6,
  min 37, 46 of 186 seconds below 55 (`boot-ws1.log`, `boot-all3.log`). Without either: flat 60 (`boot-iopl-after2.log`).
- **Found by:** `[fps]` lines; the GS thread's queue-ahead stalls barely change (0.63 s vs 0.91 s over the run), so
  the GS ring is not the cause.
- **Cause:** **Unknown.** Guess: PNG decoding of 4K textures on PCSX2's worker thread competing for CPU when a room
  first shows them; not measured.
- **Next:** a hostprof run with the pack (which threads take the time), then PCSX2's options (precache vs memory).

## Measurements (hidden `gate/play.script`, pcsx2-vk, mvu)

| run | settings | after load60s: median / mean / min vsync/s | load60s / load90s / load240s tick |
|---|---|---|---|
| `iopl-after2` | none | 60 / 60.0 / 57 | 6454 / 8259 / 17274 |
| `ws1` | widescreen pnach, upscale 3 | 60 / 59.4 / 50 | 6453 / 8259 / 17273 |
| `all3` | + textures (4K pack) | 59 / 57.6 / 37.4 | 6454 / 8261 / 17278 |

Start-up lines of `all3`: `[pnach] C:\ob\mods\SLPM-65428: 1 file(s), 1 write(s), aspect=16:9`, `[pnach] applied 1
write(s) at boot`, `[pcsx2-gs] open ... surface=797x448 upscale=3 aspect=16:9 ... serial=SLPM-65428
replacements=found`. A watch on `0x00239D48` never fired: the game does not write the value back.
Captures at `load90s` (`C:\ob\frames-ws1` vs `frames-iopl-after2`): the widescreen camera shows more of the bar on
both sides. Captures read GS local memory, so they show neither the upscale nor replaced textures.

Checks: ps2x 529/529 (+6), iop 5/5, gs_cache 45/45, ob_diag 22/22, pcsx2_vu 3/3, pcsx2_gs 68/68 (smoke PASS);
depcheck ok (0 game units); gate `pcsx2-sw` 3/3 gated green, 3 advisory. Builds: deps + GS library 73 s once
(zlib/libpng SHA-256 validated), runner 46-66 s, 0 game units.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| Texture packs need libpng/libjpeg/libwebp (`docs/ops/gs.md`, before) | PNG and DDS loaders only in v2.9.134; libzip is vendored | reading `GSTextureReplacementLoaders.cpp:25-28` | gs.md |
| Upscale, widescreen and the pack are each separate PS2Recomp patches (plan) | one patch 0050: they touch the same settings/start-up code | splitting hunks by hand was not worth it today | upstream/ps2recomp/README.md |

## Still open

- Visible check: done by the developer (`play.bat` with `C:\ob\r\ps2xRuntime\outbreak.ini` = `settings-all.ini`,
  `boot-play.err`: `upscale=3 aspect=16:9 ... replacements=found`, mailbox present): "looks good". Not yet a
  side-by-side of which textures were replaced, or a longer play than J's Bar.
- The pack's frame cost (entry 4).
- The HUD is stretched in 16:9 (the pnach only changes the 3D camera); movies' widescreen fix needs a recompile.
- Android: texture replacement still stubbed (no arm64 libpng/zlib/libzip build); upscale and pnach are shared code
  but untested there.

## Useful for other developers

- A `.pnach` from PCSX2's database works on a recompiled game when it only writes data; code writes need the
  recompiler. Check each address against the recompiled function tables, not the ELF flags.
