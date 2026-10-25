# Nocturne of Time: original moon phase icons (32x32 pixel art), generated procedurally.
import math, random
from PIL import Image, ImageDraw, ImageFont

N = 32            # icon size in pixels
R = 13.5          # moon radius
C = (N - 1) / 2   # centre
import os
OUT = os.path.dirname(os.path.abspath(__file__))

LIT = (232, 226, 196)      # lit surface, pale bone
LIT_SHADE = (196, 188, 158)
CRATER = (160, 150, 124)
DARK = (34, 36, 66)        # unlit side: deep indigo, still visible against the HUD
DARK_CRATER = (26, 27, 50)
OUTLINE = (12, 12, 24)
GLOW = (255, 96, 40)       # an ominous ember glow that grows toward Full Moon

random.seed(7)
craters = [(random.uniform(-0.75, 0.75), random.uniform(-0.75, 0.75), random.uniform(0.08, 0.2)) for _ in range(9)]

def surface(nx, ny):
    """Crater shading on the unit disc: 0 = plain, 1 = crater floor, 2 = crater rim highlight."""
    for cx, cy, cr in craters:
        d = math.hypot(nx - cx, ny - cy)
        if d < cr * 0.75:
            return 1
        if d < cr:
            return 2 if (nx - cx) + (ny - cy) < 0 else 1
    return 0

def icon(phase):
    """phase 0..7: 0 = New Moon, 7 = Full Moon; waxes from the right like SMT's indicator."""
    # Lit fraction per phase, spaced so each step reads at 32x32 (a linear step makes 2/8 vanish)
    f = [0.0, 0.24, 0.38, 0.5, 0.6, 0.7, 0.82, 1.0][phase]
    img = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    px = img.load()
    glow = f ** 2  # only really shows near Full Moon
    for y in range(N):
        for x in range(N):
            dx, dy = (x - C) / R, (y - C) / R
            d = math.hypot(dx, dy)
            if d > 1.0:
                # outer glow ring
                if glow > 0 and d < 1.0 + 3.0 / R:
                    a = int(150 * glow * (1 - (d - 1.0) / (3.0 / R)))
                    if a > 0:
                        px[x, y] = GLOW + (a,)
                continue
            if d > 1.0 - 1.2 / R:
                px[x, y] = OUTLINE + (255,)
                continue
            half = math.sqrt(max(0.0, 1 - dy * dy))
            # terminator: lit where dx > threshold; f=0 nothing lit, f=1 everything lit
            threshold = half * math.cos(math.pi * f)
            lit = dx > threshold if f > 0 else False
            s = surface(dx, dy)
            if lit:
                col = LIT if s == 0 else (CRATER if s == 1 else LIT)
                # soft limb darkening
                if d > 0.78 and s == 0:
                    col = LIT_SHADE
            else:
                col = DARK if s == 0 else DARK_CRATER
            px[x, y] = col + (255,)
    return img

icons = [icon(p) for p in range(8)]
names = ['new', '2of8', '3of8', 'half', '5of8', '6of8', '7of8', 'full']
for im, n in zip(icons, names):
    im.save(f'{OUT}/moon_{n}.png')

# Sprite sheet: 8 icons in a row
sheet = Image.new('RGBA', (N * 8, N), (0, 0, 0, 0))
for i, im in enumerate(icons):
    sheet.paste(im, (i * N, 0))
sheet.save(f'{OUT}/moon_phases_sheet.png')

# Preview: icons scaled up 4x with labels, plus a HUD mock of the counter
S = 4
labels = ['New Moon', '2/8', '3/8', 'Half Moon', '5/8', '6/8', '7/8', 'Full Moon']
prev = Image.new('RGB', (8 * (N * S + 24) + 24, N * S + 170), (20, 22, 34))
d = ImageDraw.Draw(prev)
try:
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 18)
    big = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 30)
except Exception:
    font = big = ImageFont.load_default()
for i, im in enumerate(icons):
    x = 24 + i * (N * S + 24)
    prev.paste(im.resize((N * S, N * S), Image.NEAREST), (x, 24), im.resize((N * S, N * S), Image.NEAREST))
    w = d.textlength(labels[i], font=font)
    d.text((x + (N * S - w) / 2, 24 + N * S + 8), labels[i], fill=(220, 220, 230), font=font)
# HUD mock (upper-right style): dark bar, icon, phase text
hy = N * S + 70
d.rounded_rectangle([24, hy, 24 + 300, hy + 72], radius=10, fill=(8, 10, 22), outline=(90, 110, 150), width=3)
mock = icons[2].resize((64, 64), Image.NEAREST)
prev.paste(mock, (34, hy + 4), mock)
d.text((112, hy + 16), '3/8', fill=(240, 240, 240), font=big)
d.text((190, hy + 24), 'MOON', fill=(150, 160, 190), font=font)
prev.save(f'{OUT}/moon_phases_preview.png')
print('ok')
