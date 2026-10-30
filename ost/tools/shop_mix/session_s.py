"""Take Your Time - Kokiri Haven Shop stem mix: a grimy breath (SMT grunge). Run: python3 session_d.py ['{"stem": target_lufs}']"""
import numpy as np, glob, json, sys
import scipy.signal as sg
from mix import *
from common import ltas, load, EDGES

BPM = 120
BEAT = 60 / BPM
INTRO = 8 * BEAT              # 4.0 s
LOOP = 128 * BEAT             # 64.0 s
SEC = lambda s: int(round(s * SR))
I0, L = SEC(INTRO), SEC(LOOP)
END = I0 + L

S = {f.split('/')[-1][:-4]: f for f in glob.glob('stems/*.wav')}

CH = {
    'drums':      dict(target=-18.0, eq=[('hp', 32, 0, 4), ('low', 70, -1.5), ('peak', 400, -2.0, 1.0), ('peak', 2500, 1.5, 0.9), ('lp', 11000, 0, 2)],
                       comp=(-20, 3, 0.008, 0.1), grunge=0.3, room=-15, rev=-26),
    'bass':       dict(target=-19.0, eq=[('hp', 32, 0, 4), ('peak', 250, -1.5, 1.0), ('lp', 5000, 0, 2)],
                       comp=(-18, 3, 0.005, 0.1), grit=-14, duck=0.3, mono=True),
    'e_piano':    dict(target=-18.0, eq=[('hp', 90, 0, 2), ('peak', 350, -2.0, 1.0), ('peak', 2200, 1.0, 1.0), ('lp', 7500, 0, 2)],
                       comp=(-20, 2, 0.01, 0.15), tape=True, width=1.2, rev=-15),
    'lead':       dict(target=-16.5, eq=[('hp', 200, 0, 2), ('peak', 600, -1.5, 1.0), ('peak', 2800, 1.5, 1.0)],
                       comp=(-20, 2.5, 0.01, 0.15), tape=True, rev=-13, dly=-19),
    'harpsichord': dict(target=-22.0, eq=[('hp', 150, 0, 2), ('peak', 3500, -1.5, 1.0), ('lp', 9000, 0, 2)],
                       pan=0.4, width=1.3, rev=-14),
    'harp_marimba': dict(target=-23.0, eq=[('hp', 200, 0, 2), ('lp', 8000, 0, 2)], pan=-0.35, rev=-12, dly=-20),
    'pad':        dict(target=-23.0, eq=[('hp', 150, 0, 4), ('lp', 6000, 0, 2)], width=1.5, rev=-16, duck=0.15),
    'ambience_sounds': dict(target=-25.0, eq=[('hp', 40, 0, 4)], width=1.3, rev=-14),
}
for k, v in (json.loads(sys.argv[1]) if len(sys.argv) > 1 else {}).items():
    CH[k]['target'] = v


def extend(x):
    x = np.pad(x, ((0, 0), (0, max(0, END - x.shape[1]))))[:, :END]
    return np.concatenate([x, x[:, I0:END], np.zeros((2, SEC(5)))], axis=1)


raw = {k: extend(load(f)) for k, f in S.items() if k in CH}
_t = np.arange(raw['drums'].shape[1]) / SR


def tape(x, wow=0.0008, flutter=0.00012):
    d = (0.004 + wow * np.sin(2 * np.pi * 0.55 * _t) + 0.5 * wow * np.sin(2 * np.pi * 0.23 * _t + 1)
         + flutter * np.sin(2 * np.pi * 6.3 * _t)) * SR
    idx = np.arange(len(_t)) - d
    return np.stack([np.interp(idx, np.arange(len(_t)), ch) for ch in x])


def grunge(x, amt):
    hold = 4                                     # 12 kHz sample-and-hold
    y = np.repeat(x[..., ::hold], hold, axis=-1)[..., :x.shape[-1]]
    y = np.round(y * 64) / 64                    # 7-bit
    y = np.tanh(y * 3) / 3
    return (1 - amt) * x + amt * y * (np.std(x) / (np.std(y) + 1e-12))
n = raw['drums'].shape[1]
kick, nk = kick_env(raw['drums'], n)
print('kicks found', nk)

hall = make_ir(2.2, 0.38, 20, 5500, 7)          # a concrete back room, not a cavern
room = make_ir(0.8, 0.1, 6, 8000, 8)
bus = np.zeros((2, n + SEC(6)))
rev_send, room_send, dly_send = (np.zeros((2, n)) for _ in range(3))
gains = {}
for k, c in CH.items():
    x = eq(raw[k], c['eq'])
    if 'comp' in c:
        x, _ = compress(x, *c['comp'])
    if c.get('tape'):
        x = tape(x)
    if 'grunge' in c:
        x = grunge(x, c['grunge'])
    if 'grit' in c:
        gr_ = eq(np.tanh(x / (np.max(np.abs(x)) + 1e-9) * 10), [('hp', 400, 0, 2), ('lp', 2500, 0, 2)])
        x = x + gr_ * np.std(x) / (np.std(gr_) + 1e-12) * 10 ** (c['grit'] / 20) * 3
    if c.get('mono'):
        x = np.stack([x.mean(0)] * 2)
    if 'width' in c:
        x = width(x, c['width'])
    if 'pan' in c:
        x = pan(x, c['pan'])
    if 'duck' in c:
        x = x * (1 - c['duck'] * kick)
    g = c['target'] - lufs(x, gated_active=True)
    x *= 10 ** (g / 20)
    gains[k] = round(float(g), 1)
    bus[:, :n] += x
    if 'rev' in c:
        rev_send += x * 10 ** (c['rev'] / 20)
    if 'room' in c:
        room_send += x * 10 ** (c['room'] / 20)
    if 'dly' in c:
        dly_send += x * 10 ** (c['dly'] / 20)
print('stem gains dB', gains)

for send, ir in ((rev_send, hall), (room_send, room)):
    w = reverb(send, ir)
    m = min(bus.shape[1], w.shape[1])
    bus[:, :m] += w[:, :m]
d = eq(pingpong(dly_send, BEAT * 0.75, 0.42, n_rep=8, lp_hz=3000), [('hp', 300, 0, 2)])
m = min(bus.shape[1], d.shape[1]); bus[:, :m] += d[:, :m]
bus = bus[:, :n]
# low end mono below 140 Hz
mid, side = (bus[0] + bus[1]) / 2, (bus[0] - bus[1]) / 2
side = eq(side, [('hp', 140, 0, 2)])
bus = np.stack([mid + side, mid - side])

# master: half-way tonal match to the reference, glue, limiter to -14 LUFS
ref = load('stems/ref.mp3')
r, _ = ltas(ref); s, _ = ltas(bus)
diff = s - r
diff[0] = np.median(diff)          # ignore the sub-30 Hz band (it's high-passed anyway)
centers = [np.sqrt(a * b) for a, b in zip(EDGES[:-1], EDGES[1:])]
corr = np.clip(-0.55 * (diff - np.median(diff)), -4, 4)
corr = np.convolve(np.pad(corr, 1, mode='edge'), [0.25, 0.5, 0.25], 'valid')
bands = [('peak', f, gg, 1.0) for f, gg in zip(centers, corr) if abs(gg) > 0.4 and 30 < f < 15000]
print('master EQ', [(round(f), round(float(gg), 1)) for _, f, gg, _ in bands])
x = eq(bus, [('hp', 34, 0, 4), ('peak', 42, -3.5, 1.0)] + bands)
x = np.tanh(x / np.max(np.abs(x)) * 1.3)          # tape saturation on the bus
x = x / np.max(np.abs(x)) * 0.5
x, gr = compress(x, -19, 2.0, 0.03, 0.25, knee=8)
g = 0
for _ in range(8):
    y, lg = limiter(x * 10 ** (g / 20), -1.3)
    l = lufs(y); g += -14 - l
    if abs(-14 - l) < 0.15:
        break
s2, sw = ltas(y); _, rw = ltas(ref)
print('LUFS %.2f  TP %.2f  glue GR median %.1f  limiter max %.1f' % (l, true_peak(y), np.median(gr), lg))
print('band ', ' '.join(f'{e:6d}' for e in EDGES[:-1]))
print('diff ', ' '.join(f'{a - b:6.1f}' for a, b in zip(s2, r)))
print('width', ' '.join(f'{a:6.1f}' for a in sw))
print('refw ', ' '.join(f'{a:6.1f}' for a in rw))
np.save('out/master48.npy', y.astype(np.float32))

# game loop: intro + pass-2 loop body, last 60 ms crossfaded into pass 1's ending
body = y[:, END:END + L].copy()
X = SEC(0.06)
w = np.sin(np.linspace(0, np.pi / 2, X)) ** 2
body[:, -X:] = body[:, -X:] * (1 - w) + y[:, END - X:END] * w
game = np.concatenate([y[:, :I0], body], axis=1)
print('seam', np.abs(game[:, -1] - game[:, I0]).max(), 'true', np.abs(y[:, END] - y[:, END - 1]).max())
write_wav('out/Take Your Time - Kokiri Haven Shop (beta) [master 48k 24-bit].wav', game, 48000, 24, loop=(I0, I0 + L))
g32 = sg.resample_poly(game, 2, 3, axis=1)
i32, l32 = round(I0 * 2 / 3), round(L * 2 / 3)
write_wav('out/Take Your Time - Kokiri Haven Shop (beta).wav', g32[:, :i32 + l32], 32000, 16, loop=(i32, i32 + l32))
print('32k loop', i32, i32 + l32)
pv = y[:, :END + L + SEC(3)].copy(); fl = SEC(8); pv[:, -fl:] *= np.linspace(1, 0, fl) ** 1.5
write_wav('out/tmp_preview.wav', pv, 48000, 24)
