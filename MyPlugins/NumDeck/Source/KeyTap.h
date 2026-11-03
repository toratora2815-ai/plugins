#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// テンキーを横取りして、代わりに Logic のショートカットを送る仕組み（Mac では CGEventTap）。
// コールバックはすべてメインスレッド（JUCE のメッセージスレッド）で呼ばれる。
namespace numdeck
{
    enum PadKey
    {
        Pad0 = 0, Pad1, Pad2, Pad3, Pad4, Pad5, Pad6, Pad7, Pad8, Pad9,
        PadDot, PadPlus, PadMinus, PadMultiply, PadDivide, PadEnter, PadEquals, PadClear
    };

    // ショートカットの修飾キー
    enum Mod { kCmd = 1, kShift = 2, kOpt = 4, kCtrl = 8 };

    class KeyTap
    {
    public:
        KeyTap();
        ~KeyTap();

        // テンキー（修飾キーなし）が押された／離された。true を返すとそのキーは握りつぶす（他のアプリに届かない）
        std::function<bool (int padKey, bool down, bool repeat)> onPadKey;
        // learning 中だけ呼ばれる。テンキー以外のキーが押された（keyCode は Mac の仮想キーコード）
        std::function<bool (int keyCode, int mods)> onLearnKey;
        // 修飾キーなしで押された全部のキー（テンキー以外も）。true = 握りつぶす。
        // Home などの特殊キー（音量・メディア系）は keyCode = kSpecialKeyBase + 番号
        std::function<bool (int keyCode, bool down, bool repeat)> onAnyKey;
        static constexpr int kSpecialKeyBase = 1000;
        bool learnAnyKey = false;  // 覚えさせるときにテンキーも受け付ける
        bool learning = false;

        bool start();            // 許可がないと false
        bool isRunning() const;

        static bool hasPermission();
        static void requestPermission();   // 「アクセシビリティ」の許可ダイアログを出す
        static void resetPermission();     // 古い許可（前の版の分）を消す。許可されていないときだけ使う
        static void openPermissionSettings();
        static bool isLogicFrontmost();
        static bool isLogicRunning();
        static void rememberFrontApp();    // 自分以外の前面アプリを覚えておく
        static void activateLastApp();     // 覚えておいたアプリを前面に戻す（Num Deck をクリックした後）
        static void sendKey (int keyCode, int mods);
        // Logic に送る。Logic が後ろにいるときは一瞬だけ前に出して送り、元のアプリに戻す
        // （Logic は後ろにいる間のキーを受け付けないため）
        static void sendKeyToLogic (int keyCode, int mods);
        static void sendRemote (const std::string& app, const std::string& command); // 自作アプリへの命令（分散通知）
        static bool isRefMonitorFrontmost();

        // 業務モード用: アプリやページを開く
        // アプリを開く。そのアプリがもう前面なら隠して、開く前のアプリに戻す（全画面でも同じ）
        static void openApp (const std::string& path);
        // 開いているアプリを全部終了する（Finder と Num Deck 以外。保存していない書類はアプリが聞いてくる）
        static void quitAllApps();
        // 前のウィンドウ: 左右分割の片側なら単独の全画面に、（さっき分割から出した）全画面なら元の分割に、普通なら全画面に
        static void toggleFullScreenOrSplit();
        // Chrome があれば Chrome で。profileDir（例: "Profile 2"）を指定すると、そのアカウントで開く
        static void openURL (const std::string& url, const std::string& profileDir = {});
        // 2 画面で開く: ページを Chrome の新しいウィンドウで左半分に、右半分に split
        // （"chrome" = Chrome の新しいウィンドウ、それ以外 = そのアプリ）を並べる。マウスのある画面を使う
        static void openURLSplit (const std::string& url, const std::string& profileDir, const std::string& split);
        // Chrome のアカウント一覧（フォルダ名, 表示名）。初回は macOS が「ほかのアプリのデータ」へのアクセスを聞く
        static std::vector<std::pair<std::string, std::string>> chromeProfiles();
        static bool chromeFrontPage (std::string& url, std::string& title); // Chrome の前面のタブ
        static std::string appDisplayName (const std::string& path);
        static std::vector<unsigned char> appIconPNG (const std::string& path, int size);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
