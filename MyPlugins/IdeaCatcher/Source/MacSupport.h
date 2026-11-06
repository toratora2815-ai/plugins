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
    // 録音中だけのキー（slot 1 = テンポをたたく）。keyCode < 0 で外す
    bool registerExtraHotKey (int slot, int keyCode, int mods, std::function<void()> onPress);
    constexpr int kSpaceKey = 49;

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

    // HUD 用: マウスを通す・どのデスクトップでも・全画面の上にも出る小さな窓にする
    void makeOverlay (void* nsView);
    // メニューバーの下の真ん中（x, y はスクリーン座標・左上原点）
    void hudPosition (int width, int& x, int& y);

    // Mac の通知。クリックされたら onClick(id)
    void notify (const std::string& title, const std::string& body, const std::string& id);
    void setNotificationHandler (std::function<void (const std::string&)> onClick);

    // Mac 本体のマイクの名前（なければ空。Mac mini など）。Bluetooth のマイクを開くと音質が落ちるので、こちらを使う
    std::string builtInMicName();

    // アクセシビリティの許可（prompt = 許可の画面を出す）
    bool accessibilityTrusted (bool prompt);

    // Logic で新しく開いたプロジェクト（windowsBefore にない窓）に、音声ファイルを 1 小節目から読み込む。
    // 「ファイル → 読み込む → オーディオファイル」を押して、場所を入力する。終わったら（メインスレッドで）done(ok, 理由)
    std::vector<std::string> logicWindowTitles();
    // projectName: 開いた MIDI のファイル名（拡張子なし）。窓の名前にこれか「名称未設定 / Untitled」が入っているものだけを相手にする
    void importAudioIntoLogic (const std::string& audioPath, const std::string& projectName, const std::vector<std::string>& windowsBefore,
                               std::function<void (bool, const std::string&)> done);
}
