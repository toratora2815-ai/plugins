"""測定の共通部品。作業場所は ~/plugins/build/ElderVerbFit（環境変数 EV_WORK で変えられる）
本物（AU）は auhost、自作エンジンは mine（tools/render.cpp をビルドしたもの）で書き出す"""
import numpy as np, soundfile as sf, subprocess, os, hashlib, json
from scipy import signal
SR = 48000
D = os.environ.get("EV_WORK", os.path.expanduser("~/plugins/build/ElderVerbFit"))
CACHE = os.path.join(D, "cache"); os.makedirs(CACHE, exist_ok=True)
HOST = os.path.join(D, "auhost")

BASE = dict(Mix=1, PreDelay=0, Decay=0.42, Size=1, Attack=0.5, BassMult=0.623, BassXover=0.381,
            HighShelf=0, HighFreq=0.5, EarlyDiffusion=1, LateDiffusion=1, ModRate=0.245, ModDepth=0.38,
            HighCut=0.59, LowCut=0, ColorMode=1/3, ReverbMode=1/24)

def impulse_file(kind="L"):
    p = os.path.join(CACHE, f"in_{kind}.wav")
    if not os.path.exists(p):
        x = np.zeros((int(SR * 0.6), 2), np.float32)
        n0 = int(SR * 0.1)
        if kind == "L": x[n0, 0] = 1
        elif kind == "R": x[n0, 1] = 1
        elif kind == "M": x[n0, :] = 1
        sf.write(p, x, SR, subtype="FLOAT")
    return p

def render_file(inp, params, tail, tag="ref"):
    pr = dict(BASE); pr.update(params)
    key = hashlib.md5(json.dumps([inp, sorted(pr.items()), tail, tag]).encode()).hexdigest()[:16]
    out = os.path.join(CACHE, f"{tag}_{key}.wav")
    if not os.path.exists(out):
        if tag == "ref":
            args = [HOST, "render", inp, out, str(tail)] + [f"{k}={v}" for k, v in pr.items()]
        else:
            args = [os.path.join(D, "mine"), inp, out, str(tail)] + [f"{k}={v}" for k, v in pr.items()]
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode: print(r.stdout, r.stderr); raise SystemExit
    y, sr = sf.read(out, dtype="float32")
    return y

def ir(params, tail=12, kind="L", tag="ref"):
    y = render_file(impulse_file(kind), params, tail, tag)
    return y[int(SR * 0.1):]   # インパルスの位置から

def truncate_floor(x):
    p = x.astype(np.float64) ** 2
    n = int(SR * 0.05)
    k = len(p) // n
    if k < 4: return x
    e = p[:k * n].reshape(k, n).mean(1)
    floor = np.median(e[-max(2, k // 10):])
    if floor <= 0: return x
    above = np.where(e > floor * 10)[0]
    end = (above[-1] + 1) * n if len(above) else n
    return x[:end]

def edc_db(x):
    e = np.cumsum((x.astype(np.float64) ** 2)[::-1])[::-1]
    return 10 * np.log10(e / max(e[0], 1e-30) + 1e-30)

def rt60(x, lo=-5, hi=-25):
    x = truncate_floor(x)
    d = edc_db(x)
    try:
        i0 = np.where(d <= lo)[0][0]; i1 = np.where(d <= hi)[0][0]
    except IndexError:
        return float("nan")
    if i1 - i0 < 4: return float("nan")
    t = np.arange(i0, i1) / SR
    a, b = np.polyfit(t, d[i0:i1], 1)
    return -60 / a

BANDS = [63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
def band(x, fc):
    lo, hi = fc / 2 ** 0.5, min(fc * 2 ** 0.5, SR / 2 * 0.95)
    sos = signal.butter(3, [lo, hi], btype="band", fs=SR, output="sos")
    return signal.sosfilt(sos, x)

def band_rt(x):
    return {fc: rt60(band(x, fc)) for fc in BANDS}

def onset_ms(x, thr_db=-40):
    a = np.abs(x); m = a.max()
    if not m > 0: return 500.0
    return np.where(a > m * 10 ** (thr_db / 20))[0][0] / SR * 1000

def env_db(x, win_ms=5, length_s=0.5):
    n = int(SR * win_ms / 1000); L = int(SR * length_s) // n
    e = np.array([np.sum(x[i * n:(i + 1) * n].astype(np.float64) ** 2) for i in range(L)])
    if not e.max() > 0: return np.full(L, -120.0)
    return 10 * np.log10(e / e.max() + 1e-12)

def spectrum_db(x, t0=0.05, t1=0.5, nfft=8192):
    seg = x[int(SR * t0):int(SR * t1)]
    f, p = signal.welch(seg, SR, nperseg=min(nfft, len(seg)))
    return f, 10 * np.log10(p + 1e-30)

def energy_db(x):
    return 10 * np.log10(np.sum(x.astype(np.float64) ** 2) + 1e-30)

def ned(x, win_ms=20, step_ms=10, length_s=0.5):
    # Abel & Huang の echo density（1 = ガウス雑音なみ）
    n = int(SR * win_ms / 1000); s = int(SR * step_ms / 1000); out = []
    w = np.hanning(n); w /= w.sum()
    for i in range(0, int(SR * length_s) - n, s):
        seg = x[i:i + n]; sd = np.sqrt(np.sum(w * seg ** 2))
        out.append(np.sum(w * (np.abs(seg) > sd)) / 0.3173 if sd > 0 else 0.0)
    return np.array(out)

def corr(a, b, t0=0.05, t1=1.0):
    a = a[int(SR * t0):int(SR * t1)].astype(np.float64); b = b[int(SR * t0):int(SR * t1)].astype(np.float64)
    den = np.sqrt(np.sum(a * a) * np.sum(b * b))
    return float(np.sum(a * b) / den) if den > 0 else 0.0

FREQS = np.array([50, 80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000, 12500, 16000, 19000])
def decay_curve(x, t0=0.15):
    """周波数ごとの減り方（dB/秒）。帯域ごとのエネルギーを後ろから積分（Schroeder）して -5〜-25 dB の傾きを測る"""
    x = truncate_floor(x)
    f, t, Z = signal.stft(x, SR, nperseg=4096, noverlap=3072)
    P = np.abs(Z) ** 2 + 1e-30
    i0 = np.searchsorted(t, t0)
    out = []
    for fc in FREQS:
        m = (f > fc / 2 ** (1 / 6)) & (f < fc * 2 ** (1 / 6))
        e = P[m].sum(0)[i0:]
        edc = np.cumsum(e[::-1])[::-1]; d = 10 * np.log10(edc / edc[0] + 1e-30)
        a_ = np.where(d <= -5)[0]; b_ = np.where(d <= -25)[0]
        if len(a_) == 0 or len(b_) == 0 or b_[0] - a_[0] < 2:
            out.append(60.0 / max(t[-1] - t0, 0.1) if len(b_) == 0 else 200.0); continue
        tt = t[i0:][a_[0]:b_[0]]; a, b = np.polyfit(tt, d[a_[0]:b_[0]], 1)
        out.append(-a)
    return np.array(out)

def summary(y, label=""):
    L, R = y[:, 0], y[:, 1]
    br = band_rt(L)
    return f"{label:28s} RT={rt60(L):6.2f}s  on={onset_ms(L):6.1f}/{onset_ms(R):6.1f}ms E={energy_db(L):6.1f}/{energy_db(R):6.1f}dB corr={corr(L, R):+.2f} | " + \
        " ".join(f"{v:5.2f}" for v in br.values())
