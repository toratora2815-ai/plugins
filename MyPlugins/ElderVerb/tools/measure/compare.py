"""本物と Elder Verb を同じ設定で鳴らして比べる
使い方: python compare.py au      … インストール済みのプラグイン本体（AU）と比べる
        python compare.py engine  … 作業場所の mine（エンジンだけ）と比べる"""
import numpy as np, os, sys, subprocess, json, hashlib, tempfile
import soundfile as sf
from meas import *
import fit
EV = {"Mix": "Mix", "PreDelay": "Pre-Delay", "Decay": "Decay", "Size": "Size", "Attack": "Attack", "BassMult": "Bass Mult",
      "BassXover": "Bass Xover", "HighShelf": "High Shelf", "HighFreq": "High Freq", "EarlyDiffusion": "Early Diffusion",
      "LateDiffusion": "Late Diffusion", "ModRate": "Mod Rate", "ModDepth": "Mod Depth", "HighCut": "High Cut", "LowCut": "Low Cut"}
def render_au(params, tail):
    pr = dict(BASE); pr.update(params)
    args = []
    for k, v in pr.items():
        if k == "ReverbMode": args.append(f"Reverb Mode=#{max(0, round(v * 24) - 1)}")
        elif k == "ColorMode": args.append(f"Color=#{0 if v < 0.6666 else (1 if v < 0.9999 else 2)}")
        else: args.append(f"{EV[k]}={v}")
    out = os.path.join(CACHE, "ev_" + hashlib.md5(json.dumps(args + [tail]).encode()).hexdigest()[:16] + ".wav")
    env = dict(os.environ, AU_SUB="Eldv", AU_MANU="Mypl")
    subprocess.run([HOST, "render", impulse_file("L"), out, str(tail)] + args, check=True, env=env, capture_output=True)
    return sf.read(out, dtype="float32")[0][int(SR * 0.1):]
def render_engine(params, tail):
    pr = dict(BASE); pr.update(params)
    td = tempfile.mkdtemp(dir=os.path.join(D, "tmp")); out = os.path.join(td, "e.wav")
    subprocess.run([os.path.join(D, "mine"), impulse_file("L"), out, str(tail)] + [f"{a}={b}" for a, b in pr.items()], check=True)
    return sf.read(out, dtype="float32")[0][int(SR * 0.1):]

SETTINGS = {   # 名前: 設定
    "初期値": dict(),
    "明るい": dict(ColorMode=1, HighShelf=1, HighCut=1, ModDepth=0),
    "短い・小さい": dict(Decay=0.3, Size=0.4),
}
if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "au"
    rend = render_au if which == "au" else render_engine
    sel = sys.argv[2] if len(sys.argv) > 2 else "初期値"
    pp = SETTINGS[sel]
    tot = []
    print(f"設定: {sel}   （左 = 本物 / 右 = Elder Verb）")
    for k in range(1, 23):
        p = dict(pp, ReverbMode=k / 24)
        a = ir(p, tail=10); b = rend(p, 10)
        fa, fb = fit.features(a), fit.features(b)
        m = FREQS <= 10000   # 70s は 12 kHz より上がほぼ無音（本物は -90 dB のノイズ）なので、聞こえる帯域で比べる
        dc = np.mean(np.abs(np.log(np.maximum(fb["dc"][m], .5) / np.maximum(fa["dc"][m], .5)))) * 100
        env = np.mean(np.abs(fa["env"] - fb["env"]))
        m = FREQS <= 10000   # 70s は 12 kHz より上がほぼ無音（本物は -90 dB のノイズ）なので、聞こえる帯域で比べる
        sp = np.mean(np.abs((fa["sL"][m] - fa["sL"][m].mean()) - (fb["sL"][m] - fb["sL"][m].mean())))
        tot.append((dc, env, sp))
        print(f"{k:2d} RT {fa['rt']:5.2f}/{fb['rt']:5.2f}s  最初の音 L {fa['on'][0]:5.1f}/{fb['on'][0]:5.1f} R {fa['on'][1]:5.1f}/{fb['on'][1]:5.1f}ms"
              f"  音量 {fa['E'][0]:5.1f}/{fb['E'][0]:5.1f}dB  減り方の差 {dc:4.0f}%  包絡の差 {env:4.1f}dB  音色の差 {sp:4.1f}dB", flush=True)
    t = np.array(tot); print("平均: 減り方の差 %.0f%%  包絡の差 %.1f dB  音色の差 %.1f dB" % tuple(t.mean(0)))
