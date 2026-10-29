"""Render Take Your Time - Kokiri Haven Shop (placeholder v2): grimy / industrial parts to out/parts2.npz."""
import sys
import numpy as np, scipy.signal as sg
sys.path.insert(0, '/home/claude/shop')
import render_s as RS             # sets up R (BPM 120) and gives the e-piano, kit and helpers
import compose_s2 as C
from compose import KICK, SNARE
R, RD = RS.R, RS.RD
SR, SPB = RS.SR, RS.SPB
rng = np.random.default_rng(33)
mtof, lp, hp, bp, place, env, secs = R.mtof, R.lp, R.hp, R.bp, R.place, R.env, R.secs


def at(e):
    return C.swing(e['t']) * SPB


def dark_lead(events, n):
    """Detuned square + saw, low-passed and a little hoarse, with a short glide between notes."""
    out = np.zeros(n)
    prev = None
    for e in events:
        f1 = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        L = int(gate + 0.2 * SR)
        t = np.arange(L) / SR
        f0 = prev if prev else f1
        f = f1 + (f0 - f1) * np.exp(-t / 0.03)
        f = f * (1 + np.clip((t - 0.25) / 0.3, 0, 1) * 0.007 * np.sin(2 * np.pi * 5.2 * t))
        sig = 0.6 * R.blep_sq(f * 1.003, L) + 0.5 * R.blep_saw(f * 0.997, L) + 0.4 * np.sin(2 * np.pi * np.cumsum(f) / SR)
        sig = lp(sig, 1900, 2)
        sig += 0.04 * bp(rng.normal(size=L), 1500, 5000)
        sig = np.tanh(sig * 1.6) * env(L, 0.012, 0.5, 0.8, 0.09, gate)
        place(out, at(e), sig * e['v'] / 127)
        prev = f1
    return out


def pipe(events, n):
    """Tuned metal pipe: inharmonic partials, a hard tick, long ring."""
    out = np.zeros(n)
    for e in events:
        f = mtof(e['n'][0])
        L = int(1.6 * SR)
        t = np.arange(L) / SR
        sig = sum(a * np.sin(2 * np.pi * f * r * t + rng.random() * 6) * np.exp(-t / tau)
                  for r, a, tau in ((1, 1, .9), (2.76, .5, .35), (5.4, .25, .12), (8.93, .12, .05)))
        sig[:60] += rng.normal(size=60) * 0.3
        place(out, at(e), sig * e['v'] / 127)
    return out


def drums(events, n):
    kit, hat, kicks = RS.drums([e for e in events if e['n'][0] != C.CLANK], n)
    metal = np.zeros(n)
    for e in events:
        d, v, s = e['n'][0], e['v'] / 127, at(e)
        if d == SNARE and e['v'] > 60:       # an industrial ring under the backbeat
            L = int(0.3 * SR)
            t = np.arange(L) / SR
            ring = sum(np.sin(2 * np.pi * fr * t) for fr in (523, 1187, 1679, 2531)) * np.exp(-t / 0.07)
            place(metal, s, ring * v * 0.25)
        elif d == C.CLANK:
            L = int(0.6 * SR)
            t = np.arange(L) / SR
            cl = sum(a * np.sin(2 * np.pi * fr * t) * np.exp(-t / tau)
                     for fr, a, tau in ((311, 1, .25), (743, .7, .15), (1290, .5, .09), (2210, .3, .05)))
            cl[:200] += bp(rng.normal(size=200), 1000, 6000) * np.exp(-np.arange(200) / 40)
            place(metal, s, cl * v * 0.6)
    return kit, hat, metal, kicks


def ambience(events, n):
    """Machinery hum bed (always), vinyl crackle (always), swells, steam hisses and distant clanks."""
    t = np.arange(n) / SR
    hum = sum(a * np.sin(2 * np.pi * f * t + rng.random() * 6) for f, a in ((55, 1), (110, .5), (165, .25), (220, .15)))
    hum *= 1 + 0.2 * np.sin(2 * np.pi * 0.11 * t)
    hum += 0.4 * lp(rng.normal(size=n), 300, 2) * (1 + 0.5 * np.sin(2 * np.pi * 0.07 * t))
    swell = np.zeros(n)
    fx = np.zeros(n)
    for e in events:
        k, s0, g = e['kind'], int(at(e)), secs(e['d']) * SR
        v = e['v'] / 127
        if k == 'swell':
            L = int(g)
            w = np.sin(np.pi * np.arange(L) / L) ** 2
            swell[s0:s0 + L] += w[:max(0, min(L, n - s0))] * v
        elif k == 'steam':
            L = int(g)
            tt = np.arange(L) / SR
            st = bp(rng.normal(size=L), 2500, 9000) * np.minimum(1, tt / 0.15) * np.exp(-tt / (L / SR * 0.5))
            place(fx, s0, st * v * 0.35)
        elif k == 'clank':
            L = int(1.2 * SR)
            tt = np.arange(L) / SR
            cl = sum(a * np.sin(2 * np.pi * fr * tt) * np.exp(-tt / tau)
                     for fr, a, tau in ((97, 1, .5), (233, .7, .4), (587, .5, .25), (1013, .3, .15)))
            cl[:300] += lp(rng.normal(size=300), 3000) * np.exp(-np.arange(300) / 80)
            place(fx, s0, lp(cl, 2500) * v)
    hum = lp(hum, 400) * (0.5 + 0.8 * np.clip(swell, 0, 1))
    crackle = np.zeros(n)
    pops = rng.random(n) < 18 / SR                       # ~18 crackles a second
    crackle[pops] = rng.normal(size=pops.sum()) * rng.random(pops.sum()) ** 3
    crackle = bp(crackle, 800, 7000)
    hiss = hp(rng.normal(size=n), 3000) * 0.02
    return hum, fx, crackle + hiss


if __name__ == '__main__':
    E, markers, end = C.build(loops=2)
    n = int(end * SPB + 4 * SR)
    print('synth', flush=True)
    sw = lambda part: [dict(e, t=C.swing(e['t'])) for e in E[part]]
    RS.C.swing = C.swing                                       # same swing; keeps the e-piano and kit in step
    RD.C.swing = C.swing
    parts = dict(
        lead=dark_lead(E['lead'], n),
        pipe=pipe(E['pipe'], n),
        marimba=R.arp_track(sw('marimba'), n),
        epiano=RS.epiano(E['epiano'], n),
        bass=RD.bass(E['bass'], n),
        pad=RD.pad(E['pad'], n),
    )
    kit, hat, metal, kicks = drums(E['drums'], n)
    hum, fx, vinyl = ambience(E['amb'], n)
    mask = np.zeros(n)                                          # 1 where the drums are "through the wall"
    for loop in range(2):
        for a, b in C.MUFFLED:
            s0, s1 = int((C.INTRO_BEATS + loop * C.LOOP_BEATS + a) * SPB), int((C.INTRO_BEATS + loop * C.LOOP_BEATS + b) * SPB)
            mask[s0:s1] = 1
    mask = np.convolve(mask, np.ones(int(0.3 * SR)) / int(0.3 * SR), 'same')
    parts.update(kit=kit, hat=hat, metal=metal, hum=hum, fx=fx, vinyl=vinyl, mask=mask,
                 duck=R.ducker(kicks, n, depth=0.35, tau=0.1))
    np.savez('out/parts2.npz', **{k: v.astype(np.float32) for k, v in parts.items()})
    print('done', n / SR)
