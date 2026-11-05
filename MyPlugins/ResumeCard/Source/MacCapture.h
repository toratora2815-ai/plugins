#pragma once
// Mac だけの部分（Objective-C++ は MacCapture_mac.mm）
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace rc
{
    struct AppInfo
    {
        std::string name, bundleId, appPath;
        std::string window;   // 前のウィンドウの題名
        std::string doc;      // 開いていた書類（ファイルの場所。わかるアプリだけ）
    };
    struct TabInfo { std::string browser, bundleId, title, url; };

    struct Capture
    {
        AppInfo front;
        std::string typed;      // カーソルの前の文（最後に書いていた所）。パスワード欄は取らない
        std::string selected;   // 選んでいた文字
        std::vector<AppInfo> others;
        std::vector<TabInfo> tabs;
        std::string finderPath;
        bool shot = false;      // 画面の写真が撮れたか
        double idleAtCapture = 0;
    };

    // いまの状態を取る（裏で。終わったらメインスレッドで done）。
    // shotPath が空でなければ、前のウィンドウの写真を JPEG で保存する（画面収録の許可があるときだけ）
    void capture (const std::string& shotPath, bool withText, const std::vector<std::string>& skipBundles,
                  std::function<void (Capture)> done);

    // 離れた / 戻った（画面ロック・スリープ・スクリーンセーバー）。kind: 1 = 離れた, 2 = 戻った
    void watchAwayEvents (std::function<void (int kind, const std::string& why)> onEvent);
    // キーボードやマウスを最後にさわってからの秒数
    double idleSeconds();

    // 開き直す: 書類はそのアプリで、ページはそのブラウザで（もう開いていればそのタブへ）、最後に front を前へ
    void reopen (const std::vector<AppInfo>& apps, const std::vector<TabInfo>& tabs, const std::string& finderPath,
                 const std::string& frontBundle);
    void activateApp (const std::string& bundleId);
    void openFile (const std::string& path);

    // 許可
    bool accessibilityTrusted (bool prompt);
    bool screenTrusted();
    void requestScreen();

    // どのアプリが前でも効くキー（Carbon。許可はいらない）。slot 0〜3
    struct HotKeyChoice { const char* label; int keyCode; int mods; };
    const std::vector<HotKeyChoice>& hotKeyChoices();
    bool registerHotKey (int slot, int choice, std::function<void()> onPress);

    // メニューバー
    void setStatusIcon (bool paused);
    void setStatusMenu (const std::vector<std::pair<std::string, int>>& items, std::function<void (int)> onPick);

    // ウィンドウを、全画面のアプリの上にも出して前へ（focus = キーボードもこちらへ）
    void showWindow (void* nsView, bool focus);

    bool launchAtLogin();
    bool setLaunchAtLogin (bool on);
}
