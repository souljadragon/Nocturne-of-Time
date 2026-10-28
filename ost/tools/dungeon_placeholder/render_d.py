"""Render Hollowed Husk (dungeon theme) placeholder."""
import sys
import numpy as np, scipy.signal as sg
sys.path.insert(0, '/home/claude/hollow_husk')
sys.path.insert(0, '/home/claude/mix')
import render as R
import compose_d as C
from compose import KICK, CLAP, CLHAT

R.BPM = C.BPM
R.SPB = R.SR * 60 / C.BPM
SR, SPB = R.SR, R.SPB
rng = np.random.default_rng(11)
mtof, lp, hp, bp, place, env, secs = R.mtof, R.lp, R.hp, R.bp, R.place, R.env, R.secs


def at(e):
    return C.swing(e['t']) * SPB


# ---------------------------------------------------------------- instruments
def organ(events, n):
    out = np.zeros(n)
    bars = [(0.5, .35), (1, 1), (1.5, .3), (2, .45), (4, .12)]
    for e in events:
        gate = secs(e['d']) * SR
        L = int(gate + 0.25 * SR)
        t = np.arange(L) / SR
        sig = np.zeros(L)
        for m in e['n']:
            f = mtof(m)
            for h, a in bars:
                sig += a * np.sin(2 * np.pi * f * h * t + rng.random() * 6)
        a = 1.6 if e.get('swell') else (0.004 if e['d'] < 1 else 0.06)
        sig *= env(L, a, 0.5, 0.8, 0.15, gate)
        place(out, at(e), sig * e['v'] / 127 / len(e['n']))
    t = np.arange(n) / SR
    rot = 0.8   # slow leslie
    left = out * (1 + 0.22 * np.sin(2 * np.pi * rot * t))
    right = out * (1 + 0.22 * np.sin(2 * np.pi * rot * t + 2.2))
    return np.tanh(np.stack([left, right]) * 1.2)


def piano(events, n):
    out = np.zeros(n)
    B = 0.0004
    for e in events:
        gate = secs(e['d']) * SR
        for m in e['n']:
            f = mtof(m)
            L = int(min(gate + 0.35 * SR, 4.0 * SR))
            t = np.arange(L) / SR
            sig = np.zeros(L)
            bright = e['v'] / 127
            kmax = int(min(12, 9000 / f))
            for k in range(1, kmax + 1):
                fk = k * f * np.sqrt(1 + B * k * k)
                amp = (1 / k ** 1.25) * (bright ** (0.3 * k))
                tau = 2.2 / k ** 0.7 * (220 / max(f, 110)) ** 0.3
                for c in (-0.8, 0.8):
                    sig += 0.5 * amp * np.sin(2 * np.pi * fk * 2 ** (c / 1200) * t + rng.random() * 6) * np.exp(-t / tau)
            rel = np.where(t < gate / SR, 1.0, np.exp(-(t - gate / SR) / 0.12))
            ham = np.zeros(L)
            ham[:400] = bp(rng.normal(size=400), 400, 3000) * np.exp(-np.arange(400) / 60) * 0.3
            place(out, at(e), (sig * rel + ham) * e['v'] / 127)
    return lp(out, 4500, 2)


def bass(events, n):
    out = np.zeros(n)
    for e in events:
        f = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        L = int(gate + 0.05 * SR)
        t = np.arange(L) / SR
        if e.get('sub'):
            sig = np.sin(2 * np.pi * f * t) * env(L, 0.02, 1, 1, 0.05, gate) * 0.8
        else:
            sig = (np.sin(2 * np.pi * f * t) + 0.35 * np.sin(4 * np.pi * f * t) + 0.12 * np.sin(6 * np.pi * f * t))
            sig *= env(L, 0.004, 0.35, 0.55, 0.04, gate)
        place(out, at(e), sig * e['v'] / 127)
    return np.tanh(out * 1.4)


def tv_bandpass(x, fc, q):
    """Time-varying resonant bandpass, coefficients updated every 64 samples."""
    y = np.zeros_like(x)
    zi = np.zeros(2)
    for i in range(0, len(x), 64):
        b, a = sg.iirpeak(min(max(fc[i], 60), SR * 0.45), q, fs=SR)
        y[i:i + 64], zi = sg.lfilter(b, a, x[i:i + 64], zi=zi)
    return y


def weird_bass(events, n):
    """Rubbery 'talking' bass: glides between notes, a vowel-like formant that opens on each note,
    a growl band around 1.2 kHz, a sine sub under it, and SFC-style grit."""
    out = np.zeros(n)
    sub = np.zeros(n)
    prev = None
    for e in events:
        f1 = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        L = int(gate + 0.04 * SR)
        t = np.arange(L) / SR
        f0 = prev if (e.get('glide') and prev) else f1
        f = f1 + (f0 - f1) * np.exp(-t / 0.045)
        f *= 1 + 0.006 * np.sin(2 * np.pi * 5.5 * t)
        ph = 2 * np.pi * np.cumsum(f) / SR
        osc = R.blep_saw(f, L) * 0.7 + R.blep_sq(f * 0.5, L) * 0.4 + 0.3 * np.sin(ph + 1.5 * np.sin(ph * 2))
        e_f = np.exp(-t / 0.09)
        form1 = tv_bandpass(osc, 380 + 520 * e_f + 120 * np.sin(2 * np.pi * 3 * t), 2.2)     # "ow" -> "aa"
        form2 = tv_bandpass(osc, 900 + 450 * e_f, 3.0)
        growl = bp(osc, 1050, 1700) * 0.5
        body = lp(osc, 220) * 0.6
        sig = (form1 * 1.2 + form2 * 0.7 + growl + body) * env(L, 0.003, 0.2, 0.75, 0.03, gate)
        place(out, at(e), sig * e['v'] / 127)
        prev = f1
    out = np.tanh(out * 2.0)
    q = 2 ** 9                    # light bit grit
    out = np.round(out * q) / q
    return out


def pad(events, n):
    out = np.zeros(n)
    for e in events:
        gate = secs(e['d']) * SR
        L = int(gate + 2 * SR)
        t = np.arange(L) / SR
        sig = np.zeros(L)
        for m in e['n']:
            for c in (-8, 0, 8):
                sig += R.blep_saw(mtof(m) * 2 ** (c / 1200) * (1 + 0.002 * np.sin(2 * np.pi * 0.2 * t + rng.random() * 6)), L, rng.random())
        sig *= env(L, 2.0, 2, 1, 1.5, gate)
        place(out, at(e), sig * e['v'] / 127)
    return lp(out, 650, 2)


VOWELS = {'a': (800, 1200, 2500), 'i': (300, 2300, 3000), 'u': (350, 700, 2400), 'e': (450, 1900, 2600)}
SYL = {'ha': ('h', 'a'), 'shi': ('sh', 'i'), 'ru': ('r', 'u'), 'oo': ('', 'u'), 'he': ('h', 'e'), 'sa': ('s', 'a')}


def whisper(events, n):
    """Breathy whispered syllables (formant-filtered noise), doubled for a ghostly cluster."""
    out = np.zeros((2, n))
    for e in events:
        cons, vow = SYL[e['syl']]
        dur = secs(e['d'])
        parts = []
        if cons == 'h':
            c = bp(rng.normal(size=int(0.06 * SR)), 900, 4000) * 0.5
        elif cons == 'sh':
            c = bp(rng.normal(size=int(0.13 * SR)), 1800, 5000)
        elif cons == 's':
            c = hp(rng.normal(size=int(0.12 * SR)), 4500)
        elif cons == 'r':
            c = bp(rng.normal(size=int(0.04 * SR)), 300, 1500) * (0.5 + 0.5 * np.sin(np.linspace(0, 3 * np.pi, int(0.04 * SR))))
        else:
            c = np.zeros(0)
        if len(c):
            c = c * np.sin(np.linspace(0, np.pi, len(c))) ** 0.7
        Lv = int(max(dur, 0.25) * SR)
        nz = rng.normal(size=Lv)
        v = sum(a * sg.lfilter(*sg.iirpeak(fr * (1 + rng.normal() * 0.03), 7, fs=SR), nz)
                for fr, a in zip(VOWELS[vow], (1.0, 0.7, 0.3)))
        v += 0.25 * bp(nz, 1500, 6000)
        tv = np.arange(Lv) / SR
        v *= np.minimum(1, tv / 0.04) * np.exp(-tv / (dur * 0.55))
        syl = np.concatenate([c, v])
        syl /= np.max(np.abs(syl)) + 1e-9
        for k, (dt, pn) in enumerate(((0, e.get('pan', 0)), (0.031, -e.get('pan', 0) * 0.7))):
            a = (pn + 1) * np.pi / 4
            s0 = at(e) + dt * SR
            g = e['v'] / 127 * (1 if k == 0 else 0.55)
            place(out[0], s0, syl * np.cos(a) * g)
            place(out[1], s0, syl * np.sin(a) * g)
    return out


def drums(events, n):
    kit = np.zeros(n)
    shk = np.zeros(n)
    kicks = []
    for e in events:
        d, v, s = e['n'][0], e['v'] / 127, at(e)
        if d == KICK:
            L = int(0.4 * SR)
            t = np.arange(L) / SR
            k = np.sin(2 * np.pi * np.cumsum(64 + 90 * np.exp(-t / 0.02)) / SR) * np.exp(-t / 0.09)
            k[:220] += bp(rng.normal(size=220), 1500, 6000) * np.exp(-np.arange(220) / 50) * 0.5
            place(kit, s, k * v)
            kicks.append(s)
        elif d == C.RIM:
            L = int(0.08 * SR)
            t = np.arange(L) / SR
            r = (np.sin(2 * np.pi * 1700 * t) * np.exp(-t / 0.012) + bp(rng.normal(size=L), 2000, 6000) * np.exp(-t / 0.006))
            place(kit, s, r * v * 0.5)
        elif d == CLAP:
            for off in (0, 0.011, 0.023):
                L = int(0.12 * SR)
                cl = bp(rng.normal(size=L), 800, 4500) * np.exp(-np.arange(L) / ((0.05 if off > 0.02 else 0.01) * SR))
                place(kit, s + off * SR, cl * v * 0.45)
        elif d == C.SHAKER:
            L = int(0.09 * SR)
            t = np.arange(L) / SR
            sh = bp(rng.normal(size=L), 4500, 10000) * np.minimum(1, t / 0.006) * np.exp(-t / 0.03)
            place(shk, s, sh * v)
        elif d == CLHAT:
            L = int(0.05 * SR)
            place(shk, s, hp(rng.normal(size=L), 7000) * np.exp(-np.arange(L) / (0.012 * SR)) * v * 0.7)
    return kit, shk, kicks


def tempo_echo(st, beats=0.75, fb=0.3, mix=0.25):
    D = int(beats * SPB)
    out = st.copy()
    for i, ch in enumerate(st):
        a = np.zeros(D + 1); a[0] = 1; a[D] = -fb
        y = sg.lfilter([1.0], a, ch) - ch
        out[1 - i] += lp(hp(y, 250), 3500) * mix     # cross-feed: ping-pong-ish
    return out


def render(E, total_beats, tail_s=4.0):
    n = int(total_beats * SPB + tail_s * SR)
    print('synth', flush=True)
    org = organ(E['organ'], n)
    pno = piano(E['piano'], n)
    bs = bass(E['bass'], n)
    wb = weird_bass(E['wbass'], n)
    fl = R.lead_track([dict(e, t=C.swing(e['t'])) for e in E['flute']], n)
    ar = R.arp_track([dict(e, t=C.swing(e['t'])) for e in E['arp']], n)
    pd = pad(E['pad'], n)
    wh = whisper(E['whisper'], n)
    tr = R.tree_track(E['tree'], n)
    kit, shk, kicks = drums(E['drums'], n)
    duck = R.ducker(kicks, n, depth=0.3, tau=0.12)

    np.savez('out/parts.npz', organ=org, piano=pno, bass=bs, wbass=wb, flute=fl, arp=ar, pad=pd,
             whisper=wh, tree=tr, kit=kit, shaker=shk, duck=duck)
    return n


if __name__ == '__main__':
    E, markers, end = C.build(loops=2)
    render(E, end)
