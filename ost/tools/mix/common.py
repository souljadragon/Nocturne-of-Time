import numpy as np, subprocess, glob
import scipy.signal as sg
SR = 48000
def load(f, sr=SR):
    raw = subprocess.run(['ffmpeg','-v','error','-i',f,'-f','f32le','-ac','2','-ar',str(sr),'-'],capture_output=True).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, 2).T.astype(np.float64)
EDGES = [20, 40, 63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000, 16000]
def ltas(x):
    m = x.mean(0); s = (x[0]-x[1])/2
    f, P = sg.welch(m, SR, nperseg=8192); _, S = sg.welch(s, SR, nperseg=8192)
    lv = []; wd = []
    for lo, hi in zip(EDGES[:-1], EDGES[1:]):
        k = (f >= lo) & (f < hi)
        lv.append(10*np.log10(P[k].sum()+1e-20)); wd.append(10*np.log10(S[k].sum()/P[k].sum()+1e-12))
    lv = np.array(lv); return lv - lv.max(), np.array(wd)
