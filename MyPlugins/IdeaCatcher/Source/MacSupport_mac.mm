#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
#import <UserNotifications/UserNotifications.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreAudio/CoreAudio.h>
#include <unistd.h>
#import <objc/runtime.h>
#include "MacSupport.h"

@interface IdeaStatusTarget : NSObject
{
@public
    std::function<void (int)> onPick;
}
- (void) pick: (id) sender;
@end

@implementation IdeaStatusTarget
- (void) pick: (id) sender
{
    if (onPick) onPick ((int) [(NSMenuItem*) sender tag]);
}
@end

@interface IdeaCommandListener : NSObject
{
@public
    std::function<void (const std::string&)> onCommand;
}
- (void) got: (NSNotification*) n;
@end

@implementation IdeaCommandListener
- (void) got: (NSNotification*) n
{
    NSString* s = [n object];
    if (onCommand && s != nil) onCommand ([s UTF8String]);
}
@end

@interface IdeaNoteDelegate : NSObject <UNUserNotificationCenterDelegate>
{
@public
    std::function<void (const std::string&)> onClick;
}
@end

@implementation IdeaNoteDelegate
- (void) userNotificationCenter: (UNUserNotificationCenter*) c willPresentNotification: (UNNotification*) n
          withCompletionHandler: (void (^)(UNNotificationPresentationOptions)) handler API_AVAILABLE(macos(10.14))
{
    if (@available (macOS 11.0, *)) handler (UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
    else handler (UNNotificationPresentationOptionAlert);
}
- (void) userNotificationCenter: (UNUserNotificationCenter*) c didReceiveNotificationResponse: (UNNotificationResponse*) r
          withCompletionHandler: (void (^)(void)) handler API_AVAILABLE(macos(10.14))
{
    NSString* ident = r.notification.request.content.userInfo[@"id"];
    if (onClick && ident != nil) { auto s = std::string ([ident UTF8String]); dispatch_async (dispatch_get_main_queue(), ^{ if (onClick) onClick (s); }); }
    handler();
}
@end

namespace mac
{
    void requestMic (std::function<void (bool)> done)
    {
        const auto st = [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio];
        if (st == AVAuthorizationStatusAuthorized) { done (true); return; }
        if (st == AVAuthorizationStatusDenied || st == AVAuthorizationStatusRestricted) { done (false); return; }
        auto* copy = new std::function<void (bool)> (std::move (done));
        [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio completionHandler: ^(BOOL granted) {
            dispatch_async (dispatch_get_main_queue(), ^{ (*copy) (granted == YES); delete copy; });
        }];
    }

    static NSStatusItem* statusItem()
    {
        static NSStatusItem* item = nil;
        if (item == nil)
        {
            item = [[NSStatusBar systemStatusBar] statusItemWithLength: NSVariableStatusItemLength];
           #if ! __has_feature(objc_arc)
            [item retain];
           #endif
            item.button.toolTip = @"Idea Catcher";
        }
        return item;
    }

    void setStatusIcon (int state)
    {
        NSStatusItem* item = statusItem();
        NSString* name = state == 1 ? @"record.circle.fill" : state == 2 ? @"waveform.circle" : @"lightbulb";
        if (NSImage* img = [NSImage imageWithSystemSymbolName: name accessibilityDescription: @"Idea Catcher"])
        {
            [img setTemplate: YES];
            item.button.image = img;
            item.button.contentTintColor = state == 1 ? [NSColor systemRedColor] : nil;   // 録音中だけ赤く
            item.button.title = @"";
        }
        else
            item.button.title = state == 1 ? @"● REC" : @"Idea";
    }

    void setStatusMenu (const std::vector<std::pair<std::string, int>>& items, std::function<void (int)> onPick)
    {
        static IdeaStatusTarget* target = nil;
        if (target == nil) target = [[IdeaStatusTarget alloc] init];
        target->onPick = std::move (onPick);

        NSMenu* menu = [[NSMenu alloc] initWithTitle: @"Idea Catcher"];
        for (auto& [title, tag] : items)
        {
            if (tag == 0) { [menu addItem: [NSMenuItem separatorItem]]; continue; }
            NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle: [NSString stringWithUTF8String: title.c_str()]
                                                        action: @selector (pick:) keyEquivalent: @""];
            mi.target = target;
            mi.tag = tag;
            [menu addItem: mi];
           #if ! __has_feature(objc_arc)
            [mi release];
           #endif
        }
        statusItem().menu = menu;
       #if ! __has_feature(objc_arc)
        [menu release];
       #endif
    }

    //==========================================================================
    const std::vector<HotKeyChoice>& hotKeyChoices()
    {
        static const std::vector<HotKeyChoice> c {
            { "⌃⌥I",      kVK_ANSI_I, controlKey | optionKey },
            { "⌃⌥Space",  kVK_Space,  controlKey | optionKey },
            { "⌃⌥⌘R",     kVK_ANSI_R, controlKey | optionKey | cmdKey },
            { "F13",      kVK_F13,    0 },
            { "⇧⌘0",      kVK_ANSI_0, shiftKey | cmdKey },
        };
        return c;
    }

    static std::function<void()> hotKeyFns[4];
    static EventHotKeyRef hotKeyRefs[4] = {};

    static OSStatus hotKeyHandler (EventHandlerCallRef, EventRef ev, void*)
    {
        EventHotKeyID hid {};
        GetEventParameter (ev, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof (hid), nullptr, &hid);
        const int slot = (int) hid.id - 1;
        if (slot >= 0 && slot < 4)
            dispatch_async (dispatch_get_main_queue(), ^{ if (hotKeyFns[slot]) hotKeyFns[slot](); });
        return noErr;
    }

    static bool registerSlot (int slot, int keyCode, int mods, std::function<void()> fn)
    {
        static bool handlerInstalled = false;
        if (! handlerInstalled)
        {
            EventTypeSpec spec { kEventClassKeyboard, kEventHotKeyPressed };
            InstallApplicationEventHandler (&hotKeyHandler, 1, &spec, nullptr, nullptr);
            handlerInstalled = true;
        }
        if (hotKeyRefs[slot] != nullptr) { UnregisterEventHotKey (hotKeyRefs[slot]); hotKeyRefs[slot] = nullptr; }
        hotKeyFns[slot] = std::move (fn);
        if (keyCode < 0) return true;
        EventHotKeyID hid { 'IdCt', (UInt32) slot + 1 };
        return RegisterEventHotKey ((UInt32) keyCode, (UInt32) mods, hid, GetApplicationEventTarget(), 0, &hotKeyRefs[slot]) == noErr;
    }

    bool registerHotKey (int choice, std::function<void()> onPress)
    {
        const auto& c = hotKeyChoices();
        if (choice < 0 || choice >= (int) c.size()) return false;
        return registerSlot (0, c[(size_t) choice].keyCode, c[(size_t) choice].mods, std::move (onPress));
    }

    bool registerExtraHotKey (int slot, int keyCode, int mods, std::function<void()> onPress)
    {
        if (slot < 1 || slot > 3) return false;
        return registerSlot (slot, keyCode, mods, std::move (onPress));
    }

    void listenCommands (std::function<void (const std::string&)> onCommand)
    {
        static IdeaCommandListener* l = nil;
        if (l == nil)
        {
            l = [[IdeaCommandListener alloc] init];
            [[NSDistributedNotificationCenter defaultCenter] addObserver: l selector: @selector (got:)
                                                                    name: @"com.myplugins.ideacatcher.command" object: nil];
        }
        l->onCommand = std::move (onCommand);
    }

    //==========================================================================
    static NSURL* logicURL()
    {
        NSWorkspace* ws = [NSWorkspace sharedWorkspace];
        return [ws URLForApplicationWithBundleIdentifier: @"com.apple.logic10"];
    }

    bool logicInstalled() { return logicURL() != nil; }

    bool openInLogic (const std::string& path)
    {
        NSURL* file = [NSURL fileURLWithPath: [NSString stringWithUTF8String: path.c_str()]];
        NSWorkspace* ws = [NSWorkspace sharedWorkspace];
        NSURL* app = logicURL();
        if (app == nil) return [ws openURL: file];
        NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
        cfg.activates = YES;
        [ws openURLs: @[ file ] withApplicationAtURL: app configuration: cfg completionHandler: nil];
        return true;
    }

    bool launchAtLogin()
    {
        if (@available (macOS 13.0, *))
            return [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
        return false;
    }

    bool setLaunchAtLogin (bool on)
    {
        if (@available (macOS 13.0, *))
        {
            NSError* err = nil;
            SMAppService* s = [SMAppService mainAppService];
            const BOOL ok = on ? [s registerAndReturnError: &err] : [s unregisterAndReturnError: &err];
            return ok == YES;
        }
        return false;
    }

    void showWindow (void* nsView, bool floating, bool focus)
    {
        NSView* v = (__bridge NSView*) nsView;
        NSWindow* w = [v window];
        if (w == nil) return;
        w.level = floating ? NSFloatingWindowLevel : NSNormalWindowLevel;
        w.collectionBehavior = w.collectionBehavior | NSWindowCollectionBehaviorCanJoinAllSpaces
                                                    | NSWindowCollectionBehaviorFullScreenAuxiliary;
        if (focus)
        {
            [NSApp activateIgnoringOtherApps: YES];
            [w makeKeyAndOrderFront: nil];
        }
        else
            [w orderFrontRegardless];
    }

    //==========================================================================
    // JUCE のアプリの代理役（NSApp の delegate）は「もう一度開く」を受けないので、あとから足す
    static std::function<void()> reopenFn;
    static BOOL ideaShouldHandleReopen (id, SEL, NSApplication*, BOOL)
    {
        if (reopenFn) reopenFn();
        return NO;
    }

    void onReopen (std::function<void()> fn)
    {
        reopenFn = std::move (fn);
        id d = [NSApp delegate];
        if (d == nil) return;
        Class c = object_getClass (d);
        SEL sel = @selector (applicationShouldHandleReopen:hasVisibleWindows:);
        if (! class_addMethod (c, sel, (IMP) ideaShouldHandleReopen, "c@:@c"))
            class_replaceMethod (c, sel, (IMP) ideaShouldHandleReopen, "c@:@c");
    }

    //==========================================================================
    void makeOverlay (void* nsView)
    {
        NSView* v = (__bridge NSView*) nsView;
        NSWindow* w = [v window];
        if (w == nil) return;
        w.ignoresMouseEvents = YES;
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
        const double topY = primary.frame.size.height - (vf.origin.y + vf.size.height);   // 左上原点に直す
        x = (int) (f.origin.x + f.size.width / 2 - width / 2);
        y = (int) topY + 10;
    }

    //==========================================================================
    static IdeaNoteDelegate* noteDelegate()
    {
        static IdeaNoteDelegate* d = nil;
        if (d == nil)
        {
            d = [[IdeaNoteDelegate alloc] init];
            [UNUserNotificationCenter currentNotificationCenter].delegate = d;
        }
        return d;
    }

    void setNotificationHandler (std::function<void (const std::string&)> onClick)
    {
        noteDelegate()->onClick = std::move (onClick);
    }

    void notify (const std::string& title, const std::string& body, const std::string& ident)
    {
        noteDelegate();
        UNUserNotificationCenter* c = [UNUserNotificationCenter currentNotificationCenter];
        NSString* t = [NSString stringWithUTF8String: title.c_str()];
        NSString* b = [NSString stringWithUTF8String: body.c_str()];
        NSString* i = [NSString stringWithUTF8String: ident.c_str()];
        [c requestAuthorizationWithOptions: (UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                         completionHandler: ^(BOOL granted, NSError*) {
            if (! granted) return;
            UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
            content.title = t;
            content.body = b;
            content.userInfo = @{ @"id": i };
            UNNotificationRequest* req = [UNNotificationRequest requestWithIdentifier: [[NSUUID UUID] UUIDString]
                                                                              content: content trigger: nil];
            [c addNotificationRequest: req withCompletionHandler: nil];
        }];
    }

    //==========================================================================
    std::string builtInMicName()
    {
        AudioObjectPropertyAddress addr { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr) return {};
        std::vector<AudioDeviceID> ids (size / sizeof (AudioDeviceID));
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &addr, 0, nullptr, &size, ids.data()) != noErr) return {};
        for (auto id : ids)
        {
            UInt32 transport = 0, ts = sizeof (transport);
            AudioObjectPropertyAddress ta { kAudioDevicePropertyTransportType, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
            if (AudioObjectGetPropertyData (id, &ta, 0, nullptr, &ts, &transport) != noErr || transport != kAudioDeviceTransportTypeBuiltIn) continue;
            AudioObjectPropertyAddress sa { kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput, kAudioObjectPropertyElementMain };
            UInt32 ss = 0;
            if (AudioObjectGetPropertyDataSize (id, &sa, 0, nullptr, &ss) != noErr || ss == 0) continue;   // 入力がない（スピーカー）
            CFStringRef name = nullptr; UInt32 ns = sizeof (name);
            AudioObjectPropertyAddress na { kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
            if (AudioObjectGetPropertyData (id, &na, 0, nullptr, &ns, &name) != noErr || name == nullptr) continue;
            std::string out = [(__bridge NSString*) name UTF8String] ?: "";
            CFRelease (name);
            if (! out.empty()) return out;
        }
        return {};
    }

    bool accessibilityTrusted (bool prompt)
    {
        NSDictionary* o = @{ (__bridge NSString*) kAXTrustedCheckOptionPrompt: @(prompt) };
        return AXIsProcessTrustedWithOptions ((__bridge CFDictionaryRef) o) == YES;
    }

    static NSRunningApplication* logicApp()
    {
        NSArray* a = [NSRunningApplication runningApplicationsWithBundleIdentifier: @"com.apple.logic10"];
        return a.count > 0 ? a.firstObject : nil;
    }

    static CFTypeRef axCopy (AXUIElementRef el, CFStringRef attr)
    {
        CFTypeRef v = nullptr;
        if (el == nullptr || AXUIElementCopyAttributeValue (el, attr, &v) != kAXErrorSuccess) return nullptr;
        return v;
    }

    static std::string axTitle (AXUIElementRef el)
    {
        std::string out;
        if (CFTypeRef t = axCopy (el, kAXTitleAttribute))
        {
            if (CFGetTypeID (t) == CFStringGetTypeID()) out = [(__bridge NSString*) t UTF8String] ?: "";
            CFRelease (t);
        }
        return out;
    }

    std::vector<std::string> logicWindowTitles()
    {
        std::vector<std::string> out;
        NSRunningApplication* app = logicApp();
        if (app == nil) return out;
        AXUIElementRef ax = AXUIElementCreateApplication (app.processIdentifier);
        if (CFTypeRef ws = axCopy (ax, kAXWindowsAttribute))
        {
            for (id w in (__bridge NSArray*) ws) out.push_back (axTitle ((__bridge AXUIElementRef) w));
            CFRelease (ws);
        }
        CFRelease (ax);
        return out;
    }

    // キーを Logic にだけ送る（ほかのアプリに打ち込まないように）
    static void postKey (pid_t pid, CGKeyCode key, CGEventFlags flags)
    {
        CGEventRef d = CGEventCreateKeyboardEvent (nullptr, key, true);
        CGEventRef u = CGEventCreateKeyboardEvent (nullptr, key, false);
        CGEventSetFlags (d, flags); CGEventSetFlags (u, flags);
        CGEventPostToPid (pid, d); usleep (15000); CGEventPostToPid (pid, u);
        CFRelease (d); CFRelease (u);
        usleep (60000);
    }

    static void postText (pid_t pid, NSString* text)
    {
        for (NSUInteger i = 0; i < text.length; ++i)
        {
            UniChar ch = [text characterAtIndex: i];
            CGEventRef d = CGEventCreateKeyboardEvent (nullptr, 0, true);
            CGEventRef u = CGEventCreateKeyboardEvent (nullptr, 0, false);
            CGEventKeyboardSetUnicodeString (d, 1, &ch); CGEventKeyboardSetUnicodeString (u, 1, &ch);
            CGEventSetFlags (d, 0); CGEventSetFlags (u, 0);
            CGEventPostToPid (pid, d); CGEventPostToPid (pid, u);
            CFRelease (d); CFRelease (u);
            usleep (8000);
        }
    }

    // メニューの中から ⇧⌘I（「オーディオファイルを読み込む」）を探す
    static AXUIElementRef findMenuItem (AXUIElementRef el, int depth)
    {
        if (depth > 5) return nullptr;
        CFTypeRef kids = axCopy (el, kAXChildrenAttribute);
        if (kids == nullptr) return nullptr;
        AXUIElementRef found = nullptr;
        for (id k in (__bridge NSArray*) kids)
        {
            AXUIElementRef c = (__bridge AXUIElementRef) k;
            CFTypeRef ch = axCopy (c, kAXMenuItemCmdCharAttribute);
            CFTypeRef md = axCopy (c, kAXMenuItemCmdModifiersAttribute);
            bool hit = false;
            if (ch != nullptr && CFGetTypeID (ch) == CFStringGetTypeID() && md != nullptr && CFGetTypeID (md) == CFNumberGetTypeID())
            {
                int m = 0; CFNumberGetValue ((CFNumberRef) md, kCFNumberIntType, &m);
                hit = [[(__bridge NSString*) ch uppercaseString] isEqualToString: @"I"] && m == (int) kAXMenuItemModifierShift;
            }
            if (ch) CFRelease (ch);
            if (md) CFRelease (md);
            if (hit) { found = (AXUIElementRef) CFRetain (c); break; }
            if ((found = findMenuItem (c, depth + 1)) != nullptr) break;
        }
        CFRelease (kids);
        return found;
    }

    static AXUIElementRef focusedWindow (AXUIElementRef app)
    {
        return (AXUIElementRef) axCopy (app, kAXFocusedWindowAttribute);
    }

    static bool hasSheet (AXUIElementRef w)
    {
        bool r = false;
        if (CFTypeRef kids = axCopy (w, kAXChildrenAttribute))
        {
            for (id k in (__bridge NSArray*) kids)
            {
                CFTypeRef role = axCopy ((__bridge AXUIElementRef) k, kAXRoleAttribute);
                if (role != nullptr) { r = r || [(__bridge NSString*) role isEqualToString: (__bridge NSString*) kAXSheetRole]; CFRelease (role); }
            }
            CFRelease (kids);
        }
        return r;
    }

    void importAudioIntoLogic (const std::string& audioPath, const std::string& projectName, const std::vector<std::string>& windowsBefore,
                               std::function<void (bool, const std::string&)> done)
    {
        const std::string want = projectName;
        auto* cb = new std::function<void (bool, const std::string&)> (std::move (done));
        NSString* path = [NSString stringWithUTF8String: audioPath.c_str()];
        const auto before = windowsBefore;
        dispatch_async (dispatch_get_global_queue (QOS_CLASS_USER_INITIATED, 0), ^{
            auto finish = [cb] (bool ok, std::string why)
            {
                dispatch_async (dispatch_get_main_queue(), ^{ (*cb) (ok, why); delete cb; });
            };
            if (! AXIsProcessTrusted()) { finish (false, "accessibility"); return; }

            // 1. 新しいプロジェクトの窓が出るまで待つ（Logic の起動から数えて最大 60 秒）
            pid_t pid = 0; AXUIElementRef ax = nullptr, proj = nullptr; std::string projTitle;
            for (int i = 0; i < 240 && proj == nullptr; ++i)
            {
                usleep (250000);
                NSRunningApplication* app = logicApp();
                if (app == nil) continue;
                if (ax == nullptr) { pid = app.processIdentifier; ax = AXUIElementCreateApplication (pid); }
                CFTypeRef ws = axCopy (ax, kAXWindowsAttribute);
                if (ws == nullptr) continue;
                for (id w in (__bridge NSArray*) ws)
                {
                    const auto t = axTitle ((__bridge AXUIElementRef) w);
                    if (t.empty() || std::find (before.begin(), before.end(), t) != before.end()) continue;
                    const bool named = t.find (want) != std::string::npos || t.find ("Untitled") != std::string::npos
                                    || t.find ("名称未設定") != std::string::npos;
                    if (! named) continue;   // 最初の選択画面などは相手にしない
                    proj = (AXUIElementRef) CFRetain ((__bridge AXUIElementRef) w);
                    projTitle = t;
                    break;
                }
                CFRelease (ws);
            }
            if (proj == nullptr) { if (ax) CFRelease (ax); finish (false, "noproject"); return; }
            usleep (2500000);   // 読み込みが落ち着くまで

            // 2. Logic とその窓を前へ
            auto front = [&]
            {
                __block bool isFront = false;
                dispatch_sync (dispatch_get_main_queue(), ^{
                    NSRunningApplication* app = logicApp();
                    if (app == nil) return;
                    if (! app.active) [app activateWithOptions: NSApplicationActivateIgnoringOtherApps];
                });
                AXUIElementPerformAction (proj, kAXRaiseAction);
                AXUIElementSetAttributeValue (proj, kAXMainAttribute, kCFBooleanTrue);
                for (int i = 0; i < 20 && ! isFront; ++i)
                {
                    usleep (100000);
                    dispatch_sync (dispatch_get_main_queue(), ^{
                        isFront = [[NSWorkspace sharedWorkspace].frontmostApplication.bundleIdentifier isEqualToString: @"com.apple.logic10"];
                    });
                }
                return isFront;
            };
            auto bail = [&] (const char* why) { CFRelease (proj); CFRelease (ax); finish (false, why); };
            if (! front()) { bail ("notfront"); return; }
            usleep (300000);
            postKey (pid, (CGKeyCode) kVK_Return, 0);   // 再生位置を先頭へ（Logic の初期キー）
            usleep (300000);

            // 3. 「オーディオファイルを読み込む」（⇧⌘I）をメニューから押す。見つからなければキーで
            bool pressed = false;
            if (CFTypeRef bar = axCopy (ax, kAXMenuBarAttribute))
            {
                if (AXUIElementRef item = findMenuItem ((AXUIElementRef) bar, 0))
                {
                    pressed = AXUIElementPerformAction (item, kAXPressAction) == kAXErrorSuccess;
                    CFRelease (item);
                }
                CFRelease (bar);
            }
            if (! pressed) postKey (pid, (CGKeyCode) kVK_ANSI_I, kCGEventFlagMaskCommand | kCGEventFlagMaskShift);

            // 4. ファイルを選ぶ画面が出るのを待つ
            bool panel = false;
            for (int i = 0; i < 50 && ! panel; ++i)
            {
                usleep (150000);
                if (AXUIElementRef fw = focusedWindow (ax))
                {
                    panel = axTitle (fw) != projTitle || hasSheet (fw);
                    CFRelease (fw);
                }
                if (! panel) panel = hasSheet (proj);
            }
            if (! panel) { bail ("nopanel"); return; }
            usleep (500000);
            if (! front()) { bail ("notfront"); return; }

            // 5. 場所を入れて開く（⇧⌘G → パス → Return → Return）
            postKey (pid, (CGKeyCode) kVK_ANSI_G, kCGEventFlagMaskCommand | kCGEventFlagMaskShift);
            usleep (900000);
            postText (pid, path);
            usleep (600000);
            postKey (pid, (CGKeyCode) kVK_Return, 0);
            usleep (1200000);
            postKey (pid, (CGKeyCode) kVK_Return, 0);

            // 6. 画面が閉じたら終わり
            bool closed = false;
            for (int i = 0; i < 60 && ! closed; ++i)
            {
                usleep (150000);
                AXUIElementRef fw = focusedWindow (ax);
                closed = fw != nullptr && axTitle (fw) == projTitle && ! hasSheet (fw);
                if (fw) CFRelease (fw);
            }
            CFRelease (proj); CFRelease (ax);
            finish (closed, closed ? "" : "stillopen");
        });
    }
}

