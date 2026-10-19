#!/usr/bin/env python3
"""READ-ONLY check that File #1's camera hook functions are where the decomp says, in the
build that is actually running (retail JP or the English fan patch).

The decomp (GalaxyNostalgia/Outbreak-decomp, CC0) gives, for SUBMAIN.BIN (loaded at 0x340040):
  func_00345740  "hands the camera to the renderer": calls func_0018F0E0, func_001AE9C0(0x18, ..),
                 func_00345870, func_001AE9C0(0x16, ..)          (src/SLPM_654.28/submain/ingame.c)
  func_00345870  "the view matrix from the camera": calls func_0018F7C0, func_00340210, func_001911F0,
                 func_00190C50, func_00190BF0
The English patch does not change SUBMAIN.BIN (config/versions.json), so both builds should pass.

No game code is stored here: the expected words are MIPS `jal` encodings computed from those
call targets (0x0C000000 | target >> 2), plus the shape of a function prologue.
SUBMAIN must be loaded, so run this during gameplay.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase2_check  # noqa: E402
import pine_diag  # noqa: E402

FUNCS = {
    0x00345740: {"name": "camera -> renderer", "size": 0x130,
                 "calls": [0x0018F0E0, 0x001AE9C0, 0x00345870, 0x001AE9C0]},
    0x00345870: {"name": "view matrix", "size": 0x100,
                 "calls": [0x0018F7C0, 0x00340210, 0x001911F0, 0x00190C50, 0x00190BF0]},
}


def jal(target):
    return 0x0C000000 | (target >> 2)


def is_prologue(word):
    """addiu sp, sp, -N: 0x27BDxxxx with a negative immediate."""
    return (word >> 16) == 0x27BD and (word & 0x8000)


def check(words, calls):
    """(prologue ok, calls found in order, list of jal targets seen)."""
    seen = [(w & 0x03FFFFFF) << 2 for w in words if (w >> 26) == 0x03]
    it = iter(seen)
    in_order = all(any(t == c for t in it) for c in calls)
    return is_prologue(words[0]), in_order, seen


def main(argv=None):
    ok, msg = phase2_check.ensure_forward(phase2_check.shutil.which("adb") or "adb", 28011)
    print(("adb: " if ok else "adb problem: ") + msg)
    try:
        pine = pine_diag.Pine("127.0.0.1", 28011, 5.0)
        info = pine_diag.identify(pine)
    except (OSError, pine_diag.PineError) as e:
        print(f"PINE not reachable: {e}")
        return 1
    print(f"{info['id']} CRC {info['crc']} disc version {info['game_version']}")
    all_ok = True
    try:
        for addr, f in FUNCS.items():
            words = list(struct.unpack(f"<{f['size'] // 4}I", pine.read_block(addr, f["size"])))
            pro, in_order, seen = check(words, f["calls"])
            good = pro and in_order
            all_ok &= good
            print(f"[{'PASS' if good else 'FAIL'}] 0x{addr:08X} {f['name']}: prologue {'ok' if pro else 'MISSING'} "
                  f"(0x{words[0]:08X}); expected calls in order {'found' if in_order else 'NOT found'}")
            print("        jal targets seen: " + " ".join(f"0x{t:08X}" for t in seen))
    finally:
        pine.close()
    print("Hook site matches the decomp in this build." if all_ok else
          "Mismatch: SUBMAIN not loaded (are you in gameplay?) or this build differs from the decomp.")
    return 0 if all_ok else 2


if __name__ == "__main__":
    sys.exit(main())
