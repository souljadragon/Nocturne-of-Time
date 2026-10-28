"""Render Hollow Husk - Random Encounter: SFC-style retro bus + modern layer."""
import numpy as np
import scipy.signal as sg
import struct, wave, sys
from compose import build, BPM, INTRO_BEATS, LOOP_BEATS, KICK, SNARE, CLHAT, OPHAT, CRASH, RIDE, LTOM, MTOM, HTOM

SR = 44100
SPB = SR * 60 / BPM
rng = np.random.default_rng(7)


def mtof(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def blep_saw(freq, n, ph0=0.0):
    dt = np.broadcast_to(np.asarray(freq, float) / SR, (n,)).copy()
    ph = (ph0 + np.cumsum(dt)) % 1.0
    y = 2 * ph - 1
    m = ph < dt
    x = ph[m] / dt[m]
    y[m] -= x + x - x * x - 1
    m = ph > 1 - dt
    x = (ph[m] - 1) / dt[m]
    y[m] -= x * x + x + x + 1
    return y


def blep_sq(freq, n, ph0=0.0):
    return blep_saw(freq, n, ph0) - blep_saw(freq, n, ph0 + 0.5)


def env(n, a, d, s, r, hold):
    """ADSR in seconds; hold = gate length in samples. Returns length n."""
    t = np.arange(n) / SR
    e = np.where(t < a, t / max(a, 1e-4), s + (1 - s) * np.exp(-(t - a) / max(d, 1e-4)))
    g = hold / SR
    if g < n / SR:
        lvl = np.interp(g, t, e)
        rel = lvl * np.exp(-(t - g) / max(r, 1e-4))
        e = np.where(t < g, e, rel)
    return e


def lp(x, fc, order=2):
    return sg.sosfilt(sg.butter(order, min(fc, SR * 0.45), 'low', fs=SR, output='sos'), x)


def hp(x, fc, order=2):
    return sg.sosfilt(sg.butter(order, fc, 'high', fs=SR, output='sos'), x)


def bp(x, lo, hi, order=2):
    return sg.sosfilt(sg.butter(order, [lo, min(hi, SR * 0.45)], 'band', fs=SR, output='sos'), x)


def place(buf, s, sig):
    s = int(round(s))
    if s < 0:
        sig, s = sig[-s:], 0
    e = min(len(buf), s + len(sig))
    if e > s:
        buf[s:e] += sig[:e - s]


def secs(beats):
    return beats * 60 / BPM


# ---------------------------------------------------------------- instruments
def guitar_track(events, n, take=0):
    out = np.zeros(n)
    jit = 0.004 if take else 0.0
    for e in events:
        f = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        mute = e.get('mute', False)
        L = int(gate + SR * (0.08 if mute else 0.35))
        voices = [(f, 1.0), (f * 1.4983, 0.8), (f * 2, 0.45)]
        sig = np.zeros(L)
        for fv, amp in voices:
            for c in (-7, 7):
                det = fv * 2 ** ((c + (rng.normal() * 3 if take else 0)) / 1200)
                sig += amp * blep_saw(det, L, rng.random())
        if mute:
            sig = lp(sig, 700) * env(L, 0.002, 0.09, 0.15, 0.03, gate)
        else:
            sig = lp(sig, 3200) * env(L, 0.003, 0.6, 0.7, 0.12, gate)
        place(out, e['t'] * SPB + rng.normal() * jit * SR, sig * e['v'] / 127)
    out = hp(out, 80)
    out = np.tanh(out * 6.0)
    out = lp(out, 5200, 4)
    out = hp(out, 70)
    # cabinet honk dip around 400 Hz
    b, a = sg.iirpeak(400, 1.2, fs=SR)
    out = out - 0.35 * sg.lfilter(b, a, out)
    return out


def bass_track(events, n):
    out = np.zeros(n)
    for e in events:
        f = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        L = int(gate + 0.06 * SR)
        sig = blep_saw(f, L) * 0.7 + blep_sq(f / 2 if f > 70 else f, L) * 0.3
        bright = sig * np.exp(-np.arange(L) / (SR * (0.05 if e.get('pop') else 0.025)))
        dark = lp(sig, 500)
        s = (dark + bright * (1.4 if e.get('pop') else 0.7)) * env(L, 0.002, 0.25, 0.6, 0.03, gate)
        click = np.zeros(L)
        click[:200] = rng.normal(size=200) * np.exp(-np.arange(200) / 40) * 0.5
        place(out, e['t'] * SPB, (s + click) * e['v'] / 127)
    return np.tanh(lp(out, 3500) * 1.8)


def organ_track(events, n):
    out = np.zeros(n)
    bars = [(0.5, .5), (1, 1), (1.5, .4), (2, .7), (3, .35), (4, .3), (6, .15), (8, .25)]
    for e in events:
        gate = secs(e['d']) * SR
        L = int(gate + 0.12 * SR)
        t = np.arange(L) / SR
        sig = np.zeros(L)
        for m in e['n']:
            f = mtof(m)
            for h, a in bars:
                sig += a * np.sin(2 * np.pi * f * h * t * (1 + 0.0015 * np.sin(2 * np.pi * 6.2 * t)) + rng.random() * 6)
        sig *= env(L, 0.004, 0.3, 0.85, 0.06, gate)
        sig[:150] += rng.normal(size=150) * np.exp(-np.arange(150) / 30) * 0.6
        place(out, e['t'] * SPB, sig * e['v'] / 127 / len(e['n']))
    t = np.arange(n) / SR
    out *= 1 + 0.18 * np.sin(2 * np.pi * 6.2 * t)
    return np.tanh(out * 1.6)


def lead2_track(events, n):
    """Distorted guitar lead, doubles the melody an octave below."""
    out = np.zeros(n)
    for e in events:
        f0 = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        L = int(gate + 0.15 * SR)
        t = np.arange(L) / SR
        scoop = 2 ** (-0.45 * np.exp(-t / 0.03) / 12)
        vib_amt = np.clip((t - 0.22) / 0.25, 0, 1) * 0.012
        f = f0 * scoop * (1 + vib_amt * np.sin(2 * np.pi * 5.6 * t))
        sig = blep_saw(f, L) + 0.5 * blep_saw(f * 1.004, L, 0.3)
        sig = sig * env(L, 0.004, 0.4, 0.85, 0.07, gate)
        place(out, e['t'] * SPB, sig * e['v'] / 127)
    out = np.tanh(hp(out, 150) * 5)
    out = lp(out, 4000, 4)
    b, a = sg.iirpeak(1600, 1.0, fs=SR)
    return out + 0.5 * sg.lfilter(b, a, out)


def lead_track(events, n):
    """Hollow wooden flute (ocarina / shakuhachi-like) with breath, delayed vibrato,
    and a 'wilt': held notes sag in pitch as they fade, like the tree exhaling."""
    out = np.zeros(n)
    for e in events:
        f0 = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        L = int(gate + 0.25 * SR)
        t = np.arange(L) / SR
        g = gate / SR
        scoop = 2 ** (-0.6 * np.exp(-t / 0.035) / 12)
        wilt = 1.0
        if e['d'] >= 2:
            wilt = 2 ** (-0.7 * np.clip((t - g * 0.55) / (g * 0.45 + 0.25), 0, 1) ** 2 / 12)
        vib = 1 + np.clip((t - 0.2) / 0.3, 0, 1) * 0.009 * np.sin(2 * np.pi * 5.2 * t)
        f = f0 * scoop * wilt * vib
        ph = 2 * np.pi * np.cumsum(f) / SR
        tone = np.sin(ph) + 0.22 * np.sin(2 * ph) + 0.12 * np.sin(3 * ph) + 0.05 * np.sin(5 * ph)
        tone += 0.12 * blep_sq(f, L)            # a little SFC sample grit
        breath = bp(rng.normal(size=L), f0 * 1.5, f0 * 4)
        chiff = breath * np.exp(-t / 0.03) * 1.2
        sig = (tone + 0.12 * breath + chiff) * env(L, 0.02, 0.5, 0.85, 0.12, gate)
        place(out, e['t'] * SPB, sig * e['v'] / 127)
    return np.tanh(out * 1.3)


def tree_track(events, n):
    """The dying tree: creaking wood, a low trunk groan, wind in the hollow, knocks, a body drone."""
    out = np.zeros(n)
    for e in events:
        k = e['kind']
        gate = secs(e['d']) * SR
        v = e['v'] / 127
        s0 = e['t'] * SPB
        if k == 'creak':
            L = int(gate + 0.3 * SR)
            t = np.arange(L) / SR
            rate = 18 + 30 * np.sin(np.pi * np.clip(t / (gate / SR), 0, 1)) + 6 * rng.normal(size=L).cumsum() / np.sqrt(L)
            ph = np.cumsum(np.maximum(rate, 4)) / SR
            pulses = np.diff(np.floor(ph), prepend=0) * (0.6 + 0.4 * rng.random(L))
            sig = sum(a * sg.lfilter(*sg.iirpeak(fr, 18, fs=SR), pulses) for fr, a in ((310, 1), (740, .7), (1550, .4), (2900, .2)))
            sig *= np.sin(np.pi * np.clip(t / (gate / SR + 0.1), 0, 1)) ** 0.5
            place(out, s0, sig * v * 6)
        elif k == 'groan':
            L = int(gate + 0.5 * SR)
            t = np.arange(L) / SR
            f = mtof(e['n'][0]) * (1 + 0.04 * np.sin(2 * np.pi * 0.7 * t) + 0.02 * np.sin(2 * np.pi * 2.3 * t))
            sig = lp(blep_saw(f, L) + 0.5 * blep_sq(f * 1.5, L), 380, 2)
            sig *= np.sin(np.pi * np.clip(t / (L / SR), 0, 1)) ** 1.5
            place(out, s0, sig * v * 1.3)
        elif k == 'wind':
            L = int(gate + 0.5 * SR)
            t = np.arange(L) / SR
            nz = rng.normal(size=L)
            sig = np.zeros(L)
            # a slowly moving whistle through the hollow trunk
            for c, a in ((420, 1.0), (980, 0.6)):
                fc = c * (1 + 0.35 * np.sin(2 * np.pi * (0.13 + rng.random() * 0.1) * t + rng.random() * 6))
                blk = 4096
                for i in range(0, L, blk):
                    seg = nz[i:i + blk]
                    sig[i:i + blk] += a * bp(seg, fc[i] * 0.8, fc[i] * 1.25)
            sig = lp(sig, 2500) * np.sin(np.pi * t / (L / SR)) ** 2
            place(out, s0, sig * v * 1.6)
        elif k == 'knock':
            L = int(0.25 * SR)
            t = np.arange(L) / SR
            sig = (np.sin(2 * np.pi * 210 * t) * np.exp(-t / 0.05) + 0.5 * np.sin(2 * np.pi * 570 * t) * np.exp(-t / 0.02))
            sig[:120] += bp(rng.normal(size=120), 800, 4000) * 0.6
            place(out, s0, sig * v * 0.6)
        elif k == 'drone':
            L = int(gate + 0.5 * SR)
            t = np.arange(L) / SR
            f = mtof(e['n'][0])
            sig = sum(np.sin(2 * np.pi * f * h * (1 + d) * t) / h for h, d in ((1, 0), (1, .003), (1.5, 0), (2, -.002), (3, .001)))
            sig *= np.minimum(1, t / 2.5) * np.minimum(1, (L / SR - t) / 1.0)
            place(out, s0, lp(sig, 600) * v * 0.5)
    return out


def arp_track(events, n):
    out = np.zeros(n)
    for e in events:
        f = mtof(e['n'][0])
        L = int(0.6 * SR)
        t = np.arange(L) / SR
        sig = (np.sin(2 * np.pi * f * t) * np.exp(-t / 0.28)
               + 0.4 * np.sin(2 * np.pi * f * 3.93 * t) * np.exp(-t / 0.05)
               + 0.15 * np.sin(2 * np.pi * f * 9.2 * t) * np.exp(-t / 0.015))
        sig[:80] += rng.normal(size=80) * 0.25 * np.exp(-np.arange(80) / 15)
        place(out, e['t'] * SPB, sig * e['v'] / 127)
    return out


def orch_track(events, n):
    out = np.zeros(n)
    for e in events:
        L = int(0.9 * SR)
        t = np.arange(L) / SR
        sig = np.zeros(L)
        for m in e['n']:
            f = mtof(m)
            for c in (-10, 0, 10):
                sig += blep_saw(f * 2 ** (c / 1200), L, rng.random())
        sig = lp(sig, 3800) * np.exp(-t / 0.22)
        nz = bp(rng.normal(size=L), 900, 4500) * np.exp(-t / 0.05) * 3
        timp = np.sin(2 * np.pi * mtof(e['n'][0] - 12) * t * (1 + 0.3 * np.exp(-t / 0.02))) * np.exp(-t / 0.35) * 3
        place(out, e['t'] * SPB, (sig + nz + timp) * e['v'] / 127)
    return np.tanh(out * 0.35)


def pad_track(events, n, choir=False):
    out = np.zeros(n)
    for e in events:
        gate = secs(e['d']) * SR
        L = int(gate + 0.8 * SR)
        t = np.arange(L) / SR
        sig = np.zeros(L)
        for m in e['n']:
            f = mtof(m)
            for c in ((-9, 0, 9) if choir else (-12, -4, 4, 12)):
                vib = 1 + 0.004 * np.sin(2 * np.pi * (4.8 + rng.random()) * t + rng.random() * 6)
                sig += blep_saw(f * 2 ** (c / 1200) * vib, L, rng.random())
        sig *= env(L, 0.35 if choir else 0.25, 1.0, 0.9, 0.5, gate)
        place(out, e['t'] * SPB, sig * e['v'] / 127)
    if choir:
        out = (1.0 * bp(out, 600, 850) + 0.7 * bp(out, 1000, 1300) + 0.35 * bp(out, 2500, 3100)
               + 0.25 * bp(out, 250, 400))
    else:
        out = lp(out, 2800, 2)
    return out


def sub_track(events, n):
    out = np.zeros(n)
    for e in events:
        f = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        L = int(gate + 0.03 * SR)
        t = np.arange(L) / SR
        place(out, e['t'] * SPB, np.sin(2 * np.pi * f * t) * env(L, 0.008, 1, 1, 0.01, gate) * e['v'] / 127)
    return out


def drum_tracks(events, n):
    retro = np.zeros(n)
    modern = np.zeros(n)
    hat = np.zeros(n)
    kicks = []
    for e in events:
        d = e['n'][0]
        v = e['v'] / 127
        s = e['t'] * SPB
        if d == KICK:
            L = int(0.35 * SR)
            t = np.arange(L) / SR
            ph = 2 * np.pi * np.cumsum(45 + 110 * np.exp(-t / 0.03)) / SR
            k = np.sin(ph) * np.exp(-t / 0.12)
            k[:60] += rng.normal(size=60) * 0.5
            place(retro, s, k * v)
            L2 = int(0.5 * SR)
            t2 = np.arange(L2) / SR
            place(modern, s, np.sin(2 * np.pi * np.cumsum(48 + 60 * np.exp(-t2 / 0.04)) / SR) * np.exp(-t2 / 0.2) * v * 0.8)
            kicks.append(s)
        elif d == SNARE:
            L = int(0.3 * SR)
            t = np.arange(L) / SR
            sn = 0.6 * np.sin(2 * np.pi * 190 * t) * np.exp(-t / 0.05) + bp(rng.normal(size=L), 1200, 9000) * np.exp(-t / 0.09)
            place(retro, s, sn * v)
            G = int(0.24 * SR)
            gate = lp(rng.normal(size=G), 6000) * np.minimum(1, np.arange(G) / 200)
            gate[-300:] *= np.linspace(1, 0, 300)
            place(modern, s, gate * v * 0.32)
            # clap layer
            for off in (0, 0.009, 0.02):
                cl = bp(rng.normal(size=int(0.05 * SR)), 900, 5000) * np.exp(-np.arange(int(0.05 * SR)) / (0.012 * SR))
                place(modern, s + off * SR, cl * v * 0.35)
        elif d in (CLHAT, OPHAT):
            L = int((0.3 if d == OPHAT else 0.06) * SR)
            t = np.arange(L) / SR
            place(hat, s, hp(rng.normal(size=L), 7000) * np.exp(-t / (0.12 if d == OPHAT else 0.018)) * v)
        elif d in (CRASH, RIDE):
            L = int((1.8 if d == CRASH else 0.6) * SR)
            t = np.arange(L) / SR
            metal = sum(blep_sq(f, L, rng.random()) for f in (587, 845, 1031, 1453, 1777, 2349))
            cy = hp(rng.normal(size=L) * 0.6 + metal * 0.15, 4500 if d == CRASH else 6000)
            cy *= np.exp(-t / (0.55 if d == CRASH else 0.18))
            place(hat, s, cy * v * (0.7 if d == CRASH else 0.4))
        elif d in (LTOM, MTOM, HTOM):
            f = {LTOM: 90, MTOM: 120, HTOM: 160}[d]
            L = int(0.4 * SR)
            t = np.arange(L) / SR
            tm = np.sin(2 * np.pi * np.cumsum(f * (1 + 0.5 * np.exp(-t / 0.03))) / SR) * np.exp(-t / 0.15)
            tm[:80] += rng.normal(size=80) * 0.4
            place(retro, s, tm * v * 0.9)
    return retro, hat, modern, kicks


# ---------------------------------------------------------------- busses / fx
def pan(x, p):
    a = (p + 1) * np.pi / 4
    return np.stack([x * np.cos(a), x * np.sin(a)])


def norm(x, target_rms=0.1):
    r = np.sqrt(np.mean(x ** 2)) + 1e-9
    return x * target_rms / r


def db(g):
    return 10 ** (g / 20)


def sfc_process(st):
    """SNES-ish: 32 kHz resample, Gaussian-ish top roll-off, light quantize, SPC echo."""
    out = []
    for ch in st:
        y = sg.resample_poly(ch, 320, 441)
        y = np.round(y * 4096) / 4096
        y = sg.resample_poly(y, 441, 320)[:len(ch)]
        out.append(lp(y, 11000, 2))
    out = np.stack(out)
    D = int(0.192 * SR)
    wet = np.zeros_like(out)
    for i, ch in enumerate(out):
        mono_in = ch
        a = np.zeros(D + 1)
        a[0] = 1
        a[D] = -0.42
        y = sg.lfilter([1.0], a, mono_in)
        wet[i] = lp(hp(y - mono_in, 300), 4500)
    # cross the echo a little for width
    wet = np.stack([wet[0] * 0.8 + wet[1] * 0.2, wet[1] * 0.8 + wet[0] * 0.2])
    return out + wet * 0.22


def make_ir(seconds=2.2, tau=0.32):
    L = int(seconds * SR)
    t = np.arange(L) / SR
    irs = []
    for _ in range(2):
        x = rng.normal(size=L) * np.exp(-t / tau)
        x = lp(x, 6500)
        x[:int(0.02 * SR)] = 0
        irs.append(x / np.sqrt(np.sum(x ** 2)))
    return irs


def reverb(st, irs):
    return np.stack([sg.oaconvolve(st[0], irs[0])[:st.shape[1]], sg.oaconvolve(st[1], irs[1])[:st.shape[1]]])


def ducker(kicks, n, depth=0.6, tau=0.11):
    imp = np.zeros(n)
    for s in kicks:
        s = int(round(s))
        if s < n:
            imp[s] = 1
    k = np.exp(-np.arange(int(0.6 * SR)) / (tau * SR))
    env = sg.oaconvolve(imp, k)[:n]
    return 1 - depth * np.clip(env, 0, 1)


def render(E, total_beats, tail_s=3.0):
    n = int(total_beats * SPB + tail_s * SR)
    tr = {}
    print('guitar', flush=True)
    g1 = guitar_track(E['guitar'], n, 0)
    g2 = guitar_track(E['guitar'], n, 1)
    print('others', flush=True)
    tr['bass'] = bass_track(E['bass'], n)
    tr['organ'] = organ_track(E['organ'], n)
    tr['lead'] = lead_track(E['lead'], n)
    tr['lead2'] = lead2_track(E['lead2'], n)
    tr['tree'] = tree_track(E['tree'], n)
    tr['arp'] = arp_track(E['arp'], n)
    tr['orch'] = orch_track(E['orch'], n)
    tr['choir'] = pad_track(E['choir'], n, True)
    tr['strings'] = pad_track(E['strings'], n, False)
    tr['sub'] = sub_track(E['sub'], n)
    d_retro, d_hat, d_mod, kicks = drum_tracks(E['drums'], n)
    duck = ducker(kicks, n)

    # retro bus (<= 8 voices): lead, gtr take 1, bass, organ/orch, arp, kick+toms, snare, hats
    R = (pan(norm(g1, .1) * db(-1), -0.55)
         + pan(norm(tr['bass'], .1) * db(-3), 0)
         + pan(norm(tr['lead'], .1) * db(-0.5), 0.05)
         + pan(norm(tr['organ'], .1) * db(-9), 0.35)
         + pan(norm(tr['arp'], .1) * db(-10), -0.3)
         + pan(norm(tr['orch'], .1) * db(-5), 0)
         + pan(norm(d_retro, .1) * db(1), 0)
         + pan(norm(d_hat, .1) * db(-9), 0.25))
    R = sfc_process(R)

    # modern layer: second guitar take (wide), sub, choir, strings, drum layers
    M = (pan(norm(g2, .1) * db(-1), 0.6)
         + pan(norm(tr['sub'], .1) * duck * db(-5), 0)
         + pan(norm(tr['choir'], .1) * duck * db(-9), -0.15)
         + pan(norm(tr['strings'], .1) * db(-11), 0.2)
         + pan(norm(d_mod, .1) * db(-6), 0)
         + pan(norm(tr['lead2'], .1) * db(-9), -0.25)
         + pan(norm(tr['tree'], .1) * db(-10), 0.1))

    irs = make_ir()
    send = (pan(norm(tr['lead'], .1) * db(-11), 0.1) + pan(norm(tr['tree'], .1) * db(-10), -0.2) + pan(norm(tr['choir'], .1) * db(-10), 0)
            + pan(norm(tr['strings'], .1) * db(-12), 0) + pan(norm(tr['arp'], .1) * db(-14), 0)
            + pan(norm(d_retro, .1) * db(-20), 0) + pan(norm(tr['orch'], .1) * db(-12), 0))
    W = reverb(send, irs)

    mix = R + M + W * 0.9
    mix = np.stack([hp(c, 28) for c in mix])
    # bus glue: gentle compression via rms follower, then soft clip
    rms = np.sqrt(np.maximum(lp(np.mean(mix ** 2, axis=0), 8), 0))
    rms = np.maximum(rms, 1e-6)
    thr = np.percentile(rms, 60)
    gain = np.where(rms > thr, (thr / rms) ** 0.4, 1.0)
    mix *= gain
    mix = mix / np.max(np.abs(mix)) * 1.25
    mix = np.tanh(mix)
    mix = mix / np.max(np.abs(mix)) * db(-1)
    return mix, tr, n


def write_wav(path, st, loop=None):
    data = (np.clip(st.T, -1, 1) * 32767).astype('<i2').tobytes()
    chunks = b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 2, SR, SR * 4, 4, 16)
    chunks += b'data' + struct.pack('<I', len(data)) + data
    if len(data) % 2:
        chunks += b'\0'
    if loop:
        smpl = struct.pack('<9I', 0, 0, int(1e9 / SR), 60, 0, 0, 0, 1, 0)
        smpl += struct.pack('<6I', 0, 0, loop[0], loop[1] - 1, 0, 0)
        chunks += b'smpl' + struct.pack('<I', len(smpl)) + smpl
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)


if __name__ == '__main__':
    E, markers, end = build(loops=2)
    mix, tr, n = render(E, end, tail_s=4)
    intro_s = int(round(INTRO_BEATS * SPB))
    loop_s = int(round(LOOP_BEATS * SPB))
    # preview: intro + 2 loops, fade over the last 6 s of pass 2 + tail
    prev_end = intro_s + 2 * loop_s
    fade_len = int(6 * SR)
    prev = mix[:, :prev_end + int(2 * SR)].copy()
    fl = prev.shape[1] - (prev_end - fade_len)
    prev[:, -fl:] *= np.linspace(1, 0, fl) ** 1.5
    write_wav('out/hollow_husk_preview.wav', prev)
    # game loop: intro + loop body taken from pass 2 (it carries pass 1's tails, so the seam is clean)
    game = np.concatenate([mix[:, :intro_s], mix[:, intro_s + loop_s: intro_s + 2 * loop_s]], axis=1)
    write_wav('out/hollow_husk_game_loop.wav', game, loop=(intro_s, intro_s + loop_s))
    print('intro_s', intro_s, 'loop_s', loop_s, 'game len', game.shape[1])
    # per-bar loudness check
    bar = int(4 * SPB)
    r = [20 * np.log10(np.sqrt(np.mean(mix[:, i:i + bar] ** 2)) + 1e-9) for i in range(0, prev_end, bar)]
    print(' '.join(f'{x:.0f}' for x in r))
