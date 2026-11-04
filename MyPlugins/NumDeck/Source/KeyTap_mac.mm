#include "KeyTap.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <cmath>

namespace numdeck
{
    namespace
    {
        // 自分が送ったキーの目印（自分のタップで拾い直さないため）
        constexpr int64_t kMagic = 0x4E554D44; // "NUMD"

        int padFromKeyCode (int vk)
        {
            switch (vk)
            {
                case 82: return Pad0;  case 83: return Pad1;  case 84: return Pad2;
                case 85: return Pad3;  case 86: return Pad4;  case 87: return Pad5;
                case 88: return Pad6;  case 89: return Pad7;  case 91: return Pad8;
                case 92: return Pad9;  case 65: return PadDot; case 69: return PadPlus;
                case 78: return PadMinus; case 67: return PadMultiply; case 75: return PadDivide;
                case 76: return PadEnter; case 81: return PadEquals; case 71: return PadClear;
                default: return -1;
            }
        }

        int modsFromFlags (CGEventFlags f)
        {
            int m = 0;
            if (f & kCGEventFlagMaskCommand)   m |= kCmd;
            if (f & kCGEventFlagMaskShift)     m |= kShift;
            if (f & kCGEventFlagMaskAlternate) m |= kOpt;
            if (f & kCGEventFlagMaskControl)   m |= kCtrl;
            return m;
        }

        CGEventFlags flagsFromMods (int m)
        {
            CGEventFlags f = 0;
            if (m & kCmd)   f |= kCGEventFlagMaskCommand;
            if (m & kShift) f |= kCGEventFlagMaskShift;
            if (m & kOpt)   f |= kCGEventFlagMaskAlternate;
            if (m & kCtrl)  f |= kCGEventFlagMaskControl;
            return f;
        }
    }

    struct KeyTap::Impl
    {
        KeyTap& owner;
        CFMachPortRef tap = nullptr;
        CFRunLoopSourceRef source = nullptr;

        explicit Impl (KeyTap& o) : owner (o) {}

        ~Impl()
        {
            if (source != nullptr) { CFRunLoopRemoveSource (CFRunLoopGetMain(), source, kCFRunLoopCommonModes); CFRelease (source); }
            if (tap != nullptr)    { CGEventTapEnable (tap, false); CFMachPortInvalidate (tap); CFRelease (tap); }
        }

        static CGEventRef callback (CGEventTapProxy, CGEventType type, CGEventRef e, void* ctx)
        {
            auto& self = *static_cast<Impl*> (ctx);

            // 処理が遅いと macOS にタップを止められるので、すぐ付け直す
            if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput)
            {
                if (self.tap != nullptr) CGEventTapEnable (self.tap, true);
                return e;
            }
            // メディア系・特殊キー（NX_SYSDEFINED）。キーボードによっては Home などがこちらで来る
            if (type == (CGEventType) 14)
            {
                if (! self.owner.learning && ! self.owner.onAnyKey) return e;
                bool swallow = false;
                @autoreleasepool
                {
                    NSEvent* ne = [NSEvent eventWithCGEvent: e];
                    if (ne == nil || ne.subtype != 8) return e;
                    const int code = KeyTap::kSpecialKeyBase + (int) ((ne.data1 & 0xFFFF0000) >> 16);
                    const bool sdown = ((ne.data1 & 0xFF00) >> 8) == 0xA;
                    const bool srepeat = (ne.data1 & 0x1) != 0;
                    if (self.owner.learning && self.owner.learnAnyKey)
                        swallow = ! sdown || (self.owner.onLearnKey && self.owner.onLearnKey (code, 0));
                    else if (self.owner.onAnyKey)
                        swallow = self.owner.onAnyKey (code, sdown, srepeat);
                }
                return swallow ? nullptr : e;
            }

            if (type != kCGEventKeyDown && type != kCGEventKeyUp) return e;
            if (CGEventGetIntegerValueField (e, kCGEventSourceUserData) == kMagic) return e;

            const int vk   = (int) CGEventGetIntegerValueField (e, kCGKeyboardEventKeycode);
            const int mods = modsFromFlags (CGEventGetFlags (e));
            const bool down = type == kCGEventKeyDown;
            const int pad  = padFromKeyCode (vk);

            if (self.owner.learning && (pad < 0 || self.owner.learnAnyKey))
            {
                if (down && self.owner.onLearnKey && self.owner.onLearnKey (vk, mods)) return nullptr;
                return down ? e : nullptr; // 覚えさせたキーを離した分も握りつぶす
            }

            const bool repeat = CGEventGetIntegerValueField (e, kCGKeyboardEventAutorepeat) != 0;
            if (mods == 0 && self.owner.onAnyKey && self.owner.onAnyKey (vk, down, repeat)) return nullptr;

            // 修飾キーと一緒のテンキーはそのまま通す（⌘+テンキーなど、他の使い道を残す）
            if (pad < 0 || mods != 0 || ! self.owner.onPadKey) return e;

            return self.owner.onPadKey (pad, down, repeat) ? nullptr : e;
        }

        bool start()
        {
            if (tap != nullptr) return true;
            const CGEventMask mask = CGEventMaskBit (kCGEventKeyDown) | CGEventMaskBit (kCGEventKeyUp) | CGEventMaskBit (14);
            tap = CGEventTapCreate (kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                                    mask, &Impl::callback, this);
            if (tap == nullptr) return false;
            source = CFMachPortCreateRunLoopSource (kCFAllocatorDefault, tap, 0);
            CFRunLoopAddSource (CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
            CGEventTapEnable (tap, true);
            return true;
        }
    };

    KeyTap::KeyTap() : impl (std::make_unique<Impl> (*this)) {}
    KeyTap::~KeyTap() = default;

    bool KeyTap::start()            { return impl->start(); }
    bool KeyTap::isRunning() const  { return impl->tap != nullptr && CGEventTapIsEnabled (impl->tap); }

    bool KeyTap::hasPermission() { return AXIsProcessTrusted(); }

    void KeyTap::requestPermission()
    {
        NSDictionary* opts = @{ (__bridge NSString*) kAXTrustedCheckOptionPrompt: @YES };
        AXIsProcessTrustedWithOptions ((__bridge CFDictionaryRef) opts);
    }

    void KeyTap::resetPermission()
    {
        // 一覧で ON に見えても、前の版のアプリの分で効いていないことがある。いったん消して出し直す
        NSString* bundleId = [[NSBundle mainBundle] bundleIdentifier];
        if (bundleId == nil) return;
        NSTask* task = [NSTask launchedTaskWithLaunchPath: @"/usr/bin/tccutil"
                                                arguments: @[ @"reset", @"Accessibility", bundleId ]];
        [task waitUntilExit];
    }

    void KeyTap::openPermissionSettings()
    {
        [[NSWorkspace sharedWorkspace] openURL:
            [NSURL URLWithString: @"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
    }

    static bool isLogic (NSRunningApplication* app)
    {
        return app != nil && app.bundleIdentifier != nil && [app.bundleIdentifier hasPrefix: @"com.apple.logic"];
    }

    bool KeyTap::isLogicFrontmost() { return isLogic ([[NSWorkspace sharedWorkspace] frontmostApplication]); }

    bool KeyTap::isLogicRunning()
    {
        for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications])
            if (isLogic (app)) return true;
        return false;
    }

    bool KeyTap::isRefMonitorFrontmost()
    {
        NSRunningApplication* app = [[NSWorkspace sharedWorkspace] frontmostApplication];
        return app != nil && [app.bundleIdentifier isEqualToString: @"com.myplugins.referencemonitor"];
    }

    void KeyTap::sendRemote (const std::string& app, const std::string& command)
    {
        // 受け取る側（Reference Monitor など）が後ろにいても届くよう、すぐ配達する
        NSString* name = [NSString stringWithFormat: @"com.myplugins.%s.command", app.c_str()];
        [[NSDistributedNotificationCenter defaultCenter] postNotificationName: name
                                                                       object: [NSString stringWithUTF8String: command.c_str()]
                                                                     userInfo: nil
                                                           deliverImmediately: YES];
    }

    static pid_t lastFrontPid = 0;

    void KeyTap::rememberFrontApp()
    {
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (front != nil && front.processIdentifier != [NSRunningApplication currentApplication].processIdentifier)
            lastFrontPid = front.processIdentifier;
    }

    void KeyTap::activateLastApp()
    {
        if (lastFrontPid == 0) return;
        if (NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier: lastFrontPid])
            [app activateWithOptions: 0];
    }

    void KeyTap::sendKey (int keyCode, int mods)
    {
        // 実際に押されている修飾キーが混ざらないよう、独立したイベントソースから送る
        CGEventSourceRef src = CGEventSourceCreate (kCGEventSourceStatePrivate);
        for (bool down : { true, false })
        {
            CGEventRef ev = CGEventCreateKeyboardEvent (src, (CGKeyCode) keyCode, down);
            if (ev == nullptr) continue;
            CGEventSetFlags (ev, flagsFromMods (mods));
            CGEventSetIntegerValueField (ev, kCGEventSourceUserData, kMagic);
            CGEventPost (kCGHIDEventTap, ev);
            CFRelease (ev);
        }
        if (src != nullptr) CFRelease (src);
    }
    namespace
    {
        // 何回目の送信か。最後の送信から少し経ってから元のアプリに戻す（連打しても行き来しない）
        int sendGeneration = 0;

        void sendWhenLogicFront (pid_t logicPid, pid_t backPid, int keyCode, int mods, int tries)
        {
            NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
            if ((front == nil || front.processIdentifier != logicPid) && tries < 40)
            {
                dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 10 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    sendWhenLogicFront (logicPid, backPid, keyCode, mods, tries + 1);
                });
                return;
            }
            KeyTap::sendKey (keyCode, mods);

            const int gen = ++sendGeneration;
            if (backPid == 0) return;
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 150 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                if (gen != sendGeneration) return;
                if (NSRunningApplication* back = [NSRunningApplication runningApplicationWithProcessIdentifier: backPid])
                    [back activateWithOptions: 0];
            });
        }
    }

    void KeyTap::sendKeyToLogic (int keyCode, int mods)
    {
        NSRunningApplication* logic = nil;
        for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications])
            if (isLogic (app)) { logic = app; break; }

        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (logic == nil || (front != nil && front.processIdentifier == logic.processIdentifier))
        {
            sendKey (keyCode, mods);
            return;
        }

        // 前にいたアプリ（Reference Monitor など）。Num Deck 自身なら戻さない
        pid_t backPid = 0;
        if (front != nil && front.processIdentifier != [NSRunningApplication currentApplication].processIdentifier)
            backPid = front.processIdentifier;

        [logic activateWithOptions: 0];
        sendWhenLogicFront (logic.processIdentifier, backPid, keyCode, mods, 0);
    }

    // マクロ用（Logic を前に出したまま何回も送り、最後に 1 回だけ戻す）
    static pid_t macroBackPid = 0;

    bool KeyTap::bringLogicFront()
    {
        macroBackPid = 0;
        NSRunningApplication* logic = nil;
        for (NSRunningApplication* app in [[NSWorkspace sharedWorkspace] runningApplications])
            if (isLogic (app)) { logic = app; break; }
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (logic == nil || (front != nil && front.processIdentifier == logic.processIdentifier)) return false;

        if (front != nil && front.processIdentifier != [NSRunningApplication currentApplication].processIdentifier)
            macroBackPid = front.processIdentifier;
        ++sendGeneration;   // 1 つずつ送ったときの「元に戻す」予約を取り消す
        [logic activateWithOptions: 0];
        return true;
    }

    void KeyTap::returnFromLogic()
    {
        const pid_t back = macroBackPid;
        macroBackPid = 0;
        if (back == 0) return;
        if (NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier: back])
            [a activateWithOptions: 0];
    }
}


namespace numdeck
{
    static NSString* ns (const std::string& s) { return [NSString stringWithUTF8String: s.c_str()]; }

    // アプリごとの「開く前に前面だったアプリ」（隠したときにそこへ戻る）
    static NSMutableDictionary<NSString*, NSNumber*>* appBackPids = nil;

    namespace
    {
        bool unsplitToFullScreen (NSString* path);  // 下で定義
        bool leaveFullScreenThenHide (NSRunningApplication* app, NSURL* backURL);  // 下で定義
    }

    void KeyTap::openApp (const std::string& path)
    {
        NSString* p = [ns (path) stringByStandardizingPath];
        // 全画面の左右分割に入っている → 分割を解いて、そのアプリだけを全画面に
        if (unsplitToFullScreen (p)) return;

        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (appBackPids == nil) appBackPids = [[NSMutableDictionary alloc] init];

        // もう前面にいる → 開く前のアプリに戻して、隠す。
        // 全画面のアプリは「隠す」が効かないので、戻り先のアプリを NSWorkspace で開き直してそちらの画面へ移る
        // （activateWithOptions は、後ろにいる Num Deck からだと macOS に断られることがある）
        if (front != nil && front.bundleURL != nil && [[front.bundleURL.path stringByStandardizingPath] isEqualToString: p])
        {
            NSURL* backURL = nil;
            if (NSNumber* back = appBackPids[p])
                if (NSRunningApplication* b = [NSRunningApplication runningApplicationWithProcessIdentifier: (pid_t) back.intValue])
                    if (! b.terminated && b.processIdentifier != front.processIdentifier)
                        backURL = b.bundleURL;
            if (backURL == nil)
                backURL = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: @"com.apple.finder"];
            // 全画面（1 枚だけ）なら、全画面を解いてから隠す
            if (leaveFullScreenThenHide (front, backURL)) return;
            if (backURL != nil)
                [[NSWorkspace sharedWorkspace] openApplicationAtURL: backURL
                                                      configuration: [NSWorkspaceOpenConfiguration configuration]
                                                  completionHandler: nil];
            [front hide];
            return;
        }

        if (front != nil && front.processIdentifier != [NSRunningApplication currentApplication].processIdentifier)
            appBackPids[p] = @(front.processIdentifier);

        NSURL* u = [NSURL fileURLWithPath: ns (path)];
        [[NSWorkspace sharedWorkspace] openApplicationAtURL: u
                                              configuration: [NSWorkspaceOpenConfiguration configuration]
                                          completionHandler: nil];
    }

    void KeyTap::quitAllApps()
    {
        const pid_t self = [NSRunningApplication currentApplication].processIdentifier;
        for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
        {
            if (a.activationPolicy != NSApplicationActivationPolicyRegular || a.processIdentifier == self) continue;
            if ([a.bundleIdentifier isEqualToString: @"com.apple.finder"]) continue;
            [a terminate];
        }
    }

    void KeyTap::openURL (const std::string& url, const std::string& profileDir)
    {
        NSString* s = ns (url);
        NSURL* u = [NSURL URLWithString: s];
        if (u == nil) // 日本語などが混ざったアドレス
            u = [NSURL URLWithString: [s stringByAddingPercentEncodingWithAllowedCharacters: [NSCharacterSet URLFragmentAllowedCharacterSet]]];
        if (u == nil) return;

        NSURL* chrome = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: @"com.google.Chrome"];
        if (chrome != nil && ! profileDir.empty())
        {
            // 「open -na Google Chrome --args --profile-directory=…」と同じ。
            // Chrome が起動中なら、新しい方は起動中の Chrome にページを渡してすぐ終わる
            NSWorkspaceOpenConfiguration* c = [NSWorkspaceOpenConfiguration configuration];
            c.createsNewApplicationInstance = YES;
            c.arguments = @[ [@"--profile-directory=" stringByAppendingString: ns (profileDir)], u.absoluteString ];
            [[NSWorkspace sharedWorkspace] openApplicationAtURL: chrome configuration: c completionHandler: nil];
        }
        else if (chrome != nil)
            [[NSWorkspace sharedWorkspace] openURLs: @[ u ] withApplicationAtURL: chrome
                                      configuration: [NSWorkspaceOpenConfiguration configuration]
                                  completionHandler: nil];
        else
            [[NSWorkspace sharedWorkspace] openURL: u];
    }

    namespace
    {
        // マウスのある画面の、メニューバーと Dock を除いた範囲（アクセシビリティの座標＝いちばん左上が原点）
        CGRect visibleAreaForAX()
        {
            const NSPoint m = [NSEvent mouseLocation];
            NSScreen* scr = [NSScreen mainScreen];
            for (NSScreen* s in [NSScreen screens])
                if (NSPointInRect (m, s.frame)) { scr = s; break; }
            const NSRect v = scr.visibleFrame;
            const CGFloat primaryH = [NSScreen screens].firstObject.frame.size.height;
            return CGRectMake (v.origin.x, primaryH - (v.origin.y + v.size.height), v.size.width, v.size.height);
        }

        void placeWindow (AXUIElementRef win, CGRect r)
        {
            CGPoint p = r.origin;
            CGSize s = r.size;
            AXValueRef pv = AXValueCreate ((AXValueType) kAXValueCGPointType, &p);
            AXValueRef sv = AXValueCreate ((AXValueType) kAXValueCGSizeType, &s);
            AXUIElementSetAttributeValue (win, kAXPositionAttribute, pv);
            AXUIElementSetAttributeValue (win, kAXSizeAttribute, sv);
            AXUIElementSetAttributeValue (win, kAXPositionAttribute, pv); // 大きさを変えるとずれるアプリがあるので、もう一度
            CFRelease (pv);
            CFRelease (sv);
        }

        pid_t runningPid (NSString* bundleId)
        {
            for (NSRunningApplication* a in [NSRunningApplication runningApplicationsWithBundleIdentifier: bundleId])
                if (! a.terminated) return a.processIdentifier;
            return 0;
        }

        // そのアプリの前面のウィンドウ（呼んだ側で CFRelease）
        AXUIElementRef copyFrontWindow (NSString* bundleId)
        {
            const pid_t pid = runningPid (bundleId);
            if (pid == 0) return nullptr;
            AXUIElementRef app = AXUIElementCreateApplication (pid);
            CFTypeRef w = nullptr;
            AXUIElementCopyAttributeValue (app, kAXFocusedWindowAttribute, &w);
            if (w == nullptr) AXUIElementCopyAttributeValue (app, kAXMainWindowAttribute, &w);
            CFRelease (app);
            return (AXUIElementRef) w;
        }

        bool axBool (AXUIElementRef e, CFStringRef attr)
        {
            CFTypeRef v = nullptr;
            AXUIElementCopyAttributeValue (e, attr, &v);
            const bool b = v != nullptr && CFGetTypeID (v) == CFBooleanGetTypeID() && CFBooleanGetValue ((CFBooleanRef) v);
            if (v != nullptr) CFRelease (v);
            return b;
        }

        // 全画面（ほかと左右分割中も含む）なら解除し、しまってあれば出す。解除したら true（落ち着くまで少し待つ）
        bool bringOutOfFullScreen (AXUIElementRef w)
        {
            if (axBool (w, kAXMinimizedAttribute))
                AXUIElementSetAttributeValue (w, kAXMinimizedAttribute, kCFBooleanFalse);
            if (! axBool (w, CFSTR ("AXFullScreen"))) return false;
            AXUIElementSetAttributeValue (w, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            return true;
        }

        // before と違うウィンドウが前に出るまで待って（アプリの起動を待つこともあるので最大 20 秒）、
        // 全画面なら解除してから r に置き、then（置いたウィンドウ、なければ nullptr）。before は最後に CFRelease する
        void waitAndPlace (NSString* bundleId, AXUIElementRef before, CGRect r, int tries, void (^then) (AXUIElementRef))
        {
            AXUIElementRef w = copyFrontWindow (bundleId);
            const bool ready = w != nullptr && (before == nullptr || ! CFEqual (w, before));
            if (ready && tries < 190 && bringOutOfFullScreen (w))
            {
                // 全画面を抜けるアニメーションが終わってから置き直す（同じウィンドウなので before は外す）
                CFRelease (w);
                if (before != nullptr) CFRelease (before);
                dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 1300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    waitAndPlace (bundleId, nullptr, r, tries + 1, then);
                });
                return;
            }
            if (! ready && tries < 200)
            {
                if (w != nullptr) CFRelease (w);
                dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    waitAndPlace (bundleId, before, r, tries + 1, then);
                });
                return;
            }
            if (ready) placeWindow (w, r);
            if (before != nullptr) CFRelease (before);
            if (then != nil) then (ready ? w : nullptr);
            if (w != nullptr) CFRelease (w);
        }

        CFStringRef axTitle (AXUIElementRef e)
        {
            CFTypeRef t = nullptr;
            AXUIElementCopyAttributeValue (e, kAXTitleAttribute, &t);
            return (CFStringRef) t;
        }

        // メニューの中から、titles のどれかの名前の項目（呼んだ側で CFRelease）
        AXUIElementRef copyChildItem (AXUIElementRef parent, NSArray<NSString*>* titles)
        {
            CFTypeRef kids = nullptr;
            AXUIElementCopyAttributeValue (parent, kAXChildrenAttribute, &kids);
            if (kids == nullptr) return nullptr;
            AXUIElementRef found = nullptr;
            for (id k in (__bridge NSArray*) kids)
            {
                AXUIElementRef e = (__bridge AXUIElementRef) k;
                CFStringRef t = axTitle (e);
                const bool hit = t != nullptr && [titles containsObject: (__bridge NSString*) t];
                if (t != nullptr) CFRelease (t);
                if (hit) { found = (AXUIElementRef) CFRetain (e); break; }
                // メニューバーの項目・サブメニューは、子の AXMenu を 1 段下りて探す
                CFStringRef role = nullptr;
                AXUIElementCopyAttributeValue (e, kAXRoleAttribute, (CFTypeRef*) &role);
                const bool isMenu = role != nullptr && CFEqual (role, kAXMenuRole);
                if (role != nullptr) CFRelease (role);
                if (isMenu && (found = copyChildItem (e, titles)) != nullptr) break;
            }
            CFRelease (kids);
            return found;
        }

        // path のアプリの前面のウィンドウが、全画面の左右分割（全画面なのに画面の幅より狭い）に入っていれば、
        // 分割を解いてそのウィンドウだけの全画面にする。した場合 true
        bool unsplitToFullScreen (NSString* path)
        {
            NSRunningApplication* app = nil;
            for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
                if (a.bundleURL != nil && [[a.bundleURL.path stringByStandardizingPath] isEqualToString: path]) { app = a; break; }
            if (app == nil || app.bundleIdentifier == nil) return false;
            AXUIElementRef win = copyFrontWindow (app.bundleIdentifier);
            if (win == nullptr) return false;
            if (! axBool (win, CFSTR ("AXFullScreen"))) { CFRelease (win); return false; }

            CGPoint pos {}; CGSize size {};
            CFTypeRef pv = nullptr, sv = nullptr;
            AXUIElementCopyAttributeValue (win, kAXPositionAttribute, &pv);
            AXUIElementCopyAttributeValue (win, kAXSizeAttribute, &sv);
            if (pv != nullptr) { AXValueGetValue ((AXValueRef) pv, (AXValueType) kAXValueCGPointType, &pos); CFRelease (pv); }
            if (sv != nullptr) { AXValueGetValue ((AXValueRef) sv, (AXValueType) kAXValueCGSizeType, &size); CFRelease (sv); }

            // ウィンドウがある画面の幅と比べる（画面の座標は左下が原点なので変換）
            const CGFloat primaryH = [NSScreen screens].firstObject.frame.size.height;
            const NSPoint probe = NSMakePoint (pos.x + 2, primaryH - pos.y - 2);
            CGFloat screenW = 0;
            for (NSScreen* s in [NSScreen screens])
                if (NSPointInRect (probe, s.frame)) screenW = s.frame.size.width;
            if (screenW <= 0 || size.width > screenW * 0.9) { CFRelease (win); return false; }

            [[NSWorkspace sharedWorkspace] openApplicationAtURL: app.bundleURL
                                                  configuration: [NSWorkspaceOpenConfiguration configuration]
                                              completionHandler: nil];
            AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 1400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanTrue);
                CFRelease (win);
            });
            return true;
        }

        // app の前面のウィンドウが全画面なら、全画面を解いて、落ち着いてから隠して backURL のアプリへ。した場合 true
        bool leaveFullScreenThenHide (NSRunningApplication* app, NSURL* backURL)
        {
            if (app.bundleIdentifier == nil) return false;
            AXUIElementRef win = copyFrontWindow (app.bundleIdentifier);
            if (win == nullptr) return false;
            const bool fs = axBool (win, CFSTR ("AXFullScreen"));
            if (fs) AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            CFRelease (win);
            if (! fs) return false;
            const pid_t pid = app.processIdentifier;
            NSURL* back = [backURL copy];
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 1300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                if (back != nil)
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL: back
                                                          configuration: [NSWorkspaceOpenConfiguration configuration]
                                                      completionHandler: nil];
                if (NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
                    [a hide];
            });
            return true;
        }

        // アプリのメニュー「ウィンドウ」→「フルスクリーンのタイル表示」→「画面左側」を押す
        bool pressTileLeft (pid_t pid)
        {
            AXUIElementRef app = AXUIElementCreateApplication (pid);
            CFTypeRef bar = nullptr;
            AXUIElementCopyAttributeValue (app, kAXMenuBarAttribute, &bar);
            CFRelease (app);
            if (bar == nullptr) return false;
            bool ok = false;
            if (AXUIElementRef win = copyChildItem ((AXUIElementRef) bar, @[ @"ウィンドウ", @"ウインドウ", @"Window" ]))
            {
                if (AXUIElementRef tile = copyChildItem (win, @[ @"フルスクリーンのタイル表示", @"Full Screen Tile" ]))
                {
                    if (AXUIElementRef left = copyChildItem (tile, @[ @"画面左側", @"Left of Screen" ]))
                    {
                        ok = AXUIElementPerformAction (left, kAXPressAction) == kAXErrorSuccess;
                        CFRelease (left);
                    }
                    CFRelease (tile);
                }
                CFRelease (win);
            }
            CFRelease (bar);
            return ok;
        }

        void clickAt (CGPoint p)
        {
            CGEventRef cur = CGEventCreate (nullptr);
            const CGPoint back = CGEventGetLocation (cur);
            CFRelease (cur);
            for (CGEventType t : { kCGEventMouseMoved, kCGEventLeftMouseDown, kCGEventLeftMouseUp })
            {
                CGEventRef e = CGEventCreateMouseEvent (nullptr, t, p, kCGMouseButtonLeft);
                CGEventPost (kCGHIDEventTap, e);
                CFRelease (e);
                usleep (60000);
            }
            CGEventRef mv = CGEventCreateMouseEvent (nullptr, kCGEventMouseMoved, back, kCGMouseButtonLeft);
            CGEventPost (kCGHIDEventTap, mv);
            CFRelease (mv);
        }

        // 左右に並べた 2 つを、macOS の全画面の左右分割にする。
        // 左のウィンドウを「画面左側」にすると、右に置く相手を選ぶ画面が出る。
        // そこでは各ウィンドウが元の位置を縮めた所に並ぶので、右半分に置いた相手の所（右半分の真ん中）をクリックする
        void makeFullScreenSplit (pid_t leftPid, AXUIElementRef leftWin, CGRect area)
        {
            if (NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier: leftPid])
                if (a.bundleURL != nil)
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL: a.bundleURL
                                                          configuration: [NSWorkspaceOpenConfiguration configuration]
                                                      completionHandler: nil];
            AXUIElementRef win = (AXUIElementRef) CFRetain (leftWin);
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                AXUIElementPerformAction (win, kAXRaiseAction);
                CFRelease (win);
                if (! pressTileLeft (leftPid)) return;
                const CGPoint target = CGPointMake (area.origin.x + area.size.width * 0.875, area.origin.y + area.size.height * 0.5);
                dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 1600 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    clickAt (target);
                });
            });
        }

        // Chrome を引数付きで開く（起動中なら、起動中の Chrome に渡される）
        void launchChrome (const std::string& profileDir, NSArray<NSString*>* args)
        {
            NSURL* chrome = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: @"com.google.Chrome"];
            if (chrome == nil) return;
            NSMutableArray<NSString*>* a = [NSMutableArray array];
            if (! profileDir.empty()) [a addObject: [@"--profile-directory=" stringByAppendingString: ns (profileDir)]];
            [a addObjectsFromArray: args];
            NSWorkspaceOpenConfiguration* c = [NSWorkspaceOpenConfiguration configuration];
            c.createsNewApplicationInstance = YES;
            c.arguments = a;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL: chrome configuration: c completionHandler: nil];
        }
    }

    namespace
    {
        void getFrame (AXUIElementRef w, CGPoint& pos, CGSize& size)
        {
            CFTypeRef pv = nullptr, sv = nullptr;
            AXUIElementCopyAttributeValue (w, kAXPositionAttribute, &pv);
            AXUIElementCopyAttributeValue (w, kAXSizeAttribute, &sv);
            if (pv != nullptr) { AXValueGetValue ((AXValueRef) pv, (AXValueType) kAXValueCGPointType, &pos); CFRelease (pv); }
            if (sv != nullptr) { AXValueGetValue ((AXValueRef) sv, (AXValueType) kAXValueCGSizeType, &size); CFRelease (sv); }
        }

        // pos がある画面の幅（アクセシビリティの座標から）
        CGFloat screenWidthAt (CGPoint pos)
        {
            const CGFloat primaryH = [NSScreen screens].firstObject.frame.size.height;
            const NSPoint probe = NSMakePoint (pos.x + 2, primaryH - pos.y - 2);
            for (NSScreen* s in [NSScreen screens])
                if (NSPointInRect (probe, s.frame)) return s.frame.size.width;
            return 0;
        }

        bool inSplit (AXUIElementRef w)
        {
            if (! axBool (w, CFSTR ("AXFullScreen"))) return false;
            CGPoint pos {}; CGSize size {};
            getFrame (w, pos, size);
            const CGFloat sw = screenWidthAt (pos);
            return sw > 0 && size.width < sw * 0.9;
        }

        // 半画面 → 全画面にしたときの、元の相手（全画面から戻すときに使う）
        AXUIElementRef lastSelf = nullptr, lastPartner = nullptr;
        pid_t lastSelfPid = 0, lastPartnerPid = 0;
        bool lastSelfWasLeft = true;

        void forgetPair()
        {
            if (lastSelf != nullptr) CFRelease (lastSelf);
            if (lastPartner != nullptr) CFRelease (lastPartner);
            lastSelf = lastPartner = nullptr;
        }

        // w と一緒に左右分割に入っている、もう片方のウィンドウ（呼んだ側で CFRelease）
        AXUIElementRef copySplitPartner (AXUIElementRef w, pid_t& partnerPid)
        {
            CGPoint pos {}; CGSize size {};
            getFrame (w, pos, size);
            for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
            {
                if (a.activationPolicy != NSApplicationActivationPolicyRegular) continue;
                AXUIElementRef app = AXUIElementCreateApplication (a.processIdentifier);
                CFTypeRef wins = nullptr;
                AXUIElementCopyAttributeValue (app, kAXWindowsAttribute, &wins);
                CFRelease (app);
                if (wins == nullptr) continue;
                AXUIElementRef found = nullptr;
                for (id x in (__bridge NSArray*) wins)
                {
                    AXUIElementRef o = (__bridge AXUIElementRef) x;
                    if (CFEqual (o, w) || ! inSplit (o)) continue;
                    CGPoint p2 {}; CGSize s2 {};
                    getFrame (o, p2, s2);
                    // 同じ高さの行で、横にずれている
                    if (std::abs (p2.y - pos.y) < 40 && std::abs (p2.x - pos.x) > 100) { found = (AXUIElementRef) CFRetain (o); break; }
                }
                CFRelease (wins);
                if (found != nullptr) { partnerPid = a.processIdentifier; return found; }
            }
            return nullptr;
        }
    }

    void KeyTap::toggleFullScreenOrSplit()
    {
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (front == nil || front.bundleIdentifier == nil) return;
        AXUIElementRef win = copyFrontWindow (front.bundleIdentifier);
        if (win == nullptr) return;

        // 1. 左右分割の片側 → 相手を覚えて、このウィンドウだけの全画面に
        if (inSplit (win))
        {
            pid_t partnerPid = 0;
            AXUIElementRef partner = copySplitPartner (win, partnerPid);
            forgetPair();
            if (partner != nullptr)
            {
                CGPoint p1 {}, p2 {}; CGSize s1 {}, s2 {};
                getFrame (win, p1, s1);
                getFrame (partner, p2, s2);
                lastSelf = (AXUIElementRef) CFRetain (win);
                lastPartner = partner;
                lastSelfPid = front.processIdentifier;
                lastPartnerPid = partnerPid;
                lastSelfWasLeft = p1.x < p2.x;
            }
            AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 1400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanTrue);
                CFRelease (win);
            });
            return;
        }

        // 2. 全画面（1 枚）→ さっき半画面から全画面にしたウィンドウなら、元の相手と左右分割に戻す。そうでなければ全画面をやめる
        if (axBool (win, CFSTR ("AXFullScreen")))
        {
            const bool canRestore = lastSelf != nullptr && CFEqual (lastSelf, win)
                                 && [NSRunningApplication runningApplicationWithProcessIdentifier: lastPartnerPid] != nil;
            AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            if (! canRestore) { CFRelease (win); return; }

            AXUIElementRef partner = (AXUIElementRef) CFRetain (lastPartner);
            if (axBool (partner, CFSTR ("AXFullScreen")))
                AXUIElementSetAttributeValue (partner, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            const bool selfLeft = lastSelfWasLeft;
            const pid_t selfPid = lastSelfPid, partnerPid = lastPartnerPid;
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                const CGRect area = visibleAreaForAX();
                const CGFloat half = std::floor (area.size.width / 2);
                const CGRect left  = CGRectMake (area.origin.x, area.origin.y, half, area.size.height);
                const CGRect right = CGRectMake (area.origin.x + half, area.origin.y, area.size.width - half, area.size.height);
                AXUIElementRef l = selfLeft ? win : partner;
                AXUIElementRef r = selfLeft ? partner : win;
                placeWindow (l, left);
                placeWindow (r, right);
                AXUIElementPerformAction (r, kAXRaiseAction);
                dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    makeFullScreenSplit (selfLeft ? selfPid : partnerPid, l, area);
                    CFRelease (win);
                    CFRelease (partner);
                });
            });
            return;
        }

        // 3. 普通のウィンドウ → 全画面に
        AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanTrue);
        CFRelease (win);
    }

    void KeyTap::openURLSplit (const std::string& url, const std::string& profileDir, const std::string& split)
    {
        NSString* chromeId = @"com.google.Chrome";
        if ([[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: chromeId] == nil) { openURL (url, profileDir); return; }

        const CGRect area = visibleAreaForAX();
        const CGFloat half = std::floor (area.size.width / 2);
        const CGRect left  = CGRectMake (area.origin.x, area.origin.y, half, area.size.height);
        const CGRect right = CGRectMake (area.origin.x + half, area.origin.y, area.size.width - half, area.size.height);
        const std::string rightSide = split;

        // 1. ページを新しいウィンドウで開いて左半分へ → 2. 右半分 → 3. 全画面の左右分割にする
        AXUIElementRef before = copyFrontWindow (chromeId);
        launchChrome (profileDir, @[ @"--new-window", ns (url) ]);
        waitAndPlace (chromeId, before, left, 0, ^(AXUIElementRef placedLeft) {
            if (placedLeft == nullptr) return;
            AXUIElementRef leftWin = (AXUIElementRef) CFRetain (placedLeft);
            const pid_t chromePid = runningPid (chromeId);
            auto finish = ^(AXUIElementRef placedRight) {
                if (placedRight != nullptr) makeFullScreenSplit (chromePid, leftWin, area);
                CFRelease (leftWin);
            };
            if (rightSide == "chrome")
            {
                launchChrome (profileDir, @[ @"--new-window" ]);
                waitAndPlace (chromeId, (AXUIElementRef) CFRetain (leftWin), right, 0, finish);
                return;
            }
            NSString* path = ns (rightSide);
            NSString* bid = [NSBundle bundleWithPath: path].bundleIdentifier;
            if (bid == nil) { CFRelease (leftWin); return; }
            [[NSWorkspace sharedWorkspace] openApplicationAtURL: [NSURL fileURLWithPath: path]
                                                  configuration: [NSWorkspaceOpenConfiguration configuration]
                                              completionHandler: nil];
            waitAndPlace (bid, nullptr, right, 0, finish);
        });
    }

    std::vector<std::string> KeyTap::screenNames()
    {
        std::vector<std::string> names;
        for (NSScreen* s in [NSScreen screens])
        {
            NSString* n = nil;
            if (@available (macOS 10.15, *)) n = s.localizedName;
            if (n == nil) n = [NSString stringWithFormat: @"Display %lu", (unsigned long) names.size() + 1];
            names.push_back (n.UTF8String != nullptr ? n.UTF8String : "Display");
        }
        return names;
    }

    namespace
    {
        // 名前の画面の、メニューバーと Dock を除いた範囲（アクセシビリティの座標）。見つからなければマウスのある画面
        CGRect visibleAreaOfScreen (const std::string& name)
        {
            if (! name.empty())
            {
                const CGFloat primaryH = [NSScreen screens].firstObject.frame.size.height;
                for (NSScreen* s in [NSScreen screens])
                {
                    NSString* n = nil;
                    if (@available (macOS 10.15, *)) n = s.localizedName;
                    if (n != nil && name == n.UTF8String)
                    {
                        const NSRect v = s.visibleFrame;
                        return CGRectMake (v.origin.x, primaryH - (v.origin.y + v.size.height), v.size.width, v.size.height);
                    }
                }
            }
            return visibleAreaForAX();
        }

        CGRect regionRect (CGRect a, const std::string& region)
        {
            const CGFloat hw = std::floor (a.size.width / 2), hh = std::floor (a.size.height / 2);
            if (region == "left")   return CGRectMake (a.origin.x, a.origin.y, hw, a.size.height);
            if (region == "right")  return CGRectMake (a.origin.x + hw, a.origin.y, a.size.width - hw, a.size.height);
            if (region == "top")    return CGRectMake (a.origin.x, a.origin.y, a.size.width, hh);
            if (region == "bottom") return CGRectMake (a.origin.x, a.origin.y + hh, a.size.width, a.size.height - hh);
            return a;
        }
    }

    void KeyTap::openURLPlaced (const std::string& url, const std::string& profileDir,
                                const std::string& screen, const std::string& region)
    {
        NSString* chromeId = @"com.google.Chrome";
        if (region.empty() || [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: chromeId] == nil)
        {
            openURL (url, profileDir);
            return;
        }
        // 新しいウィンドウで開き、出てきたら指定の画面の指定の場所へ動かす
        // （全画面の左右分割のほうにいても、新しいウィンドウは普通のウィンドウとして出るので、そのまま別の画面へ動かせる）
        const CGRect r = regionRect (visibleAreaOfScreen (screen), region);

        // 前にいたウィンドウが全画面（左右分割も）なら、置いたあとそちらに戻す。
        // 新しいウィンドウを開くと、その画面が全画面の外（デスクトップ）に切り替わってしまうため
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        AXUIElementRef backWin = nullptr;
        pid_t backPid = 0;
        if (front != nil && front.bundleIdentifier != nil
            && front.processIdentifier != [NSRunningApplication currentApplication].processIdentifier)
        {
            backWin = copyFrontWindow (front.bundleIdentifier);
            backPid = front.processIdentifier;
            if (backWin != nullptr && ! axBool (backWin, CFSTR ("AXFullScreen"))) { CFRelease (backWin); backWin = nullptr; }
        }

        AXUIElementRef before = copyFrontWindow (chromeId);
        launchChrome (profileDir, @[ @"--new-window", ns (url) ]);
        waitAndPlace (chromeId, before, r, 0, ^(AXUIElementRef placed) {
            if (placed != nullptr) AXUIElementPerformAction (placed, kAXRaiseAction);
            if (backWin == nullptr) return;
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 400 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                if (NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier: backPid])
                    [a activateWithOptions: 0];
                AXUIElementPerformAction (backWin, kAXRaiseAction);
                CFRelease (backWin);
            });
        });
    }

    std::vector<std::pair<std::string, std::string>> KeyTap::chromeProfiles()
    {
        std::vector<std::pair<std::string, std::string>> result;
        NSString* f = [NSHomeDirectory() stringByAppendingPathComponent: @"Library/Application Support/Google/Chrome/Local State"];
        NSData* d = [NSData dataWithContentsOfFile: f];
        if (d == nil) return result;
        id j = [NSJSONSerialization JSONObjectWithData: d options: 0 error: nil];
        if (! [j isKindOfClass: [NSDictionary class]]) return result;
        id prof = ((NSDictionary*) j)[@"profile"];
        id cache = [prof isKindOfClass: [NSDictionary class]] ? ((NSDictionary*) prof)[@"info_cache"] : nil;
        if (! [cache isKindOfClass: [NSDictionary class]]) return result;
        for (NSString* dir in [[(NSDictionary*) cache allKeys] sortedArrayUsingSelector: @selector (localizedStandardCompare:)])
        {
            id info = ((NSDictionary*) cache)[dir];
            id name = [info isKindOfClass: [NSDictionary class]] ? ((NSDictionary*) info)[@"name"] : nil;
            NSString* n = [name isKindOfClass: [NSString class]] ? (NSString*) name : dir;
            result.push_back ({ dir.UTF8String, n.UTF8String != nullptr ? n.UTF8String : dir.UTF8String });
        }
        return result;
    }

    bool KeyTap::chromeFrontPage (std::string& url, std::string& title)
    {
        NSString* src = @"tell application \"Google Chrome\"\n"
                         "if (count of windows) is 0 then return \"\"\n"
                         "set t to active tab of front window\n"
                         "return (URL of t) & linefeed & (title of t)\n"
                         "end tell";
        NSAppleScript* script = [[NSAppleScript alloc] initWithSource: src];
        NSDictionary* err = nil;
        NSAppleEventDescriptor* r = [script executeAndReturnError: &err];
        NSString* out = r != nil ? [r stringValue] : nil;
        bool ok = false;
        if (out != nil && out.length > 0)
        {
            NSRange nl = [out rangeOfString: @"\n"];
            NSString* u = nl.location == NSNotFound ? out : [out substringToIndex: nl.location];
            NSString* t = nl.location == NSNotFound ? @"" : [out substringFromIndex: nl.location + 1];
            url = u.UTF8String != nullptr ? u.UTF8String : "";
            title = t.UTF8String != nullptr ? t.UTF8String : "";
            ok = ! url.empty();
        }
       #if ! __has_feature(objc_arc)
        [script release];
       #endif
        return ok;
    }

    std::string KeyTap::appDisplayName (const std::string& path)
    {
        NSString* n = [[NSFileManager defaultManager] displayNameAtPath: ns (path)];
        if ([n hasSuffix: @".app"]) n = [n substringToIndex: n.length - 4];
        return n.UTF8String != nullptr ? n.UTF8String : path;
    }

    std::vector<unsigned char> KeyTap::appIconPNG (const std::string& path, int size)
    {
        std::vector<unsigned char> result;
        NSImage* img = [[NSWorkspace sharedWorkspace] iconForFile: ns (path)];
        if (img == nil) return result;

        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes: NULL pixelsWide: size pixelsHigh: size
                                                                    bitsPerSample: 8 samplesPerPixel: 4 hasAlpha: YES isPlanar: NO
                                                                   colorSpaceName: NSCalibratedRGBColorSpace bytesPerRow: 0 bitsPerPixel: 0];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext: [NSGraphicsContext graphicsContextWithBitmapImageRep: rep]];
        [img drawInRect: NSMakeRect (0, 0, size, size) fromRect: NSZeroRect operation: NSCompositingOperationCopy fraction: 1.0];
        [NSGraphicsContext restoreGraphicsState];

        NSData* png = [rep representationUsingType: NSBitmapImageFileTypePNG properties: @{}];
        if (png != nil)
            result.assign ((const unsigned char*) png.bytes, (const unsigned char*) png.bytes + png.length);
       #if ! __has_feature(objc_arc)
        [rep release];
       #endif
        return result;
    }
}
