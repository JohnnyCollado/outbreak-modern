"""T3 scratch: our-side noise (run A vs run B by tick) and per-screen change period from a long probe.
usage: t3_noise.py noise TAG_A TAG_B | probe TAG"""
import hashlib, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools", "gate"))
import gate  # noqa: E402

SCREENS = ["savedata", "access", "capcom", "title", "mainmenu", "charselect"]
DS = [0, 2, 4, 8, 16, 32]


def frames(tag):
    ticks = gate.read_ticks(f"C:/ob/logs/boot-{tag}.log")
    out = {}
    for (s, k), t in ticks.items():
        out.setdefault(s, []).append((k, t, f"C:/ob/frames-{tag}/{s}-{k}.png"))
    for s in out:
        out[s].sort()
    return out


def h(p):
    with open(p, "rb") as f:
        return hashlib.sha256(gate.load_rgb(p).tobytes()).hexdigest()[:10]


def noise(a, b):
    fa, fb = frames(a), frames(b)
    for s in SCREENS:
        tb = {t: p for _, t, p in fb.get(s, [])}
        for k, t, p in fa.get(s, []):
            if t not in tb:
                print(f"{s} k={k} tick={t}: no B frame at this tick (B ticks {sorted(tb)})")
                continue
            ia, ib = gate.load_rgb(p), gate.load_rgb(tb[t])
            res = [gate.metric(ia, ib, d) for d in DS]
            print(f"{s} k={k} tick={t} max={res[0][0]} " + " ".join(f"share>{d}={r[1]:.6f}" for d, r in zip(DS, res)))


def probe(tag):
    fa = frames(tag)
    for s in SCREENS:
        fr = fa.get(s, [])
        hs = [h(p) for _, _, p in fr]
        ticks = [t for _, t, _ in fr]
        # runs of identical content
        runs, start = [], 0
        for i in range(1, len(hs) + 1):
            if i == len(hs) or hs[i] != hs[start]:
                runs.append((ticks[start], ticks[i - 1], hs[start], i - start))
                start = i
        distinct = len(set(hs))
        # smallest period in captures with hs[i]==hs[i+P] for all i
        per = next((P for P in range(1, len(hs)) if all(hs[i] == hs[i + P] for i in range(len(hs) - P))), None)
        img0 = gate.load_rgb(fr[0][2])
        mx = max(gate.metric(img0, gate.load_rgb(p), 0)[0] for _, _, p in fr)
        print(f"{s}: n={len(fr)} ticks {ticks[0]}..{ticks[-1]} distinct={distinct} runs={len(runs)} "
              f"period_captures={per} max_vs_k0={mx}")
        for r in runs[:12]:
            print(f"   ticks {r[0]}-{r[1]} hash={r[2]} frames={r[3]}")
        if len(runs) > 12:
            print(f"   ... {len(runs) - 12} more runs")


if __name__ == "__main__" and sys.argv[1] != "detail":
    {"noise": lambda: noise(sys.argv[2], sys.argv[3]), "probe": lambda: probe(sys.argv[2])}[sys.argv[1]]()


def detail(tag, screen):
    from PIL import ImageChops
    fr = frames(tag)[screen]
    img0 = gate.load_rgb(fr[0][2])
    prev = img0
    for k, t, p in fr:
        im = gate.load_rgb(p)
        m0, s0 = gate.metric(img0, im, 8)
        mp, sp = gate.metric(prev, im, 8)
        bb = ImageChops.difference(prev, im).getbbox()
        print(f"{screen} k={k} t={t} vs_k0 max={m0} share8={s0:.5f} | vs_prev max={mp} share8={sp:.5f} bbox={bb}")
        prev = im


if sys.argv[1] == "detail":
    detail(sys.argv[2], sys.argv[3])
