"""Take Your Time - Kokiri Haven Shop (placeholder v2). Original composition.

A breath in a hopeless world: F minor with a Dorian lift (Bb13 = the one hopeful chord), every phrase
ending on a tense C7b9. 120 BPM, light 8th swing (55%). Grimy, bitcrushed, industrial.
Intro 2 bars (once) + loop 32 bars (64 s).
"""
import sys
sys.path.insert(0, '/home/claude/hollow_husk')
from compose import N, ev, KICK, SNARE, CLHAT, OPHAT

BPM = 120
INTRO_BEATS = 8
LOOP_BEATS = 128
SWING = 0.55
SHAKER, CLANK = 70, 56

PARTS = ['lead', 'pipe', 'epiano', 'bass', 'marimba', 'pad', 'drums', 'amb']

CH = {
    'Fm9':     (['F3', 'Eb4', 'G4', 'Ab4', 'C5'], 'F1'),
    'Bb13':    (['Bb2', 'Ab3', 'D4', 'G4'], 'Bb1'),
    'Dbmaj7':  (['Db3', 'C4', 'F4', 'Ab4'], 'Db2'),
    'C7b9':    (['C3', 'Bb3', 'Db4', 'E4', 'G4'], 'C2'),
    'C7sus4':  (['C3', 'Bb3', 'F4', 'G4'], 'C2'),
    'Abmaj7':  (['Ab2', 'G3', 'C4', 'Eb4'], 'Ab1'),
    'Gm7b5':   (['G2', 'F3', 'Bb3', 'Db4'], 'G1'),
    'Bbm9':    (['Bb2', 'Ab3', 'C4', 'Db4', 'F4'], 'Bb1'),
}
A_BARS = [[('Fm9', 4)], [('Bb13', 4)], [('Dbmaj7', 4)], [('C7b9', 4)],
          [('Fm9', 4)], [('Bb13', 4)], [('Dbmaj7', 4)], [('C7b9', 4)]]
B_BARS = [[('Abmaj7', 4)], [('Gm7b5', 2), ('C7b9', 2)], [('Fm9', 4)], [('Fm9', 4)],
          [('Dbmaj7', 4)], [('Bbm9', 4)], [('Gm7b5', 4)], [('C7b9', 4)]]
C_BARS = [[('Fm9', 4)], [('Fm9', 4)], [('Dbmaj7', 4)], [('Dbmaj7', 4)],
          [('Bbm9', 4)], [('Bbm9', 4)], [('C7sus4', 4)], [('C7b9', 4)]]

# ---- the melody (original): melancholic, mostly steps, eighths and quarters, around C5
A_MEL = [('r', .5), ('C5', .5), ('Eb5', .5), ('F5', .5), ('G5', 1), ('F5', .5), ('Eb5', .5),
         ('D5', 1), ('C5', .5), ('Bb4', .5), ('C5', 2),                       # D natural: the Dorian breath
         ('r', .5), ('Ab4', .5), ('C5', .5), ('Eb5', .5), ('F5', 1), ('Ab5', .5), ('F5', .5),
         ('E5', 1.5), ('Db5', .5), ('E5', 2),                                 # the C7b9 sting
         ('r', .5), ('C5', .5), ('Eb5', .5), ('F5', .5), ('G5', 1), ('Ab5', .5), ('G5', .5),
         ('F5', .5), ('D5', .5), ('F5', 1), ('G5', 2),
         ('Ab5', 1), ('G5', .5), ('F5', .5), ('Eb5', 1), ('C5', 1),
         ('Db5', 1.5), ('C5', .5), ('Bb4', 1), ('G4', 1)]
A2_END = [('Ab5', 1), ('G5', .5), ('F5', .5), ('G5', 1), ('Bb5', 1),
          ('Ab5', 1), ('G5', 1), ('E5', 2)]                                    # never quite resolves
B_MEL = [('Eb5', 1.5), ('C5', .5), ('Eb5', 1), ('G5', 1),
         ('F5', 2), ('E5', 1), ('C5', 1),
         ('Ab4', 1.5), ('G4', .5), ('Ab4', 1), ('C5', 1),
         ('C5', 3), ('r', 1),
         ('F5', 1.5), ('Eb5', .5), ('F5', 1), ('Ab5', 1),
         ('Db5', 3), ('F5', 1),
         ('Eb5', 1.5), ('Db5', .5), ('Bb4', 1), ('G4', 1),
         ('E5', 2), ('r', 2)]
CALL = [('C6', .5), ('Ab5', .5), ('G5', .5), ('Eb5', .5)]        # the pipe "bell over the door"


def chords(bars):
    for b, bar in enumerate(bars):
        t = 0
        for c, d in bar:
            yield b * 4 + t, d, c
            t += d


def epiano(E, t0, bars, v=70):
    for t, d, c in chords(bars):
        notes = CH[c][0]
        tb = t0 + t
        hits = [(0, .5, v + 6), (1.5, 1.5, v), (3.5, .5, v - 8)] if d == 4 else [(0, .5, v + 4), (1, 1, v)]
        for off, ln, vv in hits:
            E['epiano'].append(ev(tb + off, ln, notes, vv))


def bassline(E, t0, bars, v=96):
    allc = list(chords(bars))
    for i, (t, d, c) in enumerate(allc):
        r = N(CH[c][1])
        nxt = N(CH[allc[(i + 1) % len(allc)][2]][1])
        tb = t0 + t
        if d == 4:   # heavier and plainer than v1: root pedal with an octave pop and a chromatic slide in
            pat = [(0, 1.25, r), (1.5, .5, r + 12), (2, 1, r), (3, .5, r + 7), (3.5, .5, nxt + (1 if nxt < r else -1))]
        else:
            pat = [(0, 1, r), (1, .5, r + 12), (1.5, .5, nxt + (1 if nxt < r else -1))]
        for off, ln, n in pat:
            E['bass'].append(ev(tb + off, ln * 0.9, [n], v - (6 if off % 1 else 0)))


def drums(E, t0, bars, fill=True, muffled=False):
    v0 = 0.75 if muffled else 1.0
    for b in range(bars):
        tb = t0 + b * 4
        E['drums'] += [ev(tb, .25, KICK, int(106 * v0)), ev(tb + 1.5, .25, KICK, int(86 * v0)), ev(tb + 2, .25, KICK, int(100 * v0))]
        if b % 2 == 1:
            E['drums'].append(ev(tb + 3.5, .25, KICK, int(82 * v0)))
        E['drums'] += [ev(tb + 1, .25, SNARE, int(104 * v0)), ev(tb + 3, .25, SNARE, int(108 * v0)),
                       ev(tb + 1.75, .25, SNARE, 38), ev(tb + 2.75, .25, SNARE, 32)]
        for i in range(8):
            n = OPHAT if (i == 7 and b % 2 == 1) else CLHAT
            E['drums'].append(ev(tb + i * .5, .25, n, int((76 if i % 2 == 0 else 56) * v0)))
        for i in range(16):
            E['drums'].append(ev(tb + i * .25, .25, SHAKER, 40 if i % 2 else 48))
        # industrial: a metal clank on the "and" of 4 every other bar, a softer one on the "e" of 3 sometimes
        if b % 2 == 1:
            E['drums'].append(ev(tb + 3.5, .5, CLANK, 84))
        if b % 4 == 2:
            E['drums'].append(ev(tb + 2.25, .5, CLANK, 56))
    if fill:
        tb = t0 + (bars - 1) * 4
        E['drums'] = [e for e in E['drums'] if not (tb + 3 <= e['t'] < tb + 4 and e['n'][0] in (SNARE, CLHAT, OPHAT, KICK))]
        for i, vv in enumerate((60, 72, 84, 96)):
            E['drums'].append(ev(tb + 3 + i * .25, .25, SNARE, vv))


def melody(E, t0, mel, part='lead', v=96, octave=0):
    t = t0
    for n, d in mel:
        if n != 'r':
            E[part].append(ev(t, d * 0.92, [N(n) + 12 * octave], v))
        t += d


def amb(kind, t, d, v=100):
    return ev(t, d, [48], v, kind=kind)


def intro(E, t0):
    E['epiano'] += [ev(t0, 4, CH['Fm9'][0], 62), ev(t0 + 4, 4, CH['C7b9'][0], 64)]
    E['bass'] += [ev(t0 + 4, 3.6, [N('C2')], 78)]
    E['amb'] += [amb('swell', t0, 8, 100), amb('clank', t0 + 1, 1, 90), amb('steam', t0 + 3, 3, 70)]
    E['pipe'] += [ev(t0 + 5 + i * .5, .5, [N(n)], 58) for i, n in enumerate(['C6', 'Ab5', 'G5', 'E5'])]
    for i, vv in enumerate((40, 50, 60, 70, 80, 90)):
        E['drums'].append(ev(t0 + 6.5 + i * .25, .25, SNARE, vv))
    E['drums'].append(ev(t0 + 7.5, .25, KICK, 90))


def sec_a(E, t0, second=False):
    epiano(E, t0, A_BARS)
    bassline(E, t0, A_BARS)
    drums(E, t0, 8)
    mel = A_MEL[:-9] + A2_END if second else A_MEL
    melody(E, t0, mel, 'lead', 98 if second else 94)
    if second:
        melody(E, t0, mel, 'pipe', 54, octave=1)
        E['pad'] += [ev(t0 + b * 4, 4, CH[c][0][1:], 52) for b, c in enumerate(['Fm9', 'Bb13', 'Dbmaj7', 'C7b9'] * 2)]
    else:
        for b in (1, 5):   # dull wood (Kokiri) answers, far back
            E['marimba'] += [ev(t0 + b * 4 + 2.5 + i * .5, .5, [N(n)], 54) for i, n in enumerate(['G4', 'Bb4', 'C5'])]
    E['amb'] += [amb('clank', t0 + 15.5, 1, 70), amb('steam', t0 + 28, 2.5, 60)]


def sec_b(E, t0):
    epiano(E, t0, B_BARS, 64)
    bassline(E, t0, B_BARS, 90)
    drums(E, t0, 8)
    melody(E, t0, B_MEL, 'lead', 90)
    melody(E, t0, B_MEL, 'marimba', 48, octave=-1)
    for t, d, c in chords(B_BARS):
        E['pad'].append(ev(t0 + t, d, CH[c][0][1:], 60))
    E['amb'] += [amb('clank', t0 + 7.5, 1, 80), amb('clank', t0 + 23.5, 1, 74)]


def sec_c(E, t0):     # the breath: drums muffled "through the wall", the pipe call, machinery louder
    epiano(E, t0, C_BARS, 60)
    bassline(E, t0, C_BARS, 84)
    drums(E, t0, 8, muffled=True)
    for b in (0, 2, 4):
        melody(E, t0 + b * 4 + 1, CALL, 'pipe', 66)
        melody(E, t0 + b * 4 + 5, [('F5', .5), ('Eb5', .5), ('C5', 1)], 'pipe', 56)
    melody(E, t0 + 24, [('Ab5', 1), ('G5', 1), ('Eb5', 1), ('C5', 1), ('Db5', .5), ('C5', .5), ('Bb4', 1), ('E5', 2)], 'lead', 82)
    for t, d, c in chords(C_BARS):
        E['pad'].append(ev(t0 + t, d, CH[c][0][1:], 64))
    E['amb'] += [amb('swell', t0, 32, 110), amb('steam', t0 + 6, 3, 80), amb('clank', t0 + 13.5, 1, 86),
                 amb('steam', t0 + 18, 3, 74), amb('clank', t0 + 27.5, 1, 90)]


SECTIONS = [('A - Theme', 0, lambda E, t: sec_a(E, t)), ('A2 - Theme + pipe', 32, lambda E, t: sec_a(E, t, True)),
            ('B - Lyrical', 64, sec_b), ('C - The breath', 96, sec_c)]
MUFFLED = [(96, 128)]          # loop-relative beats where the drum bus is filtered


def build(loops=1):
    E = {p: [] for p in PARTS}
    markers = [(0, 'Intro')]
    intro(E, 0)
    t = INTRO_BEATS
    for n in range(loops):
        for name, off, fn in SECTIONS:
            fn(E, t + off)
            markers.append((t + off, ('LOOP START / ' if off == 0 and n == 0 else '') + name))
        t += LOOP_BEATS
    markers.append((t, 'LOOP END'))
    return E, markers, t


def swing(t):
    frac = t % 1.0
    if abs(frac - 0.5) < 1e-6:
        return t - 0.5 + SWING
    if abs(frac - 0.75) < 1e-6:
        return t - 0.75 + SWING + (1 - SWING) / 2
    if abs(frac - 0.25) < 1e-6:
        return t - 0.25 + SWING / 2
    return t
