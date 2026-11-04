// テスト用：Lumen Rooms のエンジンだけで音を書き出す（JUCE なし）
// 使い方: render_mine out.wav 秒 L|R|S1000|F:in.wav key=value ...
#include "../Source/RoomEngine.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
using namespace lumen;
static void writeWav (const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path, "wb"); uint32_t n = (uint32_t) L.size(), data = n * 8;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + data); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (3); w16 (2); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 8); w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (data);
    for (uint32_t i = 0; i < n; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}
static bool readWavF32 (const char* path, std::vector<float>& L, std::vector<float>& R)
{   // float32 ステレオの WAV（afconvert -f WAVE -d LEF32 で作る）
    FILE* f = std::fopen (path, "rb"); if (! f) return false;
    char id[4]; uint32_t sz; std::fseek (f, 12, SEEK_SET); int ch = 2;
    while (std::fread (id, 1, 4, f) == 4 && std::fread (&sz, 4, 1, f) == 1)
    {
        if (! std::memcmp (id, "fmt ", 4)) { uint16_t fmt, c; std::fread (&fmt, 2, 1, f); std::fread (&c, 2, 1, f); ch = c; std::fseek (f, sz - 4, SEEK_CUR); }
        else if (! std::memcmp (id, "data", 4)) { size_t n = sz / 4 / (size_t) ch; L.resize (n); R.resize (n);
            std::vector<float> b (n * (size_t) ch); std::fread (b.data(), 4, b.size(), f);
            for (size_t i = 0; i < n; ++i) { L[i] = b[i * (size_t) ch]; R[i] = b[i * (size_t) ch + (size_t) (ch > 1)]; } break; }
        else std::fseek (f, sz + (sz & 1), SEEK_CUR);
    }
    std::fclose (f); return true;
}
int main (int argc, char** argv)
{
    const int sr = 48000; double secs = atof (argv[2]); std::string side = argv[3];
    std::map<std::string, float> kv;
    for (int i = 4; i < argc; ++i) { std::string a = argv[i]; auto p = a.find ('='); kv[a.substr (0, p)] = (float) atof (a.substr (p + 1).c_str()); }
    auto get = [&] (const char* k, float d) { auto it = kv.find (k); return it == kv.end() ? d : it->second; };

    ReflParams rp; rp.pattern = (int) get ("pattern", 0); rp.size = get ("size", 0.5f); rp.proximity = (int) get ("prox", 2);
    rp.reflectivity = get ("reflectivity", 0.5f); rp.diffusion = get ("diffusion", 5); rp.rolloffHz = get ("erRoll", 6000); rp.rolloffSlope = (int) get ("erSlope", 1);
    rp.xfeedDb = get ("erXfDb", 0); rp.xfeedMs = get ("erXfMs", 0); rp.xfeedRollHz = get ("erXfRoll", 18000);
    TailParams tp; tp.timeSec = get ("time", 3.29f); tp.bloom = get ("bloom", 2.5f); tp.bassFreq = get ("bassF", 555); tp.bassMult = get ("bassX", 1.0f);
    tp.trebFreq = get ("trebF", 4300); tp.trebMult = get ("trebX", 1.0f); tp.lowBoostDb = get ("boost", -15); tp.lowBoostWeight = get ("weight", 0.5f);
    tp.predelayMs = get ("predelay", 10); tp.echoDb = get ("echo", -100); tp.echoMs = get ("echoMs", 100); tp.echoRollHz = get ("echoRoll", 18000);
    tp.rolloffHz = get ("roll", 6000); tp.rolloffSlope = (int) get ("slope", 3); tp.lowCutHz = get ("lowcut", 20);
    tp.undDepth = get ("und", 0.25f); tp.undShape = get ("undShape", 0.25f); tp.modDepth = get ("mod", 0.75f); tp.modRateHz = get ("modRate", 0.7f);
    tp.xfeedDb = get ("xfDb", -3); tp.xfeedMs = get ("xfMs", 10); tp.xfeedRollHz = get ("xfRoll", 18000);
    MixParams mp; mp.dryWet = get ("mix", 1); mp.reflRev = get ("balance", 0.5f); mp.reflWidth = get ("erWidth", 1.125f); mp.revWidth = get ("width", 1);

    RoomEngine eng; eng.prepare (sr, 512);
    for (int s = 0; s < 2; ++s)
    {
        auto& t = eng.tailAt (s).tune;
        t.tapMinMs = get ("t_min", t.tapMinMs); t.tapSpanMs = get ("t_span", t.tapSpanMs); t.tapSpanPerBloom = get ("t_spb", t.tapSpanPerBloom);
        t.tapCurve = get ("t_curve", t.tapCurve); t.tapFall = get ("t_fall", t.tapFall); t.earlyGain = get ("t_early", t.earlyGain);
        t.modMaxMs = get ("t_mod", t.modMaxMs); t.bassFc = get ("t_bfc", t.bassFc); t.bassK = get ("t_bk", t.bassK); t.levelOffsetDb = get ("t_lvl", t.levelOffsetDb);
    }
    eng.setTail (tp); eng.setMix (mp);
    std::vector<float> own, cross; buildReflections (rp, sr, own, cross);

    size_t n = (size_t) (secs * sr);
    std::vector<float> L (n, 0), R (n, 0);
    const size_t pre = 4800;
    if (side == "L" || side == "B") L[pre] = 1;
    if (side == "R" || side == "B") R[pre] = 1;
    if (side[0] == 'S') { float f = (float) atof (side.c_str() + 1); for (size_t i = 0; i < std::min (n, (size_t) 96000); ++i) L[i] = R[i] = 0.25f * std::sin (2 * kPi * f * i / sr); }
    if (side.rfind ("F:", 0) == 0) { std::vector<float> a, b; readWavF32 (side.c_str() + 2, a, b); for (size_t i = 0; i < std::min (n, a.size()); ++i) { L[i] = a[i]; R[i] = b[i]; } }

    // 反射（疎な直接畳み込み）
    std::vector<float> erL (n, 0), erR (n, 0);
    std::vector<std::pair<int, float>> so, sc;
    for (size_t i = 0; i < own.size(); ++i) { if (own[i] != 0) so.push_back ({ (int) i, own[i] }); if (cross[i] != 0) sc.push_back ({ (int) i, cross[i] }); }
    for (size_t i = 0; i < n; ++i)
    {
        if (L[i] != 0) { for (auto& t : so) if (i + t.first < n) erL[i + t.first] += L[i] * t.second; for (auto& t : sc) if (i + t.first < n) erR[i + t.first] += L[i] * t.second; }
        if (R[i] != 0) { for (auto& t : so) if (i + t.first < n) erR[i + t.first] += R[i] * t.second; for (auto& t : sc) if (i + t.first < n) erL[i + t.first] += R[i] * t.second; }
    }
    for (size_t s = 0; s < n; s += 512) { int k = (int) std::min ((size_t) 512, n - s); eng.process (L.data() + s, R.data() + s, erL.data() + s, erR.data() + s, k); }
    // 測定側（AU ホストは 128 サンプル早く出てくる）と位置を合わせる
    std::vector<float> oL (n, 0), oR (n, 0);
    for (size_t i = 0; i + 128 < n; ++i) { oL[i] = L[i + 128]; oR[i] = R[i + 128]; }
    writeWav (argv[1], oL, oR, sr);
    return 0;
}
