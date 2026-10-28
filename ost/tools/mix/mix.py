"""Mix the user's Hollow Husk stems. 48 kHz float internally."""
import numpy as np, scipy.signal as sg, scipy.ndimage as nd, struct, json, sys
from common import load, ltas, EDGES, SR

BPM = 160
BEAT = 60 / BPM
LOOP_START, LOOP_END = 6.0, 66.0          # seconds (bar 5 .. bar 45)
rng = np.random.default_rng(3)


# ------------------------------------------------------------------ dsp
def biquad(kind, f0, gain_db=0.0, q=0.707):
    A = 10 ** (gain_db / 40)
    w = 2 * np.pi * f0 / SR
    c, s = np.cos(w), np.sin(w)
    al = s / (2 * q)
    if kind == 'peak':
        b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]
    elif kind == 'low':      # low shelf
        sq = 2 * np.sqrt(A) * al
        b = [A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq)]
        a = [(A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq]
    elif kind == 'high':     # high shelf
        sq = 2 * np.sqrt(A) * al
        b = [A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq)]
        a = [(A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq]
    b, a = np.array(b) / a[0], np.array(a) / a[0]
    return b, a


def eq(x, bands):
    for kind, f, g, *q in bands:
        if kind == 'hp':
            x = sg.sosfiltfilt(sg.butter(q[0] if q else 2, f, 'high', fs=SR, output='sos'), x, axis=-1)
        elif kind == 'lp':
            x = sg.sosfiltfilt(sg.butter(q[0] if q else 2, f, 'low', fs=SR, output='sos'), x, axis=-1)
        else:
            b, a = biquad(kind, f, g, q[0] if q else 0.707)
            x = sg.lfilter(b, a, x, axis=-1)
    return x


def follower(level, att, rel):
    """One-pole attack/release smoothing of a (non-negative) control signal."""
    out = np.empty_like(level)
    a_att, a_rel = np.exp(-1 / (att * SR)), np.exp(-1 / (rel * SR))
    # vectorised-ish: process in python but on decimated signal for speed
    y = 0.0
    for i, v in enumerate(level):
        a = a_att if v > y else a_rel
        y = a * y + (1 - a) * v
        out[i] = y
    return out


def compress(x, thr_db, ratio, att=0.005, rel=0.12, makeup=0.0, knee=6.0, side=None):
    det = np.max(np.abs(x if side is None else side), axis=0)
    dec = 16
    lvl = det.reshape(-1)[: len(det) // dec * dec].reshape(-1, dec).max(1)
    env = follower(lvl, att, rel)
    env = np.repeat(env, dec)
    env = np.pad(env, (0, len(det) - len(env)), mode='edge')
    l = 20 * np.log10(env + 1e-9)
    over = l - thr_db
    gr = np.where(over <= -knee / 2, 0,
                  np.where(over >= knee / 2, over * (1 - 1 / ratio),
                           (1 - 1 / ratio) * (over + knee / 2) ** 2 / (2 * knee)))
    return x * 10 ** ((makeup - gr) / 20), gr


def pan(x, p):
    """x stereo; balance-pan by p in -1..1 (constant power on the mono-ish image)."""
    a = (p + 1) * np.pi / 4
    gl, gr = np.cos(a) * np.sqrt(2), np.sin(a) * np.sqrt(2)
    return np.stack([x[0] * gl, x[1] * gr])


def width(x, w, low_mono=150):
    m = (x[0] + x[1]) / 2
    s = (x[0] - x[1]) / 2 * w
    s = sg.sosfiltfilt(sg.butter(2, low_mono, 'high', fs=SR, output='sos'), s)
    return np.stack([m + s, m - s])


def double(x, ms=16.0, depth_ms=0.35, rate=0.27):
    """Fake second guitar take: modulated short delay. Hard L/R with the original."""
    m = x.mean(0)
    n = len(m)
    t = np.arange(n)
    d = (ms + depth_ms * np.sin(2 * np.pi * rate * t / SR) + 0.2 * np.sin(2 * np.pi * 0.11 * t / SR)) * SR / 1000
    idx = t - d
    y = np.interp(idx, t, m, left=0)
    y = eq(y, [('peak', 1800, 1.5, 1.0), ('peak', 350, -1.0, 1.0)])   # different 'mic' colour
    return m, y


def kick_env(drums, n):
    low = sg.sosfiltfilt(sg.butter(4, 110, 'low', fs=SR, output='sos'), drums.mean(0))
    e = np.abs(sg.hilbert(low))
    pk, _ = sg.find_peaks(e, height=np.percentile(e, 99) * 0.35, distance=int(0.08 * SR))
    imp = np.zeros(n); imp[pk[pk < n]] = 1
    k = np.exp(-np.arange(int(0.4 * SR)) / (0.09 * SR))
    k[: int(0.004 * SR)] = np.linspace(0, 1, int(0.004 * SR))
    return np.clip(sg.oaconvolve(imp, k)[:n], 0, 1), len(pk)


def make_ir(seconds, tau, pre_ms, lp_hz, seed):
    r = np.random.default_rng(seed)
    L = int(seconds * SR)
    t = np.arange(L) / SR
    out = []
    for ch in range(2):
        x = r.normal(size=L) * np.exp(-t / tau)
        x = sg.sosfilt(sg.butter(2, lp_hz, 'low', fs=SR, output='sos'), x)
        x = sg.sosfilt(sg.butter(2, 250, 'high', fs=SR, output='sos'), x)
        x[: int(pre_ms / 1000 * SR)] = 0
        out.append(x / np.sqrt(np.sum(x ** 2)))
    return out


def reverb(x, ir):
    return np.stack([sg.oaconvolve(x.mean(0), ir[0]), sg.oaconvolve(x.mean(0), ir[1])])


def pingpong(x, t_s, fb, n_rep=6, lp_hz=3500):
    m = x.mean(0)
    D = int(t_s * SR)
    L = len(m) + D * n_rep
    out = np.zeros((2, L))
    sig = m.copy()
    for k in range(1, n_rep + 1):
        sig = sg.sosfilt(sg.butter(1, lp_hz, 'low', fs=SR, output='sos'), sig) * fb
        out[k % 2, k * D: k * D + len(sig)] += sig
    return out


# K-weighted loudness (BS.1770 @48k)
KB1, KA1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1, -1.69065929318241, 0.73248077421585]
KB2, KA2 = [1, -2, 1], [1, -1.99004745483398, 0.99007225036621]


def lufs(x, gated_active=False):
    y = sg.lfilter(KB2, KA2, sg.lfilter(KB1, KA1, x, axis=-1), axis=-1)
    blk, hop = int(0.4 * SR), int(0.1 * SR)
    p = np.array([np.mean(y[:, i:i + blk] ** 2, axis=1).sum() for i in range(0, y.shape[1] - blk, hop)])
    l = -0.691 + 10 * np.log10(p + 1e-20)
    g = l > -70
    if gated_active:      # loudness while the part is actually playing
        g &= l > l.max() - 20
    else:
        rel = -0.691 + 10 * np.log10(p[g].mean()) - 10
        g &= l > rel
    return -0.691 + 10 * np.log10(p[g].mean())


def true_peak(x):
    return 20 * np.log10(np.abs(sg.resample_poly(x, 4, 1, axis=-1)).max())


def limiter(x, ceiling_db=-1.0, look_ms=1.5, rel=0.08):
    c = 10 ** (ceiling_db / 20)
    over = sg.resample_poly(np.max(np.abs(x), axis=0), 4, 1)
    over = over.reshape(-1, 4).max(1)[: x.shape[1]]
    need = np.minimum(1, c / np.maximum(over, 1e-9))
    L = int(look_ms / 1000 * SR)
    need = nd.minimum_filter1d(need, 2 * L + 1)
    # release smoothing (instant attack) on decimated control
    dec = 8
    nd_ = need[: len(need) // dec * dec].reshape(-1, dec).min(1)
    g = np.empty_like(nd_); y = 1.0; a = np.exp(-1 / (rel * SR / dec))
    for i, v in enumerate(nd_):
        y = v if v < y else a * y + (1 - a) * v
        g[i] = y
    g = np.pad(np.repeat(g, dec), (0, x.shape[1] - len(g) * dec), mode='edge')
    g = nd.uniform_filter1d(g, L)      # smooth the corners
    g = np.minimum(g, need)
    return x * g, 20 * np.log10(g.min())


def write_wav(path, st, sr, bits, loop=None):
    if bits == 24:
        q = np.clip(st.T, -1, 1 - 2 ** -23) * 2 ** 23
        q = np.round(q).astype('<i4').reshape(-1)
        data = np.frombuffer(q.tobytes(), np.uint8).reshape(-1, 4)[:, :3].tobytes()
    else:
        tpdf = (rng.random(st.shape) - rng.random(st.shape)) / 32768
        data = (np.clip(st + tpdf, -1, 1 - 1 / 32768).T * 32767).round().astype('<i2').tobytes()
    ba = 2 * bits // 8
    chunks = b'fmt ' + struct.pack('<IHHIIHH', 16, 1, 2, sr, sr * ba, ba, bits)
    chunks += b'data' + struct.pack('<I', len(data)) + data + (b'\0' if len(data) % 2 else b'')
    if loop:
        smpl = struct.pack('<9I', 0, 0, int(1e9 / sr), 60, 0, 0, 0, 1, 0) + struct.pack('<6I', 0, 0, loop[0], loop[1] - 1, 0, 0)
        chunks += b'smpl' + struct.pack('<I', len(smpl)) + smpl
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 4 + len(chunks)) + b'WAVE' + chunks)
