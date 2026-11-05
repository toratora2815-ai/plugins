// TaskParser の確認用:  clang++ -std=c++17 -I../Source test_parser.cpp -o /tmp/tp && /tmp/tp
#include "TaskParser.h"
#include <cstdio>
using namespace aside;

int main()
{
    const Now now { 2026, 11, 6, 5, 10, 30 };   // 2026/11/6（金）10:30
    struct C { const char* in; bool ok; const char* title; const char* due; bool ev; };
    const C cs[] {
        { "あ、あとで請求書出さなきゃ", true, "請求書出す", "", false },
        { "明日の3時までに見積もり送らないと", true, "見積もり送る", "明日 15:00", false },
        { "金曜の夜8時に田中さんと打ち合わせ、忘れないように", true, "田中さんと打ち合わせ", "11/13（金） 20:00", true },
        { "来週の月曜に経費精算しなきゃ", true, "経費精算する", "11/9（月）", false },
        { "そういえば牛乳買わなきゃ", true, "牛乳買う", "", false },
        { "30分後に洗濯物取り込まないと", true, "洗濯物取り込む", "今日 11:00", false },
        { "11月20日までに年賀状のデザイン決めないと", true, "年賀状のデザイン決める", "11/20（金）", false },
        { "忘れないうちに、佐藤さんに電話する", true, "佐藤さんに電話する", "", false },
        { "今日は天気がいいな", false, "", "", false },
        { "このベースの音いいね", false, "", "", false },
        { "夕方に銀行行かないと", true, "銀行行く", "今日 17:00", false },
        { "あとでミックスのボーカルもう少し上げる", true, "ミックスのボーカルもう少し上げる", "", false },
        { "明後日の午前10時に歯医者の予約、メモして", true, "歯医者の予約", "11/8（日） 10:00", true },
        { "三時に資料持って行かなきゃ", true, "資料持って行く", "今日 15:00", false },
        { "請求書を出すのを忘れないように", true, "請求書を出す", "", false },
        { "メモ、プラグインのREADME直す", true, "プラグインのREADME直す", "", false },
        { "20日に家賃振り込まなきゃ", true, "家賃振り込む", "11/20（金）", false },
    };
    int ok = 0, n = 0;
    for (auto& c : cs)
    {
        ++n;
        const auto t = parse (c.in, now);
        const auto due = dueText (t.due, now);
        const bool pass = t.ok == c.ok && (! c.ok || (t.title == c.title && due == c.due && t.isEvent == c.ev));
        ok += pass;
        std::printf ("%s %s\n     → ok=%d 「%s」 %s%s\n", pass ? "OK" : "NG", c.in, (int) t.ok, t.title.c_str(), due.c_str(), t.isEvent ? " [予定]" : "");
    }
    std::printf ("%d / %d\n", ok, n);
    return ok == n ? 0 : 1;
}
