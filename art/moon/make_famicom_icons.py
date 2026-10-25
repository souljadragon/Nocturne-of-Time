# Nocturne of Time: Famicom-style moon sprites from the hand-drawn sheet.
#   python3 make_famicom_icons.py source/moon_faces_sheet.png
# Same faces, shadows and glowing eyes as make_icons_from_sheet.py, but shrunk to a 24px sprite, snapped to a
# handful of Famicom (NES) palette colours with no blending, and given a hard 1px black outline.
import importlib.util
import os
import sys

from PIL import Image, ImageFilter, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "famicom")
spec = importlib.util.spec_from_file_location("icons", os.path.join(HERE, "make_icons_from_sheet.py"))
icons = importlib.util.module_from_spec(spec)
spec.loader.exec_module(icons)

SIZE = 32   # sprite
DISC = 30   # moon inside it, leaving room for the outline

# Famicom palette entries
BLACK = (0, 0, 0)
BROWN_D = (80, 48, 0)      # 0x07
BROWN = (172, 124, 0)      # 0x17
CREAM = (252, 224, 168)    # 0x37
WHITE = (252, 252, 252)    # 0x30
GREY_D = (60, 60, 60)
VIOLET_D = (68, 40, 188)   # 0x12
VIOLET = (104, 68, 252)    # 0x22 / 0x13 blend
LILAC = (168, 120, 248)    # 0x23
LILAC_L = (216, 184, 248)  # 0x33

WARM = [BLACK, BROWN_D, BROWN, CREAM, WHITE, LILAC]          # New .. 6/8 (lilac only for glowing eyes)
COLD = [BLACK, VIOLET_D, VIOLET, LILAC, LILAC_L, WHITE]       # 7/8 and Full
PALETTES = {k: WARM for k in icons.ORDER}
PALETTES["7of8"] = COLD
PALETTES["full"] = COLD
PALETTES["new"] = [BLACK, GREY_D, BROWN_D, BROWN, CREAM]     # unlit: mostly dark, no glow


def nearest(c, pal):
    r, g, b = c
    best = None
    for p in pal:
        # weighted RGB distance (greens matter most to the eye)
        d = 2 * (r - p[0]) ** 2 + 4 * (g - p[1]) ** 2 + 3 * (b - p[2]) ** 2
        if best is None or d < best[0]:
            best = (d, p)
    return best[1]


def sprite(disc, name):
    small = disc.resize((DISC, DISC), Image.BOX)
    px = small.load()
    pal = PALETTES[name]
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    o = out.load()
    off = (SIZE - DISC) // 2
    for y in range(DISC):
        for x in range(DISC):
            r, g, b, a = px[x, y]
            if a < 128:
                continue
            # a touch more contrast so features survive the small palette; inside the moon only the deepest
            # lines go black, so a shadowed side still reads (dark brown) against a black screen
            c = tuple(max(0, min(255, int((v - 128) * 1.15 + 128))) for v in (r, g, b))
            lum = 0.3 * r + 0.59 * g + 0.11 * b
            black_below = 10 if name in ("new", "2of8") else 30  # the dark faces keep their brown shading
            choices = pal if lum < black_below else [p for p in pal if p != BLACK]
            if name == "2of8" and lum < 90:
                choices = [BLACK] if lum < 10 else [BROWN_D]  # the unlit side stays in the darkest brown
            if pal is WARM and not (b >= r - 5 and b > 100):
                choices = [p for p in choices if p != LILAC]  # violet only where the drawing glows
            o[x + off, y + off] = nearest(c, choices) + (255,)
    # hard 1px black outline around the silhouette
    sil = out.getchannel("A").point(lambda v: 255 if v else 0)
    grown = sil.filter(ImageFilter.MaxFilter(3))
    outline = Image.new("RGBA", (SIZE, SIZE), BLACK + (255,))
    outline.putalpha(grown)
    return Image.alpha_composite(outline, out)


def main(sheet_path):
    os.makedirs(OUT, exist_ok=True)
    sheet = Image.open(sheet_path).convert("RGB")
    raw = {k: icons.cut_disc(sheet, *icons.CIRCLES[k]) for k in icons.ORDER}
    fit = icons.fit_shadow(raw["half"])
    sprites = []
    for k in icons.ORDER:
        disc = icons.glow_eyes(icons.reshade(raw[k], k, fit), icons.EYES[k], icons.EYE_GLOW[k])
        sp = sprite(disc, k)
        sp.save(os.path.join(OUT, f"moon_famicom_{k}_{SIZE}.png"))
        sp.resize((SIZE * 4, SIZE * 4), Image.NEAREST).save(os.path.join(OUT, f"moon_famicom_{k}_{SIZE * 4}.png"))
        sprites.append(sp)

    # preview: all eight at 4x on black with labels
    S = 4
    cell = SIZE * S + 24
    prev = Image.new("RGB", (cell * 8 + 24, SIZE * S + 70), (0, 0, 0))
    d = ImageDraw.Draw(prev)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
    except OSError:
        font = ImageFont.load_default()
    labels = ["NEW", "2/8", "3/8", "HALF", "5/8", "6/8", "7/8", "FULL"]
    for i, sp in enumerate(sprites):
        big = sp.resize((SIZE * S, SIZE * S), Image.NEAREST)
        x = 24 + i * cell
        prev.paste(big, (x, 20), big)
        w = d.textlength(labels[i], font=font)
        d.text((x + (SIZE * S - w) / 2, SIZE * S + 32), labels[i], fill=(252, 252, 252), font=font)
    prev.save(os.path.join(OUT, "moon_famicom_preview.png"))
    print("ok")


if __name__ == "__main__":
    main(sys.argv[1])
