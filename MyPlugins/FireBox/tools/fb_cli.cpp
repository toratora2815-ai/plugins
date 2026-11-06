// テスト用: raw float32 ステレオ（インターリーブ）を Fire Box のエンジンに通す
// 使い方: fb_cli in.raw out.raw sr clip2d.bin key=val ...   （key は Params の名前、bool は 0/1、live=1）
#include "../Source/FireEngine.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
int main (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "usage\n"); return 1; }
    const double sr = std::atof (argv[3]);
    std::vector<float> tab ((size_t) firebox::data::clipKnobs * firebox::data::clipKnobs * firebox::data::clipX);
    if (FILE* f = std::fopen (argv[4], "rb")) { std::fread (tab.data(), 4, tab.size(), f); std::fclose (f); }
    firebox::Params p; bool live = false, bypass = false; int block = 512;
    struct Ev { long at; std::string kv; }; std::vector<Ev> evs;   // @サンプル:key=val で途中から変える
    auto apply = [&] (const std::string& a)
    {
        auto e = a.find ('='); std::string k = a.substr (0, e); double v = std::atof (a.substr (e + 1).c_str());
        if (k == "in") p.inDb = v; else if (k == "out") p.outDb = v; else if (k == "bass") p.bass = v; else if (k == "thick") p.thick = v;
        else if (k == "body") p.body = v; else if (k == "comp") p.comp = v; else if (k == "sc") p.sc = v; else if (k == "crunch") p.crunch = v;
        else if (k == "mix") p.mix = v; else if (k == "loud") p.loud = v;
        else if (k == "bassOn") p.bassOn = v > 0.5; else if (k == "thickOn") p.thickOn = v > 0.5; else if (k == "bodyOn") p.bodyOn = v > 0.5;
        else if (k == "compOn") p.compOn = v > 0.5; else if (k == "ott") p.ott = v > 0.5; else if (k == "scOn") p.scOn = v > 0.5;
        else if (k == "crunchOn") p.crunchOn = v > 0.5; else if (k == "loudOn") p.loudOn = v > 0.5; else if (k == "unity") p.unity = v > 0.5;
        else if (k == "live") live = v > 0.5; else if (k == "bypass") bypass = v > 0.5; else if (k == "block") block = (int) v;
    };
    for (int i = 5; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a[0] == '@') { auto c = a.find (':'); evs.push_back ({ std::atol (a.substr (1, c - 1).c_str()), a.substr (c + 1) }); }
        else apply (a);
    }
    FILE* f = std::fopen (argv[1], "rb"); std::fseek (f, 0, SEEK_END); long sz = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    std::vector<float> x ((size_t) sz / 4); std::fread (x.data(), 4, x.size(), f); std::fclose (f);
    const int n = (int) (x.size() / 2);
    static firebox::Engine eng; eng.setClipTable (tab.data()); eng.prepare (sr, live);
    const int lat = eng.getLatency();
    std::vector<float> L ((size_t) (n + lat), 0.0f), R ((size_t) (n + lat), 0.0f);
    for (int i = 0; i < n; ++i) { L[(size_t) i] = x[(size_t) 2 * i]; R[(size_t) i] = x[(size_t) 2 * i + 1]; }
    for (int s = 0; s < n + lat; s += block) { for (auto& ev : evs) if (ev.at >= s && ev.at < s + block) apply (ev.kv); int m = std::min (block, n + lat - s); eng.process (L.data() + s, R.data() + s, m, p, bypass); }
    std::vector<float> y ((size_t) n * 2);
    for (int i = 0; i < n; ++i) { y[(size_t) 2 * i] = L[(size_t) (i + lat)]; y[(size_t) 2 * i + 1] = R[(size_t) (i + lat)]; }
    f = std::fopen (argv[2], "wb"); std::fwrite (y.data(), 4, y.size(), f); std::fclose (f);
    std::fprintf (stderr, "latency %d\n", lat);
    return 0;
}
