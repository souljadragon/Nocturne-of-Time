"""Hollowed Husk - dungeon exploration theme (placeholder). Original composition.

110 BPM, 4/4, D minor with an unsettled major third (F#) and phrygian Eb.
Intro 2 bars (once) + loop 48 bars. Light 16th swing on everything rhythmic.
"""
import sys
sys.path.insert(0, '/home/claude/hollow_husk')
from compose import N, ev, seq, KICK, SNARE, CLHAT, OPHAT, CRASH, RIDE, LTOM, MTOM, HTOM, CLAP

BPM = 110
INTRO_BEATS = 8
LOOP_BEATS = 192
SWING = 0.58          # second 16th of each 8th lands at 58% instead of 50%
RIM, SHAKER = 37, 70

PARTS = ['organ', 'piano', 'bass', 'wbass', 'flute', 'arp', 'pad', 'whisper', 'tree', 'drums']

CH = {  # organ voicing, piano voicing, bass root
    'Dm9':   (['D3', 'F3', 'A3', 'C4', 'E4'], ['F4', 'A4', 'C5', 'E5'], 'D2'),
    'DmM7':  (['D3', 'F3', 'A3', 'C#4', 'E4'], ['F4', 'A4', 'C#5', 'E5'], 'D2'),
    'Eb':    (['Eb3', 'G3', 'Bb3', 'D4', 'A4'], ['G4', 'Bb4', 'D5', 'A5'], 'Eb2'),
    'Bb':    (['Bb2', 'F3', 'A3', 'D4'], ['F4', 'A4', 'D5'], 'Bb1'),
    'Gm6':   (['G2', 'D3', 'E3', 'Bb3'], ['Bb4', 'D5', 'E5', 'G5'], 'G1'),
    'Gm9':   (['G2', 'Bb3', 'D4', 'F4', 'A4'], ['Bb4', 'D5', 'F5', 'A5'], 'G1'),
    'A7b9':  (['A2', 'G3', 'C#4', 'Bb4'], ['C#5', 'G5', 'Bb5'], 'A1'),
    'Bbm6':  (['Bb2', 'Db4', 'F4', 'G4'], ['Db5', 'F5', 'G5'], 'Bb1'),
}
GROOVE = ['Dm9', 'Dm9', 'Eb', 'Eb', 'Bb', 'Gm6', 'A7b9', 'A7b9']
BREAK = ['Gm9', 'Gm9', 'Eb', 'Eb', 'Bbm6', 'Bbm6', 'A7b9', 'A7b9']
WIND = ['Dm9', 'DmM7', 'Dm9', 'DmM7', 'Bbm6', 'Bbm6', 'A7b9', 'A7b9']

# the Husk motif, shared with the battle theme (rhythm 1.5 .5 1 .5 .5)
MOTIF_RHY = [1.5, .5, 1, .5, .5]
HUSK = [  # call (motif) + answer (held), per 2 bars of GROOVE
    (['D5', 'E5', 'F5', 'E5', 'D5'], [('D5', 4)]),
    (['Eb5', 'F5', 'G5', 'F5', 'Eb5'], [('D5', 4)]),
    (['F5', 'G5', 'A5', 'G5', 'F5'], [('E5', 4)]),
    (['C#5', 'D5', 'E5', 'D5', 'C#5'], [('A4', 4)]),
]

# piano bounce: 16th positions in a bar (0-15) and lengths in 16ths
BOUNCE = [(0, 2), (3, 2), (6, 1), (8, 2), (11, 1), (14, 2)]
# round bass: (16th pos, length, semitone offset)
RBASS = [(0, 3, 0), (6, 2, 12), (10, 2, 7), (14, 2, 0)]
# the weird bass: wandering, octave leaps, chromatic glides (glide=True slides into the note)
WBASS = [(0, 3, 0, False), (3, 2, 12, True), (5, 1, 7, False), (6, 2, 0, False), (8, 2, 10, True),
         (10, 1, 12, False), (11, 2, 3, True), (13, 3, -1, True)]


def bar_chords(E, t0, prog, organ_v=70, organ_style='hold'):
    for b, c in enumerate(prog):
        tb = t0 + b * 4
        org = CH[c][0]
        if organ_style == 'hold':
            E['organ'].append(ev(tb, 4, org, organ_v))
        elif organ_style == 'offbeat':     # stabs on the "and"s, blending with the piano
            for k in (0.5, 1.5, 2.5, 3.5):
                E['organ'].append(ev(tb + k, 0.35, org[1:], organ_v))
        elif organ_style == 'swell':
            E['organ'].append(ev(tb, 4, org, organ_v, swell=True))


def piano_bounce(E, t0, prog, v=74):
    for b, c in enumerate(prog):
        tb = t0 + b * 4
        for pos, ln in BOUNCE:
            acc = pos in (0, 8)
            E['piano'].append(ev(tb + pos * .25, ln * .25, CH[c][1], v + (8 if acc else 0)))


def round_bass(E, t0, prog, v=92):
    for b, c in enumerate(prog):
        tb = t0 + b * 4
        r = N(CH[c][2])
        for pos, ln, off in RBASS:
            E['bass'].append(ev(tb + pos * .25, ln * .25, [r + off], v))


def weird_bass(E, t0, prog, v=100):
    for b, c in enumerate(prog):
        tb = t0 + b * 4
        r = N(CH[c][2]) + 12
        for pos, ln, off, gl in WBASS:
            E['wbass'].append(ev(tb + pos * .25, ln * .25, [r + off], v, glide=gl))


def drums(E, t0, bars, level='light', clap=False):
    for b in range(bars):
        tb = t0 + b * 4
        if level == 'heartbeat':
            E['drums'] += [ev(tb, .25, KICK, 70), ev(tb + .75, .25, KICK, 54)]
            continue
        E['drums'] += [ev(tb, .25, KICK, 96), ev(tb + 1.75, .25, KICK, 78), ev(tb + 2.5, .25, KICK, 88)]
        E['drums'] += [ev(tb + 1, .25, CLAP if clap else RIM, 84), ev(tb + 3, .25, CLAP if clap else RIM, 88)]
        if level == 'full':
            E['drums'] += [ev(tb + 3.75, .25, RIM, 50), ev(tb + 2.25, .25, RIM, 40)]
        for i in range(16):
            E['drums'].append(ev(tb + i * .25, .25, SHAKER, (66 if i % 2 == 0 else 48) + (6 if i % 4 == 2 else 0)))
        if level == 'full':
            for i in (2, 6, 10, 14):
                E['drums'].append(ev(tb + i * .25, .25, CLHAT, 50))


def whisper_phrase(E, t, syllables, v=80, step=.5, pan=0.0):
    """syllables: string of syllable codes; '-' = rest."""
    for i, s in enumerate(syllables.split()):
        if s != '-':
            E['whisper'].append(ev(t + i * step, step * 1.4, [60], v, syl=s, pan=pan))


def husk(E, t0, part='piano', v=84, octave=0):
    t = t0
    for call, ans in HUSK:
        for n, d in zip(call, MOTIF_RHY):
            E[part].append(ev(t, d * 0.95, [N(n) + 12 * octave], v))
            t += d
        for n, d in ans:
            E[part].append(ev(t, d * 0.95, [N(n) + 12 * octave], v - 6))
            t += d


def arp(E, t0, prog, v=50):
    pat = [0, 2, 1, 3]
    for b, c in enumerate(prog):
        tones = CH[c][1]
        for i in (0, 3, 6, 10, 13):           # sparse, syncopated, never a steady stream
            E['arp'].append(ev(t0 + b * 4 + i * .25, .25, N(tones[pat[i % 4] % len(tones)]) + 12, v))


def tree(kind, t, d, v=100, note=38):
    return ev(t, d, [note], v, kind=kind)


# ------------------------------------------------------------------- sections
def intro(E, t0):
    E['organ'].append(ev(t0, 8, CH['Dm9'][0], 64, swell=True))
    E['pad'].append(ev(t0, 8, ['D2', 'A2'], 70))
    E['tree'] += [tree('wind', t0, 8, 90), tree('creak', t0 + 1, 2.5, 100), tree('groan', t0 + 3, 5, 80, 26)]
    whisper_phrase(E, t0 + 4, 'ha - shi - - oo - - - - -', 72, pan=-0.4)


def sec_a(E, t0):     # bars 1-8: the groove sets in
    bar_chords(E, t0, GROOVE, 62)
    piano_bounce(E, t0, GROOVE, 70)
    round_bass(E, t0, GROOVE)
    drums(E, t0, 8, 'light')
    E['pad'].append(ev(t0, 32, ['D2', 'A2'], 54))
    whisper_phrase(E, t0 + 14, 'shi ha - ru - oo', 70, pan=0.5)
    whisper_phrase(E, t0 + 30, 'sa - sa - he', 66, pan=-0.5)
    E['tree'] += [tree('wind', t0, 32, 60), tree('knock', t0 + 15.5, .25, 70), tree('knock', t0 + 31.5, .25, 70)]


def sec_b(E, t0):     # bars 9-16: the Husk motif on piano, clap backbeat, wood arp
    bar_chords(E, t0, GROOVE, 58)
    piano_bounce(E, t0, GROOVE, 60)
    husk(E, t0, 'piano', 86, octave=0)
    round_bass(E, t0, GROOVE)
    drums(E, t0, 8, 'full', clap=True)
    arp(E, t0, GROOVE, 46)
    whisper_phrase(E, t0 + 22, 'ha - - shi - - oo -', 64, pan=0.3)


def sec_c(E, t0):     # bars 17-24: breakdown, the tree breathes
    bar_chords(E, t0, BREAK, 72, 'swell')
    drums(E, t0, 8, 'heartbeat')
    E['pad'].append(ev(t0, 32, ['G1', 'D2'], 70))
    for b, c in enumerate(BREAK):    # sparse, high piano: single chromatic drops
        tb = t0 + b * 4
        top = CH[c][1][-1]
        E['piano'] += [ev(tb + 1.5, 1, [N(top) + 12], 58), ev(tb + 2.75, 1.25, [N(top) + 11], 50)]
        E['bass'].append(ev(tb, 3.5, [N(CH[c][2])], 76))
    for i, (txt, p) in enumerate([('ha - shi - ru - - -', -0.6), ('oo - - he - sa - -', 0.6),
                                  ('shi shi - ha - - oo -', -0.3), ('ru - - - ha - - -', 0.4)]):
        whisper_phrase(E, t0 + i * 8 + 1, txt, 78, pan=p)
    E['tree'] += [tree('wind', t0, 32, 100), tree('creak', t0 + 2, 3, 104), tree('groan', t0 + 8, 8, 90, 26),
                  tree('creak', t0 + 18, 2.5, 96), tree('groan', t0 + 24, 8, 96, 27), tree('drone', t0, 32, 70, 38)]


def sec_d(E, t0):     # bars 25-32: the weird bass arrives
    bar_chords(E, t0, GROOVE, 60, 'offbeat')
    piano_bounce(E, t0, GROOVE, 72)
    weird_bass(E, t0, GROOVE)
    E['bass'] += [ev(t0 + b * 4, 4, [N(CH[c][2])], 70, sub=True) for b, c in enumerate(GROOVE)]
    drums(E, t0, 8, 'full', clap=True)
    whisper_phrase(E, t0 + 6, 'ha - shi - -', 62, step=.25, pan=-0.5)
    whisper_phrase(E, t0 + 22, 'oo - ru - -', 62, step=.25, pan=0.5)


def sec_e(E, t0):     # bars 33-40: Husk motif on the wooden flute over the weird bass
    bar_chords(E, t0, GROOVE, 54, 'hold')
    piano_bounce(E, t0, GROOVE, 66)
    weird_bass(E, t0, GROOVE, 94)
    E['bass'] += [ev(t0 + b * 4, 4, [N(CH[c][2])], 70, sub=True) for b, c in enumerate(GROOVE)]
    husk(E, t0, 'flute', 84, octave=-1)
    arp(E, t0, GROOVE, 44)
    drums(E, t0, 8, 'full', clap=True)


def sec_f(E, t0):     # bars 41-48: winding down, the major-seventh unease, back to the top
    bar_chords(E, t0, WIND, 64)
    piano_bounce(E, t0, WIND[:4], 62)
    for b, c in enumerate(WIND[4:]):
        tb = t0 + 16 + b * 4
        E['piano'] += [ev(tb, 1.5, CH[c][1], 64), ev(tb + 2.5, 1.5, CH[c][1], 56)]
    round_bass(E, t0, WIND, 86)
    drums(E, t0, 6, 'light')
    drums(E, t0 + 24, 2, 'heartbeat')
    E['pad'].append(ev(t0, 32, ['D2', 'A2'], 56))
    whisper_phrase(E, t0 + 10, 'shi - ha - - -', 66, pan=-0.4)
    whisper_phrase(E, t0 + 26, 'ha shi ru - oo - - - - -', 76, pan=0.4)
    E['tree'] += [tree('wind', t0 + 16, 16, 80), tree('creak', t0 + 28, 3, 100)]


SECTIONS = [('A - Groove', 0, sec_a), ('B - Husk motif (piano)', 32, sec_b), ('C - Breakdown', 64, sec_c),
            ('D - The weird bass', 96, sec_d), ('E - Husk motif (flute)', 128, sec_e), ('F - Wind-down', 160, sec_f)]


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
    """Delay every second 16th inside each 8th."""
    pair = t % 0.5
    if abs(pair - 0.25) < 1e-6:
        return t - 0.25 + 0.5 * SWING
    return t
