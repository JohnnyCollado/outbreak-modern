#!/usr/bin/env python3
"""Copy the standalone patch (patches/SLPM-65428_32088394.pnach) to ARMSX2 on the device, over adb.

ARMSX2 reads patches from <data folder>/patches (its Patch Manager lists <data folder>/cheats,
/patches and /cheats_ws). The data folder is the app's own files folder unless a different one
was picked in ARMSX2's setup, so this finds every ARMSX2 data folder on the device -- a folder
with ARMSX2's `inis` (settings) or `memcards` folder in it -- and copies the patch into each.

  python tools/push_patch.py [--serial SERIAL] [--dry-run]
  python tools/push_patch.py --download     only into /sdcard/Download (Android's folder is singular),
                                            for ARMSX2's Patch Manager > Import
"""
import argparse
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phase2_check  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNACH = os.path.join(ROOT, "patches", "SLPM-65428_32088394.pnach")


def shell(adb, *cmd, timeout=60):
    rc, out, err = phase2_check.run(adb + ["shell", *cmd], timeout=timeout)
    return out


def data_roots(adb):
    pkgs = [l.split(":", 1)[1].strip() for l in shell(adb, "pm", "list", "packages").splitlines()
            if "armsx2" in l.lower() and ":" in l]
    found = []
    # the app's own files folder(s), then anywhere on shared storage or an SD card
    probe = " ".join(f"/sdcard/Android/data/{p}/files" for p in pkgs)
    script = (f"for d in {probe}; do [ -d $d ] && echo $d; done; "
              "find /sdcard /storage -maxdepth 6 -type d \\( -name inis -o -name memcards \\) 2>/dev/null")
    for line in shell(adb, script, timeout=120).splitlines():
        d = line.strip()
        if not d:
            continue
        if d.endswith("/inis") or d.endswith("/memcards"):
            d = d.rsplit("/", 1)[0]
        d = d.replace("/storage/emulated/0/", "/sdcard/").replace("/storage/self/primary/", "/sdcard/")
        if d not in found:
            found.append(d)
    return pkgs, found


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial")
    ap.add_argument("--adb", default=shutil.which("adb") or "adb")
    ap.add_argument("--dry-run", action="store_true", help="only show where it would go")
    ap.add_argument("--download", action="store_true",
                    help="put it in /sdcard/Download for Patch Manager > Import, instead of the data folder")
    a = ap.parse_args(argv)
    if not os.path.exists(PNACH):
        raise SystemExit(f"{PNACH} missing: git pull first")
    serial = a.serial
    if not serial:
        rc, out, err = phase2_check.run([a.adb, "devices"])
        serials = [l.split()[0] for l in out.splitlines()[1:] if l.strip() and "device" in l.split()[1:2]]
        serial, why = phase2_check.pick_device(a.adb, serials)
        if not serial:
            raise SystemExit(f"adb: {why}; pass --serial (see `adb devices`)")
    adb = [a.adb, "-s", serial]
    name = os.path.basename(PNACH)
    if a.download:
        # A push to a folder that doesn't exist (e.g. "Downloads") makes a FILE of that name instead.
        rc, out, err = phase2_check.run(adb + ["push", PNACH, f"/sdcard/Download/{name}"], timeout=60)
        listed = shell(adb, f"ls -l '/sdcard/Download/{name}' 2>&1").strip()
        print(f"{'ok' if rc == 0 and name in listed else 'FAILED'}: {listed or err.strip()}")
        print("\nIn ARMSX2: Patch Manager > Import, pick it from Download, enable \"Modern Camera (standalone)\",\n"
              "then restart the game. ARMSX2 doesn't read the Download folder by itself.")
        stray = shell(adb, "[ -f /sdcard/Downloads ] && ls -l /sdcard/Downloads").strip()
        if stray:
            print(f"\nNote: /sdcard/Downloads is a FILE (an earlier push to a folder that doesn't exist):\n  {stray}\n"
                  "  It's a copy of a patch; delete it with: adb shell rm /sdcard/Downloads")
        return 0
    pkgs, roots = data_roots(adb)
    print(f"device {serial}; ARMSX2 package(s): {', '.join(pkgs) or 'NONE'}")
    if not roots:
        raise SystemExit("no ARMSX2 data folder found (no `inis` or `memcards` folder). Open ARMSX2 once, "
                         "or import the file from ARMSX2's Patch Manager instead.")
    for r in roots:
        dest = f"{r}/patches"
        print(f"  data folder {r}  ->  {dest}/{name}")
        if a.dry_run:
            continue
        shell(adb, f"mkdir -p '{dest}'")
        rc, out, err = phase2_check.run(adb + ["push", PNACH, f"{dest}/{name}"], timeout=60)
        listed = shell(adb, f"ls -l '{dest}/{name}' 2>&1").strip()
        print(f"    {'ok' if rc == 0 and name in listed else 'FAILED'}: {listed or err.strip()}")
    print("\nIn ARMSX2: start File #1, open Patch Manager, enable \"Modern Camera (standalone)\".\n"
          "If ARMSX2 was already running, restart the game so it rereads the patches folder.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
