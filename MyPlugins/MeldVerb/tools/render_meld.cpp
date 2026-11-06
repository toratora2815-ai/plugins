// テスト用: Meld Verb のエンジンだけで音を書き出す（JUCE なし）
// 使い方: render_meld in.wav out.wav [番号=値 ...]
//   番号は測定に使った参考プラグインのパラメーター番号と同じ並び（比べやすいように）
//   出力は参考プラグインと同じ 272 サンプル遅らせて書く（並べて比べるため）
#include "../Source/MeldEngine.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
using namespace meld;

static bool readWav (const char* path, std::vector<float>& L, std::vector<float>& R, int& sr)
{
    FILE* f = std::fopen (path, "rb"); if (! f) return false;
    char id[4]; uint32_t sz; std::fseek (f, 12, SEEK_SET); int ch = 2, bits = 32, fmt = 3;
    while (std::fread (id, 1, 4, f) == 4 && std::fread (&sz, 4, 1, f) == 1)
    {
        if (! std::memcmp (id, "fmt ", 4))
        {
            uint16_t fm, c; uint32_t rate; std::fread (&fm, 2, 1, f); std::fread (&c, 2, 1, f); std::fread (&rate, 4, 1, f);
            uint32_t br; uint16_t ba, bp; std::fread (&br, 4, 1, f); std::fread (&ba, 2, 1, f); std::fread (&bp, 2, 1, f);
            fmt = fm; ch = c; sr = (int) rate; bits = bp; std::fseek (f, (long) sz - 16, SEEK_CUR);
        }
        else if (! std::memcmp (id, "data", 4))
        {
            const size_t bps = (size_t) bits / 8, n = sz / bps / (size_t) ch;
            L.resize (n); R.resize (n);
            std::vector<uint8_t> b (sz); std::fread (b.data(), 1, sz, f);
            for (size_t i = 0; i < n; ++i)
                for (int c = 0; c < std::min (ch, 2); ++c)
                {
                    const uint8_t* p = b.data() + (i * (size_t) ch + (size_t) c) * bps;
                    float v = 0;
                    if (fmt == 3 && bits == 32) std::memcpy (&v, p, 4);
                    else if (bits == 16) { int16_t s; std::memcpy (&s, p, 2); v = s / 32768.0f; }
                    else if (bits == 24) { int32_t s = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); v = (float) (s / 2147483648.0); }
                    (c == 0 ? L : R)[i] = v;
                    if (ch == 1) R[i] = v;
                }
            break;
        }
        else std::fseek (f, (long) (sz + (sz & 1)), SEEK_CUR);
    }
    std::fclose (f); return true;
}
static void writeWav (const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    FILE* f = std::fopen (path, "wb"); uint32_t n = (uint32_t) L.size(), data = n * 8;
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + data); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (3); w16 (2); w32 ((uint32_t) sr); w32 ((uint32_t) sr * 8); w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (data);
    for (uint32_t i = 0; i < n; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}

static void apply (Params& p, int a, float v)
{
    auto eq = [&] (EqSettings& e, int base, float val)   // base = バイパスの番号
    {
        switch (a - base)
        {
            case 0: e.on = val < 0.5f; break;
            case 1: e.lowGain = val; break; case 2: e.lowFreq = val; break; case 3: e.lowSlope = val; break; case 4: e.lowShape = (int) val; break;
            case 5: e.highGain = val; break; case 6: e.highFreq = val; break; case 7: e.highSlope = val; break; case 8: e.highShape = (int) val; break;
            case 9: e.bellGain = val; break; case 10: e.bellFreq = val; break; case 11: e.bellQ = val; break;
        }
    };
    switch (a)
    {
        case 1: p.outputDb = v; break; case 2: p.mix = v; break;
        case 3: p.levelDb[0] = v; break; case 4: p.levelDb[1] = v; break; case 5: p.levelDb[2] = v; break;
        case 6: p.early.timeMs = v; break; case 7: p.early.size = v; break; case 8: p.early.diffusion = v; break; case 9: p.early.angle = v; break; case 10: p.early.lowpassHz = v; break;
        case 11: p.plate.timeSec = v; break; case 14: p.plate.size = v; break; case 15: p.plate.diffusion = v; break; case 16: p.plate.attackMs = v; break;
        case 17: p.plate.lowpassHz = v; break; case 18: p.plate.damping = v; break; case 19: p.plate.balance = v; break; case 20: p.plate.crossoverHz = v; break;
        case 21: p.hall.timeSec = v; break; case 24: p.hall.size = v; break; case 25: p.hall.diffusion = v; break; case 26: p.hall.attackMs = v; break;
        case 27: p.hall.lowpassHz = v; break; case 28: p.hall.damping = v; break; case 29: p.hall.balance = v; break; case 30: p.hall.crossoverHz = v; break;
        case 31: p.modRate = v; break; case 32: p.modDepth = v; break; case 33: p.modType = (int) v; break; case 34: p.predelayMs = v; break;
        case 61: p.smoothOn = v < 0.5f; break; case 62: p.smoothAmount = v; break;
        default:
            if (a >= 37 && a <= 48) eq (p.preEq, 37, v);
            else if (a >= 49 && a <= 60) eq (p.revEq, 49, v);
    }
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "render_meld in.wav out.wav [n=v ...]\n"); return 1; }
    std::vector<float> L, R; int sr = 48000;
    if (! readWav (argv[1], L, R, sr)) { std::fprintf (stderr, "read failed\n"); return 1; }
    Params p = defaultParams();
    // 調整用: t.<名前>=値 でエンジンの内部の定数を変えられる
    std::vector<std::pair<std::string, float>> tunes;
    for (int i = 3; i < argc; ++i)
    {
        std::string s = argv[i]; auto e = s.find ('=');
        if (e == std::string::npos) continue;
        const std::string k = s.substr (0, e); const float v = (float) std::atof (s.c_str() + e + 1);
        if (k.rfind ("t.", 0) == 0) tunes.push_back ({ k.substr (2), v });
        else apply (p, std::atoi (k.c_str()), v);
    }
    Engine eng; eng.prepare (sr);
    for (auto& [k, v] : tunes)
    {
        for (int w = 0; w < 2; ++w)
        {
            auto& t = (w == 0 ? eng.plateEngine() : eng.hallEngine()).tune;
            const char pre = w == 0 ? 'p' : 'h';
            if (k.size() < 3 || k[0] != pre || k[1] != '.') continue;
            const std::string n = k.substr (2);
#define T(x) if (n == #x) t.x = v;
            T(msPerSize) T(sizeRef) T(rtMul) T(c0) T(lfLoss) T(lfHz) T(lpMax0) T(lpMaxPerDamp) T(balMax) T(balPow) T(onsetMs) T(onsetPerSize)
            T(attackCurve) T(attackFloor) T(beta) T(sameSide) T(apG) T(modMaxMs) T(levelDb) T(inDiffG) T(spanPerSize) T(outHp) T(outLp) T(knee) T(airC) T(airHz) T(capSizePow) T(lpQ) T(shelfHz) T(shelfDb) T(gainPow) T(crossSide) T(lpStaticDb) T(sizeGainPow) T(onsetPerAttack) T(crossNeg)
#undef T
        }
        auto& e = eng.earlyEngine().tune;
        if (k.rfind ("e.", 0) == 0)
        {
            const std::string n = k.substr (2);
#define T(x) if (n == #x) e.x = v;
            T(firstMs) T(burstMs) T(burstPow) T(apG) T(apPow) T(decayDb) T(riseDb) T(levelDb) T(lrTrim) T(firstDry) T(tailDb) T(tailRt) T(tailPow) T(shadowDb) T(hpHz)
#undef T
        }
        auto& s = eng.smoothing().tune;
        if (k.rfind ("s.", 0) == 0)
        {
            const std::string n = k.substr (2);
#define T(x) if (n == #x) s.x = v;
            T(fastMs) T(slowMs) T(base) T(slope) T(lo) T(hi) T(glideMs)
#undef T
            eng.smoothing().setCoefs();
        }
    }
    eng.set (p, true);
    eng.reset();
    eng.set (p, true);
    const int lat = 272, n = (int) L.size();
    std::vector<float> oL ((size_t) n, 0.0f), oR ((size_t) n, 0.0f);
    std::vector<float> bl (512), br (512);
    for (int pos = 0; pos < n; pos += 512)
    {
        const int k = std::min (512, n - pos);
        std::copy (L.begin() + pos, L.begin() + pos + k, bl.begin());
        std::copy (R.begin() + pos, R.begin() + pos + k, br.begin());
        eng.process (bl.data(), br.data(), k);
        for (int i = 0; i < k; ++i)
            if (pos + i + lat < n) { oL[(size_t) (pos + i + lat)] = bl[(size_t) i]; oR[(size_t) (pos + i + lat)] = br[(size_t) i]; }
    }
    writeWav (argv[2], oL, oR, sr);
    std::printf ("ok %d\n", n);
    return 0;
}
