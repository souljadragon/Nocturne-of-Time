"""Take Your Time - Kokiri Haven Shop (placeholder v1). Original composition.

120 BPM, 4/4, F major (the relative major of Hollowed Husk's D minor: same family, but home and safe).
Light 8th swing (55%). Intro 2 bars (once) + loop 32 bars (64 s).
"""
import sys
sys.path.insert(0, '/home/claude/hollow_husk')
from compose import N, ev, seq, KICK, SNARE, CLHAT, OPHAT, CRASH, RIDE

BPM = 120
INTRO_BEATS = 8
LOOP_BEATS = 128
SWING = 0.55          # the off-8th lands at 55% of the beat
SHAKER = 70

PARTS = ['lead', 'bell', 'epiano', 'bass', 'marimba', 'pad', 'drums']

CH = {  # e-piano voicing (left hand + right hand), bass root
    'Fmaj9':   (['F3', 'E4', 'G4', 'A4', 'C5'], 'F2'),
    'Fmaj7':   (['F3', 'E4', 'A4', 'C5'], 'F2'),
    'Dm9':     (['D3', 'C4', 'E4', 'F4', 'A4'], 'D2'),
    'Bbmaj7':  (['Bb2', 'A3', 'D4', 'F4'], 'Bb1'),
    'Bbmaj9':  (['Bb2', 'A3', 'C4', 'D4', 'F4'], 'Bb1'),
    'C7sus4':  (['C3', 'Bb3', 'F4', 'G4'], 'C2'),
    'C7':      (['C3', 'Bb3', 'E4', 'G4'], 'C2'),
    'C9sus4':  (['C3', 'Bb3', 'D4', 'F4', 'G4'], 'C2'),
    'Am7':     (['A2', 'G3', 'C4', 'E4'], 'A1'),
    'Gm7':     (['G2', 'F3', 'Bb3', 'D4'], 'G1'),
    'Gm9':     (['G2', 'F3', 'A3', 'Bb3', 'D4'], 'G1'),
    'D7':      (['D3', 'C4', 'F#4', 'A4'], 'D2'),
}
# each bar: list of (chord, beats)
A_BARS = [[('Fmaj9', 4)], [('Dm9', 4)], [('Bbmaj7', 4)], [('C7sus4', 2), ('C7', 2)],
          [('Fmaj9', 4)], [('Am7', 4)], [('Bbmaj7', 4)], [('Gm7', 2), ('C7', 2)]]
B_BARS = [[('Bbmaj7', 4)], [('Am7', 4)], [('Gm7', 4)], [('Fmaj7', 4)],
          [('Bbmaj7', 4)], [('Am7', 2), ('D7', 2)], [('Gm7', 4)], [('C7sus4', 2), ('C7', 2)]]
C_BARS = [[('Dm9', 4)], [('Bbmaj9', 4)], [('Dm9', 4)], [('Bbmaj9', 4)],
          [('Gm9', 4)], [('Gm9', 4)], [('C9sus4', 4)], [('C7', 4)]]

# ---- the melody (original). Mostly steps, a few 4th/5th leaps, eighths and quarters, around C5.
A_MEL = [('r', .5), ('C5', .5), ('F5', .5), ('G5', .5), ('A5', 1), ('G5', .5), ('F5', .5),      # 1 "take your time"
         ('E5', 1), ('D5', .5), ('C5', .5), ('D5', 2),                                           # 2
         ('r', .5), ('D5', .5), ('F5', .5), ('G5', .5), ('A5', 1), ('C6', .5), ('A5', .5),       # 3
         ('G5', 1.5), ('F5', .5), ('G5', 2),                                                     # 4
         ('r', .5), ('C5', .5), ('F5', .5), ('G5', .5), ('A5', 1), ('G5', .5), ('F5', .5),       # 5
         ('E5', .5), ('G5', .5), ('C6', 1), ('A5', 2),                                           # 6
         ('Bb5', 1), ('A5', .5), ('G5', .5), ('F5', 1), ('D5', 1),                               # 7
         ('G5', 1.5), ('F5', .5), ('E5', 1), ('C5', 1)]                                          # 8 (open, leads on)
A2_END = [('Bb5', 1), ('A5', .5), ('G5', .5), ('A5', 1), ('C6', 1),                              # 7'
          ('G5', 1), ('E5', 1), ('F5', 2)]                                                       # 8' (home)
B_MEL = [('D5', 1.5), ('C5', .5), ('D5', 1), ('F5', 1),
         ('E5', 3), ('C5', 1),
         ('D5', 1.5), ('C5', .5), ('Bb4', 1), ('D5', 1),
         ('C5', 3), ('A4', 1),
         ('D5', 1.5), ('C5', .5), ('D5', 1), ('F5', 1),
         ('E5', 2), ('F#5', 1), ('A5', 1),
         ('G5', 1.5), ('F5', .5), ('D5', 1), ('Bb4', 1),
         ('C5', 3), ('r', 1)]
# the marimba "doorbell" call in C (a little 2-beat figure, answered)
CALL = [('F5', .5), ('A5', .5), ('C6', .5), ('A5', .5)]


def chords(bars):
    for b, bar in enumerate(bars):
        t = 0
        for c, d in bar:
            yield b * 4 + t, d, c
            t += d


def epiano(E, t0, bars, v=70, style='bounce'):
    for t, d, c in chords(bars):
        notes = CH[c][0]
        tb = t0 + t
        if style == 'bounce':          # stab on the beat, held chord on the "and" of 2 (or 1 for half-bar chords)
            hits = [(0, .5, v + 6), (1.5, 1.5 if d == 4 else .5, v), (3.5, .5, v - 8)] if d == 4 else [(0, .5, v + 4), (1, 1, v)]
        else:                          # 'hold' for the breather
            hits = [(0, d, v)]
        for off, ln, vv in hits:
            if off < d:
                E['epiano'].append(ev(tb + off, ln, notes, vv))


def bassline(E, t0, bars, v=96):
    allc = list(chords(bars))
    for i, (t, d, c) in enumerate(allc):
        r = N(CH[c][1])
        nxt = N(CH[allc[(i + 1) % len(allc)][2]][1])
        tb = t0 + t
        if d == 4:
            pat = [(0, 1, r), (1.5, .5, r + 12), (2, 1, r + 7), (3, .5, r + 12), (3.5, .5, nxt + (1 if nxt < r else -1))]
        else:
            pat = [(0, 1, r), (1, .5, r + 7), (1.5, .5, nxt + (1 if nxt < r else -1))]
        for off, ln, n in pat:
            E['bass'].append(ev(tb + off, ln * 0.9, [n], v - (6 if off % 1 else 0)))


def drums(E, t0, bars, fill=False, light=False):
    for b in range(bars):
        tb = t0 + b * 4
        E['drums'] += [ev(tb, .25, KICK, 104), ev(tb + 1.5, .25, KICK, 84), ev(tb + 2, .25, KICK, 98)]
        if b % 2 == 1:
            E['drums'].append(ev(tb + 3.5, .25, KICK, 80))
        if not light:
            E['drums'] += [ev(tb + 1, .25, SNARE, 100), ev(tb + 3, .25, SNARE, 104),
                           ev(tb + 1.75, .25, SNARE, 40), ev(tb + 2.75, .25, SNARE, 34)]
        else:
            E['drums'] += [ev(tb + 1, .25, SNARE, 70), ev(tb + 3, .25, SNARE, 74)]
        for i in range(8):
            n = OPHAT if (i == 7 and b % 2 == 1) else CLHAT
            E['drums'].append(ev(tb + i * .5, .25, n, (78 if i % 2 == 0 else 58)))
        for i in range(16):
            E['drums'].append(ev(tb + i * .25, .25, SHAKER, 44 if i % 2 else 52))
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


# ----------------------------------------------------------------------- sections
def intro(E, t0):
    E['epiano'] += [ev(t0, 4, CH['Fmaj9'][0], 66), ev(t0 + 4, 2, CH['C7sus4'][0], 64), ev(t0 + 6, 2, CH['C7'][0], 64)]
    E['bass'] += [ev(t0, 3.6, [N('F2')], 80), ev(t0 + 4, 3.6, [N('C2')], 80)]
    E['marimba'] += [ev(t0 + 1 + i * .5, .5, [N(n)], 60) for i, n in enumerate(['C5', 'F5', 'A5', 'C6'])]
    for i, vv in enumerate((40, 50, 60, 70, 80, 90)):
        E['drums'].append(ev(t0 + 6.5 + i * .25, .25, SNARE, vv))
    E['drums'].append(ev(t0 + 7.5, .25, KICK, 90))


def sec_a(E, t0, second=False):
    epiano(E, t0, A_BARS)
    bassline(E, t0, A_BARS)
    drums(E, t0, 8, fill=True)
    mel = A_MEL[:-9] + A2_END if second else A_MEL
    melody(E, t0, mel, 'lead', 98 if second else 94)
    if second:
        melody(E, t0, mel, 'bell', 62, octave=1)          # a soft bell an octave up the second time
        E['pad'] += [ev(t0 + b * 4, 4, CH[c][0][1:], 50) for b, c in enumerate(['Fmaj9', 'Dm9', 'Bbmaj7', 'C7', 'Fmaj9', 'Am7', 'Bbmaj7', 'C7'])]
    else:   # marimba answers in the gaps at the end of each 2-bar phrase
        for b in (1, 3, 5):
            E['marimba'] += [ev(t0 + b * 4 + 2.5 + i * .5, .5, [N(n)], 58) for i, n in enumerate(['A4', 'C5', 'D5'])]


def sec_b(E, t0):
    epiano(E, t0, B_BARS, 64)
    bassline(E, t0, B_BARS, 90)
    drums(E, t0, 8, fill=True)
    melody(E, t0, B_MEL, 'lead', 90)
    melody(E, t0, B_MEL, 'marimba', 52, octave=-1)       # marimba doubles the lyrical B line an octave down
    for t, d, c in chords(B_BARS):
        E['pad'].append(ev(t0 + t, d, CH[c][0][1:], 58))


def sec_c(E, t0):     # the breather: no lead, the doorbell call, drums lighter
    epiano(E, t0, C_BARS, 62, 'bounce')
    bassline(E, t0, C_BARS, 86)
    drums(E, t0, 8, light=True, fill=True)
    for b in (0, 2, 4):
        melody(E, t0 + b * 4 + 1, CALL, 'marimba', 66)
        melody(E, t0 + b * 4 + 5, [('G5', .5), ('E5', .5), ('C5', 1)], 'marimba', 58)
    melody(E, t0 + 24, [('A5', 1), ('G5', 1), ('E5', 1), ('C5', 1), ('D5', .5), ('E5', .5), ('G5', 1), ('C5', 2)], 'bell', 60)
    for t, d, c in chords(C_BARS):
        E['pad'].append(ev(t0 + t, d, CH[c][0][1:], 62))


SECTIONS = [('A - Theme', 0, lambda E, t: sec_a(E, t)), ('A2 - Theme + bell', 32, lambda E, t: sec_a(E, t, True)),
            ('B - Lyrical', 64, sec_b), ('C - Breather', 96, sec_c)]


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
    if abs(frac - 0.75) < 1e-6:                       # 16ths after the swung 8th move with it
        return t - 0.75 + SWING + (1 - SWING) / 2
    if abs(frac - 0.25) < 1e-6:
        return t - 0.25 + SWING / 2
    return t
