#pragma once
// Mac だけの部分（Objective-C++ は ThirdMac_mac.mm）
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "PatternFinder.h"

namespace third::mac
{
    // ---- 見張る: アプリの切り替え・⌘/⌃ のショートカット・Return などの特別なキー・クリックした所の名前。
    // 打った文字の中身は記録しない（「文字の入力」という 1 つの操作になるだけ）
    bool startWatching (std::function<void (const Step&)> onStep, const std::vector<std::string>& skipBundles);
    void stopWatching();
    bool watching();
    void setSkip (const std::vector<std::string>& skipBundles);

    // ---- 許可
    bool inputMonitoringTrusted (bool prompt);   // 入力監視（キーとクリックを見る）
    bool accessibilityTrusted (bool prompt);     // アクセシビリティ（押した所の名前・くり返し）
    void openPrivacy (const char* pane);         // "ListenEvent" / "Accessibility"

    // ---- くり返す（裏で 1 つずつ）。progress(何番目, "run" / "wait")、done(うまくいったか, 理由)
    void run (const std::vector<Step>& steps, std::function<void (int, const std::string&)> progress,
              std::function<void (bool, const std::string&)> done);
    void continueRun();   // 「入力」で止まっているのを続ける
    void cancelRun();
    bool running();

    std::string appName (const std::string& bundle);

    // ---- どのアプリが前でも効くキー（Carbon）。slot 0 = 続ける、1〜9 = 自動にした流れ
    bool registerHotKey (int slot, int keyCode, int mods, std::function<void()> onPress);
    int keyCodeForDigit (int digit);
    int modsCtrlOpt();
    int keyCodeT();

    // ---- メニューバー
    void setStatusIcon (int state);   // 0 = 止めている, 1 = 見張っている, 2 = 見つけた, 3 = くり返し中
    void setStatusMenu (const std::vector<std::pair<std::string, int>>& items, std::function<void (int)> onPick);

    // ---- 知らせの窓（どのデスクトップでも・全画面の上にも）
    void makeHud (void* nsView);
    void hudPosition (int width, int& x, int& y);
    void showWindow (void* nsView);

    bool launchAtLogin();
    bool setLaunchAtLogin (bool on);
}
