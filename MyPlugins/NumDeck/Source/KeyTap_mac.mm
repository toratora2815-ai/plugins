#include "KeyTap.h"
#include "Windows.h"
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <cmath>
#include <map>
#include <vector>

// メニューバーのアイコンのメニューを受け取る（Objective-C のクラスは名前空間の外に置く）
@interface NumDeckStatusTarget : NSObject <NSMenuDelegate>
{
@public
    std::function<void (int)> onPick;
    std::function<bool (int)> checked;
}
- (void) pick: (id) sender;
@end

@implementation NumDeckStatusTarget
- (void) pick: (id) sender { if (onPick) onPick ((int) [sender tag]); }
- (void) menuNeedsUpdate: (NSMenu*) menu
{
    for (NSMenuItem* item in menu.itemArray)
        item.state = (checked && item.tag > 0 && checked ((int) item.tag)) ? NSControlStateValueOn : NSControlStateValueOff;
}
@end

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
        // Reference Monitor と Idea Catcher（Logic と一緒に使う自作アプリ）が前にいる間も、Logic と同じに扱う
        return app != nil && ([app.bundleIdentifier isEqualToString: @"com.myplugins.referencemonitor"]
                              || [app.bundleIdentifier isEqualToString: @"com.myplugins.ideacatcher"]);
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

    void KeyTap::openApp (const std::string& path)
    {
        NSString* p = [ns (path) stringByStandardizingPath];

        // Num Deck 自身のタイル: ほかのアプリには触らず、Num Deck の画面を隠す・出すだけ
        // （全画面の LINE などの上で押しても、そのアプリのまま。Finder などに切り替わらない）
        if ([[[NSBundle mainBundle].bundlePath stringByStandardizingPath] isEqualToString: p])
        {
            bool visible = false;
            for (NSWindow* w in [NSApp windows])
                if (w.isVisible && w.level < NSStatusWindowLevel && w.frame.size.width > 200) visible = true;
            if (! [NSApp isHidden] && visible)
                [NSApp hide: nil];
            else
            {
                [NSApp unhide: nil];
                for (NSWindow* w in [NSApp windows])
                    if (w.frame.size.width > 200 && w.level < NSStatusWindowLevel) [w orderFrontRegardless];
            }
            return;
        }

        // 全画面の左右分割に入っている → 分割を解いて、そのアプリだけを全画面に
        if (win::unsplitToFullScreen (p)) return;

        NSRunningApplication* front = [[NSWorkspace sharedWorkspace] frontmostApplication];
        if (appBackPids == nil) appBackPids = [[NSMutableDictionary alloc] init];

        // もう前面にいる → 開く前のアプリに戻して、隠す。
        // 全画面のアプリは「隠す」が効かないので、戻り先のアプリを NSWorkspace で開き直してそちらの画面へ移る
        // （activateWithOptions は、後ろにいる Num Deck からだと macOS に断られることがある）
        if (front != nil && front.bundleURL != nil && [[front.bundleURL.path stringByStandardizingPath] isEqualToString: p])
        {
            // 戻る先: このタイルで開く前に前面だったアプリ → なければ直前に使っていたアプリ（rememberFrontApp）。
            // どちらも分からなければ、何も開かずに隠すだけ（macOS が次のアプリを前にする）。
            // Finder は戻る先にしない（開き直すと新しいフォルダのウィンドウが出てしまう。本人も使っていないので前にも出さない）
            NSRunningApplication* back = nil;
            for (NSNumber* pid in @[ appBackPids[p] ?: @0, @(lastFrontPid) ])
            {
                NSRunningApplication* b = pid.intValue != 0 ? [NSRunningApplication runningApplicationWithProcessIdentifier: (pid_t) pid.intValue] : nil;
                if (b != nil && ! b.terminated && b.processIdentifier != front.processIdentifier
                    && ! [b.bundleIdentifier isEqualToString: @"com.apple.finder"]) { back = b; break; }
            }
            const bool isSelf = front.processIdentifier == [NSRunningApplication currentApplication].processIdentifier;
            NSURL* backURL = back != nil ? back.bundleURL : nil;
            // 全画面（1 枚だけ）なら、全画面を解いてから隠す
            if (win::leaveFullScreenThenHide (front, backURL)) return;
            if (back != nil)
            {
                [back unhide];
                if (isSelf)
                    [back activateWithOptions: 0];   // 自分が前面なら譲れる
                else
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL: backURL
                                                          configuration: [NSWorkspaceOpenConfiguration configuration]
                                                      completionHandler: nil];
            }
            if (isSelf) [NSApp hide: nil];
            else        [front hide];
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
        const bool changed = overFullScreen != on;
        overFullScreen = on;
        // ふつうのアプリのウィンドウは、ほかのアプリの全画面の上には出られない。
        // 「いつも手前」のあいだは補助アプリ（Dock とメニューバーに出ない）にして、どの全画面の上にも出せるようにする
        if (changed)
        {
            [NSApp setActivationPolicy: on ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular];
            dispatch_async (dispatch_get_main_queue(), ^{
                for (NSWindow* w in [NSApp windows])
                    if (w.isVisible) [w orderFrontRegardless];
            });
        }
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

    void KeyTap::setStatusMenu (std::function<void (int)> onPick, std::function<bool (int)> checked)
    {
        static NSStatusItem* item = nil;
        static NumDeckStatusTarget* target = nil;
        if (target == nil) target = [[NumDeckStatusTarget alloc] init];
        target->onPick = std::move (onPick);
        target->checked = std::move (checked);
        if (item != nil) return;

        item = [[NSStatusBar systemStatusBar] statusItemWithLength: NSVariableStatusItemLength];
       #if ! __has_feature(objc_arc)
        [item retain];
       #endif
        if (NSImage* img = [NSImage imageWithSystemSymbolName: @"square.grid.3x3.fill" accessibilityDescription: @"Num Deck"])
        {
            [img setTemplate: YES];  // "template" は C++ の予約語なのでドット記法は使えない
            item.button.image = img;
        }
        else
            item.button.title = @"Num";
        item.button.toolTip = @"Num Deck";

        NSMenu* menu = [[NSMenu alloc] initWithTitle: @"Num Deck"];
        menu.delegate = target;
        auto add = ^(NSString* title, int tag) {
            NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle: title action: @selector (pick:) keyEquivalent: @""];
            mi.target = target;
            mi.tag = tag;
            [menu addItem: mi];
           #if ! __has_feature(objc_arc)
            [mi release];
           #endif
        };
        add (@"Num Deck の画面を出す", 1);
        [menu addItem: [NSMenuItem separatorItem]];
        add (@"テンキー ON", 2);
        add (@"いつも手前（全画面の上にも出す）", 3);
        [menu addItem: [NSMenuItem separatorItem]];
        add (@"Num Deck を終了", 4);
        item.menu = menu;
       #if ! __has_feature(objc_arc)
        [menu release];
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
        NSString* src = @"with timeout of 3 seconds\n"
                         "tell application \"Google Chrome\"\n"
                         "if (count of windows) is 0 then return \"\"\n"
                         "set t to active tab of front window\n"
                         "return (URL of t) & linefeed & (title of t)\n"
                         "end tell\n"
                         "end timeout";
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
