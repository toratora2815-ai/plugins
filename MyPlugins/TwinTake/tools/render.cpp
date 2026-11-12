// Twin Take の試作・数値テスト用（JUCE なし、TwinTakeDSP.h をそのまま使う）
// ビルド: clang++ -std=c++17 -O2 -o render render.cpp -framework AudioToolbox -framework CoreFoundation
// 使い方:
//   render in.wav out.wav [mode=vocal|guitar voices=2 double=-6 loose=0.3 width=0.8 lowkeep=200 take=1 start=秒 len=秒 block=512 drywet=1]
//   render test        数値テスト（NaN・発振・遅れ・ブロックの大きさ・モノ・無音・つまみの端）
#include "../Source/TwinTakeDSP.h"
#include <AudioToolbox/AudioToolbox.h>
#include <fenv.h>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using Chans = std::vector<std::vector<float>>;

static bool readAudio (const std::string& path, Chans& ch, double& sr)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) path.c_str(), (CFIndex) path.size(), false);
    ExtAudioFileRef f = nullptr;
    OSStatus st = ExtAudioFileOpenURL (url, &f);
    CFRelease (url);
    if (st != noErr) return false;
    AudioStreamBasicDescription in {}; UInt32 sz = sizeof in;
    ExtAudioFileGetProperty (f, kExtAudioFileProperty_FileDataFormat, &sz, &in);
    sr = in.mSampleRate; const UInt32 nch = in.mChannelsPerFrame;
    AudioStreamBasicDescription cf {};
    cf.mSampleRate = sr; cf.mFormatID = kAudioFormatLinearPCM; cf.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    cf.mBytesPerPacket = 4; cf.mFramesPerPacket = 1; cf.mBytesPerFrame = 4; cf.mChannelsPerFrame = nch; cf.mBitsPerChannel = 32;
    ExtAudioFileSetProperty (f, kExtAudioFileProperty_ClientDataFormat, sizeof cf, &cf);
    ch.assign (nch, {});
    std::vector<std::vector<float>> b (nch, std::vector<float> (8192));
    std::vector<uint8_t> mem (offsetof (AudioBufferList, mBuffers) + sizeof (AudioBuffer) * nch);
    auto* abl = (AudioBufferList*) mem.data();
    while (true)
    {
        abl->mNumberBuffers = nch;
        for (UInt32 c = 0; c < nch; ++c) { abl->mBuffers[c].mNumberChannels = 1; abl->mBuffers[c].mDataByteSize = 8192 * 4; abl->mBuffers[c].mData = b[c].data(); }
        UInt32 frames = 8192;
        if (ExtAudioFileRead (f, &frames, abl) != noErr || frames == 0) break;
        for (UInt32 c = 0; c < nch; ++c) ch[c].insert (ch[c].end(), b[c].begin(), b[c].begin() + frames);
    }
    ExtAudioFileDispose (f);
    return ! ch.empty();
}

static bool writeWav (const std::string& path, const Chans& ch, double sr)   // 24 ビット
{
    FILE* f = fopen (path.c_str(), "wb");
    if (! f) return false;
    const uint32_t n = (uint32_t) ch[0].size(), nc = (uint32_t) ch.size(), bps = 3;
    const uint32_t dataBytes = n * nc * bps;
    auto u32 = [&] (uint32_t v) { fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { fwrite (&v, 2, 1, f); };
    fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); fwrite ("WAVE", 1, 4, f);
    fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 ((uint16_t) nc); u32 ((uint32_t) sr); u32 ((uint32_t) sr * nc * bps); u16 ((uint16_t) (nc * bps)); u16 (24);
    fwrite ("data", 1, 4, f); u32 (dataBytes);
    std::vector<uint8_t> row (nc * bps);
    for (uint32_t i = 0; i < n; ++i)
    {
        for (uint32_t c = 0; c < nc; ++c)
        {
            float v = std::clamp (ch[c][i], -1.0f, 1.0f);
            int32_t q = (int32_t) std::lround (v * 8388607.0f);
            row[c * 3] = (uint8_t) (q & 0xff); row[c * 3 + 1] = (uint8_t) ((q >> 8) & 0xff); row[c * 3 + 2] = (uint8_t) ((q >> 16) & 0xff);
        }
        fwrite (row.data(), 1, row.size(), f);
    }
    fclose (f);
    return true;
}

static Chans runEngine (const Chans& in, double sr, const twin::Params& prm, int block, twin::Stats* st = nullptr, bool stereoOut = true, int64_t tl0 = 0)
{
    twin::Engine e; e.prepare (sr); e.setParams (prm); e.reset();
    const size_t n = in[0].size();
    Chans out (stereoOut ? 2 : 1, std::vector<float> (n));
    for (size_t i = 0; i < n; ++i) { out[0][i] = in[0][i]; if (stereoOut) out[1][i] = in[std::min<size_t> (1, in.size() - 1)][i]; }
    size_t pos = 0; int k = 0;
    static const int seq[] = { 1, 7, 64, 333, 512, 4096, 17, 2048, 3, 999 };
    while (pos < n)
    {
        int b = block > 0 ? block : seq[k++ % 10];
        b = (int) std::min<size_t> ((size_t) b, n - pos);
        e.process (out[0].data() + pos, stereoOut ? out[1].data() + pos : nullptr, stereoOut ? 2 : 1, b, tl0 + (int64_t) pos);
        pos += (size_t) b;
    }
    if (st) *st = e.getStats();
    return out;
}

static std::map<std::string, std::string> parseKV (int argc, char** argv, int from)
{
    std::map<std::string, std::string> m;
    for (int i = from; i < argc; ++i) { std::string a = argv[i]; auto eq = a.find ('='); if (eq != std::string::npos) m[a.substr (0, eq)] = a.substr (eq + 1); }
    return m;
}
static twin::Params paramsFrom (std::map<std::string, std::string>& kv)
{
    twin::Params p;
    auto f = [&] (const char* k, float d) { return kv.count (k) ? (float) std::atof (kv[k].c_str()) : d; };
    p.mode = (kv.count ("mode") && kv["mode"] == "guitar") ? 1 : 0;
    p.voices = (int) f ("voices", 2); p.doubleDb = f ("double", p.mode ? 0.0f : -6.0f); p.loose = f ("loose", 0.3f);
    p.width = f ("width", p.mode ? 1.0f : 0.8f); p.lowKeepHz = f ("lowkeep", 200); p.take = (int) f ("take", 1);
    p.doubleOnly = f ("doubleonly", 0) > 0.5f; p.tail = f ("tail", 1) > 0.5f;
    return p;
}

// ---- 数値テスト ----
static double rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i];
    return 10 * std::log10 (s / std::max<size_t> (1, b - a) + 1e-30);
}
static int test()
{
    int fails = 0;
    auto check = [&] (bool ok, const std::string& what) { printf ("%s %s\n", ok ? "  OK " : "  NG ", what.c_str()); if (! ok) ++fails; };
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        printf ("== %.0f Hz\n", sr);
        // 歌っぽい試験の音: 音程の変わる母音（倍音）＋子音（ノイズ）＋無音の間
        const size_t n = (size_t) (sr * 12);
        Chans in (2, std::vector<float> (n));
        uint32_t s = 1;
        auto rnd = [&] { s = s * 1664525u + 1013904223u; return (float) ((s >> 8) * (1.0 / 8388608.0)) - 1.0f; };
        double ph = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / sr; const int note = (int) (t / 0.5); const double tn = t - note * 0.5;
            const double f0 = 110.0 * std::pow (2.0, ((note * 5) % 12) / 12.0);
            ph += 2 * M_PI * f0 / sr;
            double v = 0; for (int h = 1; h <= 12; ++h) v += std::sin (ph * h) / h;
            double env = tn < 0.35 ? std::min (1.0, tn / 0.01) : std::max (0.0, 1.0 - (tn - 0.35) / 0.05);
            if (note % 6 == 5) env = 0;                                   // 間
            double cons = (tn < 0.04 && note % 3 == 0) ? rnd() * 0.5 : 0; // 子音
            float x = (float) (0.25 * v * env + cons);
            in[0][i] = x; in[1][i] = x;
        }
        for (int mode : { 0, 1 })
            for (int voices : { 1, 2, 4 })
            {
                if (mode == 1 && voices != 1) continue;
                twin::Params p; p.mode = mode; p.voices = voices; p.doubleDb = 0; p.loose = 1.0f; p.width = 1.0f; p.lowKeepHz = 150; p.take = 3;
                twin::Stats st;
                auto a = runEngine (in, sr, p, 512, &st);
                auto b = runEngine (in, sr, p, 0);            // ブロックの大きさをばらばらに
                double peak = 0; bool finite = true;
                for (auto& c : a) for (float v : c) { if (! std::isfinite (v)) finite = false; peak = std::max (peak, (double) std::fabs (v)); }
                double maxDiff = 0; for (int c = 0; c < 2; ++c) for (size_t i = 0; i < n; ++i) maxDiff = std::max (maxDiff, (double) std::fabs (a[c][i] - b[c][i]));
                const std::string tag = std::string (mode ? "GUITAR" : "VOCAL") + " voices " + std::to_string (voices);
                check (finite, tag + ": NaN・無限大なし");
                check (peak < 2.5, tag + ": ピーク " + std::to_string (20 * std::log10 (peak)) + " dBFS");
                check (maxDiff < 1e-5, tag + ": ブロックの大きさで変わらない（差 " + std::to_string (maxDiff) + "）");
                printf ("       オンセット %lld 回・つなぎ直し %lld 回・最後の遅れ %.1f ms・音程 %+.1f セント\n", (long long) st.onsets, (long long) st.forced, st.lastDelayMs[0], st.lastCents[0]);
                // 止めた後: 最後の 0.2 秒の後に 2 秒無音を足して、鳴り止むか
                Chans z = in; for (auto& c : z) c.insert (c.end(), (size_t) (sr * 2), 0.0f);
                auto t2 = runEngine (z, sr, p, 512);
                const size_t m2 = z[0].size();
                check (rmsDb (t2[0], m2 - (size_t) (sr * 0.5), m2) < -120 && rmsDb (t2[1], m2 - (size_t) (sr * 0.5), m2) < -120, tag + ": 止めると鳴り止む");
            }
        // 遅れ: GUITAR の原音はちょうど 40 ms 遅れて左に（WIDTH 100%、ダブル無音、LOW KEEP OFF）
        {
            twin::Params p; p.mode = 1; p.doubleDb = -100; p.width = 1.0f; p.lowKeepHz = 0;
            const size_t n2 = (size_t) sr;
            Chans imp (2, std::vector<float> (n2, 0.0f)); imp[0][100] = 1; imp[1][100] = 1;
            auto o = runEngine (imp, sr, p, 256);
            size_t pk = 0; for (size_t i = 0; i < n2; ++i) if (std::fabs (o[0][i]) > std::fabs (o[0][pk])) pk = i;
            twin::Engine e; e.prepare (sr); e.setParams (p);
            double rmax = 0; for (float v : o[1]) rmax = std::max (rmax, (double) std::fabs (v));
            // LOW KEEP OFF でも LR4 を通す（ON⇄OFF でへこまないように）ので、インパルスの高さは 1 より少し低い。位置と左だけかを見る
            check (std::abs ((int) pk - 100 - e.latencySamples()) <= 2 && std::fabs (o[0][pk]) > 0.8f && rmax < 1e-6,
                   "GUITAR: 原音が申告どおり " + std::to_string (e.latencySamples()) + " サンプル遅れて左だけに（実際 " + std::to_string ((int) pk - 100) + "）");
            twin::Params pv; pv.mode = 0; pv.doubleDb = -100;
            auto ov = runEngine (imp, sr, pv, 256);
            check (std::fabs (ov[0][100] - 1.0f) < 1e-6 && std::fabs (ov[1][100] - 1.0f) < 1e-6, "VOCAL: 原音は遅れなし・そのまま");
        }
        // LOW KEEP: GUITAR でダブル無音・WIDTH 0 のとき、LR4 の分け方で低域・高域の境目でも大きさが平ら
        // （原音はまん中 = パンの決まりで各チャンネル −3 dB。どの周波数でも同じ −3 dB なら OK）
        {
            twin::Params p; p.mode = 1; p.doubleDb = -100; p.width = 0.0f; p.lowKeepHz = 200;
            for (double f : { 50.0, 140.0, 200.0, 280.0, 1000.0 })
            {
                const size_t n2 = (size_t) sr * 2;
                Chans x (2, std::vector<float> (n2));
                for (size_t i = 0; i < n2; ++i) { x[0][i] = x[1][i] = (float) (0.5 * std::sin (2 * M_PI * f * i / sr)); }
                auto o = runEngine (x, sr, p, 512);
                const double g = rmsDb (o[0], n2 / 2, n2) - rmsDb (x[0], n2 / 2, n2);
                check (std::fabs (g + 3.0103) < 0.15, "GUITAR WIDTH 0: " + std::to_string ((int) f) + " Hz も −3 dB で平ら（" + std::to_string (g) + " dB）");
            }
        }
        // NaN・無限大が 1 サンプル来ても、そのあと元に戻る
        {
            twin::Params p; p.mode = 0; p.voices = 2;
            Chans x = in; x[0][(size_t) sr] = NAN; x[1][(size_t) sr] = INFINITY; x[0][(size_t) sr * 2] = 1.0e30f;
            auto o = runEngine (x, sr, p, 512);
            int bad = 0; for (auto& c : o) for (size_t i = (size_t) (sr * 2.1); i < c.size(); ++i) if (! std::isfinite (c[i])) ++bad;
            check (bad == 0 && rmsDb (o[0], (size_t) (sr * 3), (size_t) (sr * 4)) > -60, "NaN・無限大のあとも元に戻る（あとの NaN " + std::to_string (bad) + " 個）");
        }
        // つまみの切り替えでプチッとしない（VOICES・MODE・GUITAR の LOW KEEP・モノ出力の VOICES）
        // 伸ばした 1 つの音（220 + 660 Hz、0.1 秒から鳴り続ける）の 1.5 秒で切り替えて、切り替えた所の 2 回差分のとがりが、
        // その前のふだんの +6 dB 以下（MODE は 10 ms で下げて上げるので +12 dB 以下）。子音のノイズがないので、プチッとすればすぐ分かる
        {
            const size_t n3 = (size_t) (sr * 2.2);
            Chans tone (2, std::vector<float> (n3, 0.0f));
            for (size_t i = (size_t) (0.1 * sr); i < n3; ++i)
            {
                const double t = (double) i / sr;
                const float v = (float) (0.3 * std::sin (2 * M_PI * 220.0 * t) + 0.1 * std::sin (2 * M_PI * 660.0 * t));
                tone[0][i] = tone[1][i] = v;
            }
            const size_t T = (size_t) (1.5 * sr) / 256 * 256;
            // 戻り値: ふだんに対するとがり（dB）。absDb には入力のピーク（0.4）に対するとがりの大きさ（dB）
            double absDb = 0;
            auto spike = [&] (const Chans& o, int chs)
            {
                double base = 1e-30, peak = 1e-30;
                for (int c = 0; c < chs; ++c)
                    for (size_t i = 2; i < o[(size_t) c].size(); ++i)
                    {
                        const auto& x = o[(size_t) c];
                        const double d2 = std::fabs ((double) x[i] - 2.0 * x[i - 1] + x[i - 2]);
                        if (i > T - (size_t) (0.30 * sr) && i < T - (size_t) (0.05 * sr)) base = std::max (base, d2);
                        if (i >= T - (size_t) (0.005 * sr) && i < T + (size_t) (0.05 * sr)) peak = std::max (peak, d2);
                    }
                absDb = 20 * std::log10 (peak / 0.4);
                return 20 * std::log10 (peak / base);
            };
            struct Sw { const char* name; twin::Params a, b; bool mono; double limit; };
            twin::Params v1; v1.voices = 1; v1.doubleDb = 0; twin::Params v2 = v1; v2.voices = 2; twin::Params v4 = v1; v4.voices = 4;
            twin::Params gOn; gOn.mode = 1; gOn.doubleDb = 0; gOn.lowKeepHz = 200; twin::Params gOff = gOn; gOff.lowKeepHz = 0;
            twin::Params vo; vo.mode = 0; twin::Params gu = vo; gu.mode = 1;
            twin::Params dOn = v2; dOn.doubleOnly = true; twin::Params gdOn = gOn; gdOn.doubleOnly = true;
            const Sw sws[] = { { "VOICES 1→2", v1, v2, false, 6 }, { "VOICES 2→4", v2, v4, false, 6 }, { "VOICES 4→1", v4, v1, false, 6 },
                               { "DOUBLE ONLY OFF→ON", v2, dOn, false, 6 }, { "DOUBLE ONLY ON→OFF", dOn, v2, false, 6 }, { "GUITAR DOUBLE ONLY OFF→ON", gOn, gdOn, false, 6 },
                               { "モノ 1→1 VOICES 2→4", v2, v4, true, 6 }, { "モノ 1→1 VOICES 4→1", v4, v1, true, 6 },
                               { "GUITAR LOW KEEP ON→OFF", gOn, gOff, false, 6 }, { "GUITAR LOW KEEP OFF→ON", gOff, gOn, false, 6 },
                               { "MODE VOCAL→GUITAR", vo, gu, false, 12 }, { "MODE GUITAR→VOCAL", gu, vo, false, 12 } };
            for (auto& sw : sws)
            {
                twin::Engine e; e.prepare (sr); e.setParams (sw.a); e.reset();
                Chans o = tone;
                for (size_t pos = 0; pos < n3; pos += 256)
                {
                    if (pos == T) e.setParams (sw.b);
                    const int b = (int) std::min<size_t> (256, n3 - pos);
                    e.process (o[0].data() + pos, sw.mono ? nullptr : o[1].data() + pos, sw.mono ? 1 : 2, b, (int64_t) pos);
                }
                const double sdb = spike (o, sw.mono ? 1 : 2);
                // ふだんの +limit dB 以下か、入力のピークより 60 dB 以上小さい（検査係の目安は −50 dB。高いサンプルレートでは、ふだんの 2 回差分がとても小さいので）
                check (sdb < sw.limit || absDb < -60.0, std::string (sw.name) + ": 切り替えでプチッとしない（ふだんより " + std::to_string (sdb) +
                       " dB、入力のピークより " + std::to_string (absDb) + " dB）");
            }
        }
        // 再生位置が飛んだら、前の場所を読んでいるダブルが 6 ms 以内に消える（VOCAL・GUITAR）
        for (int mode : { 0, 1 })
        {
            // GUITAR は LOW KEEP OFF（低域も原音と一緒に左へ）にして、右 = ダブルだけにする
            twin::Params p; p.mode = mode; p.voices = 2; p.doubleDb = 0; p.width = 1.0f; p.lowKeepHz = mode == 1 ? 0.0f : 200.0f;
            twin::Engine e; e.prepare (sr); e.setParams (p); e.reset();
            const size_t J = (size_t) (3.2 * sr) / 256 * 256;   // 伸ばした音の途中（ブロックの頭）で飛ぶ。飛んだ先は無音
            Chans x = in; for (auto& c : x) std::fill (c.begin() + (long) J, c.end(), 0.0f);
            Chans o = x;
            for (size_t pos = 0; pos < n; pos += 256)
            {
                const int b = (int) std::min<size_t> (256, n - pos);
                const int64_t tl = pos < J ? (int64_t) pos : (int64_t) pos + 5000000;   // J から先は別の場所
                e.process (o[0].data() + pos, o[1].data() + pos, 2, b, tl);
            }
            // VOCAL: 原音はすぐ切りかわるので、ダブルも 5 ms で消える（7 ms 後に −50 dB 未満。残りは LOW KEEP の HP の余韻）
            // GUITAR: 原音は 40 ms 遅れて出るので、ダブル（右）は飛んだ所まで読み終えるまで鳴り続け（5〜15 ms 後に抜けない）、
            //         いちばん長い遅れ（65 ms）＋余裕の 76 ms 後には消える
            const auto& ch = mode == 1 ? o[1] : o[0];
            const size_t from = J + (size_t) ((mode == 1 ? 0.076 : 0.007) * sr);
            const double before = rmsDb (ch, J - (size_t) (0.03 * sr), J);
            const double after = rmsDb (ch, from, from + (size_t) (0.03 * sr));
            const double early = rmsDb (ch, J + (size_t) (0.005 * sr), J + (size_t) (0.015 * sr));
            check (e.getStats().jumps == 1 && after < -50 && after < before - 35 && (mode == 0 || early > before - 10),
                   std::string (mode ? "GUITAR" : "VOCAL") + ": 位置が飛んだとき、原音が切りかわるまでダブルが鳴り、その後は消える（飛ぶ前 " + std::to_string (before) +
                   " dB、5〜15 ms 後 " + std::to_string (early) + " dB、" + (mode ? "76" : "7") + " ms 後 " + std::to_string (after) + " dB）");
        }
        // 曲の位置がブロックごとに 1 サンプル揺れても（四捨五入・Varispeed）、飛んだとみなさずダブルが鳴り続ける
        {
            twin::Params p; p.mode = 0; p.voices = 2;
            twin::Engine e; e.prepare (sr); e.setParams (p); e.reset();
            Chans o = in; double t = 0;
            for (size_t pos = 0; pos < n; pos += 128)
            {
                const int b = (int) std::min<size_t> (128, n - pos);
                e.process (o[0].data() + pos, o[1].data() + pos, 2, b, (int64_t) std::llround (t) + ((pos / 128) % 2 ? 1 : 0));
                t += b * 0.98;   // 98 % の速さ
            }
            auto ref = runEngine (in, sr, p, 128);
            // ダブルだけ（出力 − 原音）の大きさで比べる。位置が違うので選ぶテイクは違うが、消えていなければ ±3 dB に入る
            std::vector<float> dd (n), dr (n);
            for (size_t i = 0; i < n; ++i) { dd[i] = o[0][i] - in[0][i]; dr[i] = ref[0][i] - in[0][i]; }
            const double d1 = rmsDb (dd, n / 4, n), d2 = rmsDb (dr, n / 4, n);
            check (e.getStats().jumps == 0 && std::fabs (d1 - d2) < 3.0, "曲の位置が揺れてもダブルが消えない（飛びの判定 " + std::to_string (e.getStats().jumps) +
                   " 回、ダブルの大きさ " + std::to_string (d1) + " dB / ふつう " + std::to_string (d2) + " dB）");
        }
        // GUITAR: ダブルが前と後ろを行き来しない（音ごとの遅れの跳びは 6 + 9·LOOSE ms まで）
        {
            twin::Params p; p.mode = 1; p.loose = 1.0f; p.take = 5;
            twin::Engine e; e.prepare (sr); e.setParams (p); e.reset();
            Chans o = in; float last = -1, maxJump = 0; int64_t seen = -1;
            for (size_t pos = 0; pos < n; pos += 64)
            {
                const int b = (int) std::min<size_t> (64, n - pos);
                e.process (o[0].data() + pos, o[1].data() + pos, 2, b, (int64_t) pos);
                if (e.getStats().onsets != seen) { seen = e.getStats().onsets; const float d = e.getStats().lastDelayMs[0]; if (last >= 0) maxJump = std::max (maxJump, std::fabs (d - last)); last = d; }
            }
            check (maxJump <= 15.5f, "GUITAR: 音ごとの遅れの跳び " + std::to_string (maxJump) + " ms（15 ms まで）");
        }
        // モノ出力（Logic のモノのトラック）: GUITAR の原音はそのままの大きさ（パンで −3 / −6 dB にならない）
        {
            twin::Params p; p.mode = 1; p.doubleDb = -100; p.lowKeepHz = 200;
            const size_t n2 = (size_t) sr * 2;
            Chans x (1, std::vector<float> (n2));
            for (size_t i = 0; i < n2; ++i) x[0][i] = (float) (0.5 * std::sin (2 * M_PI * 1000.0 * i / sr));
            auto o = runEngine (x, sr, p, 512, nullptr, false);
            const double g = rmsDb (o[0], n2 / 2, n2) - rmsDb (x[0], n2 / 2, n2);
            check (std::fabs (g) < 0.1, "モノ 1→1 の GUITAR: 原音の大きさがそのまま（" + std::to_string (g) + " dB）");
        }
        // モノ出力（Logic のモノのトラック）
        {
            twin::Params p; p.mode = 0; p.voices = 2;
            Chans m1 { in[0] };
            auto o = runEngine (m1, sr, p, 512, nullptr, false);
            bool fin = true; for (float v : o[0]) if (! std::isfinite (v)) fin = false;
            check (fin, "モノ 1→1: NaN なし");
        }
        // 無音を入れたら無音
        {
            twin::Params p; p.mode = 0; p.voices = 4; p.doubleDb = 0;
            Chans z (2, std::vector<float> ((size_t) sr * 2, 0.0f));
            auto o = runEngine (z, sr, p, 512);
            double mx = 0; for (auto& c : o) for (float v : c) mx = std::max (mx, (double) std::fabs (v));
            check (mx == 0.0, "無音を入れたら無音");
        }

        // ---- 1.1.0: 語尾（TAIL）・長い音の揺れ（DRIFT）・DOUBLE ONLY ----
        // 倍音の多い伸ばした音（196 Hz）。env(t) で大きさを決める
        auto toneWith = [&] (double secs, auto env)
        {
            const size_t m = (size_t) (secs * sr);
            Chans x (2, std::vector<float> (m));
            for (size_t i = 0; i < m; ++i)
            {
                const double t = (double) i / sr;
                double v = 0; for (int h = 1; h <= 10; ++h) v += std::sin (2 * M_PI * 196.0 * h * t) / h;
                x[0][i] = x[1][i] = (float) (0.25 * v * env (t));
            }
            return x;
        };
        auto doublesOf = [] (const Chans& o, const Chans& x) { std::vector<float> d (o[0].size()); for (size_t i = 0; i < d.size(); ++i) d[i] = o[0][i] - x[0][i]; return d; };
        auto at = [&] (double t) { return (size_t) (t * sr); };
        const double S = 1.1;
        // 1) パッと切れる語尾（LOOSE 100%）: 原音が切れて 25 ms 後には、ダブルが −40 dB 未満
        {
            auto x = toneWith (1.6, [&] (double t) { return t < 0.1 ? 0.0 : (t < S ? std::min (1.0, (t - 0.1) / 0.01) : 0.0); });
            double worst = -200;
            for (int take = 1; take <= 8; ++take)
            {
                twin::Params p; p.mode = 0; p.voices = 1; p.doubleDb = 0; p.loose = 1.0f; p.take = take;
                auto d = doublesOf (runEngine (x, sr, p, 256), x);
                worst = std::max (worst, rmsDb (d, at (S + 0.025), at (S + 0.045)) - rmsDb (d, at (S - 0.3), at (S - 0.05)));
            }
            check (worst < -40.0, "語尾: 原音がパッと切れて 25 ms 後のダブル " + std::to_string (worst) + " dB（−40 dB 未満、LOOSE 100%・TAKE 1〜8 のいちばん大きいもの）");
        }
        // 1b) GUITAR でダブルが後ろのテイク: 原音（左、40 ms 遅れて出る）が止まって 10〜30 ms 後に、右のダブルも消えている
        {
            auto x = toneWith (1.6, [&] (double t) { return t < 0.1 ? 0.0 : (t < S ? std::min (1.0, (t - 0.1) / 0.01) : 0.0); });
            double worst = -200;
            for (int take : { 1, 2, 5 })   // trackSeed 0 でダブルが後ろになる TAKE
            {
                twin::Params p; p.mode = 1; p.doubleDb = 0; p.loose = 1.0f; p.width = 1.0f; p.lowKeepHz = 0; p.take = take;
                auto o = runEngine (x, sr, p, 256);
                worst = std::max (worst, rmsDb (o[1], at (S + 0.05), at (S + 0.07)) - rmsDb (o[1], at (S - 0.3), at (S - 0.05)));
            }
            check (worst < -35.0, "語尾: GUITAR（ダブルが後ろ）で原音が止まって 10〜30 ms 後の右のダブル " + std::to_string (worst) + " dB（−35 dB 未満）");
        }
        // 2) ゆっくり消える語尾（80 ms で −60 dB、指数で）: 原音が 30 dB 下がった所からの 30 ms で、ダブルがまだ大きく残っていない。
        //    語尾の処理なしだと、遅れて読むダブルはまだほとんど元の大きさ（LOOSE 100% で 43 ms 後ろ）
        {
            auto x = toneWith (1.6, [&] (double t) { return t < 0.1 ? 0.0 : (t < S ? std::min (1.0, (t - 0.1) / 0.01) : std::pow (10.0, -3.0 * (t - S) / 0.08)); });
            const double T30 = S + 0.04;   // 原音が −30 dB になる所
            double worstOn = -200, worstOff = -200;
            for (int take = 1; take <= 8; ++take)
                for (int tl = 0; tl < 2; ++tl)
                {
                    twin::Params p; p.mode = 0; p.voices = 1; p.doubleDb = 0; p.loose = 1.0f; p.take = take; p.tail = tl == 0;
                    auto d = doublesOf (runEngine (x, sr, p, 256), x);
                    const double r = rmsDb (d, at (T30), at (T30 + 0.03)) - rmsDb (d, at (S - 0.3), at (S - 0.05));
                    (tl == 0 ? worstOn : worstOff) = std::max (tl == 0 ? worstOn : worstOff, r);
                }
            check (worstOn < -15.0 && worstOff > worstOn + 8.0, "語尾: ゆっくり消える語尾で、原音が −30 dB の所からのダブル " + std::to_string (worstOn) +
                   " dB（−15 dB 未満。語尾の処理なしだと " + std::to_string (worstOff) + " dB）");
        }
        // 3) 20 ms の切れ目: 原音が戻っても、ダブルが切れ目を読み終えるまで、手前の母音の残りがポコッと戻らない
        {
            const double G0 = 1.1, G1 = 1.12;
            auto x = toneWith (2.0, [&] (double t) { return t < 0.1 ? 0.0 : (t < G0 ? std::min (1.0, (t - 0.1) / 0.01) : (t < G1 ? 0.0 : 1.0)); });
            double worst = -200; int checked = 0;
            for (int take = 1; take <= 8; ++take)
            {
                twin::Params p; p.mode = 0; p.voices = 1; p.doubleDb = 0; p.loose = 1.0f; p.take = take;
                twin::Engine e; e.prepare (sr); e.setParams (p); e.reset();
                Chans o = x; float dEst = 0, dNew = 0;
                for (size_t pos = 0; pos < o[0].size(); pos += 256)
                {
                    const int b = (int) std::min<size_t> (256, o[0].size() - pos);
                    e.process (o[0].data() + pos, o[1].data() + pos, 2, b, (int64_t) pos);
                    if (pos < at (G0)) dEst = e.getStats().lastDelayMs[0];
                    if (pos > at (G1 + 0.005) && dNew == 0) dNew = e.getStats().lastDelayMs[0];
                }
                auto d = doublesOf (o, x);
                const double steady = rmsDb (d, at (G0 - 0.3), at (G0 - 0.05));
                // 窓の終わり = min(手前の音の遅れ − 6 ms, 次の音のダブルの遅れ − 3 ms)（伸ばす間に音程のずれで遅れが 4〜5 ms 縮むため）
                const double dEnd = std::min ((double) dEst - 6.0, (double) dNew - 3.0);
                for (double t0 = G1 + 0.002; t0 + 0.002 < G1 + dEnd * 0.001; t0 += 0.002)
                { worst = std::max (worst, rmsDb (d, at (t0), at (t0 + 0.002)) - steady); ++checked; }
            }
            check (checked > 0 && worst < -15.0, "語尾: 20 ms の切れ目のあと、ダブルが切れ目を通り過ぎるまで戻らない（いちばん大きい所 " + std::to_string (worst) + " dB、−15 dB 未満）");
        }
        // 4) −12 dB のへこみ（30 ms）: ダブルの下げ量は 8 dB まで（「超えた分だけ」下げる。消したりしない）
        {
            const double D0 = 1.1;
            auto env = [&] (double t)
            {
                if (t < 0.1) return 0.0;
                double g = std::min (1.0, (t - 0.1) / 0.01);
                const double dip = 0.25119;   // −12 dB
                if (t >= D0 && t < D0 + 0.040)
                {
                    const double u = t - D0;
                    const double k = u < 0.005 ? u / 0.005 : (u < 0.035 ? 1.0 : (0.040 - u) / 0.005);
                    g *= 1.0 - (1.0 - dip) * k;
                }
                return g;
            };
            auto x = toneWith (1.6, env);
            double most = 0, least = 0;
            for (int take = 1; take <= 8; ++take)
            {
                twin::Params p; p.mode = 0; p.voices = 1; p.doubleDb = 0; p.loose = 1.0f; p.take = take;
                twin::Params q = p; q.tail = false;
                auto dOn = doublesOf (runEngine (x, sr, p, 256), x), dOff = doublesOf (runEngine (x, sr, q, 256), x);
                for (double t0 = D0; t0 < D0 + 0.12; t0 += 0.002)
                {
                    const double r = rmsDb (dOn, at (t0), at (t0 + 0.002)) - rmsDb (dOff, at (t0), at (t0 + 0.002));
                    most = std::min (most, r);
                }
                least = std::min (least, most);
            }
            check (most >= -8.5 && most < -2.0, "語尾: −12 dB のへこみでダブルを下げるのは " + std::to_string (-most) + " dB まで（8 dB まで、少しは下げる）");
        }
        // 5) 長い音の中の揺れ: ±3 セント以内・0 セントをまたがない（伸ばした 6 秒の音、LOOSE 100%・4 本）
        {
            auto x = toneWith (6.5, [&] (double t) { return t < 0.1 ? 0.0 : std::min (1.0, (t - 0.1) / 0.01); });
            twin::Params p; p.mode = 0; p.voices = 4; p.doubleDb = 0; p.loose = 1.0f; p.take = 2;
            twin::Stats st; runEngine (x, sr, p, 512, &st);
            check (st.driftMaxAbs > 0.3f && st.driftMaxAbs <= 3.0001f && st.driftMinRatio >= 0.4999f,
                   "揺れ: 幅 " + std::to_string (st.driftMaxAbs) + " セント（±3 まで）、ずれに対するいちばん小さい割合 " + std::to_string (st.driftMinRatio) + "（0.5 以上 = 0 をまたがない）");
            twin::Params g = p; g.mode = 1; twin::Stats sg; runEngine (x, sr, g, 512, &sg);
            check (sg.driftMaxAbs == 0.0f, "揺れ: GUITAR では揺らさない");
        }
        // 6) DOUBLE ONLY: 原音が残らない（ダブル無音なら出力も無音）
        for (int mode : { 0, 1 })
        {
            twin::Params p; p.mode = mode; p.doubleOnly = true; p.doubleDb = -100;
            auto o = runEngine (in, sr, p, 512);
            double mx = 0; for (auto& c : o) for (float v : c) mx = std::max (mx, (double) std::fabs (v));
            check (mx < 1.0e-6, std::string (mode ? "GUITAR" : "VOCAL") + " DOUBLE ONLY: 原音が残らない（いちばん大きい値 " + std::to_string (mx) + "）");
        }
        {
            // DOUBLE ONLY で、ダブルは ON のときと同じ（原音を足していないだけ）
            twin::Params a; a.mode = 0; a.voices = 2; a.doubleDb = -3; twin::Params b = a; b.doubleOnly = true;
            auto oa = runEngine (in, sr, a, 512), ob = runEngine (in, sr, b, 512);
            double md = 0; for (size_t i = 0; i < n; ++i) md = std::max (md, (double) std::fabs ((oa[0][i] - in[0][i]) - ob[0][i]));
            check (md < 1.0e-5, "VOCAL DOUBLE ONLY: ダブルはそのまま（差 " + std::to_string (md) + "）");
        }
        // 7) 語尾の処理で音の頭を下げない: 無音のあとの音（立ち上がり 3 ms・0.3 秒・間 0.2 秒）で、音の頭から 10〜70 ms のダブルが処理なしと −0.5 dB 以内（DSP 検査係）
        {
            auto x = toneWith (4.0, [&] (double t) { if (t < 0.2) return 0.0; const double u = std::fmod (t - 0.2, 0.5); return u < 0.3 ? std::min (1.0, u / 0.003) * std::min (1.0, (0.3 - u) / 0.005) : 0.0; });
            double worst = 0;
            for (int take = 1; take <= 8; ++take)
            {
                twin::Params p; p.mode = 0; p.voices = 1; p.doubleDb = 0; p.loose = 1.0f; p.take = take; twin::Params q = p; q.tail = false;
                auto dOn = doublesOf (runEngine (x, sr, p, 256), x), dOff = doublesOf (runEngine (x, sr, q, 256), x);
                for (int k = 0; k < 7; ++k) { const double t0 = 0.2 + 0.5 * k; worst = std::min (worst, rmsDb (dOn, at (t0 + 0.010), at (t0 + 0.070)) - rmsDb (dOff, at (t0 + 0.010), at (t0 + 0.070))); }
            }
            check (worst > -0.5, "語尾: 音の頭のダブルは下げない（処理なしとの差 " + std::to_string (worst) + " dB、−0.5 dB 以内）");
        }
        // 8) GUITAR で TAKE を「後ろ → 前」に変えても、ダブルが下げたまま固まらない（ミュートでダブルを下げている途中で変える。DSP 検査係）
        //    同じ再生の中で、ミュートの前と戻った後の「ダブル（右）− 原音」が 1.5 dB 以内（固まると −15 dB のまま）
        {
            const size_t m = (size_t) (4.0 * sr);
            Chans x (2, std::vector<float> (m));
            double ph = 0;
            for (size_t i = 0; i < m; ++i)
            {
                const double t = (double) i / sr; ph += 2 * M_PI * 147.0 / sr;
                double v = 0; for (int h = 1; h <= 10; ++h) v += std::sin (ph * h) / h;
                double g = t < 0.2 ? 0.0 : std::min (1.0, (t - 0.2) / 0.005);
                if (t >= 1.0 && t < 1.03) g *= 0.1; else if (t >= 1.03 && t < 1.23) g *= 0.1 + 0.9 * (t - 1.03) / 0.2;
                x[0][i] = x[1][i] = (float) (0.25 * v * g);
            }
            double worst = 0;
            for (int changeMs : { 1050, 1060, 1070 })
            {
                twin::Params p; p.mode = 1; p.doubleDb = 0; p.loose = 0.4f; p.width = 1.0f; p.lowKeepHz = 0; p.take = 1;   // 1 = 後ろ
                twin::Params q = p; q.take = 3;                                                                           // 3 = 前
                twin::Engine e; e.prepare (sr); e.setParams (p); e.reset();
                Chans o = x;
                for (size_t i = 0; i < m; i += 64)
                {
                    if (i == (size_t) (changeMs * 0.001 * sr) / 64 * 64) e.setParams (q);
                    e.process (o[0].data() + i, o[1].data() + i, 2, (int) std::min<size_t> (64, m - i), (int64_t) i);
                }
                const double before = rmsDb (o[1], at (0.5), at (0.9)) - rmsDb (x[0], at (0.5), at (0.9));
                const double after = rmsDb (o[1], at (2.0), at (3.5)) - rmsDb (x[0], at (2.0), at (3.5));
                worst = std::max (worst, std::fabs (after - before));
            }
            check (worst < 1.5, "語尾: GUITAR で TAKE を後ろ→前に変えても、ダブルの大きさが戻る（ミュートの前との差 " + std::to_string (worst) + " dB）");
        }
        // 9) 止まっていた長さが違っても、ダブルの大きさは同じ（語尾の区切りを曲の位置にそろえている。DSP 検査係: 直す前は 10 ms ごとに最大 1.2 dB 違った）
        //    サンプル単位では前（1.0.0）からある −80 dB ほどの小さな差があるので、10 ms ごとの大きさで比べる
        {
            twin::Params p; p.mode = 0; p.voices = 2; p.doubleDb = 0; p.loose = 1.0f; p.take = 4;
            std::vector<double> refLv; double worst = 0;
            for (int pre : { 0, 1, 333, 1001 })
            {
                twin::Engine e; e.prepare (sr); e.setParams (p); e.reset();
                std::vector<float> z ((size_t) pre + 1, 0.0f);
                int64_t free = 777;
                for (int k = 0; k < pre; k += 64) { const int b = std::min (64, pre - k); e.process (z.data(), z.data(), 2, b, free); free += b; }
                Chans o = in;
                for (size_t pos = 0; pos < n; pos += 256)
                {
                    const int b = (int) std::min<size_t> (256, n - pos);
                    e.process (o[0].data() + pos, o[1].data() + pos, 2, b, (int64_t) (480000 + pos));   // 曲の同じ所から再生
                }
                auto d = doublesOf (o, in);
                std::vector<double> lv;
                for (size_t a = (size_t) sr; a + (size_t) (0.01 * sr) < n; a += (size_t) (0.01 * sr)) lv.push_back (rmsDb (d, a, a + (size_t) (0.01 * sr)));
                if (refLv.empty()) refLv = lv;
                else for (size_t k = 0; k < lv.size(); ++k) if (refLv[k] > -60.0) worst = std::max (worst, std::fabs (lv[k] - refLv[k]));
            }
            check (worst < 0.05, "止まっていた長さが違っても、ダブルの 10 ms ごとの大きさは同じ（いちばん大きい差 " + std::to_string (worst) + " dB）");
        }
    }
    printf (fails ? "\n%d 個 NG\n" : "\n全部 OK\n", fails);
    return fails ? 1 : 0;
}

int main (int argc, char** argv)
{
    fesetenv (FE_DFL_DISABLE_DENORMS_ENV);
    if (argc >= 2 && std::string (argv[1]) == "test") return test();
    if (argc < 3) { fprintf (stderr, "usage: render in out.wav [k=v ...] | render test\n"); return 2; }
    Chans in; double sr = 0;
    if (! readAudio (argv[1], in, sr)) { fprintf (stderr, "読めない: %s\n", argv[1]); return 1; }
    auto kv = parseKV (argc, argv, 3);
    const double start = kv.count ("start") ? std::atof (kv["start"].c_str()) : 0, len = kv.count ("len") ? std::atof (kv["len"].c_str()) : 1e9;
    const size_t a = std::min (in[0].size(), (size_t) (start * sr)), b = std::min (in[0].size(), a + (size_t) (len * sr));
    for (auto& c : in) c = std::vector<float> (c.begin() + (long) a, c.begin() + (long) b);
    if (in.size() == 1) in.push_back (in[0]);
    auto prm = paramsFrom (kv);
    twin::Stats st;
    const int block = kv.count ("block") ? std::atoi (kv["block"].c_str()) : 512;
    auto out = runEngine (in, sr, prm, block, &st);
    if (kv.count ("drywet") && kv["drywet"] == "0") out = in;
    if (kv.count ("trimlat") && kv["trimlat"] == "1")   // 聴き比べ用: 申告した遅れの分を頭から取る
    {
        twin::Engine e; e.prepare (sr); e.setParams (prm);
        const size_t L = (size_t) e.latencySamples();
        for (auto& c : out) { c.erase (c.begin(), c.begin() + (long) std::min (L, c.size())); c.insert (c.end(), L, 0.0f); }
    }
    writeWav (argv[2], out, sr);
    printf ("%s: %.1f 秒、オンセット %lld 回（%.1f 回/秒）、つなぎ直し %lld 回、子音・息で閉じた割合 %.0f%%\n", argv[2], (double) out[0].size() / sr,
            (long long) st.onsets, st.onsets / ((double) out[0].size() / sr), (long long) st.forced, 100.0 * (1.0 - st.gate));
    return 0;
}
