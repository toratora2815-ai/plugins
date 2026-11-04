#pragma once
// Mac だけで使う、ウィンドウを動かす部分（Windows_mac.mm）の入口。KeyTap_mac.mm から使う
#import <AppKit/AppKit.h>
#include <string>

namespace numdeck::win
{
    inline NSString* ns (const std::string& s) { return [NSString stringWithUTF8String: s.c_str()]; }

    // アプリのウィンドウが全画面の左右分割に入っていれば、分割を解いてそのアプリだけの全画面に。した場合 true
    bool unsplitToFullScreen (NSString* path);
    // アプリの前面のウィンドウが全画面なら、全画面を解いてから隠して backURL のアプリへ。した場合 true
    bool leaveFullScreenThenHide (NSRunningApplication* app, NSURL* backURL);
}
