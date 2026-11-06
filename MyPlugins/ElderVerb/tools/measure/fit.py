"""モードごとの性格（Profile）を、本物との差が小さくなるように探す
使い方: python fit.py 1,2,3 [回数] [試す数]   → 作業場所の profiles/mode<k>.json に保存"""
import numpy as np, json, os, sys, subprocess, tempfile, time, pickle, functools
from scipy.optimize import minimize
import soundfile as sf
from meas import *

FLAT = dict(ColorMode=1, ModDepth=0, HighCut=1, BassMult=0.5, HighShelf=1, Decay=0.42, Size=1, Attack=0.5,
            EarlyDiffusion=1, LateDiffusion=1, PreDelay=0, Mix=1, LowCut=0, BassXover=0.381, HighFreq=0.5, ModRate=0.245)
CONFIGS = {
    "flat":  (dict(), 5),
    "size3": (dict(Size=0.3), 5),
    "dec3":  (dict(Decay=0.3), 3),
    "att0":  (dict(Attack=0.0), 4),
    "att1":  (dict(Attack=1.0), 4),
    "hs75":  (dict(HighShelf=0.75), 4),
    "dec6":  (dict(Decay=0.6), 12),
    "ed0":   (dict(EarlyDiffusion=0.0), 3),
}

def features(y):
    L, R = y[:, 0].astype(np.float64), y[:, 1].astype(np.float64)
    f = {}
    f["env"] = np.maximum(env_db(L + R, 10, 0.4), -40)
    f["envL"] = np.maximum(env_db(L, 5, 0.15), -40)
    f["envR"] = np.maximum(env_db(R, 5, 0.15), -40)
    f["ned"] = ned(L, 20, 20, 0.4)
    f["dc"] = decay_curve(L)
    f["rt"] = rt60(L)
    f["on"] = np.array([onset_ms(L), onset_ms(R)])
    f["E"] = np.array([energy_db(L), energy_db(R)])
    def sb(x, t0, t1):
        fr, p = spectrum_db(x, t0, t1, 4096)
        return np.array([10 * np.log10(np.mean(10 ** (p[(fr > fc / 2 ** (1 / 6)) & (fr < fc * 2 ** (1 / 6))] / 10)) + 1e-30) for fc in FREQS])
    f["sE"] = sb(L, 0, 0.12)
    f["sL"] = sb(L, 0.3, 1.5)
    f["corr"] = corr(L, R, 0.02, 1.0)
    return f

def ref_features(k):
    p = os.path.join(CACHE, f"reffeat_{k}.pkl")
    if os.path.exists(p): return pickle.load(open(p, "rb"))
    out = {}
    for name, (pp, tail) in CONFIGS.items():
        out[name] = features(ir(dict(FLAT, ReverbMode=k / 24, **pp), tail=tail))
    pickle.dump(out, open(p, "wb"))
    return out

MINE = os.path.join(D, "mine")
def render_mine(k, pp, tail, prof, tmpdir, inp=None):
    q = dict(FLAT, ReverbMode=k / 24, **pp)
    out = os.path.join(tmpdir, "o.wav")
    args = [MINE, inp or impulse_file("L"), out, str(tail)] + [f"{a}={b}" for a, b in q.items()] + [f"p_{a}={b}" for a, b in prof.items()]
    subprocess.run(args, check=True)
    y, _ = sf.read(out, dtype="float32")
    return y[int(SR * 0.1):] if inp is None else y

def diff(fr, fm, which):
    e = 0.0
    if "env" in which:
        e += np.mean(np.abs(fr["env"] - fm["env"])) * 1.0
        e += np.mean(np.abs(fr["envL"] - fm["envL"])) * 0.5 + np.mean(np.abs(fr["envR"] - fm["envR"])) * 0.5
    if "ned" in which: e += np.mean(np.abs(fr["ned"] - fm["ned"])) * 12
    if "on" in which:  e += np.sum(np.minimum(np.abs(fr["on"] - fm["on"]), 60)) * 0.4
    if "E" in which:   e += np.sum(np.abs(fr["E"] - fm["E"])) * 0.5
    if "dc" in which:
        r = np.log(np.maximum(fm["dc"], 0.5) / np.maximum(fr["dc"], 0.5))
        e += np.mean(np.abs(r)) * 25
    if "rt" in which:  e += abs(np.log(max(np.nan_to_num(fm["rt"], nan=0.05), 0.05) / max(np.nan_to_num(fr["rt"], nan=0.05), 0.05))) * 30
    if "spec" in which:
        for key in ("sE", "sL"):
            d = (fm[key] - fm[key].mean()) - (fr[key] - fr[key].mean())
            e += np.mean(np.abs(d)) * 0.6
    if "corr" in which: e += abs(np.nan_to_num(fr["corr"]) - np.nan_to_num(fm["corr"])) * 5
    return e

# 項目: (名前, 下限, 上限)
STRUCT = [("loopMs", 120, 800), ("sizeMin", 0.03, 0.6), ("inDiffMs", 2, 60), ("inDiffAmt", 0.0, 0.85), ("lateAmt", 0.0, 0.8),
          ("earlyMs", 0, 60), ("crossMs", 0, 200), ("crossDb", -20, 3), ("clusterMs", 5, 300), ("clusterDecayDb", -10, 50),
          ("clusterDb", -20, 15), ("clusterN", 1, 24), ("tankMs", 0, 300), ("attackMs", 0, 200), ("gainDb", -20, 20)]
DECAY = [("rtScale", 0.4, 3.0), ("hsScale", 0.02, 3.0), ("dampHz", 0, 20000), ("lowDb", -12, 12), ("midDb", -12, 12), ("highDb", -18, 12), ("gainDb", -20, 20)]
GATE = [("gateMs", 50, 2000), ("sizeMin", 0.03, 0.6), ("inDiffMs", 2, 60), ("inDiffAmt", 0.0, 0.85), ("earlyMs", 0, 60), ("crossDb", -20, 3),
        ("gainDb", -20, 20), ("lowDb", -12, 12), ("midDb", -12, 12), ("highDb", -18, 12), ("clusterDb", -20, 15)]

def init_from_ref(ref):
    """測った時刻から、時間に関する値の出発点と探す範囲を決める"""
    onL, onR = ref["flat"]["on"]
    v = dict(earlyMs=max(0.5, onL), crossMs=max(0.5, onR), inDiffMs=max(2.0, ref["ed0"]["on"][0] - onL),
             attackMs=max(0.0, ref["att1"]["on"][0] - onL))
    rng = {a: (max(0.0, b * 0.75 - 1), b * 1.25 + 2) for a, b in v.items()}
    return v, rng

def fit_stage(k, prof, plist, cfgs, which, ref, maxfev, tmpdir, log, rng=None):
    if rng: plist = [(p[0],) + rng[p[0]] if p[0] in rng else p for p in plist]
    lo = np.array([p[1] for p in plist], float); hi = np.array([p[2] for p in plist], float)
    x0 = np.array([(prof.get(p[0], (p[1] + p[2]) / 2) - p[1]) / (p[2] - p[1]) for p in plist]).clip(0, 1)
    best = [1e18, dict(prof)]
    def f(x):
        x = np.clip(x, 0, 1)
        q = dict(prof); q.update({p[0]: float(lo[i] + x[i] * (hi[i] - lo[i])) for i, p in enumerate(plist)})
        e = 0
        for c in cfgs:
            pp, tail = CONFIGS[c]
            try:
                d = diff(ref[c], features(render_mine(k, pp, tail, q, tmpdir)), which)
            except Exception as ex:
                log.write(f"    error {c}: {ex!r}\n"); log.flush(); d = 500.0
            e += d if np.isfinite(d) else 500.0
        if e < best[0]: best[0] = e; best[1] = q
        return e
    minimize(f, x0, method="Nelder-Mead", options=dict(maxfev=maxfev, xatol=1e-3, fatol=1e-3))
    log.write(f"  stage {which} -> {best[0]:.3f}\n"); log.flush()
    return best[1], best[0]

def fit_mode(k, rounds=2, fev=350):
    tmpdir = tempfile.mkdtemp(dir=os.path.join(D, "tmp"))
    log = open(os.path.join(D, "fitlog", f"mode{k}.txt"), "a")
    ref = ref_features(k)
    pf = os.path.join(D, "profiles", f"mode{k}.json")
    prof = (json.load(open(pf)) if os.path.exists(pf) else None) or {}
    gate = (k == 15)
    v0, rng = init_from_ref(ref)
    for a, b in v0.items(): prof.setdefault(a, b)
    log.write(f"mode {k} start {time.ctime()}\n"); log.flush()
    e = 0
    for r in range(rounds):
        if gate:
            prof, e = fit_stage(k, prof, GATE, ["flat", "size3", "att0", "att1"], ("env", "ned", "on", "E", "spec", "corr"), ref, fev * 2, tmpdir, log, rng)
        else:
            prof, e = fit_stage(k, prof, STRUCT, ["flat", "size3", "att0", "att1", "ed0"], ("env", "ned", "on", "E", "corr"), ref, fev * 2, tmpdir, log, rng)
            json.dump(prof, open(pf, "w"), indent=1)
            prof, e = fit_stage(k, prof, DECAY, ["flat", "dec3", "hs75", "dec6"], ("dc", "rt", "spec", "E"), ref, fev, tmpdir, log)
        json.dump(prof, open(pf, "w"), indent=1)
    log.write(f"mode {k} done {time.ctime()} err {e:.3f}\n"); log.close()
    return k, e

if __name__ == "__main__":
    for d in ("tmp", "fitlog", "profiles"): os.makedirs(os.path.join(D, d), exist_ok=True)
    modes = [int(a) for a in sys.argv[1].split(",")] if len(sys.argv) > 1 else list(range(1, 23))
    rounds = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    fev = int(sys.argv[3]) if len(sys.argv) > 3 else 350
    for k in modes: ref_features(k)   # 本物を先に全部（AU は 1 つずつ）
    from multiprocessing import Pool
    with Pool(min(10, len(modes))) as pool:
        for k, e in pool.imap_unordered(functools.partial(fit_mode, rounds=rounds, fev=fev), modes):
            print("done", k, e, flush=True)
