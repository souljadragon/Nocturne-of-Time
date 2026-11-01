#!/usr/bin/env python3
"""Nocturne of Time: placeholder textures for the Hollowed Husk's grid floors (Famicom-style, 32x32 RGBA).

Writes soh/soh/Enhancements/NocturneOfTime/Dungeon/HuskTextures.h. Procedural and seeded, so a rerun gives the
same pixels; swap any of them for hand-drawn art later (same size and order).
    python3 art/husk/make_husk_textures.py
"""
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "soh", "soh", "Enhancements", "NocturneOfTime", "Dungeon", "HuskTextures.h")
N = 32
CLEAR = (0, 0, 0, 0)


def rgb(h, a=255):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255, a)


# NES-ish browns, greens, violets
BARK = [rgb(0x503000), rgb(0x6C4818), rgb(0x885C20), rgb(0x3C2410)]
DIRT = [rgb(0x403018), rgb(0x503C20), rgb(0x2C2010), rgb(0x584428)]
CEIL = [rgb(0x281808), rgb(0x382410), rgb(0x1C1008)]


def blank(c=CLEAR):
    return [[c] * N for _ in range(N)]


def noise(pal, seed, streak=0):
    r = random.Random(seed)
    img = blank()
    for y in range(N):
        for x in range(N):
            img[y][x] = pal[r.randrange(len(pal))]
    # vertical bark grain
    if streak:
        for _ in range(streak):
            x = r.randrange(N)
            for y in range(N):
                if r.random() < 0.8:
                    img[y][(x + (y // 8)) % N] = pal[-1]
    return img


def bark():
    img = noise(BARK[:3], 1, streak=10)
    for y in range(0, N, 16):  # rings
        for x in range(N):
            if (x + y) % 3:
                img[y][x] = BARK[3]
    return img


def floor():
    img = noise(DIRT, 2)
    r = random.Random(3)
    for _ in range(4):  # roots
        y = r.randrange(N)
        for x in range(N):
            img[(y + x // 6) % N][x] = rgb(0x6C4818)
    return img


def ceiling():
    return noise(CEIL, 4, streak=6)


def web():
    img = blank()
    c = rgb(0xD8D8D8, 230)
    for i in range(N):
        img[i][i] = c
        img[i][N - 1 - i] = c
        img[N // 2][i] = c
        img[i][N // 2] = c
    for rr in (5, 10, 15):
        for i in range(N):
            for j in range(N):
                d = abs(i - N // 2) + abs(j - N // 2)
                if d == rr:
                    img[i][j] = c
    return img


def post():
    img = noise([rgb(0x3C2410), rgb(0x503000), rgb(0x2C1C08)], 5, streak=4)
    for x in range(N):
        img[0][x] = img[1][x] = rgb(0x787878)
    return img


def flame(seed):
    img = blank()
    r = random.Random(seed)
    for y in range(N):
        width = int((y / N) * 12 * (0.8 + 0.4 * r.random()))
        for x in range(16 - width, 16 + width):
            if 0 <= x < N:
                d = abs(x - 16) / max(1, width)
                img[y][x] = rgb(0xFCE0A8) if d < 0.3 and y > 18 else (rgb(0xF8B800) if d < 0.6 else rgb(0xF83800))
    return img


def chest(opened):
    wood = [rgb(0x885C20), rgb(0x6C4818)]
    img = noise(wood, 6)
    gold = rgb(0xF8B800)
    for x in range(N):
        img[0][x] = img[N - 1][x] = gold
        img[N // 2][x] = gold
    for y in range(N):
        img[y][0] = img[y][N - 1] = gold
    if opened:
        for y in range(2, N // 2):
            for x in range(2, N - 2):
                img[y][x] = rgb(0x100800)
    else:
        for y in range(N // 2 - 3, N // 2 + 4):
            for x in range(13, 19):
                img[y][x] = gold
    return img


def stone():
    img = noise([rgb(0x7C7C7C), rgb(0x6C6C6C), rgb(0x888888)], 7)
    eye = rgb(0xF83800)
    for x in range(8, 24):  # the Sheikah eye
        img[12][x] = rgb(0x3C3C3C)
        img[20][x] = rgb(0x3C3C3C)
    for y in range(13, 20):
        img[y][8] = img[y][23] = rgb(0x3C3C3C)
    for y in range(14, 19):
        for x in range(14, 18):
            img[y][x] = eye
    for y in range(21, 28):
        img[y][15] = img[y][16] = rgb(0x3C3C3C)
    return img


def blob(c1, c2, seed):
    img = blank()
    r = random.Random(seed)
    for y in range(N):
        for x in range(N):
            d = ((x - 16) ** 2 + (y - 18) ** 2) ** 0.5
            if d < 13 + r.random() * 1.5:
                img[y][x] = c1 if r.random() < 0.8 else c2
    for y in range(8, 14):
        for x in range(12, 20):
            if ((x - 16) ** 2 + (y - 11) ** 2) < 8:
                img[y][x] = c2
    return img


def membrane():
    img = blank()
    r = random.Random(8)
    for y in range(N):
        for x in range(N):
            img[y][x] = rgb(0x881400, 200) if r.random() < 0.8 else rgb(0xA81000, 220)
    for y in range(N):  # veins
        img[y][(y * 3) % N] = rgb(0xA878F8, 230)
        img[y][(N - 1 - y * 2) % N] = rgb(0x6844FC, 230)
    return img


def scatter(colors, density, seed):
    img = blank()
    r = random.Random(seed)
    for y in range(N):
        for x in range(N):
            if r.random() < density:
                img[y][x] = colors[r.randrange(len(colors))]
    return img


def brambles():
    img = scatter([rgb(0x005800), rgb(0x007800)], 0.25, 9)
    r = random.Random(10)
    for _ in range(14):  # thorns
        x, y = r.randrange(N), r.randrange(N)
        img[y][x] = rgb(0xF0D0B0)
    return img


def rot():
    return scatter([rgb(0x6844FC, 200), rgb(0x4428BC, 200), rgb(0x940084, 180)], 0.45, 11)


def hole():
    img = blank()
    for y in range(N):
        for x in range(N):
            d = ((x - 15.5) ** 2 + (y - 15.5) ** 2) ** 0.5
            if d < 13:
                img[y][x] = rgb(0x000000)
            elif d < 15:
                img[y][x] = rgb(0x3C2410)
    return img


def vines():
    img = blank()
    r = random.Random(12)
    for x0 in (5, 13, 21, 27):
        x = x0
        for y in range(N):
            x = max(0, min(N - 1, x + r.choice((-1, 0, 0, 1))))
            img[y][x] = rgb(0x00A800)
            if r.random() < 0.15 and x + 1 < N:
                img[y][x + 1] = rgb(0x58D854)
    return img


def mark():
    img = blank()
    c = rgb(0xFCE0A8, 160)
    for y in (8, 14, 20):
        for x in range(6, 26):
            if (x // 3) % 2 == 0:
                img[y][x] = c
    return img


def glow():
    img = blank()
    for y in range(N):
        for x in range(N):
            d = ((x - 15.5) ** 2 + (y - 15.5) ** 2) ** 0.5
            if d < 15:
                img[y][x] = rgb(0xFCE0A8, int(140 * (1 - d / 15)))
    return img


TEXTURES = [
    ("Wall", bark()), ("Floor", floor()), ("Ceiling", ceiling()), ("Web", web()), ("Post", post()),
    ("Flame0", flame(20)), ("Flame1", flame(21)), ("Chest", chest(False)), ("ChestOpen", chest(True)),
    ("Stone", stone()), ("Sac", blob(rgb(0xF8B800), rgb(0xF83800), 13)), ("Gate", membrane()),
    ("Brambles", brambles()), ("Rot", rot()), ("Hole", hole()), ("Vines", vines()), ("Mark", mark()),
    ("Glow", glow()),
]


def main():
    lines = ["#pragma once", "// Generated by art/husk/make_husk_textures.py - do not edit by hand.",
             "#include <cstdint>", "", "namespace HuskTex {", "constexpr int kSize = 32;"]
    names = []
    for name, img in TEXTURES:
        data = []
        for row in img:
            for (r, g, b, a) in row:
                data += [r, g, b, a]
        lines.append(f"alignas(8) static const uint8_t g{name}[{len(data)}] = {{")
        for i in range(0, len(data), 32):
            lines.append("    " + ", ".join(str(v) for v in data[i:i + 32]) + ",")
        lines.append("};")
        names.append(name)
    lines.append("enum Id { " + ", ".join(names) + ", Count };")
    lines.append("static const uint8_t* const kAll[Count] = { " + ", ".join("g" + n for n in names) + " };")
    lines.append("} // namespace HuskTex")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
