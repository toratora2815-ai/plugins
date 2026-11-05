#pragma once
// 操作の記録から「同じ流れの 3 回目」を見つける（JUCE なし。単体テストできる）
//
// 操作 1 つ = Step（アプリを前に・ショートカット・名前のあるボタンを押す・文字の入力 など）。
// 新しい操作が来るたびに、「いちばん新しい操作で終わる 3〜10 個の並び」が、前にも同じ順で
// 何回あったかを数える（重ならないように後ろから）。minCount 回に届いたら知らせる。
#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace third
{
enum Kind { App = 0, Key = 1, Click = 2, Type = 3 };

struct Step
{
    int kind = App;
    std::string key;     // 同じ操作かどうかを比べる文字列（例 "app:com.google.Chrome" / "key:cmd+8" / "click:com.apple.finder:AXButton:保存"）
    std::string label;   // 画面に出す説明
    bool strong = true;  // 名前のない所のクリックや文字の入力は弱い（それだけの流れは知らせない）
    double t = 0;        // 秒

    // くり返すときに使う情報
    std::string bundle;          // アプリ
    int keyCode = -1;            // キー
    unsigned long long flags = 0;
    std::string role, title;     // クリックした所（アクセシビリティの役割と名前）
    double relX = -1, relY = -1; // ウィンドウの中の位置（0〜1）。名前で見つからないときに使う
};

struct Found
{
    bool ok = false;
    int length = 0, count = 0;
    int start = 0;               // いちばん新しい回の始まり（steps の添字）
    std::string signature;       // 並びの key をつないだもの
};

inline std::string signatureOf (const std::vector<Step>& s, int from, int len)
{
    std::string sig;
    for (int i = from; i < from + len; ++i) { sig += s[(size_t) i].key; sig += '\x1f'; }
    return sig;
}

struct Rules
{
    int minCount = 3;
    int minLen = 3, maxLen = 10;
    double maxSpan = 180.0;      // 1 回の流れはこの秒数以内
    double window = 6 * 3600.0;  // これより古い操作は数えない
};

// 知らせた並びを skip に入れるときの形（2 回つなげておくと、ずらした並び（回し）もその一部になる）
inline std::string skipKey (const std::string& signature) { return signature + signature; }

// steps の最後で終わる並びを探す。skip に入っている並び（もう知らせた・出さない）と、その一部は知らせない。
// waitIfLonger: 前の回がどれも「次も同じ操作」だったときは、流れがまだ続くとみて今は知らせない
// （手が止まったときに waitIfLonger = false でもう一度呼ぶ）
inline Found findRepeat (const std::vector<Step>& steps, const std::set<std::string>& skip, const Rules& r = {}, bool waitIfLonger = true)
{
    Found best;
    const int n = (int) steps.size();
    if (n < r.minLen * r.minCount) return best;
    const double now = steps.back().t;

    for (int len = std::min (r.maxLen, n / r.minCount); len >= r.minLen; --len)
    {
        const int s0 = n - len;
        // 弱い操作だけ・同じ操作だけの並びは知らせない
        int strong = 0;
        std::set<std::string> distinct;
        for (int i = s0; i < n; ++i)
        {
            strong += steps[(size_t) i].strong ? 1 : 0;
            distinct.insert (steps[(size_t) i].key);
        }
        if (strong < 2 || (int) distinct.size() < std::max (2, len / 2)) continue;
        // 始まりと終わりが弱い操作の並びは、もっと短い並びにまかせる
        if (! steps[(size_t) s0].strong) continue;
        if (steps[(size_t) (n - 1)].t - steps[(size_t) s0].t > r.maxSpan) continue;

        int count = 1, lastStart = s0;
        std::set<std::string> nexts;   // 前の回のすぐ後の操作
        for (int i = s0 - len; i >= 0; --i)
        {
            if (now - steps[(size_t) i].t > r.window) break;
            if (i + len > lastStart) continue;
            bool same = true;
            for (int k = 0; k < len && same; ++k) same = steps[(size_t) (i + k)].key == steps[(size_t) (s0 + k)].key;
            if (! same) continue;
            if (steps[(size_t) (i + len - 1)].t - steps[(size_t) i].t > r.maxSpan) continue;
            ++count;
            lastStart = i;
            nexts.insert (steps[(size_t) (i + len)].key);
        }
        if (count < r.minCount) continue;
        if (waitIfLonger && nexts.size() == 1) return best;   // まだ続きそう（長い方を待つ）

        const auto sig = signatureOf (steps, s0, len);
        bool skipped = false;
        for (auto& s : skip) if (s.find (sig) != std::string::npos) { skipped = true; break; }
        if (skipped) continue;
        best = { true, len, count, s0, sig };
        return best;   // 長いものから見ているので、最初に見つかったものがいちばん長い
    }
    return best;
}

// 続けて同じ「入力」は 1 つにまとめる（記録に足す前に使う）
inline void push (std::vector<Step>& steps, const Step& s, size_t maxSteps = 20000)
{
    if (s.kind == Type && ! steps.empty() && steps.back().kind == Type && steps.back().bundle == s.bundle)
    {
        steps.back().t = s.t;
        return;
    }
    steps.push_back (s);
    if (steps.size() > maxSteps) steps.erase (steps.begin(), steps.begin() + (long) (steps.size() - maxSteps));
}
} // namespace third
