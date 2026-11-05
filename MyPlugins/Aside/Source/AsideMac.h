#pragma once
// Mac だけの部分（Objective-C++ は AsideMac_mac.mm）
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace aside::mac
{
    // ---- 聞く（音声認識は Mac の中だけで。できない Mac では聞かない）
    // onText(文字, 確定したか): 話し終わるたびに確定した文字が届く（メインスレッド）
    // onLevel(0〜1): 声の大きさ（画面のメーター用）
    // onState(状態): "listening" / "denied-mic" / "denied-speech" / "no-ondevice" / "error:..."
    void startListening (std::function<void (const std::string&, bool)> onText,
                         std::function<void (float)> onLevel,
                         std::function<void (const std::string&)> onState);
    void stopListening();
    bool listening();

    // ---- リマインダー・カレンダー（EventKit）
    // 結果はメインスレッドで done(識別子, エラー)。識別子は取り消しに使う
    struct Item
    {
        std::string title, notes;
        bool hasDate = false, hasTime = false;
        int y = 0, m = 0, d = 0, hh = 0, mm = 0;
        bool event = false;            // true ならカレンダーの予定（1 時間）
        std::string listName;          // リマインダーのリスト名（なければ作る）
    };
    void addItem (const Item& item, std::function<void (const std::string& id, const std::string& error)> done);
    void removeItem (const std::string& id, bool event);
    void openReminders();

    // ---- いま前にいるアプリ
    std::string frontBundle();

    // ---- どのアプリが前でも効くキー（Carbon）
    struct HotKeyChoice { const char* label; int keyCode; int mods; };
    const std::vector<HotKeyChoice>& hotKeyChoices();
    bool registerHotKey (int choice, std::function<void()> onPress);

    // ---- メニューバー
    void setStatusIcon (int state);   // 0 = 聞いていない, 1 = 聞いている, 2 = 声が入っている, 3 = 次の一言をメモ
    void setStatusMenu (const std::vector<std::pair<std::string, int>>& items, std::function<void (int)> onPick);

    // ---- 小さな知らせ（HUD）の窓: どのデスクトップでも・全画面の上にも出る。クリックは通す / 通さない
    void makeHud (void* nsView, bool clickThrough);
    void hudPosition (int width, int& x, int& y);
    void showWindow (void* nsView);

    bool launchAtLogin();
    bool setLaunchAtLogin (bool on);
}
