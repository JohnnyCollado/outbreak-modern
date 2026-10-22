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
import json
import os
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
            # Walk back to the prologue: `addiu $sp, $sp, -N` right after the previous `jr ra`, or
            # our own patch there (`jr ra; nop`) if a run already switched the servo off.
            for j in range(i, max(-1, i - 128), -1):
                patched = words[j] == JR_RA and j + 1 < len(words) and words[j + 1] == mips.NOP
                if ((words[j] >> 16) == 0x27BD and words[j] & 0x8000) or patched:
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


STATE_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "results",
                          "f1servo-state.json")


class Servo:
    """Find, switch off, keep off and restore the aim servo through a PINE client.

    The original words are also saved to STATE_FILE, so a later run can restore them even if
    this one never got to (window closed, crash)."""

    def __init__(self, pine, state_file=STATE_FILE):
        self.pine, self.addr, self.orig, self.state_file = pine, None, None, state_file
        self.active = False                   # True while the servo is known to be off

    def _save(self):
        os.makedirs(os.path.dirname(self.state_file), exist_ok=True)
        with open(self.state_file, "w") as f:
            json.dump({"addr": self.addr, "orig": list(self.orig)}, f)

    def _load(self):
        try:
            with open(self.state_file) as f:
                st = json.load(f)
            return tuple(st["orig"]) if st.get("addr") == self.addr else None
        except (OSError, ValueError, KeyError):
            return None

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
            self.active, self.orig = True, self._load()
            return (f"servo at 0x{self.addr:08X} already off (an earlier run left it so); "
                    + ("its original words are on file and will be restored on exit" if self.orig else
                       "its original words are NOT on file: load your save state afterwards to restore it"))
        if mips.is_branch_or_jump(cur[1]) or (cur[0] >> 16) != 0x27BD:
            return f"servo at 0x{self.addr:08X} doesn't start like a function (0x{cur[0]:08X}); refusing"
        self.orig = cur
        self._save()
        self.pine.write64(self.addr, patch_word64())
        self.active = True
        return f"servo at 0x{self.addr:08X} switched off"

    def ensure(self):
        """Re-apply after GAME.BIN reloads (the servo's own code comes back). True if re-applied."""
        if self.addr is None or not self.active:
            return False
        cur = self._words()
        if cur == PATCH:
            return False
        if self.orig is not None and cur != self.orig:     # something else is loaded there now
            self.addr = self.orig = None
            self.active = False
            return False
        self.orig = None                                   # fresh code: record it and patch again
        return "switched off" in self.disable()

    def restore(self):
        if self.addr is not None and self.orig is not None and self._words() == PATCH:
            self.pine.write64(self.addr, self.orig[0] | (self.orig[1] << 32))
            self.active = False
            try:
                os.remove(self.state_file)
            except OSError:
                pass
            return f"servo at 0x{self.addr:08X} restored"
        if self.active and self.orig is None:
            return "servo left OFF (original words unknown): load your save state to get the game's aiming back"
        return "servo untouched"
