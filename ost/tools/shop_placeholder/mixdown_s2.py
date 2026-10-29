"""Mix and master shop v2: grimy tape, crushed drums, industrial bed; -14 LUFS, 32 kHz game loop."""
import sys, json
sys.path.insert(0, '/home/claude/hollow_husk'); sys.path.insert(0, '/home/claude/mix')
import numpy as np, scipy.signal as sg
import render as R, compose_s2 as C, mix as MX
from common import ltas, load, EDGES
R.BPM = C.BPM; R.SPB = R.SR * 60 / C.BPM
SR = R.SR
P = {k: v.astype(float) for k, v in np.load('out/parts2.npz').items()}
G = dict(lead=-2, pipe=-12, marimba=-15, epiano=-4, bass=-4.5, grit=-13, pad=-14, kit=3.5, hat=-10, metal=-11,
         hum=-21, fx=-16, vinyl=-27)
G.update(json.loads(sys.argv[1]) if len(sys.argv) > 1 else {})
nm, pan, db, lp, hp, bp = R.norm, R.pan, R.db, R.lp, R.hp, R.bp
n = len(P['lead'])
t = np.arange(n) / SR


def tape(st, wow=0.0011, flutter=0.00015):
    """Wow and flutter: a slowly wandering delay, so pitch drifts like a tired tape."""
    d = (0.004 + wow * np.sin(2 * np.pi * 0.55 * t) + 0.5 * wow * np.sin(2 * np.pi * 0.23 * t + 1)
         + flutter * np.sin(2 * np.pi * 6.3 * t)) * SR
    idx = np.arange(n) - d
    return np.stack([np.interp(idx, np.arange(n), ch) for ch in st])


def crush(x, bits=7, rate=11025, mix=0.45):
    hold = int(SR / rate)
    y = np.repeat(x[..., ::hold], hold, axis=-1)[..., :x.shape[-1]]
    q = 2 ** (bits - 1)
    y = np.round(y * q) / q
    return (1 - mix) * x + mix * y


# --- keys bus: e-piano (auto-pan) + lead + pipe + pad, through tape and a dusty low-pass
trem = 0.12 * np.sin(2 * np.pi * (C.BPM / 60 / 2) * t)
ep = nm(P['epiano'], .1) * db(G['epiano'])
epst = np.stack([ep * (1 - trem), ep * (1 + trem)])
keys = (epst + pan(nm(P['lead'], .1) * db(G['lead']), 0.05) + pan(nm(P['pipe'], .1) * db(G['pipe']), 0.35)
        + pan(nm(lp(P['marimba'], 2500), .1) * db(G['marimba']), -0.4) + R.pan(nm(P['pad'], .1) * db(G['pad']), 0))
keys = tape(keys)
keys = np.stack([lp(hp(c, 90), 6500, 2) for c in keys])
keys = 0.65 * keys + 0.35 * R.sfc_process(keys)

# --- bass: round body + a distorted grit layer
b = nm(P['bass'], .1)
grit = bp(np.tanh(b * 12), 400, 2500)
bass = pan((b * db(G['bass']) + nm(grit, .1) * db(G['grit'])) * P['duck'], 0)

# --- drums: crushed and saturated, muffled "through the wall" in the C section
kit = nm(P['kit'], .1) * db(G['kit'])
hat = nm(lp(P['hat'], 8500, 2), .1) * db(G['hat'])
metal = nm(P['metal'], .1) * db(G['metal'])
dr = np.stack([kit + hat * 0.9 + metal * 0.9, kit + hat * 1.1 + metal * 1.1])
dr = crush(dr)
dr = np.tanh(dr * 2.5) / 2.5 * 1.3
muff = np.stack([lp(c, 700, 2) for c in dr]) * 1.4
dr = dr * (1 - P['mask']) + muff * P['mask']

# --- the industrial bed
bed = (pan(nm(P['hum'], .1) * db(G['hum']), 0) + pan(nm(P['fx'], .1) * db(G['fx']), -0.3)
       + pan(nm(P['vinyl'], .1) * db(G['vinyl']), 0.1))

irs = R.make_ir(2.2, 0.38)                                         # a concrete back room
send = keys * db(-11) + pan(nm(P['fx'], .1) * db(-9), 0) + dr * db(-22)
wet = np.stack([lp(c, 4500) for c in R.reverb(send, irs)])
mix = keys + bass + dr + bed + wet
m, s = (mix[0] + mix[1]) / 2, (mix[0] - mix[1]) / 2
s = hp(s, 140, 2)
mix = np.stack([m + s, m - s])
mix = np.tanh(mix / np.max(np.abs(mix)) * 1.4)                       # gentle tape saturation on the bus
m48 = sg.resample_poly(mix, 160, 147, axis=1)
ref = load('ref/ref.mp3')
r, rw = ltas(ref); sp, _ = ltas(m48)
diff = sp - r; diff[0] = np.median(diff)
centers = [np.sqrt(a * b_) for a, b_ in zip(EDGES[:-1], EDGES[1:])]
corr = np.clip(-0.6 * (diff - np.median(diff)), -4, 4)
corr = np.convolve(np.pad(corr, 1, mode='edge'), [0.25, 0.5, 0.25], 'valid')
bands = [('peak', f, g, 0.9) for f, g in zip(centers, corr) if abs(g) > 0.4 and 30 < f < 15000]
x = MX.eq(m48, [('hp', 32, 0, 4), ('peak', 45, -3.5, 1.0)] + bands)
x = x / np.max(np.abs(x)) * 0.5
x, gr = MX.compress(x, -19, 2.0, 0.03, 0.25, knee=8)
g = 0
for _ in range(8):
    y, lg = MX.limiter(x * 10 ** (g / 20), -1.3)
    l = MX.lufs(y); g += -14 - l
    if abs(-14 - l) < 0.15: break
s2, sw = ltas(y)
print('LUFS %.2f TP %.2f glue %.1f lim %.1f' % (l, MX.true_peak(y), np.median(gr), lg))
print('band ', ' '.join(f'{e:6d}' for e in EDGES[:-1]))
print('diff ', ' '.join(f'{a - b_:6.1f}' for a, b_ in zip(s2, r)))
np.save('out/master48_v2.npy', y.astype(np.float32))
S48 = 48000
i0 = round(C.INTRO_BEATS * 60 / C.BPM * S48); L = round(C.LOOP_BEATS * 60 / C.BPM * S48)
body = y[:, i0 + L:i0 + 2 * L].copy()
X = int(0.06 * S48)
w = np.sin(np.linspace(0, np.pi / 2, X)) ** 2
body[:, -X:] = body[:, -X:] * (1 - w) + y[:, i0 + L - X:i0 + L] * w
game = np.concatenate([y[:, :i0], body], axis=1)
print('seam', np.abs(game[:, -1] - game[:, i0]).max(), 'true', np.abs(y[:, i0 + L] - y[:, i0 + L - 1]).max())
T = 'Take Your Time - Kokiri Haven Shop (placeholder v2)'
g32 = sg.resample_poly(game, 2, 3, axis=1)
i32, L32 = round(i0 * 2 / 3), round(L * 2 / 3)
MX.write_wav(f'out/{T}.wav', g32[:, :i32 + L32], 32000, 16, loop=(i32, i32 + L32))
print('32k loop', i32, i32 + L32)
pv = y[:, :i0 + 2 * L + 3 * S48].copy(); fl_ = 6 * S48; pv[:, -fl_:] *= np.linspace(1, 0, fl_) ** 1.5
MX.write_wav('out/tmp_preview.wav', pv, S48, 24)
