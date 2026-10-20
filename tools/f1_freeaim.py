"""Free vertical aim for File #1: switch off the aim-elevation servo so we can set the angle.

What the servo is (found on the device, 2026-10-20; the decomp doesn't cover it yet):
  A GAME.BIN function (0x556800 in the user's English-patched build) runs each logic tick for
  OUR player only (it returns early for NPCs and for anyone but player number 0x48BF7D). It
  steps the elevation at player +0xBC8 toward +8191 (up held) / -8191 (down held) by 1170, or
  back to 0 by 2730, through a helper `step(&value, target, rate)`; a snap mode sets +-8192 or 0;
  a CPU branch aims at a target. Every other store to +0xBC8 is a one-off reset to 0.

So turning that one function into `jr ra; nop` leaves the angle to us, for our player only.
The function is found by its contents (the 1170 and 2730 step constants and its +0xBC8
stores), not by address, so a build with a different GAME.BIN layout is found too. The two
original words are kept and put back on exit. GAME.BIN being reloaded simply undoes the patch,
and `ensure()` re-applies it.

No game code is stored here: only the shape we search for (immediates and opcodes).
"""
import struct

import mips

AIM_ELEV = 0x0BC8
RATE_STEP = 0x24060492    # addiu $a2, $zero, 1170   (the up/down step)
RATE_BACK = 0x24060AAA    # addiu $a2, $zero, 2730   (the return-to-level step)
JR_RA = 0x03E00008
SCAN = (0x00540000, 0x00900000)   # GAME overlay slots (decomp docs/LAYOUT.md)
PATCH = (JR_RA, mips.NOP)


def is_store_elev(w):
    return (w >> 26) == 0x29 and (w & 0xFFFF) == AIM_ELEV       # sh $x, 0xBC8($y)


def find_servo(read_block):
    """Address of the servo function, or None. `read_block(addr, size)` reads EE RAM."""
    lo, hi = SCAN
    chunk, pad = 0x10000, 0x400                              # pad: read around each chunk so a
    for base in range(lo, hi, chunk):                        # function on a boundary isn't split
        start = max(lo, base - pad)
        data = read_block(start, min(hi, base + chunk + pad) - start)
        words = struct.unpack(f"<{len(data) // 4}I", data)
        first = (base - start) // 4
        for i in range(first, first + chunk // 4):
            if i >= len(words) or words[i] != RATE_BACK:
                continue
            near = words[max(0, i - 24):i]
            if near.count(RATE_STEP) != 2:
                continue
            # Walk back to the prologue: `addiu $sp, $sp, -N` right after the previous `jr ra`.
            for j in range(i, max(-1, i - 128), -1):
                if (words[j] >> 16) == 0x27BD and words[j] & 0x8000:
                    prev = words[max(0, j - 4):j]
                    if JR_RA in prev:
                        end = next((k for k in range(i, min(len(words), i + 64)) if words[k] == JR_RA), None)
                        body = words[j:end] if end else []
                        if sum(is_store_elev(x) for x in body) >= 5:
                            return start + 4 * j
                        break
    return None


def patch_word64():
    return PATCH[0] | (PATCH[1] << 32)


class Servo:
    """Find, switch off, keep off and restore the aim servo through a PINE client."""

    def __init__(self, pine):
        self.pine, self.addr, self.orig = pine, None, None

    def locate(self):
        self.addr = find_servo(self.pine.read_block)
        return self.addr

    def _words(self):
        return struct.unpack("<2I", self.pine.read_block(self.addr, 8))

    def disable(self):
        """Patch the servo's first two instructions to return at once. Returns a status string."""
        if self.addr is None and self.locate() is None:
            return "servo not found (in gameplay? File #1 only)"
        if self.addr % 8:
            return f"servo at 0x{self.addr:08X} is not 8-aligned; refusing (no atomic write)"
        cur = self._words()
        if cur == PATCH:
            return f"servo at 0x{self.addr:08X} already off"
        if mips.is_branch_or_jump(cur[1]) or (cur[0] >> 16) != 0x27BD:
            return f"servo at 0x{self.addr:08X} doesn't start like a function (0x{cur[0]:08X}); refusing"
        self.orig = cur
        self.pine.write64(self.addr, patch_word64())
        return f"servo at 0x{self.addr:08X} switched off"

    def ensure(self):
        """Re-apply after GAME.BIN reloads (the original words come back). Returns True if re-applied."""
        if self.addr is None or self.orig is None:
            return False
        cur = self._words()
        if cur == self.orig:
            self.pine.write64(self.addr, patch_word64())
            return True
        if cur != PATCH:                      # something else is loaded there now: find it again
            self.addr = self.orig = None
        return False

    def restore(self):
        if self.addr is not None and self.orig is not None and self._words() == PATCH:
            self.pine.write64(self.addr, self.orig[0] | (self.orig[1] << 32))
            return f"servo at 0x{self.addr:08X} restored"
        return "servo untouched"
