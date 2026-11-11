// MyPlugins 健康診断: 入っている自作 AU（メーカー Mypl）を 1 つずつ読み込んで、音の処理の異常を数値で探す道具。
// Logic も画面も使わない（自前のオフラインのホスト）。夜中に tools/health-check/nightly.sh から動く。
//
// ビルド: clang++ -std=c++17 -O2 -o hc hc.cpp -framework AudioToolbox -framework AudioUnit -framework CoreAudio -framework CoreFoundation
// 使い方:
//   hc list                          入っている自作 AU の一覧（種類・コード・版・名前）
//   hc params <コード>               つまみの一覧（名前・範囲・既定値・検査での扱い）
//   hc all <出力フォルダ> [--repo DIR] [--only コード,コード] [--groups basic,click,...] [--seed N] [--skip-auval]
//   hc run <コード> <group> <out.json> [--seed N] [--set K]
//        （all が検査ごとに別プロセスで呼ぶ。プラグインが落ちても固まっても、ほかの検査は続く）
// 検査（group）:
//   basic     読み込み・無音・音楽を通す（NaN・ピーク・既定の音量差・DC・CPU・遅れの申告・止めた後の減り方）・リセット
//   click     つまみを急に動かしたときのプチノイズ（1 つずつ）
//   random    つまみをランダムに 12 通り（発振・音割れ・NaN）。--set K でその 1 通りだけ
//   state     保存して読み直したら同じつまみ・同じ音か（保存漏れ）
//   block     ブロックの大きさ（1〜4096 サンプル）で音が変わらないか
//   rates     44.1 / 48 / 96 / 192 kHz で同じように動くか
//   mono      モノラルのトラック（1→1、1→2）で動くか
//   gainmatch GAIN MATCH を ON にしたら音量が合うか
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

extern char** environ;
using Chans = std::vector<std::vector<float>>;

// ---------------------------------------------------------------- 小物
static std::string fmt (const char* f, ...)
{
    char b[4096]; va_list a; va_start (a, f); vsnprintf (b, sizeof b, f, a); va_end (a); return b;
}
static OSType fcc (const std::string& s) { OSType v = 0; for (int i = 0; i < 4; ++i) v = (v << 8) | (uint8_t) (i < (int) s.size() ? s[(size_t) i] : ' '); return v; }
static std::string fourcc (OSType v) { std::string s; for (int i = 3; i >= 0; --i) s += (char) ((v >> (i * 8)) & 0xff); return s; }
static std::string cfToStd (CFStringRef s)
{
    if (! s) return {};
    char buf[2048];
    if (CFStringGetCString (s, buf, sizeof buf, kCFStringEncodingUTF8)) return buf;
    return {};
}
static void pump (double sec) { CFRunLoopRunInMode (kCFRunLoopDefaultMode, sec, false); }
static double db (double amp) { return amp > 1e-10 ? 20.0 * std::log10 (amp) : -200.0; }
static double pdb (double pw) { return pw > 1e-20 ? 10.0 * std::log10 (pw) : -200.0; }
static std::string lower (std::string s) { for (auto& c : s) c = (char) tolower ((unsigned char) c); return s; }
static bool has (const std::string& s, const char* k) { return s.find (k) != std::string::npos; }
static double nowSec() { mach_timebase_info_data_t tb; mach_timebase_info (&tb); return (double) mach_absolute_time() * tb.numer / tb.denom * 1e-9; }
static void say (const std::string& s) { printf ("%s\n", s.c_str()); fflush (stdout); }

// ---------------------------------------------------------------- JSON（書くだけ）
struct Json
{
    std::string out; std::vector<bool> nc;
    static std::string q (const std::string& s)
    {
        std::string r = "\"";
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\') { r += '\\'; r += (char) c; }
            else if (c == '\n') r += "\\n";
            else if (c < 0x20) r += fmt ("\\u%04x", c);
            else r += (char) c;
        }
        return r + "\"";
    }
    void comma() { if (! nc.empty() && nc.back()) out += ','; }
    void key (const std::string& k) { comma(); out += q (k) + ":"; if (! nc.empty()) nc.back() = false; }
    void raw (const std::string& v) { comma(); out += v; if (! nc.empty()) nc.back() = true; }
    void num (double v) { raw (std::isfinite (v) ? fmt ("%.6g", v) : std::string ("null")); }
    void str (const std::string& v) { raw (q (v)); }
    void boolean (bool b) { raw (b ? "true" : "false"); }
    void open (char c) { comma(); out += c; nc.push_back (false); }
    void close (char c) { out += c; nc.pop_back(); if (! nc.empty()) nc.back() = true; }
    void kn (const std::string& k, double v) { key (k); num (v); }
    void ks (const std::string& k, const std::string& v) { key (k); str (v); }
    void kb (const std::string& k, bool v) { key (k); boolean (v); }
};

// 見つけたこと（level: 異常 / 注意 / 参考）と、一覧表に出す数値
struct Finding { std::string level, msg; };
static std::vector<Finding> gF;
static std::map<std::string, std::string> gM;
static void addF (const char* level, const std::string& msg) { gF.push_back ({ level, msg }); }
static void metric (const std::string& k, double v) { gM[k] = std::isfinite (v) ? fmt ("%.6g", v) : "null"; }

// ---------------------------------------------------------------- 試験の音
struct Rng
{
    uint32_t s; explicit Rng (uint32_t seed) : s (seed ? seed : 1u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bip() { return uni() * 2.0f - 1.0f; }
};
struct Pink   // Paul Kellet のピンクノイズ
{
    float b[7] = {};
    float next (float w)
    {
        b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f;
        b[2] = 0.96900f * b[2] + w * 0.1538520f; b[3] = 0.86650f * b[3] + w * 0.3104856f;
        b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
        float p = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f;
        b[6] = w * 0.115926f; return p;
    }
};
static void normRms (Chans& x, size_t a, size_t b, double targetDb)
{
    double p = 0; size_t n = 0;
    for (auto& c : x) { for (size_t i = a; i < b; ++i) p += (double) c[i] * c[i]; n += b - a; }
    double r = std::sqrt (p / (double) std::max<size_t> (1, n));
    double g = r > 0 ? std::pow (10.0, targetDb / 20.0) / r : 0;
    for (auto& c : x) for (size_t i = a; i < b; ++i) c[i] *= (float) g;
}
static void normPeak (Chans& x, size_t a, size_t b, double targetDb)
{
    double m = 0;
    for (auto& c : x) for (size_t i = a; i < b; ++i) m = std::max (m, (double) std::fabs (c[i]));
    double g = m > 0 ? std::pow (10.0, targetDb / 20.0) / m : 0;
    for (auto& c : x) for (size_t i = a; i < b; ++i) c[i] *= (float) g;
}
static void fadeEdges (Chans& x, size_t a, size_t b, size_t f)
{
    for (auto& c : x)
        for (size_t i = 0; i < f && a + i < b; ++i) { float g = (float) i / (float) f; c[a + i] *= g; c[b - 1 - i] *= g; }
}

// 7 秒の「音楽っぽい」試験の音: A ピンクノイズ −18 dBFS / B 和音のはじき（120 BPM の 8 分）/ C ドラム / D 大きいノイズ / E スイープ
struct Program { Chans x; double sr = 48000; size_t seg[6] = {}; size_t len = 0; };
static Program makeProgram (double sr)
{
    Program P; P.sr = sr;
    const double secs[6] = { 0, 2, 4, 5, 6, 7 };
    for (int i = 0; i < 6; ++i) P.seg[i] = (size_t) std::llround (secs[i] * sr);
    P.len = P.seg[5];
    P.x.assign (2, std::vector<float> (P.len, 0.0f));
    auto& L = P.x[0]; auto& R = P.x[1];
    Rng r (12345); Pink p1, p2, p3, p4;
    for (size_t i = P.seg[0]; i < P.seg[1]; ++i) { float a = p1.next (r.bip()), b = p2.next (r.bip()); L[i] = a; R[i] = 0.7f * a + 0.714f * b; }
    normRms (P.x, P.seg[0], P.seg[1], -18);
    {
        const double f[4] = { 110.0, 138.59, 164.81, 220.0 };
        for (size_t i = P.seg[1]; i < P.seg[2]; ++i)
        {
            double t = (double) (i - P.seg[1]) / sr; int k = (int) (t / 0.25); double tl = t - k * 0.25;
            double env = std::exp (-tl / 0.12) * 0.8 + 0.2 * std::exp (-tl / 0.6);
            double vL = 0, vR = 0;
            for (int n = 0; n < 4; ++n)
            {
                if (n > 1 && ((k + n) % 2) == 1) continue;
                double s = 0;
                for (int h = 1; h <= 10; ++h) { if (f[n] * h > sr * 0.45) break; s += std::sin (2 * M_PI * f[n] * h * t) / h; }
                vL += s * (n % 2 ? 0.6 : 1.0); vR += s * (n % 2 ? 1.0 : 0.6);
            }
            L[i] = (float) (vL * env); R[i] = (float) (vR * env);
        }
        normPeak (P.x, P.seg[1], P.seg[2], -10);
    }
    for (size_t i = P.seg[2]; i < P.seg[3]; ++i)
    {
        double t = (double) (i - P.seg[2]) / sr; int k = (int) (t / 0.25); double tl = t - k * 0.25; double v;
        if (k % 2 == 0) v = std::sin (2 * M_PI * (50 * tl + 120 * 0.03 * (1 - std::exp (-tl / 0.03)))) * std::exp (-tl / 0.15);
        else v = r.bip() * std::exp (-tl / 0.07) * 0.7 + std::sin (2 * M_PI * 185 * tl) * std::exp (-tl / 0.05) * 0.5;
        L[i] = (float) v; R[i] = (float) v;
    }
    normPeak (P.x, P.seg[2], P.seg[3], -6);
    for (size_t i = P.seg[3]; i < P.seg[4]; ++i) { float a = p3.next (r.bip()), b = p4.next (r.bip()); L[i] = a; R[i] = 0.8f * a + 0.6f * b; }
    normRms (P.x, P.seg[3], P.seg[4], -8);
    for (size_t i = P.seg[3]; i < P.seg[4]; ++i) { L[i] = std::clamp (L[i], -0.99f, 0.99f); R[i] = std::clamp (R[i], -0.99f, 0.99f); }
    {
        double f1 = 20, f2 = std::min (20000.0, 0.45 * sr), T = (double) (P.seg[5] - P.seg[4]) / sr, Lk = std::log (f2 / f1);
        for (size_t i = P.seg[4]; i < P.seg[5]; ++i)
        {
            double t = (double) (i - P.seg[4]) / sr;
            float v = (float) (0.25 * std::sin (2 * M_PI * f1 * T / Lk * (std::exp (t / T * Lk) - 1)));
            L[i] = v; R[i] = v;
        }
    }
    for (int s = 0; s < 5; ++s) fadeEdges (P.x, P.seg[s], P.seg[s + 1], (size_t) (0.005 * sr));
    return P;
}

struct MidiEv { size_t frame; uint8_t st, d1, d2; };
static std::vector<MidiEv> makeMidi (double sr)   // 音源用: 和音・単音の上り・強い和音・速い連打
{
    std::vector<MidiEv> v;
    auto at = [&] (double t, int st, int d1, int d2) { v.push_back ({ (size_t) (t * sr), (uint8_t) st, (uint8_t) d1, (uint8_t) d2 }); };
    for (int n : { 48, 52, 55 }) { at (0.1, 0x90, n, 90); at (1.9, 0x80, n, 0); }
    for (int k = 0; k < 16; ++k) { int n = 36 + k * 3; at (2.0 + k * 0.125, 0x90, n, 40 + k * 5); at (2.0 + k * 0.125 + 0.1, 0x80, n, 0); }
    for (int n : { 40, 47, 52, 56, 59, 64 }) { at (4.0, 0x90, n, 127); at (5.9, 0x80, n, 0); }
    for (int k = 0; k < 16; ++k) { at (6.0 + k * 0.0625, 0x90, 60, 100); at (6.0 + k * 0.0625 + 0.05, 0x80, 60, 0); }
    std::stable_sort (v.begin(), v.end(), [] (const MidiEv& a, const MidiEv& b) { return a.frame < b.frame; });
    return v;
}

// ---------------------------------------------------------------- AU を動かすホスト
static AudioStreamBasicDescription fmtFloat (double sr, UInt32 ch)
{
    AudioStreamBasicDescription f {};
    f.mSampleRate = sr; f.mFormatID = kAudioFormatLinearPCM;
    f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    f.mBytesPerPacket = 4; f.mFramesPerPacket = 1; f.mBytesPerFrame = 4; f.mChannelsPerFrame = ch; f.mBitsPerChannel = 32;
    return f;
}

struct Param
{
    AudioUnitParameterID id = 0; std::string name; float lo = 0, hi = 1, def = 0;
    UInt32 unit = 0, flags = 0; bool discrete = false, boolean = false, writable = true;
};
enum Use { kUse = 0, kNoRandom = 1, kNever = 2 };
// つまみの名前で扱いを決める: Plugin Link と GAIN MATCH は触らない。止める・覚える系はランダムに入れない
static int classify (const std::string& name)
{
    auto n = lower (name);
    if (has (n, "plugin link") || n == "link group" || has (n, "link match") || has (n, "gain match") || has (n, "auto gain") || n == "unity") return kNever;
    for (const char* k : { "bypass", "freeze", "infinite", "learn", "capture", "record", "write", "solo", "delta", "listen", "check", "tap" })
        if (has (n, k)) return kNoRandom;
    return kUse;
}
static bool isGainMatch (const std::string& name)
{
    auto n = lower (name);
    return has (n, "gain match") || has (n, "auto gain") || n == "unity";
}

struct Host
{
    AudioUnit au = nullptr; OSType type = 0;
    double sr = 48000; UInt32 inCh = 2, outCh = 2, maxFrames = 4096, nIn = 0, nOut = 0;
    const float* src[16] = {};
    std::vector<std::vector<float>> scratch;
    double pos = 0, tempo = 120; bool playing = true;

    Host() = default;
    Host (const Host&) = delete;
    ~Host() { if (au) { AudioUnitUninitialize (au); AudioComponentInstanceDispose (au); } }
    bool instrument() const { return type == kAudioUnitType_MusicDevice; }

    static OSStatus inCB (void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32 bus, UInt32 frames, AudioBufferList* io)
    {
        auto* h = (Host*) ref;
        for (UInt32 b = 0; b < io->mNumberBuffers; ++b)
        {
            if (io->mBuffers[b].mData == nullptr)
            {
                if (h->scratch.size() <= b) h->scratch.resize (b + 1);
                h->scratch[b].assign (std::max<UInt32> (frames, h->maxFrames), 0.0f);
                io->mBuffers[b].mData = h->scratch[b].data();
            }
            auto* d = (float*) io->mBuffers[b].mData;
            io->mBuffers[b].mDataByteSize = frames * 4;
            bool feed = bus == 0 && ! h->instrument() && b < h->inCh && h->src[b] != nullptr;
            if (feed) memcpy (d, h->src[b], frames * 4); else memset (d, 0, frames * 4);
        }
        return noErr;
    }
    static OSStatus cbBeat (void* ud, Float64* beat, Float64* tempo)
    {
        auto* h = (Host*) ud;
        if (beat) *beat = h->pos / h->sr * h->tempo / 60.0;
        if (tempo) *tempo = h->tempo;
        return noErr;
    }
    static OSStatus cbMus (void* ud, UInt32* next, Float32* num, UInt32* den, Float64* down)
    {
        auto* h = (Host*) ud; double b = h->pos / h->sr * h->tempo / 60.0;
        if (next) *next = (UInt32) std::llround ((1.0 - (b - std::floor (b))) * h->sr * 60.0 / h->tempo);
        if (num) *num = 4;
        if (den) *den = 4;
        if (down) *down = std::floor (b / 4.0) * 4.0;
        return noErr;
    }
    static OSStatus cbTr (void* ud, Boolean* playing, Boolean* rec, Boolean* changed, Float64* cur, Boolean* cyc, Float64* cs, Float64* ce)
    {
        auto* h = (Host*) ud;
        if (playing) *playing = h->playing;
        if (rec) *rec = false;
        if (changed) *changed = false;
        if (cur) *cur = h->pos;
        if (cyc) *cyc = false;
        if (cs) *cs = 0;
        if (ce) *ce = 0;
        return noErr;
    }
    bool open (AudioComponent c, std::string& err)
    {
        AudioComponentDescription d {}; AudioComponentGetDescription (c, &d); type = d.componentType;
        OSStatus st = AudioComponentInstanceNew (c, &au);
        if (st != noErr) { au = nullptr; err = fmt ("AudioComponentInstanceNew が失敗 (%d)", (int) st); return false; }
        return true;
    }
    void setRate (AudioUnitScope sc, UInt32 e)
    {
        AudioStreamBasicDescription f {}; UInt32 sz = sizeof f;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_StreamFormat, sc, e, &f, &sz) == noErr && f.mChannelsPerFrame > 0)
        { f.mSampleRate = sr; AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, sc, e, &f, sizeof f); }
    }
    HostCallbackInfo hci {};
    bool setup (double sr_, UInt32 in_, UInt32 out_, std::string& err)
    {
        sr = sr_; inCh = in_; outCh = out_;
        UInt32 sz = sizeof (UInt32);
        AudioUnitGetProperty (au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Input, 0, &nIn, &sz);
        sz = sizeof (UInt32);
        AudioUnitGetProperty (au, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &nOut, &sz);
        auto fo = fmtFloat (sr, outCh);
        OSStatus st = AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fo, sizeof fo);
        if (st != noErr) { err = fmt ("出力 %u ch・%.0f Hz を受け付けない (%d)", outCh, sr, (int) st); return false; }
        for (UInt32 e = 1; e < nOut; ++e) setRate (kAudioUnitScope_Output, e);
        if (nIn > 0)
        {
            if (! instrument())
            {
                auto fi = fmtFloat (sr, inCh);
                st = AudioUnitSetProperty (au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fi, sizeof fi);
                if (st != noErr) { err = fmt ("入力 %u ch・%.0f Hz を受け付けない (%d)", inCh, sr, (int) st); return false; }
            }
            else setRate (kAudioUnitScope_Input, 0);
            for (UInt32 e = 1; e < nIn; ++e) setRate (kAudioUnitScope_Input, e);
            AURenderCallbackStruct cb { inCB, this };
            for (UInt32 e = 0; e < nIn; ++e)
                AudioUnitSetProperty (au, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, e, &cb, sizeof cb);
        }
        UInt32 mf = maxFrames;
        AudioUnitSetProperty (au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &mf, sizeof mf);
        hci = {}; hci.hostUserData = this; hci.beatAndTempoProc = cbBeat; hci.musicalTimeLocationProc = cbMus; hci.transportStateProc2 = cbTr;
        AudioUnitSetProperty (au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &hci, sizeof hci);
        st = AudioUnitInitialize (au);
        if (st != noErr) { err = fmt ("AudioUnitInitialize が失敗 (%d)", (int) st); return false; }
        AudioUnitSetProperty (au, kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, 0, &hci, sizeof hci);
        pump (0.05);
        return true;
    }
    bool render (const float* const* in, float* const* out, UInt32 n, OSStatus* errOut)
    {
        for (UInt32 c = 0; c < 16; ++c) src[c] = (in != nullptr && c < inCh) ? in[c] : nullptr;
        std::vector<uint8_t> mem (offsetof (AudioBufferList, mBuffers) + sizeof (AudioBuffer) * outCh);
        auto* abl = (AudioBufferList*) mem.data();
        abl->mNumberBuffers = outCh;
        for (UInt32 c = 0; c < outCh; ++c) { abl->mBuffers[c].mNumberChannels = 1; abl->mBuffers[c].mDataByteSize = n * 4; abl->mBuffers[c].mData = out[c]; }
        AudioTimeStamp ts {}; ts.mSampleTime = pos; ts.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags fl = 0;
        OSStatus st = AudioUnitRender (au, &fl, &ts, 0, n, abl);
        for (UInt32 c = 0; c < outCh; ++c)
            if (abl->mBuffers[c].mData != nullptr && abl->mBuffers[c].mData != out[c]) memcpy (out[c], abl->mBuffers[c].mData, n * 4);
        pos += n;
        if (errOut) *errOut = st;
        return st == noErr;
    }
    void midi (uint8_t st, uint8_t d1, uint8_t d2, UInt32 off) { MusicDeviceMIDIEvent (au, st, d1, d2, off); }
    std::vector<Param> params()
    {
        std::vector<Param> r; UInt32 sz = 0; Boolean w = false;
        if (AudioUnitGetPropertyInfo (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &sz, &w) != noErr || sz == 0) return r;
        std::vector<AudioUnitParameterID> ids (sz / sizeof (AudioUnitParameterID));
        AudioUnitGetProperty (au, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        for (auto id : ids)
        {
            AudioUnitParameterInfo inf {}; UInt32 s2 = sizeof inf;
            if (AudioUnitGetProperty (au, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id, &inf, &s2) != noErr) continue;
            Param p; p.id = id; p.lo = inf.minValue; p.hi = inf.maxValue; p.def = inf.defaultValue; p.unit = inf.unit; p.flags = inf.flags;
            if ((inf.flags & kAudioUnitParameterFlag_HasCFNameString) && inf.cfNameString != nullptr)
            {
                p.name = cfToStd (inf.cfNameString);
                if (inf.flags & kAudioUnitParameterFlag_CFNameRelease) CFRelease (inf.cfNameString);
            }
            else p.name = inf.name;
            p.boolean = inf.unit == kAudioUnitParameterUnit_Boolean;
            p.discrete = p.boolean || inf.unit == kAudioUnitParameterUnit_Indexed;
            p.writable = (inf.flags & kAudioUnitParameterFlag_IsWritable) != 0 && (inf.flags & kAudioUnitParameterFlag_MeterReadOnly) == 0;
            r.push_back (p);
        }
        return r;
    }
    float get (AudioUnitParameterID id) { AudioUnitParameterValue v = 0; AudioUnitGetParameter (au, id, kAudioUnitScope_Global, 0, &v); return v; }
    void set (AudioUnitParameterID id, float v) { AudioUnitSetParameter (au, id, kAudioUnitScope_Global, 0, v, 0); }
    double latencySec() { Float64 v = 0; UInt32 sz = sizeof v; AudioUnitGetProperty (au, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0, &v, &sz); return v; }
    double tailSec() { Float64 v = 0; UInt32 sz = sizeof v; AudioUnitGetProperty (au, kAudioUnitProperty_TailTime, kAudioUnitScope_Global, 0, &v, &sz); return v; }
    CFPropertyListRef getState()
    {
        CFPropertyListRef p = nullptr; UInt32 sz = sizeof p;
        if (AudioUnitGetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p, &sz) != noErr) return nullptr;
        return p;
    }
    OSStatus setState (CFPropertyListRef p) { return AudioUnitSetProperty (au, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &p, sizeof p); }
    void reset() { AudioUnitReset (au, kAudioUnitScope_Global, 0); }
    std::vector<std::pair<int, int>> channelInfos()
    {
        std::vector<std::pair<int, int>> r; UInt32 sz = 0; Boolean w = false;
        if (AudioUnitGetPropertyInfo (au, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, &sz, &w) != noErr || sz == 0) return r;
        std::vector<AUChannelInfo> v (sz / sizeof (AUChannelInfo));
        AudioUnitGetProperty (au, kAudioUnitProperty_SupportedNumChannels, kAudioUnitScope_Global, 0, v.data(), &sz);
        for (auto& c : v) r.push_back ({ c.inChannels, c.outChannels });
        return r;
    }
};

static AudioComponent findComp (OSType sub)
{
    AudioComponentDescription d {}; d.componentSubType = sub; d.componentManufacturer = fcc ("Mypl");
    return AudioComponentFindNext (nullptr, &d);
}
static std::unique_ptr<Host> makeHost (AudioComponent c, double sr, UInt32 inCh, UInt32 outCh, std::string& err)
{
    auto h = std::make_unique<Host>();
    if (! h->open (c, err)) return nullptr;
    if (! h->setup (sr, inCh, outCh, err)) return nullptr;
    return h;
}

// ---------------------------------------------------------------- 音を通す
struct Out { Chans y; std::vector<double> cpu; int bad = 0; double firstBad = -1; OSStatus err = 0; double errAt = 0; };
using BlockFn = std::function<UInt32 (size_t)>;
static UInt32 fixed512 (size_t) { return 512; }

static Out run (Host& h, const Chans* in, size_t inLen, size_t total, BlockFn blockFn,
                const std::vector<MidiEv>* midi = nullptr, std::function<void (size_t)> before = nullptr)
{
    Out o; o.y.assign (h.outCh, std::vector<float> (total, 0.0f));
    std::vector<std::vector<float>> inb (std::max<UInt32> (1, h.inCh), std::vector<float> (h.maxFrames, 0.0f));
    std::vector<std::vector<float>> ob (h.outCh, std::vector<float> (h.maxFrames, 0.0f));
    mach_timebase_info_data_t tb; mach_timebase_info (&tb);
    size_t mi = 0, p = 0; int blocks = 0;
    while (p < total)
    {
        UInt32 n = (UInt32) std::min<size_t> (blockFn (p), total - p);
        n = std::max<UInt32> (1, std::min (n, h.maxFrames));
        if (before) before (p);
        const float* ip[16] = {};
        for (UInt32 c = 0; c < h.inCh; ++c)
        {
            for (UInt32 i = 0; i < n; ++i)
            {
                size_t k = p + i;
                inb[c][i] = (in != nullptr && k < inLen) ? (*in)[std::min<size_t> (c, in->size() - 1)][k] : 0.0f;
            }
            ip[c] = inb[c].data();
        }
        if (midi != nullptr && h.instrument())
            while (mi < midi->size() && (*midi)[mi].frame < p + n)
            {
                auto& e = (*midi)[mi];
                h.midi (e.st, e.d1, e.d2, e.frame > p ? (UInt32) (e.frame - p) : 0);
                ++mi;
            }
        float* op[16] = {};
        for (UInt32 c = 0; c < h.outCh; ++c) { std::fill (ob[c].begin(), ob[c].begin() + n, 0.0f); op[c] = ob[c].data(); }
        uint64_t t0 = mach_absolute_time();
        OSStatus st = noErr; bool ok = h.render (ip, op, n, &st);
        uint64_t t1 = mach_absolute_time();
        double ns = (double) (t1 - t0) * tb.numer / tb.denom;
        o.cpu.push_back (ns * 1e-9 / ((double) n / h.sr) * 100.0);
        if (! ok) { o.err = st; o.errAt = (double) p / h.sr; break; }
        for (UInt32 c = 0; c < h.outCh; ++c)
            for (UInt32 i = 0; i < n; ++i)
            {
                float v = op[c][i];
                if (! std::isfinite (v)) { if (o.bad == 0) o.firstBad = (double) (p + i) / h.sr; ++o.bad; v = 0; }
                o.y[c][p + i] = v;
            }
        p += n;
        if (++blocks % 64 == 0) pump (0);
    }
    return o;
}

// ---------------------------------------------------------------- 測る
static double meanPow (const Chans& y, size_t a, size_t b)
{
    double p = 0; size_t n = 0;
    for (auto& c : y) { size_t e = std::min (b, c.size()); for (size_t i = a; i < e; ++i) p += (double) c[i] * c[i]; n += e > a ? e - a : 0; }
    return n ? p / (double) n : 0;
}
static double peakAbs (const Chans& y, size_t a, size_t b)
{
    double m = 0;
    for (auto& c : y) { size_t e = std::min (b, c.size()); for (size_t i = a; i < e; ++i) m = std::max (m, (double) std::fabs (c[i])); }
    return m;
}
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run (double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
static void kCoeffs (double fs, Biquad& s1, Biquad& s2)   // BS.1770 の K 特性（どの周波数でも）
{
    {
        double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        double K = std::tan (M_PI * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q + K * K;
        s1.b0 = (Vh + Vb * K / Q + K * K) / a0; s1.b1 = 2.0 * (K * K - Vh) / a0; s1.b2 = (Vh - Vb * K / Q + K * K) / a0;
        s1.a1 = 2.0 * (K * K - 1.0) / a0; s1.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (M_PI * f0 / fs);
        double a0 = 1.0 + K / Q + K * K;
        s2.b0 = 1.0; s2.b1 = -2.0; s2.b2 = 1.0; s2.a1 = 2.0 * (K * K - 1.0) / a0; s2.a2 = (1.0 - K / Q + K * K) / a0;
    }
}
// K 特性をかけた平均の強さ（チャンネルの平均）。a より前もフィルターに通して、測り始めの癖をなくす
static double kPow (const Chans& y, size_t a, size_t b, double fs)
{
    double p = 0; size_t n = 0;
    for (auto& c : y)
    {
        Biquad s1, s2; kCoeffs (fs, s1, s2);
        size_t e = std::min (b, c.size());
        for (size_t i = 0; i < e; ++i) { double v = s2.run (s1.run (c[i])); if (i >= a) p += v * v; }
        n += e > a ? e - a : 0;
    }
    return n ? p / (double) n : 0;
}
static std::vector<double> winDb (const Chans& y, size_t a, size_t b, size_t w)   // w サンプルごとの強さ（チャンネルの大きい方）
{
    std::vector<double> r;
    for (size_t s = a; s + w <= b; s += w)
    {
        double m = 0;
        for (auto& c : y) { double p = 0; for (size_t i = s; i < s + w; ++i) p += (double) c[i] * c[i]; m = std::max (m, p / (double) w); }
        r.push_back (pdb (m));
    }
    return r;
}
static double slopePerSec (const std::vector<double>& v, size_t from, double dt)   // 直線で当てはめた傾き（dB/秒）
{
    if (v.size() < from + 2) return 0;
    double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = from; i < v.size(); ++i) { double x = (double) i * dt, y = v[i]; n += 1; sx += x; sy += y; sxx += x * x; sxy += x * y; }
    double d = n * sxx - sx * sx;
    return d != 0 ? (n * sxy - sx * sy) / d : 0;
}
static double relDiffDb (const Chans& a, const Chans& b, size_t from, size_t to)   // a に対する a−b の大きさ（dB）
{
    double pa = 0, pd = 0;
    for (size_t c = 0; c < std::min (a.size(), b.size()); ++c)
        for (size_t i = from; i < std::min ({ to, a[c].size(), b[c].size() }); ++i) { double d = (double) a[c][i] - b[c][i]; pa += (double) a[c][i] * a[c][i]; pd += d * d; }
    if (pa <= 1e-30) return pd <= 1e-30 ? -200.0 : 0.0;
    return pdb (pd / pa);
}
static void cpuStats (const std::vector<double>& cpu, size_t from, size_t to, double& avg, double& p99, double& mx)
{
    std::vector<double> v;
    for (size_t i = from; i < std::min (to, cpu.size()); ++i) v.push_back (cpu[i]);
    avg = p99 = mx = 0;
    if (v.empty()) return;
    double s = 0; for (double x : v) s += x;
    avg = s / (double) v.size();
    std::sort (v.begin(), v.end());
    p99 = v[std::min (v.size() - 1, (size_t) ((double) v.size() * 0.99))];
    mx = v.back();
}
// 遅れを測る: 入力（スイープ部分）と出力の相互相関がいちばん強いずれ
static void measureLatency (const Chans& x, const Chans& y, size_t a, size_t b, int maxLag, int& bestLag, double& bestCorr)
{
    size_t N = b - a, M = N + (size_t) maxLag;
    std::vector<float> X (N), Y (M, 0.0f);
    for (size_t i = 0; i < N; ++i) X[i] = x[0][a + i] + x[std::min<size_t> (1, x.size() - 1)][a + i];
    for (size_t i = 0; i < M; ++i) { size_t k = a + i; float s = 0; for (auto& c : y) if (k < c.size()) s += c[k]; Y[i] = s; }
    double ex = 0; for (float v : X) ex += (double) v * v;
    std::vector<double> cum (M + 1, 0.0);
    for (size_t i = 0; i < M; ++i) cum[i + 1] = cum[i] + (double) Y[i] * Y[i];
    bestLag = 0; bestCorr = 0;
    for (int l = 0; l <= maxLag; ++l)
    {
        float s = 0; const float* yp = Y.data() + l;
        for (size_t i = 0; i < N; ++i) s += X[i] * yp[i];
        double ey = cum[(size_t) l + N] - cum[(size_t) l];
        double c = (ex > 0 && ey > 0) ? (double) s / std::sqrt (ex * ey) : 0;
        if (std::fabs (c) > std::fabs (bestCorr)) { bestCorr = c; bestLag = l; }
    }
}

// ---------------------------------------------------------------- 検査: basic
static void testBasic (AudioComponent comp, Json& j)
{
    std::string err;
    double t0 = nowSec();
    auto hp = makeHost (comp, 48000, 2, 2, err);
    if (! hp) { addF ("異常", "読み込めない・初期化できない: " + err); j.ks ("error", err); return; }
    Host& h = *hp;
    double initMs = (nowSec() - t0) * 1000.0;
    int lat = (int) std::llround (h.latencySec() * 48000.0);
    double tail = h.tailSec();
    j.kn ("init_ms", initMs); j.kn ("latency_samples", lat); j.kn ("tail_sec", tail); j.kb ("instrument", h.instrument());
    metric ("init_ms", initMs); metric ("latency", lat);
    if (initMs > 1500) addF ("注意", fmt ("読み込みに %.1f 秒かかる（Logic の曲を開くのが遅くなる）", initMs / 1000.0));
    auto ps = h.params();
    j.kn ("num_params", (double) ps.size());
    j.key ("channel_configs"); j.open ('[');
    for (auto& c : h.channelInfos()) j.str (fmt ("%d-%d", c.first, c.second));
    j.close (']');
    j.key ("params"); j.open ('[');
    for (auto& p : ps)
    {
        j.open ('{'); j.kn ("id", p.id); j.ks ("name", p.name); j.kn ("min", p.lo); j.kn ("max", p.hi); j.kn ("default", p.def);
        j.kb ("discrete", p.discrete); j.kb ("writable", p.writable); j.close ('}');
    }
    j.close (']');

    // 1) 無音を 2 秒
    say ("phase: silence");
    auto s = run (h, nullptr, 0, 96000, fixed512);
    double silPk = db (peakAbs (s.y, 0, 96000)), silRms = pdb (meanPow (s.y, 48000, 96000));
    j.kn ("silence_peak_db", silPk); j.kn ("silence_rms_db", silRms);
    if (s.err) { addF ("異常", fmt ("無音を通すだけで処理が止まる（エラー %d）", (int) s.err)); return; }
    if (s.bad) addF ("異常", fmt ("無音を入れただけで NaN・無限大が %d 個出る", s.bad));
    else if (silPk > -100) addF ("参考", fmt ("無音を入れても音が出る（ピーク %.1f dBFS・平均 %.1f dBFS）。ノイズを足す仕様なら問題なし", silPk, silRms));

    // 2) 7 秒の試験の音 + 20 秒の無音
    say ("phase: program");
    auto P = makeProgram (48000); auto midi = makeMidi (48000);
    const size_t tailN = 20 * 48000, total = P.len + tailN;
    auto o = run (h, &P.x, P.len, total, fixed512, &midi);
    if (o.err) { addF ("異常", fmt ("音を通している途中で処理が止まる（エラー %d、%.2f 秒の所）", (int) o.err, o.errAt)); return; }
    if (o.bad) addF ("異常", fmt ("音楽を通すと NaN・無限大が出る（%d 個、最初は %.2f 秒の所）", o.bad, o.firstBad));
    size_t L0 = (size_t) std::max (0, lat);
    double pk = db (peakAbs (o.y, 0, std::min (total, P.len + L0 + 48000)));
    double inLoud = pdb (kPow (P.x, 0, P.len, 48000));
    double outLoud = pdb (kPow (o.y, L0, std::min (total, P.len + L0), 48000));
    double gain = outLoud - inLoud;
    j.kn ("peak_db", pk); j.kn ("gain_db", gain); j.kn ("out_loudness_db", outLoud);
    metric ("peak_db", pk); metric ("gain_db", h.instrument() ? NAN : gain);
    if (pk > 12) addF ("異常", fmt ("既定のつまみで出力のピークが %+.1f dBFS（音割れ・異常な持ち上がり）", pk));
    else if (pk > 6) addF ("注意", fmt ("既定のつまみで出力のピークが %+.1f dBFS（−8 dBFS の大きいノイズを入れた時）", pk));
    if (h.instrument() && pk < -100) addF ("参考", "音源: MIDI を送っても音が出ない（素材を読み込んでいない状態なら正常）");
    if (! h.instrument() && gain < -40) addF ("注意", fmt ("既定のつまみで音がほとんど出ない（入力より %.1f dB 小さい）", gain));
    double dcMax = 0;
    for (auto& c : o.y) { double m = 0; for (float v : c) m += v; dcMax = std::max (dcMax, std::fabs (m / (double) c.size())); }
    j.kn ("dc_db", db (dcMax));
    if (db (dcMax) > -50) addF ("注意", fmt ("直流（DC、0 からのずれ）が乗っている（%.1f dBFS）。コンプやリミッターの効きがずれる", db (dcMax)));

    // CPU（ブロック 512 = 10.7 ms ごと）
    size_t progBlocks = P.len / 512;
    double avg, p99, mx, tavg, tp99, tmx;
    cpuStats (o.cpu, 4, progBlocks, avg, p99, mx);
    cpuStats (o.cpu, progBlocks + 94, o.cpu.size(), tavg, tp99, tmx);   // 止めて 1 秒後から
    j.kn ("cpu_avg", avg); j.kn ("cpu_p99", p99); j.kn ("cpu_max", mx); j.kn ("cpu_tail_avg", tavg);
    metric ("cpu_avg", avg); metric ("cpu_p99", p99);
    if (avg > 25) addF ("注意", fmt ("重い（1 コアの %.0f%% を使う、48 kHz・512 サンプル）", avg));
    if (p99 > 60) addF ("注意", fmt ("ときどき重くなる瞬間がある（1%% のブロックで 1 コアの %.0f%% 以上）", p99));
    if (tavg > 3.0 * avg && tavg > 2.0) addF ("注意", fmt ("音を止めた後の無音の間に重くなる（%.1f%% → %.1f%%）。デノーマル（極小の数）の疑い", avg, tavg));

    // 遅れの申告が正しいか（スイープの所で相互相関）
    if (! h.instrument())
    {
        int maxLag = std::min (std::max (0, lat) + 4096, 24000), mLag = 0; double corr = 0;
        measureLatency (P.x, o.y, P.seg[4], P.seg[5], maxLag, mLag, corr);
        j.kn ("latency_measured", mLag); j.kn ("latency_corr", corr);
        if (std::fabs (corr) > 0.5 && std::abs (mLag - lat) > 8)
            addF ("異常", fmt ("遅れの申告がずれている（申告 %d・実際 %d サンプル、相関 %.2f）。Logic で音の位置がずれる", lat, mLag, corr));
        if (corr < -0.5) addF ("注意", fmt ("音の向き（位相）が反転している（相関 %.2f）", corr));
    }

    // 止めた後の減り方（0.5 秒ごと）
    auto tw = winDb (o.y, P.len + L0, std::min (total, P.len + L0 + tailN - 24000), 24000);
    double tailEnd = tw.empty() ? -200 : tw.back();
    double sl = slopePerSec (tw, tw.size() > 10 ? tw.size() - 10 : 0, 0.5);
    double decay = NAN;
    for (size_t i = 0; i < tw.size(); ++i) if (tw[i] < -90) { decay = (double) i * 0.5; break; }
    j.key ("tail_db"); j.open ('['); for (double v : tw) j.num (v); j.close (']');
    j.kn ("tail_end_db", tailEnd); j.kn ("tail_slope_db_s", sl); j.kn ("decay_to_m90_s", decay);
    metric ("tail_end_db", tailEnd); metric ("decay_s", decay);
    if (tailEnd > -70 && sl > -0.3)
    {
        if (silRms > -110 && std::fabs (tailEnd - silRms) < 6)
            addF ("参考", fmt ("音を止めた後も %.1f dBFS のノイズが一定で残る（無音を入れた時と同じ。足すノイズの仕様なら問題なし）", tailEnd));
        else
            addF ("注意", fmt ("音を止めて 20 秒たっても鳴り止まない（%.1f dBFS、減り方 %.2f dB/秒）。発振の疑い", tailEnd, sl));
    }
    if (std::isfinite (decay) && tail < 1e5 && decay > tail + 2.0)
        addF ("参考", fmt ("響きの長さの申告（%.1f 秒）より長く鳴る（−90 dBFS まで %.1f 秒）", tail, decay));

    // 3) リセット: 大きい音の途中で reset → 前の音が残らないか
    say ("phase: reset");
    Chans hot (2); for (int c = 0; c < 2; ++c) hot[(size_t) c].assign (P.x[(size_t) c].begin() + (long) P.seg[3], P.x[(size_t) c].begin() + (long) P.seg[4]);
    std::vector<MidiEv> hm { { 0, 0x90, 60, 110 }, { 0, 0x90, 64, 110 } };
    run (h, &hot, hot[0].size(), hot[0].size(), fixed512, &hm);
    h.reset();
    if (h.instrument()) for (int n : { 60, 64 }) h.midi (0x80, (uint8_t) n, 0, 0);
    auto r2 = run (h, nullptr, 0, 48000, fixed512);
    double after = pdb (meanPow (r2.y, (size_t) L0 + 2400, 48000));
    j.kn ("after_reset_db", after);
    if (after > -60 && after > silRms + 10)
        addF ("参考", fmt ("リセット（Logic が再生を止めた・位置を飛ばした時）の後も前の音が残る（%.1f dBFS）", after));
}

// ---------------------------------------------------------------- 検査: click（つまみを急に動かす）
static void hp5k (Biquad& a, Biquad& b, double fs)   // 4 次バターワースのハイパス 5 kHz
{
    auto mk = [&] (Biquad& q, double Q)
    {
        double w = 2 * M_PI * 5000.0 / fs, c = std::cos (w), al = std::sin (w) / (2 * Q), a0 = 1 + al;
        q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; q.b2 = (1 + c) / 2 / a0; q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0;
    };
    mk (a, 0.5411961); mk (b, 1.3065630);
}
static void testClick (AudioComponent comp, Json& j)
{
    std::string err; const double sr = 48000;
    auto hp = makeHost (comp, sr, 2, 2, err);
    if (! hp) { addF ("異常", "読み込めない: " + err); return; }
    Host& h = *hp;
    auto ps = h.params();
    double ph1 = 0, ph2 = 0;
    auto chunk = [&] (double sec)
    {
        size_t n = (size_t) (sec * sr); Chans x (2, std::vector<float> (n));
        for (size_t i = 0; i < n; ++i)
        {
            float v = (float) (0.25 * std::sin (ph1) + 0.12 * std::sin (ph2));
            ph1 += 2 * M_PI * 110.0 / sr; ph2 += 2 * M_PI * 523.25 / sr;
            if (ph1 > 2 * M_PI) ph1 -= 2 * M_PI;
            if (ph2 > 2 * M_PI) ph2 -= 2 * M_PI;
            x[0][i] = v; x[1][i] = v;
        }
        return run (h, &x, n, n, [] (size_t) { return (UInt32) 256; });
    };
    if (h.instrument()) for (int n : { 48, 55, 60, 64 }) h.midi (0x90, (uint8_t) n, 100, 0);
    chunk (0.6);
    const size_t W = 48;   // 1 ms
    // pre の後ろ（変える前）と post（変えた後）をつないでハイパスを通し、1 ms ごとの強さを比べる。
    // spike = 変えた所から spikeMs の間でいちばん強い所、ref = 変える前と、落ち着いた後でいちばん強い所
    auto score = [&] (const Out& pre, const Out& post, double spikeMs, double settleMs, double& spikeOut, double& refOut)
    {
        double refPre = 0, refPost = 0, spike = 0;
        for (size_t c = 0; c < h.outCh; ++c)
        {
            Biquad f1, f2; hp5k (f1, f2, sr);
            std::vector<double> e;
            std::vector<float> all (pre.y[c]); all.insert (all.end(), post.y[c].begin(), post.y[c].end());
            double acc = 0; size_t k = 0;
            for (size_t i = 0; i < all.size(); ++i)
            {
                double v = f2.run (f1.run (all[i])); acc += v * v;
                if (++k == W) { e.push_back (acc / (double) W); acc = 0; k = 0; }
            }
            size_t preW = pre.y[c].size() / W;
            for (size_t i = 20; i < preW && i < e.size(); ++i) refPre = std::max (refPre, e[i]);
            for (size_t i = preW; i < preW + (size_t) spikeMs && i < e.size(); ++i) spike = std::max (spike, e[i]);
            for (size_t i = preW + (size_t) settleMs; i < e.size(); ++i) refPost = std::max (refPost, e[i]);
        }
        double ref = std::max ({ refPre, refPost, 1e-14 });
        spikeOut = pdb (spike); refOut = pdb (ref);
        return (pdb (spike) > -80) ? pdb (spike) - pdb (ref) : 0.0;
    };
    struct Hit { std::string name; double db; };
    std::vector<Hit> rampHits, jumpHits, switchHits;
    j.key ("click"); j.open ('[');
    int tested = 0; double worstScore = -100;
    for (auto& p : ps)
    {
        if (! p.writable || classify (p.name) == kNever) continue;
        float a, b;
        if (p.discrete) { a = p.def; b = (p.def + 1 <= p.hi) ? p.def + 1 : p.def - 1; if (b < p.lo) continue; }
        else { a = p.lo + 0.25f * (p.hi - p.lo); b = p.lo + 0.75f * (p.hi - p.lo); }
        // 1) 急に動かす（画面でクリックした・段のあるオートメーション・プリセット）
        h.set (p.id, a); chunk (0.35);
        auto pre = chunk (0.2);
        h.set (p.id, b);
        auto post = chunk (0.3);
        int bad = pre.bad + post.bad;
        double sp = 0, rf = 0, sc = score (pre, post, 50, 80, sp, rf);
        // 2) 引っかかった連続のつまみは、オートメーションのように 512 サンプルごとに 10 段で動かしても鳴るか
        double rsc = NAN;
        if (! p.discrete && sc > 20)
        {
            h.set (p.id, a); chunk (0.35);
            auto pre2 = chunk (0.2);
            Out ramp; ramp.y.assign (h.outCh, {});
            for (int s = 1; s <= 10; ++s)
            {
                h.set (p.id, a + (b - a) * (float) s / 10.0f);
                size_t n = 512; Chans x (2, std::vector<float> (n));
                for (size_t i = 0; i < n; ++i)
                {
                    float v = (float) (0.25 * std::sin (ph1) + 0.12 * std::sin (ph2));
                    ph1 += 2 * M_PI * 110.0 / sr; ph2 += 2 * M_PI * 523.25 / sr;
                    if (ph1 > 2 * M_PI) ph1 -= 2 * M_PI;
                    if (ph2 > 2 * M_PI) ph2 -= 2 * M_PI;
                    x[0][i] = v; x[1][i] = v;
                }
                auto o = run (h, &x, n, n, [] (size_t) { return (UInt32) 512; });
                bad += o.bad;
                for (size_t c = 0; c < h.outCh; ++c) ramp.y[c].insert (ramp.y[c].end(), o.y[c].begin(), o.y[c].end());
            }
            auto post2 = chunk (0.3);
            for (size_t c = 0; c < h.outCh; ++c) ramp.y[c].insert (ramp.y[c].end(), post2.y[c].begin(), post2.y[c].end());
            double sp2 = 0, rf2 = 0;
            rsc = score (pre2, ramp, 110, 190, sp2, rf2);
        }
        h.set (p.id, p.def); chunk (0.2);
        j.open ('{'); j.ks ("name", p.name); j.kn ("id", p.id); j.kb ("discrete", p.discrete); j.kn ("from", a); j.kn ("to", b);
        j.kn ("score_db", sc); j.kn ("ramp_score_db", rsc); j.kn ("spike_db", sp); j.kn ("ref_db", rf); j.kn ("bad", bad); j.close ('}');
        if (bad) addF ("異常", fmt ("「%s」を動かすと NaN・無限大が出る", p.name.c_str()));
        else if (! p.discrete && std::isfinite (rsc) && rsc > 12) rampHits.push_back ({ p.name, rsc });
        else if (! p.discrete && sc > 20) jumpHits.push_back ({ p.name, sc });
        else if (p.discrete && sc > 30) switchHits.push_back ({ p.name, sc });
        worstScore = std::max (worstScore, sc);
        ++tested;
    }
    j.close (']');
    j.kn ("tested", tested);
    metric ("click_worst_db", worstScore);
    auto names = [] (std::vector<Hit> v)
    {
        std::sort (v.begin(), v.end(), [] (const Hit& x, const Hit& y) { return x.db > y.db; });
        std::string s;
        for (size_t i = 0; i < v.size() && i < 12; ++i) s += (i ? "、" : "") + v[i].name + fmt ("（%+.0f）", v[i].db);
        if (v.size() > 12) s += fmt (" ほか %zu 個", v.size() - 12);
        return s;
    };
    if (! rampHits.empty())
        addF ("注意", fmt ("オートメーションのようになめらかに動かしても、ジジッ・プチッと鳴るつまみ %zu 個（値がブロックごとに段で変わる＝なめらかにする処理がない疑い）: ", rampHits.size()) + names (rampHits));
    if (! jumpHits.empty())
        addF ("参考", fmt ("急に大きく動かした時だけプチッと鳴るつまみ %zu 個: ", jumpHits.size()) + names (jumpHits));
    if (! switchHits.empty())
        addF ("参考", fmt ("切り替えでプチッと鳴るもの %zu 個: ", switchHits.size()) + names (switchHits));
}

// ---------------------------------------------------------------- 検査: random
static bool isMixName (const std::string& name) { auto n = lower (name); return has (n, "mix") || has (n, "wet") || has (n, "dry"); }
static bool isOutputName (const std::string& name)
{
    auto n = lower (name);
    return has (n, "output") || n == "out" || has (n, "out trim") || has (n, "out gain") || has (n, "trim");
}
static std::vector<std::pair<Param, float>> randomValues (const std::vector<Param>& ps, Rng& rk, bool moderate, bool skipMix)
{
    std::vector<std::pair<Param, float>> r;
    for (auto& p : ps)
    {
        if (! p.writable || classify (p.name) != kUse) continue;
        if (skipMix && (isMixName (p.name) || isOutputName (p.name))) continue;
        float v;
        if (p.discrete) { int nst = std::max (1, (int) std::lround (p.hi - p.lo) + 1); v = p.lo + (float) (rk.next() % (uint32_t) nst); }
        else { float u = rk.uni(); if (moderate) u = 0.1f + 0.8f * u; v = p.lo + u * (p.hi - p.lo); }
        r.push_back ({ p, v });
    }
    return r;
}
static void writeValues (Json& j, const std::vector<std::pair<Param, float>>& vals)
{
    j.key ("values"); j.open ('[');
    for (auto& [p, v] : vals) { j.open ('['); j.str (p.name); j.num (p.id); j.num (v); j.close (']'); }
    j.close (']');
}
static void testRandom (AudioComponent comp, Json& j, uint32_t seed, int onlySet)
{
    auto P = makeProgram (48000);
    j.kn ("seed", seed);
    j.key ("sets"); j.open ('[');
    int flagged = 0;
    std::vector<std::string> ringList, hotList, heavyList;
    for (int k = 0; k < 12; ++k)
    {
        if (onlySet >= 0 && k != onlySet) continue;
        say (fmt ("phase: random %d", k));
        std::string err;
        auto hp = makeHost (comp, 48000, 2, 2, err);
        if (! hp) { addF ("異常", "読み込めない: " + err); break; }
        Host& h = *hp;
        Rng rk (seed * 7919u + (uint32_t) k * 104729u + 1u);
        bool moderate = (k % 2 == 0);
        auto vals = randomValues (h.params(), rk, moderate, false);
        for (auto& [p, v] : vals) h.set (p.id, v);
        pump (0.05);
        size_t off = (size_t) ((k % 4) * 1.25 * 48000), len = 72000, total = len + 3 * 48000;
        Chans x (2, std::vector<float> (len));
        for (int c = 0; c < 2; ++c) for (size_t i = 0; i < len; ++i) x[(size_t) c][i] = P.x[(size_t) c][off + i];
        std::vector<MidiEv> mid { { 0, 0x90, 48, 100 }, { 0, 0x90, 55, 100 }, { 0, 0x90, 64, 100 }, { 67200, 0x80, 48, 0 }, { 67200, 0x80, 55, 0 }, { 67200, 0x80, 64, 0 } };
        auto o = run (h, &x, len, total, fixed512, &mid);
        double pk = db (peakAbs (o.y, 0, total));
        auto tw = winDb (o.y, len, total, 12000);
        double te = tw.empty() ? -200 : tw.back(), sl = slopePerSec (tw, tw.size() > 6 ? tw.size() - 6 : 0, 0.25);
        double avg, p99, mx; cpuStats (o.cpu, 4, o.cpu.size(), avg, p99, mx);
        bool nan = o.bad > 0 || o.err != 0, hot = pk > 12, ring = te > -60 && sl > -0.5;
        j.open ('{'); j.kn ("set", k); j.kb ("moderate", moderate); j.kn ("peak_db", pk); j.kn ("tail_end_db", te); j.kn ("tail_slope", sl);
        j.kn ("bad", o.bad); j.kn ("err", o.err); j.kn ("cpu_avg", avg); j.kn ("cpu_p99", p99);
        if (nan || hot || ring || onlySet >= 0) writeValues (j, vals);
        j.close ('}');
        std::string how = moderate ? "ほどほど" : "端まで";
        if (o.err) addF ("異常", fmt ("ランダム設定 %d（%s）で処理が止まる（エラー %d）", k, how.c_str(), (int) o.err));
        if (o.bad) addF ("異常", fmt ("ランダム設定 %d（%s）で NaN・無限大が出る（%d 個）", k, how.c_str(), o.bad));
        if (pk > 40) addF ("異常", fmt ("ランダム設定 %d（%s）で出力のピークが %+.1f dBFS（壊れたような大きさ）", k, how.c_str(), pk));
        else if (hot) hotList.push_back (fmt ("%d（%s %+.1f dBFS）", k, how.c_str(), pk));
        if (ring) ringList.push_back (fmt ("%d（%s 3 秒後 %.0f dBFS・%+.1f dB/秒）", k, how.c_str(), te, sl));
        if (avg > 40) heavyList.push_back (fmt ("%d（%s %.0f%%）", k, how.c_str(), avg));
        if (nan || hot || ring) ++flagged;
    }
    j.close (']');
    metric ("random_flagged", flagged);
    auto join = [] (const std::vector<std::string>& v) { std::string s; for (size_t i = 0; i < v.size(); ++i) s += (i ? "、" : "") + v[i]; return s; };
    if (! ringList.empty())
        addF ("注意", fmt ("音を止めても鳴り止まないランダム設定が %zu 個（発振の疑い。フィードバックを上げきった時など仕様のこともある）: ", ringList.size()) + join (ringList));
    if (! hotList.empty())
        addF ("注意", fmt ("出力のピークが +12 dBFS を超えたランダム設定が %zu 個（GAIN・OUTPUT を上げきった時など仕様のこともある）: ", hotList.size()) + join (hotList));
    if (! heavyList.empty())
        addF ("注意", fmt ("重くなるランダム設定が %zu 個: ", heavyList.size()) + join (heavyList));
}

// ---------------------------------------------------------------- 検査: state（保存して読み直す）
static void testState (AudioComponent comp, Json& j, uint32_t seed)
{
    std::string err;
    auto A = makeHost (comp, 48000, 2, 2, err), B = makeHost (comp, 48000, 2, 2, err), C = makeHost (comp, 48000, 2, 2, err);
    if (! A || ! B || ! C) { addF ("異常", "読み込めない: " + err); return; }
    auto ps = A->params();
    Rng rk (seed ^ 0x5eedu);
    auto vals = randomValues (ps, rk, true, false);
    for (auto& [p, v] : vals) { A->set (p.id, v); C->set (p.id, v); }
    pump (0.1);
    CFPropertyListRef st = A->getState();
    if (st == nullptr) { addF ("異常", "状態（ClassInfo）を取り出せない。Logic に保存されない"); return; }
    OSStatus rs = B->setState (st);
    CFRelease (st);
    pump (0.2);
    if (rs != noErr) addF ("異常", fmt ("保存した状態を読み込めない（エラー %d）", (int) rs));
    std::vector<std::string> mism;
    j.key ("mismatch"); j.open ('[');
    for (auto& p : ps)
    {
        if (! p.writable) continue;
        float va = A->get (p.id), vb = B->get (p.id);
        if (std::fabs (va - vb) > 1e-3f * std::max (1e-6f, p.hi - p.lo))
        { mism.push_back (p.name); j.open ('['); j.str (p.name); j.num (va); j.num (vb); j.close (']'); }
    }
    j.close (']');
    if (! mism.empty())
    {
        std::string names; for (size_t i = 0; i < mism.size() && i < 8; ++i) names += (i ? "、" : "") + mism[i];
        addF ("異常", fmt ("保存して開き直すと戻らないつまみがある（%zu 個: %s）", mism.size(), names.c_str()));
    }
    say ("phase: state audio");
    auto P = makeProgram (48000); auto midi = makeMidi (48000);
    size_t total = P.len + 2 * 48000;
    A->reset(); B->reset(); C->reset();
    auto oa = run (*A, &P.x, P.len, total, fixed512, &midi);
    auto ob = run (*B, &P.x, P.len, total, fixed512, &midi);
    auto oc = run (*C, &P.x, P.len, total, fixed512, &midi);
    double dAC = relDiffDb (oa.y, oc.y, 0, total), dAB = relDiffDb (oa.y, ob.y, 0, total);
    double la = pdb (kPow (oa.y, 0, total, 48000)), lb = pdb (kPow (ob.y, 0, total, 48000));
    j.kn ("det_diff_db", dAC); j.kn ("state_diff_db", dAB); j.kn ("loud_a", la); j.kn ("loud_b", lb);
    writeValues (j, vals);
    metric ("state_diff_db", dAB);
    bool silent = la < -150 && lb < -150;
    if (silent) return;
    if (dAC < -80 && dAB > -60)
        addF ("異常", fmt ("保存して開き直すと音が変わる（差 %.0f dB）。つまみ以外の状態が保存されていない疑い", dAB));
    else if (dAC >= -80)
    {
        addF ("参考", fmt ("同じ設定でも毎回少し違う音（ランダムな揺れなど、差 %.0f dB）。保存の検査は音量だけで比べた", dAC));
        if (std::fabs (la - lb) > 1.0) addF ("注意", fmt ("保存して開き直すと音量が %.1f dB 変わる", lb - la));
    }
}

// ---------------------------------------------------------------- 検査: block（ブロックの大きさ）
static void testBlock (AudioComponent comp, Json& j)
{
    std::string err;
    auto D = makeHost (comp, 48000, 2, 2, err), E = makeHost (comp, 48000, 2, 2, err), F = makeHost (comp, 48000, 2, 2, err);
    if (! D || ! E || ! F) { addF ("異常", "読み込めない: " + err); return; }
    auto P = makeProgram (48000); auto midi = makeMidi (48000);
    size_t len = P.seg[4], total = len + 48000;
    static const UInt32 seq[] = { 1, 3, 64, 17, 512, 4096, 999, 2, 128, 2048, 333, 1, 1024, 7, 4096, 255 };
    size_t si = 0;
    auto od = run (*D, &P.x, len, total, fixed512, &midi);
    auto of = run (*F, &P.x, len, total, fixed512, &midi);
    auto oe = run (*E, &P.x, len, total, [&] (size_t) { return seq[si++ % (sizeof seq / sizeof seq[0])]; }, &midi);
    double det = relDiffDb (od.y, of.y, 0, total), blk = relDiffDb (od.y, oe.y, 0, total);
    j.kn ("det_diff_db", det); j.kn ("block_diff_db", blk); j.kn ("bad", oe.bad); j.kn ("err", oe.err);
    metric ("block_diff_db", blk);
    if (oe.err) addF ("異常", fmt ("ブロックの大きさを変えると処理が止まる（エラー %d）", (int) oe.err));
    if (oe.bad) addF ("異常", fmt ("ブロックの大きさを変えると NaN・無限大が出る（%d 個）", oe.bad));
    if (pdb (meanPow (od.y, 0, total)) < -150) return;
    if (det < -100)
    {
        if (blk > -30) addF ("注意", fmt ("ブロックの大きさ（1〜4096）で音が大きく変わる（差 %.0f dB）。Logic のバッファ設定や書き出しで音が変わる", blk));
        else if (blk > -60) addF ("参考", fmt ("ブロックの大きさで音が少し変わる（差 %.0f dB）", blk));
    }
}

// ---------------------------------------------------------------- 検査: rates（サンプルレート）
static void testRates (AudioComponent comp, Json& j)
{
    j.key ("rates"); j.open ('[');
    double g48 = NAN, t48 = NAN;
    for (double sr : { 48000.0, 44100.0, 96000.0, 192000.0 })
    {
        say (fmt ("phase: rate %.0f", sr));
        std::string err;
        auto hp = makeHost (comp, sr, 2, 2, err);
        j.open ('{'); j.kn ("sr", sr);
        if (! hp) { j.ks ("error", err); j.close ('}'); addF ("注意", fmt ("%.1f kHz で開けない（%s）", sr / 1000, err.c_str())); continue; }
        Host& h = *hp;
        auto P = makeProgram (sr); auto midi = makeMidi (sr);
        size_t total = P.len + (size_t) (8 * sr);
        int lat = (int) std::llround (h.latencySec() * sr);
        auto o = run (h, &P.x, P.len, total, fixed512, &midi);
        size_t L0 = (size_t) std::max (0, lat);
        double gain = pdb (kPow (o.y, L0, std::min (total, P.len + L0), sr)) - pdb (kPow (P.x, 0, P.len, sr));
        double pk = db (peakAbs (o.y, 0, total));
        auto tw = winDb (o.y, total - (size_t) (sr * 0.5), total, (size_t) (sr * 0.5));
        double te = tw.empty() ? -200 : tw.back();
        double avg, p99, mx; cpuStats (o.cpu, 4, P.len / 512, avg, p99, mx);
        j.kn ("gain_db", gain); j.kn ("peak_db", pk); j.kn ("tail_end_db", te); j.kn ("latency_ms", lat / sr * 1000.0);
        j.kn ("cpu_avg", avg); j.kn ("bad", o.bad); j.kn ("err", o.err); j.close ('}');
        if (sr == 48000.0) { g48 = gain; t48 = te; metric ("cpu96_avg", NAN); continue; }
        if (sr == 96000.0) metric ("cpu96_avg", avg);
        if (o.err) addF ("異常", fmt ("%.1f kHz で処理が止まる（エラー %d）", sr / 1000, (int) o.err));
        if (o.bad) addF ("異常", fmt ("%.1f kHz で NaN・無限大が出る（%d 個）", sr / 1000, o.bad));
        if (! h.instrument() && std::isfinite (g48) && std::fabs (gain - g48) > 1.5)
            addF ("注意", fmt ("%.1f kHz だと音量が 48 kHz と %+.1f dB 違う", sr / 1000, gain - g48));
        if (te > -70 && std::isfinite (t48) && t48 < -90)
            addF ("注意", fmt ("%.1f kHz だと音を止めても鳴り止まない（%.1f dBFS）", sr / 1000, te));
        if (pk > 12) addF ("注意", fmt ("%.1f kHz で出力のピークが %+.1f dBFS", sr / 1000, pk));
        if (avg > 60) addF ("注意", fmt ("%.1f kHz だと重い（1 コアの %.0f%%）", sr / 1000, avg));
    }
    j.close (']');
}

// ---------------------------------------------------------------- 検査: mono（モノラルのトラック）
static bool supports (const std::vector<std::pair<int, int>>& ci, int in, int out)
{
    if (ci.empty()) return in == out;
    for (auto& [a, b] : ci)
    {
        if (a == in && b == out) return true;
        if (a == -1 && b == -1 && in == out) return true;
        if (a == -1 && b == -2) return true;
        if (a == -2 && b == -1) return true;
        if (a == -1 && b == out) return true;
        if (a == in && b == -1) return true;
        if (a < -2 && in <= -a && b == out) return true;
        if (b < -2 && out <= -b && a == in) return true;
    }
    return false;
}
static void testMono (AudioComponent comp, Json& j)
{
    std::string err;
    auto probe = makeHost (comp, 48000, 2, 2, err);
    if (! probe) { addF ("異常", "読み込めない: " + err); return; }
    if (probe->instrument()) { j.ks ("skip", "instrument"); return; }
    auto ci = probe->channelInfos();
    probe.reset();
    auto P = makeProgram (48000);
    bool any = false;
    j.key ("configs"); j.open ('[');
    for (auto io : { std::pair<int, int> { 1, 1 }, std::pair<int, int> { 1, 2 } })
    {
        j.open ('{'); j.ks ("config", fmt ("%d-%d", io.first, io.second));
        bool sup = supports (ci, io.first, io.second);
        j.kb ("listed", sup);
        if (! sup) { j.close ('}'); continue; }
        say (fmt ("phase: mono %d-%d", io.first, io.second));
        auto hp = makeHost (comp, 48000, (UInt32) io.first, (UInt32) io.second, err);
        if (! hp) { j.ks ("error", err); j.close ('}'); addF ("注意", fmt ("モノラル（%d→%d）に対応と言っているのに開けない（%s）", io.first, io.second, err.c_str())); continue; }
        any = true;
        Chans mono { P.x[0] };
        size_t total = P.len + 4 * 48000;
        auto o = run (*hp, &mono, P.len, total, fixed512);
        int lat = (int) std::llround (hp->latencySec() * 48000);
        double gain = pdb (kPow (o.y, (size_t) std::max (0, lat), P.len + (size_t) std::max (0, lat), 48000)) - pdb (kPow (mono, 0, P.len, 48000));
        double pk = db (peakAbs (o.y, 0, total));
        j.kn ("gain_db", gain); j.kn ("peak_db", pk); j.kn ("bad", o.bad); j.kn ("err", o.err);
        if (o.err) addF ("異常", fmt ("モノラル（%d→%d）で処理が止まる（エラー %d）", io.first, io.second, (int) o.err));
        if (o.bad) addF ("異常", fmt ("モノラル（%d→%d）で NaN・無限大が出る（%d 個）", io.first, io.second, o.bad));
        if (pk > 12) addF ("注意", fmt ("モノラル（%d→%d）で出力のピークが %+.1f dBFS", io.first, io.second, pk));
        if (io.second == 2)
        {
            double l = pdb (meanPow ({ o.y[0] }, 0, total)), r = pdb (meanPow ({ o.y[1] }, 0, total));
            j.kn ("left_db", l); j.kn ("right_db", r);
            if ((l < -100) != (r < -100)) addF ("注意", fmt ("モノラル→ステレオ（1→2）で片側しか音が出ない（左 %.0f・右 %.0f dBFS）", l, r));
        }
        j.close ('}');
    }
    j.close (']');
    if (! any) addF ("参考", "モノラルのトラック（1→1・1→2）に対応していない（Logic ではステレオのトラックにだけ挿せる）");
}

// ---------------------------------------------------------------- 検査: gainmatch
static void testGainMatch (AudioComponent comp, Json& j, uint32_t seed)
{
    std::string err;
    auto N = makeHost (comp, 48000, 2, 2, err), G = makeHost (comp, 48000, 2, 2, err);
    if (! N || ! G) { addF ("異常", "読み込めない: " + err); return; }
    auto ps = N->params();
    const Param* gm = nullptr;
    for (auto& p : ps) if (p.writable && isGainMatch (p.name)) gm = &p;
    j.kb ("present", gm != nullptr);
    if (gm == nullptr || N->instrument()) return;
    // つまみはほどほどにランダム。ただし MIX・OUTPUT は動かさない（OUTPUT は GAIN MATCH の後でかけるプラグインがある）。
    // MIX の既定が 90% 以上（センド使い）だと GAIN MATCH は仕様で効かないので、30% にして試す
    Rng rk (seed ^ 0x6a1du);
    auto vals = randomValues (ps, rk, true, true);
    for (auto& p : ps)
        if (p.writable && ! p.discrete && isMixName (p.name) && p.def >= p.lo + 0.9f * (p.hi - p.lo))
            vals.push_back ({ p, p.lo + 0.3f * (p.hi - p.lo) });
    for (auto& [p, v] : vals) { N->set (p.id, v); G->set (p.id, v); }
    N->set (gm->id, gm->lo); G->set (gm->id, gm->hi);
    pump (0.2);
    // 変わらない音（ピンクノイズ −18 dBFS）で比べる。音楽だと、歪み系は「覚えた区間」と「比べる区間」の中身の違いで差が出てしまう
    size_t total = 16 * 48000;
    Chans x (2, std::vector<float> (total));
    {
        Rng r (777); Pink p1, p2;
        for (size_t i = 0; i < total; ++i) { float u = p1.next (r.bip()), v = p2.next (r.bip()); x[0][i] = u; x[1][i] = 0.7f * u + 0.714f * v; }
        normRms (x, 0, total, -18);
    }
    auto on = run (*N, &x, total, total, fixed512);
    auto og = run (*G, &x, total, total, fixed512);
    int lat = (int) std::llround (N->latencySec() * 48000);
    size_t a = total - 4 * 48000;
    double in = pdb (kPow (x, a - (size_t) std::max (0, lat), total - (size_t) std::max (0, lat), 48000));
    double nat = pdb (kPow (on.y, a, total, 48000)) - in, res = pdb (kPow (og.y, a, total, 48000)) - in;
    j.kn ("natural_db", nat); j.kn ("residual_db", res); j.kn ("bad", og.bad);
    // 1 秒ごとの「ON − OFF」（補正がどう動いたか）
    j.key ("correction_db_per_sec"); j.open ('[');
    for (size_t s = 0; s + 48000 <= total; s += 48000) j.num (pdb (kPow (og.y, s, s + 48000, 48000)) - pdb (kPow (on.y, s, s + 48000, 48000)));
    j.close (']');
    writeValues (j, vals);
    if (og.bad) addF ("異常", fmt ("GAIN MATCH を ON にすると NaN・無限大が出る（%d 個）", og.bad));
    // 補正できる幅は「下げ 24 dB・上げ 12 dB」まで（GainMatch.h）。その中に入るときだけ判定する
    bool within = nat > -11 && nat < 23;
    j.kb ("within_range", within);
    metric ("gm_residual_db", within ? res : NAN);
    if (within && std::fabs (res) > 1.5)
        addF ("注意", fmt ("GAIN MATCH を ON にしても音量が %+.1f dB ずれる（OFF だと %+.1f dB、10 秒たった後。MIX・OUTPUT は動かさずに試した）", res, nat));
}

// ---------------------------------------------------------------- 1 つの検査を動かす（子プロセス）
static void writeFile (const std::string& path, const std::string& s) { std::ofstream f (path, std::ios::binary); f << s; }
static std::string readFile (const std::string& path) { std::ifstream f (path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

static int cmdRun (const std::string& code, const std::string& group, const std::string& outPath, uint32_t seed, int set)
{
    AudioComponent comp = findComp (fcc (code));
    if (comp == nullptr) { fprintf (stderr, "not found: %s\n", code.c_str()); return 2; }
    Json j; j.open ('{'); j.ks ("group", group);
    double t0 = nowSec();
    say ("start " + code + " " + group);
    if (group == "basic") testBasic (comp, j);
    else if (group == "click") testClick (comp, j);
    else if (group == "random") testRandom (comp, j, seed, set);
    else if (group == "state") testState (comp, j, seed);
    else if (group == "block") testBlock (comp, j);
    else if (group == "rates") testRates (comp, j);
    else if (group == "mono") testMono (comp, j);
    else if (group == "gainmatch") testGainMatch (comp, j, seed);
    else { fprintf (stderr, "unknown group %s\n", group.c_str()); return 2; }
    say ("phase: done (instances closed)");
    j.kn ("seconds", nowSec() - t0);
    j.key ("findings"); j.open ('[');
    for (auto& f : gF) { j.open ('{'); j.ks ("level", f.level); j.ks ("msg", f.msg); j.close ('}'); }
    j.close (']');
    j.close ('}');
    writeFile (outPath, j.out);
    std::string ft, mt;
    for (auto& f : gF) ft += f.level + "\t" + f.msg + "\n";
    for (auto& [k, v] : gM) mt += k + "\t" + v + "\n";
    writeFile (outPath + ".findings", ft);
    writeFile (outPath + ".metrics", mt);
    fflush (stdout);
    _exit (0);   // プラグインの後片付け（静的な破棄）で落ちても結果は残す
}

// ---------------------------------------------------------------- 全部（親プロセス）
struct CompInfo { AudioComponent c; OSType type, sub; std::string name, version; };
static std::vector<CompInfo> listComps()
{
    std::vector<CompInfo> r;
    AudioComponentDescription d {}; d.componentManufacturer = fcc ("Mypl");
    AudioComponent c = nullptr;
    while ((c = AudioComponentFindNext (c, &d)) != nullptr)
    {
        AudioComponentDescription cd {}; AudioComponentGetDescription (c, &cd);
        CFStringRef nm = nullptr; AudioComponentCopyName (c, &nm);
        std::string name = cfToStd (nm); if (nm) CFRelease (nm);
        auto colon = name.find (": "); if (colon != std::string::npos) name = name.substr (colon + 2);
        UInt32 v = 0; AudioComponentGetVersion (c, &v);
        r.push_back ({ c, cd.componentType, cd.componentSubType, name, fmt ("%u.%u.%u", v >> 16, (v >> 8) & 0xff, v & 0xff) });
    }
    std::sort (r.begin(), r.end(), [] (const CompInfo& a, const CompInfo& b) { return a.name < b.name; });
    return r;
}
static std::string selfPath()
{
    char buf[4096]; uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath (buf, &sz) != 0) return "hc";
    char real[PATH_MAX]; return realpath (buf, real) ? real : buf;
}
// 別プロセスで動かして待つ。時間切れなら止める
static int spawnWait (const std::vector<std::string>& args, const std::string& logPath, double timeoutSec, bool& timedOut, int& sig)
{
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init (&fa);
    posix_spawn_file_actions_addopen (&fa, 1, logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2 (&fa, 1, 2);
    std::vector<char*> av; for (auto& a : args) av.push_back ((char*) a.c_str()); av.push_back (nullptr);
    pid_t pid = 0;
    int rc = posix_spawn (&pid, av[0], &fa, nullptr, av.data(), environ);
    posix_spawn_file_actions_destroy (&fa);
    timedOut = false; sig = 0;
    if (rc != 0) return -1;
    int status = 0; double t0 = nowSec();
    while (true)
    {
        pid_t r = waitpid (pid, &status, WNOHANG);
        if (r == pid) break;
        if (nowSec() - t0 > timeoutSec) { kill (pid, SIGKILL); waitpid (pid, &status, 0); timedOut = true; break; }
        usleep (100000);
    }
    if (WIFSIGNALED (status)) sig = WTERMSIG (status);
    return WIFEXITED (status) ? WEXITSTATUS (status) : -1;
}
static std::string lastLine (const std::string& s)
{
    std::string t = s; while (! t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
    auto p = t.rfind ('\n'); return p == std::string::npos ? t : t.substr (p + 1);
}
struct RepoInfo { std::string folder, version; };
static std::map<std::string, RepoInfo> scanRepo (const std::string& repo)
{
    std::map<std::string, RepoInfo> m;
    std::string base = repo + "/MyPlugins";
    DIR* d = opendir (base.c_str()); if (! d) return m;
    std::regex rc ("PLUGIN_CODE\\s+([A-Za-z0-9]{4})"), rv ("project\\s*\\(\\s*\\w+\\s+VERSION\\s+([0-9.]+)");
    while (auto* e = readdir (d))
    {
        if (e->d_name[0] == '.') continue;
        std::string txt = readFile (base + "/" + e->d_name + "/CMakeLists.txt");
        std::smatch mc, mv;
        if (std::regex_search (txt, mc, rc) && std::regex_search (txt, mv, rv)) m[mc[1]] = { e->d_name, mv[1] };
    }
    closedir (d);
    return m;
}
static std::vector<std::string> split (const std::string& s, char c)
{
    std::vector<std::string> r; std::string cur;
    for (char ch : s) { if (ch == c) { if (! cur.empty()) r.push_back (cur); cur.clear(); } else cur += ch; }
    if (! cur.empty()) r.push_back (cur);
    return r;
}
static const char* groupJa (const std::string& g)
{
    static const std::map<std::string, const char*> m {
        { "basic", "基本" }, { "click", "プチノイズ" }, { "random", "ランダム設定" }, { "state", "保存" }, { "block", "ブロックの大きさ" },
        { "rates", "サンプルレート" }, { "mono", "モノラル" }, { "gainmatch", "GAIN MATCH" }, { "auval", "auval" }, { "install", "入っている版" } };
    auto it = m.find (g); return it == m.end() ? g.c_str() : it->second;
}

static int cmdAll (const std::string& outDir, const std::string& repo, const std::vector<std::string>& only, std::vector<std::string> groups, uint32_t seed, bool skipAuval)
{
    if (groups.empty()) groups = { "basic", "click", "random", "state", "block", "rates", "mono", "gainmatch" };
    const std::map<std::string, double> timeout { { "basic", 300 }, { "click", 900 }, { "random", 900 }, { "state", 300 }, { "block", 300 }, { "rates", 900 }, { "mono", 300 }, { "gainmatch", 300 } };
    mkdir (outDir.c_str(), 0755);
    std::string raw = outDir + "/raw"; mkdir (raw.c_str(), 0755);
    auto repoMap = scanRepo (repo);
    auto comps = listComps();
    std::string me = selfPath();
    double tStart = nowSec();
    struct PF { std::string level, group, msg; };
    struct PR { CompInfo ci; RepoInfo ri; std::vector<PF> f; std::map<std::string, std::string> m; double secs = 0; std::string auval = "-"; };
    std::vector<PR> results;
    Json sum; sum.open ('[');
    for (auto& ci : comps)
    {
        std::string code = fourcc (ci.sub);
        if (! only.empty())
        {
            bool hit = false;
            for (auto& o : only) if (o == code || o == ci.name || (repoMap.count (code) && repoMap[code].folder == o)) hit = true;
            if (! hit) continue;
        }
        PR pr; pr.ci = ci; if (repoMap.count (code)) pr.ri = repoMap[code];
        fprintf (stderr, "== %s (%s %s)\n", ci.name.c_str(), fourcc (ci.type).c_str(), code.c_str());
        double t0 = nowSec();
        if (pr.ri.folder.empty()) pr.f.push_back ({ "注意", "install", "リポジトリにない（消したプラグインが入ったまま？ ~/Library/Audio/Plug-Ins/Components から外してよいか本人に確認）" });
        else if (pr.ri.version != ci.version) pr.f.push_back ({ "注意", "install", fmt ("入っている版 %s がリポジトリの版 %s と違う（入れ直しが必要かも）", ci.version.c_str(), pr.ri.version.c_str()) });
        Json pj; pj.open ('{');
        pj.ks ("name", ci.name); pj.ks ("code", code); pj.ks ("type", fourcc (ci.type)); pj.ks ("installed_version", ci.version);
        pj.ks ("repo_folder", pr.ri.folder); pj.ks ("repo_version", pr.ri.version);
        for (auto& g : groups)
        {
            std::string out = raw + "/" + code + "-" + g + ".json";
            unlink (out.c_str());
            bool to = false; int sig = 0;
            double gt = nowSec();
            int rc = spawnWait ({ me, "run", code, g, out, "--seed", std::to_string (seed) }, out + ".log", timeout.count (g) ? timeout.at (g) : 600, to, sig);
            std::string js = readFile (out);
            fprintf (stderr, "   %-9s %5.1fs %s\n", g.c_str(), nowSec() - gt, js.empty() ? "(no result)" : "ok");
            pj.key (g);
            if (! js.empty()) pj.raw (js);
            else
            {
                std::string last = lastLine (readFile (out + ".log"));
                std::string why = to ? fmt ("%.0f 秒たっても終わらない（固まった？）", timeout.count (g) ? timeout.at (g) : 600.0)
                                     : sig ? fmt ("落ちた（シグナル %d %s）", sig, strsignal (sig)) : fmt ("結果が出ない（終了コード %d）", rc);
                pr.f.push_back ({ "異常", g, why + "。最後にしていたこと: " + last });
                pj.open ('{'); pj.ks ("error", why); pj.ks ("last", last); pj.close ('}');
            }
            for (auto& line : split (readFile (out + ".findings"), '\n'))
            {
                auto tab = line.find ('\t'); if (tab == std::string::npos) continue;
                pr.f.push_back ({ line.substr (0, tab), g, line.substr (tab + 1) });
            }
            for (auto& line : split (readFile (out + ".metrics"), '\n'))
            {
                auto tab = line.find ('\t'); if (tab == std::string::npos) continue;
                pr.m[line.substr (0, tab)] = line.substr (tab + 1);
            }
        }
        if (! skipAuval)
        {
            std::string out = raw + "/" + code + "-auval.txt";
            bool to = false; int sig = 0;
            spawnWait ({ "/usr/bin/auval", "-v", fourcc (ci.type), code, "Mypl" }, out, 300, to, sig);
            std::string txt = readFile (out);
            bool ok = txt.find ("AU VALIDATION SUCCEEDED") != std::string::npos;
            pr.auval = ok ? "OK" : (to ? "時間切れ" : "失敗");
            if (! ok) pr.f.push_back ({ "異常", "auval", "auval（Apple の検査）に通らない。詳しくは raw/" + code + "-auval.txt" });
        }
        pr.secs = nowSec() - t0;
        pj.ks ("auval", pr.auval); pj.kn ("seconds", pr.secs);
        pj.key ("findings"); pj.open ('[');
        for (auto& f : pr.f) { pj.open ('{'); pj.ks ("level", f.level); pj.ks ("group", f.group); pj.ks ("msg", f.msg); pj.close ('}'); }
        pj.close (']');
        pj.close ('}');
        writeFile (raw + "/" + code + ".json", pj.out);
        sum.raw (pj.out);
        results.push_back (pr);
    }
    sum.close (']');
    writeFile (outDir + "/summary.json", sum.out);

    // 本人と夜の Claude が読む一覧（日本語）
    auto count = [] (const PR& r, const char* lv) { int n = 0; for (auto& f : r.f) if (f.level == lv) ++n; return n; };
    int nBad = 0, nWarn = 0, nOk = 0;
    for (auto& r : results) { if (count (r, "異常")) ++nBad; else if (count (r, "注意")) ++nWarn; else ++nOk; }
    time_t tt = time (nullptr); char ts[64]; strftime (ts, sizeof ts, "%Y-%m-%d %H:%M", localtime (&tt));
    std::string md = fmt ("# 自作プラグイン健康診断（%s）\n\n", ts);
    md += fmt ("- 検査したプラグイン: %zu 個（%.0f 分、乱数の種 %u）\n", results.size(), (nowSec() - tStart) / 60.0, seed);
    md += fmt ("- 異常あり %d 個 / 注意だけ %d 個 / 問題なし %d 個\n", nBad, nWarn, nOk);
    md += "- 検査: " ; for (auto& g : groups) md += std::string (groupJa (g)) + " "; md += skipAuval ? "\n\n" : "auval\n\n";
    md += "## 異常・注意\n\n";
    for (auto& r : results)
    {
        if (! count (r, "異常") && ! count (r, "注意")) continue;
        md += fmt ("### %s %s（%s）\n", r.ci.name.c_str(), r.ci.version.c_str(), fourcc (r.ci.sub).c_str());
        for (const char* lv : { "異常", "注意" })
            for (auto& f : r.f) if (f.level == lv) md += fmt ("- 【%s】%s: %s\n", lv, groupJa (f.group), f.msg.c_str());
        md += "\n";
    }
    md += "## 参考（仕様のことが多いもの）\n\n";
    for (auto& r : results)
    {
        if (! count (r, "参考")) continue;
        md += fmt ("### %s\n", r.ci.name.c_str());
        for (auto& f : r.f) if (f.level == "参考") md += fmt ("- %s: %s\n", groupJa (f.group), f.msg.c_str());
        md += "\n";
    }
    md += "## 数値の一覧\n\n| プラグイン | 版 | 遅れ | CPU 平均 / 99% | 96k CPU | 既定の音量差 | 止めて20秒後 | 保存の差 | ブロック差 | GAIN MATCH 残り | auval | 時間 |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    auto mv = [] (const PR& r, const char* k, const char* f) -> std::string
    {
        auto it = r.m.find (k); if (it == r.m.end() || it->second == "null") return "-";
        return fmt (f, std::atof (it->second.c_str()));
    };
    for (auto& r : results)
        md += fmt ("| %s | %s | %s | %s / %s %% | %s %% | %s | %s | %s | %s | %s | %s | %.0f 秒 |\n", r.ci.name.c_str(), r.ci.version.c_str(),
                   mv (r, "latency", "%.0f").c_str(), mv (r, "cpu_avg", "%.1f").c_str(), mv (r, "cpu_p99", "%.0f").c_str(), mv (r, "cpu96_avg", "%.1f").c_str(),
                   mv (r, "gain_db", "%+.1f dB").c_str(), mv (r, "tail_end_db", "%.0f dB").c_str(), mv (r, "state_diff_db", "%.0f dB").c_str(),
                   mv (r, "block_diff_db", "%.0f dB").c_str(), mv (r, "gm_residual_db", "%+.1f dB").c_str(), r.auval.c_str(), r.secs);
    md += "\n遅れ = サンプル（48 kHz）。CPU = 1 コアの何 % か（48 kHz・512 サンプル）。差は小さい（マイナスが大きい）ほど同じ。\n";
    md += "詳しい数値は raw/<コード>-<検査>.json、落ちた・固まったときの最後の様子は raw/<コード>-<検査>.json.log。\n";
    writeFile (outDir + "/summary.md", md);
    fprintf (stderr, "done: %s/summary.md\n", outDir.c_str());
    return 0;
}

static int cmdList()
{
    for (auto& c : listComps()) printf ("%s %s %s %s\n", fourcc (c.type).c_str(), fourcc (c.sub).c_str(), c.version.c_str(), c.name.c_str());
    return 0;
}
static int cmdParams (const std::string& code)
{
    AudioComponent comp = findComp (fcc (code));
    if (! comp) { fprintf (stderr, "not found\n"); return 2; }
    std::string err; auto h = makeHost (comp, 48000, 2, 2, err);
    if (! h) { fprintf (stderr, "%s\n", err.c_str()); return 1; }
    static const char* use[] = { "使う", "ランダム以外", "触らない" };
    for (auto& p : h->params())
        printf ("%10u  %-28s %8.3f..%-8.3f def %-8.3f %s%s %s\n", p.id, p.name.c_str(), p.lo, p.hi, p.def, p.discrete ? "段階" : "連続",
                p.writable ? "" : "（読むだけ）", use[classify (p.name)]);
    printf ("latency %.0f samples, tail %.2f s\n", h->latencySec() * 48000, h->tailSec());
    fflush (stdout);
    _exit (0);
}

int main (int argc, char** argv)
{
    setvbuf (stdout, nullptr, _IOLBF, 0);
    std::vector<std::string> a (argv + 1, argv + argc);
    auto opt = [&] (const char* k, const std::string& d) -> std::string
    {
        for (size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == k) return a[i + 1];
        return d;
    };
    auto flag = [&] (const char* k) { return std::find (a.begin(), a.end(), k) != a.end(); };
    time_t tt = time (nullptr); char d8[16]; strftime (d8, sizeof d8, "%Y%m%d", localtime (&tt));
    uint32_t seed = (uint32_t) std::stoul (opt ("--seed", d8));
    if (a.empty()) { fprintf (stderr, "usage: hc list | params <code> | all <outdir> [...] | run <code> <group> <out.json>\n"); return 2; }
    if (a[0] == "list") return cmdList();
    if (a[0] == "params" && a.size() >= 2) return cmdParams (a[1]);
    if (a[0] == "run" && a.size() >= 4) return cmdRun (a[1], a[2], a[3], seed, std::stoi (opt ("--set", "-1")));
    if (a[0] == "all" && a.size() >= 2)
    {
        std::string home = getenv ("HOME") ? getenv ("HOME") : ".";
        return cmdAll (a[1], opt ("--repo", home + "/plugins"), split (opt ("--only", ""), ','), split (opt ("--groups", ""), ','), seed, flag ("--skip-auval"));
    }
    fprintf (stderr, "bad arguments\n");
    return 2;
}
