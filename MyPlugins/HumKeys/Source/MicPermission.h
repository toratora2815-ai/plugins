#pragma once
#include <functional>
// 結果はメインスレッドで返る
void requestMicPermission (std::function<void (bool)> done);
