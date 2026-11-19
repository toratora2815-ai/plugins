#pragma once
// AIO Effecter の部品（モジュール）の共通の形。JUCE を使わない（将来 AIO Guitar のエフェクター欄にそのまま移せるように）。
//
// つまみの値は 0..1（Guitar Rig が保存しているのと同じ尺度）。部品の中で、測って合わせ込んだ式で実際の値（dB・ms など）に直す。
// 音はステレオ（L / R）をその場で書き換える。
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

namespace aiofx
{
constexpr int kMaxKnobs = 16;   // 0.2.0 で 12 → 16（Guitar Rig の部品はつまみが多いものがある）

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-12f)); }

// 画面に出す値の文字（0..1 → "−33 dB" など）。CTextFn は同じ部品のほかのつまみも見る（SYNC で音符を出すときなど）
using TextFn = std::string (*) (float v01);
using CTextFn = std::string (*) (float v01, const float* allKnobs);

struct KnobInfo
{
    const char* name;      // 画面の名前（英語・大文字）
    float def;             // 初期値 0..1
    int steps = 0;         // 0 = 連続、N = N 個から選ぶ（値は k / (N-1)）
    TextFn text = nullptr;
    CTextFn ctext = nullptr;
};

struct ModuleInfo
{
    const char* id;        // 保存に使う名前（変えない）
    const char* name;      // 画面の名前
    const char* category;  // 「DYNAMICS」「REVERB」…
    const char* hint;      // ひとこと説明（画面の下）
    std::vector<KnobInfo> knobs;
};

class Module
{
public:
    virtual ~Module() = default;
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void setKnob (int index, float v01) = 0;   // 何度呼んでもよい（変わったときだけ計算し直す）
    virtual void process (float* L, float* R, int n) = 0;
    virtual void setTempo (double /*bpm*/) {}   // ホストのテンポ（SYNC のある部品だけ使う）
    // ホストの曲の位置（このブロックの頭の拍の位置 ppq、再生中か）。揺れを小節にそろえる部品だけ使う（ppq < 0 = 分からない）
    virtual void setTransport (double /*ppq*/, bool /*playing*/) {}
    virtual int latency() const { return 0; }
    // 画面で部品を選んだ直後に 1 回だけ（Rack::service）。prepareToPlay で作り直したときは呼ばない（回り始めの演出など）
    virtual void startFresh() {}
    // 出口に越えてはいけない天井がある部品（MAXIMIZER）。ラックは reset のあと、この部品の段ではドライを混ぜない。
    // 先読み（遅れ）のあるリミッターを足すときは、必ず true を返すこと（DSP 検査 0.7.0 の 4 回目）
    virtual bool hasCeiling() const { return false; }
    // 遅れのある部品で、ホストの reset のあと先読みが埋まるまでドライを出して、15 ms で部品の音へ戻してよいか（コンプ類の「下げを消した直後の跳ね」を隠す）。
    // 音色を大きく変える部品（フィルター）は false（ドライが漏れる: DSP 検査 0.8.0）。天井のある部品も false
    virtual bool dryAfterReset() const { return ! hasCeiling(); }
    virtual double tailSeconds() const { return 0.0; }
    // 画面用（0..1、ゲートの開き具合・コンプの下げ量など。ないものは -1）
    virtual float meter() const { return -1.0f; }
};

// ---- 値の文字 ----
inline std::string fmt (float v, int decimals, const char* unit)
{
    char b[48];
    std::snprintf (b, sizeof (b), "%.*f%s", decimals, (double) v, unit);
    return b;
}
inline std::string fmtTime (float sec)
{
    if (sec < 1.0f) return fmt (sec * 1000.0f, sec < 0.01f ? 2 : (sec < 0.1f ? 1 : 0), " ms");
    return fmt (sec, sec < 10.0f ? 2 : 1, " s");
}
inline std::string fmtHz (float hz)
{
    if (hz >= 1000.0f) return fmt (hz / 1000.0f, hz < 10000.0f ? 2 : 1, " kHz");
    return fmt (hz, 0, " Hz");
}
inline std::string fmtPct (float v01) { return fmt (v01 * 100.0f, 0, " %"); }

// ---- 小さい道具 ----
// 1 次のなめらか（つまみの急な変化をならす）。目標へ時定数 tau 秒で近づく
struct Smooth
{
    float cur = 0.0f, target = 0.0f, a = 0.0f;
    void prepare (double sr, double tau) { a = (float) std::exp (-1.0 / (std::max (1.0e-4, tau) * sr)); }
    void set (float t, bool snap = false) { target = t; if (snap) cur = t; }
    float next() { cur = target + (cur - target) * a; return cur; }
    bool settled() const { return std::abs (cur - target) < 1.0e-6f; }
};

// 小数が同じか（== の警告を避ける書き方）
inline bool same (float a, float b) { return ! (a < b || b < a); }

// 小さすぎる値（デノーマル）を 0 に
inline float flush (float x) { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

// ---- 合わせ込み（測定の道具 tools/ だけが使う）: 名前 → 係数の場所 ----
// 部品のヘッダーで inline FitAdd xxx { { "部品.名前", &Fit().値 }, ... }; と並べておくと、道具が名前で書き換えられる
struct FitEntry { const char* name; float* p; };
inline std::vector<FitEntry>& fitEntries() { static std::vector<FitEntry> v; return v; }
struct FitAdd { FitAdd (std::initializer_list<FitEntry> l) { for (auto& e : l) fitEntries().push_back (e); } };
inline float* findFit (const std::string& name)
{
    for (auto& e : fitEntries()) if (name == e.name) return e.p;
    return nullptr;
}
} // namespace aiofx
