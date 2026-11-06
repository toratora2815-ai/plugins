"""揺れの深さ（modMs）をモードごとに合わせる: 1 kHz の正弦を入れて、響きの中の音程のにじみ（spread）を比べる
使い方: python fitmod.py 1,2,3"""
import numpy as np, json, os, sys, subprocess, tempfile
import soundfile as sf
from scipy import signal
from scipy.optimize import minimize_scalar
from meas import *
MINE = os.path.join(D, "mine")
FLAT = dict(ColorMode=1, HighCut=1, BassMult=0.5, HighShelf=1, Decay=0.5, Size=1, Attack=0.5, EarlyDiffusion=1, LateDiffusion=1)
# 揺れの種類: 0 = COLOR で決まる（80s ランダム / NOW サイン）, 1 = いつもランダム, 3 = 強いコーラス
STYLE = {5: 1, 6: 3, 14: 1, 16: 1, 17: 1, 18: 1}
def sine_file():
    p = os.path.join(CACHE, "in_sine1k_long.wav")
    if not os.path.exists(p):
        t = np.arange(int(SR * 4)) / SR; x = np.zeros((int(SR * 4.1), 2), np.float32); x[:len(t), 0] = 0.3 * np.sin(2 * np.pi * 1000 * t); x[:, 1] = x[:, 0]
        sf.write(p, x, SR, subtype="FLOAT")
    return p
def spread(y):
    s = y[int(1.0 * SR):int(4.0 * SR), 0]
    f, P = signal.welch(s, SR, nperseg=32768); m = (f > 900) & (f < 1100); P = P[m]; f = f[m]
    w = P / P.sum(); mu = (w * f).sum(); return float(np.sqrt((w * (f - mu) ** 2).sum()))
CFG = [dict(ModDepth=0.38, ModRate=0.245), dict(ModDepth=1.0, ModRate=0.245), dict(ModDepth=0.5, ModRate=0.75), dict(ModDepth=0.38, ModRate=0.245, ColorMode=0.8)]
def fit(k):
    pf = os.path.join(D, "profiles", f"mode{k}.json"); prof = json.load(open(pf))
    prof["modStyle"] = float(STYLE.get(k, 0))
    if k == 15:   # ゲートには揺れがない
        json.dump(prof, open(pf, "w"), indent=1); return k, 0, [], []
    ref = [spread(render_file(sine_file(), dict(FLAT, ReverbMode=k / 24, **c), 0.1)) for c in CFG]
    td = tempfile.mkdtemp(dir=os.path.join(D, "tmp"))
    def mine(ms):
        q = dict(prof, modMs=ms); out = []
        for c in CFG:
            pp = dict(BASE); pp.update(FLAT); pp.update(c); pp["ReverbMode"] = k / 24
            o = os.path.join(td, "s.wav")
            subprocess.run([MINE, sine_file(), o, "0.1"] + [f"{a}={b}" for a, b in pp.items()] + [f"p_{a}={b}" for a, b in q.items()], check=True)
            out.append(spread(sf.read(o, dtype="float32")[0]))
        return out
    loss = lambda lm: sum(abs(np.log(a / b)) for a, b in zip(mine(np.exp(lm)), ref))
    r = minimize_scalar(loss, bounds=(np.log(0.02), np.log(8)), method="bounded", options=dict(xatol=0.02))
    prof["modMs"] = float(np.exp(r.x)); json.dump(prof, open(pf, "w"), indent=1)
    return k, prof["modMs"], ref, mine(prof["modMs"])
if __name__ == "__main__":
    from multiprocessing import Pool
    os.makedirs(os.path.join(D, "tmp"), exist_ok=True)
    modes = [int(a) for a in sys.argv[1].split(",")]
    for k in modes:
        if k != 15:
            for c in CFG: render_file(sine_file(), dict(FLAT, ReverbMode=k / 24, **c), 0.1)
    with Pool(min(10, len(modes))) as pool:
        for k, ms, ref, mm in pool.imap_unordered(fit, modes):
            print(k, f"modMs={ms:.3f}", "ref", np.round(ref, 2), "mine", np.round(mm, 2), flush=True)
