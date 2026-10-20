#!/usr/bin/env python3
"""A crosshair for File #1 drawn by the game's own on-screen text lines: data only, no code.

The game keeps 32 text lines (decomp main/font.c, D_002F7440 + 0x10, 0x8C bytes each:
used s16, x s16, y s16, timer s16, size u8, fade s8, alpha u8, prio u8, text[0x80]). A
per-frame callback (func_001B3400, registered by ingame.c through func_001B3920) draws every
used line, if Options+0x16 != 0 and 0x48C024 == 0. Another per-frame routine (func_001B2D00)
counts each line's timer down and frees it at 0, and clears them all while Options+0x2B == 0.
Overlapping lines: the one with the larger prio number is hidden, so ours uses 0xFF and always
gives way to the game's own messages.

  python tools/f1_crosshair.py check       READ-ONLY: are those conditions met in this build?
  python tools/f1_crosshair.py show 5      draw "+" at the centre for 5 s, then remove it
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase2_check  # noqa: E402

LINES = 0x002F7440 + 0x10
LINE_SIZE, N_LINES = 0x8C, 32
CB_COUNT, CB_LIST = 0x0023BB30, 0x0029B0D0       # per-frame callbacks: count, fn pointers[8]
DRAWER = 0x001B3400                              # func_001B3400: draws the text lines
OPTIONS = 0x00306590
DRAW_ON, LINES_ON = OPTIONS + 0x16, OPTIONS + 0x2B
DRAW_BLOCK = 0x0048C024                          # must be 0 for the lines to be drawn
SCREEN_W, SCREEN_H = 640, 448                    # Camera.width 0x280, height 0x1C0 (camera.h)
SLOT = N_LINES - 1                               # the last line: the game fills from the start
OUR_PRIO = 0xFF


def line_addr(i):
    return LINES + i * LINE_SIZE


def pack_line(x, y, size, text, alpha=0x80, timer=0x7FFF, prio=OUR_PRIO):
    body = text.encode("ascii") + b"\0"
    return struct.pack("<hhhhBbBB", 1, x, y, timer, size, 0, alpha, prio) + body.ljust(0x80, b"\0")


class Crosshair:
    """Show/keep/hide one text line. Never touches a line the game is using."""

    def __init__(self, pine, size=24, text="+", dx=0, dy=0, slot=SLOT):
        self.pine, self.size, self.text, self.slot = pine, size, text, slot
        # The string is drawn from its left edge; a half-width glyph is about size/2 wide.
        self.x = SCREEN_W // 2 - (len(text) * size) // 4 + dx
        self.y = SCREEN_H // 2 - size // 2 + dy
        self.shown = False

    def _ours(self):
        raw = self.pine.read_block(line_addr(self.slot), 0x10)
        used, = struct.unpack_from("<h", raw, 0)
        return used == 0 or raw[0x0B] == OUR_PRIO

    def show(self):
        """Draw (or keep drawing) the crosshair. Returns False if the slot is the game's."""
        if not self.shown and not self._ours():
            return False
        line = pack_line(self.x, self.y, self.size, self.text)
        self.pine.write_multi([(line_addr(self.slot), list(struct.unpack(f"<{len(line) // 4}I", line)))])
        self.shown = True
        return True

    def hide(self):
        if self.shown and self._ours():
            self.pine.write_multi([(line_addr(self.slot), [0])])     # used = 0 (and x = 0)
        self.shown = False


def cmd_check():
    pine, _ = phase2_check.connect_game()
    ok = True
    try:
        n = struct.unpack("<i", pine.read_block(CB_COUNT, 4))[0]
        fns = struct.unpack("<8I", pine.read_block(CB_LIST, 32))
        drawer = DRAWER in fns[:max(0, min(n, 8))]
        print(f"per-frame callbacks: {n}: " + " ".join(f"0x{f:08X}" for f in fns[:max(0, min(n, 8))]))
        print(f"  text-line drawer 0x{DRAWER:08X} registered: {'yes' if drawer else 'NO'}")
        prologue = struct.unpack("<I", pine.read_block(DRAWER, 4))[0]
        looks = (prologue >> 16) == 0x27BD and prologue & 0x8000
        print(f"  drawer code starts with a prologue (0x{prologue:08X}): {'yes' if looks else 'NO (different build?)'}")
        draw_on, lines_on = pine.read_block(DRAW_ON, 1)[0], pine.read_block(LINES_ON, 1)[0]
        block = pine.read_block(DRAW_BLOCK, 1)[0]
        print(f"  Options+0x16 = {draw_on} (needs != 0), Options+0x2B = {lines_on} (needs != 0), "
              f"0x48C024 = {block} (needs 0)")
        ok = drawer and looks and draw_on != 0 and lines_on != 0 and block == 0
        used = []
        for i in range(N_LINES):
            raw = pine.read_block(line_addr(i), 0x14)
            if struct.unpack_from("<h", raw, 0)[0]:
                x, y, timer = struct.unpack_from("<hhh", raw, 2)
                used.append(f"#{i} at ({x},{y}) size {raw[8]} prio {raw[0x0B]} timer {timer} text {raw[0x0C:0x14]!r}")
        print(f"  lines in use: {len(used)}" + "".join("\n    " + u for u in used))
        free = not any(u.startswith(f"#{SLOT} ") for u in used)
        print(f"  our slot #{SLOT} free: {'yes' if free else 'NO'}")
        ok = ok and free
    finally:
        pine.close()
    print("\nREADY: `show` should draw a crosshair." if ok else
          "\nNOT READY in this state (in gameplay? menus closed?). Paste this to Claude.")
    return 0 if ok else 2


def cmd_show(seconds):
    pine, _ = phase2_check.connect_game()
    c = Crosshair(pine)
    try:
        if not c.show():
            print("the slot is in use by the game; not drawing")
            return 2
        print(f"drawing '+' at ({c.x},{c.y}) for {seconds:g} s ...")
        t_end = time.time() + seconds
        while time.time() < t_end:
            time.sleep(0.5)
            c.show()                                  # keep the timer topped up
    finally:
        c.hide()
        pine.close()
    print("removed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "check":
        sys.exit(cmd_check())
    if len(sys.argv) > 1 and sys.argv[1] == "show":
        sys.exit(cmd_show(float(sys.argv[2]) if len(sys.argv) > 2 else 5))
    print(__doc__)
