// My Crystal のエンジンでファイルを処理する（測った本物と比べる用）
// 使い方: render_mycrystal in.f32 out.f32 bpm key=value,...   （in / out は 48 kHz・ステレオ交互の float32）
// キー: in out mix analog pitch rev splice delay sync snote dnote tempo rec thr gd poff soff doff smooth lc hc att rel dmode fb
//       （単位はつまみと同じ: dB・%・セント・ms・Hz。mix・rec は 0..1）
#include "../Source/MyCrystalDSP.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
using namespace mycry;
int main (int argc, char** argv)
{
    if (argc < 4) { fprintf (stderr, "usage: render_mycrystal in.f32 out.f32 bpm [k=v,...]\n"); return 1; }
    FILE* f = fopen (argv[1], "rb"); if (! f) return 2;
    fseek (f, 0, SEEK_END); long bytes = ftell (f); fseek (f, 0, SEEK_SET);
    const long n = bytes / 8; std::vector<float> io ((size_t) n * 2);
    fread (io.data(), 8, (size_t) n, f); fclose (f);
    Knobs k; k.sync = false;
    Transport tr; tr.hostBpm = atof (argv[3]); tr.hostPlaying = true; tr.hasPosition = true;
    if (argc > 4)
    {
        std::string s = argv[4]; size_t p0 = 0;
        while (p0 < s.size())
        {
            size_t p1 = s.find (',', p0); if (p1 == std::string::npos) p1 = s.size();
            std::string kv = s.substr (p0, p1 - p0); p0 = p1 + 1;
            size_t e = kv.find ('='); if (e == std::string::npos) continue;
            std::string key = kv.substr (0, e); float v = (float) atof (kv.substr (e + 1).c_str());
            if (key == "in") k.inDb = v; else if (key == "out") k.outDb = v; else if (key == "mix") k.mix = v;
            else if (key == "analog") k.analog = v > 0.5f; else if (key == "pitch") k.pitchCents = v; else if (key == "rev") k.reverse = v > 0.5f;
            else if (key == "splice") k.spliceMs = v; else if (key == "delay") k.delayMs = v; else if (key == "sync") k.sync = v > 0.5f;
            else if (key == "snote") k.spliceNote = (int) v; else if (key == "dnote") k.delayNote = (int) v; else if (key == "tempo") k.tempoBpm = v;
            else if (key == "rec") k.recycle = v; else if (key == "thr") k.thresholdDb = v; else if (key == "gd") k.gateDuckDb = v;
            else if (key == "poff") k.pitchOffCents = v; else if (key == "soff") k.spliceOffPct = v; else if (key == "doff") k.delayOffPct = v;
            else if (key == "smooth") k.smoothMs = v; else if (key == "lc") k.lowCutHz = v; else if (key == "hc") k.highCutHz = v;
            else if (key == "att") k.attackMs = v; else if (key == "rel") k.releaseMs = v; else if (key == "dmode") k.duckMode = (int) v; else if (key == "fb") k.fbMode = (int) v;
        }
    }
    Engine e; e.prepare (48000, 2);
    const int bs = 256; std::vector<float> L (bs), R (bs);
    for (long s = 0; s < n; s += bs)
    {
        const int m = (int) std::min<long> (bs, n - s);
        for (int i = 0; i < m; ++i) { L[(size_t) i] = io[(size_t) (2 * (s + i))]; R[(size_t) i] = io[(size_t) (2 * (s + i) + 1)]; }
        float* ch[2] = { L.data(), R.data() };
        tr.ppq = (double) s / 48000.0 * tr.hostBpm / 60.0;
        e.process (ch, m, k, tr);
        for (int i = 0; i < m; ++i) { io[(size_t) (2 * (s + i))] = L[(size_t) i]; io[(size_t) (2 * (s + i) + 1)] = R[(size_t) i]; }
    }
    FILE* g = fopen (argv[2], "wb"); fwrite (io.data(), 8, (size_t) n, g); fclose (g);
    return 0;
}
