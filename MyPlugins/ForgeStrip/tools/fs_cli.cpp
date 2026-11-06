// 測定・合わせ込み用: fs_cli in.wav out.f32 [sc=side.wav] [設定=値 ...]
//   in は 48 kHz ステレオ float32 / int16 の wav。out はステレオ float32（インターリーブ）の生データ
//   例: fs_cli burst.wav o.f32 on=comp1 comp1.thr=-40 comp1.ratio=30 comp1.autogain=0
// c++ -O2 -std=c++17 -I ../Source tools/fs_cli.cpp -o fs_cli
#include "../Source/dsp/Strip.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
#include <functional>

static std::vector<float> readWav (const char* p, int& ch)
{
    FILE* f = std::fopen (p, "rb"); if (! f) { std::fprintf (stderr, "cannot open %s\n", p); std::exit (1); }
    std::vector<unsigned char> d; unsigned char buf[65536]; size_t r;
    while ((r = std::fread (buf, 1, sizeof (buf), f)) > 0) d.insert (d.end(), buf, buf + r);
    std::fclose (f);
    size_t i = 12; int fmt = 1, bits = 16; ch = 2; std::vector<float> out;
    while (i + 8 <= d.size())
    {
        const std::string id (d.begin() + (long) i, d.begin() + (long) i + 4);
        const unsigned sz = d[i + 4] | (d[i + 5] << 8) | (d[i + 6] << 16) | ((unsigned) d[i + 7] << 24);
        if (id == "fmt ") { fmt = d[i + 8] | (d[i + 9] << 8); ch = d[i + 10]; bits = d[i + 22]; }
        if (id == "data")
        {
            const size_t n = sz / (size_t) (bits / 8);
            out.resize (n);
            for (size_t k = 0; k < n; ++k)
            {
                const unsigned char* q = &d[i + 8 + k * (size_t) (bits / 8)];
                if (fmt == 3 && bits == 32) { float v; std::memcpy (&v, q, 4); out[k] = v; }
                else if (bits == 16) out[k] = (short) (q[0] | (q[1] << 8)) / 32768.0f;
                else if (bits == 24) { int v = (q[0] << 8) | (q[1] << 16) | (q[2] << 24); out[k] = (float) v / 2147483648.0f; }
            }
            break;
        }
        i += 8 + sz + (sz & 1);
    }
    return out;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: fs_cli in.wav out.f32 [sc=file] [key=value...]\n"); return 1; }
    int ch = 2; auto in = readWav (argv[1], ch);
    const int n = (int) in.size() / ch;
    std::vector<float> L ((size_t) n), R ((size_t) n);
    for (int i = 0; i < n; ++i) { L[(size_t) i] = in[(size_t) (i * ch)]; R[(size_t) i] = in[(size_t) (i * ch + (ch > 1 ? 1 : 0))]; }
    std::vector<float> SL, SR;

    fs::StripSettings s;
    for (int m = 0; m < fs::numModules; ++m) s.on[m] = false;
    std::map<std::string, std::function<void (float)>> keys;
    auto band = [&] (const std::string& pre, auto* arr, auto f) { for (int b = 0; b < 3; ++b) f (pre + ".b" + std::to_string (b + 1) + ".", arr[b]); };
    for (int k = 0; k < 2; ++k)
    {
        auto& c = s.comp[k]; std::string p = "comp" + std::to_string (k + 1);
        keys[p + ".bands"] = [&c] (float v) { c.bands = (int) v; };
        keys[p + ".x1"] = [&c] (float v) { c.xover1 = v; }; keys[p + ".x2"] = [&c] (float v) { c.xover2 = v; };
        keys[p + ".autogain"] = [&c] (float v) { c.autoGain = v > 0.5f; };
        keys[p + ".autorel"] = [&c] (float v) { c.autoRelease = v > 0.5f; };
        keys[p + ".vintage"] = [&c] (float v) { c.vintage = v > 0.5f; };
        keys[p + ".detect"] = [&c] (float v) { c.detect = (int) v; };
        keys[p + ".punch"] = [&c] (float v) { c.punch = v > 0.5f; };
        keys[p + ".pamt"] = [&c] (float v) { c.band[0].punchAmount = v; }; keys[p + ".patt"] = [&c] (float v) { c.band[0].punchAttack = v; };
        keys[p + ".psus"] = [&c] (float v) { c.band[0].punchSustain = v; }; keys[p + ".pgain"] = [&c] (float v) { c.band[0].punchGain = v; };
        for (int b = 0; b < 3; ++b)
        {
            auto& bb = c.band[b]; std::string q = b == 0 ? p + "." : p + ".b" + std::to_string (b + 1) + ".";
            keys[q + "thr"] = [&bb] (float v) { bb.threshold = v; }; keys[q + "ratio"] = [&bb] (float v) { bb.ratio = v; };
            keys[q + "att"] = [&bb] (float v) { bb.attack = v; }; keys[q + "rel"] = [&bb] (float v) { bb.release = v; };
            keys[q + "knee"] = [&bb] (float v) { bb.knee = v; }; keys[q + "gain"] = [&bb] (float v) { bb.gain = v; };
        }
    }
    (void) band;
    auto& t = s.transient;
    keys["ts.att"] = [&t] (float v) { t.band[0].attack = v; }; keys["ts.sus"] = [&t] (float v) { t.band[0].sustain = v; };
    keys["ts.contour"] = [&t] (float v) { t.band[0].contour = (int) v; }; keys["ts.mode"] = [&t] (float v) { t.mode = (int) v; };
    auto& tm = fs::tsTune::mode (1);
    for (int m = 0; m < 3; ++m)
    {
        auto& md = fs::tsTune::mode (m); std::string p = "T.ts" + std::to_string (m) + ".";
        keys[p + "slowAtk"] = [&md] (float v) { md.slowAtk = v; }; keys[p + "holdRel"] = [&md] (float v) { md.holdRel = v; };
        keys[p + "attRange"] = [&md] (float v) { md.attackRange = v; }; keys[p + "susRange"] = [&md] (float v) { md.sustainRange = v; };
        keys[p + "slowRel"] = [&md] (float v) { md.slowRel = v; }; keys[p + "gainRel"] = [&md] (float v) { md.gainRel = v; };
    }
    (void) tm;
    auto& ngt = fs::tsTune::neg();
    keys["T.neg.slowAtk"] = [&ngt] (float v) { ngt.slowAtk = v; }; keys["T.neg.range"] = [&ngt] (float v) { ngt.attackRange = v; };
    keys["T.neg.gainRel"] = [&ngt] (float v) { ngt.gainRel = v; };
    auto& gt = s.gate.band[0];
    keys["gate.open"] = [&gt] (float v) { gt.open = v; }; keys["gate.close"] = [&gt] (float v) { gt.close = v; };
    keys["gate.ratio"] = [&gt] (float v) { gt.ratio = v; }; keys["gate.att"] = [&gt] (float v) { gt.attack = v; };
    keys["gate.rel"] = [&gt] (float v) { gt.release = v; }; keys["gate.hold"] = [&gt] (float v) { gt.hold = v; };
    auto& dn = s.density.band[0];
    keys["den.range"] = [&dn] (float v) { dn.range = v; }; keys["den.ratio"] = [&dn] (float v) { dn.ratio = v; };
    keys["den.thr"] = [&dn] (float v) { dn.threshold = v; }; keys["den.speed"] = [&dn] (float v) { dn.speed = v; };
    auto& cl = s.clipper.band[0];
    keys["clip.thr"] = [&cl] (float v) { cl.threshold = v; }; keys["clip.boost"] = [&cl] (float v) { cl.boost = v; };
    auto& ex = s.exciter.band[0];
    keys["exc.x"] = [&ex] (float v) { ex.x = v; }; keys["exc.y"] = [&ex] (float v) { ex.y = v; }; keys["exc.drive"] = [&ex] (float v) { ex.drive = v; };
    auto& sc = s.sculptor;
    keys["sc.target"] = [&sc] (float v) { sc.target = (int) v; }; keys["sc.amount"] = [&sc] (float v) { sc.amount = v; };
    keys["sc.tone"] = [&sc] (float v) { sc.tone = v; }; keys["sc.speed"] = [&sc] (float v) { sc.speed = v; };
    auto& um = s.unmask;
    keys["um.amount"] = [&um] (float v) { um.amount = v; }; keys["um.sens"] = [&um] (float v) { um.sensitivity = v; };
    auto& ut = fs::unmaskTune::t();
    keys["T.um.sensOffset"] = [&ut] (float v) { ut.sensOffset = v; }; keys["T.um.sensPerPct"] = [&ut] (float v) { ut.sensPerPct = v; };
    keys["T.um.slope"] = [&ut] (float v) { ut.slope = v; };
    auto& vt = fs::vintTune::t();
    keys["T.v.ka"] = [&vt] (float v) { vt.ka = v; }; keys["T.v.kr"] = [&vt] (float v) { vt.kr = v; };
    keys["T.v.kh"] = [&vt] (float v) { vt.kh = v; }; keys["T.v.w"] = [&vt] (float v) { vt.w = v; }; keys["T.v.ts"] = [&vt] (float v) { vt.ts = v; };
    keys["T.ar.lo"] = [] (float v) { fs::autoRelTune::lo() = v; }; keys["T.ar.tau"] = [] (float v) { fs::autoRelTune::tau() = v; };
    auto& agt = fs::autoGainTune::t();
    keys["T.ag.up1"] = [&agt] (float v) { agt.up1 = v; }; keys["T.ag.dn1"] = [&agt] (float v) { agt.dn1 = v; };
    keys["T.ag.up2"] = [&agt] (float v) { agt.up2 = v; }; keys["T.ag.dn2"] = [&agt] (float v) { agt.dn2 = v; };
    auto& pt = fs::punchTune::t();
    keys["T.p.ku"] = [&pt] (float v) { pt.ku = v; }; keys["T.p.kd"] = [&pt] (float v) { pt.kd = v; }; keys["T.p.ke"] = [&pt] (float v) { pt.ke = v; }; keys["T.p.kr"] = [&pt] (float v) { pt.kr = v; }; keys["T.p.ga"] = [&pt] (float v) { pt.gAtk = v; }; keys["T.p.kh"] = [&pt] (float v) { pt.kh = v; };
    auto& ppt = fs::ppTune::t();
    keys["T.pp.ref"] = [&ppt] (float v) { ppt.refMs = v; }; keys["T.pp.mid"] = [&ppt] (float v) { ppt.midMs = v; };
    keys["T.pp.ku"] = [&ppt] (float v) { ppt.ku = v; }; keys["T.pp.kd"] = [&ppt] (float v) { ppt.kd = v; }; keys["T.pp.kt"] = [&ppt] (float v) { ppt.kt = v; };
    keys["T.pp.up"] = [&ppt] (float v) { ppt.upMs = v; }; keys["T.pp.pol"] = [&ppt] (float v) { ppt.polish = v; };
    auto& ph = s.phase;
    keys["ph.rot"] = [&ph] (float v) { ph.rotation = v; }; keys["ph.delay"] = [&ph] (float v) { ph.delayMs = v; };
    for (int b = 0; b < 12; ++b)
    {
        auto& e = s.eq.band[b]; std::string p = "eq.b" + std::to_string (b + 1) + ".";
        keys[p + "on"] = [&e] (float v) { e.on = v > 0.5f; }; keys[p + "shape"] = [&e] (float v) { e.shape = (int) v; };
        keys[p + "freq"] = [&e] (float v) { e.freq = v; }; keys[p + "gain"] = [&e] (float v) { e.gain = v; }; keys[p + "q"] = [&e] (float v) { e.q = v; };
    }
    keys["eq.softsat"] = [&s] (float v) { s.eq.softSat = v > 0.5f; };
    for (int b = 0; b < 12; ++b) { auto& e = s.eq.band[b]; std::string p = "eq.b" + std::to_string (b + 1) + ".";
        keys[p + "dyn"] = [&e] (float v) { e.dynamic = v > 0.5f; }; keys[p + "up"] = [&e] (float v) { e.up = v > 0.5f; }; keys[p + "thr"] = [&e] (float v) { e.threshold = v; }; }
    keys["lim.on"] = [&s] (float v) { s.limiterOn = v > 0.5f; }; keys["lim.ceiling"] = [&s] (float v) { s.limiter.ceiling = v; };

    for (int a = 3; a < argc; ++a)
    {
        std::string kv = argv[a]; const auto eq = kv.find ('=');
        const std::string k = kv.substr (0, eq), v = kv.substr (eq + 1);
        if (k == "sc")
        {
            int c2 = 2; auto sv = readWav (v.c_str(), c2); SL.assign ((size_t) n, 0.0f); SR.assign ((size_t) n, 0.0f);
            for (int i = 0; i < n && i * c2 < (int) sv.size(); ++i) { SL[(size_t) i] = sv[(size_t) (i * c2)]; SR[(size_t) i] = sv[(size_t) (i * c2 + (c2 > 1 ? 1 : 0))]; }
            continue;
        }
        if (k == "on")
        {
            static const std::map<std::string, int> ids { { "sculptor", fs::ModSculptor }, { "eq", fs::ModEq }, { "comp1", fs::ModComp1 },
                { "exciter", fs::ModExciter }, { "ts", fs::ModTransient }, { "comp2", fs::ModComp2 }, { "gate", fs::ModGate },
                { "density", fs::ModDensity }, { "clipper", fs::ModClipper }, { "phase", fs::ModPhase }, { "unmask", fs::ModUnmask } };
            size_t p = 0;
            while (p <= v.size())
            {
                size_t q = v.find (',', p); if (q == std::string::npos) q = v.size();
                const auto it = ids.find (v.substr (p, q - p));
                if (it == ids.end()) { std::fprintf (stderr, "unknown module %s\n", v.substr (p, q - p).c_str()); return 2; }
                s.on[it->second] = true; p = q + 1;
            }
            continue;
        }
        const auto it = keys.find (k);
        if (it == keys.end()) { std::fprintf (stderr, "unknown key %s\n", k.c_str()); return 2; }
        it->second (std::stof (v));
    }
    s.comp[1].vintage = s.comp[1].vintage;   // そのまま
    fs::Strip strip;
    strip.prepare (48000.0, 512);
    strip.set (s);
    strip.process (L.data(), R.data(), n, SL.empty() ? nullptr : SL.data(), SR.empty() ? nullptr : SR.data());
    FILE* o = std::fopen (argv[2], "wb");
    for (int i = 0; i < n; ++i) { std::fwrite (&L[(size_t) i], 4, 1, o); std::fwrite (&R[(size_t) i], 4, 1, o); }
    std::fclose (o);
    std::printf ("latency %d\n", strip.latencyFor (s));
    return 0;
}
