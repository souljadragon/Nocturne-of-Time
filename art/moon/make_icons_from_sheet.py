# Nocturne of Time: turn the hand-drawn moon sheet into HUD icons.
#
#   python3 make_icons_from_sheet.py <sheet.png>
#
# The sheet has the checkerboard baked in, so each moon is cut out as a circle (centres/radii measured from
# each moon's dark outline), scaled down, given a crisp 1px outline, and the purple glow for 7/8 and Full
# Moon is redrawn cleanly. Outputs per-phase PNGs at 32px and 48px, sprite sheets and a preview.
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = os.path.dirname(os.path.abspath(__file__))

# name -> (centre x, centre y, radius) on the source sheet; the duplicate second 6/8 is not used
CIRCLES = {
    "new": (168, 152, 120),
    "2of8": (502, 153, 119),
    "3of8": (834, 154, 119),
    "half": (172, 477, 123),
    "5of8": (503, 479, 121),
    "6of8": (839, 475, 122),
    "7of8": (503, 821, 117),
    "full": (839, 823, 118),
}
ORDER = ["new", "2of8", "3of8", "half", "5of8", "6of8", "7of8", "full"]
LABELS = ["New Moon", "2/8", "3/8", "Half Moon", "5/8", "6/8", "7/8", "Full Moon"]
GLOW = {"7of8": 0.55, "full": 1.0}  # strength of the purple aura
GLOW_COLOR = (140, 80, 255)
OUTLINE = (10, 8, 14, 255)


def cut_disc(sheet, cx, cy, r, inset=2):
    """Square crop around the disc with everything outside the circle made transparent (4x supersampled)."""
    size = 2 * r
    crop = sheet.crop((cx - r, cy - r, cx + r, cy + r)).convert("RGBA")
    ss = 4
    mask = Image.new("L", (size * ss, size * ss), 0)
    ImageDraw.Draw(mask).ellipse([inset * ss, inset * ss, (size - inset) * ss - 1, (size - inset) * ss - 1], fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)
    crop.putalpha(mask)
    return crop


def make_icon(disc, canvas, disc_px, glow):
    """Scale the disc to disc_px and centre it on a canvas x canvas icon with an outline and optional glow."""
    small = disc.resize((disc_px, disc_px), Image.LANCZOS)
    icon = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    off = (canvas - disc_px) // 2

    if glow > 0:
        aura = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
        d = ImageDraw.Draw(aura)
        pad = max(1, round(canvas * 0.06))
        d.ellipse([off - pad, off - pad, off + disc_px + pad - 1, off + disc_px + pad - 1],
                  fill=GLOW_COLOR + (int(255 * glow),))
        aura = aura.filter(ImageFilter.GaussianBlur(canvas * 0.05))
        icon = Image.alpha_composite(icon, aura)

    # 1px dark outline: the disc's silhouette grown by one pixel
    sil = Image.new("L", (canvas, canvas), 0)
    sil.paste(small.getchannel("A").point(lambda a: 255 if a > 100 else 0), (off, off))
    grown = sil.filter(ImageFilter.MaxFilter(3))
    outline = Image.new("RGBA", (canvas, canvas), OUTLINE)
    outline.putalpha(grown)
    icon = Image.alpha_composite(icon, outline)

    layer = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    layer.paste(small, (off, off), small)
    # Snap the disc's edge alpha so it reads as pixel art instead of a soft blur
    a = layer.getchannel("A").point(lambda v: 255 if v > 110 else 0)
    layer.putalpha(a)
    return Image.alpha_composite(icon, layer)


def main(sheet_path):
    sheet = Image.open(sheet_path).convert("RGB")
    discs = {k: cut_disc(sheet, *CIRCLES[k]) for k in ORDER}

    for canvas, disc_px in ((32, 28), (48, 42)):
        icons = [make_icon(discs[k], canvas, disc_px, GLOW.get(k, 0)) for k in ORDER]
        strip = Image.new("RGBA", (canvas * 8, canvas), (0, 0, 0, 0))
        for i, (k, im) in enumerate(zip(ORDER, icons)):
            im.save(os.path.join(OUT, f"moon_{k}_{canvas}.png"))
            strip.paste(im, (i * canvas, 0))
        strip.save(os.path.join(OUT, f"moon_phases_sheet_{canvas}.png"))

    # Preview: 48px icons at 3x with labels, the 32px set at 3x, and a HUD counter mock
    icons48 = [Image.open(os.path.join(OUT, f"moon_{k}_48.png")) for k in ORDER]
    icons32 = [Image.open(os.path.join(OUT, f"moon_{k}_32.png")) for k in ORDER]
    S = 3
    cell = 48 * S + 20
    prev = Image.new("RGB", (cell * 8 + 20, 48 * S + 32 * S + 190), (20, 22, 34))
    d = ImageDraw.Draw(prev)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
        big = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 30)
    except OSError:
        font = big = ImageFont.load_default()
    for i in range(8):
        x = 20 + i * cell
        a = icons48[i].resize((48 * S, 48 * S), Image.NEAREST)
        prev.paste(a, (x, 16), a)
        w = d.textlength(LABELS[i], font=font)
        d.text((x + (48 * S - w) / 2, 16 + 48 * S + 4), LABELS[i], fill=(220, 220, 230), font=font)
        b = icons32[i].resize((32 * S, 32 * S), Image.NEAREST)
        prev.paste(b, (x + (48 - 32) * S // 2, 48 * S + 44), b)
    hy = 48 * S + 32 * S + 64
    d.text((20, hy - 22), "48px set (top), 32px set (middle), HUD mock (bottom)", fill=(150, 160, 190), font=font)
    d.rounded_rectangle([20, hy, 20 + 300, hy + 100], radius=10, fill=(8, 10, 22), outline=(90, 110, 150), width=3)
    m = icons48[5].resize((96, 96), Image.NEAREST)
    prev.paste(m, (28, hy + 2), m)
    d.text((134, hy + 30), "6/8", fill=(240, 240, 240), font=big)
    d.text((206, hy + 38), "MOON", fill=(150, 160, 190), font=font)
    prev.save(os.path.join(OUT, "moon_phases_preview.png"))
    print("ok")


if __name__ == "__main__":
    main(sys.argv[1])
