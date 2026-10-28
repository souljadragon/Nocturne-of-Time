import sys, json
sys.path.insert(0, '/home/claude/hollow_husk'); sys.path.insert(0, '/home/claude/mix')
import numpy as np, scipy.signal as sg
import render as R, compose_d as C, mix as MX
from common import ltas, load, EDGES
R.BPM = C.BPM; R.SPB = R.SR * 60 / C.BPM
P = dict(np.load('out/parts.npz'))
G = dict(piano=-2, organ=-5, arp=-9, flute=-3, wbass=-4.5, bass=-3, pad=-12, kit=-5, shaker=-21, whisper=-10, tree=-12)
G.update(json.loads(sys.argv[1]) if len(sys.argv) > 1 else {})
nm, pan, db, lp, hp = R.norm, R.pan, R.db, R.lp, R.hp
SR = R.SR
pno = lp(P['piano'], 3200, 2)
wh = np.stack([lp(hp(c, 300), 5500, 2) for c in P['whisper']])
shk = lp(P['shaker'], 9000)
org = np.stack([lp(c, 3500) for c in P['organ']])
wb = P['wbass']
ORG = lambda g: np.stack([nm(org[0], .1), nm(org[1], .1)]) * db(g)
retro = (pan(nm(pno, .1) * db(G['piano']), -0.1) + ORG(G['organ']) + pan(nm(P['arp'], .1) * db(G['arp']), 0.35)
         + pan(nm(P['flute'], .1) * db(G['flute']), 0.1) + pan(nm(wb, .1) * db(G['wbass']), 0))
retro = 0.5 * retro + 0.5 * R.sfc_process(retro)
from render_d import tempo_echo
retro = tempo_echo(retro, mix=0.18)
modern = (pan(nm(P['bass'], .1) * P['duck'] * db(G['bass']), 0) + pan(nm(P['pad'], .1) * db(G['pad']), 0)
          + pan(nm(P['kit'], .1) * db(G['kit']), 0) + pan(nm(shk, .1) * db(G['shaker']), 0.3)
          + nm(wh, .1) * db(G['whisper']) + pan(nm(P['tree'], .1) * db(G['tree']), -0.1))
irs = R.make_ir(3.2, 0.55)
send = (nm(wh, .1) * db(G['whisper'] + 3) + pan(nm(pno, .1) * db(-14), 0) + ORG(-12) + pan(nm(P['flute'], .1) * db(-10), 0)
        + pan(nm(P['tree'], .1) * db(-10), 0) + pan(nm(P['arp'], .1) * db(-12), 0) + pan(nm(P['kit'], .1) * db(-22), 0))
wet = R.reverb(send, irs)
wet = np.stack([lp(c, 5000) for c in wet])
mix = retro + modern + wet
# keep the low end mono below 150 Hz
m, s = (mix[0] + mix[1]) / 2, (mix[0] - mix[1]) / 2
s = hp(s, 150, 2)
mix = np.stack([m + s, m - s])
m48 = sg.resample_poly(mix, 160, 147, axis=1)
# warm tilt toward the reference, half-way, like the stem mix
ref = load('ref/ref.mp3')
r, _ = ltas(ref); sp, _ = ltas(m48)
diff = sp - r
centers = [np.sqrt(a * b) for a, b in zip(EDGES[:-1], EDGES[1:])]
corr = np.clip(-0.6 * (diff - np.median(diff)), -4, 4)
corr = np.convolve(np.pad(corr, 1, mode='edge'), [0.25, 0.5, 0.25], 'valid')
bands = [('peak', f, g, 0.9) for f, g in zip(centers, corr) if abs(g) > 0.4 and 30 < f < 15000]
x = MX.eq(m48, [('hp', 30, 0, 4)] + bands)
x = x / np.max(np.abs(x)) * 0.5
x, gr = MX.compress(x, -19, 2.0, 0.03, 0.3, knee=8)
g = 0
for _ in range(8):
    y, _ = MX.limiter(x * 10 ** (g / 20), -1.3)
    l = MX.lufs(y); g += -14 - l
    if abs(-14 - l) < 0.15: break
s2, sw = ltas(y)
print('LUFS %.2f TP %.2f' % (l, MX.true_peak(y)))
print('diff ', ' '.join(f'{a-b:6.1f}' for a, b in zip(s2, r)))
print('width', ' '.join(f'{a:6.1f}' for a in sw))
np.save('out/master48.npy', y.astype(np.float32))
S48 = 48000
i0 = round(C.INTRO_BEATS * 60 / C.BPM * S48); L = round(C.LOOP_BEATS * 60 / C.BPM * S48)
body = y[:, i0 + L:i0 + 2 * L].copy()
X = int(0.06 * S48)          # crossfade the last 60 ms into pass 1's ending, which flows straight into the loop start
w = np.sin(np.linspace(0, np.pi / 2, X)) ** 2
body[:, -X:] = body[:, -X:] * (1 - w) + y[:, i0 + L - X:i0 + L] * w
game = np.concatenate([y[:, :i0], body], axis=1)
print('seam check', np.abs(game[:, -1] - game[:, i0]).max(), 'true', np.abs(y[:, i0 + L] - y[:, i0 + L - 1]).max())
MX.write_wav('out/hollowed_husk_master_48k24.wav', game, S48, 24, loop=(i0, i0 + L))
g32 = sg.resample_poly(game, 2, 3, axis=1)
i32, L32 = round(i0 * 2 / 3), round(L * 2 / 3)
MX.write_wav('out/hollowed_husk_game_loop_32k.wav', g32[:, :i32 + L32], 32000, 16, loop=(i32, i32 + L32))
print('32k loop', i32, i32 + L32)
pv = y[:, :i0 + 2 * L + 3 * S48].copy(); fl_ = 8 * S48; pv[:, -fl_:] *= np.linspace(1, 0, fl_) ** 1.5
MX.write_wav('out/tmp_preview.wav', pv, S48, 24)
