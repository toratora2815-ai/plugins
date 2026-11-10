// 数値テスト用: My Tremolo のエンジン（JUCE なし）でファイルを処理する
//   render_mytrem in.f32 out.f32 bpm key=value ...   （48 kHz ステレオ、float の生データ）
//   key: ppq0（始まりの位置） jumpat・jumpto（その秒で位置を飛ばす） in out depth groove feel accent width shape rhythm rate sync style thr att rel gate rmod dmod lowkeep pattern=...
//   出力は遅れ（28 サンプル）を取り除いてある
#include "../Source/MyTremoloDSP.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace mytrem;
int main (int argc, char** argv)
{
    if (argc < 4) { fprintf (stderr, "usage\n"); return 1; }
    FILE* f = fopen (argv[1], "rb"); if (! f) return 1;
    fseek (f, 0, SEEK_END); long n = ftell (f) / 8; fseek (f, 0, SEEK_SET);
    std::vector<float> in ((size_t) n * 2); fread (in.data(), 4, (size_t) n * 2, f); fclose (f);
    const double bpm = atof (argv[3]);
    Knobs k; Pattern pat; bool usePat = false; int bs = 256; double ppq0 = 0, jumpAt = 1e9, jumpTo = 0;
    for (int i = 4; i < argc; ++i)
    {
        std::string a = argv[i]; auto e = a.find ('='); if (e == std::string::npos) continue;
        std::string key = a.substr (0, e); std::string sv = a.substr (e + 1); float v = (float) atof (sv.c_str());
        if (key == "in") k.inDb = v; else if (key == "out") k.outDb = v; else if (key == "depth") k.depth = v;
        else if (key == "groove") k.groove = v; else if (key == "feel") k.feel = v; else if (key == "accent") k.accentDb = v;
        else if (key == "width") k.width = v; else if (key == "shape") k.shape = (int) v; else if (key == "rhythm") k.rhythm = (int) v;
        else if (key == "rate") k.rateBpm = v; else if (key == "sync") k.sync = v > 0.5f; else if (key == "style") k.style = (int) v;
        else if (key == "thr") k.thresholdDb = v; else if (key == "att") k.attackMs = v; else if (key == "rel") k.releaseMs = v;
        else if (key == "gate") k.gate = v > 0.5f; else if (key == "rmod") k.rateModOct = v; else if (key == "dmod") k.depthMod = v;
        else if (key == "lowkeep") k.lowKeepHz = v; else if (key == "bs") bs = (int) v; else if (key == "ppq0") ppq0 = atof (sv.c_str()); else if (key == "jumpat") jumpAt = atof (sv.c_str()); else if (key == "jumpto") jumpTo = atof (sv.c_str());
        else if (key == "pattern")
        {   // bars;beats;grid;cells
            int b = 1, bt = 4, g = 1; char cells[512] = {};
            sscanf (sv.c_str(), "%d;%d;%d;%511s", &b, &bt, &g, cells);
            pat.bars = b; pat.beatsPerBar = bt; pat.grid = g; pat.cell.fill (0);
            for (int c = 0; c < pat.numCells() && cells[c]; ++c) pat.cell[(size_t) c] = (uint8_t) (cells[c] - '0');
            usePat = true;
        }
    }
    static Engine eng;
    eng.prepare (48000, 2);
    if (usePat) eng.setPattern (pat);
    const int lat = eng.latencySamples();
    std::vector<float> L ((size_t) (n + lat), 0.0f), R ((size_t) (n + lat), 0.0f);
    for (long i = 0; i < n; ++i) { L[(size_t) i] = in[(size_t) i * 2]; R[(size_t) i] = in[(size_t) i * 2 + 1]; }
    for (long s = 0; s < n + lat; s += bs)
    {
        const int m = (int) std::min ((long) bs, n + lat - s);
        float* ch[2] = { L.data() + s, R.data() + s };
        Transport tr; tr.hostPlaying = true; tr.hasPosition = true; tr.hostBpm = bpm; { const double ts = (double) s / 48000.0; tr.ppq = ts >= jumpAt ? jumpTo + (ts - jumpAt) * bpm / 60.0 : ppq0 + ts * bpm / 60.0; }
        eng.process (ch, m, k, tr);
    }
    std::vector<float> out ((size_t) n * 2);
    for (long i = 0; i < n; ++i) { out[(size_t) i * 2] = L[(size_t) (i + lat)]; out[(size_t) i * 2 + 1] = R[(size_t) (i + lat)]; }
    f = fopen (argv[2], "wb"); fwrite (out.data(), 4, (size_t) n * 2, f); fclose (f);
    return 0;
}
