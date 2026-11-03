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
}


namespace numdeck
{
    static NSString* ns (const std::string& s) { return [NSString stringWithUTF8String: s.c_str()]; }

    // アプリごとの「開く前に前面だったアプリ」（隠したときにそこへ戻る）
    static NSMutableDictionary<NSString*, NSNumber*>* appBackPids = nil;

    void KeyTap::openApp (const std::string& path)
    {
        NSString* p = [ns (path) stringByStandardizingPath];
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

        // before と違うウィンドウが前に出るまで待って（最大 6 秒）、r に置いてから then。before は最後に CFRelease する
        void waitAndPlace (NSString* bundleId, AXUIElementRef before, CGRect r, int tries, void (^then) (void))
        {
            AXUIElementRef w = copyFrontWindow (bundleId);
            const bool ready = w != nullptr && (before == nullptr || ! CFEqual (w, before));
            if (! ready && tries < 60)
            {
                if (w != nullptr) CFRelease (w);
                dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    waitAndPlace (bundleId, before, r, tries + 1, then);
                });
                return;
            }
            if (ready) placeWindow (w, r);
            if (w != nullptr) CFRelease (w);
            if (before != nullptr) CFRelease (before);
            if (then != nil) then();
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

    void KeyTap::openURLSplit (const std::string& url, const std::string& profileDir, const std::string& split)
    {
        NSString* chromeId = @"com.google.Chrome";
        if ([[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: chromeId] == nil) { openURL (url, profileDir); return; }

        const CGRect area = visibleAreaForAX();
        const CGFloat half = std::floor (area.size.width / 2);
        const CGRect left  = CGRectMake (area.origin.x, area.origin.y, half, area.size.height);
        const CGRect right = CGRectMake (area.origin.x + half, area.origin.y, area.size.width - half, area.size.height);
        const std::string rightSide = split;

        // 1. ページを新しいウィンドウで開いて左半分へ
        AXUIElementRef before = copyFrontWindow (chromeId);
        launchChrome (profileDir, @[ @"--new-window", ns (url) ]);
        waitAndPlace (chromeId, before, left, 0, ^{
            // 2. 右半分
            if (rightSide == "chrome")
            {
                AXUIElementRef leftWin = copyFrontWindow (chromeId);
                launchChrome (profileDir, @[ @"--new-window" ]);
                waitAndPlace (chromeId, leftWin, right, 0, nil);
                return;
            }
            NSString* path = ns (rightSide);
            NSString* bid = [NSBundle bundleWithPath: path].bundleIdentifier;
            if (bid == nil) return;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL: [NSURL fileURLWithPath: path]
                                                  configuration: [NSWorkspaceOpenConfiguration configuration]
                                              completionHandler: nil];
            waitAndPlace (bid, nullptr, right, 0, nil);
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
