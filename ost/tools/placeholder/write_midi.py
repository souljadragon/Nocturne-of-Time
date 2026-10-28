"""Write a type-1 MIDI file, one track per part, for FL Studio."""
import struct
from compose import build, BPM, INTRO_BEATS, LOOP_BEATS

PPQ = 480
# (part, track name, channel, GM program 0-based)
LAYOUT = [
    ('lead', 'Lead - hollow wood flute (motif)', 0, 77),
    ('lead2', 'Lead double - distorted guitar, 8vb', 10, 30),
    ('guitar', 'Rhythm Guitar (drop C, power chords)', 1, 30),
    ('bass', 'Bass (pick/slap)', 2, 36),
    ('organ', 'Organ', 3, 18),
    ('arp', 'Wood Arp (marimba / log drum)', 4, 12),
    ('orch', 'Orchestra Hit', 5, 55),
    ('choir', 'Choir Aah (modern layer)', 6, 52),
    ('strings', 'Strings (modern layer)', 7, 48),
    ('sub', 'Sub Bass (modern layer, sine)', 8, 38),
    ('drums', 'Drums (GM map)', 9, 0),
    ('tree', 'Tree FX - creak/groan/wind/knock/drone (guide notes)', 11, 115),
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
    cond = [(0, 0, meta(0, 0x03, 'Hollow Husk - Random Encounter')),
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
            for n in notes:
                evs.append((on, 3, bytes([0x90 | ch, n, min(127, e['v'])])))
                evs.append((max(off, on + 1), 2, bytes([0x80 | ch, n, 0])))
        tracks.append(track(evs))
    with open(path, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks), PPQ))
        for t in tracks:
            f.write(t)


if __name__ == '__main__':
    main('out/hollow_husk_parts.mid')
