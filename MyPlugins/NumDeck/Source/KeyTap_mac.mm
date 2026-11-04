#include "KeyTap.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <cmath>
#include <map>
#include <vector>

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

    void KeyTap::quitAllApps (const std::vector<std::string>& keep)
    {
        const pid_t self = [NSRunningApplication currentApplication].processIdentifier;
        for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
        {
            if (a.activationPolicy != NSApplicationActivationPolicyRegular || a.processIdentifier == self) continue;
            NSString* bid = a.bundleIdentifier;
            if (bid == nil || [bid isEqualToString: @"com.apple.finder"]) continue;
            bool kept = false;
            for (auto& k : keep)  // 前方一致（"com.apple.logic" で Logic Pro のどの版も）
                if (! k.empty() && [bid hasPrefix: ns (k)]) kept = true;
            if (! kept) [a terminate];
        }
    }

    std::vector<std::pair<std::string, std::string>> KeyTap::runningApps()
    {
        std::vector<std::pair<std::string, std::string>> r;
        const pid_t self = [NSRunningApplication currentApplication].processIdentifier;
        for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
        {
            if (a.activationPolicy != NSApplicationActivationPolicyRegular || a.processIdentifier == self || a.bundleIdentifier == nil) continue;
            NSString* n = a.localizedName != nil ? a.localizedName : a.bundleIdentifier;
            r.push_back ({ a.bundleIdentifier.UTF8String, n.UTF8String != nullptr ? n.UTF8String : a.bundleIdentifier.UTF8String });
        }
        return r;
    }

    // Num Deck のウィンドウ（本体・メニュー・小窓・マクロの画面）を全部、ほかのアプリの全画面の上にも出られるようにする。
    // メニューなどは開くたびに新しいウィンドウができるので、アプリの画面が更新されるたびに付け直す
    static bool overFullScreen = false;

    static void applyOverFullScreen (NSWindow* w)
    {
        if ([w isKindOfClass: [NSPanel class]] && w.level >= NSStatusWindowLevel) return;  // 押したときの表示（HUD）はそのまま
        const NSWindowCollectionBehavior extra = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        NSWindowCollectionBehavior b = w.collectionBehavior;
        if (overFullScreen)
        {
            b &= ~(NSWindowCollectionBehaviorMoveToActiveSpace | NSWindowCollectionBehaviorFullScreenPrimary);
            if ((b & extra) != extra) w.collectionBehavior = b | extra;
            if (w.level < NSFloatingWindowLevel) w.level = NSFloatingWindowLevel;
        }
        else if ((b & extra) != 0)
            w.collectionBehavior = b & ~extra;
    }

    void KeyTap::setOverFullScreen (void*, bool on)
    {
        static id observer = nil;
        overFullScreen = on;
        for (NSWindow* w in [NSApp windows]) applyOverFullScreen (w);
        if (observer != nil) return;
        observer = [[NSNotificationCenter defaultCenter] addObserverForName: NSApplicationDidUpdateNotification object: nil
                                                                      queue: nil usingBlock: ^(NSNotification*) {
            if (! overFullScreen) return;
            for (NSWindow* w in [NSApp windows]) applyOverFullScreen (w);
        }];
       #if ! __has_feature(objc_arc)
        [observer retain];
       #endif
    }

    // 押した操作の名前を、画面の下に一瞬だけ出す（全画面のアプリの上にも出る。クリックは通り抜ける）
    void KeyTap::showHUD (const std::string& text)
    {
        static NSPanel* panel = nil;
        static NSTextField* label = nil;
        static int generation = 0;
        if (panel == nil)
        {
            panel = [[NSPanel alloc] initWithContentRect: NSMakeRect (0, 0, 300, 52)
                                               styleMask: NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
                                                 backing: NSBackingStoreBuffered defer: NO];
            panel.level = NSStatusWindowLevel;
            panel.opaque = NO;
            panel.backgroundColor = [NSColor clearColor];
            panel.ignoresMouseEvents = YES;
            panel.hasShadow = YES;
            panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary
                                     | NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle;
            NSView* v = [[NSView alloc] initWithFrame: NSMakeRect (0, 0, 300, 52)];
            v.wantsLayer = YES;
            v.layer.backgroundColor = [NSColor colorWithCalibratedRed: 0.08 green: 0.10 blue: 0.13 alpha: 0.92].CGColor;
            v.layer.cornerRadius = 14;
            v.layer.borderWidth = 1.5;
            v.layer.borderColor = [NSColor colorWithCalibratedRed: 1.0 green: 0.55 blue: 0.25 alpha: 0.9].CGColor;
            label = [NSTextField labelWithString: @""];
            label.font = [NSFont boldSystemFontOfSize: 19];
            label.textColor = [NSColor whiteColor];
            label.alignment = NSTextAlignmentCenter;
            [v addSubview: label];
            panel.contentView = v;
        }
        label.stringValue = ns (text);
        [label sizeToFit];
        const CGFloat w = std::max<CGFloat> (160, label.frame.size.width + 48), h = 52;
        const NSPoint m = [NSEvent mouseLocation];
        NSScreen* scr = [NSScreen mainScreen];
        for (NSScreen* s in [NSScreen screens])
            if (NSPointInRect (m, s.frame)) { scr = s; break; }
        const NSRect vf = scr.visibleFrame;
        [panel setFrame: NSMakeRect (NSMidX (vf) - w / 2, vf.origin.y + 70, w, h) display: NO];
        label.frame = NSMakeRect (0, (h - label.frame.size.height) / 2, w, label.frame.size.height);
        panel.alphaValue = 1.0;
        [panel orderFrontRegardless];
        const int gen = ++generation;
        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 900 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            if (gen != generation) return;
            [NSAnimationContext runAnimationGroup: ^(NSAnimationContext* ctx) { ctx.duration = 0.25; panel.animator.alphaValue = 0.0; }
                                completionHandler: ^{ if (gen == generation) [panel orderOut: nil]; }];
        });
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

        // cond が true になるまで 0.1 秒ごとに確かめて待つ（最大 timeoutMs）。then には間に合ったかを渡す。
        // 決め打ちの待ち時間だと、Mac が忙しいときに全画面の切り替えが終わる前に次へ進んでずれるため
        void waitFor (bool (^cond) (void), int timeoutMs, void (^then) (bool))
        {
            if (cond()) { then (true); return; }
            if (timeoutMs <= 0) { then (false); return; }
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                waitFor (cond, timeoutMs - 100, then);
            });
        }

        void later (int ms, void (^block) (void))
        {
            dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) ms * (int64_t) NSEC_PER_MSEC), dispatch_get_main_queue(), block);
        }

        bool isFullScreen (AXUIElementRef w) { return axBool (w, CFSTR ("AXFullScreen")); }

        // アプリを前に出す（activateWithOptions は後ろにいる Num Deck からだと断られることがあるので、開き直す）
        void bringAppForward (pid_t pid)
        {
            if (NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
                if (a.bundleURL != nil)
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL: a.bundleURL
                                                          configuration: [NSWorkspaceOpenConfiguration configuration]
                                                      completionHandler: nil];
        }

        // そのアプリの、今キーボードが向いているウィンドウ（呼んだ側で CFRelease）
        AXUIElementRef copyFocusedWindowOf (pid_t pid)
        {
            AXUIElementRef app = AXUIElementCreateApplication (pid);
            CFTypeRef w = nullptr;
            AXUIElementCopyAttributeValue (app, kAXFocusedWindowAttribute, &w);
            CFRelease (app);
            return (AXUIElementRef) w;
        }

        // win を、そのアプリの前面（キーボードが向く）ウィンドウにする
        void focusWindow (AXUIElementRef win)
        {
            AXUIElementSetAttributeValue (win, kAXMainAttribute, kCFBooleanTrue);
            AXUIElementPerformAction (win, kAXRaiseAction);
        }

        // pid のアプリが前面で、そのキーボードが win を向いているか
        bool isFocused (pid_t pid, AXUIElementRef win)
        {
            NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
            if (front == nil || front.processIdentifier != pid) return false;
            AXUIElementRef f = copyFocusedWindowOf (pid);
            const bool ok = f != nullptr && CFEqual (f, win);
            if (f != nullptr) CFRelease (f);
            return ok;
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
                // 全画面を抜けきってから置き直す（同じウィンドウなので before は外す）
                if (before != nullptr) CFRelease (before);
                waitFor (^{ return ! isFullScreen (w); }, 4000, ^(bool) {
                    CFRelease (w);
                    later (300, ^{ waitAndPlace (bundleId, nullptr, r, tries + 1, then); });
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
            waitFor (^{ return ! isFullScreen (win); }, 4000, ^(bool) {
                later (400, ^{
                    AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanTrue);
                    CFRelease (win);
                });
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
            if (! fs) { CFRelease (win); return false; }
            AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            const pid_t pid = app.processIdentifier;
            NSURL* back = [backURL copy];
            waitFor (^{ return ! isFullScreen (win); }, 4000, ^(bool) { CFRelease (win); later (300, ^{
                if (back != nil)
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL: back
                                                          configuration: [NSWorkspaceOpenConfiguration configuration]
                                                      completionHandler: nil];
                if (NSRunningApplication* a = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
                    [a hide];
            }); });
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
        // 相手を選ぶ画面には、今の画面にあるほかのアプリのウィンドウも並ぶ。違うものを選ばないよう、
        // 左右の 2 つのアプリ以外（Num Deck も）をいったん隠し、分割できたら元に戻す
        void makeFullScreenSplit (pid_t leftPid, AXUIElementRef leftWin, pid_t rightPid, AXUIElementRef rightWin, CGRect area)
        {
            NSMutableArray<NSRunningApplication*>* hidden = [NSMutableArray array];
            for (NSRunningApplication* a in [[NSWorkspace sharedWorkspace] runningApplications])
            {
                if (a.activationPolicy != NSApplicationActivationPolicyRegular || a.hidden) continue;
                if (a.processIdentifier == leftPid || a.processIdentifier == rightPid) continue;
                if ([a hide]) [hidden addObject: a];
            }
           #if ! __has_feature(objc_arc)
            [hidden retain];
           #endif
            AXUIElementRef win = (AXUIElementRef) CFRetain (leftWin);
            AXUIElementRef rwin = (AXUIElementRef) CFRetain (rightWin);
            auto finish = ^(const char* message) {
                if (message != nullptr) KeyTap::showHUD (message);
                for (NSRunningApplication* a in hidden) [a unhide];
               #if ! __has_feature(objc_arc)
                [hidden release];
               #endif
                CFRelease (win);
                CFRelease (rwin);
            };

            // 1. 左のウィンドウのアプリを前に出し、キーボードが左のウィンドウを向くまで待つ
            //    （向く前に「画面左側」を押すと、同じアプリの別のウィンドウ（研修の画面など）が分割されてしまう）
            bringAppForward (leftPid);
            focusWindow (win);
            waitFor (^{ if (! isFocused (leftPid, win)) focusWindow (win); return isFocused (leftPid, win); }, 3000, ^(bool focused) {
                if (! focused) { finish ("うまく並べられませんでした（もう一度押してください）"); return; }
                // 2. 「ウィンドウ」→「フルスクリーンのタイル表示」→「画面左側」
                if (! pressTileLeft (leftPid)) { finish ("この画面では 2 分割にできませんでした"); return; }
                // 3. 左が全画面に入る（＝右に置く相手を選ぶ画面が出た）のを待ってから、右半分の真ん中をクリック
                waitFor (^{ return isFullScreen (win); }, 4000, ^(bool entered) {
                    if (! entered) { finish ("うまく並べられませんでした（もう一度押してください）"); return; }
                    later (600, ^{
                        clickAt (CGPointMake (area.origin.x + area.size.width * 0.875, area.origin.y + area.size.height * 0.5));
                        // 4. 右に入ったか確かめる。入っていなければ、選ぶ画面で自分で選んでもらう
                        waitFor (^{ return isFullScreen (rwin); }, 3000, ^(bool ok) {
                            finish (ok ? nullptr : "右に置くウィンドウをクリックしてください");
                        });
                    });
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

        bool stillExists (AXUIElementRef w)
        {
            if (w == nullptr) return false;
            CFTypeRef role = nullptr;
            const bool ok = AXUIElementCopyAttributeValue (w, kAXRoleAttribute, &role) == kAXErrorSuccess;
            if (role != nullptr) CFRelease (role);
            return ok;
        }

        // l を左、r を右にして全画面の左右分割にする（どちらかが全画面なら先に解除して待つ）
        void resplit (AXUIElementRef l, pid_t lPid, AXUIElementRef r, pid_t rPid)
        {
            for (AXUIElementRef w : { l, r })
            {
                if (axBool (w, kAXMinimizedAttribute)) AXUIElementSetAttributeValue (w, kAXMinimizedAttribute, kCFBooleanFalse);
                if (isFullScreen (w)) AXUIElementSetAttributeValue (w, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            }
            AXUIElementRef lw = (AXUIElementRef) CFRetain (l);
            AXUIElementRef rw = (AXUIElementRef) CFRetain (r);
            waitFor (^{ return ! isFullScreen (lw) && ! isFullScreen (rw); }, 5000, ^(bool) {
                later (400, ^{
                    const CGRect area = visibleAreaForAX();
                    const CGFloat half = std::floor (area.size.width / 2);
                    placeWindow (lw, CGRectMake (area.origin.x, area.origin.y, half, area.size.height));
                    placeWindow (rw, CGRectMake (area.origin.x + half, area.origin.y, area.size.width - half, area.size.height));
                    AXUIElementPerformAction (rw, kAXRaiseAction);
                    later (300, ^{
                        makeFullScreenSplit (lPid, lw, rPid, rw, area);
                        CFRelease (lw);
                        CFRelease (rw);
                    });
                });
            });
        }

    }

    void KeyTap::toggleFullScreenOrSplit()
    {
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (front == nil || front.bundleIdentifier == nil) return;
        AXUIElementRef win = copyFrontWindow (front.bundleIdentifier);
        if (win == nullptr) return;

        // 1. 左右分割の片側 → このウィンドウだけの全画面に
        if (inSplit (win))
        {
            showHUD ("全画面");
            AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            waitFor (^{ return ! isFullScreen (win); }, 4000, ^(bool) {
                later (400, ^{
                    AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanTrue);
                    CFRelease (win);
                });
            });
            return;
        }

        // 2. 全画面（1 枚）→ 全画面を解除するだけ（2 画面には戻さない）
        if (isFullScreen (win))
        {
            showHUD ("全画面を解除");
            AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanFalse);
            CFRelease (win);
            return;
        }

        // 3. 普通のウィンドウ → 全画面に
        showHUD ("全画面");
        AXUIElementSetAttributeValue (win, CFSTR ("AXFullScreen"), kCFBooleanTrue);
        CFRelease (win);
    }

    namespace
    {
        // 2 画面で開いたリンクごとの、左右のウィンドウ（もう一度押したとき、残っていれば使い回す）
        struct LinkWindows { AXUIElementRef left = nullptr, right = nullptr; pid_t leftPid = 0, rightPid = 0; };
        std::map<std::string, LinkWindows> linkWindows;

        void rememberLink (const std::string& url, AXUIElementRef l, pid_t lp, AXUIElementRef r, pid_t rp)
        {
            auto& e = linkWindows[url];
            if (e.left != nullptr) CFRelease (e.left);
            if (e.right != nullptr) CFRelease (e.right);
            e = { (AXUIElementRef) CFRetain (l), (AXUIElementRef) CFRetain (r), lp, rp };
        }
    }

    namespace
    {
        // Chrome のウィンドウの中から、host のページのタブがあるものを探し、そのタブを選んで前に出す（AppleScript）。
        // 全画面（左右分割も）の別の画面にあっても、そこへ移る。見つかったら true
        bool raiseChromeWindowWithHost (const std::string& host)
        {
            if (host.empty()) return false;
            NSString* src = [NSString stringWithFormat:
                @"tell application \"Google Chrome\"\n"
                 "repeat with w in windows\n"
                 "set n to count of tabs of w\n"
                 "repeat with i from 1 to n\n"
                 "if URL of tab i of w contains \"%@\" then\n"
                 "set active tab index of w to i\n"
                 "set index of w to 1\n"
                 "activate\n"
                 "return \"found\"\n"
                 "end if\n"
                 "end repeat\n"
                 "end repeat\n"
                 "return \"\"\n"
                 "end tell", ns (host)];
            NSAppleScript* script = [[NSAppleScript alloc] initWithSource: src];
            NSDictionary* err = nil;
            NSAppleEventDescriptor* r = [script executeAndReturnError: &err];
            const bool found = r != nil && [[r stringValue] isEqualToString: @"found"];
           #if ! __has_feature(objc_arc)
            [script release];
           #endif
            return found;
        }

        std::string hostOf (const std::string& url)
        {
            NSURL* u = [NSURL URLWithString: ns (url)];
            return u != nil && u.host != nil ? std::string (u.host.UTF8String) : std::string();
        }
    }

    void KeyTap::openURLSplit (const std::string& url, const std::string& profileDir, const std::string& split)
    {
        NSString* chromeId = @"com.google.Chrome";
        if ([[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: chromeId] == nil) { openURL (url, profileDir); return; }

        const CGRect area = visibleAreaForAX();
        const CGFloat half = std::floor (area.size.width / 2);
        const CGRect left  = CGRectMake (area.origin.x, area.origin.y, half, area.size.height);
        const CGRect right = CGRectMake (area.origin.x + half, area.origin.y, area.size.width - half, area.size.height);
        const std::string rightSide = split, key = url;

        // 左のウィンドウが決まったあと: 右を用意して、全画面の左右分割にする
        auto withLeft = ^(AXUIElementRef placedLeft, bool reuseRight) {
            AXUIElementRef leftWin = (AXUIElementRef) CFRetain (placedLeft);
            const pid_t chromePid = runningPid (chromeId);
            auto finish = ^(AXUIElementRef placedRight) {
                if (placedRight != nullptr)
                {
                    pid_t rp = 0;
                    AXUIElementGetPid (placedRight, &rp);
                    rememberLink (key, leftWin, chromePid, placedRight, rp);
                    makeFullScreenSplit (chromePid, leftWin, rp, placedRight, area);
                }
                CFRelease (leftWin);
            };
            // 前にこのリンクで使った右のウィンドウが残っていれば、それを使う
            auto it = linkWindows.find (key);
            if (reuseRight && it != linkWindows.end() && stillExists (it->second.right) && ! CFEqual (it->second.right, leftWin))
            {
                AXUIElementRef rw = (AXUIElementRef) CFRetain (it->second.right);
                const pid_t rp = it->second.rightPid;
                rememberLink (key, leftWin, chromePid, rw, rp);
                resplit (leftWin, chromePid, rw, rp);
                CFRelease (rw);
                CFRelease (leftWin);
                return;
            }
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
        };

        // 新しいウィンドウで開く
        auto openNew = ^{
            AXUIElementRef before = copyFrontWindow (chromeId);
            launchChrome (profileDir, @[ @"--new-window", ns (key) ]);
            waitAndPlace (chromeId, before, left, 0, ^(AXUIElementRef placedLeft) {
                if (placedLeft != nullptr) withLeft (placedLeft, false);
            });
        };

        // このページのタブがある Chrome のウィンドウがあれば、それを使い回す（Num Deck を入れ直したあとでも見つかる）。
        // そのウィンドウがもう左右分割に入っていれば、その画面へ移るだけ。普通のウィンドウなら左に置いて分割し直す
        if (! raiseChromeWindowWithHost (hostOf (url))) { openNew(); return; }
        waitFor (^{ NSRunningApplication* f = [[NSWorkspace sharedWorkspace] frontmostApplication];
                    return f != nil && [f.bundleIdentifier isEqualToString: chromeId]; }, 3000, ^(bool) {
            later (500, ^{
                AXUIElementRef w = copyFocusedWindowOf (runningPid (chromeId));
                if (w == nullptr) { openNew(); return; }
                if (inSplit (w)) { CFRelease (w); return; }
                auto placeAndGo = ^{
                    placeWindow (w, left);
                    withLeft (w, true);
                    CFRelease (w);
                };
                if (isFullScreen (w))
                {
                    AXUIElementSetAttributeValue (w, CFSTR ("AXFullScreen"), kCFBooleanFalse);
                    waitFor (^{ return ! isFullScreen (w); }, 4000, ^(bool) { later (400, placeAndGo); });
                }
                else
                    placeAndGo();
            });
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
                bringAppForward (backPid);
                focusWindow (backWin);
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
