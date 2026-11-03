#pragma once
#include <functional>
#include <memory>
#include <string>

// ほかのアプリ（Num Deck など）からの操作を受け取る。Mac の分散通知
// "com.myplugins.referencemonitor.command" の object に命令の文字列（"ab" など）が入ってくる。
// コールバックはメインスレッドで呼ばれる。
class RemoteCommands
{
public:
    explicit RemoteCommands (std::function<void (const std::string&)> onCommand);
    ~RemoteCommands();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
