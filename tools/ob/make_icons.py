"""Make the app icons from one square image (docs/assets/gaame icon.png, the developer's, 2026-10-26).

usage: make_icons.py [SOURCE.png] [--square SQUARE.png] [--round ROUND.png] [--preview OUT.png]

With --square / --round (the developer's pre-shaped versions, transparent outside the shape): the classic icon and
Windows use the rounded square, ic_launcher_round and the adaptive foreground the circle (scaled to the 72 dp circle
every mask keeps, so a round mask clips nothing).

Writes:
  native/android/res/mipmap-<density>/ic_launcher.png             classic icon (the whole image), 48..192 px
  native/android/res/mipmap-<density>/ic_launcher_foreground.png  adaptive foreground: the image scaled into the
                                                                   circle every launcher mask keeps (60 of 108 dp)
  native/android/res/mipmap-anydpi-v26/ic_launcher.xml             the adaptive icon (background colour + foreground)
  native/android/res/values/ic_launcher_background.xml             that colour, sampled from the image's edge
  native/windows/outbreak.ico (16..256 px) and native/windows/outbreak-icon.png (256 px, the runtime window icon)
--preview: one PNG with the adaptive icon under a circle and a rounded-square mask, and the classic icon.
"""
import os
import sys

from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DENSITIES = {"mdpi": 1.0, "hdpi": 1.5, "xhdpi": 2.0, "xxhdpi": 3.0, "xxxhdpi": 4.0}


def edge_colour(img):
    """Average of the image's border pixels: the background the adaptive icon's padding blends into."""
    w, h = img.size
    px = img.load()
    pts = [(x, 0) for x in range(w)] + [(0, y) for y in range(h // 3)] + [(w - 1, y) for y in range(h // 3)] # the misty top
    r = g = b = 0
    for p in pts:
        c = px[p]
        r, g, b = r + c[0], g + c[1], b + c[2]
    n = len(pts)
    return (r // n, g // n, b // n)


def adaptive_layer(img, size, bg):
    """108 dp layer: the image at 60 dp, so the title's corners (13-91 % across, 7 % down the image) stay inside the
    72 dp circle every mask keeps and only the art's own corners can be clipped (developer, 2026-10-26: 51 dp, the whole
    square inside the circle, was too small); the rest the background colour."""
    layer = Image.new("RGBA", (size, size), bg + (255,))
    inner = round(size * 60 / 108)
    art = img.resize((inner, inner), Image.LANCZOS)
    off = (size - inner) // 2
    layer.paste(art, (off, off))
    return layer


def main(argv):
    preview = None
    square = None
    roundsrc = None
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == "--preview":
            preview = argv[i + 1]
            i += 2
        elif argv[i] == "--square":
            square = argv[i + 1]
            i += 2
        elif argv[i] == "--round":
            roundsrc = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1
    src = args[0] if args else os.path.join(REPO, "docs", "assets", "gaame icon.png")
    img = Image.open(src).convert("RGB")
    if img.size[0] != img.size[1]:
        sys.exit(f"{src}: not square ({img.size[0]}x{img.size[1]})")
    bg = edge_colour(img)
    res = os.path.join(REPO, "native", "android", "res")
    sq = Image.open(square).convert("RGBA") if square else None
    rd = Image.open(roundsrc).convert("RGBA") if roundsrc else None
    for name, scale in DENSITIES.items():
        d = os.path.join(res, f"mipmap-{name}")
        os.makedirs(d, exist_ok=True)
        (sq or img).resize((round(48 * scale),) * 2, Image.LANCZOS).save(os.path.join(d, "ic_launcher.png"), optimize=True)
        if rd:
            rd.resize((round(48 * scale),) * 2, Image.LANCZOS).save(os.path.join(d, "ic_launcher_round.png"), optimize=True)
            # The circle fills the 72 dp circle every launcher mask keeps (centre of the 108 dp layer).
            size = round(108 * scale)
            layer = Image.new("RGBA", (size, size), bg + (255,))
            inner = round(size * 72 / 108)
            art = rd.resize((inner, inner), Image.LANCZOS)
            layer.alpha_composite(art, ((size - inner) // 2, (size - inner) // 2))
            layer.save(os.path.join(d, "ic_launcher_foreground.png"), optimize=True)
        else:
            adaptive_layer(img, round(108 * scale), bg).save(os.path.join(d, "ic_launcher_foreground.png"), optimize=True)
    os.makedirs(os.path.join(res, "mipmap-anydpi-v26"), exist_ok=True)
    with open(os.path.join(res, "mipmap-anydpi-v26", "ic_launcher.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n'
                '<!-- Made by tools/ob/make_icons.py from docs/assets/gaame icon.png. -->\n'
                '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
                '    <background android:drawable="@color/ic_launcher_background" />\n'
                '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
                '</adaptive-icon>\n')
    os.makedirs(os.path.join(res, "values"), exist_ok=True)
    with open(os.path.join(res, "values", "ic_launcher_background.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
                f'    <color name="ic_launcher_background">#{bg[0]:02X}{bg[1]:02X}{bg[2]:02X}</color>\n</resources>\n')
    win = os.path.join(REPO, "native", "windows")
    os.makedirs(win, exist_ok=True)
    (sq or img).save(os.path.join(win, "outbreak.ico"), sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    (sq or img).resize((256, 256), Image.LANCZOS).save(os.path.join(win, "outbreak-icon.png"), optimize=True)
    if preview:
        s = 432
        if rd:
            layer = Image.new("RGBA", (s, s), bg + (255,))
            inner = round(s * 72 / 108)
            layer.alpha_composite(rd.resize((inner, inner), Image.LANCZOS), ((s - inner) // 2, (s - inner) // 2))
        else:
            layer = adaptive_layer(img, s, bg)
        out = Image.new("RGBA", (s * 3 + 80, s + 40), (40, 40, 44, 255))
        for k, shape in enumerate(("circle", "squircle")):
            m = Image.new("L", (s, s), 0)
            dr = ImageDraw.Draw(m)
            # Launchers draw the 72 dp middle plus a little: the visible disc / rounded square of 72/108 of the layer.
            pad = round(s * 18 / 108)
            box = (pad, pad, s - pad, s - pad)
            if shape == "circle":
                dr.ellipse(box, fill=255)
            else:
                dr.rounded_rectangle(box, radius=round(s * 0.2), fill=255)
            out.paste(layer, (20 + k * (s + 20), 20), m)
        cls = (sq or img).resize((s - 2 * round(s * 18 / 108),) * 2, Image.LANCZOS).convert("RGBA")
        out.alpha_composite(cls, (20 + 2 * (s + 20) + round(s * 18 / 108), 20 + round(s * 18 / 108)))
        out.save(preview)
    print(f"icons from {src}: background #{bg[0]:02X}{bg[1]:02X}{bg[2]:02X}; Android res + native/windows written")


if __name__ == "__main__":
    main(sys.argv[1:])
