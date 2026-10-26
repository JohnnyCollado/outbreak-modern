# HLE libvu0 audit: every stub against the game's own code

- **Date:** 2026-10-26
- **Who:** Claude Code side session (started from the shadows session); no developer runs needed
- **Goal:** check every PS2Recomp stand-in for Sony's libvu0 that SLPM-65428 binds (`C:\ob\work\decomp.toml`) against
  the game's own copy of the function, and fix the ones that differ.
- **Outcome:** 44 entries checked (42 stubs + 2 recompiled). Besides the five fixed or cleared in the shadows session,
  six stubs differed and are fixed as PS2Recomp 0065-0070; two more differ but are unreachable or a hardware reset,
  left as they are (Still open). Only one of the six, Normalize, is called by the game, and its one caller is not
  affected, so no visible change is expected. Tests 550/550 and every suite green; gate 3/3 green (`PS2X_GS=pcsx2-sw`).
- **Versions:** outbreak-modern `98370b1` + the shadows work (PS2Recomp `eb8688b` + 0001-0064 -> 0070, `C:\ob\up` `f82361f`-`b89cb55`), PCSX2 v2.9.134
  GS, Windows 11.

## Summary

The game links Sony's real libvu0 at `0x17E7E8`-`0x17FF2C` of the main ELF; PS2Recomp replaces most of it with C++
written from descriptions. Each function was disassembled from the disc image and compared with its stub line by line.
Only 9 functions are called by the game at all (direct `jal`/`j`; no pointers to them exist in any ELF), almost all
from the SUBMAIN shadow code; the other 33 are dead code here but are fixed anyway for other games. The six
differences: Normalize put w in the length (and inherited by CameraMatrix and NormalLightMatrix), ScaleVector read a
leftover register when the scale was 0, ClipScreen/ClipScreen3 tested a made-up guard band, and ClipAll skipped the
w test.

## The audit table

"Called" counts direct calls in the recompiled main ELF (`C:\ob\work\output`) and all five overlays
(`C:\ob\ovl\output-*`), plus a scan of every ELF for the address as a data word or a `lui`/`addiu` pair (none found).
Address ranges: function start to the next function.

| Function | Original (SLPM_654.28) | Verdict | Called by the game |
|---|---|---|---|
| sceVu0ApplyMatrix | 0x17E7E8-0x17E864 | match (shadows session) | SUBMAIN 0x347D60 (5) |
| sceVu0MulMatrix | 0x17E868-0x17E8F4 | fixed earlier: 0064 (order) | SUBMAIN 0x347D60 (1) |
| sceVu0OuterProduct | 0x17E8F8-0x17E964 | match: VOPMULA/VOPMSUB.xyz = a1 x a2, VSUB.w gives w = 0 | SUBMAIN 0x3475C0 (1) |
| sceVu0InnerProduct | 0x17E968-0x17E9D4 | fixed earlier: 0063 (w) | SUBMAIN 0x347D60 (2) |
| **sceVu0Normalize** | 0x17E9D8-0x17EA54 | **differs: length over x, y, z only, w written 0, no 1e-6 threshold -> 0065** | SUBMAIN 0x3475C0 (1) |
| sceVu0TransposeMatrix | 0x17EA58-0x17EADC | match (PEXTLW/PEXTUW/PCPYLD/PCPYUD transpose) | no |
| sceVu0InversMatrix | 0x17EAE0-0x17EB8C | match (3x3 transposed, w column 0, row 3 = -(t . R[col]), [15] kept) | no |
| sceVu0DivVector | 0x17EB90-0x17EBFC | match (xyzw * 1/$f12); divide by 0 differs, see Still open | no |
| sceVu0DivVectorXYZ | 0x17EC00-0x17EC6C | match (xyz, w kept); divide by 0 as above | no |
| sceVu0InterVector | 0x17EC70-0x17ECF4 | match (a1*t + a2*(1-t), xyzw) | no |
| sceVu0AddVector / SubVector / MulVector | 0x17ECF8-0x17EE2C | match (a1 op a2, xyzw) | no |
| **sceVu0ScaleVector** | 0x17EE30-0x17EE94 | **differs: stub took $a2 as the scale when $f12 == 0 -> 0066** | no (libvu0's own NormalLightMatrix) |
| sceVu0TransMatrix | 0x17EE98-0x17EF14 | match (row 3 xyz += v, w kept) | no |
| sceVu0CopyVector / CopyMatrix | 0x17EF18-0x17EFCC | match (16 / 64 bytes) | no |
| sceVu0FTOI4/FTOI0/ITOF4/ITOF0Vector | 0x17EFD0-0x17F10C | match in range; out-of-range FTOI differs, see Still open | no |
| sceVu0UnitMatrix | 0x17F110-0x17F164 | match | no |
| _sceVu0ecossin | 0x17F168-0x17F1DC | differs in ABI (register helper: angle in vf6.x, result (sin, cos) in vf4; the stub writes (cos, sin) to *a0); unreachable | no (only from libvu0's RotMatrixX/Y/Z, which are stubs) |
| sceVu0RotMatrixZ / X / Y | 0x17F1E0-0x17F4BC | match (same rotation matrices, out = src x R); sin/cos from a degree-9 polynomial + sqrt vs `std::sin/cos`: rounding only | no |
| sceVu0RotMatrix | 0x17F4C0-0x17F50C | match (Z, then Y, then X) | no |
| sceVu0ClampVector | 0x17F510-0x17F594 | match (VMAXx then VMINIx) | no |
| **sceVu0CameraMatrix** | 0x17F598-0x17F644 | **differs: built on sceVu0Normalize; the stub put fwd.w in the length -> 0067** | no |
| **sceVu0NormalLightMatrix** | 0x17F648-0x17F704 | **differs: same, light.w ended up in the bottom row -> 0068** | no |
| sceVu0LightColorMatrix | 0x17F708-0x17F76C | match (4 CopyVector; 5th argument in $t0) | no |
| sceVu0ViewScreenMatrix | 0x17F770-0x17F874 | match (same zScale/zOffset, same two matrices, MulMatrix(m, proj, scale)) | no |
| sceVu0DropShadowMatrix | 0x17F878-0x17FA04 | match (shadows session) | SUBMAIN 0x347BB0 (1) |
| sceVu0RotTransPersN / RotTransPers | 0x17FA08-0x17FB94 | fixed earlier: 0062 (mode) | RotTransPers: SUBMAIN 0x347D60 (2) |
| sceVu0InterVectorXYZ | 0x17FB98-0x17FC24 | match (xyz lerp, w from a1) | no |
| sceVu0ScaleVectorXYZ | 0x17FC28-0x17FC8C | match | no |
| **sceVu0ClipScreen** | 0x17FC90-0x17FD0C | **differs: libvu0 returns STATUS & 0xC0 of v - 0 (x, y, w) and 4096 - v (x, y); the stub a +-4096 guard band with its own codes -> 0069** | no |
| **sceVu0ClipScreen3** | 0x17FD10-0x17FDBC | **differs: as ClipScreen, three vertices -> 0069** | no |
| **sceVu0ClipAll** | 0x17FDC0-0x17FEAC | **differs: also tests lo.w < t.w < hi.w, and a vertex on a plane is outside -> 0070** | no |
| sceVpu0Reset | 0x17FEB0-0x17FF2C | differs: the stub does nothing; the original resets VIF0 and VU0 and sends 4 VIF codes | main 0x19EB50 (1, at start-up) |
| sceVu0CopyMatrix (2nd copy) | 0x190ED0-0x190EF0 | match (lq/sq 64 bytes) | 50 calls in 35 functions (GAME 32, SUBMAIN 2, main 1) |
| sceVu0MemWriteQ / MemReadQ | 0x102380 / 0x1023C0 | not stubbed: `entry_points`, the game's own code is recompiled | main (9 / 10) |

Not bound for this game: the stubs `sceVu0CopyVectorXYZ` and `sceVu0ITOF12Vector` (no such function in the ELF).

## Struggles

### 1. Which functions does the game call?

- **Seen:** 42 libvu0 entries; checking them in order of use needed the callers.
- **Found by:** the recompiler writes every call as a `// 0x...: ... jal func_<ADDR>` comment; a grep over
  `C:\ob\work\output` and `C:\ob\ovl\output-*` for `jal`/`j` to each address, and a script over every ELF
  (`SLPM_654.28`, `C:\ob\ovl\*.elf`) for the 41 addresses as data words or `lui`+`addiu`/`ori` pairs (session
  scratchpad `ptrs.py`): 0 pointer hits.
- **Cause:** the game uses libvu0 almost only in SUBMAIN's shadow code: 0x3475C0 (OuterProduct, Normalize),
  0x347BB0 (DropShadowMatrix), 0x347D60 (ApplyMatrix, MulMatrix, InnerProduct, RotTransPers).
- **Take-away:** of the stubs the game reaches, all were already checked except OuterProduct (match), Normalize and the
  second CopyMatrix (match).

### 2. The disassembler from the shadows session did not decode everything

- **Seen:** `eedis.py` printed `v0x18`, `op 0x8`, `op 0x1c` inside libvu0.
- **Cause:** COP2 special2 0x18-0x1B (VMULAx/y/z/w), `addi`, and the MMI ops that TransposeMatrix/InversMatrix use
  (PEXTLW, PEXTUW, PCPYLD, PCPYUD) were missing.
- **Fix:** added per PCSX2's opcode tables (`R5900OpcodeTables.cpp`, `VUops.cpp`), plus `andi`, `slti`, shifts,
  `beql`/`bnel`. The whole library (`0x17E7E8`-`0x17FEB0`, 1,458 words) now decodes with no unknown opcode.
- **Evidence:** session scratchpad `eedis.py`, listing `lib.txt`.

### 3. Normalize: w in the length

- **Seen:** the stub's length is `sqrt(x*x + y*y + z*z + w*w)`, it scales all four lanes, and returns 0 for a length
  under 1e-6 (`VU.cpp`, `sceVu0Normalize`).
- **Found by:** `0x17E9F8` `vmul.xyz vf5, vf4, vf4`, `vaddy.x`, `vaddz.x`, `vsqrt Q, vf5x`, `vdiv Q, vf0w, vf5x`
  (1 / length), `vsub.xyzw vf6, vf0, vf0` (all zero), `vmulq.xyz vf6, vf4, Q`, store: x, y, z only, w = 0, and no
  threshold (a zero length gives Q = +max, and 0 * max = 0).
- **Impact in this game:** none expected. The one caller, SUBMAIN `0x3475C0`, normalizes `OuterProduct(e1, e2)`
  (w = 0 already, `0x347784`-`0x347798`) and keeps x, y, z (`0x3477a4`-`0x3477c8`). Only edges shorter than 1e-6
  change (stub: 0; libvu0: unit length).
- **Fix:** PS2Recomp 0065, helper `normalizeVu0`.

### 4. Normalize's mistake reappears in CameraMatrix and NormalLightMatrix

- **Found by:** libvu0's CameraMatrix (`0x17F5C4`-`0x17F624`) is calls: UnitMatrix, OuterProduct(yd, zd), Normalize
  (row 0), Normalize(zd) (row 2), OuterProduct(row 2, row 0) (row 1), TransMatrix(p), InversMatrix. NormalLightMatrix
  (`0x17F670`-`0x17F6E8`): ScaleVector(l, -1) and Normalize per light, row 3 = (0, 0, 0, 1), TransposeMatrix. The stubs
  had their own four-lane normalize lambdas.
- **Fix:** 0067, 0068 use `normalizeVu0`. Everything else in both matched.

### 5. ScaleVector's fallback

- **Seen:** with `$f12 == 0` the stub took the scale from the bits of `$a2`, then from `$a2` as an integer.
- **Found by:** `0x17EE58`-`0x17EE64`: `mfc1 t0, $f20` (the saved `$f12`), `qmtc2`, `vmulx.xyzw`. `$a2` is never read.
- **Fix:** 0066. (libvu0's own NormalLightMatrix calls ScaleVector with -1, so it was not affected.)

### 6. The clip functions return VU status flags

- **Found by:** ClipScreen `0x17FCA4`-`0x17FCE4`: vf6.xy = 4096.0 (built as `0x45800000` twice in `$v1`),
  `ctc2 zero, vi16` (clears the status flag), `vsub.xyw vf5, vf7, vf4` (v - 0), `vsub.xy vf5, vf6, vf7` (4096 - v),
  five `vnop`, `cfc2 v1, vi16`, `andi v1, v1, 0xc0`. ClipScreen3 does the same for three vertices before one read.
  ClipAll `0x17FE18`-`0x17FE64`: t = v * m; vf11 = lo.xyz * t.w, vf12 = hi.xyz * t.w; status cleared;
  `vsub.xyw vf11, vf8, vf11` and `vsub.xyw vf12, vf12, vf8`, with vf11.w = lo.w and vf12.w = hi.w; returns 0 at the
  first vertex with `STATUS & 0xC0 == 0`, else 1.
- **Cause:** status bits 0x40/0x80 are the sticky zero/sign flags. Zero and sign follow PCSX2's `VU_MAC_UPDATE`
  (`pcsx2/VUflags.cpp:15-47`, C:\ob\pcsx2 v2.9.134): zero when the exponent is 0 (+-0 and denormals), sign from bit 31.
  So ClipScreen's "inside" is x, y in (0, 4096) with w > 0, and its return values are 0, 0x40, 0x80, 0xC0; the stub
  tested -4096..4096 and returned 1/2/4/8. ClipAll's stub skipped the w lane and counted equality as inside.
- **Fix:** 0069 (helper `vuStickyZeroSign`), 0070. The older tests encoded the stubs' reading and were corrected (w = 1
  in the ClipScreen tests, hi.w = 100 in the ClipAll ones), as 0063/0064 did.

### 7. Tests first, then the commits

- **Found by:** all ten new tests (and the corrected old ones) applied to `C:\ob\src` with the stubs unchanged, one
  `ob.sh test` run: 539/550, the 11 failures exactly the new or corrected tests, at least one per patch (0065: 2, 0066: 1, 0067: 1, 0068: 1, 0069: 4, 0070: 2). Then each patch committed in `C:\ob\up` with its own tests (session scratchpad
  `edits.py` applies one patch's test part or test + fix), synced, one run: 550/550 and every suite green (iop 5/5, gs_cache 45/45, ob_diag 23/23, pcsx2_vu 3/3, pcsx2_gs 84/84, pcsx2_spu2 7/7, pcsx2_ipu 22/22), 1 min 37 s; `ob.sh recompile` 37 s; gate 3/3 gated green, 3 advisory, 78 s.
- **Evidence:** `C:\ob\logs\ob-libvu0-without.log`, `C:\ob\logs\ob-libvu0-with.log`, `C:\ob\logs\ob-libvu0-gate.log`.

## Mistakes and corrections

| Claimed | What was true | How it was caught | Corrected in |
|---|---|---|---|
| (while reading) RotMatrixZ's sin/cos branch: angle > 0 picks pi/2 + angle | `eedis.py` prints `c.lt.s` as `fd, fs, ft`; the test is fs < ft (angle < 0) | re-reading the operand order | the RotMatrixZ/X/Y row (the result is the same: (sin, cos), so the rotation matrices match) |
| `edits.py` written with CRLF conversions through a Bash heredoc | the `\r\n` escapes became real line breaks (the known heredoc trap) | Python syntax error | fixed with a separate script file |

## Still open

- **sceVpu0Reset** (called once at start-up, main `0x19EB50`): the stub does nothing; the original writes VIF0
  registers (`0x10003810` FBRST = 1, `0x10003820`/`0x10003830` = 0), sets FBRST bit 1 in vi28 (VU0 reset), `sync`, and
  sends `0x01000404` STCYCL, `0x20000000` STMASK 0, `0x05000000` STMOD, `0x04000000` ITOP to the VIF0 FIFO. Not math;
  harmless so far, but un-stubbing it (letting the game's code run) would be the faithful choice.
- **_sceVu0ecossin**: an internal register helper bound to a C-ABI stub. Unreachable in this game (only libvu0's own
  RotMatrixX/Y/Z call it, and they are stubs); taking it out of `stubs` in `decomp.toml` would let the real code run.
- **Edge values not modelled in any stub:** VU division by 0 gives +-max (DivVector, DivVectorXYZ, RotTransPers'
  divide; the stubs give 0; PS2Recomp's README already lists which max is right as unknown); FTOI out of range
  saturates on the VU (`static_cast` gives INT_MIN on x86); the VU rounds toward zero and has no denormals. None
  reachable from this game's calls as far as checked.
- Upstream PS2Recomp has the same stubs; reporting 0062-0070 there leaves the machine, so only with the developer's OK.

## Useful for other developers

- Each HLE stub can be checked against the game's own copy: libvu0 is linked into the ELF, and a 100-line Python
  disassembler for EE + COP2 macro mode + MMI is enough (`eedis.py`; PCSX2's opcode tables for the encodings).
- libvu0's Normalize, InnerProduct and the clip functions work on x, y, z (and w only where named); stubs written from
  descriptions tend to use all four lanes.
