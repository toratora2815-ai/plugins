#pragma once
#include <functional>

namespace mac
{
    // マイクの許可（初回は macOS の確認が出る。画面は止めない。結果はメインスレッドで）
    void requestMic (std::function<void (bool)> done);
}
