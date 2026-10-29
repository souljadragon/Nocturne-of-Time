"""Write a type-1 MIDI file, one track per part, for FL Studio."""
import struct
import sys
sys.path.insert(0, '/home/claude/hollow_husk')
sys.path.insert(0, '/home/claude/shop')
from compose_s2 import build, BPM, INTRO_BEATS, LOOP_BEATS

PPQ = 480
# (part, track name, channel, GM program 0-based)
LAYOUT = [
    ('lead', 'Lead - dark detuned square/saw, short glide (melody)', 0, 80),
    ('pipe', 'Tuned metal pipe (A2 double 8va, the call in C)', 1, 14),
    ('epiano', 'Electric piano - tape-wobbled (bouncy comping)', 2, 4),
    ('bass', 'Bass - round + distorted grit layer', 3, 33),
    ('marimba', 'Dull marimba, far back (answers, B double)', 4, 12),
    ('pad', 'Dark pad', 5, 89),
    ('drums', 'Drums (36 kick, 38 snare+metal ring, 42/46 hats, 56 metal clank, 70 shaker)', 9, 0),
    ('amb', 'Industrial bed - swell/steam/clank (guide notes; hum + vinyl run throughout)', 6, 122),
]


def vlq(n):
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(b))


def track(events):
    events.sort(key=lambda e: (e[0], e[1]))
    out, last = b'', 0
    for tick, _, data in events:
        out += vlq(tick - last) + data
        last = tick
    out += vlq(0) + b'\xff\x2f\x00'
    return b'MTrk' + struct.pack('>I', len(out)) + out


def meta(t, typ, text):
    d = text.encode('ascii', 'replace')
    return b'\xff' + bytes([typ]) + vlq(len(d)) + d


def main(path):
    E, markers, end = build(loops=1)
    tracks = []
    us = int(60_000_000 / BPM)
    cond = [(0, 0, meta(0, 0x03, 'Take Your Time - Kokiri Haven Shop (placeholder v2) - swing 8ths 55%')),
            (0, 0, b'\xff\x51\x03' + us.to_bytes(3, 'big')),
            (0, 0, b'\xff\x58\x04\x04\x02\x18\x08')]
    for t, name in markers:
        cond.append((int(t * PPQ), 1, meta(0, 0x06, name)))
    tracks.append(track(cond))
    for part, name, ch, prog in LAYOUT:
        evs = [(0, 0, meta(0, 0x03, name))]
        if ch != 9:
            evs.append((0, 1, bytes([0xC0 | ch, prog])))
        for e in E[part]:
            on = int(round(e['t'] * PPQ))
            off = int(round((e['t'] + e['d']) * PPQ)) - 1
            notes = list(e['n'])
            if part == 'guitar':  # power chord: root, fifth, octave
                r = notes[0]
                notes = [r, r + 7, r + 12]
            if part == 'whisper':
                evs.append((on, 2, meta(0, 0x05, e['syl'])))
            for n in notes:
                evs.append((on, 3, bytes([0x90 | ch, n, min(127, e['v'])])))
                evs.append((max(off, on + 1), 2, bytes([0x80 | ch, n, 0])))
        tracks.append(track(evs))
    with open(path, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks), PPQ))
        for t in tracks:
            f.write(t)


if __name__ == '__main__':
    main('out/Take Your Time - Kokiri Haven Shop (placeholder v2) [parts].mid')
