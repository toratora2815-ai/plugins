#pragma once
// Mac だけの部分（Objective-C++ は MacSupport_mac.mm）
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace mac
{
    // マイクの許可（初回は macOS の確認が出る。画面は止めない）
    void requestMic (std::function<void (bool)> done);

    // メニューバーのアイコン。state: 0 = 待機, 1 = 録音中, 2 = 解析中
    void setStatusIcon (int state);
    // メニューの中身（tag 0 = 区切り線）。選ばれたら onPick(tag)
    void setStatusMenu (const std::vector<std::pair<std::string, int>>& items, std::function<void (int)> onPick);

    // どのアプリが前でも効くキー（Carbon のホットキー。アクセシビリティの許可はいらない）
    struct HotKeyChoice { const char* label; int keyCode; int mods; };
    const std::vector<HotKeyChoice>& hotKeyChoices();
    bool registerHotKey (int choice, std::function<void()> onPress);

    // ほかのアプリから: 分散通知 com.myplugins.ideacatcher.command、object = "toggle" / "show" / "open"
    void listenCommands (std::function<void (const std::string&)> onCommand);

    // Logic Pro でファイルを開く（Logic がなければふつうに開く）
    bool openInLogic (const std::string& path);
    bool logicInstalled();

    // ログイン時に起動（macOS 13 以降。それより前は false）
    bool launchAtLogin();
    bool setLaunchAtLogin (bool on);

    // ウィンドウを、全画面のアプリの上にも出せるようにして前へ（focus = キーボードもこちらへ）
    void showWindow (void* nsView, bool floating, bool focus);
}
