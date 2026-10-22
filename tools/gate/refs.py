"""Reference-capture helper for the boot-screen gate (phase 1.2).

Usage (cwd = repo root):
  python tools/gate/refs.py check-settings [--ini P]
      Effective PCSX2 settings (ini overlaid by ps2_emu/gamesettings/SLPM-65428*.ini)
      vs EXPECTED; prints each mismatch; exit 0 only if none.
  python tools/gate/refs.py begin [--out D]
      Writes the capture-session marker <out>/session.json (default out C:/ob/ref).
  python tools/gate/refs.py intake [--dry-run] [--snaps D] [--out D] [--ini P] [--manifest P]
      Maps snaps newer than the marker to ORDER screens (takes a then b), checks 512x448.
      --dry-run writes nothing. A real run needs exactly 12 good snaps; it copies them
      (snaps are never moved or deleted) to <out>/<screen>-<take>.png, writes the
      manifest (default gate/ref_manifest.json) and <out>/sheet.png.
"""
import argparse
import datetime
import glob
import hashlib
import json
import os
import shutil
import sys

from PIL import Image, ImageDraw

ORDER = ["savedata", "access", "capcom", "title", "mainmenu", "charselect"]
PCSX2_VERSION = "2.9.134"
DEFAULT_INI = "ps2_emu/inis/PCSX2.ini"
GAMESETTINGS_GLOB = "ps2_emu/gamesettings/SLPM-65428*.ini"
DEFAULT_SNAPS = "ps2_emu/snaps"
DEFAULT_OUT = "C:/ob/ref"
DEFAULT_MANIFEST = "gate/ref_manifest.json"
BOOT_SCRIPT = "gate/boot.script"
REF_SIZE = (512, 448)

GS = "EmuCore/GS"
EXPECTED = [
    ("Folders", "Snapshots", "snaps"),
    (GS, "Renderer", "13"),
    (GS, "ScreenshotSize", "2"),
    (GS, "ScreenshotFormat", "0"),
    (GS, "OrganizeScreenshotsByGame", "false"),
    (GS, "deinterlace_mode", "1"),
    (GS, "linear_present_mode", "0"),
    (GS, "CropLeft", "0"),
    (GS, "CropTop", "0"),
    (GS, "CropRight", "0"),
    (GS, "CropBottom", "0"),
    (GS, "fxaa", "false"),
    (GS, "ShadeBoost", "false"),
    (GS, "TVShader", "0"),
    (GS, "CASMode", "0"),
    (GS, "pcrtc_offsets", "false"),
    (GS, "pcrtc_overscan", "false"),
    (GS, "upscale_multiplier", "1"),
]
RECORDED = [(GS, "pcrtc_antiblur"), (GS, "dithering_ps2"), (GS, "AspectRatio")]


def parse_ini(path, into):
    """Hand parser: `[section]` and `key = value`; later lines win. Case kept."""
    section = None
    with open(path, encoding="utf-8-sig") as f:
        for line in f:
            s = line.strip()
            if not s or s[0] in ";#":
                continue
            if s.startswith("[") and s.endswith("]"):
                section = s[1:-1]
                continue
            if "=" in s and section is not None:
                k, v = s.split("=", 1)
                into[(section, k.strip())] = v.strip()
    return into


def effective(ini):
    vals = parse_ini(ini, {})
    for gs in sorted(glob.glob(GAMESETTINGS_GLOB)):
        parse_ini(gs, vals)
    return vals


def cmd_check_settings(a):
    vals = effective(a.ini)
    bad = 0
    for sec, key, want in EXPECTED:
        have = vals.get((sec, key))
        if have != want:
            bad += 1
            print(f"[{sec}] {key}: have {'missing' if have is None else have} want {want}")
    print(f"{bad} mismatch(es)" if bad else "settings OK")
    return 1 if bad else 0


def cmd_begin(a):
    os.makedirs(a.out, exist_ok=True)
    t = datetime.datetime.now().timestamp()
    iso = datetime.datetime.fromtimestamp(t).astimezone().isoformat(timespec="seconds")
    with open(os.path.join(a.out, "session.json"), "w", encoding="utf-8") as f:
        json.dump({"t": t, "iso": iso}, f, indent=2)
    print(f"session marker {iso} -> {os.path.join(a.out, 'session.json')}")
    return 0


def capture_window():
    """{screen: n} from the `capture <screen> <n>` lines of BOOT_SCRIPT (the gate's frames per screen)."""
    win = {}
    with open(BOOT_SCRIPT, encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if len(p) == 3 and p[0] == "capture" and p[2].isdigit():
                win[p[1]] = int(p[2])
    return win


def cmd_intake(a):
    marker = os.path.join(a.out, "session.json")
    if not os.path.isfile(marker):
        print("no session marker: run begin first")
        return 2
    with open(marker, encoding="utf-8") as f:
        sess = json.load(f)
    pngs = [(os.path.getmtime(p), os.path.basename(p), p) for p in glob.glob(os.path.join(a.snaps, "*.png"))]
    new = sorted((x for x in pngs if x[0] > sess["t"]), key=lambda x: (x[0], x[1]))
    print(f"{len(pngs) - len(new)} older snap(s) ignored")
    plan, rejects = [], 0
    for i, (_, name, path) in enumerate(new):
        screen, take = ORDER[i % len(ORDER)], ("a" if i < len(ORDER) else "b")
        with Image.open(path) as im:
            w, h = im.size
        ok = (w, h) == REF_SIZE
        rejects += not ok
        print(f"{name} -> {screen}-{take} {w}x{h} {'OK' if ok else 'REJECT(size)'}")
        plan.append((screen, take, name, path, w, h))
    full = 2 * len(ORDER)
    print(f"{len(new)}/{full} present")
    if a.dry_run:
        return 0 if rejects == 0 and len(new) <= full else 1
    if rejects or len(new) != full:
        print(f"intake refused: need exactly {full} snaps and no REJECT; nothing written")
        return 1
    vals = effective(a.ini)
    settings = {k: vals.get((s, k), "missing") for s, k, _ in EXPECTED}
    settings.update({k: vals.get((s, k), "missing") for s, k in RECORDED})
    refs = []
    for screen, take, name, path, w, h in sorted(plan, key=lambda p: (ORDER.index(p[0]), p[1])):
        dst = os.path.join(a.out, f"{screen}-{take}.png")
        shutil.copy2(path, dst)
        with open(dst, "rb") as f:
            sha = hashlib.sha256(f.read()).hexdigest()
        refs.append({"screen": screen, "take": take, "file": os.path.basename(dst),
                     "sha256": sha, "w": w, "h": h, "source": name})
    manifest = {"pcsx2": PCSX2_VERSION, "settings": settings, "window": capture_window(),
                "session": sess["iso"], "refs": refs}
    md = os.path.dirname(a.manifest)
    if md:
        os.makedirs(md, exist_ok=True)
    with open(a.manifest, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    write_sheet(a.out, refs)
    print(f"wrote {len(refs)} refs to {a.out}, manifest {a.manifest}, sheet {os.path.join(a.out, 'sheet.png')}")
    return 0


def write_sheet(out, refs):
    """6 cols (ORDER) x 2 rows (takes a, b) of half-size thumbnails with labels."""
    tw, th, lab = REF_SIZE[0] // 2, REF_SIZE[1] // 2, 28
    sheet = Image.new("RGB", (tw * len(ORDER), (th + lab) * 2), "black")
    draw = ImageDraw.Draw(sheet)
    for r in refs:
        col, row = ORDER.index(r["screen"]), (0 if r["take"] == "a" else 1)
        x, y = col * tw, row * (th + lab)
        with Image.open(os.path.join(out, r["file"])) as im:
            sheet.paste(im.convert("RGB").resize((tw, th)), (x, y + lab))
        draw.text((x + 2, y + 2), f"{r['screen']}-{r['take']}", fill="white")
        draw.text((x + 2, y + 14), r["source"][-24:], fill="gray")
    sheet.save(os.path.join(out, "sheet.png"))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check-settings")
    c.add_argument("--ini", default=DEFAULT_INI)
    c.set_defaults(fn=cmd_check_settings)
    b = sub.add_parser("begin")
    b.add_argument("--out", default=DEFAULT_OUT)
    b.set_defaults(fn=cmd_begin)
    i = sub.add_parser("intake")
    i.add_argument("--dry-run", action="store_true")
    i.add_argument("--snaps", default=DEFAULT_SNAPS)
    i.add_argument("--out", default=DEFAULT_OUT)
    i.add_argument("--ini", default=DEFAULT_INI)
    i.add_argument("--manifest", default=DEFAULT_MANIFEST)
    i.set_defaults(fn=cmd_intake)
    a = p.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
