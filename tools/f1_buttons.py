"""Modern button layout for File #1 while the camera mod runs: L2 aims, R2 fires.

How the game maps buttons (decomp main/padread.c, confirmed on the device 2026-10-20):
  D_00292C80[port][16] is {mask, group}: entry i sets game bit (1 << i) when
  `raw buttons & mask` (padread.c:377), so a mask with several bits means "any of these".
  It is filled from a preset table once, when a controller connects (padread.c:255-273).
Device dump (port 0): [5] CIRCLE -> 0x20 (fire/interact), [6] R2 -> 0x40 (aim, no target),
[7] L2 -> 0x80 (with a right-stick flick: special steps, e.g. voice lines), [10] R1 -> 0x400
(aim with lock-on), [11] L1 -> 0x800 (use the item in hand), the rest as the PS2 pad.

Raw button bits (the u16 the masks test; its bytes are swapped against the usual PS2 listing):
  0x0001 L2  0x0002 R2  0x0004 L1  0x0008 R1  0x0010 TRIANGLE  0x0020 CIRCLE  0x0040 CROSS
  0x0080 SQUARE  0x0100 SELECT  0x0200 L3  0x0400 R3  0x0800 START  0x1000 UP  0x2000 RIGHT
  0x4000 DOWN  0x8000 LEFT
"""
import struct

PAD_MAP = 0x00292C80
ENTRIES = 16
L2, R2, CIRCLE = 0x0001, 0x0002, 0x0020

# entry -> (mask, group). Group travels with the button (it looks like the pressure group:
# 1 D-pad, 2 face, 3 shoulders, 0 none).
MODERN = {
    6: (L2, 3),              # aim without a target: L2
    5: (R2 | CIRCLE, 3),     # fire / interact: R2, and circle still works
    7: (0, 0),               # L2's old action: off (it pairs with right-stick flicks; the camera owns that stick)
}


class ButtonMap:
    """Apply, keep and restore the modern layout through a PINE client (port 0)."""

    def __init__(self, pine, layout=None):
        self.pine, self.layout, self.orig = pine, layout or MODERN, None

    def _read(self):
        raw = self.pine.read_block(PAD_MAP, ENTRIES * 8)
        return [struct.unpack_from("<II", raw, 8 * i) for i in range(ENTRIES)]

    def _wanted(self, base):
        return [self.layout.get(i, base[i]) for i in range(ENTRIES)]

    def _write(self, entries):
        self.pine.write_multi([(PAD_MAP, [w for e in entries for w in e])])

    def apply(self):
        cur = self._read()
        if not any(m for m, _ in cur):
            return "button map is empty (no controller read yet?); not changed"
        if self.orig is None:
            self.orig = cur
        self._write(self._wanted(self.orig))
        return "modern buttons: L2 aims, R2 fires (circle still works), L2's old action off"

    def ensure(self):
        """Re-apply if the game reset the map (controller reconnect). True if re-applied."""
        if self.orig is None:
            return False
        cur = self._read()
        if cur == self._wanted(self.orig):
            return False
        self.orig = cur if cur != self.orig else self.orig   # a new preset: build on it
        self._write(self._wanted(self.orig))
        return True

    def restore(self):
        if self.orig is None:
            return "buttons untouched"
        self._write(self.orig)
        return "original buttons restored"
