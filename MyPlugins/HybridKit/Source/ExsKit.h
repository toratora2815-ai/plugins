// Hybrid Kit — Logic の Drum Kit Designer（Producer Kit）の録音を読む部分（JUCE なし）
//
// 読むもの（どちらもこの Mac の中にあるものを読むだけ。音のファイルはリポジトリにも Releases にも入れない）:
//   /Library/Application Support/Logic/Sampler Instruments/03 Drums & Percussion/04 Drum Kit Designer/Drum Kit Designer/<キット>.exs
//     … どの鍵盤・どの強さで、どのファイルのどこを鳴らすかの表（EXS。リトルエンディアンの塊が並ぶ）
//   /Library/Application Support/Logic/EXS Factory Samples/Drum Kit Designer Consolidated/Drum Kit Designer/*.caf
//     … 16 bit・44.1 kHz の CAF（ビッグエンディアン）。1 つのファイルに何百打ぶんも並んでいる
//
// 調べて分かったこと（2026-11-12、NOTES.md にも）:
//   - グループ名は「キット:部品:奏法:マイク」（例 AnKit8:Bd1:all:di）。マイクは di=近い / d2=近い 2 本目 / oh=オーバーヘッド /
//     ct=かぶり（ほかの近いマイクに入った分、ステレオ）/ r1=ルーム A（ステレオ）/ r2=ルーム B（モノ）
//   - ハイハットは奏法が o1〜o7（開き具合）・foot・splash。同じ鍵盤に 7 段階ぶんが重なっている
//   - ゾーンのデータ（128 バイト）: [1]=元の鍵盤 [6][7]=鍵盤の範囲 [9][10]=強さの範囲 [12..19]=頭の始め・終わり
//     [88]=グループ番号 [92]=ファイル番号 [116..123]=残りの響き（尾）の始め・終わり [124]=音量の補正（dB、float）
//   - 頭（0.6〜1.8 秒）は強さごとに別の録音、尾は近い強さどうしで使い回し。頭の終わりと尾の始まりの音量はほぼ同じ
//   - 録音は強さの段階ごとに音量がそろえてあり、[124] の補正を足すとほぼ同じ大きさになる → 強弱は鳴らすときに付ける
//   - マイクごとのゾーンは同じ瞬間で切ってある（オーバーヘッド・ルームは距離の分だけ遅れて鳴り始める）。スネアの上下・キックの 2 本は極性がそろっている
//
// メモリ: 録音ファイルは mmap（読むのはそのページを触ったときだけ）。叩いた瞬間（各ゾーンの頭 4096 サンプル）だけは自前のメモリに写しておき、
//   その先は鳴らし始めに Prefetcher が裏で先に読む（オーディオスレッドでディスクを待たない）
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <memory>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <pthread.h>
#include <unistd.h>
#include <vector>

namespace hk
{
inline uint32_t rd32le (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint32_t rd32be (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | (uint32_t) p[3]; }
inline uint64_t rd64be (const uint8_t* p) { return ((uint64_t) rd32be (p) << 32) | rd32be (p + 4); }

//==============================================================================
// EXS（Logic の Sampler の楽器ファイル）
struct ExsZone
{
    int rootKey = 60, keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
    uint32_t start = 0, end = 0, tailStart = 0, tailEnd = 0;
    float gainDb = 0;
    int group = -1, sample = -1;
};

struct ExsGroup
{
    std::string name;
    float volDb = 0;
    std::string piece, artic, mic;   // 「キット:部品:奏法:マイク」を分けたもの
};

struct ExsSample { std::string file, folder; };

struct ExsInstrument
{
    std::vector<ExsZone> zones;
    std::vector<ExsGroup> groups;
    std::vector<ExsSample> samples;

    bool load (const std::string& path)
    {
        zones.clear(); groups.clear(); samples.clear();
        FILE* f = std::fopen (path.c_str(), "rb");
        if (f == nullptr) return false;
        std::fseek (f, 0, SEEK_END);
        const long n = std::ftell (f);
        std::fseek (f, 0, SEEK_SET);
        if (n <= 0 || n > 64 * 1024 * 1024) { std::fclose (f); return false; }
        std::vector<uint8_t> d ((size_t) n);
        const size_t got = std::fread (d.data(), 1, d.size(), f);
        std::fclose (f);
        if (got != d.size()) return false;

        size_t p = 0;
        while (p + 84 <= d.size())
        {
            const uint8_t* b = d.data() + p;
            if (std::memcmp (b + 16, "TBOS", 4) != 0) break;   // リトルエンディアンの EXS だけ
            const int type = b[3] & 0x0F;
            const uint32_t size = rd32le (b + 4);
            if (p + 84 + (size_t) size > d.size()) break;
            char nm[65] = {};
            std::memcpy (nm, b + 20, 64);
            const uint8_t* z = b + 84;

            if (type == 1 && size >= 96)
            {
                ExsZone zone;
                zone.rootKey = z[1];
                zone.keyLo = z[6]; zone.keyHi = z[7];
                zone.velLo = z[9]; zone.velHi = z[10];
                zone.start = rd32le (z + 12); zone.end = rd32le (z + 16);
                zone.gainDb = (float) (int8_t) z[4];
                zone.group = (int) (int32_t) rd32le (z + 88);
                zone.sample = (int) (int32_t) rd32le (z + 92);
                if (size >= 128)
                {
                    zone.tailStart = rd32le (z + 116); zone.tailEnd = rd32le (z + 120);
                    float g; std::memcpy (&g, z + 124, 4);
                    if (std::isfinite (g) && std::abs (g) < 60.0f) zone.gainDb = g;
                }
                zones.push_back (zone);
            }
            else if (type == 2)
            {
                ExsGroup g;
                g.name = nm;
                g.volDb = size > 0 ? (float) (int8_t) z[0] : 0.0f;
                std::vector<std::string> parts;
                size_t s = 0;
                for (size_t i = 0; i <= g.name.size(); ++i)
                    if (i == g.name.size() || g.name[i] == ':') { parts.push_back (g.name.substr (s, i - s)); s = i + 1; }
                if (parts.size() >= 4) { g.piece = parts[1]; g.artic = parts[2]; g.mic = parts[3]; }
                if (parts.size() >= 3 && (parts.size() < 4 || parts[3].empty()))   // 「キット:部品:マイク:」の形（Vintage Kit Rods のタム）
                    { g.piece = parts[1]; g.artic = "all"; g.mic = parts[2]; }
                groups.push_back (g);
            }
            else if (type == 3)
            {
                ExsSample s;
                s.file = nm;
                for (uint32_t i = 0; i + 1 < size; ++i)   // フォルダーは '/' で始まる文字列
                    if (z[i] == '/' && (i == 0 || z[i - 1] == 0))
                    {
                        uint32_t e = i;
                        while (e < size && z[e] != 0) ++e;
                        s.folder.assign ((const char*) z + i, e - i);
                        // その後ろに長いファイル名があれば使う（塊の名前は昔の 31 文字に縮めた「…#38B0.caf」のことがある）
                        for (uint32_t j = e; j + 4 < size; ++j)
                        {
                            if (z[j] == 0 || (j > e && z[j - 1] != 0)) continue;
                            uint32_t k = j;
                            while (k < size && z[k] != 0) ++k;
                            const std::string cand ((const char*) z + j, k - j);
                            if (cand.size() > 4 && (cand.rfind (".caf") == cand.size() - 4 || cand.rfind (".wav") == cand.size() - 4 || cand.rfind (".aif") == cand.size() - 4))
                            {
                                s.file = cand;
                                break;
                            }
                            j = k;
                        }
                        break;
                    }
                samples.push_back (s);
            }
            p += 84 + (size_t) size;
        }
        return ! zones.empty();
    }
};

//==============================================================================
// CAF（16 bit の PCM だけ）を mmap で開く
struct MappedCaf
{
    std::string path;
    const uint8_t* base = nullptr;
    size_t mapLen = 0;
    double sampleRate = 0;
    int channels = 0, bits = 0;
    bool bigEndian = true, ok = false;
    uint64_t dataOffset = 0, frames = 0;

    MappedCaf() = default;
    MappedCaf (const MappedCaf&) = delete;
    MappedCaf& operator= (const MappedCaf&) = delete;
    ~MappedCaf() { if (base != nullptr) munmap ((void*) base, mapLen); }

    bool open (const std::string& p)
    {
        path = p; ok = false;
        const int fd = ::open (p.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st {};
        if (fstat (fd, &st) != 0 || st.st_size < 64) { ::close (fd); return false; }
        const size_t len = (size_t) st.st_size;
        void* m = mmap (nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close (fd);
        if (m == MAP_FAILED) return false;
        base = (const uint8_t*) m; mapLen = len;
        if (std::memcmp (base, "caff", 4) != 0) return false;
        size_t q = 8;
        bool pcm = false;
        while (q + 12 <= len)
        {
            const uint64_t sz = rd64be (base + q + 4);
            const uint8_t* c = base + q + 12;
            if (std::memcmp (base + q, "desc", 4) == 0 && q + 12 + 32 <= len)
            {
                uint64_t srBits = rd64be (c);
                std::memcpy (&sampleRate, &srBits, 8);
                pcm = std::memcmp (c + 8, "lpcm", 4) == 0;
                bigEndian = (rd32be (c + 12) & 2u) == 0;   // CAF の決まり: 2 = リトルエンディアン
                channels = (int) rd32be (c + 24);
                bits = (int) rd32be (c + 28);
            }
            else if (std::memcmp (base + q, "data", 4) == 0)
            {
                dataOffset = q + 12 + 4;   // 4 バイトの編集回数の後
                const uint64_t bytes = (sz == ~0ull || q + 12 + sz > len) ? len - dataOffset : sz - 4;
                if (channels > 0 && bits > 0) frames = bytes / (uint64_t) (channels * bits / 8);
                ok = pcm && bits == 16 && channels >= 1 && channels <= 2 && sampleRate > 8000;
                return ok;
            }
            if (sz > len) break;
            q += 12 + (size_t) sz;
        }
        return false;
    }

    // i 番目のフレームの ch チャンネル（範囲の確認は呼ぶ側）
    float at (uint64_t i, int ch) const
    {
        const uint8_t* s = base + dataOffset + (i * (uint64_t) channels + (uint64_t) ch) * 2;
        const int16_t v = bigEndian ? (int16_t) ((s[0] << 8) | s[1]) : (int16_t) ((s[1] << 8) | s[0]);
        return (float) v * (1.0f / 32768.0f);
    }
    const uint8_t* bytesAt (uint64_t frame) const { return base + dataOffset + frame * (uint64_t) channels * 2; }
};

//==============================================================================
// オーディオスレッド → 裏のスレッドへ「この範囲をもうすぐ読む」を渡して、先にページを触っておく（ロックなし、1 対 1）
class Prefetcher
{
public:
    Prefetcher() = default;
    ~Prefetcher() { stop(); }

    void start()
    {
        if (running.exchange (true)) return;
        worker = std::thread ([this] { run(); });
    }
    void stop()
    {
        if (! running.exchange (false)) return;
        if (worker.joinable()) worker.join();
    }
    // オーディオスレッドから。いっぱいなら捨てる（そのときはオーディオスレッドが読みに行くだけ）
    void request (const uint8_t* from, size_t bytes)
    {
        if (from == nullptr || bytes == 0) return;
        const uint32_t w = writePos.load (std::memory_order_relaxed);
        if (w - readPos.load (std::memory_order_acquire) >= queue.size()) return;
        queue[w % queue.size()] = { from, bytes };
        writePos.store (w + 1, std::memory_order_release);
    }
    // ここまでに渡した数（オーディオスレッドで読む）と、触り終えた数。キットを消すのは done() がその時の enqueued() に届いてから
    // （先読みが古いキットのページを触っている途中で munmap すると落ちる。DSP 検査係 2026-11-12）
    uint32_t enqueued() const { return writePos.load (std::memory_order_acquire); }
    uint32_t done() const { return donePos.load (std::memory_order_acquire); }
    static bool reached (uint32_t doneCount, uint32_t fence) { return (int32_t) (doneCount - fence) >= 0; }

private:
    struct Req { const uint8_t* p = nullptr; size_t n = 0; };
    std::array<Req, 1024> queue {};
    std::atomic<uint32_t> writePos { 0 }, readPos { 0 }, donePos { 0 };
    std::atomic<bool> running { false };
    std::thread worker;
    volatile uint8_t sink = 0;

    void run()
    {
        pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);   // 先読みは急ぐ
        while (running.load())
        {
            const uint32_t r = readPos.load (std::memory_order_relaxed);
            if (r == writePos.load (std::memory_order_acquire)) { std::this_thread::sleep_for (std::chrono::milliseconds (2)); continue; }
            const Req q = queue[r % queue.size()];
            readPos.store (r + 1, std::memory_order_release);
            uint8_t acc = 0;
            for (size_t i = 0; i < q.n; i += 4096) acc ^= q.p[i];   // ページを 1 バイトずつ触る
            acc ^= q.p[q.n - 1];
            sink = acc;
            donePos.store (r + 1, std::memory_order_release);
        }
    }
};

//==============================================================================
// キットの形
enum Piece { Kick, Snare, TomHi, TomMidHi, TomMidLo, TomLo, HiHat, CrashL, CrashR, Ride, NumPieces };
enum Mic { Close, Close2, OH, Bleed, RoomA, RoomB, NumMics };

inline const char* pieceName (int p)
{
    static const char* n[] = { "Kick", "Snare", "Tom Hi", "Tom Mid Hi", "Tom Mid Lo", "Tom Lo", "Hi-Hat", "Crash L", "Crash R", "Ride" };
    return p >= 0 && p < NumPieces ? n[p] : "?";
}

inline int pieceFromExs (const std::string& s)
{
    if (s == "Bd1" || s == "Bd2" || s == "Bd4") return Kick;
    if (s == "Sn1" || s == "Sn2") return Snare;
    if (s == "TomHi") return TomHi;
    if (s == "TomMidHi") return TomMidHi;
    if (s == "TomMidLo") return TomMidLo;
    if (s == "TomLo") return TomLo;
    if (s == "HH") return HiHat;
    if (s == "CrL" || s == "Crl") return CrashL;
    if (s == "CrR") return CrashR;
    if (s == "Rd" || s == "Rd1") return Ride;
    return -1;
}

inline int micFromExs (const std::string& s)
{
    if (s == "di") return Close;
    if (s == "d2") return Close2;
    if (s == "oh") return OH;
    if (s == "ct") return Bleed;
    if (s == "r1") return RoomA;
    if (s == "r2") return RoomB;
    return -1;
}

constexpr uint32_t kAttackFrames = 4096;   // 叩いた瞬間（約 93 ms）は自前のメモリに写す
constexpr uint32_t kSpliceFrames = 441;    // 頭 → 尾を重ねる長さ（10 ms）

// 頭 → 尾のつなぎ方（層ごと。refineSplices が裏で決める。鳴らし始めに声へ写すので、鳴っている途中では変わらない）
struct SpliceParams
{
    uint32_t skip = 0;    // 尾の始めを飛ばす数
    uint32_t trim = 0;    // 頭の終わりを削る数
    float rho = 0;        // 重ねる所の似かた（0 = 似ていない → 等パワー、1 = 同じ → 直線）
};

struct MicPart
{
    const MappedCaf* file = nullptr;
    uint64_t headStart = 0, tailStart = 0;   // ファイルの中のフレーム位置
    uint32_t headFrames = 0, tailFrames = 0;
    int channels = 1;
    float gain = 1.0f;                       // グループの音量 + ゾーンの補正（直線）
    const int16_t* attack = nullptr;         // 頭の始め（ネイティブの 16 bit、channels 本のインターリーブ）
    uint32_t attackFrames = 0;


    void raw (uint64_t i, float& l, float& r) const   // 頭の i 番目（写しがあればそこから）
    {
        if (i < attackFrames)
        {
            const int16_t* s = attack + i * (uint64_t) channels;
            l = (float) s[0] * (1.0f / 32768.0f);
            r = channels == 2 ? (float) s[1] * (1.0f / 32768.0f) : l;
            return;
        }
        l = file->at (headStart + i, 0);
        r = channels == 2 ? file->at (headStart + i, 1) : l;
    }
    void rawTail (uint64_t i, float& l, float& r) const
    {
        l = file->at (tailStart + i, 0);
        r = channels == 2 ? file->at (tailStart + i, 1) : l;
    }
    // pos 番目の値。頭 → 尾は 10 ms で重ねる（似かた rho に合わせて、足した大きさが変わらないように）。範囲外なら false
    bool fetch (uint64_t pos, float& l, float& r, const SpliceParams& sp = {}, float tailGain = 1.0f) const
    {
        if (file == nullptr) return false;
        if (tailFrames == 0)
        {
            if (pos >= headFrames) return false;
            raw (pos, l, r);
            return true;
        }
        const uint32_t H = headFrames - std::min (sp.trim, headFrames / 2);
        const uint32_t X = std::min<uint32_t> (kSpliceFrames, H / 4);
        const uint64_t xs = (uint64_t) H - X;
        if (pos < xs) { raw (pos, l, r); return true; }
        const uint64_t tp = (uint64_t) sp.skip + (pos - xs);
        if (tp >= tailFrames) return false;
        float tl, tr; rawTail (tp, tl, tr);
        tl *= tailGain; tr *= tailGain;
        if (pos < H)
        {
            float hl, hr; raw (pos, hl, hr);
            const float w = (float) (pos - xs) / (float) X;
            float a = 1.0f - w, b = w;
            const float k = 1.0f / std::sqrt (std::max (1.0e-6f, a * a + b * b + 2.0f * sp.rho * a * b));
            a *= k; b *= k;
            l = hl * a + tl * b; r = hr * a + tr * b;
        }
        else { l = tl; r = tr; }
        return true;
    }
    // この先（写しの後ろ）のディスクの範囲を先読みに渡す（全マイクの頭を先に、尾は後で。KitEngine の startVoice）
    void prefetchHead (Prefetcher& pf) const
    {
        if (file != nullptr && headFrames > attackFrames)
            pf.request (file->bytesAt (headStart + attackFrames), (size_t) (headFrames - attackFrames) * (size_t) channels * 2);
    }
    void prefetchTail (Prefetcher& pf) const
    {
        if (file != nullptr && tailFrames > 0)
            pf.request (file->bytesAt (tailStart), (size_t) tailFrames * (size_t) channels * 2);
    }
};

// 層ごとに、裏のスレッドが計算して入れる値（EDM の層の合わせ方）。オーディオスレッドは読むだけ
struct LayerFx
{
    std::atomic<float> subDelay { 0 };    // サブを鳴らし始める位置（頭の始めからのサンプル）
    std::atomic<float> subGain { 0 };     // 極性 × 自動で合わせた大きさ
    std::atomic<float> edmGain { 0 };     // スネアの EDM の層の自動で合わせた大きさ
    std::atomic<uint32_t> spliceSkip { 0 }, spliceTrim { 0 };   // 頭 → 尾のつなぎ方（refineSplices）
    std::atomic<float> spliceRho { 0 };
    std::array<std::atomic<float>, 6> tailGain { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
};

struct Layer
{
    int velLo = 0, velHi = 127;
    std::array<MicPart, NumMics> mic {};
    std::array<bool, NumMics> has {};
    int onset = 0;                    // 近いマイクの鳴り始め（頭の始めからのサンプル）
    int fx = -1;                      // Kit::fx の番号
};

struct Artic
{
    int piece = -1;
    std::string name;                 // all / stop / o1..o7 / foot / splash
    int key = 0;                      // EXS の鍵盤
    std::vector<Layer> layers;        // velLo 順

    int layerFor (int vel) const
    {
        for (int i = 0; i < (int) layers.size(); ++i)
            if (vel >= layers[(size_t) i].velLo && vel <= layers[(size_t) i].velHi) return i;
        return layers.empty() ? -1 : (vel < layers.front().velLo ? 0 : (int) layers.size() - 1);
    }
};

// 奏法の種類（引き表の番号）: all / stop / foot / splash / o1..o7
inline int articKind (const std::string& n)
{
    if (n == "all") return 0;
    if (n == "stop") return 1;
    if (n == "foot") return 2;
    if (n == "splash") return 3;
    if (n.size() == 2 && n[0] == 'o' && n[1] >= '1' && n[1] <= '7') return 4 + (n[1] - '1');
    return -1;
}
constexpr int kNumArticKinds = 11;

struct Kit
{
    std::string name;
    double sampleRate = 44100;
    std::vector<std::unique_ptr<MappedCaf>> files;
    std::vector<Artic> artics;
    std::array<bool, NumPieces> present {};
    std::array<std::array<bool, NumMics>, NumPieces> micPresent {};
    std::array<std::array<float, 2>, NumPieces> pan {};   // モノの近いマイクの左右の掛け算（オーバーヘッドの左右から）
    std::vector<int16_t> attackPool;
    std::unique_ptr<LayerFx[]> fx;
    int numFx = 0;

    std::vector<int16_t> lut;   // [部品][鍵盤][奏法の種類] → artics の番号（なければ −1）。オーディオスレッドで文字列を比べないため

    const Artic* lookup (int piece, int key, int kind) const
    {
        if (piece < 0 || piece >= NumPieces || key < 0 || key > 127 || kind < 0 || kind >= kNumArticKinds || lut.empty()) return nullptr;
        const int i = lut[((size_t) piece * 128 + (size_t) key) * kNumArticKinds + (size_t) kind];
        return i >= 0 ? &artics[(size_t) i] : nullptr;
    }
    const Artic* find (int piece, int key, const std::string& artic) const { return lookup (piece, key, articKind (artic)); }

    // Producer Kit の .exs を読んで形を作る（裏のスレッドで呼ぶ）。progress は 0..1
    bool loadProducerKit (const std::string& exsPath, std::string& error, std::atomic<float>* progress = nullptr,
                          const std::atomic<bool>* cancel = nullptr)
    {
        ExsInstrument exs;
        if (! exs.load (exsPath)) { error = "EXS を読めない: " + exsPath; return false; }
        files.clear(); artics.clear(); present = {}; micPresent = {};
        for (auto& s : exs.samples)
        {
            auto c = std::make_unique<MappedCaf>();
            if (! c->open (s.folder + "/" + s.file))
                std::fprintf (stderr, "[HybridKit] 開けない録音: %s/%s\n", s.folder.c_str(), s.file.c_str());
            else
                sampleRate = c->sampleRate;
            files.push_back (std::move (c));
        }
        std::map<std::tuple<int, std::string, int>, size_t> index;
        for (auto& z : exs.zones)
        {
            if (z.group < 0 || z.group >= (int) exs.groups.size() || z.sample < 0 || z.sample >= (int) files.size()) continue;
            const auto& g = exs.groups[(size_t) z.group];
            const int piece = pieceFromExs (g.piece), mic = micFromExs (g.mic);
            const MappedCaf* f = files[(size_t) z.sample].get();
            if (piece < 0 || mic < 0 || ! f->ok) continue;
            if (z.end <= z.start || z.end > f->frames) continue;
            const bool tailOk = z.tailEnd > z.tailStart && z.tailEnd <= f->frames;
            for (int key = z.keyLo; key <= z.keyHi; ++key)
            {
                auto k = std::make_tuple (piece, g.artic, key);
                auto it = index.find (k);
                if (it == index.end())
                {
                    Artic a; a.piece = piece; a.name = g.artic; a.key = key;
                    it = index.emplace (k, artics.size()).first;
                    artics.push_back (a);
                }
                Artic& a = artics[it->second];
                Layer* L = nullptr;
                for (auto& l : a.layers)
                    if (l.velLo == z.velLo && l.velHi == z.velHi) { L = &l; break; }
                if (L == nullptr)
                {
                    a.layers.push_back ({});
                    L = &a.layers.back();
                    L->velLo = z.velLo; L->velHi = z.velHi;
                }
                MicPart& m = L->mic[(size_t) mic];
                m.file = f;
                m.channels = f->channels;
                m.headStart = z.start; m.headFrames = z.end - z.start;
                m.tailStart = tailOk ? z.tailStart : 0; m.tailFrames = tailOk ? z.tailEnd - z.tailStart : 0;
                m.gain = std::pow (10.0f, (g.volDb + z.gainDb) / 20.0f);
                L->has[(size_t) mic] = true;
                present[(size_t) piece] = true;
                micPresent[(size_t) piece][(size_t) mic] = true;
            }
        }
        for (auto& a : artics)
            std::sort (a.layers.begin(), a.layers.end(), [] (const Layer& x, const Layer& y) { return x.velLo < y.velLo; });
        const size_t slash = exsPath.find_last_of ('/');
        name = exsPath.substr (slash == std::string::npos ? 0 : slash + 1);
        if (name.size() > 4 && name.substr (name.size() - 4) == ".exs") name.resize (name.size() - 4);
        if (artics.empty()) { error = "使える部品がない: " + exsPath; return false; }

        // 叩いた瞬間を写す（ここでディスクを読む。いちばん時間がかかる所）
        size_t total = 0, parts = 0;
        for (auto& a : artics) for (auto& L : a.layers) for (int m = 0; m < NumMics; ++m)
            if (L.has[(size_t) m]) { total += (size_t) std::min (kAttackFrames, L.mic[(size_t) m].headFrames) * (size_t) L.mic[(size_t) m].channels; ++parts; }
        attackPool.assign (total, 0);
        size_t at = 0, done = 0;
        for (auto& a : artics)
            for (auto& L : a.layers)
            {
                for (int m = 0; m < NumMics; ++m)
                {
                    if (! L.has[(size_t) m]) continue;
                    if (cancel != nullptr && cancel->load()) { error = "中止"; return false; }
                    MicPart& p = L.mic[(size_t) m];
                    const uint32_t n = std::min (kAttackFrames, p.headFrames);
                    int16_t* dst = attackPool.data() + at;
                    for (uint32_t i = 0; i < n; ++i)
                        for (int c = 0; c < p.channels; ++c)
                            dst[i * (uint32_t) p.channels + (uint32_t) c] = (int16_t) std::lround (p.file->at (p.headStart + i, c) * 32768.0f);
                    p.attack = dst; p.attackFrames = n;
                    at += (size_t) n * (size_t) p.channels;
                    if (progress != nullptr && (++done & 63) == 0) progress->store (0.9f * (float) done / (float) std::max<size_t> (1, parts));
                }
                // 近いマイクの鳴り始め（写した所の中で、いちばん大きい所の 10 % を最初に超えた所）
                const MicPart& c = L.has[Close] ? L.mic[Close] : (L.has[OH] ? L.mic[OH] : L.mic[RoomA]);
                if (c.attack != nullptr)
                {
                    int pk = 0;
                    const size_t n = (size_t) c.attackFrames * (size_t) c.channels;
                    for (size_t i = 0; i < n; ++i) pk = std::max (pk, std::abs ((int) c.attack[i]));
                    for (uint32_t i = 0; i < c.attackFrames; ++i)
                    {
                        int v = 0;
                        for (int k = 0; k < c.channels; ++k) v = std::max (v, std::abs ((int) c.attack[i * (uint32_t) c.channels + (uint32_t) k]));
                        if (v * 10 > pk) { L.onset = (int) i; break; }
                    }
                }
            }
        // 引き表
        lut.assign ((size_t) NumPieces * 128 * kNumArticKinds, (int16_t) -1);
        for (size_t i = 0; i < artics.size(); ++i)
        {
            const int k = articKind (artics[i].name);
            if (k >= 0 && artics[i].key >= 0 && artics[i].key < 128)
                lut[((size_t) artics[i].piece * 128 + (size_t) artics[i].key) * kNumArticKinds + (size_t) k] = (int16_t) i;
        }
        // 層ごとの EDM 合わせの入れ物
        int nl = 0;
        for (auto& a : artics) for (auto& L : a.layers) L.fx = nl++;
        fx = std::make_unique<LayerFx[]> ((size_t) nl);
        numFx = nl;
        autoPan();
        if (progress != nullptr) progress->store (1.0f);
        return true;
    }

    // 近いマイク（モノ）の左右を、オーバーヘッドに写った位置から決める（真ん中寄りに控えめ）
    void autoPan()
    {
        for (auto& p : pan) p = { 1.0f, 1.0f };
        std::array<bool, NumPieces> done {};
        for (auto& a : artics)
        {
            if (a.layers.empty() || a.name == "stop" || done[(size_t) a.piece]) continue;
            const Layer& L = a.layers[a.layers.size() / 2];
            if (! L.has[OH] || ! L.has[Close] || L.mic[OH].channels != 2 || L.mic[OH].attack == nullptr) continue;
            const MicPart& o = L.mic[OH];
            double el = 0, er = 0;
            for (uint32_t i = 0; i < o.attackFrames; ++i)
            {
                const double l = o.attack[2 * i], r = o.attack[2 * i + 1];
                el += l * l; er += r * r;
            }
            if (el + er <= 0) continue;
            float p = (float) ((std::sqrt (er) - std::sqrt (el)) / (std::sqrt (er) + std::sqrt (el)));
            p = std::clamp (p * 1.6f, -0.8f, 0.8f);
            if (a.piece == Kick || a.piece == Snare) p *= 0.25f;
            pan[(size_t) a.piece] = { std::min (1.0f, 1.0f - p), std::min (1.0f, 1.0f + p) };
            done[(size_t) a.piece] = true;
        }
    }
};

// 頭 → 尾のつなぎ方を層ごとに決める（裏のスレッド。キットを出した後に動かし、終わるまでは素のつなぎ方で鳴る）
// 尾は近い強さの層どうしで共有した別の録音なので、そのまま重ねると同じ音程の所が打ち消し合ってへこむ（最悪 −18 dB。DSP 検査係 2026-11-12）。
// 全マイクを混ぜた音で、頭の終わり 10 ms と尾の始めがいちばん似るずれ（±5 ms。尾の頭を飛ばすか、頭の終わりを削る）を探し、
// 似かた（相関）に合わせて重ね方を決める。大きさは各マイクの頭の終わり 40 ms と尾の始め 40 ms をそろえる（−12〜+3 dB）
// 最後まで決めたら true（途中でやめたら false。同じキットに戻ったら呼び直す）
inline bool refineSplices (Kit& kit, const std::atomic<bool>* cancel = nullptr)
{
    if (kit.fx == nullptr) return true;
    constexpr uint32_t D = 220, W = 1764;
    std::vector<float> hm, tm;
    for (auto& a : kit.artics)
        for (auto& L : a.layers)
        {
            if (cancel != nullptr && cancel->load()) return false;
            int ref = -1;
            for (int m = 0; m < NumMics; ++m) if (L.has[(size_t) m] && L.mic[(size_t) m].tailFrames > 0) { ref = m; break; }
            if (ref < 0) continue;
            const uint32_t H = L.mic[(size_t) ref].headFrames;
            const uint32_t X = std::min<uint32_t> (kSpliceFrames, H / 4);
            const uint32_t n = X + D;
            if (H < n + W || L.mic[(size_t) ref].tailFrames < n + W) continue;
            hm.assign (n, 0.0f); tm.assign (n, 0.0f);
            for (int m = 0; m < NumMics; ++m)
            {
                const MicPart& p = L.mic[(size_t) m];
                if (! L.has[(size_t) m] || p.headFrames != H || p.tailFrames < n) continue;
                for (uint32_t i = 0; i < n; ++i)
                {
                    float x, y;
                    p.raw (H - n + i, x, y); hm[i] += 0.5f * (x + y) * p.gain;
                    p.rawTail (i, x, y);     tm[i] += 0.5f * (x + y) * p.gain;
                }
            }
            // ずれ lag ≥ 0: 尾の頭を lag 飛ばす / lag < 0: 頭の終わりを −lag 削る
            auto corr = [&] (int lag)
            {
                const uint32_t trim = lag < 0 ? (uint32_t) -lag : 0, skip = lag > 0 ? (uint32_t) lag : 0;
                double sxy = 0, sxx = 0, syy = 0;
                for (uint32_t j = 0; j < X; ++j)
                {
                    const double x = hm[D - trim + j], y = tm[skip + j];
                    sxy += x * y; sxx += x * x; syy += y * y;
                }
                return sxy / std::sqrt (sxx * syy + 1e-30);
            };
            double best = -2.0; int bestLag = 0;
            for (int lag = -(int) D; lag <= (int) D; ++lag)
            {
                const double c = corr (lag);
                if (c > best) { best = c; bestLag = lag; }
            }
            if (best < 0.3) bestLag = 0;   // 似ていない（雑音のような尾）なら、ずらさず等パワーで
            const double rho = std::clamp (corr (bestLag), 0.0, 1.0);
            const uint32_t trim = bestLag < 0 ? (uint32_t) -bestLag : 0, skip = bestLag > 0 ? (uint32_t) bestLag : 0;
            LayerFx& f = kit.fx[(size_t) L.fx];
            for (int m = 0; m < NumMics; ++m)
            {
                const MicPart& p = L.mic[(size_t) m];
                float g = 1.0f;
                if (L.has[(size_t) m] && p.tailFrames >= skip + W && p.headFrames >= trim + W)
                {
                    double eh = 0, et = 0;
                    for (uint32_t j = 0; j < W; ++j)
                    {
                        float x, y;
                        p.raw (p.headFrames - trim - W + j, x, y); eh += 0.5 * ((double) x * x + (double) y * y);
                        p.rawTail (skip + j, x, y);                 et += 0.5 * ((double) x * x + (double) y * y);
                    }
                    if (et > 1e-14 && eh > 1e-14) g = (float) std::clamp (std::sqrt (eh / et), 0.25, 1.41);
                }
                f.tailGain[(size_t) m].store (g);
            }
            f.spliceSkip.store (skip);
            f.spliceTrim.store (trim);
            f.spliceRho.store ((float) rho);
        }
    return true;
}

// Drum Kit Designer の Producer Kit（この順番は保存した曲の番号になるので変えない。足すときは後ろに）
inline const std::vector<std::string>& producerKitNames()
{
    // Vintage Kit Brushes は奏法の並びが違う（ブラシのこすりなど）、Vintage Kit Rods は尾の欄の意味が違う（2017 年の世代。
    // 尾が別の大きく鳴っている所を指していて二度鳴りする。DSP 検査係 2026-11-12）ので、まだ入れていない
    static const std::vector<std::string> n { "Studio Kit", "Modern Maple Kit", "Stadium Kit", "Tight Maple Kit", "Birch Kit",
                                              "Classic Sixties Kit", "Seventies Plexi Kit", "Pawn Shop Kit" };
    return n;
}

inline std::string dkdKitFolder()
{
    return "/Library/Application Support/Logic/Sampler Instruments/03 Drums & Percussion/04 Drum Kit Designer/Drum Kit Designer";
}

inline bool kitInstalled (const std::string& kitName)
{
    struct stat st {};
    return ::stat ((dkdKitFolder() + "/" + kitName + ".exs").c_str(), &st) == 0;
}
} // namespace hk
