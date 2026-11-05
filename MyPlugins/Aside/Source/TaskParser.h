#pragma once
// ひとりごと（文字にしたもの）から「やること」と「いつ」を取り出す（JUCE なし。単体テストできる）
//
//   「あ、あとで請求書出さなきゃ」        → やること「請求書出す」
//   「明日の3時までに見積もり送らないと」  → 「見積もり送る」 明日 15:00
//   「金曜の夜8時に田中さんと打ち合わせ、忘れないように」→ 予定「田中さんと打ち合わせ」 金曜 20:00
//
// きっかけの言葉（あとで・しなきゃ・忘れないように・メモ など）がないものは、やることにしない。
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace aside
{
struct Due
{
    bool hasDate = false, hasTime = false;
    int y = 0, m = 0, d = 0, hh = 0, mm = 0;
};

struct Task
{
    bool ok = false;
    std::string title;      // やること（UTF-8）
    Due due;
    bool isEvent = false;   // 打ち合わせ・会議など、時刻のある予定（カレンダーへ）
    std::string trigger;    // 見つけたきっかけの言葉
};

struct Now { int y, m, d, wday, hh, mm; };   // wday: 0 = 日曜

inline Now nowLocal()
{
    std::time_t t = std::time (nullptr);
    std::tm tm {};
    localtime_r (&t, &tm);
    return { tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_wday, tm.tm_hour, tm.tm_min };
}

namespace detail
{
using u32 = std::u32string;

inline u32 toU32 (const std::string& s)
{
    u32 out;
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = (unsigned char) s[i];
        char32_t cp = 0; int n = 0;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { ++i; continue; }
        if (i + (size_t) n > s.size()) break;
        for (int k = 1; k < n; ++k) cp = (cp << 6) | ((unsigned char) s[i + (size_t) k] & 0x3f);
        out += cp;
        i += (size_t) n;
    }
    return out;
}

inline std::string toU8 (const u32& s)
{
    std::string out;
    for (char32_t c : s)
    {
        if (c < 0x80) out += (char) c;
        else if (c < 0x800) { out += (char) (0xc0 | (c >> 6)); out += (char) (0x80 | (c & 0x3f)); }
        else if (c < 0x10000) { out += (char) (0xe0 | (c >> 12)); out += (char) (0x80 | ((c >> 6) & 0x3f)); out += (char) (0x80 | (c & 0x3f)); }
        else { out += (char) (0xf0 | (c >> 18)); out += (char) (0x80 | ((c >> 12) & 0x3f)); out += (char) (0x80 | ((c >> 6) & 0x3f)); out += (char) (0x80 | (c & 0x3f)); }
    }
    return out;
}

inline u32 U (const char* s) { return toU32 (s); }

inline bool contains (const u32& s, const u32& w) { return s.find (w) != u32::npos; }

// w を全部消す
inline void eraseAll (u32& s, const u32& w)
{
    for (size_t p; (p = s.find (w)) != u32::npos;) s.erase (p, w.size());
}

// 全角の数字・記号を半角に、漢数字（九十九まで）を数字に
inline u32 normalise (u32 s)
{
    for (auto& c : s)
    {
        if (c >= U'０' && c <= U'９') c = U'0' + (c - U'０');
        else if (c == U'：') c = U':';
        else if (c == U'／') c = U'/';
        else if (c == U'　') c = U' ';
    }
    auto kd = [] (char32_t c) -> int
    {
        const char32_t* k = U"〇一二三四五六七八九";
        for (int i = 0; i < 10; ++i) if (k[i] == c) return i;
        return -1;
    };
    u32 out;
    for (size_t i = 0; i < s.size();)
    {
        // 漢数字のかたまり（十を含む）を数字に。ただし「一緒」「一回」などの言葉は数字にしない方がよいので、
        // 後ろに 時・日・分・月・曜 などが続くときだけ変える
        size_t j = i;
        while (j < s.size() && (kd (s[j]) >= 0 || s[j] == U'十')) ++j;
        if (j > i && j < s.size() && contains (U"時日分月半週", u32 (1, s[j])))
        {
            int val = 0, cur = 0;
            for (size_t k = i; k < j; ++k)
            {
                if (s[k] == U'十') { val += (cur == 0 ? 1 : cur) * 10; cur = 0; }
                else cur = cur * 10 + kd (s[k]);
            }
            val += cur;
            for (char c : std::to_string (val)) out += (char32_t) c;
            i = j;
            continue;
        }
        if (j > i) { out += s.substr (i, j - i); i = j; continue; }
        out += s[i++];
    }
    return out;
}

inline bool isDigit (char32_t c) { return c >= U'0' && c <= U'9'; }

// s の p から数字を読む。読めた桁数を返す
inline int readInt (const u32& s, size_t p, int& v)
{
    v = 0; int n = 0;
    while (p + (size_t) n < s.size() && isDigit (s[p + (size_t) n]) && n < 4) v = v * 10 + (int) (s[p + (size_t) n++] - U'0');
    return n;
}

inline void addDays (int& y, int& m, int& d, int days)
{
    std::tm tm {};
    tm.tm_year = y - 1900; tm.tm_mon = m - 1; tm.tm_mday = d + days; tm.tm_hour = 12;
    std::mktime (&tm);
    y = tm.tm_year + 1900; m = tm.tm_mon + 1; d = tm.tm_mday;
}

inline void addMinutes (Now& n, int minutes)
{
    std::tm tm {};
    tm.tm_year = n.y - 1900; tm.tm_mon = n.m - 1; tm.tm_mday = n.d; tm.tm_hour = n.hh; tm.tm_min = n.mm + minutes; tm.tm_isdst = -1;
    std::mktime (&tm);
    n = { tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_wday, tm.tm_hour, tm.tm_min };
}

// [from, to) を消して、消した所を覚える
struct Cutter
{
    u32& s;
    std::vector<std::pair<size_t, size_t>> cuts;
    void cut (size_t a, size_t b) { cuts.push_back ({ a, b }); }
    u32 rest() const
    {
        u32 out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            bool in = false;
            for (auto& c : cuts) if (i >= c.first && i < c.second) { in = true; break; }
            if (! in) out += s[i];
        }
        return out;
    }
};

// 「〜ない」の形（出さ）を、辞書の形（出す）に。わからなければそのまま
inline u32 naiToPlain (const u32& stem)
{
    if (stem.empty()) return stem;
    const char32_t last = stem.back();
    const u32 base = stem.substr (0, stem.size() - 1);
    static const char32_t a[] = U"かがさたなばまらわ", u[] = U"くぐすつぬぶむるう";
    for (int i = 0; i < 9; ++i) if (last == a[i]) return base + u32 (1, u[i]);
    if (last == U'し') return base + U"する";
    if (last == U'こ' && base.empty()) return U"くる";
    return stem + U"る";   // 見・食べ・かけ など
}
} // namespace detail

//==============================================================================
inline Task parse (const std::string& utf8, const Now& now)
{
    using namespace detail;
    Task t;
    u32 s = normalise (toU32 (utf8));
    // 句読点と空白は区切りとしてだけ使う
    for (auto& c : s) if (c == U'？' || c == U'?' || c == U'！' || c == U'!') c = U'。';

    // 1. きっかけの言葉
    struct Trig { const char* w; bool strong; };
    static const Trig trigs[] {
        { "あとで", true }, { "後で", true }, { "忘れないうちに", true }, { "忘れないように", true }, { "忘れずに", true },
        { "忘れてた", true }, { "リマインド", true }, { "メモして", true }, { "メモ", true }, { "やることに", true },
        { "なきゃ", true }, { "なくちゃ", true }, { "なければ", true }, { "ないと", true }, { "ねば", true },
        { "しとかな", true }, { "しておかな", true }, { "しとこう", true }, { "しておこう", true }, { "やっとこう", true },
        { "やっておこう", true }, { "とかないと", true }, { "べき", true }, { "予定", false }, { "までに", false } };
    bool strong = false;
    for (auto& tr : trigs)
        if (contains (s, U (tr.w)))
        {
            if (t.trigger.empty() || tr.strong) t.trigger = tr.w;
            strong = strong || tr.strong;
        }
    if (! strong) return t;

    // 2. 時刻と日付
    Cutter cut { s, {} };
    Due due;
    int y = now.y, m = now.m, d = now.d;

    auto findCut = [&] (const char* w) -> bool
    {
        const auto ww = U (w);
        const size_t p = s.find (ww);
        if (p == u32::npos) return false;
        cut.cut (p, p + ww.size());
        return true;
    };

    // 相対: N 分後 / N 時間後
    for (size_t p = 0; p < s.size(); ++p)
    {
        int v = 0;
        const int n = readInt (s, p, v);
        if (n == 0) continue;
        const u32 rest = s.substr (p + (size_t) n);
        int mins = -1; size_t len = 0;
        if (rest.rfind (U"分後", 0) == 0) { mins = v; len = 2; }
        else if (rest.rfind (U"時間後", 0) == 0) { mins = v * 60; len = 3; }
        else if (rest.rfind (U"時間半後", 0) == 0) { mins = v * 60 + 30; len = 4; }
        if (mins < 0) { p += (size_t) n - 1; continue; }
        Now x = now;
        addMinutes (x, mins);
        due = { true, true, x.y, x.m, x.d, x.hh, x.mm };
        cut.cut (p, p + (size_t) n + len);
        break;
    }

    if (! due.hasDate)
    {
        // 日
        int addD = -1;
        if (findCut ("明後日") || findCut ("あさって")) addD = 2;
        else if (findCut ("明日") || findCut ("あした") || findCut ("あす")) addD = 1;
        else if (findCut ("今日") || findCut ("きょう") || findCut ("今夜") || findCut ("今晩")) addD = 0;
        if (addD >= 0) { addDays (y, m, d, addD); due.hasDate = true; }

        // 曜日（来週の / 今週の）
        static const char32_t wd[] = U"日月火水木金土";
        const bool nextWeek = findCut ("来週");
        const bool thisWeek = findCut ("今週");
        for (size_t p = 0; p + 1 < s.size() && ! due.hasDate; ++p)
        {
            if (s[p + 1] != U'曜') continue;
            int w = -1;
            for (int i = 0; i < 7; ++i) if (s[p] == wd[i]) w = i;
            if (w < 0) continue;
            size_t e = p + 2;
            if (e < s.size() && s[e] == U'日') ++e;
            int delta = (w - now.wday + 7) % 7;
            if (nextWeek) delta = (7 - (now.wday + 6) % 7) + (w + 6) % 7;   // 来週の月曜を基準に
            else if (delta == 0 && ! thisWeek) delta = 7;
            y = now.y; m = now.m; d = now.d;
            addDays (y, m, d, delta);
            due.hasDate = true;
            cut.cut (p, e);
        }
        if (nextWeek && ! due.hasDate) { y = now.y; m = now.m; d = now.d; addDays (y, m, d, 7 - (now.wday + 6) % 7); due.hasDate = true; }

        // M月D日 / M/D / D日
        for (size_t p = 0; p < s.size() && ! due.hasDate; ++p)
        {
            int a = 0, b = 0;
            const int n1 = readInt (s, p, a);
            if (n1 == 0) continue;
            size_t q = p + (size_t) n1;
            if (q < s.size() && (s[q] == U'月' || s[q] == U'/'))
            {
                const int n2 = readInt (s, q + 1, b);
                if (n2 > 0 && a >= 1 && a <= 12 && b >= 1 && b <= 31)
                {
                    size_t e = q + 1 + (size_t) n2;
                    if (e < s.size() && s[e] == U'日') ++e;
                    y = now.y; m = a; d = b;
                    if (m < now.m || (m == now.m && d < now.d)) ++y;
                    due.hasDate = true;
                    cut.cut (p, e);
                    break;
                }
            }
            if (q < s.size() && s[q] == U'日' && a >= 1 && a <= 31 && (q + 1 >= s.size() || s[q + 1] != U'間'))
            {
                y = now.y; m = now.m; d = a;
                if (d < now.d) { if (++m > 12) { m = 1; ++y; } }
                due.hasDate = true;
                cut.cut (p, q + 1);
                break;
            }
            p = q - 1;
        }

        // 時刻: (午前|午後|朝|昼|夕方|夜)? N時(半|M分)?
        int ampm = 0;   // 1 = 午前, 2 = 午後
        int defaultHour = -1;
        if (findCut ("午前")) ampm = 1;
        if (findCut ("午後")) ampm = 2;
        if (contains (s, U"夕方")) { ampm = 2; defaultHour = 17; findCut ("夕方"); }
        if (contains (s, U"今夜") || contains (s, U"今晩")) { ampm = 2; defaultHour = 20; }
        else if (contains (s, U"夜")) { ampm = 2; defaultHour = 20; findCut ("夜"); }
        if (contains (s, U"朝")) { ampm = 1; defaultHour = 9; findCut ("朝"); }
        if (contains (s, U"昼")) { if (ampm == 0) ampm = 2; defaultHour = 12; findCut ("昼"); }
        bool gotTime = false;
        for (size_t p = 0; p < s.size(); ++p)
        {
            int h = 0;
            const int n = readInt (s, p, h);
            if (n == 0) continue;
            size_t q = p + (size_t) n;
            if (q < s.size() && (s[q] == U'時' || s[q] == U':') && h <= 24 && (q + 1 >= s.size() || s[q + 1] != U'間'))
            {
                int mi = 0;
                size_t e = q + 1;
                if (e < s.size() && s[e] == U'半') { mi = 30; ++e; }
                else
                {
                    int v = 0;
                    const int n2 = readInt (s, e, v);
                    if (n2 > 0 && v < 60) { mi = v; e += (size_t) n2; if (e < s.size() && s[e] == U'分') ++e; }
                }
                if (ampm == 2 && h < 12) h += 12;
                else if (ampm == 0 && h >= 1 && h <= 7) h += 12;   // 「3時」は午後 3 時のことが多い
                due.hh = h % 24; due.mm = mi; due.hasTime = true; gotTime = true;
                cut.cut (p, e);
                break;
            }
            p = q - 1;
        }
        if (! gotTime && defaultHour >= 0) { due.hh = defaultHour; due.mm = 0; due.hasTime = true; }
        if (due.hasTime && ! due.hasDate)
        {
            // 時刻だけなら今日。もう過ぎていれば明日
            if (due.hh < now.hh || (due.hh == now.hh && due.mm <= now.mm)) addDays (y, m, d, 1);
            due.hasDate = true;
        }
        if (due.hasDate) { due.y = y; due.m = m; due.d = d; }
    }
    t.due = due;

    // 3. やること（きっかけの言葉・つなぎの言葉・日付を消す）
    u32 r = cut.rest();
    static const char* fillers[] { "そういえば", "そうだ", "えーっと", "えーと", "えっと", "うーん", "あのー", "あっ", "あー",
                                   "やばい", "やば", "よし", "ああ", "あ、", "まあ" };
    static const char* drops[] { "忘れないうちに", "忘れないように", "忘れずに", "忘れてた", "リマインドして", "リマインド",
                                 "メモしといて", "メモしておいて", "メモして", "あとで", "後で", "やることに入れて", "までに" };
    for (auto* f : fillers) { const auto w = U (f); if (r.rfind (w, 0) == 0) r.erase (0, w.size()); }
    for (auto* f : drops) eraseAll (r, U (f));
    // 「〜しなきゃ」などを辞書の形に
    static const char* oblig[] { "しなきゃいけない", "なきゃいけない", "なくちゃいけない", "なければならない", "なければいけない",
                                 "ないといけない", "とかないと", "とかなきゃ", "なきゃ", "なくちゃ", "なければ", "ないと", "ねば" };
    for (auto* o : oblig)
    {
        const auto w = U (o);
        const size_t p = r.find (w);
        if (p == u32::npos) continue;
        u32 head = r.substr (0, p), tailS = r.substr (p + w.size());
        if (u32 (U (o)).rfind (U"とか", 0) == 0) { r = head + U"とく" + tailS; break; }   // 出しとかないと → 出しとく
        if (u32 (U (o)).rfind (U"しなきゃ", 0) == 0) { r = head + U"する" + tailS; break; }
        // head の最後の言葉（ひらがな 1 文字）を変える
        r = naiToPlain (head) + tailS;
        break;
    }
    // 終わりの「な」「ね」「よ」「わ」「ー」「、」「。」「って」「のを」「こと」と、はじめの区切り
    auto trimSet = U"、。，．,. なねよわーぁ　";
    for (bool again = true; again;)
    {
        again = false;
        while (! r.empty() && contains (trimSet, u32 (1, r.back()))) { r.pop_back(); again = true; }
        for (auto* suf : { U"って", U"ことを", U"こと", U"のを", U"メモ", U"まで", U"を", U"の", U"は" })
        {
            const u32 w (suf);
            if (r.size() > w.size() + 1 && r.compare (r.size() - w.size(), w.size(), w) == 0) { r.erase (r.size() - w.size()); again = true; break; }
        }
    }
    if (r.rfind (U"メモ", 0) == 0) r.erase (0, 2);
    while (! r.empty() && contains (U"、。，．,. の　に", u32 (1, r.front()))) r.erase (0, 1);
    // 途中の「、」は 1 つ目まで（「請求書出す、忘れないように」の後ろは消えている）
    for (size_t p; (p = r.find (U"、、")) != u32::npos;) r.erase (p, 1);
    while (! r.empty() && contains (U"、。の", u32 (1, r.back()))) r.pop_back();
    if (r.size() < 2) return t;

    t.title = toU8 (r);
    static const char* eventWords[] { "打ち合わせ", "会議", "ミーティング", "MTG", "面談", "予約", "アポ", "飲み会", "電話会議", "通院" };
    for (auto* e : eventWords) if (contains (r, U (e))) t.isEvent = due.hasTime;
    t.ok = true;
    return t;
}

inline std::string dueText (const Due& d, const Now& now)
{
    if (! d.hasDate) return {};
    static const char* wd[] { "日", "月", "火", "水", "木", "金", "土" };
    std::tm tm {};
    tm.tm_year = d.y - 1900; tm.tm_mon = d.m - 1; tm.tm_mday = d.d; tm.tm_hour = 12;
    std::mktime (&tm);
    int ny = now.y, nm = now.m, nd = now.d;
    std::string day;
    if (d.y == ny && d.m == nm && d.d == nd) day = "今日";
    else { detail::addDays (ny, nm, nd, 1); day = (d.y == ny && d.m == nm && d.d == nd) ? "明日" : std::to_string (d.m) + "/" + std::to_string (d.d) + "（" + wd[tm.tm_wday] + "）"; }
    if (d.hasTime)
    {
        char b[16];
        std::snprintf (b, sizeof b, " %d:%02d", d.hh, d.mm);
        day += b;
    }
    return day;
}
} // namespace aside
