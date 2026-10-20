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
# The presets padread.c copies into PAD_MAP when a controller connects, by PadPort.kind
# (D_00292D60[port] + 7: 0 digital, 1 analog, 2 analog with pressure).
PAD_KIND = 0x00292D60 + 7
PRESETS = {0: 0x00213620, 1: 0x002135A0, 2: 0x002135A0}
L2, R2, R1, CIRCLE, CROSS = 0x0001, 0x0002, 0x0008, 0x0020, 0x0040

# entry -> (mask, group). Group travels with the button (it looks like the pressure group:
# 1 D-pad, 2 face, 3 shoulders, 0 none).
MODERN = {
    6: (L2, 3),              # aim without a target: L2
    5: (R2 | CIRCLE, 3),     # fire / interact: R2, and circle still works
    10: (0, 0),              # aim WITH lock-on (was R1): off; with free aim the camera aims
    7: (0, 0),               # L2's old action: off (it pairs with right-stick flicks; the camera owns that stick)
}
# Entry 4 (game bit 0x10) is run, Kevin's kick while an aim bit 0x440 is held (charmove.c
# func_0061E810) AND "back" in the menus. The map can't tell them apart, so it follows the context
# (device reports: cross shouldn't run, but must still kick and back out of menus):
#   aiming                    -> cross (the kick)
#   not aiming, moving        -> R1 only (so cross + a direction doesn't run)
#   not aiming, not moving    -> R1 or cross (cross backs out of menus; tapping it standing still
#                                does what it did in the original game)
DIRECTIONS = 0xF000           # the D-pad bits of the raw button word
DPAD_DOWN = 0x4000
SELECT, R3 = 0x0100, 0x0400
QUIT_COMBO = SELECT | R3      # held ~1 s: stop the mod from the pad (on the device, no Ctrl+C)


def plausible(raw):
    """False for a pad read caught while the game rewrites it (device: 0xFFFF, 0xFFF7 -- every
    button 'held'). A D-pad can't press up with down, or left with right."""
    return (raw & 0x5000) != 0x5000 and (raw & 0xA000) != 0xA000


def quick_turn(raw, prev_raw, lx, ly):
    """Down + cross, the classic quick turn: 'stick' or 'dpad' on the frame cross goes down while
    down is held (left stick pulled most of the way, within about 30 degrees of straight down: a
    run that only leans toward you shouldn't count), else None.
    lx, ly: the left stick in -1..1, ly > 0 = pulled toward you."""
    if not (raw & CROSS) or prev_raw & CROSS:
        return None
    if raw & DPAD_DOWN:
        return "dpad"
    return "stick" if ly > 0.7 and abs(lx) < 0.58 * ly else None


def run_entry(aiming, moving, cross_runs=False):
    if cross_runs:
        return (R1 | CROSS, 3)
    if aiming:
        return (CROSS, 2)
    return (R1, 3) if moving else (R1 | CROSS, 3)


class ButtonMap:
    """Apply, keep and restore the modern layout through a PINE client (port 0)."""

    def __init__(self, pine, layout=None, cross_runs=False):
        self.pine, self.layout, self.orig = pine, layout or MODERN, None
        self.cross_runs = cross_runs
        self.ctx = (False, False)                 # (aiming, moving)

    def _read(self):
        raw = self.pine.read_block(PAD_MAP, ENTRIES * 8)
        return [struct.unpack_from("<II", raw, 8 * i) for i in range(ENTRIES)]

    def _wanted(self, base):
        layout = {**self.layout, 4: run_entry(*self.ctx, self.cross_runs)}
        return [layout.get(i, base[i]) for i in range(ENTRIES)]

    def _write(self, entries):
        self.pine.write_multi([(PAD_MAP, [w for e in entries for w in e])])

    def set_context(self, aiming, moving):
        """Switch entry 4 (run / kick / menu back) for the context. Returns True if it changed."""
        new = (aiming, moving)
        if self.orig is None or new == self.ctx:
            return False
        before = run_entry(*self.ctx, self.cross_runs)
        self.ctx = new
        after = run_entry(*new, self.cross_runs)
        if after != before:
            self.pine.write_multi([(PAD_MAP + 4 * 8, list(after))])
        return after != before

    def _preset(self):
        """The game's own default map for this controller, or None if it doesn't look like one.
        Restoring to it is right even if an earlier run left the map changed."""
        kind = self.pine.read_block(PAD_KIND, 1)[0]
        if kind not in PRESETS:
            return None
        raw = self.pine.read_block(PRESETS[kind], ENTRIES * 8)
        table = [struct.unpack_from("<II", raw, 8 * i) for i in range(ENTRIES)]
        single = all(m == 0 or (m & (m - 1) == 0 and m < 0x10000) for m, _ in table)
        if single and all(g <= 3 for _, g in table) and sum(1 for m, _ in table if m) >= 12:
            return table
        return None

    def apply(self):
        cur = self._read()
        if not any(m for m, _ in cur):
            return "button map is empty (no controller read yet?); not changed"
        if self.orig is None:
            self.orig = self._preset() or cur
        self._write(self._wanted(self.orig))
        return ("modern buttons: L2 aims, R2 fires (circle still works), R1 runs; "
                + ("cross also runs (--cross-runs)" if self.cross_runs else
                   "cross kicks while aiming and backs out of menus, but doesn't run")
                + "; lock-on aim and L2's old action off")

    def ensure(self):
        """Re-apply if the game reset the map (controller reconnect). True if re-applied."""
        if self.orig is None:
            return False
        cur = self._read()
        if cur == self._wanted(self.orig):
            return False
        self.orig = self._preset() or (cur if cur != self.orig else self.orig)   # reconnect: the preset
        self._write(self._wanted(self.orig))
        return True

    def restore(self):
        if self.orig is None:
            return "buttons untouched"
        self._write(self.orig)
        return "original buttons restored"
