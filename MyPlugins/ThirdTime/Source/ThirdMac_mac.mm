#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
#include <atomic>
#include <mutex>
#include <unistd.h>
#include "ThirdMac.h"

#if __has_feature(objc_arc)
 #define TT_RELEASE(x)
#else
 #define TT_RELEASE(x) [x release]
#endif

namespace
{
std::string str (NSString* s) { return s != nil && [s UTF8String] != nullptr ? std::string ([s UTF8String]) : std::string(); }
NSString* ns (const std::string& s) { return [NSString stringWithUTF8String: s.c_str()] ?: @""; }
double nowSec() { return [NSDate timeIntervalSinceReferenceDate]; }
}

@interface TTObserver : NSObject
{
@public
    std::function<void (int)> onPick;
}
- (void) activated: (NSNotification*) n;
- (void) pick: (id) sender;
@end

namespace third::mac
{
    static std::function<void (const Step&)> stepFn;
    static std::vector<std::string> skip;
    static std::mutex skipLock;
    static CFMachPortRef tap = nullptr;
    static CFRunLoopSourceRef tapSrc = nullptr;
    static std::atomic<bool> replaying { false };
    static TTObserver* observer = nil;

    static dispatch_queue_t stepQueue()
    {
        static dispatch_queue_t q = dispatch_queue_create ("com.myplugins.thirdtime.steps", DISPATCH_QUEUE_SERIAL);
        return q;
    }

    static bool skipped (const std::string& bundle)
    {
        std::lock_guard<std::mutex> l (skipLock);
        if (bundle == str ([NSBundle mainBundle].bundleIdentifier)) return true;
        for (auto& s : skip) if (! s.empty() && bundle.rfind (s, 0) == 0) return true;
        return false;
    }

    static void deliver (Step s)
    {
        if (s.bundle.empty() || skipped (s.bundle)) return;
        dispatch_async (dispatch_get_main_queue(), ^{ if (stepFn) stepFn (s); });
    }

    std::string appName (const std::string& bundle)
    {
        NSArray* a = [NSRunningApplication runningApplicationsWithBundleIdentifier: ns (bundle)];
        if (a.count > 0) return str (((NSRunningApplication*) a.firstObject).localizedName);
        NSURL* u = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: ns (bundle)];
        if (u != nil) return str ([[u lastPathComponent] stringByDeletingPathExtension]);
        return bundle;
    }

    //==========================================================================
    static std::string keyName (CGKeyCode kc, CGEventRef e)
    {
        switch (kc)
        {
            case kVK_Return: return "Return";       case kVK_ANSI_KeypadEnter: return "Enter";
            case kVK_Tab: return "Tab";             case kVK_Escape: return "Esc";
            case kVK_Delete: return "Delete";       case kVK_ForwardDelete: return "⌦";
            case kVK_Space: return "Space";
            case kVK_LeftArrow: return "←";         case kVK_RightArrow: return "→";
            case kVK_UpArrow: return "↑";           case kVK_DownArrow: return "↓";
            case kVK_Home: return "Home";           case kVK_End: return "End";
            case kVK_PageUp: return "PageUp";       case kVK_PageDown: return "PageDown";
            case kVK_F1: return "F1"; case kVK_F2: return "F2"; case kVK_F3: return "F3"; case kVK_F4: return "F4";
            case kVK_F5: return "F5"; case kVK_F6: return "F6"; case kVK_F7: return "F7"; case kVK_F8: return "F8";
            case kVK_F9: return "F9"; case kVK_F10: return "F10"; case kVK_F11: return "F11"; case kVK_F12: return "F12";
            default: break;
        }
        // ⌘ や ⌃ を外した文字（キーボードの配列どおり）
        CGEventRef c = CGEventCreateCopy (e);
        CGEventSetFlags (c, 0);
        UniChar buf[4] {}; UniCharCount n = 0;
        CGEventKeyboardGetUnicodeString (c, 4, &n, buf);
        CFRelease (c);
        if (n == 0) return "#" + std::to_string (kc);
        return str ([[NSString stringWithCharacters: buf length: n] uppercaseString]);
    }

    static std::string modsText (CGEventFlags f)
    {
        std::string s;
        if (f & kCGEventFlagMaskControl) s += "⌃";
        if (f & kCGEventFlagMaskAlternate) s += "⌥";
        if (f & kCGEventFlagMaskShift) s += "⇧";
        if (f & kCGEventFlagMaskCommand) s += "⌘";
        return s;
    }

    static std::string meaning (const std::string& combo)
    {
        static const std::pair<const char*, const char*> m[] {
            { "⌘C", "コピー" }, { "⌘V", "貼り付け" }, { "⌘X", "切り取り" }, { "⌘S", "保存" }, { "⌘Z", "取り消し" },
            { "⌘A", "全部選ぶ" }, { "⌘T", "新しいタブ" }, { "⌘W", "閉じる" }, { "⌘N", "新規" }, { "⌘F", "検索" },
            { "⌘P", "プリント" }, { "⌘L", "アドレス欄" }, { "⌘R", "再読み込み" }, { "⇧⌘S", "別名で保存" }, { "⌥⇧⌘V", "書式なしで貼り付け" },
            { "Return", "決定" }, { "Esc", "やめる" }, { "Tab", "次の欄" } };
        for (auto& [k, v] : m) if (combo == k) return std::string (k) + "（" + v + "）";
        return combo;
    }

    static bool isSpecial (CGKeyCode kc)
    {
        return kc == kVK_Return || kc == kVK_ANSI_KeypadEnter || kc == kVK_Tab || kc == kVK_Escape
            || (kc >= kVK_F1 && kc <= kVK_F12 && kc != kVK_Delete);
    }

    static CFTypeRef axCopy (AXUIElementRef el, CFStringRef attr)
    {
        CFTypeRef v = nullptr;
        if (el == nullptr || AXUIElementCopyAttributeValue (el, attr, &v) != kAXErrorSuccess) return nullptr;
        return v;
    }
    static std::string axStr (AXUIElementRef el, CFStringRef attr)
    {
        std::string out;
        if (CFTypeRef t = axCopy (el, attr))
        {
            if (CFGetTypeID (t) == CFStringGetTypeID()) out = str ((__bridge NSString*) t);
            CFRelease (t);
        }
        return out;
    }
    static std::string nameOf (AXUIElementRef el)
    {
        auto t = axStr (el, kAXTitleAttribute);
        if (t.empty()) t = axStr (el, kAXDescriptionAttribute);
        if (t.empty()) t = axStr (el, CFSTR ("AXPlaceholderValue"));
        if (t.empty() && axStr (el, kAXRoleAttribute) == "AXStaticText") t = axStr (el, kAXValueAttribute);
        return t;
    }
    static bool frameOf (AXUIElementRef el, CGRect& r)
    {
        CFTypeRef p = axCopy (el, kAXPositionAttribute), s = axCopy (el, kAXSizeAttribute);
        bool ok = false;
        if (p && s) { ok = AXValueGetValue ((AXValueRef) p, (AXValueType) kAXValueCGPointType, &r.origin)
                        && AXValueGetValue ((AXValueRef) s, (AXValueType) kAXValueCGSizeType, &r.size); }
        if (p) CFRelease (p);
        if (s) CFRelease (s);
        return ok;
    }

    static Step clickStep (CGPoint p, const std::string& frontBundle, double t)
    {
        Step s;
        s.kind = Click;
        s.t = t;
        s.bundle = frontBundle;
        s.strong = false;
        AXUIElementRef sys = AXUIElementCreateSystemWide();
        AXUIElementSetMessagingTimeout (sys, 0.5f);
        AXUIElementRef el = nullptr;
        if (AXUIElementCopyElementAtPosition (sys, (float) p.x, (float) p.y, &el) == kAXErrorSuccess && el != nullptr)
        {
            pid_t pid = 0;
            AXUIElementGetPid (el, &pid);
            if (NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier: pid])
                s.bundle = str (app.bundleIdentifier);
            s.role = axStr (el, kAXRoleAttribute);
            s.title = nameOf (el);
            // 名前がなければ、すぐ上のボタンなどの名前を使う
            AXUIElementRef cur = (AXUIElementRef) CFRetain (el);
            for (int up = 0; up < 2 && s.title.empty(); ++up)
            {
                AXUIElementRef parent = (AXUIElementRef) axCopy (cur, kAXParentAttribute);
                CFRelease (cur);
                cur = parent;
                if (cur == nullptr) break;
                const auto r = axStr (cur, kAXRoleAttribute);
                if (r == "AXButton" || r == "AXMenuItem" || r == "AXLink" || r == "AXCheckBox" || r == "AXRadioButton" || r == "AXPopUpButton" || r == "AXTab")
                { s.role = r; s.title = nameOf (cur); }
            }
            if (cur) CFRelease (cur);
            // ウィンドウの中の位置
            if (CFTypeRef w = axCopy (el, kAXWindowAttribute))
            {
                CGRect f;
                if (frameOf ((AXUIElementRef) w, f) && f.size.width > 0 && f.size.height > 0)
                { s.relX = (p.x - f.origin.x) / f.size.width; s.relY = (p.y - f.origin.y) / f.size.height; }
                CFRelease (w);
            }
            CFRelease (el);
        }
        CFRelease (sys);
        if (s.title.size() > 60) s.title.clear();   // 長い名前（本文など）は毎回ちがうので使わない
        static const char* weakRoles[] { "AXTextArea", "AXScrollArea", "AXGroup", "AXWebArea", "AXSplitGroup", "AXLayoutArea", "AXUnknown", "AXImage", "AXWindow", "AXRow", "AXCell" };
        bool weakRole = s.role.empty();
        for (auto* w : weakRoles) if (s.role == w) weakRole = true;
        s.strong = ! s.title.empty() && ! weakRole;
        const auto app = appName (s.bundle);
        s.key = "click:" + s.bundle + ":" + s.role + ":" + s.title;
        if (s.strong)
        {
            const bool field = s.role == "AXTextField" || s.role == "AXComboBox" || s.role == "AXSearchField";
            s.label = "「" + s.title + "」" + (field ? "の欄をクリック" : s.role == "AXMenuItem" ? "（メニュー）" : "を押す") + "（" + app + "）";
        }
        else s.label = "クリック（" + app + "）";
        return s;
    }

    static CGEventRef tapCallback (CGEventTapProxy, CGEventType type, CGEventRef e, void*)
    {
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput)
        {
            if (tap) CGEventTapEnable (tap, true);
            return e;
        }
        if (replaying.load()) return e;
        const double t = nowSec();
        NSRunningApplication* front = [NSWorkspace sharedWorkspace].frontmostApplication;
        const std::string bundle = str (front.bundleIdentifier);

        if (type == kCGEventKeyDown)
        {
            if (CGEventGetIntegerValueField (e, kCGKeyboardEventAutorepeat) != 0) return e;
            const CGKeyCode kc = (CGKeyCode) CGEventGetIntegerValueField (e, kCGKeyboardEventKeycode);
            const CGEventFlags fl = CGEventGetFlags (e) & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskShift);
            Step s;
            s.t = t;
            s.bundle = bundle;
            if (fl & (kCGEventFlagMaskCommand | kCGEventFlagMaskControl))
            {
                if (kc == kVK_Tab || kc == kVK_Space) return e;   // アプリ切り替え・入力切り替えは「アプリを前に」で数える
                s.kind = Key; s.keyCode = kc; s.flags = fl;
                const auto combo = modsText (fl) + keyName (kc, e);
                s.key = "key:" + bundle + ":" + std::to_string ((unsigned long long) fl) + ":" + std::to_string (kc);
                s.label = meaning (combo);
            }
            else if (isSpecial (kc))
            {
                s.kind = Key; s.keyCode = kc; s.flags = fl;
                s.key = "key:" + bundle + ":" + std::to_string ((unsigned long long) fl) + ":" + std::to_string (kc);
                s.label = meaning (modsText (fl) + keyName (kc, e));
            }
            else
            {
                s.kind = Type; s.strong = false;
                s.key = "type:" + bundle;
                s.label = "文字を入力（" + appName (bundle) + "）";
            }
            dispatch_async (stepQueue(), ^{ deliver (s); });
        }
        else if (type == kCGEventLeftMouseDown)
        {
            const CGPoint p = CGEventGetLocation (e);
            dispatch_async (stepQueue(), ^{ deliver (clickStep (p, bundle, t)); });
        }
        return e;
    }

    bool startWatching (std::function<void (const Step&)> onStep, const std::vector<std::string>& skipBundles)
    {
        stepFn = std::move (onStep);
        setSkip (skipBundles);
        if (tap != nullptr) return true;
        const CGEventMask mask = CGEventMaskBit (kCGEventKeyDown) | CGEventMaskBit (kCGEventLeftMouseDown);
        tap = CGEventTapCreate (kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, mask, &tapCallback, nullptr);
        if (tap == nullptr) return false;
        tapSrc = CFMachPortCreateRunLoopSource (kCFAllocatorDefault, tap, 0);
        CFRunLoopAddSource (CFRunLoopGetMain(), tapSrc, kCFRunLoopCommonModes);
        CGEventTapEnable (tap, true);
        if (observer == nil)
        {
            observer = [[TTObserver alloc] init];
            [[NSWorkspace sharedWorkspace].notificationCenter addObserver: observer selector: @selector (activated:)
                                                                      name: NSWorkspaceDidActivateApplicationNotification object: nil];
        }
        return true;
    }

    void stopWatching()
    {
        if (tap == nullptr) return;
        CGEventTapEnable (tap, false);
        CFRunLoopRemoveSource (CFRunLoopGetMain(), tapSrc, kCFRunLoopCommonModes);
        CFRelease (tapSrc); tapSrc = nullptr;
        CFRelease (tap); tap = nullptr;
    }

    bool watching() { return tap != nullptr; }

    void setSkip (const std::vector<std::string>& s)
    {
        std::lock_guard<std::mutex> l (skipLock);
        skip = s;
    }

    void appActivated (NSRunningApplication* app)
    {
        if (tap == nullptr || replaying.load() || app == nil) return;
        Step s;
        s.kind = App;
        s.t = nowSec();
        s.bundle = str (app.bundleIdentifier);
        s.key = "app:" + s.bundle;
        s.label = str (app.localizedName) + " を前に";
        dispatch_async (stepQueue(), ^{ deliver (s); });
    }

    //==========================================================================
    bool inputMonitoringTrusted (bool prompt)
    {
        if (CGPreflightListenEventAccess()) return true;
        if (prompt) CGRequestListenEventAccess();
        return false;
    }

    bool accessibilityTrusted (bool prompt)
    {
        NSDictionary* o = @{ (__bridge NSString*) kAXTrustedCheckOptionPrompt: @(prompt) };
        return AXIsProcessTrustedWithOptions ((__bridge CFDictionaryRef) o) == YES;
    }

    void openPrivacy (const char* pane)
    {
        NSString* u = [NSString stringWithFormat: @"x-apple.systempreferences:com.apple.preference.security?Privacy_%s", pane];
        [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: u]];
    }

    //==========================================================================
    static std::atomic<bool> runFlag { false }, cancelFlag { false };
    static dispatch_semaphore_t contSem = dispatch_semaphore_create (0);

    static std::string frontBundleNow()
    {
        __block std::string b;
        if ([NSThread isMainThread]) return str ([NSWorkspace sharedWorkspace].frontmostApplication.bundleIdentifier);
        dispatch_sync (dispatch_get_main_queue(), ^{ b = str ([NSWorkspace sharedWorkspace].frontmostApplication.bundleIdentifier); });
        return b;
    }

    static void postKey (int kc, unsigned long long flags)
    {
        CGEventRef d = CGEventCreateKeyboardEvent (nullptr, (CGKeyCode) kc, true);
        CGEventRef u = CGEventCreateKeyboardEvent (nullptr, (CGKeyCode) kc, false);
        CGEventSetFlags (d, (CGEventFlags) flags); CGEventSetFlags (u, (CGEventFlags) flags);
        CGEventPost (kCGHIDEventTap, d); usleep (20000); CGEventPost (kCGHIDEventTap, u);
        CFRelease (d); CFRelease (u);
    }

    static void postClick (CGPoint p)
    {
        CGEventRef m = CGEventCreateMouseEvent (nullptr, kCGEventMouseMoved, p, kCGMouseButtonLeft);
        CGEventRef d = CGEventCreateMouseEvent (nullptr, kCGEventLeftMouseDown, p, kCGMouseButtonLeft);
        CGEventRef u = CGEventCreateMouseEvent (nullptr, kCGEventLeftMouseUp, p, kCGMouseButtonLeft);
        CGEventPost (kCGHIDEventTap, m); usleep (30000);
        CGEventPost (kCGHIDEventTap, d); usleep (40000); CGEventPost (kCGHIDEventTap, u);
        CFRelease (m); CFRelease (d); CFRelease (u);
    }

    // 窓の中から、役割と名前が同じものを探す（幅優先、最大 3000 個）
    static AXUIElementRef findElement (AXUIElementRef root, const std::string& role, const std::string& title)
    {
        std::vector<AXUIElementRef> q { (AXUIElementRef) CFRetain (root) };
        AXUIElementRef found = nullptr;
        size_t i = 0;
        for (; i < q.size() && i < 3000 && found == nullptr; ++i)
        {
            AXUIElementRef el = q[i];
            if (axStr (el, kAXRoleAttribute) == role && nameOf (el) == title) { found = (AXUIElementRef) CFRetain (el); break; }
            if (CFTypeRef kids = axCopy (el, kAXChildrenAttribute))
            {
                if (CFGetTypeID (kids) == CFArrayGetTypeID())
                    for (id k in (__bridge NSArray*) kids) q.push_back ((AXUIElementRef) CFRetain ((__bridge AXUIElementRef) k));
                CFRelease (kids);
            }
        }
        for (auto* e : q) CFRelease (e);
        return found;
    }

    static bool doClick (const Step& s)
    {
        NSArray* apps = [NSRunningApplication runningApplicationsWithBundleIdentifier: ns (s.bundle)];
        if (apps.count == 0) return false;
        AXUIElementRef app = AXUIElementCreateApplication (((NSRunningApplication*) apps.firstObject).processIdentifier);
        AXUIElementSetMessagingTimeout (app, 1.0f);
        bool ok = false;
        // 名前で探す: 前の窓 → メニューバー
        if (! s.title.empty())
        {
            AXUIElementRef el = nullptr;
            if (CFTypeRef w = axCopy (app, kAXFocusedWindowAttribute)) { el = findElement ((AXUIElementRef) w, s.role, s.title); CFRelease (w); }
            if (el == nullptr) if (CFTypeRef mb = axCopy (app, kAXMenuBarAttribute)) { el = findElement ((AXUIElementRef) mb, s.role, s.title); CFRelease (mb); }
            if (el != nullptr)
            {
                const bool field = s.role == "AXTextField" || s.role == "AXComboBox" || s.role == "AXSearchField" || s.role == "AXTextArea";
                if (field) ok = AXUIElementSetAttributeValue (el, kAXFocusedAttribute, kCFBooleanTrue) == kAXErrorSuccess;
                if (! ok) ok = AXUIElementPerformAction (el, kAXPressAction) == kAXErrorSuccess;
                if (! ok)
                {
                    CGRect f;
                    if (frameOf (el, f)) { postClick (CGPointMake (CGRectGetMidX (f), CGRectGetMidY (f))); ok = true; }
                }
                CFRelease (el);
            }
        }
        // 見つからなければ、窓の中の同じ位置をクリック
        if (! ok && s.relX >= 0)
            if (CFTypeRef w = axCopy (app, kAXFocusedWindowAttribute))
            {
                CGRect f;
                if (frameOf ((AXUIElementRef) w, f)) { postClick (CGPointMake (f.origin.x + s.relX * f.size.width, f.origin.y + s.relY * f.size.height)); ok = true; }
                CFRelease (w);
            }
        CFRelease (app);
        return ok;
    }

    void run (const std::vector<Step>& steps, std::function<void (int, const std::string&)> progress,
              std::function<void (bool, const std::string&)> done)
    {
        if (runFlag.exchange (true)) return;
        cancelFlag = false;
        while (dispatch_semaphore_wait (contSem, DISPATCH_TIME_NOW) == 0) {}
        auto* pf = new std::function<void (int, const std::string&)> (std::move (progress));
        auto* df = new std::function<void (bool, const std::string&)> (std::move (done));
        const auto S = steps;
        dispatch_async (dispatch_get_global_queue (QOS_CLASS_USER_INITIATED, 0), ^{
            @autoreleasepool
            {
                replaying = true;
                auto prog = [pf] (int i, std::string st) { dispatch_async (dispatch_get_main_queue(), ^{ (*pf) (i, st); }); };
                bool ok = true; std::string why;
                for (int i = 0; i < (int) S.size() && ok; ++i)
                {
                    if (cancelFlag) { ok = false; why = "cancel"; break; }
                    const auto& s = S[(size_t) i];
                    prog (i, "run");
                    if (s.kind == App)
                    {
                        dispatch_sync (dispatch_get_main_queue(), ^{
                            NSArray* a = [NSRunningApplication runningApplicationsWithBundleIdentifier: ns (s.bundle)];
                            if (a.count > 0) { [(NSRunningApplication*) a.firstObject unhide]; [(NSRunningApplication*) a.firstObject activateWithOptions: NSApplicationActivateIgnoringOtherApps]; }
                            else if (NSURL* u = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: ns (s.bundle)])
                                [[NSWorkspace sharedWorkspace] openApplicationAtURL: u configuration: [NSWorkspaceOpenConfiguration configuration] completionHandler: nil];
                        });
                        bool front = false;
                        for (int k = 0; k < 50 && ! front; ++k) { usleep (100000); front = frontBundleNow() == s.bundle; }
                        if (! front) { ok = false; why = "app:" + s.bundle; }
                        usleep (250000);
                    }
                    else if (s.kind == Key) { postKey (s.keyCode, s.flags); usleep (150000); }
                    else if (s.kind == Click) { if (! doClick (s)) { ok = false; why = "click:" + s.title; } usleep (250000); }
                    else if (s.kind == Type)
                    {
                        // 文字は覚えていないので、ここだけは自分で入力してもらう
                        replaying = false;
                        prog (i, "wait");
                        const long r = dispatch_semaphore_wait (contSem, dispatch_time (DISPATCH_TIME_NOW, (int64_t) 90 * NSEC_PER_SEC));
                        replaying = true;
                        if (r != 0) { ok = false; why = "timeout"; }
                        if (cancelFlag) { ok = false; why = "cancel"; }
                        usleep (150000);
                    }
                }
                replaying = false;
                runFlag = false;
                dispatch_async (dispatch_get_main_queue(), ^{ (*df) (ok, why); delete df; delete pf; });
            }
        });
    }

    void continueRun() { dispatch_semaphore_signal (contSem); }
    void cancelRun() { cancelFlag = true; dispatch_semaphore_signal (contSem); }
    bool running() { return runFlag.load(); }

    //==========================================================================
    static std::function<void()> hotKeyFns[10];
    static EventHotKeyRef hotKeyRefs[10] = {};

    static OSStatus hotKeyHandler (EventHandlerCallRef, EventRef ev, void*)
    {
        EventHotKeyID hid {};
        GetEventParameter (ev, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof (hid), nullptr, &hid);
        const int slot = (int) hid.id - 1;
        if (slot >= 0 && slot < 10) dispatch_async (dispatch_get_main_queue(), ^{ if (hotKeyFns[slot]) hotKeyFns[slot](); });
        return noErr;
    }

    bool registerHotKey (int slot, int keyCode, int mods, std::function<void()> fn)
    {
        if (slot < 0 || slot >= 10) return false;
        static bool installed = false;
        if (! installed)
        {
            EventTypeSpec spec { kEventClassKeyboard, kEventHotKeyPressed };
            InstallApplicationEventHandler (&hotKeyHandler, 1, &spec, nullptr, nullptr);
            installed = true;
        }
        if (hotKeyRefs[slot] != nullptr) { UnregisterEventHotKey (hotKeyRefs[slot]); hotKeyRefs[slot] = nullptr; }
        hotKeyFns[slot] = std::move (fn);
        if (keyCode < 0) return true;
        EventHotKeyID hid { 'ThTm', (UInt32) slot + 1 };
        return RegisterEventHotKey ((UInt32) keyCode, (UInt32) mods, hid, GetApplicationEventTarget(), 0, &hotKeyRefs[slot]) == noErr;
    }

    int keyCodeForDigit (int d)
    {
        static const int k[] { kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4, kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9 };
        return d >= 0 && d <= 9 ? k[d] : -1;
    }
    int modsCtrlOpt() { return controlKey | optionKey; }
    int keyCodeT() { return kVK_ANSI_T; }

    //==========================================================================
    static NSStatusItem* statusItem()
    {
        static NSStatusItem* item = nil;
        if (item == nil)
        {
            item = [[[NSStatusBar systemStatusBar] statusItemWithLength: NSVariableStatusItemLength] retain];
            item.button.toolTip = @"Third Time";
        }
        return item;
    }

    void setStatusIcon (int state)
    {
        NSStatusItem* item = statusItem();
        NSString* name = state == 0 ? @"repeat.circle" : state == 2 ? @"3.circle.fill" : state == 3 ? @"play.circle.fill" : @"3.circle";
        if (NSImage* img = [NSImage imageWithSystemSymbolName: name accessibilityDescription: @"Third Time"])
        {
            [img setTemplate: YES];
            item.button.image = img;
            item.button.title = @"";
            item.button.contentTintColor = state == 2 ? [NSColor systemOrangeColor] : state == 3 ? [NSColor systemGreenColor] : nil;
            item.button.appearsDisabled = state == 0;
        }
        else item.button.title = @"3x";
    }

    void setStatusMenu (const std::vector<std::pair<std::string, int>>& items, std::function<void (int)> onPick)
    {
        if (observer == nil) observer = [[TTObserver alloc] init];
        observer->onPick = std::move (onPick);
        NSMenu* menu = [[NSMenu alloc] initWithTitle: @"Third Time"];
        menu.autoenablesItems = NO;
        for (auto& [title, tag] : items)
        {
            if (tag == 0) { [menu addItem: [NSMenuItem separatorItem]]; continue; }
            NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle: ns (title) action: @selector (pick:) keyEquivalent: @""];
            mi.target = observer;
            mi.tag = tag;
            if (tag < 0) mi.enabled = NO;
            [menu addItem: mi];
            TT_RELEASE (mi);
        }
        statusItem().menu = menu;
        TT_RELEASE (menu);
    }

    void makeHud (void* nsView)
    {
        NSWindow* w = [(__bridge NSView*) nsView window];
        if (w == nil) return;
        w.level = NSStatusWindowLevel;
        w.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary
                             | NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle;
        w.hasShadow = YES;
        [w orderFrontRegardless];
    }

    void hudPosition (int width, int& x, int& y)
    {
        NSScreen* sc = [NSScreen mainScreen];
        if (sc == nil) { x = 400; y = 40; return; }
        const NSRect f = sc.frame, vf = sc.visibleFrame;
        NSScreen* primary = [NSScreen screens].firstObject;
        const double topY = primary.frame.size.height - (vf.origin.y + vf.size.height);
        x = (int) (f.origin.x + f.size.width - width - 16);
        y = (int) topY + 10;
    }

    void showWindow (void* nsView)
    {
        NSWindow* w = [(__bridge NSView*) nsView window];
        if (w == nil) return;
        [NSApp activateIgnoringOtherApps: YES];
        [w makeKeyAndOrderFront: nil];
    }

    bool launchAtLogin()
    {
        if (@available (macOS 13.0, *)) return [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
        return false;
    }

    bool setLaunchAtLogin (bool on)
    {
        if (@available (macOS 13.0, *))
        {
            NSError* err = nil;
            SMAppService* s = [SMAppService mainAppService];
            return (on ? [s registerAndReturnError: &err] : [s unregisterAndReturnError: &err]) == YES;
        }
        return false;
    }
}

@implementation TTObserver
- (void) activated: (NSNotification*) n
{
    third::mac::appActivated (n.userInfo[NSWorkspaceApplicationKey]);
}
- (void) pick: (id) sender { if (onPick) onPick ((int) [(NSMenuItem*) sender tag]); }
@end
