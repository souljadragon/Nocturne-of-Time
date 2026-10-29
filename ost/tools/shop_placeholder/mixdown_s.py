"""Mix and master the shop placeholder: warm, cosy, bouncy; -14 LUFS, 32 kHz game loop."""
import sys, json
sys.path.insert(0, '/home/claude/hollow_husk'); sys.path.insert(0, '/home/claude/mix')
import numpy as np, scipy.signal as sg
import render as R, compose_s as C, mix as MX
from common import ltas, load, EDGES
R.BPM = C.BPM; R.SPB = R.SR * 60 / C.BPM
SR = R.SR
P = {k: v.astype(float) for k, v in np.load('out/parts.npz').items()}
G = dict(lead=-2, bell=-14, marimba=-9, epiano=-3.5, bass=-4, pad=-15, kit=-4, hat=-14)
G.update(json.loads(sys.argv[1]) if len(sys.argv) > 1 else {})
nm, pan, db, lp, hp = R.norm, R.pan, R.db, R.lp, R.hp
n = len(P['lead'])
t = np.arange(n) / SR
trem = 0.12 * np.sin(2 * np.pi * (C.BPM / 60 / 2) * t)            # e-piano auto-pan, one cycle per 2 beats
ep = nm(P['epiano'], .1) * db(G['epiano'])
epst = np.stack([ep * (1 - trem), ep * (1 + trem)])
retro = (pan(nm(P['lead'], .1) * db(G['lead']), 0.05) + pan(nm(P['marimba'], .1) * db(G['marimba']), -0.35)
         + pan(nm(P['bell'], .1) * db(G['bell']), 0.35) + epst)
retro = 0.6 * retro + 0.4 * R.sfc_process(retro)                   # a lighter SFC touch than the dungeon
modern = (pan(nm(P['bass'], .1) * P['duck'] * db(G['bass']), 0) + pan(nm(P['pad'], .1) * db(G['pad']), 0)
          + pan(nm(P['kit'], .1) * db(G['kit']), 0) + pan(nm(lp(P['hat'], 9000, 2), .1) * db(G['hat']), 0.25))
irs = R.make_ir(1.6, 0.25)                                          # a small warm room, not a cavern
send = (pan(nm(P['lead'], .1) * db(-13), 0) + epst * db(-10) + pan(nm(P['marimba'], .1) * db(-12), 0)
        + pan(nm(P['bell'], .1) * db(-12), 0) + pan(nm(P['pad'], .1) * db(-14), 0) + pan(nm(P['kit'], .1) * db(-24), 0))
wet = np.stack([lp(c, 6000) for c in R.reverb(send, irs)])
mix = retro + modern + wet
m, s = (mix[0] + mix[1]) / 2, (mix[0] - mix[1]) / 2
s = hp(s, 140, 2)
mix = np.stack([m + s, m - s])
m48 = sg.resample_poly(mix, 160, 147, axis=1)
ref = load('ref/ref.mp3')
r, rw = ltas(ref); sp, _ = ltas(m48)
diff = sp - r
diff[0] = np.median(diff)
centers = [np.sqrt(a * b) for a, b in zip(EDGES[:-1], EDGES[1:])]
corr = np.clip(-0.6 * (diff - np.median(diff)), -4, 4)
corr = np.convolve(np.pad(corr, 1, mode='edge'), [0.25, 0.5, 0.25], 'valid')
bands = [('peak', f, g, 0.9) for f, g in zip(centers, corr) if abs(g) > 0.4 and 30 < f < 15000]
x = MX.eq(m48, [('hp', 30, 0, 4)] + bands)
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
print('diff ', ' '.join(f'{a - b:6.1f}' for a, b in zip(s2, r)))
print('width', ' '.join(f'{a:6.1f}' for a in sw))
print('refw ', ' '.join(f'{a:6.1f}' for a in rw))
np.save('out/master48.npy', y.astype(np.float32))
S48 = 48000
i0 = round(C.INTRO_BEATS * 60 / C.BPM * S48); L = round(C.LOOP_BEATS * 60 / C.BPM * S48)
body = y[:, i0 + L:i0 + 2 * L].copy()
X = int(0.06 * S48)
w = np.sin(np.linspace(0, np.pi / 2, X)) ** 2
body[:, -X:] = body[:, -X:] * (1 - w) + y[:, i0 + L - X:i0 + L] * w
game = np.concatenate([y[:, :i0], body], axis=1)
print('seam', np.abs(game[:, -1] - game[:, i0]).max(), 'true', np.abs(y[:, i0 + L] - y[:, i0 + L - 1]).max())
T = 'Take Your Time - Kokiri Haven Shop (placeholder v1)'
g32 = sg.resample_poly(game, 2, 3, axis=1)
i32, L32 = round(i0 * 2 / 3), round(L * 2 / 3)
MX.write_wav(f'out/{T}.wav', g32[:, :i32 + L32], 32000, 16, loop=(i32, i32 + L32))
print('32k loop', i32, i32 + L32)
pv = y[:, :i0 + 2 * L + 3 * S48].copy(); fl_ = 6 * S48; pv[:, -fl_:] *= np.linspace(1, 0, fl_) ** 1.5
MX.write_wav('out/tmp_preview.wav', pv, S48, 24)
