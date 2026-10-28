"""Hollow Husk - Random Encounter (placeholder) -- the composition as note events.

All timing in beats (quarter notes). 160 BPM, 4/4, key D minor (harmonic minor
with phrygian Eb colour). Intro = 4 bars (beats 0-16), loop = 40 bars (160 beats).
"""

BPM = 160
INTRO_BEATS = 16
LOOP_BEATS = 160

_NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def N(s):
    s = s.strip()
    p = _NAMES[s[0]]
    i = 1
    while s[i] in '#b':
        p += 1 if s[i] == '#' else -1
        i += 1
    return p + 12 * (int(s[i:]) + 1)


def ev(t, d, notes, v=100, **kw):
    if isinstance(notes, (int, str)):
        notes = [notes]
    notes = [N(n) if isinstance(n, str) else n for n in notes]
    e = {'t': t, 'd': d, 'n': notes, 'v': v}
    e.update(kw)
    return e


def seq(t0, items, v=100):
    out, t = [], t0
    for name, d in items:
        if name != 'r':
            out.append(ev(t, d, name, v))
        t += d
    return out


MOTIF_RHY = [1.5, .5, 1, .5, .5]   # the "Husk" motif: long-short-step-turn


def motif(t0, notes, v=100, scale=1.0):
    """The one melodic cell the whole track is built from."""
    return seq(t0, [(n, d * scale) for n, d in zip(notes, MOTIF_RHY)], v)


def tree(kind, t, d, v=100, note=38):
    return ev(t, d, [note], v, kind=kind)


# chord tables -------------------------------------------------------------
GTR_ROOT = {'Dm': 38, 'Bb': 34, 'Gm': 43, 'A': 45, 'C': 36, 'Eb': 39, 'F': 41}
ORGAN = {'Dm': ['D4', 'F4', 'A4'], 'Bb': ['D4', 'F4', 'Bb4'], 'Gm': ['D4', 'G4', 'Bb4'],
         'A': ['C#4', 'E4', 'A4'], 'C': ['E4', 'G4', 'C5'], 'Eb': ['Eb4', 'G4', 'Bb4']}
CHOIR = {'Dm': ['D4', 'A4', 'F5'], 'Bb': ['D4', 'Bb4', 'F5'], 'Gm': ['D4', 'Bb4', 'G5'],
         'A': ['E4', 'A4', 'C#5'], 'C': ['E4', 'C5', 'G5'], 'Eb': ['Eb4', 'Bb4', 'G5']}
ARP = {'Dm': ['D5', 'F5', 'A5', 'E6'], 'Bb': ['D5', 'F5', 'Bb5', 'C6'],
       'Gm': ['D5', 'G5', 'Bb5', 'A5'], 'A': ['C#5', 'E5', 'A5', 'Bb5'],
       'C': ['C5', 'E5', 'G5', 'D6'], 'Eb': ['Eb5', 'G5', 'Bb5', 'F6']}
ORCH = {'Dm': ['D3', 'A3', 'D4', 'F4'], 'Eb': ['Eb3', 'Bb3', 'Eb4', 'G4'],
        'C': ['C3', 'G3', 'C4', 'E4'], 'Bb': ['Bb2', 'F3', 'Bb3', 'D4'],
        'A': ['A2', 'E3', 'A3', 'C#4']}


def sub_of(root):
    return 26 + ((root - 26) % 12)


# drum note numbers (GM)
KICK, SNARE, CLHAT, OPHAT, CRASH, RIDE, LTOM, MTOM, HTOM, CLAP = 36, 38, 42, 46, 49, 51, 45, 47, 50, 39

PARTS = ['lead', 'lead2', 'tree', 'guitar', 'bass', 'organ', 'arp', 'orch', 'choir', 'strings', 'sub', 'drums']


def new():
    return {p: [] for p in PARTS}


def arp_bar(E, t, chord, v=70, pat=(0, 1, 2, 3, 2, 1, 3, 1)):
    tones = ARP[chord]
    for i in range(16):
        E['arp'].append(ev(t + i * 0.25, 0.25, tones[pat[i % len(pat)]], v - (8 if i % 2 else 0)))


RIFF = [0, 0, 3, 0, 1, 0, 0, -2, 0, 0, 3, 0, 5, 3, 1, -2]
RIFF_TURN = [0, 0, 3, 0, 1, 0, 0, -2, 7, 7, 6, 6, 5, 5, 1, 1]


def riff(E, t, pattern, gv=104, bass=True):
    for i, o in enumerate(pattern):
        r = 38 + o
        mute = (o == 0)
        E['guitar'].append(ev(t + i * 0.5, 0.5, [r], gv - (10 if mute else 0), mute=mute))
        if bass:
            E['bass'].append(ev(t + i * 0.5, 0.45, [r], 96 if not mute else 84, pop=(not mute and o > 2)))
            E['sub'].append(ev(t + i * 0.5, 0.5, [sub_of(r)], 90))


# --------------------------------------------------------------------------
def intro(E, t0):
    hits = [(0, 'Dm', 3.5), (4, 'Eb', 3.5), (8, 'Dm', 2.5), (10.5, 'C', 1.5), (12, 'Bb', 2), (14, 'A', 2)]
    for t, c, d in hits:
        E['orch'].append(ev(t0 + t, 1.0, ORCH[c], 118))
        E['guitar'].append(ev(t0 + t, d, [GTR_ROOT[c]], 112, mute=False))
        E['bass'].append(ev(t0 + t, d, [GTR_ROOT[c]], 100))
        E['drums'].append(ev(t0 + t, 0.25, KICK, 120))
        E['drums'].append(ev(t0 + t, 1, CRASH, 110))
        E['sub'].append(ev(t0 + t, d, [sub_of(GTR_ROOT[c])], 100))
    for b, c in enumerate(['Dm', 'Eb', 'Dm']):
        arp_bar(E, t0 + b * 4, c, 62)
    E['tree'] += [tree('drone', t0, 16, 90, 38), tree('groan', t0, 8, 110, 26), tree('wind', t0, 14, 90),
                  tree('creak', t0 + 1.5, 2.5, 110), tree('creak', t0 + 9, 1.5, 90)]
    E['choir'] += [ev(t0, 8, ['D4', 'A4'], 70), ev(t0 + 8, 4, ['D4', 'A4'], 76), ev(t0 + 12, 4, ['C#4', 'E4', 'A4'], 90)]
    # bar 4 fill: snare 16ths 12-14 crescendo, toms 14-16
    for i in range(8):
        E['drums'].append(ev(t0 + 12 + i * 0.25, 0.25, SNARE, 70 + i * 6))
    for i, n in enumerate([HTOM, HTOM, MTOM, MTOM, LTOM, LTOM, KICK, SNARE]):
        E['drums'].append(ev(t0 + 14 + i * 0.25, 0.25, n, 110))


def section_a(E, t0):
    for k in range(4):
        riff(E, t0 + k * 8, RIFF if k < 3 else RIFF_TURN)
    org = [('Dm', 8), ('Dm', 8), ('Eb', 8), ('A', 8)]
    t = t0
    for c, d in org:
        E['organ'].append(ev(t, d, ORGAN[c], 70))
        t += d
    E['lead'] += motif(t0 + 16, ['Eb5', 'F5', 'G5', 'F5', 'Eb5'], 96) + seq(t0 + 20, [('D5', 4)], 92)
    E['lead'] += motif(t0 + 24, ['C#5', 'D5', 'E5', 'D5', 'C#5'], 96) + seq(t0 + 28, [('A4', 3)], 92)
    for k in range(4):            # hollow knocks under the backbeat
        for beat in (1, 3, 5, 7):
            E['tree'].append(tree('knock', t0 + k * 8 + beat, .25, 90))
    for b, c in enumerate(['Dm'] * 4 + ['Eb'] * 2 + ['A'] * 2):
        arp_bar(E, t0 + b * 4, c, 54)
    # drums
    for k in range(4):
        tb = t0 + k * 8
        pat = RIFF if k < 3 else RIFF_TURN
        for i, o in enumerate(pat):
            if o == 0 or i % 4 == 0:
                E['drums'].append(ev(tb + i * 0.5, 0.25, KICK, 112))
        for beat in range(8):
            if beat % 2 == 1:
                E['drums'].append(ev(tb + beat, 0.25, SNARE, 112))
            E['drums'].append(ev(tb + beat, 0.25, CLHAT, 84))
            E['drums'].append(ev(tb + beat + 0.5, 0.25, CLHAT, 66))
    E['drums'].append(ev(t0, 1, CRASH, 118))
    E['drums'].append(ev(t0 + 16, 1, CRASH, 108))
    # double kick into B
    for i in range(16):
        E['drums'].append(ev(t0 + 28 + i * 0.25, 0.25, KICK, 100))
    for i in range(4):
        E['drums'].append(ev(t0 + 31 + i * 0.25, 0.25, SNARE, 90 + i * 8))


B_CHORDS = ['Dm', 'Bb', 'Gm', 'A', 'Dm', 'Bb', 'C', 'A', 'Dm', 'Bb', 'Gm', 'A', 'Bb', 'C', 'A', 'A']
B1_OLD = [('A4', 1.5), ('D5', .5), ('F5', 1), ('E5', 1),
          ('D5', 1.5), ('C5', .5), ('Bb4', 1), ('A4', 1),
          ('G4', 1), ('Bb4', 1), ('D5', 1.5), ('Eb5', .5),
          ('C#5', 3), ('E5', 1),
          ('F5', 1.5), ('E5', .5), ('D5', 1), ('A4', 1),
          ('Bb4', 1), ('D5', 1), ('F5', 1), ('G5', 1),
          ('E5', 1.5), ('F5', .5), ('G5', 1), ('E5', 1),
          ('A5', 2), ('G5', .5), ('F5', .5), ('E5', .5), ('C#5', .5)]
B2_OLD = [('A4', 1.5), ('D5', .5), ('F5', 1), ('A5', 1),
          ('Bb5', 1.5), ('A5', .5), ('F5', 1), ('D5', 1),
          ('G5', 1), ('F5', .5), ('Eb5', .5), ('D5', 1), ('Bb4', 1),
          ('C#5', 1.5), ('D5', .5), ('E5', 2),
          ('F5', 1.5), ('G5', .5), ('A5', 1), ('Bb5', 1),
          ('C6', 2), ('Bb5', .5), ('A5', .5), ('G5', 1),
          ('A5', 1.5), ('G5', .5), ('F5', 1), ('E5', 1),
          ('C#5', 1), ('E5', 1), ('A5', 2)]


# B1: the rising arch (base, step up, two up, back). B2: the arch flipped (the tree wilting),
# then it climbs again and ends on the A-section phrase.
B_PHRASES = [
    (['D5', 'E5', 'F5', 'E5', 'D5'], [('D5', 3), ('C5', .5), ('Bb4', .5)]),     # Dm | Bb
    (['Bb4', 'C5', 'D5', 'C5', 'Bb4'], [('A4', 3), ('C#5', .5), ('E5', .5)]),   # Gm | A
    (['F5', 'G5', 'A5', 'G5', 'F5'], [('F5', 3), ('E5', .5), ('D5', .5)]),      # Dm | Bb
    (['E5', 'F5', 'G5', 'F5', 'E5'], [('C#5', 3), ('D5', .5), ('E5', .5)]),     # C  | A
    (['A5', 'G5', 'F5', 'G5', 'A5'], [('Bb5', 3), ('A5', .5), ('G5', .5)]),     # Dm | Bb
    (['G5', 'F5', 'Eb5', 'F5', 'G5'], [('E5', 3), ('C#5', .5), ('E5', .5)]),    # Gm | A
    (['F5', 'G5', 'A5', 'G5', 'F5'], [('G5', 3), ('A5', .5), ('Bb5', .5)]),     # Bb | C
    (['C#5', 'D5', 'E5', 'D5', 'C#5'], [('A4', 4)]),                            # A  | A
]


def section_b(E, t0):
    for i, (call, answer) in enumerate(B_PHRASES):
        t = t0 + i * 8
        v = 100 if i < 4 else 110
        notes = motif(t, call, v) + seq(t + 4, answer, v - 4)
        E['lead'] += notes
        if i >= 4:   # B2: distorted guitar doubles an octave below
            E['lead2'] += [dict(e, n=[m - 12 for m in e['n']]) for e in notes]
        if i < 4:
            E['tree'] += [tree('knock', t + b, .25, 80) for b in (1, 3, 5, 7)]
    E['tree'].append(tree('creak', t0 + 30.5, 1.5, 90))
    for b, c in enumerate(B_CHORDS):
        tb = t0 + b * 4
        r = GTR_ROOT[c]
        second = b >= 8
        # guitar: gallop in B1, straight 16th chug in B2 with open accents
        if not second:
            for beat in range(4):
                for off, d in [(0, .5), (.5, .25), (.75, .25)]:
                    acc = (beat == 0 and off == 0)
                    E['guitar'].append(ev(tb + beat + off, d, [r], 108 if acc else 94, mute=not acc))
        else:
            for i in range(16):
                acc = i in (0, 6, 12)
                E['guitar'].append(ev(tb + i * .25, .25, [r], 110 if acc else 92, mute=not acc))
        # bass with slap pops on the offbeats
        for i in range(8):
            pop = i in (3, 7)
            n = r + 12 if pop else r
            E['bass'].append(ev(tb + i * .5, .45, [n], 100 if pop else 90, pop=pop))
        E['sub'].append(ev(tb, 4, [sub_of(r)], 92))
        # organ stabs
        E['organ'].append(ev(tb, 1.5, ORGAN[c], 82))
        E['organ'].append(ev(tb + 2.5, 1.5, ORGAN[c], 78))
        arp_bar(E, tb, c, 46 if not second else 40)
        if second:
            E['strings'].append(ev(tb, 4, CHOIR[c], 74))
        # drums
        if not second or b < 12:
            for k in (0, 1.5, 2, 3.5):
                E['drums'].append(ev(tb + k, .25, KICK, 110))
        else:
            for i in range(16):
                E['drums'].append(ev(tb + i * .25, .25, KICK, 96 + (8 if i % 4 == 0 else 0)))
        for k in (1, 3):
            E['drums'].append(ev(tb + k, .25, SNARE, 114))
        if not second:
            for i in range(8):
                n = OPHAT if i == 7 else CLHAT
                E['drums'].append(ev(tb + i * .5, .25, n, 82 if i % 2 == 0 else 64))
        else:
            for i in range(4):
                E['drums'].append(ev(tb + i, .5, RIDE, 92))
                E['drums'].append(ev(tb + i + .5, .5, RIDE, 70))
        if b in (0, 4, 8, 12):
            E['drums'].append(ev(tb, 1, CRASH, 114))
    for i in range(4):
        E['drums'].append(ev(t0 + 63 + i * .25, .25, SNARE, 88 + i * 9))


C_CHORDS = ['Bb', 'C', 'Dm', 'Dm', 'Gm', 'Eb', 'A', 'A']
C_MEL = [('D5', 2), ('F5', 2), ('E5', 3), ('G5', 1), ('F5', 2), ('E5', 1), ('D5', 1), ('A4', 4),
         ('Bb4', 2), ('D5', 2), ('Eb5', 2), ('G5', 1.5), ('F5', .5), ('E5', 2), ('C#5', 2), ('A4', 2), ('r', 2)]


def section_c(E, t0):
    E['lead'] += motif(t0, ['D5', 'E5', 'F5', 'E5', 'D5'], 96, scale=2) + seq(t0 + 8, [('A4', 6), ('Bb4', 1), ('C5', 1)], 92)
    E['lead'] += motif(t0 + 16, ['Bb4', 'C5', 'D5', 'C5', 'Bb4'], 100, scale=2) + seq(t0 + 24, [('A4', 6)], 96)
    E['lead2'] += [dict(e, n=[e['n'][0] - 12], v=e['v'] - 10) for e in E['lead'][-6:]]
    E['tree'] += [tree('wind', t0, 32, 100), tree('creak', t0 + 0.5, 3, 100), tree('groan', t0 + 8, 8, 90, 26),
                  tree('creak', t0 + 16.5, 2.5, 96), tree('groan', t0 + 20, 8, 100, 27)]
    for b, c in enumerate(C_CHORDS):
        tb = t0 + b * 4
        r = GTR_ROOT[c]
        if b < 6:
            E['guitar'].append(ev(tb, 4, [r], 100, mute=False))
            E['bass'].append(ev(tb, 3.8, [r], 92))
        else:
            for i in range(8):
                E['guitar'].append(ev(tb + i * .5, .5, [r], 90 + i * 2, mute=(i % 4 != 0)))
                E['bass'].append(ev(tb + i * .5, .45, [r], 92))
        E['sub'].append(ev(tb, 4, [sub_of(r)], 96))
        E['choir'].append(ev(tb, 4, CHOIR[c], 100))
        E['strings'].append(ev(tb, 4, ORGAN[c], 84))
        arp_bar(E, tb, c, 78, pat=(0, 2, 1, 3, 0, 2, 3, 1))
        # half-time drums
        if b < 7:
            E['drums'].append(ev(tb, .25, KICK, 116))
            E['drums'].append(ev(tb + 2.5, .25, KICK, 100))
            E['drums'].append(ev(tb + 2, .25, SNARE, 118))
            for i in range(4):
                E['drums'].append(ev(tb + i, .25, CLHAT, 74))
        if b in (0, 4):
            E['drums'].append(ev(tb, 1, CRASH, 112))
    E['orch'] += [ev(t0 + 20, 1, ORCH['Eb'], 110), ev(t0 + 24, 1, ORCH['A'], 118)]
    # bar 8: snare roll crescendo + toms
    tb = t0 + 28
    for i in range(12):
        E['drums'].append(ev(tb + i * .25, .25, SNARE, 60 + i * 5))
    for i, n in enumerate([HTOM, MTOM, LTOM, KICK]):
        E['drums'].append(ev(tb + 3 + i * .25, .25, n, 116))


def section_d(E, t0):
    riff(E, t0, RIFF)
    riff(E, t0 + 8, RIFF)
    for k in (0, 8):
        E['orch'] += [ev(t0 + k + 1, .5, ORCH['Dm'], 104), ev(t0 + k + 6, .5, ORCH['Eb'], 108)]
    d_mel = (motif(t0, ['D5', 'E5', 'F5', 'E5', 'D5'], 108) + seq(t0 + 4, [('D5', 3), ('C5', .5), ('Bb4', .5)], 104)
             + motif(t0 + 8, ['Eb5', 'F5', 'G5', 'F5', 'Eb5'], 110) + seq(t0 + 12, [('D5', 4)], 104)
             + motif(t0 + 16, ['F5', 'G5', 'A5', 'G5', 'F5'], 110) + seq(t0 + 20, [('F5', 1.5), ('E5', 2.5)], 104))
    E['lead'] += d_mel
    E['lead2'] += [dict(e, n=[e['n'][0] - 12]) for e in d_mel]
    E['tree'] += [tree('groan', t0 + 16, 8, 80, 26), tree('creak', t0 + 24, 2, 80)]
    E['choir'] += [ev(t0, 8, CHOIR['Dm'], 86), ev(t0 + 8, 8, CHOIR['Eb'], 92)]
    for k in range(2):
        tb = t0 + k * 8
        for i, o in enumerate(RIFF):
            if o == 0 or i % 4 == 0:
                E['drums'].append(ev(tb + i * .5, .25, KICK, 112))
        for beat in range(8):
            if beat % 2:
                E['drums'].append(ev(tb + beat, .25, SNARE, 114))
            E['drums'].append(ev(tb + beat, .5, RIDE, 90))
            E['drums'].append(ev(tb + beat + .5, .5, RIDE, 68))
    E['drums'].append(ev(t0, 1, CRASH, 118))
    E['drums'].append(ev(t0 + 8, 1, CRASH, 110))
    # bars 5-6: stop-time hits
    tb = t0 + 16
    hits = [(0, 'Bb', 1.5), (1.5, 'Bb', 1.5), (3, 'C', 1), (4, 'Bb', 1.5), (5.5, 'C', 1.5), (7, 'A', 1)]
    for t, c, d in hits:
        r = GTR_ROOT[c]
        E['orch'].append(ev(tb + t, .75, ORCH[c], 116))
        E['guitar'].append(ev(tb + t, d * .9, [r], 112, mute=False))
        E['bass'].append(ev(tb + t, d * .9, [r], 100))
        E['sub'].append(ev(tb + t, d, [sub_of(r)], 96))
        E['drums'].append(ev(tb + t, .25, KICK, 120))
        E['drums'].append(ev(tb + t, 1, CRASH, 104))
    for i in range(16):
        E['drums'].append(ev(tb + i * .5, .25, CLHAT, 60))
    # bars 7-8: riff turn + fill back to the loop start
    tb = t0 + 24
    riff(E, tb, RIFF_TURN)
    for i in range(16):
        E['drums'].append(ev(tb + i * .25, .25, KICK, 100))
    for beat in (1, 3):
        E['drums'].append(ev(tb + beat, .25, SNARE, 112))
    for i, n in enumerate([SNARE, SNARE, HTOM, HTOM, SNARE, SNARE, MTOM, MTOM,
                           SNARE, SNARE, LTOM, LTOM, SNARE, SNARE, SNARE, SNARE]):
        E['drums'].append(ev(tb + 4 + i * .25, .25, n, 86 + i * 2))
    E['orch'].append(ev(tb + 7.5, .5, ORCH['A'], 112))


SECTIONS = [('A - Riff', 0, section_a), ('B - Theme', 32, section_b),
            ('C - Half-time bridge', 96, section_c), ('D - Breakdown', 128, section_d)]


def build(loops=1, with_intro=True):
    E = new()
    markers = []
    t = 0
    if with_intro:
        intro(E, 0)
        markers.append((0, 'Intro'))
        t = INTRO_BEATS
    for n in range(loops):
        for name, off, fn in SECTIONS:
            fn(E, t + off)
            markers.append((t + off, ('LOOP START / ' if off == 0 and n == 0 else '') + name))
        t += LOOP_BEATS
    markers.append((t, 'LOOP END'))
    return E, markers, t
