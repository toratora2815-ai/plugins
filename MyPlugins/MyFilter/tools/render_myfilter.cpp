// 数値テスト用: My Filter のエンジン（JUCE なし）でファイルを処理する
//   render_myfilter in.f32 out.f32 bpm key=value ...   （48 kHz ステレオ、float の生データ）
//   key: in out mix style filters par freq1 res1 shape1 poles1 gain1 fmod1 rmod1 lmod1（2 も同じ） mod mode lfo shp rhythm tempo sync groove
//        thr egain att rel trig trigon a d s r lroff lrflip smooth smode mono（1 = モノ → ステレオ）
//   freq は 0..1（20 Hz〜20 kHz）。出力は遅れ（28 サンプル）を取り除いてある
#include "../Source/MyFilterDSP.h"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace myfilt;
int main (int argc, char** argv)
{
    if (argc < 4) { fprintf (stderr, "usage\n"); return 1; }
    FILE* f = fopen (argv[1], "rb"); if (! f) return 1;
    fseek (f, 0, SEEK_END); long n = ftell (f) / 8; fseek (f, 0, SEEK_SET);
    std::vector<float> in ((size_t) n * 2); fread (in.data(), 4, (size_t) n * 2, f); fclose (f);
    const double bpm = atof (argv[3]);
    Knobs k; int bs = 256; bool mono = false;
    for (int i = 4; i < argc; ++i)
    {
        std::string a = argv[i]; auto e = a.find ('='); if (e == std::string::npos) continue;
        std::string key = a.substr (0, e); float v = (float) atof (a.substr (e + 1).c_str());
        int fi = (! key.empty() && key.back() == '2') ? 1 : 0;
        std::string kb = (! key.empty() && (key.back() == '1' || key.back() == '2')) ? key.substr (0, key.size() - 1) : key;
        if (key == "in") k.inDb = v; else if (key == "out") k.outDb = v; else if (key == "mix") k.mix = v;
        else if (key == "style") k.style = (int) v; else if (key == "filters") k.filters = (int) v; else if (key == "par") k.parallel = v > 0.5f;
        else if (kb == "freq") k.freq[fi] = v; else if (kb == "res") k.res[fi] = v; else if (kb == "shape") k.shape[fi] = (int) v;
        else if (kb == "poles") k.poles[fi] = (int) v; else if (kb == "gain") k.gainDb[fi] = v; else if (kb == "fmod") k.freqModOct[fi] = v;
        else if (kb == "rmod") k.resMod[fi] = v; else if (kb == "lmod") k.levelMod[fi] = v;
        else if (key == "mod") k.mod = v; else if (key == "mode") k.mode = (int) v; else if (key == "lfo") k.lfoHz = v;
        else if (key == "shp") k.shapeIdx = (int) v; else if (key == "rhythm") k.rhythm = (int) v; else if (key == "tempo") k.tempoBpm = v;
        else if (key == "sync") k.sync = v > 0.5f; else if (key == "groove") k.groove = v;
        else if (key == "thr") k.thresholdDb = v; else if (key == "egain") k.envGainDb = v; else if (key == "att") k.attackMs = v; else if (key == "rel") k.releaseMs = v;
        else if (key == "trig") k.trigDb = v; else if (key == "trigon") k.trigger = v > 0.5f;
        else if (key == "a") k.aMs = v; else if (key == "d") k.dMs = v; else if (key == "s") k.sustain = v; else if (key == "r") k.rMs = v;
        else if (key == "lroff") k.lrOffsetOct = v; else if (key == "lrflip") k.lrFlip = v > 0.5f;
        else if (key == "smooth") k.smoothing = v; else if (key == "smode") k.smoothMode = (int) v;
        else if (key == "bs") bs = (int) v; else if (key == "mono") mono = v > 0.5f;
    }
    static Engine eng;
    eng.prepare (48000, mono ? 1 : 2, 2);
    const int lat = eng.latencySamples();
    std::vector<float> L ((size_t) (n + lat), 0.0f), R ((size_t) (n + lat), 0.0f);
    for (long i = 0; i < n; ++i) { L[(size_t) i] = in[(size_t) i * 2]; R[(size_t) i] = in[(size_t) i * 2 + 1]; }
    for (long s = 0; s < n + lat; s += bs)
    {
        const int m = (int) std::min ((long) bs, n + lat - s);
        float* ch[2] = { L.data() + s, R.data() + s };
        Transport tr; tr.hostPlaying = true; tr.hasPosition = true; tr.hostBpm = bpm; tr.ppq = (double) s / 48000.0 * bpm / 60.0;
        eng.process (ch, m, k, tr);
    }
    std::vector<float> out ((size_t) n * 2);
    for (long i = 0; i < n; ++i) { out[(size_t) i * 2] = L[(size_t) (i + lat)]; out[(size_t) i * 2 + 1] = R[(size_t) (i + lat)]; }
    f = fopen (argv[2], "wb"); fwrite (out.data(), 4, (size_t) n * 2, f); fclose (f);
    return 0;
}
