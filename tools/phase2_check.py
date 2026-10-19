#!/usr/bin/env python3
"""Automated Phase 2 checks: Windows/Linux/macOS PC -> ADB -> ARMSX2 PINE on the Odin.

Runs everything that does not need a human, then (unless --no-interactive) walks
you through the manual HeySnippy mod checks and records your answers. Writes
results/phase2-<timestamp>.json and .md next to this repo's root.

Before running:
  - Odin: USB debugging on, ARMSX2 >= 2.6.6.5, Settings > Advanced >
    PINE (Remote Control) > Enable PINE.
  - Boot File #1 or #2, reach a controllable character, MAKE A SAVE STATE.
  - Close the camera mod and any other PINE client (PINE serves one client).

Usage:
  python tools/phase2_check.py                  # full run
  python tools/phase2_check.py --no-interactive # automated part only
  python tools/phase2_check.py --adb C:/platform-tools/adb.exe --serial XYZ

Read-only: no guest memory is written by this script.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pine_diag  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MIN_VERSION = (2, 6, 6, 5)  # first tag containing Android PINE (commits 912b1d8f9, 727ffd7c7)

# Serials the mod supports. CRCs come from the community PNACH filenames bundled in
# ARMSX2's resources/patches.zip -- labels, not verified facts. This run verifies them.
KNOWN = {
    "SLPM-65428": {"name": "File #1 (JP)", "crc": {"03c8f393": "retail (per PNACH label)",
                                                   "198efdc1": "English fan patch",
                                                   "32088394": "English fan patch"}},
    "SLPM-65692": {"name": "File #2 (JP)", "crc": {"6fca6fbd": "retail (per PNACH label)",
                                                   "70c1fe97": "English fan patch",
                                                   "dc0bfb60": "English fan patch"}},
    # US releases: a secondary goal of this project, but NOT supported by HeySnippy's mod, whose
    # address maps are JP-only and which refuses any other serial. PINE/latency results from these
    # are still valid. CRCs are from ARMSX2's GameIndex.yaml patches and bundled PNACH filenames;
    # which one is the retail print is not established.
    "SLUS-20765": {"name": "File #1 (US)", "mod_supported": False,
                   "crc": {"0245ef6d": "listed in GameIndex.yaml", "02445b50": "bundled PNACH"}},
    "SLUS-20984": {"name": "File #2 (US)", "mod_supported": False,
                   "crc": {"1e65a50e": "listed in GameIndex.yaml", "1e65a59b": "bundled PNACH"}},
}

MANUAL_CHECKS = [
    ("9a_connects", "Start the mod (pick the matching File). Did its status light go GREEN?"),
    ("9b_camera", "Did the camera move to over-the-shoulder behind your character?"),
    ("9c_input", "With a pad plugged into the PC, does its right stick rotate the camera?"),
    ("9d_smooth", "Is camera motion smooth (no obvious stutter/lag vs. the PCSX2 videos)?"),
    ("9e_stop_restores", "Press Stop in the mod. Did the original fixed cameras come back?"),
]


class Step:
    """One check: name, outcome (PASS/FAIL/WARN/INFO/SKIP), detail, raw evidence."""

    def __init__(self, results, name):
        self.results, self.name = results, name

    def __call__(self, outcome, detail, **evidence):
        self.results["steps"].append({"step": self.name, "outcome": outcome, "detail": detail, **evidence})
        print(f"[{outcome:<4}] {self.name}: {detail}")
        return outcome


def run(cmd, timeout=15, input_bytes=None):
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout, input=input_bytes)
        return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")
    except (OSError, subprocess.TimeoutExpired) as e:
        return -1, "", str(e)


def parse_version(s):
    nums = re.findall(r"\d+", s or "")
    return tuple(int(n) for n in nums[:4]) if nums else None


def pick_device(adb_exe, serials):
    """Choose the device to test when --serial was not given. Returns (serial or None, reason).

    Keeps only devices with an ARMSX2 package. One handset can show up twice (USB plus
    wireless debugging); those share ro.serialno, and the USB entry is preferred because
    it is the faster link and the one the forward is meant to ride.
    """
    if len(serials) == 1:
        return serials[0], "only device attached"
    if not serials:
        return None, "no authorized devices (unlock the Odin and accept the USB debugging prompt)"
    by_hw = {}
    for serial in serials:
        pkgs = run([adb_exe, "-s", serial, "shell", "pm", "list", "packages"])[1].lower()
        if "armsx2" not in pkgs:
            continue
        hw = run([adb_exe, "-s", serial, "shell", "getprop", "ro.serialno"])[1].strip() or serial
        is_network = ":" in serial or "._adb" in serial
        if hw not in by_hw or (by_hw[hw][1] and not is_network):
            by_hw[hw] = (serial, is_network)
    if len(by_hw) == 1:
        serial, is_network = next(iter(by_hw.values()))
        return serial, "the only device with ARMSX2 installed" + (" (wireless)" if is_network else "")
    if not by_hw:
        return None, f"{len(serials)} devices attached and none has ARMSX2 installed"
    return None, f"{len(by_hw)} different devices have ARMSX2 installed"


def decode_pine_string_reply(hexdump):
    """Decode an `od -An -tx1` dump of a PINE string reply. Returns (ok, text)."""
    b = bytes(int(h, 16) for h in hexdump.split())
    if len(b) < 9 or b[4] != 0:
        return False, b.hex()
    n = int.from_bytes(b[5:9], "little")
    return True, b[9:9 + n].split(b"\0", 1)[0].decode("utf-8", "replace")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--adb", default=shutil.which("adb") or "adb")
    ap.add_argument("--serial", help="adb device serial when several are attached")
    ap.add_argument("--port", type=int, default=28011)
    ap.add_argument("--bench", type=int, default=2000)
    ap.add_argument("--no-interactive", action="store_true")
    a = ap.parse_args(argv)

    adb = [a.adb] + (["-s", a.serial] if a.serial else [])
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    results = {"timestamp": stamp, "port": a.port, "steps": [], "device": {}, "pine": {}, "manual": {}}

    # 1. adb present and exactly one device
    s = Step(results, "adb devices")
    rc, out, err = run([a.adb, "devices", "-l"])
    if rc != 0:
        s("FAIL", f"adb not runnable ({err.strip() or rc}). Install platform-tools or pass --adb.")
        return finish(results)
    devices = [l for l in out.splitlines()[1:] if l.strip() and "device" in l.split()[1:2]]
    if not a.serial:
        a.serial, why = pick_device(a.adb, [l.split()[0] for l in devices])
        if not a.serial:
            listing = "\n".join("    " + l for l in devices) or "    (none)"
            s("FAIL", f"{why}. Attached devices:\n{listing}\n  Re-run with --serial <first column>", raw=out)
            return finish(results)
        adb = [a.adb, "-s", a.serial]
        s("PASS", f"using {a.serial} ({why})", raw=out)
    else:
        s("PASS", f"using {a.serial} (--serial)", raw=out)

    # 2. device facts
    s = Step(results, "device info")
    props = {}
    for key in ("ro.product.manufacturer", "ro.product.model", "ro.build.version.release",
                "ro.build.version.sdk", "ro.build.fingerprint"):
        props[key] = run(adb + ["shell", "getprop", key])[1].strip()
    results["device"] = props
    s("INFO", f"{props['ro.product.manufacturer']} {props['ro.product.model']}, "
              f"Android {props['ro.build.version.release']} (SDK {props['ro.build.version.sdk']})")

    # 3. ARMSX2 installed + version (stable is com.armsx2; nightly uses another id)
    s = Step(results, "ARMSX2 version")
    pkgs = [l.split(":", 1)[1].strip() for l in run(adb + ["shell", "pm", "list", "packages"])[1].splitlines()
            if "armsx2" in l.lower()]
    if not pkgs:
        s("FAIL", "no package containing 'armsx2' installed")
    for pkg in pkgs:
        dump = run(adb + ["shell", "dumpsys", "package", pkg], timeout=30)[1]
        vn = (re.search(r"versionName=(\S+)", dump) or [None, None])[1]
        vc = (re.search(r"versionCode=(\d+)", dump) or [None, None])[1]
        results["device"].setdefault("armsx2", []).append({"package": pkg, "versionName": vn, "versionCode": vc})
        v = parse_version(vn)
        if v is None:
            s("WARN", f"{pkg}: unparseable versionName {vn!r}; confirm the PINE section exists in Settings > Advanced")
        elif v >= MIN_VERSION:
            s("PASS", f"{pkg} {vn} (>= 2.6.6.5, has Android PINE)")
        else:
            s("WARN", f"{pkg} {vn} looks older than 2.6.6.5; nightly versions may be numbered differently. "
                      "If Settings > Advanced has no PINE section, update ARMSX2.")

    # 4. on-device cross-uid loopback probe: adb shell runs as uid 'shell', not ARMSX2's uid,
    #    so an answer here shows the loopback port is reachable from outside the ARMSX2 sandbox.
    s = Step(results, "on-device loopback (shell uid)")
    probe = (f"printf '\\005\\000\\000\\000\\010' | nc -w 2 127.0.0.1 {a.port} | od -An -tx1")
    rc, out, err = run(adb + ["shell", probe], timeout=20)
    ok, text = decode_pine_string_reply(out) if out.strip() else (False, "")
    if ok:
        s("PASS", f"shell uid got a Version reply over 127.0.0.1:{a.port}: {text!r}", raw=out)
    else:
        s("WARN", "no PINE reply from on-device nc (PINE off, no game, or toybox nc lacks -w). "
                  "Not fatal; the forwarded test below is the one that matters.", raw=out, stderr=err)

    # 5. forward
    s = Step(results, "adb forward")
    rc, out, err = run(adb + ["forward", f"tcp:{a.port}", f"tcp:{a.port}"])
    if rc != 0:
        s("FAIL", f"adb forward failed: {err.strip()}")
        return finish(results)
    s("PASS", f"tcp:{a.port} -> device tcp:{a.port}")

    # 6. ARMSX2's own PINE log lines
    s = Step(results, "logcat PINE lines")
    rc, out, _ = run(adb + ["logcat", "-d"], timeout=30)
    lines = [l for l in out.splitlines() if "PINE:" in l][-15:]
    s("INFO" if lines else "WARN", f"{len(lines)} recent line(s)" if lines else
      "no 'PINE:' lines in logcat buffer (may have rotated out; not fatal)", raw="\n".join(lines))

    # 7. identify over the forward
    s = Step(results, "PINE identify")
    try:
        pine = pine_diag.Pine("127.0.0.1", a.port, 5.0)
    except OSError as e:
        s("FAIL", f"cannot connect through the forward: {e}. Is PINE enabled? Is another client connected?")
        return finish(results)
    try:
        info = pine_diag.identify(pine)
        results["pine"]["identify"] = info
        s("PASS" if info["version"].startswith("ARMSX2") else "WARN",
          f"version={info['version']!r} status={info['status']}")

        s = Step(results, "game identity")
        gid, crc = info.get("id"), (info.get("crc") or "").lower()
        mod_supported = False
        if gid in KNOWN:
            label = KNOWN[gid]["crc"].get(crc, "UNKNOWN CRC - record it")
            mod_supported = KNOWN[gid].get("mod_supported", True)
            outcome = "PASS" if "retail" in label else "WARN"
            if not mod_supported:
                outcome = "WARN"
                label += "; HeySnippy's mod does not support this serial (JP only), manual mod checks skipped"
            s(outcome, f"{gid} {KNOWN[gid]['name']}, CRC {crc} -> {label}; title={info.get('title')!r}, "
                       f"version={info.get('game_version')!r}")
        else:
            s("FAIL", f"booted game id {gid!r} is not a supported serial (boot File #1 or #2 first)")

        # 8. latency through ADB
        s = Step(results, "Read32 latency over ADB")
        r = pine_diag.bench(pine, a.bench)
        results["pine"]["bench_adb"] = r
        s("INFO", f"p50 {r['p50_ms']:.3f} ms, p95 {r['p95_ms']:.3f} ms, p99 {r['p99_ms']:.3f} ms, "
                  f"max {r['max_ms']:.3f} ms, ~{r['round_trips_per_s']:.0f} round trips/s")
    except (pine_diag.PineError, OSError) as e:
        s("FAIL", f"PINE error: {e}")
        return finish(results)
    finally:
        pine.close()  # release the single PINE client slot for the mod

    # 9. manual mod checks
    if not a.no_interactive and mod_supported:
        print("\n--- Manual checks with Outbreak-ModernCam.exe (this script has released PINE) ---")
        print("Leave this window open. Answer y / n / s(kip); a note after the letter is saved, e.g. 'n light stays red'.")
        for key, q in MANUAL_CHECKS:
            ans = input(f"{q} [y/n/s] ").strip()
            verdict = {"y": "PASS", "n": "FAIL"}.get(ans[:1].lower(), "SKIP")
            results["manual"][key] = {"question": q, "outcome": verdict, "note": ans[1:].strip()}
    return finish(results)


def finish(results):
    out_dir = os.path.join(REPO_ROOT, "results")
    os.makedirs(out_dir, exist_ok=True)
    base = os.path.join(out_dir, f"phase2-{results['timestamp']}")
    with open(base + ".json", "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    with open(base + ".md", "w", encoding="utf-8") as f:
        f.write(f"# Phase 2 run {results['timestamp']}\n\n| Step | Outcome | Detail |\n|---|---|---|\n")
        for st in results["steps"]:
            f.write(f"| {st['step']} | {st['outcome']} | {st['detail'].replace('|', '/')} |\n")
        if results["manual"]:
            f.write("\n## Manual checks\n\n| Check | Outcome | Note |\n|---|---|---|\n")
            for k, m in results["manual"].items():
                f.write(f"| {k} | {m['outcome']} | {m['note'].replace('|', '/')} |\n")
    print(f"\nSaved {base}.json and {base}.md  -- commit them or paste the .md back to Claude.")
    return 1 if any(s["outcome"] == "FAIL" for s in results["steps"]) else 0


if __name__ == "__main__":
    sys.exit(main())
