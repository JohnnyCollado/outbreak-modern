"""Boot-screen gate (phase 1.2): runner captures vs PCSX2 references. Pillow only.

Usage (any cwd; defaults resolve against the repo root):
  python tools/gate/gate.py compare --frames D --log F [--refs D] [--manifest P] [--d N] [--p F] [--start EPOCH]
      Per screen: every frame <frames>/<screen>-<k>.png vs <refs>/<screen>-a.png, RGB only;
      delta = max(|dR|,|dG|,|dB|), share = count(delta > d) / (w*h); best frame = lowest share,
      then lowest max, then lowest k; tick from the boot log. GREEN = share <= p.
  python tools/gate/gate.py selftest [--refs D] [--manifest P] [--d N] [--p F] [--start EPOCH]
      Per screen: ref b vs ref a must be GREEN; two damaged copies of ref a ("missing layer",
      "shifted glyph") must be RED. Damaged copies live in .run/1.2/selftest-<pid>/, deleted after.
  python tools/gate/gate.py stamp
      sha256 over the recompile inputs (see stamp_entries).
  python tools/gate/gate.py synth DIR
      Synthetic refs a/b + manifest and frame sets identical/erased/shifted with boot logs.
Exit: 0 green, 1 red, 2 refusal (no references, no tolerance, sha/size/count/tick mismatch), 5 selftest red.
d/p: --d/--p if given, else gate/tolerance.json {d, p, ...}.
"""
import argparse
import datetime
import fnmatch
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

from PIL import Image, ImageChops, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCREENS = ["savedata", "access", "capcom", "title", "mainmenu", "charselect"]
DEFAULT_REFS = "C:/ob/ref"
DEFAULT_MANIFEST = os.path.join(REPO, "gate", "ref_manifest.json")
TOLERANCE = os.path.join(REPO, "gate", "tolerance.json")
TEMP_ROOT = os.path.join(REPO, ".run", "1.2")
TICK_RE = re.compile(r"\[script\] capture (\S+) k=(\d+) tick=(\d+)")
FRAME_RE = re.compile(r"^(.+)-(\d+)\.png$")


class Refusal(Exception):
    pass


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_rgb(path):
    with Image.open(path) as im:
        return im.convert("RGB")


def metric(ref, img, d):
    """(max delta, share of pixels with delta > d); RGB only."""
    diff = ImageChops.difference(ref, img).split()
    m = ImageChops.lighter(diff[0], ImageChops.lighter(diff[1], diff[2]))
    hist = m.histogram()
    mx = max((v for v in range(256) if hist[v]), default=0)
    return mx, sum(hist[d + 1:]) / (ref.width * ref.height)


def wall(start):
    return f"wall {time.time() - start:.1f} s"


def finish(code, start, head):
    print(f"gate: {head}, exit {code}, {wall(start)}")
    return code


def check_refs(a, takes):
    """No-references check (first); returns the manifest. Raises Refusal('no references (...)')."""
    if not os.path.isfile(a.manifest):
        raise Refusal(f"no references ({a.manifest} missing)")
    for s in SCREENS:
        for t in takes:
            p = os.path.join(a.refs, f"{s}-{t}.png")
            if not os.path.isfile(p):
                raise Refusal(f"no references ({p} missing)")
    with open(a.manifest, encoding="utf-8") as f:
        return json.load(f)


def tolerance(a):
    d, p = a.d, a.p
    if d is None or p is None:
        if not os.path.isfile(TOLERANCE):
            raise Refusal(f"no tolerance (--d/--p not given and {TOLERANCE} missing)")
        with open(TOLERANCE, encoding="utf-8") as f:
            tol = json.load(f)
        d = tol["d"] if d is None else d
        p = tol["p"] if p is None else p
    return int(d), float(p)


def verify_ref(man, refs, screen, take):
    """Ref image with sha256 and size checked against the manifest; problems as strings."""
    path = os.path.join(refs, f"{screen}-{take}.png")
    ent = [r for r in man.get("refs", []) if r.get("screen") == screen and r.get("take") == take]
    if not ent:
        return None, [f"{screen}-{take}: not in manifest"]
    e, bad = ent[0], []
    if sha256_file(path) != e.get("sha256"):
        bad.append(f"{screen}-{take}: sha256 != manifest")
    im = load_rgb(path)
    if im.size != (e.get("w"), e.get("h")):
        bad.append(f"{screen}-{take}: size {im.size[0]}x{im.size[1]} != manifest {e.get('w')}x{e.get('h')}")
    return im, bad


def read_ticks(log):
    ticks = {}
    if os.path.isfile(log):
        with open(log, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = TICK_RE.search(line)
                if m:
                    ticks[(m.group(1), int(m.group(2)))] = int(m.group(3))
    return ticks


def cmd_compare(a):
    try:
        man = check_refs(a, ["a"])
        d, p = tolerance(a)
    except Refusal as e:
        return finish(2, a.start, str(e))
    window = man.get("window")
    ticks = read_ticks(a.log)
    frames = {}
    if os.path.isdir(a.frames):
        for name in sorted(os.listdir(a.frames)):
            m = FRAME_RE.match(name)
            if m and m.group(1) in SCREENS:
                frames.setdefault(m.group(1), []).append(int(m.group(2)))
    bad, refs = [], {}
    for s in SCREENS:
        refs[s], probs = verify_ref(man, a.refs, s, "a")
        bad += probs
        want = window.get(s) if isinstance(window, dict) else window
        got = sorted(frames.get(s, []))
        if not isinstance(want, int) or len(got) != want:
            bad.append(f"{s}: {len(got)} capture(s) != manifest window {want}")
        for k in got:
            if (s, k) not in ticks:
                bad.append(f"{s}-{k}: no tick in {a.log}")
    if not bad:
        results = []
        for s in SCREENS:
            best = None
            for k in sorted(frames[s]):
                img = load_rgb(os.path.join(a.frames, f"{s}-{k}.png"))
                if img.size != refs[s].size:
                    bad.append(f"{s}-{k}: size {img.size[0]}x{img.size[1]} != ref {refs[s].size[0]}x{refs[s].size[1]}")
                    continue
                mx, share = metric(refs[s], img, d)
                key = (share, mx, k)
                if best is None or key < best:
                    best = key
            results.append((s, best))
    if bad:
        for b in bad:
            print(f"refused: {b}")
        return finish(2, a.start, f"refused ({len(bad)} problem(s))")
    green = 0
    for s, (share, mx, k) in results:
        ok = share <= p
        green += ok
        print(f"{s} best k={k} tick={ticks[(s, k)]} max={mx} share={share:.6f} p={p:.6f} {'GREEN' if ok else 'RED'}")
    code = 0 if green == len(SCREENS) else 1
    return finish(code, a.start, f"{green}/{len(SCREENS)} green")


# ---- selftest damage -------------------------------------------------------------------------

def missing_layer(im):
    """Fill the bbox of the top-scoring colour component with its 1-px ring's median colour.
    Components: 4-connected over colours quantized to 3 bits/channel; those > 25% of the image
    are excluded. Score = area x max-channel distance (component mean vs ring median).
    Ties: lowest y, then x. Returns (damaged copy, description)."""
    w, h = im.size
    src = list(im.get_flattened_data())
    q = [((r >> 5) << 6) | ((g >> 5) << 3) | (b >> 5) for r, g, b in src]
    label = [-1] * (w * h)
    comps = []  # (area, minx, miny, maxx, maxy, sr, sg, sb)
    for start in range(w * h):
        if label[start] != -1:
            continue
        c, n = q[start], len(comps)
        label[start] = n
        stack = [start]
        area, minx, miny, maxx, maxy, sr, sg, sb = 0, w, h, -1, -1, 0, 0, 0
        while stack:
            i = stack.pop()
            y, x = divmod(i, w)
            area += 1
            r, g, b = src[i]
            sr += r; sg += g; sb += b
            minx = min(minx, x); maxx = max(maxx, x); miny = min(miny, y); maxy = max(maxy, y)
            for j, ok in ((i - 1, x > 0), (i + 1, x < w - 1), (i - w, y > 0), (i + w, y < h - 1)):
                if ok and label[j] == -1 and q[j] == c:
                    label[j] = n
                    stack.append(j)
        comps.append((area, minx, miny, maxx, maxy, sr, sg, sb))
    best = None  # (score, -miny, -minx, box, colour)
    for area, minx, miny, maxx, maxy, sr, sg, sb in sorted(comps, key=lambda t: -t[0]):
        if area > 0.25 * w * h:
            continue
        if best is not None and area * 255 < best[0]:
            break
        x0, y0, x1, y1 = max(minx - 1, 0), max(miny - 1, 0), min(maxx + 1, w - 1), min(maxy + 1, h - 1)
        ring = [src[y * w + x] for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
                if (x == minx - 1 or x == maxx + 1 or y == miny - 1 or y == maxy + 1)]
        if not ring:
            continue
        med = tuple(sorted(px[ch] for px in ring)[(len(ring) - 1) // 2] for ch in range(3))
        mean = (sr / area, sg / area, sb / area)
        dist = max(abs(mean[ch] - med[ch]) for ch in range(3))
        key = (area * dist, -miny, -minx, (minx, miny, maxx + 1, maxy + 1), med)
        if best is None or key[:3] > best[:3]:
            best = key
    out = im.copy()
    if best is None:
        return out, "no candidate"
    out.paste(best[4], best[3])
    return out, f"bbox={best[3]} fill={best[4]}"


def shifted_glyph(im):
    """Shift the 24x24 window with the highest FIND_EDGES luminance energy (stride-4 grid)
    by +2 px in x. Ties: lowest y, then x. Returns (damaged copy, description)."""
    w, h = im.size
    e = list(im.convert("L").filter(ImageFilter.FIND_EDGES).get_flattened_data())
    integ = [0] * ((w + 1) * (h + 1))
    for y in range(h):
        row, base, prev = 0, (y + 1) * (w + 1), y * (w + 1)
        for x in range(w):
            row += e[y * w + x]
            integ[base + x + 1] = integ[prev + x + 1] + row
    best = None
    for y in range(0, h - 24 + 1, 4):
        for x in range(0, w - 26 + 1, 4):
            s = (integ[(y + 24) * (w + 1) + x + 24] - integ[y * (w + 1) + x + 24]
                 - integ[(y + 24) * (w + 1) + x] + integ[y * (w + 1) + x])
            if best is None or s > best[0]:
                best = (s, x, y)
    _, x, y = best
    out = im.copy()
    out.paste(im.crop((x, y, x + 24, y + 24)), (x + 2, y))
    return out, f"window=({x},{y}) energy={best[0]}"


def cmd_selftest(a):
    try:
        check_refs(a, ["a", "b"])
        d, p = tolerance(a)
    except Refusal as e:
        return finish(2, a.start, str(e))
    tmp = os.path.join(TEMP_ROOT, f"selftest-{os.getpid()}")
    os.makedirs(tmp, exist_ok=True)
    ok_n, total = 0, 0
    try:
        for s in SCREENS:
            ra = load_rgb(os.path.join(a.refs, f"{s}-a.png"))
            rb = load_rgb(os.path.join(a.refs, f"{s}-b.png"))
            cases = [("b-vs-a", rb, "", True)]
            for name, fn in (("missing layer", missing_layer), ("shifted glyph", shifted_glyph)):
                dmg, desc = fn(ra)
                path = os.path.join(tmp, f"{s}-{name.replace(' ', '_')}.png")
                dmg.save(path)
                cases.append((name, load_rgb(path), desc, False))
            for name, img, desc, want_green in cases:
                if img.size != ra.size:
                    green, mx, share = False, -1, 1.0
                else:
                    mx, share = metric(ra, img, d)
                    green = share <= p
                ok = green == want_green
                ok_n += ok
                total += 1
                print(f"{s} {name}: max={mx} share={share:.6f} p={p:.6f} {'GREEN' if green else 'RED'} "
                      f"{'ok' if ok else 'FAIL'}{(' ' + desc) if desc else ''}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return finish(0 if ok_n == total else 5, a.start, f"selftest {ok_n}/{total} ok")


# ---- stamp -----------------------------------------------------------------------------------

def stamp_entries():
    """(label, content sha256) of every recompile input; labels are root-relative paths."""
    root = os.environ.get("OB_ROOT", "C:/ob")
    env = lambda k, dflt: os.environ.get(k, dflt)
    src, t = env("OB_SRC", root + "/src"), env("OB_T", root + "/t")
    work, ovl = env("OB_WORK", root + "/work"), env("OB_OVL", root + "/ovl")
    splat, run = env("OB_SPLAT", root + "/splat/SLPM_654.28"), env("OB_RUN", REPO + "/game_files")
    up = root + "/up"
    out = []

    def add_file(label, path):
        out.append((label, sha256_file(path) if os.path.isfile(path) else "MISSING"))

    def add_tree(label, top, skip_dir=lambda rel: False, skip_file=lambda rel: False):
        if not os.path.isdir(top):
            out.append((label + "/", "MISSING"))
            return
        for dp, dns, fns in os.walk(top):
            rel_dir = os.path.relpath(dp, top).replace("\\", "/")
            rel_dir = "" if rel_dir == "." else rel_dir + "/"
            dns[:] = [n for n in dns if not skip_dir(rel_dir + n + "/")]
            for fn in fns:
                if not skip_file(rel_dir + fn):
                    add_file(f"{label}/{rel_dir}{fn}", os.path.join(dp, fn))

    def git(*args):
        r = subprocess.run(["git", "-C", up, *args], capture_output=True)
        return r.stdout if r.returncode == 0 else b"GIT-ERROR " + r.stderr

    out.append(("up:HEAD", git("rev-parse", "HEAD").strip().decode("ascii", "replace")))
    out.append(("up:diff", hashlib.sha256(git("diff", "HEAD")).hexdigest()))
    for rel in sorted(x for x in git("ls-files", "--others", "--exclude-standard", "-z").decode("utf-8", "replace").split("\0") if x):
        add_file(f"up:untracked/{rel}", os.path.join(up, rel))
    build_dirs = {".git", ".vs", "build", "out"}
    add_tree("src", src,
             skip_dir=lambda rel: rel == "ps2xRuntime/src/runner/"
             or rel.rstrip("/").split("/")[-1] in build_dirs
             or rel.rstrip("/").split("/")[-1].startswith(("build-", "cmake-build")),
             skip_file=lambda rel: fnmatch.fnmatch(rel.split("/")[-1], "*_recompiled_*.h"))
    add_file("t/ps2xAnalyzer/ps2_analyzer.exe", t + "/ps2xAnalyzer/ps2_analyzer.exe")
    add_file("t/ps2xRecomp/ps2_recomp.exe", t + "/ps2xRecomp/ps2_recomp.exe")
    add_file("work/SLPM_654.28", work + "/SLPM_654.28")
    add_file("work/main-functions.csv", work + "/main-functions.csv")
    add_file("ovl/demo.toml", ovl + "/demo.toml")
    for b in ("DEMO", "SUBMAIN", "NETDNAS"):
        add_file(f"run/PROG/{b}.BIN", f"{run}/PROG/{b}.BIN")
    for o in ("demo", "submain", "netdnas"):  # as tools/ob.sh cmd_recompile resolves them
        rel = f"en/{o}" if os.path.isdir(f"{splat}/en/{o}") else o
        add_tree(f"splat/{rel}", f"{splat}/{rel}")
    for rel in ("tools/mwo3_to_elf.py", "tools/splat_functions.py"):
        add_file(f"repo/{rel}", os.path.join(REPO, rel))
    lines, inside = [], False
    with open(os.path.join(REPO, "tools", "ob.sh"), "rb") as f:
        for line in f:
            if line.startswith(b"cmd_recompile() {"):
                inside = True
            if inside:
                lines.append(line.rstrip(b"\r\n"))
                if line.rstrip(b"\r\n") == b"}":
                    break
    out.append(("repo/tools/ob.sh:cmd_recompile", hashlib.sha256(b"\n".join(lines)).hexdigest()))
    return sorted(out)


def cmd_stamp(a):
    h = hashlib.sha256()
    for label, sha in stamp_entries():
        h.update(f"{label}\t{sha}\n".encode("utf-8"))
    print(h.hexdigest())
    return 0


# ---- synth -----------------------------------------------------------------------------------

def synth_ref(i):
    """Synthetic screen i: flat background, two solid layers, a 2-px vertical stripe block."""
    bg = (20 + 5 * i, 24, 40)
    im = Image.new("RGB", (512, 448), bg)
    im.paste((200, 80 + 20 * i, 60), (60 + 10 * i, 50, 180 + 10 * i, 130))
    im.paste((60, 160, 220 - 10 * i), (340, 90 + 8 * i, 400, 130 + 8 * i))
    for x in range(300, 348, 4):
        im.paste((235, 235, 235), (x, 250, x + 2, 282))
    return im


def cmd_synth(a):
    out = a.dir
    refs = os.path.join(out, "ref")
    os.makedirs(refs, exist_ok=True)
    entries = []
    for i, s in enumerate(SCREENS):
        ra = synth_ref(i)
        rb = ra.point(lambda v: min(255, v + 3))
        for take, im in (("a", ra), ("b", rb)):
            path = os.path.join(refs, f"{s}-{take}.png")
            im.save(path)
            entries.append({"screen": s, "take": take, "file": os.path.basename(path),
                            "sha256": sha256_file(path), "w": im.width, "h": im.height, "source": "synth"})
        for kind in ("identical", "erased", "shifted"):
            fd = os.path.join(out, f"frames-{kind}")
            os.makedirs(fd, exist_ok=True)
            img = ra.copy()
            if kind == "erased":
                img.paste((0, 0, 0), (70 + 10 * i, 60, 110 + 10 * i, 90))
            elif kind == "shifted":
                img.paste(ra.crop((300, 252, 324, 276)), (302, 252))
            img = img.convert("RGBA")
            img.putalpha(128)  # alpha must be ignored
            for k in range(3):
                img.save(os.path.join(fd, f"{s}-{k}.png"))
    for kind in ("identical", "erased", "shifted"):
        with open(os.path.join(out, f"boot-{kind}.log"), "w", encoding="utf-8") as f:
            f.write("[run] window hidden=1\n")
            for i, s in enumerate(SCREENS):
                f.write(f"[script] log {s}\n")
                for k in range(3):
                    f.write(f"[script] capture {s} k={k} tick={100 * (i + 1) + 2 * k}\n")
    manifest = {"pcsx2": "synthetic", "settings": {}, "window": {s: 3 for s in SCREENS},
                "session": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
                "refs": entries}
    with open(os.path.join(out, "ref_manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"synth: refs + ref_manifest.json + frames-identical/erased/shifted + boot-*.log in {out}")
    return 0


def main():
    ap = argparse.ArgumentParser(description="Boot-screen gate (phase 1.2)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("compare", "selftest"):
        sp = sub.add_parser(name)
        if name == "compare":
            sp.add_argument("--frames", required=True)
            sp.add_argument("--log", required=True)
        sp.add_argument("--refs", default=DEFAULT_REFS)
        sp.add_argument("--manifest", default=DEFAULT_MANIFEST)
        sp.add_argument("--d", type=int)
        sp.add_argument("--p", type=float)
        sp.add_argument("--start", type=float, default=time.time(), help="epoch seconds the wall time counts from")
    sub.add_parser("stamp")
    sp = sub.add_parser("synth")
    sp.add_argument("dir")
    a = ap.parse_args()
    return {"compare": cmd_compare, "selftest": cmd_selftest, "stamp": cmd_stamp, "synth": cmd_synth}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
