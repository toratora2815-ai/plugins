#pragma once
#include <functional>
#include <memory>

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
        bool learning = false;

        bool start();            // 許可がないと false
        bool isRunning() const;

        static bool hasPermission();
        static void requestPermission();   // 「アクセシビリティ」の許可ダイアログを出す
        static void openPermissionSettings();
        static bool isLogicFrontmost();
        static bool isLogicRunning();
        static void sendKey (int keyCode, int mods);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
