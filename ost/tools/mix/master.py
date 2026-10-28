import numpy as np, subprocess
from mix import *
from common import ltas, load, EDGES
SEC = lambda s: int(round(s * SR))
bus = np.load('out/bus_premaster.npy').astype(float)
ref = load('stems/SMT_IV_Battle_B2_reference.mp3')
r, _ = ltas(ref); s, _ = ltas(bus)
diff = s - r
# match the reference halfway, smoothly, capped at +/-3 dB (different song, so don't chase it fully)
centers = [np.sqrt(a * b) for a, b in zip(EDGES[:-1], EDGES[1:])]
corr = np.clip(-0.5 * (diff - np.median(diff)), -3, 3)
corr = np.convolve(np.pad(corr, 1, mode='edge'), [0.25, 0.5, 0.25], 'valid')
bands = [('peak', f, g, 1.0) for f, g in zip(centers, corr) if abs(g) > 0.4 and f < 15000]
print('master EQ', [(round(f), round(g, 1)) for _, f, g, _ in bands])
x = eq(bus, [('hp', 30, 0, 4), ('peak', 630, -1.5, 1.2)] + bands)
x = x / np.max(np.abs(x)) * 0.5
x, gr = compress(x, -19.5, 2.0, 0.03, 0.25, knee=8)        # glue
print('glue GR median/max dB', round(float(np.median(gr)), 1), round(float(gr.max()), 1))
np.save('out/master_pre_limit.npy', x.astype(np.float32))

def finish(x, target):
    g = 0.0
    for _ in range(6):
        y, lim_gr = limiter(x * 10 ** (g / 20), -1.0 if target < -12 else -1.6)
        l = lufs(y)
        g += target - l
        if abs(target - l) < 0.15:
            break
    return y, l, lim_gr

n66 = SEC(66)
for name, target in (('game', -14.0), ('ab', -9.5)):
    y, l, lg = finish(x, target)
    print(name, 'LUFS', round(l, 2), 'true peak', round(true_peak(y), 2), 'max limiter GR', round(lg, 1))
    if name == 'game':
        loop = np.concatenate([y[:, :SEC(6)], y[:, n66:n66 + SEC(60)]], axis=1)
        write_wav('out/hollow_husk_mix_48k24.wav', loop, 48000, 24, loop=(SEC(6), SEC(66)))
        l441 = sg.resample_poly(loop, 147, 160, axis=1)
        ls, le = round(6 * 44100), round(66 * 44100)
        write_wav('out/hollow_husk_mix_game_loop.wav', l441[:, :le], 44100, 16, loop=(ls, le))
        # seam check: last sample -> loop start vs. neighbouring steps
        print('seam step', np.abs(l441[:, le - 1] - l441[:, ls]).max(), 'typical', np.abs(np.diff(l441[:, ls:ls + 2000])).mean())
    pv = y[:, :n66 + SEC(60) + SEC(3)].copy()
    fl = SEC(8); pv[:, -fl:] *= np.linspace(1, 0, fl) ** 1.5
    write_wav(f'out/tmp_{name}.wav', pv, 48000, 24)
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', f'out/tmp_{name}.wav', '-ar', '44100', '-b:a', '320k',
                    f'out/hollow_husk_mix_preview_{"game_level" if name == "game" else "ref_level"}.mp3'])
