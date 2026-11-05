// PatternFinder の確認用:  clang++ -std=c++17 -I../Source test_finder.cpp -o /tmp/tf && /tmp/tf
#include "PatternFinder.h"
#include <cstdio>
#include <random>
using namespace third;

Step S (int kind, const char* key, bool strong = true) { Step s; s.kind = kind; s.key = key; s.label = key; s.strong = strong; s.bundle = "b"; return s; }

int fails = 0;
void expect (bool c, const char* what) { std::printf ("%s %s\n", c ? "OK" : "NG", what); fails += c ? 0 : 1; }

int main()
{
    // 1. 「Excel → ⌘C → Chrome → クリック(入力欄) → ⌘V → Return」を 3 回。間に関係ない操作
    const std::vector<Step> flow { S (App, "app:excel"), S (Key, "key:cmd+C"), S (App, "app:chrome"),
                                   S (Click, "click:chrome:AXTextField:検索"), S (Key, "key:cmd+V"), S (Key, "key:ret") };
    std::mt19937 rng (3);
    std::vector<Step> log;
    std::set<std::string> shown;
    double t = 0;
    int firstAt = -1, reports = 0;
    for (int rep = 0; rep < 5; ++rep)
    {
        for (int k = 0; k < 4; ++k)   // 関係ない操作（毎回ちがう）
        {
            auto s = S (Click, ("click:x:AXButton:" + std::to_string (rng() % 1000)).c_str());
            s.t = (t += 20); push (log, s);
            auto ty = S (Type, "type", false); ty.t = (t += 3); push (log, ty);
        }
        for (auto s : flow)
        {
            s.t = (t += 2);
            push (log, s);
            auto f = findRepeat (log, shown);
            if (f.ok)
            {
                ++reports;
                if (firstAt < 0) firstAt = rep;
                std::printf ("   found len=%d count=%d at rep %d\n", f.length, f.count, rep + 1);
                shown.insert (skipKey (f.signature));
            }
        }
        auto f = findRepeat (log, shown, {}, false);   // 手が止まった
        if (f.ok) { ++reports; if (firstAt < 0) firstAt = rep; std::printf ("   found (pause) len=%d at rep %d\n", f.length, rep + 1); shown.insert (skipKey (f.signature)); }
    }
    expect (firstAt == 2, "3 回目で見つかる");
    expect (reports == 1, "同じ流れは 1 回だけ知らせる");

    // 2. 同じキーの連打（⌘Z ×9）は知らせない
    {
        std::vector<Step> l; std::set<std::string> sh; bool any = false;
        for (int i = 0; i < 9; ++i) { auto s = S (Key, "key:cmd+Z"); s.t = i; push (l, s); any = any || findRepeat (l, sh).ok; }
        expect (! any, "同じキーの連打は知らせない");
    }
    // 3. 入力とクリック（弱い操作）だけの並びは知らせない
    {
        std::vector<Step> l; std::set<std::string> sh; bool any = false; double tt = 0;
        for (int i = 0; i < 6; ++i)
            for (auto s : { S (Click, "click:a:AXTextArea", false), S (Type, "type", false), S (Click, "click:a:AXGroup", false) })
            { s.t = (tt += 1); s.bundle = std::to_string (i % 2); push (l, s); any = any || findRepeat (l, sh).ok; }
        expect (! any, "弱い操作だけの並びは知らせない");
    }
    // 4. 1 回の流れが 3 分より長くかかるものは知らせない
    {
        std::vector<Step> l; std::set<std::string> sh; bool any = false; double tt = 0;
        for (int rep = 0; rep < 4; ++rep) for (auto s : flow) { s.t = (tt += 100); push (l, s); any = any || findRepeat (l, sh).ok; }
        expect (! any, "ゆっくりすぎる流れは知らせない");
    }
    // 5. 「もう出さない」にした流れは知らせない
    {
        std::vector<Step> l; std::set<std::string> sh { skipKey (signatureOf (flow, 0, (int) flow.size())) }; bool any = false; double tt = 0;
        for (int rep = 0; rep < 4; ++rep) { for (auto s : flow) { s.t = (tt += 2); push (l, s); any = any || findRepeat (l, sh).ok; } any = any || findRepeat (l, sh, {}, false).ok; }
        expect (! any, "出さないにした流れは知らせない");
    }
    // 5b. 続けてくり返す（間に何もない）流れも、手が止まったときに 1 回だけ
    {
        std::vector<Step> l; std::set<std::string> sh; int rep3 = 0; double tt = 0;
        for (int rep = 0; rep < 5; ++rep)
        {
            for (auto s : flow) { s.t = (tt += 2); push (l, s); if (auto f = findRepeat (l, sh); f.ok) { ++rep3; sh.insert (skipKey (f.signature)); } }
            if (rep == 2) if (auto f = findRepeat (l, sh, {}, false); f.ok) { ++rep3; sh.insert (skipKey (f.signature)); std::printf ("   back-to-back len=%d\n", f.length); }
        }
        expect (rep3 == 1, "続けてくり返す流れは 1 回だけ知らせる");
    }
    // 6. 速さ: 2 万操作の記録でも 1 回の探索が速い
    {
        std::vector<Step> l; std::set<std::string> sh; double tt = 0;
        for (int i = 0; i < 20000; ++i) { auto s = S (Click, ("click:k" + std::to_string (rng() % 50)).c_str()); s.t = (tt += 1); l.push_back (s); }
        const auto t0 = std::clock();
        for (int i = 0; i < 100; ++i) findRepeat (l, sh);
        const double ms = 1000.0 * (std::clock() - t0) / CLOCKS_PER_SEC / 100;
        std::printf ("   %.2f ms / 回\n", ms);
        expect (ms < 20, "探索は 20 ms 以内");
    }
    std::printf (fails == 0 ? "all ok\n" : "%d failed\n", fails);
    return fails;
}
