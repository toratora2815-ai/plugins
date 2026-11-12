// Noise Out のエンジンでファイルを処理する（測った本物と比べる用）
// 使い方: render_noiseout in.f32 out.f32 [key=value,...]   （in は 48 kHz・ステレオ交互の float32。out は既定でステレオ、mono=1 で左だけ）
// キー: hum humf(0 AUTO/1 50/2 60) humd harm noise mode(0 ADAPTIVE/1 LEARNED) red thr room rred tail out delta
//       learn=開始秒:終了秒（その間 LEARN を押す）、freeze=開始秒:終了秒、comp=1（遅れの分を詰めて元の音とそろえる）、sr=サンプルレート
//       T.名前=値（Tuning の数値を上書き）
// 終わりに「latency <サンプル数>」と、AUTO の HUM で見つけた周波数を表示する
#include "../Source/NoiseOutDSP.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <map>
#include <vector>

using namespace nzo;

static bool setTuning (const std::string& key, float v)
{
    auto& t = tuning();
    std::map<std::string, float*> m {
        { "nFreqOct", &t.nFreqOct }, { "nFreqMin", &t.nFreqMin }, { "nAttMs", &t.nAttMs }, { "nRelMs", &t.nRelMs },
        { "nTrkMs", &t.nTrkMs }, { "nUpDbs", &t.nUpDbs }, { "nUpProp", &t.nUpProp }, { "nDownMs", &t.nDownMs },
        { "nBiasA", &t.nBiasA }, { "nBiasL", &t.nBiasL }, { "nSlope", &t.nSlope }, { "nKnee", &t.nKnee },
        { "nGainOct", &t.nGainOct }, { "nGAttMs", &t.nGAttMs }, { "nGRelMs", &t.nGRelMs }, { "nThrScale", &t.nThrScale },
        { "nLowBias", &t.nLowBias },
        { "nMusBias", &t.nMusBias }, { "nMusSlope", &t.nMusSlope }, { "nMusKnee", &t.nMusKnee }, { "nMusUp", &t.nMusUp }, { "nMusRel", &t.nMusRel }, { "nMusLow", &t.nMusLow }, { "nGenOct", &t.nGenOct }, { "nGenBias", &t.nGenBias }, { "nGenHf", &t.nGenHf }, { "nGenRel", &t.nGenRel }, { "nHfW1", &t.nHfW1 }, { "nHfW3", &t.nHfW3 }, { "nHfW2", &t.nHfW2 },
        { "rSmMs", &t.rSmMs }, { "rTdMs", &t.rTdMs }, { "rRate", &t.rRate }, { "rRateP", &t.rRateP },
        { "rWinA", &t.rWinA }, { "rWinB", &t.rWinB }, { "rDryK", &t.rDryK }, { "rEnvDbs", &t.rEnvDbs },
        { "rMu0", &t.rMu0 }, { "rQ0", &t.rQ0 }, { "rMuP", &t.rMuP }, { "rA", &t.rA }, { "rGamma", &t.rGamma }, { "rMaxDb", &t.rMaxDb }, { "rBoostK", &t.rBoostK },
        { "rGAttMs", &t.rGAttMs }, { "rGRelMs", &t.rGRelMs }, { "rGainOct", &t.rGainOct },
        { "rSrcMix", &t.rSrcMix }, { "rAP", &t.rAP }, { "rX1", &t.rX1 }, { "rX2", &t.rX2 }, { "rX3", &t.rX3 }, { "rXW", &t.rXW }, { "rBandP", &t.rBandP }, { "rSmK", &t.rSmK },
        { "rEst", &t.rEst }, { "rHc", &t.rHc }, { "rHk", &t.rHk }, { "rLaw", &t.rLaw }, { "rLc", &t.rLc }, { "rLk", &t.rLk },
        { "hDcHz", &t.hDcHz },
        { "mFcBase", &t.mFcBase }, { "mThr0", &t.mThr0 }, { "mThrMin", &t.mThrMin }, { "mThrP", &t.mThrP }, { "mMaxMs", &t.mMaxMs }, { "mBaseMs", &t.mBaseMs }, { "mMargMs", &t.mMargMs }, { "mDepth", &t.mDepth }, { "mRepHz", &t.mRepHz }, { "mEndDb", &t.mEndDb },
        { "bFlat", &t.bFlat }, { "bSensK", &t.bSensK }, { "bCmin", &t.bCmin }, { "bCmax", &t.bCmax }, { "bLoRatio", &t.bLoRatio }, { "bDv", &t.bDv }, { "bDf", &t.bDf },
        { "bMinMs", &t.bMinMs }, { "bAttMs", &t.bAttMs }, { "bRelMs", &t.bRelMs }, { "bSat", &t.bSat },
        { "pThr", &t.pThr }, { "pSensK", &t.pSensK }, { "pRatioDb", &t.pRatioDb }, { "pSlope", &t.pSlope }, { "pMaxDb", &t.pMaxDb }, { "pBaseMs", &t.pBaseMs },
        { "pRelMs", &t.pRelMs }, { "pAhead", &t.pAhead }, { "pWidth", &t.pWidth }, { "pStrP", &t.pStrP } };
    auto it = m.find (key);
    if (it == m.end()) return false;
    *it->second = v;
    return true;
}

int main (int argc, char** argv)
{
    if (argc < 3) { fprintf (stderr, "usage: render_noiseout in.f32 out.f32 [k=v,...]\n"); return 1; }
    FILE* f = fopen (argv[1], "rb"); if (! f) { fprintf (stderr, "cannot open %s\n", argv[1]); return 2; }
    fseek (f, 0, SEEK_END); long bytes = ftell (f); fseek (f, 0, SEEK_SET);
    const long n = bytes / 8; std::vector<float> io ((size_t) n * 2);
    if (fread (io.data(), 8, (size_t) n, f) != (size_t) n) { fclose (f); return 2; }
    fclose (f);
    Knobs k;
    bool mono = false, comp = false;
    double sr = 48000.0, learnA = -1, learnB = -1, frA = -1, frB = -1;
    if (argc > 3)
    {
        std::string s = argv[3]; size_t p0 = 0;
        while (p0 < s.size())
        {
            size_t p1 = s.find (',', p0); if (p1 == std::string::npos) p1 = s.size();
            std::string kv = s.substr (p0, p1 - p0); p0 = p1 + 1;
            size_t e = kv.find ('='); if (e == std::string::npos) continue;
            std::string key = kv.substr (0, e), val = kv.substr (e + 1);
            const float v = (float) atof (val.c_str());
            if (key.rfind ("T.", 0) == 0) { if (! setTuning (key.substr (2), v)) { fprintf (stderr, "unknown tuning %s\n", key.c_str()); return 3; } continue; }
            if (key == "hum") k.humOn = v > 0.5f; else if (key == "humf") k.humFreq = (int) v; else if (key == "humd") k.humDepthDb = v;
            else if (key == "harm") k.humHarm = (int) v; else if (key == "noise") k.noiseOn = v > 0.5f; else if (key == "mode") k.noiseMode = (int) v;
            else if (key == "red") k.noiseRedDb = v; else if (key == "music") k.noiseMusic = v > 0.5f; else if (key == "gentle") k.noiseGentle = v > 0.5f; else if (key == "thr") k.noiseThrDb = v; else if (key == "room") k.roomOn = v > 0.5f;
            else if (key == "rred") k.roomRedDb = v; else if (key == "tail") k.roomTail = v; else if (key == "rsm") k.roomSmooth = v;
            else if (key == "rb0") k.roomBand[0] = v; else if (key == "rb1") k.roomBand[1] = v; else if (key == "rb2") k.roomBand[2] = v; else if (key == "rb3") k.roomBand[3] = v; else if (key == "out") k.outDb = v;
            else if (key == "delta") k.delta = v > 0.5f;
            else if (key == "mouth") k.mouthOn = v > 0.5f; else if (key == "msens") k.mouthSens = v; else if (key == "mskew") k.mouthSkew = v;
            else if (key == "breath") k.breathOn = v > 0.5f; else if (key == "bgain") k.breathGainDb = v; else if (key == "bsens") k.breathSens = v;
            else if (key == "plosive") k.plosiveOn = v > 0.5f; else if (key == "pstr") k.plosiveStrength = v; else if (key == "psens") k.plosiveSens = v; else if (key == "pfreq") k.plosiveFreqHz = v;
            else if (key == "hwidth") k.humWidthQ = v; else if (key == "mono") mono = v > 0.5f; else if (key == "comp") comp = v > 0.5f;
            else if (key == "sr") sr = v;
            else if (key == "learn" || key == "freeze")
            {
                const size_t c = val.find (':');
                const double a = atof (val.substr (0, c).c_str()), b = c == std::string::npos ? 1e9 : atof (val.substr (c + 1).c_str());
                if (key == "learn") { learnA = a; learnB = b; } else { frA = a; frB = b; }
            }
            else { fprintf (stderr, "unknown key %s\n", key.c_str()); return 3; }
        }
    }
    static Engine e;
    e.prepare (sr, 2);
    const int lat = e.latencySamples();
    const long total = n + (comp ? lat : 0);
    std::vector<float> out ((size_t) total * 2, 0.0f);
    const int bs = 512; std::vector<float> L (bs), R (bs);
    for (long s = 0; s < total; s += bs)
    {
        const int m = (int) std::min<long> (bs, total - s);
        for (int i = 0; i < m; ++i)
        {
            const long j = s + i;
            L[(size_t) i] = j < n ? io[(size_t) (2 * j)] : 0.0f; R[(size_t) i] = j < n ? io[(size_t) (2 * j + 1)] : 0.0f;
        }
        const double tsec = (double) s / sr;
        Knobs kk = k;
        kk.learnStart = tsec >= learnA ? 1 : 0;          // 押した回数（learnA 秒で 1 回）
        kk.learnStop = tsec >= learnB ? 1 : 0;           // 止めた回数（learnB 秒で 1 回）
        kk.freezeAdapt = tsec >= frA && tsec < frB;
        if (learnA >= 0 && tsec >= learnB && e.hasProfile()) kk.noiseMode = 1;   // 覚え終わったら LEARNED へ（プラグインと同じ）
        float* ch[2] = { L.data(), R.data() };
        e.process (ch, m, kk);
        for (int i = 0; i < m; ++i) { out[(size_t) (2 * (s + i))] = L[(size_t) i]; out[(size_t) (2 * (s + i) + 1)] = R[(size_t) i]; }
    }
    const long off = comp ? lat : 0;
    FILE* g = fopen (argv[2], "wb");
    if (mono) { std::vector<float> m1 ((size_t) n); for (long i = 0; i < n; ++i) m1[(size_t) i] = out[(size_t) (2 * (i + off))]; fwrite (m1.data(), 4, (size_t) n, g); }
    else fwrite (out.data() + 2 * off, 8, (size_t) n, g);
    fclose (g);
    printf ("latency %d hum %.3f found %d conf %.1f\n", lat, (double) e.humF0.load(), e.humFound.load() ? 1 : 0, (double) e.humConf.load());
    return 0;
}
