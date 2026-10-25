# Nocturne of Time: cut the hand-made character portraits out of their framed source images.
#
#   python3 make_portraits.py
#
# Each source image holds two portraits inside white frames. The inside of each frame is cropped (just inside
# the frame's inner border), squared from the centre, and saved at 64px and 32px, plus a preview sheet.
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "source")

# source file -> [(name, (left, top, right, bottom) inside the white frame)]
FRAMES = {
    "link_saria.jpg": [("link", (166, 209, 1202, 1262)), ("saria", (1373, 209, 2420, 1262))],
    "malon_darunia.jpg": [("malon", (124, 93, 582, 523)), ("darunia", (814, 93, 1270, 523))],
    "sheik_kaepora.jpg": [("sheik", (86, 198, 1212, 1347)), ("kaepora", (1363, 198, 2490, 1347))],
    "ruto_impa.jpg": [("ruto", (166, 209, 1202, 1261)), ("impa", (1373, 209, 2421, 1261))],
    "nabooru_zelda.jpg": [("nabooru", (66, 83, 477, 501)), ("zelda", (546, 83, 962, 501))],
}
ORDER = ["link", "saria", "malon", "darunia", "ruto", "impa", "sheik", "nabooru", "zelda", "kaepora"]
LABELS = {"kaepora": "Kaepora"}
INSET = 0.025  # skip the thin dark line just inside each frame
SIZES = (64, 32)


def main():
    crops = {}
    for file, entries in FRAMES.items():
        img = Image.open(os.path.join(SRC, file)).convert("RGB")
        for name, (l, t, r, b) in entries:
            w, h = r - l, b - t
            pad = int(min(w, h) * INSET)
            l, t, r, b = l + pad, t + pad, r - pad, b - pad
            side = min(r - l, b - t)
            cx, cy = (l + r) // 2, (t + b) // 2
            crops[name] = img.crop((cx - side // 2, cy - side // 2, cx - side // 2 + side, cy - side // 2 + side))

    for size in SIZES:
        sheet = Image.new("RGB", (size * len(ORDER), size), (0, 0, 0))
        for i, name in enumerate(ORDER):
            small = crops[name].resize((size, size), Image.LANCZOS)
            small.save(os.path.join(HERE, f"portrait_{name}_{size}.png"))
            sheet.paste(small, (i * size, 0))
        sheet.save(os.path.join(HERE, f"portraits_sheet_{size}.png"))

    # Preview: 64px portraits at 2x with names
    S = 2
    cell = 64 * S + 16
    prev = Image.new("RGB", (cell * 5 + 16, (64 * S + 30) * 2 + 16), (20, 22, 34))
    d = ImageDraw.Draw(prev)
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14)
    except OSError:
        font = ImageFont.load_default()
    for i, name in enumerate(ORDER):
        x = 16 + (i % 5) * cell
        y = 16 + (i // 5) * (64 * S + 30)
        im = Image.open(os.path.join(HERE, f"portrait_{name}_64.png")).resize((64 * S, 64 * S), Image.NEAREST)
        d.rectangle([x - 2, y - 2, x + 64 * S + 1, y + 64 * S + 1], outline=(120, 140, 170), width=2)
        prev.paste(im, (x, y))
        label = LABELS.get(name, name.capitalize())
        w = d.textlength(label, font=font)
        d.text((x + (64 * S - w) / 2, y + 64 * S + 6), label, fill=(220, 220, 230), font=font)
    prev.save(os.path.join(HERE, "portraits_preview.png"))
    print("ok")


if __name__ == "__main__":
    main()
