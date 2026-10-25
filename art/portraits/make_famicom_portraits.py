# Nocturne of Time: Famicom-style versions of the party portraits (mock-up variants).
#   python3 make_famicom_portraits.py
# From the original art: shrink to a small sprite, pick a handful of colours from the NES palette, map every
# pixel to them with no dithering, black background. Three looks:
#   A  48px, 5 colours + accent          - closest to the art
#   B  32px, 4 colours + accent          - chunkiest
#   C  40px, 5 colours + accent + ink    - black outlines on edges, like the Famicom Megami Tensei portraits
import importlib.util
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "famicom")
spec = importlib.util.spec_from_file_location("portraits", os.path.join(HERE, "make_portraits.py"))
src = importlib.util.module_from_spec(spec)
spec.loader.exec_module(src)

# NES (2C02) palette, without the duplicate blacks
NES = [
    0x7C7C7C, 0x0000FC, 0x0000BC, 0x4428BC, 0x940084, 0xA80020, 0xA81000, 0x881400, 0x503000, 0x007800, 0x006800,
    0x005800, 0x004058, 0x000000, 0xBCBCBC, 0x0078F8, 0x0058F8, 0x6844FC, 0xD800CC, 0xE40058, 0xF83800, 0xE45C10,
    0xAC7C00, 0x00B800, 0x00A800, 0x00A844, 0x008888, 0xF8F8F8, 0x3CBCFC, 0x6888FC, 0x9878F8, 0xF878F8, 0xF85898,
    0xF87858, 0xFCA044, 0xF8B800, 0xB8F818, 0x58D854, 0x58F898, 0x00E8D8, 0x787878, 0xFCFCFC, 0xA4E4FC, 0xB8B8F8,
    0xD8B8F8, 0xF8B8F8, 0xF8A4C0, 0xF0D0B0, 0xFCE0A8, 0xF8D878, 0xD8F878, 0xB8F8B8, 0xB8F8D8, 0x00FCFC, 0xD8D8D8,
]
NES = [((c >> 16) & 255, (c >> 8) & 255, c & 255) for c in NES]
BLACK = (0, 0, 0)
SKIN = [(0xF0, 0xD0, 0xB0), (0xFC, 0xE0, 0xA8), (0xF8, 0xB8, 0xF8), (0xF8, 0xA4, 0xC0), (0xF8, 0x78, 0x58),
        (0xFC, 0xA0, 0x44)]

# Per-character colours to avoid (a shadow can land on a vivid NES entry that reads as a stain)
AVOID = {"link": [(0xA8, 0x10, 0x00), (0xA8, 0x00, 0x20), (0x88, 0x14, 0x00), (0xE4, 0x00, 0x58), (0xF8, 0x38, 0x00),
                  (0x7C, 0x7C, 0x7C), (0xBC, 0xBC, 0xBC), (0x78, 0x78, 0x78), (0xD8, 0xD8, 0xD8)]}  # skin shades in orange, as on the Famicom

# Hand-picked palettes for portraits the automatic pick gets wrong (NES colours; black is added)
def _c(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


PALETTE = {
    # blonde hair, cream wraps with tan shading, blue armour, pale skin, red eyes and Sheikah eye
    "sheik": [_c(0xF8B800), _c(0xAC7C00), _c(0x503000), _c(0xFCE0A8), _c(0xF0D0B0), _c(0x7C7C7C), _c(0x6888FC),
              _c(0x4428BC), _c(0xA81000)],
    # gold hair and brown shade, cream skin with peach shading, blue eyes, lilac dress, red gem
    "zelda": [_c(0xF8B800), _c(0xAC7C00), _c(0x503000), _c(0xFCE0A8), _c(0xF0D0B0), _c(0xFCA044), _c(0x3CBCFC),
              _c(0xD8B8F8), _c(0xF83800)],
}

# Colours limited to a band of rows (as a fraction of the height): Sheik's red is only for the eye on her chest
ZONES = {"sheik": {_c(0xA81000): (0.68, 1.0)}}

# Hand-placed pixels after the conversion: (style, name) -> [(x, y, colour)]
FIXES = {
    # Saria's small smile is lighter than her skin shadow and vanishes at 48px
    ("A", "saria"): [(22, 31, _c(0xE45C10)), (23, 31, _c(0xE45C10)), (24, 31, _c(0xE45C10)), (21, 30, _c(0xF87858)),
                     (25, 30, _c(0xF87858))],
    # Zelda's diadem gem, a clear red stone at 48px
    ("A", "zelda"): [(23, 12, _c(0xF83800)), (24, 12, _c(0xF83800)), (23, 13, _c(0xA81000)), (24, 13, _c(0xA81000))],
}

STYLES = {"A": (48, 5, False), "B": (32, 4, False), "C": (40, 5, True)}  # size, main colours (+1 accent, black), ink


def dist(a, b):
    return 2 * (a[0] - b[0]) ** 2 + 4 * (a[1] - b[1]) ** 2 + 3 * (a[2] - b[2]) ** 2


def nearest(c, pal):
    return min(pal, key=lambda p: dist(c, p))


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def saturation(c):
    return max(c) - min(c)


def famicom(img, size, colors, ink, name=""):
    small = img.resize((size, size), Image.BOX).convert("RGB")
    # a little punch before choosing colours
    small = small.point(lambda v: max(0, min(255, int((v - 128) * 1.15 + 128))))
    px = small.load()
    # 1. every pixel to its nearest NES colour, and how often each is used
    counts = {}
    mapped = {}
    for y in range(size):
        for x in range(size):
            c = px[x, y]
            n = BLACK if lum(c) < 34 else nearest(c, [q for q in NES if q not in AVOID.get(name, [])])
            mapped[x, y] = n
            if n != BLACK:
                counts[n] = counts.get(n, 0) + 1
    # 2. keep the most used colours that are clearly different from each other...
    pal = []
    for c, _ in sorted(counts.items(), key=lambda kv: -kv[1]):
        if all(dist(c, q) > 9000 for q in pal):
            pal.append(c)
        if len(pal) == colors:
            break
    if name in PALETTE:
        pal = list(PALETTE[name])
    # skin always survives when the face shows it (otherwise a small palette can turn it grey)
    skins = [c for c in SKIN if counts.get(c, 0) >= size * size * 0.04]
    if skins and not any(q in SKIN for q in pal):
        pal[-1] = max(skins, key=lambda c: counts[c])
    # ...plus one accent: the most vivid colour left that covers only a few pixels (eyes, gems)
    small_area = size * size * 0.025
    rest = [] if name in PALETTE else [c for c, n in counts.items()
            if 3 <= n <= small_area and c not in SKIN and all(dist(c, q) > 12000 for q in pal)]
    if rest:
        pal.append(max(rest, key=saturation))
    pal.append(BLACK)
    out = Image.new("RGB", (size, size))
    o = out.load()
    for y in range(size):
        for x in range(size):
            c = mapped[x, y]
            choices = pal
            if name in ZONES:
                choices = [q for q in pal
                           if q not in ZONES[name] or ZONES[name][q][0] <= y / size < ZONES[name][q][1]]
            o[x, y] = BLACK if c == BLACK else nearest(px[x, y], choices)
    if ink:
        # black ink where the picture changes sharply, on the darker side of the edge
        res = out.copy()
        r = res.load()
        for y in range(size):
            for x in range(size):
                c = o[x, y]
                if c == BLACK:
                    continue
                for dx, dy in ((1, 0), (0, 1), (-1, 0), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < size and 0 <= ny < size:
                        n = o[nx, ny]
                        if n != BLACK and lum(n) - lum(c) > 70:
                            r[x, y] = BLACK
                            break
        out = res
    return out


def main():
    crops = {}
    for file, entries in src.FRAMES.items():
        img = Image.open(os.path.join(src.SRC, file)).convert("RGB")
        for name, (l, t, r, b) in entries:
            w, h = r - l, b - t
            pad = int(min(w, h) * src.INSET)
            l, t, r, b = l + pad, t + pad, r - pad, b - pad
            side = min(r - l, b - t)
            cx, cy = (l + r) // 2, (t + b) // 2
            crops[name] = img.crop((cx - side // 2, cy - side // 2, cx - side // 2 + side, cy - side // 2 + side))

    os.makedirs(OUT, exist_ok=True)
    S = 3  # preview scale of a 64px cell
    cell = 64 * S
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 18)
    for style, (size, colors, ink) in STYLES.items():
        for name in src.ORDER:
            f = famicom(crops[name], size, colors, ink, name)
            fp = f.load()
            for x, y, c in FIXES.get((style, name), []):
                fp[x, y] = c
            f.save(os.path.join(OUT, f"portrait_{style}_{name}_{size}.png"))
            f.resize((size * 4, size * 4), Image.NEAREST).save(os.path.join(OUT, f"portrait_{style}_{name}_x4.png"))

    # comparison sheet: original 64px and the three styles, all shown at the same 192px
    rows = ["orig"] + list(STYLES)
    prev = Image.new("RGB", (40 + len(src.ORDER) * (cell + 16), 40 + len(rows) * (cell + 40)), (0, 0, 0))
    d = ImageDraw.Draw(prev)
    for ri, style in enumerate(rows):
        y = 30 + ri * (cell + 40)
        label = "Original 64px" if style == "orig" else f"{style}: {STYLES[style][0]}px, {STYLES[style][1]}+1 colours" + (
            " + ink" if STYLES[style][2] else "")
        d.text((20, y - 24), label, fill=(184, 248, 184), font=font)
        for ci, name in enumerate(src.ORDER):
            x = 20 + ci * (cell + 16)
            if style == "orig":
                im = Image.open(os.path.join(HERE, f"portrait_{name}_64.png")).convert("RGB")
            else:
                im = Image.open(os.path.join(OUT, f"portrait_{style}_{name}_{STYLES[style][0]}.png"))
            prev.paste(im.resize((cell, cell), Image.NEAREST), (x, y))
    prev.save(os.path.join(OUT, "famicom_portraits_compare.png"))
    print("ok")


if __name__ == "__main__":
    main()
