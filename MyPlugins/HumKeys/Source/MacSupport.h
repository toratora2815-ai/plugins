#pragma once
#include <functional>
#include <memory>
#include <string>

// Mac だけの機能（Objective-C++ 側は MacSupport_mac.mm）
namespace macsupport
{
    // マイクの許可を、画面を止めずに確かめる（初回は macOS の確認が出る）。結果はメインスレッドで返る
    void requestMicPermission (std::function<void (bool)> done);

    // 「いつも手前」：ほかのアプリの全画面（Logic の全画面など）の上にも出す。
    // そのあいだは補助アプリ（Dock とメニューバーに出ない）になる（ふつうのアプリのままだと全画面の上に出られない）
    void setFloatOnTop (bool on);

    // アクセシビリティ（ほかのアプリを操作する）の許可。prompt = true なら macOS の確認を出す
    bool accessibilityTrusted (bool prompt);

    // MIDI ファイルを、いま開いている Logic のプロジェクトへ読み込む（ファイル → 読み込む → MIDI ファイル）。
    // Logic は再生ヘッドの位置（小節の頭に丸める）に置く。bar > 0 なら先に「位置へ移動」（/ キー）でその小節へ。
    // 結果はメインスレッドで返る（ok, 理由: "accessibility" / "nologic" / "notfront" / "nomenu" / "nopanel" / "stillopen"、
    // movedTo = 小節へ動かせたか）
    void insertMidiIntoLogic (const std::string& midiPath, int bar, std::function<void (bool ok, const std::string& why, bool moved)> done);
}

// Num Deck などからの操作を受け取る（分散通知 "com.myplugins.humkeys.command"、object に命令の文字列）
class RemoteCommands
{
public:
    explicit RemoteCommands (std::function<void (const std::string&)> onCommand);
    ~RemoteCommands();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
