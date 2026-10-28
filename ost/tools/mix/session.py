"""Hollow Husk mix session: per-stem chains, balance, master. Run: python3 session.py"""
import numpy as np, glob, json, subprocess, sys
from mix import *

S = {f.split('husk_')[1][:-4]: f for f in glob.glob('stems/hollowed_husk_*.wav')}
SEC = lambda s: int(round(s * SR))
N66 = SEC(66)

# ---- per-stem settings ----------------------------------------------------------
# target: loudness (LUFS, K-weighted) while the part is playing, before the master
CH = {
    'drums':   dict(target=-15.5, eq=[('hp', 36, 0, 4), ('low', 60, -2.0), ('peak', 420, -2.5, 1.0), ('peak', 4500, 1.5, 0.8), ('high', 10000, 2.0)],
                    para=(-26, 6, 0.40), rev=-26),
    'sub_bass': dict(target=-22.5, eq=[('hp', 28, 0, 4), ('lp', 95, 0, 4)], duck=0.55, mono=True),
    'bass':    dict(target=-19.0, eq=[('hp', 40, 0, 2), ('peak', 250, -2.0, 1.0), ('peak', 900, 3.0, 1.0), ('lp', 6000, 0, 2)],
                    comp=(-18, 4, 0.004, 0.08), duck=0.25, mono=True),
    'dist_guitar_main_chords': dict(target=-16.5, eq=[('hp', 85, 0, 4), ('peak', 280, -2.0, 1.2), ('peak', 550, -1.5, 1.2), ('peak', 2600, 3.5, 0.9), ('lp', 10500, 0, 2)],
                    comp=(-16, 2.5, 0.01, 0.12), double=0.85),
    'dist_guitar_lead': dict(target=-19.5, eq=[('hp', 120, 0, 2), ('peak', 300, -2.0, 1.0), ('peak', 1800, 2.0, 1.0), ('lp', 9000, 0, 2)],
                    pan=-0.3, rev=-20, dly=-24),
    'lead':    dict(target=-16.0, eq=[('hp', 180, 0, 2), ('peak', 650, -2.5, 1.0), ('peak', 3000, 2.5, 1.0), ('high', 8000, 1.5)],
                    comp=(-20, 3, 0.008, 0.15), rev=-15, dly=-19),
    'organ':   dict(target=-22.5, eq=[('hp', 150, 0, 2), ('peak', 450, -2.5, 1.0)], pan=0.35, width=1.2, rev=-18),
    'string_chords': dict(target=-22.0, eq=[('hp', 180, 0, 2), ('lp', 9000, 0, 2)], width=1.4, rev=-14),
    'choir_pad': dict(target=-21.0, eq=[('hp', 200, 0, 2), ('peak', 700, -2.5, 1.0), ('peak', 3000, 1.5, 0.8)], width=1.35, rev=-12, duck=0.15),
    'arp':     dict(target=-23.0, eq=[('hp', 300, 0, 2), ('peak', 2500, 1.0, 1.0)], pan=-0.3, rev=-16, dly=-20),
    'orchestra_hit': dict(target=-19.5, eq=[('hp', 120, 0, 4), ('peak', 250, -2.0, 1.0)], comp=(-14, 3, 0.002, 0.1), rev=-16),
}
OVERRIDE = json.loads(sys.argv[1]) if len(sys.argv) > 1 else {}
for k, v in OVERRIDE.items():
    CH[k]['target'] = v


def extend(x):
    """intro + loop + loop + 4 s pad: so FX and dynamics are continuous over the seam."""
    x = np.pad(x, ((0, 0), (0, max(0, N66 - x.shape[1]))))[:, :N66]
    return np.concatenate([x, x[:, SEC(LOOP_START):N66], np.zeros((2, SEC(4)))], axis=1)


raw = {k: extend(load(f)) for k, f in S.items()}
n = raw['drums'].shape[1]
kick, nk = kick_env(raw['drums'], n)
print('kicks found', nk)

hall = make_ir(2.4, 0.42, 25, 7000, 1)
room = make_ir(0.9, 0.12, 8, 9000, 2)
dotted8 = BEAT * 0.75

bus = np.zeros((2, n + SEC(5)))
rev_send = np.zeros((2, n)); room_send = np.zeros((2, n)); dly_send = np.zeros((2, n))
report = {}
for k, c in CH.items():
    x = eq(raw[k], c['eq'])
    if 'comp' in c:
        x, _ = compress(x, *c['comp'])
    if 'para' in c:
        thr, ratio, mixamt = c['para']
        sm, _ = compress(x, thr, ratio, 0.001, 0.08)
        x = x + mixamt * sm
    if c.get('mono'):
        x = np.stack([x.mean(0)] * 2)
    if 'double' in c:
        m, d = double(x)
        x = pan(np.stack([m, m]), -c['double']) + pan(np.stack([d, d]), c['double'])
        x *= 0.75
    if 'width' in c:
        x = width(x, c['width'])
    if 'pan' in c:
        x = pan(x, c['pan'])
    if 'duck' in c:
        x = x * (1 - c['duck'] * kick)
    g = c['target'] - lufs(x, gated_active=True)
    x *= 10 ** (g / 20)
    report[k] = round(g, 1)
    bus[:, :n] += x
    if 'rev' in c:
        (rev_send if k != 'drums' else room_send)[:] += x * 10 ** (c['rev'] / 20)
    if 'dly' in c:
        dly_send[:] += x * 10 ** (c['dly'] / 20)
print('stem gains dB', report)

w = reverb(rev_send, hall); bus[:, :w.shape[1]] += w[:, :bus.shape[1]] * 1.0
w = reverb(room_send, room); bus[:, :min(bus.shape[1], w.shape[1])] += w[:, :bus.shape[1]]
d = pingpong(dly_send, dotted8, 0.35)
d = eq(d, [('hp', 300, 0, 2)])
bus[:, :min(bus.shape[1], d.shape[1])] += d[:, :bus.shape[1]]
bus = bus[:, :n]
np.save('out/bus_premaster.npy', bus.astype(np.float32))
