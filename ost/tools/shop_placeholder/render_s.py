"""Render Take Your Time - Kokiri Haven Shop (placeholder v1): synthesise each part to out/parts.npz."""
import sys
import numpy as np, scipy.signal as sg
sys.path.insert(0, '/home/claude/hollow_husk')
sys.path.insert(0, '/home/claude/dungeon')
import render as R
import compose_s as C
from compose import KICK, SNARE, CLHAT, OPHAT

R.BPM = C.BPM
R.SPB = R.SR * 60 / C.BPM
SR, SPB = R.SR, R.SPB
import render_d as RD          # reuse the round bass and the pad
RD.SPB = SPB
RD.C.swing = C.swing
rng = np.random.default_rng(21)
mtof, lp, hp, bp, place, env, secs = R.mtof, R.lp, R.hp, R.bp, R.place, R.env, R.secs


def at(e):
    return C.swing(e['t']) * SPB


def epiano(events, n):
    """Warm FM electric piano: sine carrier, ratio-1 modulator with a decaying index (bark),
    a faint high tine, gentle tremolo (auto-pan) added later."""
    out = np.zeros(n)
    for e in events:
        gate = secs(e['d']) * SR
        L = int(gate + 0.5 * SR)
        t = np.arange(L) / SR
        vel = e['v'] / 127
        for m in e['n']:
            f = mtof(m)
            idx = (0.6 + 1.6 * vel) * np.exp(-t / 0.35) + 0.25
            mod = np.sin(2 * np.pi * f * t + rng.random())
            car = np.sin(2 * np.pi * f * t + idx * mod)
            tine = 0.18 * vel * np.sin(2 * np.pi * f * 14.0 * t) * np.exp(-t / 0.02)
            decay = np.exp(-t / (1.6 * (261 / max(f, 120)) ** 0.4))
            rel = np.where(t < gate / SR, 1.0, np.exp(-(t - gate / SR) / 0.09))
            place(out, at(e), (car * decay + tine) * rel * vel / len(e['n']) ** 0.6)
    return lp(out, 6000, 2)


def soft_lead(events, n):
    """SFC-style 'pulse flute': narrow pulse + sine body, soft attack, delayed vibrato, a little breath."""
    out = np.zeros(n)
    for e in events:
        f0 = mtof(e['n'][0])
        gate = secs(e['d']) * SR
        L = int(gate + 0.2 * SR)
        t = np.arange(L) / SR
        vib = 1 + np.clip((t - 0.18) / 0.25, 0, 1) * 0.006 * np.sin(2 * np.pi * 5.0 * t)
        f = f0 * vib
        ph = 2 * np.pi * np.cumsum(f) / SR
        pulse = R.blep_saw(f, L) - R.blep_saw(f, L, 0.25)            # 25% pulse
        sig = 0.8 * np.sin(ph) + 0.25 * np.sin(2 * ph) + 0.22 * lp(pulse, 2800)
        sig += 0.05 * bp(rng.normal(size=L), f0, f0 * 3) * np.exp(-t / 0.08)
        sig *= env(L, 0.015, 0.4, 0.85, 0.08, gate)
        place(out, at(e), sig * e['v'] / 127)
    return out


def drums(events, n):
    kit = np.zeros(n)
    hat = np.zeros(n)
    kicks = []
    for e in events:
        d, v, s = e['n'][0], e['v'] / 127, at(e)
        if d == KICK:
            L = int(0.35 * SR)
            t = np.arange(L) / SR
            k = np.sin(2 * np.pi * np.cumsum(55 + 80 * np.exp(-t / 0.03)) / SR) * np.exp(-t / 0.13)
            k[:150] += bp(rng.normal(size=150), 1000, 5000) * np.exp(-np.arange(150) / 40) * 0.35
            place(kit, s, k * v)
            kicks.append(s)
        elif d == SNARE:
            L = int(0.25 * SR)
            t = np.arange(L) / SR
            body = 0.55 * np.sin(2 * np.pi * 200 * t) * np.exp(-t / 0.04)
            wires = bp(rng.normal(size=L), 1500, 8000) * np.exp(-t / 0.075)
            place(kit, s, (body + wires) * v * 0.8)
        elif d in (CLHAT, OPHAT):
            L = int((0.28 if d == OPHAT else 0.05) * SR)
            t = np.arange(L) / SR
            place(hat, s, hp(rng.normal(size=L), 7500) * np.exp(-t / (0.1 if d == OPHAT else 0.014)) * v)
        elif d == C.SHAKER:
            L = int(0.08 * SR)
            t = np.arange(L) / SR
            place(hat, s, bp(rng.normal(size=L), 4500, 10000) * np.minimum(1, t / 0.006) * np.exp(-t / 0.025) * v * 0.6)
    return kit, hat, kicks


if __name__ == '__main__':
    E, markers, end = C.build(loops=2)
    n = int(end * SPB + 4 * SR)
    print('synth', flush=True)
    sw = lambda part: [dict(e, t=C.swing(e['t'])) for e in E[part]]
    parts = dict(
        lead=soft_lead(E['lead'], n),
        bell=R.arp_track(sw('bell'), n),
        marimba=R.arp_track(sw('marimba'), n),
        epiano=epiano(E['epiano'], n),
        bass=RD.bass(E['bass'], n),
        pad=RD.pad(E['pad'], n),
    )
    kit, hat, kicks = drums(E['drums'], n)
    parts.update(kit=kit, hat=hat, duck=R.ducker(kicks, n, depth=0.3, tau=0.1))
    np.savez('out/parts.npz', **{k: v.astype(np.float32) for k, v in parts.items()})
    print('done', n / SR, 's')
