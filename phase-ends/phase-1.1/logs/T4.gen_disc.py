"""Scratch: discovery script for T4 (blind auto-press cycle + watches + sparse captures)."""
import sys

out = sys.argv[1]
lines = [
    "timeout 480",
    "watch 0x002F85D8 2      # boot task (slot 0) step,sub",
    "watch 0x0023BE34 4      # TitleWork*",
    "watch *0x0023BE34+0 4   # TitleWork state,sub,step,pad",
    "watch 0x00239F30 1      # overlay id at 0x540000",
    "watch 0x0023BBB0 4      # update-check answer",
]
k = 0


def cap():
    global k
    lines.append(f"capture d{k:04d} 1")
    k += 1


for cycle in range(32):
    lines.append("press left 8")
    for _ in range(4):
        lines.append("wait vsync 58")
        cap()
    lines.append("press circle 8")
    for _ in range(2):
        lines.append("wait vsync 55")
        cap()
    lines.append("press start 8")
    for _ in range(2):
        lines.append("wait vsync 55")
        cap()
lines.append("quit")
open(out, "w", newline="\n").write("\n".join(lines) + "\n")
