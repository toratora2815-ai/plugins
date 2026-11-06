"""作業場所の profiles/*.json → Source/Profiles.h
使い方: python genprof.py ../../Source/Profiles.h"""
import json, os, sys
from meas import D
from fit import STRUCT, DECAY, GATE
names = ["Concert Hall", "Plate", "Room", "Chamber", "Random Space", "Chorus Space", "Ambience", "Bright Hall", "Sanctum", "Grainy Hall",
         "Grainy Plate", "Smooth Plate", "Smooth Room", "Smooth Space", "Gated", "Wild Chamber", "Wild Hall", "Wild Space", "Cathedral",
         "Grand Hall", "Vintage Chamber", "Vintage Hall"]
fields = sorted({p[0] for p in STRUCT + DECAY + GATE} | {"modMs", "modStyle"})
out = ["// 自動で作ったファイル（測って合わせたモードごとの性格）。手で直さない（tools/measure/genprof.py）",
       "// ElderEngine.h の中（Profile の後）から読み込む", "#pragma once", "inline void applyFittedProfiles (Profile* p)", "{"]
for k in range(1, 23):
    f = os.path.join(D, "profiles", f"mode{k}.json")
    if not os.path.exists(f): continue
    pr = json.load(open(f))
    fmt = lambda v: (s if any(c in (s := f"{v:.6g}") for c in ".e") else s + ".0") + "f"
    body = " ".join(f"q.{a} = {fmt(pr[a])};" for a in fields if a in pr)
    if k == 15: body = "q.kind = 1; " + body
    out.append(f"    {{ auto& q = p[{k - 1}]; {body} }}   // {names[k - 1]}")
out += ["}"]
open(sys.argv[1], "w").write("\n".join(out) + "\n")
print("\n".join(out))
