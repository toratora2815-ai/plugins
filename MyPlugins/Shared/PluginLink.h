#pragma once
// ============================================================================
// Plugin Link … 自作プラグイン同士が裏で情報をやりとりする「掲示板」
//
// しくみ:
//   ~/Library/Application Support/MyPlugins/PluginLink-v1.bin を共有メモリとして全員で開く
//   （プラグインごとに別のバイナリなので、ふつうの static 変数は共有できないため）。
//   ・各プラグインは「席（Slot）」を 1 つ取り、種類・グループ（LINK 1〜4）・数値 8 個を書く
//   ・同じグループの相手の席を読んで、その数値で自分の動きを変える
//   ・生きている印（aliveMs）が 3 秒更新されない席は「いない」扱い。10 秒で空き席に戻る
//   すべて lock-free の atomic で読み書きするので、音の処理のスレッドからも安全に読める。
//
// このファイルは JUCE を使わない（単体テストでそのまま使える）。
// ============================================================================
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <random>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

namespace plink
{
    enum Kind : uint32_t
    {
        kindNone = 0,
        kindBalanceScope = 1,   // 送る: 出すぎの帯域
        kindFaderPilot = 2,     // 送る: フェーダーの位置
        kindVelvetTamer = 3,    // 受ける: FOCUS をそこへ
        kindHorizonSpread = 4,  // 受ける: 広がりを少し抑える
    };

    inline const char* kindName (uint32_t k)
    {
        switch (k)
        {
            case kindBalanceScope:  return "Balance Scope";
            case kindFaderPilot:    return "Fader Pilot";
            case kindVelvetTamer:   return "Velvet Tamer";
            case kindHorizonSpread: return "Horizon Spread";
            default:                return "?";
        }
    }

    // ---- 送る数値の番号 ----
    // Balance Scope
    namespace bs
    {
        enum { valid = 0,      // 1 = 判定できるだけ聴いた
               excessDb = 1,   // 許容幅を何 dB 超えているか（0 = 出すぎなし）
               lowHz = 2, highHz = 3, peakHz = 4 };
    }
    // Fader Pilot
    namespace fp
    {
        enum { rideDb = 0,     // 今のフェーダー位置（+ = 上げている）
               active = 1 };   // 1 = 声が鳴っている（FLOOR より大きい）
    }

    constexpr int numGroups = 4;   // LINK 1〜4（0 = OFF）
    constexpr int numSlots = 64;
    constexpr int numValues = 8;
    constexpr int nameLen = 48;
    constexpr int64_t aliveTimeoutMs = 3000;
    constexpr int64_t reclaimMs = 10000;
    constexpr uint32_t magicValue = 0x4C4E4B31;   // "LNK1"

    struct Slot
    {
        std::atomic<uint64_t> owner;      // 0 = 空き
        std::atomic<uint32_t> kind;
        std::atomic<uint32_t> group;
        std::atomic<int64_t>  aliveMs;    // 生きている印
        std::atomic<int64_t>  valuesMs;   // 最後に数値を書いた時刻
        std::atomic<float>    v[numValues];
        std::atomic<uint32_t> nameSeq;    // 奇数の間は書きかけ
        char name[nameLen];               // トラック名（なければ空）
    };

    struct Board
    {
        std::atomic<uint32_t> magic;
        uint32_t reserved[15];
        Slot slots[numSlots];
    };

    static_assert (std::atomic<uint64_t>::is_always_lock_free, "need lock-free 64-bit atomics");
    static_assert (std::atomic<int64_t>::is_always_lock_free, "need lock-free 64-bit atomics");
    static_assert (std::atomic<float>::is_always_lock_free, "need lock-free float atomics");

    // どのプロセスでも同じ時計（起動してからの時間、ms）
    inline int64_t nowMs()
    {
        timespec ts {};
        clock_gettime (CLOCK_MONOTONIC, &ts);
        return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }

    // 共有メモリの場所（テストのときは環境変数 PLUGIN_LINK_FILE で変えられる）
    inline std::string boardPath()
    {
        if (const char* p = std::getenv ("PLUGIN_LINK_FILE")) return p;
        const char* home = std::getenv ("HOME");
        std::string dir = std::string (home != nullptr ? home : "/tmp") + "/Library/Application Support/MyPlugins";
        ::mkdir (dir.c_str(), 0755);
        return dir + "/PluginLink-v1.bin";
    }

    // このバイナリの中で 1 回だけ開く（開けなかったら nullptr ＝ リンクなしで動く）
    inline Board* board()
    {
        static Board* b = []() -> Board*
        {
            const auto path = boardPath();
            const int fd = ::open (path.c_str(), O_RDWR | O_CREAT, 0644);
            if (fd < 0) return nullptr;
            struct stat st {};
            if (::fstat (fd, &st) != 0) { ::close (fd); return nullptr; }
            if (st.st_size < (off_t) sizeof (Board) && ::ftruncate (fd, (off_t) sizeof (Board)) != 0)
            {
                ::close (fd);
                return nullptr;
            }
            void* p = ::mmap (nullptr, sizeof (Board), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            ::close (fd);
            if (p == MAP_FAILED) return nullptr;
            auto* bd = static_cast<Board*> (p);
            bd->magic.store (magicValue);   // 0 で埋まった新しいファイルはそのまま使える
            return bd;
        }();
        return b;
    }

    // 相手の様子（読むとき用のコピー）
    struct Peer
    {
        bool found = false;
        uint32_t kind = kindNone;
        float v[numValues] {};
        int64_t valuesAgeMs = 0;     // 数値が何 ms 前のものか
        char name[nameLen] {};
    };

    // プラグイン 1 個（インスタンス）ぶんの席
    class Endpoint
    {
    public:
        explicit Endpoint (Kind k) : kind (k)
        {
            std::random_device rd;
            id = ((uint64_t) rd() << 32) ^ (uint64_t) rd() ^ (uint64_t) (uintptr_t) this;
            if (id == 0) id = 1;
            claim();
        }

        ~Endpoint()
        {
            if (auto* s = slot.load())
            {
                s->group.store (0);
                uint64_t expected = id;
                s->owner.compare_exchange_strong (expected, 0);
            }
        }

        bool available() const { return slot.load() != nullptr; }

        // メッセージスレッドのタイマーから定期的に呼ぶ。席を取られていたら取り直す
        void maintain()
        {
            auto* s = slot.load();
            if (s == nullptr || s->owner.load() != id) claim();
            heartbeat();
        }

        // 生きている印（どのスレッドからでもよい）
        void heartbeat()
        {
            if (auto* s = slot.load (std::memory_order_acquire))
                if (s->owner.load (std::memory_order_relaxed) == id)
                    s->aliveMs.store (nowMs(), std::memory_order_relaxed);
        }

        void setGroup (int g)   // メッセージスレッドから
        {
            group.store ((g >= 1 && g <= numGroups) ? g : 0);
            if (auto* s = slot.load()) s->group.store ((uint32_t) group.load(), std::memory_order_relaxed);
        }
        int getGroup() const { return group.load (std::memory_order_relaxed); }

        // 数値を書く（音の処理のスレッドから呼んでよい）
        void publish (int index, float value)
        {
            auto* s = slot.load (std::memory_order_acquire);
            if (s == nullptr || index < 0 || index >= numValues || s->owner.load (std::memory_order_relaxed) != id) return;
            s->v[index].store (value, std::memory_order_relaxed);
        }
        void stamp()   // 数値をひとまとまり書き終えたら呼ぶ
        {
            if (auto* s = slot.load (std::memory_order_acquire))
                if (s->owner.load (std::memory_order_relaxed) == id)
                    s->valuesMs.store (nowMs(), std::memory_order_release);
        }

        void setName (const std::string& n)   // メッセージスレッドから
        {
            myName = n;
            auto* s = slot.load();
            if (s == nullptr) return;
            s->nameSeq.fetch_add (1);                          // 奇数 = 書きかけ
            std::memset (s->name, 0, nameLen);
            std::strncpy (s->name, n.c_str(), nameLen - 1);
            s->nameSeq.fetch_add (1);
        }

        // 同じグループにいる、その種類の相手のうち一番新しく数値を書いたもの（自分は除く）
        bool find (Kind wanted, Peer& out) const
        {
            out = {};
            auto* bd = board();
            const int g = group.load (std::memory_order_relaxed);
            const Slot* self = slot.load (std::memory_order_relaxed);
            if (bd == nullptr || g == 0) return false;
            const int64_t now = nowMs();
            const Slot* best = nullptr;
            int64_t bestMs = 0;
            for (auto& s : bd->slots)
            {
                if (&s == self || s.owner.load (std::memory_order_relaxed) == 0) continue;
                if (s.kind.load (std::memory_order_relaxed) != (uint32_t) wanted) continue;
                if (s.group.load (std::memory_order_relaxed) != (uint32_t) g) continue;
                if (now - s.aliveMs.load (std::memory_order_relaxed) > aliveTimeoutMs) continue;
                const int64_t vm = s.valuesMs.load (std::memory_order_acquire);
                if (best == nullptr || vm > bestMs) { best = &s; bestMs = vm; }
            }
            if (best == nullptr) return false;
            out.found = true;
            out.kind = (uint32_t) wanted;
            for (int i = 0; i < numValues; ++i) out.v[i] = best->v[i].load (std::memory_order_relaxed);
            out.valuesAgeMs = bestMs > 0 ? now - bestMs : 1 << 30;
            readName (*best, out.name);
            return true;
        }

        // 同じグループに何人いるか（自分を除く。画面の表示用）
        int countPeers() const
        {
            auto* bd = board();
            const int g = group.load();
            const Slot* self = slot.load();
            if (bd == nullptr || g == 0) return 0;
            const int64_t now = nowMs();
            int n = 0;
            for (auto& s : bd->slots)
                if (&s != self && s.owner.load() != 0 && s.group.load() == (uint32_t) g
                    && now - s.aliveMs.load() <= aliveTimeoutMs)
                    ++n;
            return n;
        }

    private:
        static void readName (const Slot& s, char* dst)
        {
            for (int tries = 0; tries < 4; ++tries)
            {
                const uint32_t a = s.nameSeq.load (std::memory_order_acquire);
                if (a & 1u) continue;
                std::memcpy (dst, s.name, nameLen);
                std::atomic_thread_fence (std::memory_order_acquire);
                if (s.nameSeq.load (std::memory_order_relaxed) == a) { dst[nameLen - 1] = 0; return; }
            }
            dst[0] = 0;
        }

        void claim()
        {
            slot.store (nullptr);
            auto* bd = board();
            if (bd == nullptr) return;
            const int64_t now = nowMs();
            for (auto& s : bd->slots)
            {
                uint64_t cur = s.owner.load();
                const bool stale = cur != 0 && now - s.aliveMs.load() > reclaimMs;
                if (cur != 0 && ! stale) continue;
                // 書き始める前に「生きている」にしておく（ほかの人に古い席だと思われないように）
                if (! s.owner.compare_exchange_strong (cur, id)) continue;
                s.aliveMs.store (now);
                s.kind.store ((uint32_t) kind);
                s.group.store ((uint32_t) group.load());
                s.valuesMs.store (0);
                for (auto& x : s.v) x.store (0.0f);
                slot.store (&s, std::memory_order_release);
                if (! myName.empty()) setName (myName);
                else { s.nameSeq.fetch_add (1); s.name[0] = 0; s.nameSeq.fetch_add (1); }
                return;
            }
        }

        Kind kind;
        uint64_t id = 1;
        std::atomic<Slot*> slot { nullptr };
        std::atomic<int> group { 0 };
        std::string myName;
    };
}
