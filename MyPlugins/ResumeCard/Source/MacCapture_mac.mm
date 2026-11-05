#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <ImageIO/ImageIO.h>
#include <algorithm>
#include "MacCapture.h"

// ARC なしでビルドする（JUCE の標準）。作ったものは自分で release する
#if __has_feature(objc_arc)
 #define RC_RELEASE(x)
#else
 #define RC_RELEASE(x) [x release]
#endif

@interface RCTarget : NSObject
{
@public
    std::function<void (int)> onPick;
    std::function<void (int, const std::string&)> onAway;
}
- (void) pick: (id) sender;
- (void) away: (NSNotification*) n;
- (void) back: (NSNotification*) n;
@end

@implementation RCTarget
- (void) pick: (id) sender { if (onPick) onPick ((int) [(NSMenuItem*) sender tag]); }
- (void) away: (NSNotification*) n { if (onAway) onAway (1, [[n name] UTF8String] ?: ""); }
- (void) back: (NSNotification*) n { if (onAway) onAway (2, [[n name] UTF8String] ?: ""); }
@end

namespace rc
{
    static std::string str (NSString* s) { return s != nil && [s UTF8String] != nullptr ? std::string ([s UTF8String]) : std::string(); }
    static NSString* ns (const std::string& s) { return [NSString stringWithUTF8String: s.c_str()] ?: @""; }

    static CFTypeRef axCopy (AXUIElementRef el, CFStringRef attr)
    {
        CFTypeRef v = nullptr;
        if (el == nullptr || AXUIElementCopyAttributeValue (el, attr, &v) != kAXErrorSuccess) return nullptr;
        return v;
    }

    static std::string axString (AXUIElementRef el, CFStringRef attr)
    {
        std::string out;
        if (CFTypeRef t = axCopy (el, attr))
        {
            if (CFGetTypeID (t) == CFStringGetTypeID()) out = str ((__bridge NSString*) t);
            else if (CFGetTypeID (t) == CFURLGetTypeID()) out = str ([(__bridge NSURL*) t absoluteString]);
            CFRelease (t);
        }
        return out;
    }

    // "file:///Users/..." → "/Users/..."
    static std::string docPath (const std::string& d)
    {
        if (d.empty()) return {};
        NSURL* u = [NSURL URLWithString: ns (d)];
        if (u != nil && [u isFileURL]) return str ([u path]);
        if (! d.empty() && d[0] == '/') return d;
        return {};
    }

    static AppInfo appInfo (NSRunningApplication* app, bool focusedWindow)
    {
        AppInfo a;
        a.name = str (app.localizedName);
        a.bundleId = str (app.bundleIdentifier);
        a.appPath = str (app.bundleURL.path);
        AXUIElementRef ax = AXUIElementCreateApplication (app.processIdentifier);
        AXUIElementSetMessagingTimeout (ax, 0.8f);
        CFTypeRef w = axCopy (ax, focusedWindow ? kAXFocusedWindowAttribute : kAXMainWindowAttribute);
        if (w == nullptr) w = axCopy (ax, kAXMainWindowAttribute);
        if (w != nullptr)
        {
            a.window = axString ((AXUIElementRef) w, kAXTitleAttribute);
            a.doc = docPath (axString ((AXUIElementRef) w, kAXDocumentAttribute));
            CFRelease (w);
        }
        CFRelease (ax);
        return a;
    }

    // 前のアプリで、カーソルがある欄の「カーソルの前の文」と「選んでいる文字」
    static void focusedText (NSRunningApplication* app, std::string& typed, std::string& selected)
    {
        AXUIElementRef ax = AXUIElementCreateApplication (app.processIdentifier);
        AXUIElementSetMessagingTimeout (ax, 0.8f);
        if (CFTypeRef f = axCopy (ax, kAXFocusedUIElementAttribute))
        {
            AXUIElementRef el = (AXUIElementRef) f;
            const auto sub = axString (el, kAXSubroleAttribute);
            const auto role = axString (el, kAXRoleAttribute);
            const bool secure = sub == str ((__bridge NSString*) kAXSecureTextFieldSubrole);
            const bool texty = role == "AXTextArea" || role == "AXTextField" || role == "AXComboBox" || role == "AXWebArea" || role == "AXStaticText";
            if (! secure && texty)
            {
                CFTypeRef v = axCopy (el, kAXValueAttribute);
                if (v != nullptr && CFGetTypeID (v) == CFStringGetTypeID())
                {
                    NSString* s = (__bridge NSString*) v;
                    NSUInteger caret = s.length;
                    if (CFTypeRef r = axCopy (el, kAXSelectedTextRangeAttribute))
                    {
                        CFRange range;
                        if (CFGetTypeID (r) == AXValueGetTypeID() && AXValueGetValue ((AXValueRef) r, (AXValueType) kAXValueCFRangeType, &range)
                            && range.location >= 0 && (NSUInteger) range.location <= s.length)
                            caret = (NSUInteger) range.location;
                        CFRelease (r);
                    }
                    const NSUInteger from = caret > 400 ? caret - 400 : 0;
                    NSString* before = [s substringWithRange: [s rangeOfComposedCharacterSequencesForRange: NSMakeRange (from, caret - from)]];
                    typed = str ([before stringByTrimmingCharactersInSet: [NSCharacterSet whitespaceAndNewlineCharacterSet]]);
                }
                if (v) CFRelease (v);
                selected = axString (el, kAXSelectedTextAttribute);
                if (selected.size() > 600) selected = selected.substr (0, 600);
            }
            CFRelease (f);
        }
        CFRelease (ax);
    }

    static NSString* runScript (NSString* src)
    {
        NSAppleScript* script = [[NSAppleScript alloc] initWithSource: src];
        NSDictionary* err = nil;
        NSAppleEventDescriptor* r = [script executeAndReturnError: &err];
        NSString* out = r != nil ? [[[r stringValue] copy] autorelease] : nil;
        RC_RELEASE (script);
        return out;
    }

    static bool running (NSString* bundle)
    {
        return [NSRunningApplication runningApplicationsWithBundleIdentifier: bundle].count > 0;
    }

    // 開いているブラウザの、各ウィンドウの前のタブ
    static void browserTabs (std::vector<TabInfo>& tabs)
    {
        struct B { NSString* bundle; NSString* app; NSString* tabExpr; const char* label; };
        const B bs[] { { @"com.google.Chrome", @"Google Chrome", @"active tab of w", "Chrome" },
                       { @"com.apple.Safari",  @"Safari",        @"current tab of w", "Safari" },
                       { @"com.brave.Browser", @"Brave Browser", @"active tab of w", "Brave" },
                       { @"com.microsoft.edgemac", @"Microsoft Edge", @"active tab of w", "Edge" } };
        for (auto& b : bs)
        {
            if (! running (b.bundle)) continue;   // 動いていないブラウザに話しかけると起動してしまう
            NSString* nameExpr = [b.bundle isEqualToString: @"com.apple.Safari"] ? @"name of t" : @"title of t";
            NSString* src = [NSString stringWithFormat:
                @"with timeout of 3 seconds\n"
                 "tell application \"%@\"\n"
                 "set out to \"\"\n"
                 "repeat with w in windows\n"
                 "try\n"
                 "set t to %@\n"
                 "set out to out & (URL of t) & tab & (%@) & linefeed\n"
                 "end try\n"
                 "end repeat\n"
                 "return out\n"
                 "end tell\n"
                 "end timeout", b.app, b.tabExpr, nameExpr];
            NSString* out = runScript (src);
            if (out == nil) continue;
            for (NSString* l in [out componentsSeparatedByString: @"\n"])
            {
                NSRange t = [l rangeOfString: @"\t"];
                if (t.location == NSNotFound || t.location == 0) continue;
                TabInfo ti;
                ti.browser = b.label;
                ti.bundleId = str (b.bundle);
                ti.url = str ([l substringToIndex: t.location]);
                ti.title = str ([l substringFromIndex: t.location + 1]);
                if (ti.url.rfind ("http", 0) == 0 || ti.url.rfind ("file:", 0) == 0) tabs.push_back (ti);
                if (tabs.size() >= 12) return;
            }
        }
    }

    static std::string finderFront()
    {
        if (! running (@"com.apple.finder")) return {};
        NSString* out = runScript (@"with timeout of 2 seconds\n"
                                    "tell application \"Finder\"\n"
                                    "if (count of Finder windows) is 0 then return \"\"\n"
                                    "return POSIX path of (target of front Finder window as alias)\n"
                                    "end tell\n"
                                    "end timeout");
        return str (out);
    }

    //==========================================================================
    // 前のアプリのいちばん手前のウィンドウを写真に（macOS 14 以降、画面収録の許可があるとき）
    static void shoot (pid_t pid, const std::string& path, std::function<void (bool)> done)
    {
        if (@available (macOS 14.0, *))
        {
            if (! CGPreflightScreenCaptureAccess()) { done (false); return; }
            CGWindowID wid = 0;
            if (CFArrayRef list = CGWindowListCopyWindowInfo (kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID))
            {
                for (NSDictionary* d in (__bridge NSArray*) list)
                {
                    if ([d[(id) kCGWindowOwnerPID] intValue] != pid || [d[(id) kCGWindowLayer] intValue] != 0) continue;
                    NSDictionary* b = d[(id) kCGWindowBounds];
                    if ([b[@"Width"] doubleValue] < 120 || [b[@"Height"] doubleValue] < 80) continue;
                    wid = (CGWindowID) [d[(id) kCGWindowNumber] unsignedIntValue];
                    break;
                }
                CFRelease (list);
            }
            if (wid == 0) { done (false); return; }
            auto* cb = new std::function<void (bool)> (std::move (done));
            NSString* p = ns (path);
            [SCShareableContent getShareableContentExcludingDesktopWindows: YES onScreenWindowsOnly: YES
                                                         completionHandler: ^(SCShareableContent* content, NSError* err) {
                SCWindow* target = nil;
                for (SCWindow* w in content.windows) if (w.windowID == wid) { target = w; break; }
                if (target == nil || err != nil) { (*cb) (false); delete cb; return; }
                SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow: target];
                SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
                const double sc = std::min (1.0, 1400.0 / std::max (1.0, (double) target.frame.size.width));
                cfg.width = (size_t) std::max (64.0, target.frame.size.width * sc);
                cfg.height = (size_t) std::max (48.0, target.frame.size.height * sc);
                cfg.showsCursor = NO;
                [SCScreenshotManager captureImageWithFilter: filter configuration: cfg
                                          completionHandler: ^(CGImageRef img, NSError* e2) {
                    bool ok = false;
                    if (img != nullptr && e2 == nil)
                    {
                        NSURL* url = [NSURL fileURLWithPath: p];
                        if (CGImageDestinationRef dst = CGImageDestinationCreateWithURL ((__bridge CFURLRef) url, CFSTR ("public.jpeg"), 1, nullptr))
                        {
                            NSDictionary* o = @{ (id) kCGImageDestinationLossyCompressionQuality: @0.62 };
                            CGImageDestinationAddImage (dst, img, (__bridge CFDictionaryRef) o);
                            ok = CGImageDestinationFinalize (dst);
                            CFRelease (dst);
                        }
                    }
                    (*cb) (ok); delete cb;
                }];
                RC_RELEASE (filter); RC_RELEASE (cfg);
            }];
            return;
        }
        done (false);
    }

    static dispatch_queue_t captureQueue()
    {
        static dispatch_queue_t q = dispatch_queue_create ("com.myplugins.resumecard.capture", DISPATCH_QUEUE_SERIAL);
        return q;
    }

    void capture (const std::string& shotPath, bool withText, const std::vector<std::string>& skipBundles,
                  std::function<void (Capture)> done)
    {
        auto* cb = new std::function<void (Capture)> (std::move (done));
        const auto skip = skipBundles;
        const auto path = shotPath;
        NSRunningApplication* front = [[NSWorkspace sharedWorkspace].frontmostApplication retain];
        dispatch_async (captureQueue(), ^{
            @autoreleasepool
            {
                Capture c;
                c.idleAtCapture = idleSeconds();
                auto skipped = [&skip] (const std::string& b)
                { for (auto& s : skip) if (! s.empty() && b.rfind (s, 0) == 0) return true; return false; };
                const bool trusted = AXIsProcessTrusted();
                pid_t frontPid = 0;
                if (front != nil && ! skipped (str (front.bundleIdentifier)))
                {
                    frontPid = front.processIdentifier;
                    if (trusted) c.front = appInfo (front, true);
                    else { c.front.name = str (front.localizedName); c.front.bundleId = str (front.bundleIdentifier); c.front.appPath = str (front.bundleURL.path); }
                    if (trusted && withText) focusedText (front, c.typed, c.selected);
                }
                if (trusted)
                {
                    for (NSRunningApplication* a in [NSWorkspace sharedWorkspace].runningApplications)
                    {
                        if (a.activationPolicy != NSApplicationActivationPolicyRegular || a.hidden || a == front) continue;
                        if ([a.bundleIdentifier isEqualToString: [NSBundle mainBundle].bundleIdentifier]) continue;
                        if (skipped (str (a.bundleIdentifier))) continue;
                        auto info = appInfo (a, false);
                        if (info.window.empty() && info.doc.empty()) continue;   // ウィンドウのないアプリは出さない
                        c.others.push_back (info);
                        if (c.others.size() >= 8) break;
                    }
                }
                browserTabs (c.tabs);
                c.tabs.erase (std::remove_if (c.tabs.begin(), c.tabs.end(), [&] (const TabInfo& t) { return skipped (t.bundleId); }), c.tabs.end());
                c.finderPath = skipped ("com.apple.finder") ? std::string() : finderFront();
                RC_RELEASE (front);

                auto finish = [cb] (Capture cc) { dispatch_async (dispatch_get_main_queue(), ^{ (*cb) (cc); delete cb; }); };
                if (path.empty() || frontPid == 0) { finish (c); return; }
                shoot (frontPid, path, [c, finish] (bool ok) mutable { c.shot = ok; finish (c); });
            }
        });
    }

    //==========================================================================
    static RCTarget* target()
    {
        static RCTarget* t = [[RCTarget alloc] init];
        return t;
    }

    void watchAwayEvents (std::function<void (int, const std::string&)> onEvent)
    {
        RCTarget* t = target();
        t->onAway = std::move (onEvent);
        static bool once = false;
        if (once) return;
        once = true;
        NSNotificationCenter* wc = [NSWorkspace sharedWorkspace].notificationCenter;
        [wc addObserver: t selector: @selector (away:) name: NSWorkspaceWillSleepNotification object: nil];
        [wc addObserver: t selector: @selector (away:) name: NSWorkspaceScreensDidSleepNotification object: nil];
        [wc addObserver: t selector: @selector (away:) name: NSWorkspaceSessionDidResignActiveNotification object: nil];
        [wc addObserver: t selector: @selector (back:) name: NSWorkspaceDidWakeNotification object: nil];
        [wc addObserver: t selector: @selector (back:) name: NSWorkspaceScreensDidWakeNotification object: nil];
        [wc addObserver: t selector: @selector (back:) name: NSWorkspaceSessionDidBecomeActiveNotification object: nil];
        NSDistributedNotificationCenter* dc = [NSDistributedNotificationCenter defaultCenter];
        [dc addObserver: t selector: @selector (away:) name: @"com.apple.screenIsLocked" object: nil];
        [dc addObserver: t selector: @selector (back:) name: @"com.apple.screenIsUnlocked" object: nil];
        [dc addObserver: t selector: @selector (away:) name: @"com.apple.screensaver.didstart" object: nil];
        [dc addObserver: t selector: @selector (back:) name: @"com.apple.screensaver.didstop" object: nil];
    }

    double idleSeconds()
    {
        return CGEventSourceSecondsSinceLastEventType (kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType);
    }

    //==========================================================================
    void activateApp (const std::string& bundleId)
    {
        NSArray* a = [NSRunningApplication runningApplicationsWithBundleIdentifier: ns (bundleId)];
        if (a.count > 0) { [(NSRunningApplication*) a.firstObject unhide]; [(NSRunningApplication*) a.firstObject activateWithOptions: NSApplicationActivateIgnoringOtherApps]; return; }
        NSURL* url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: ns (bundleId)];
        if (url == nil) return;
        NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
        cfg.activates = YES;
        [[NSWorkspace sharedWorkspace] openApplicationAtURL: url configuration: cfg completionHandler: nil];
    }

    void openFile (const std::string& path)
    {
        [[NSWorkspace sharedWorkspace] openURL: [NSURL fileURLWithPath: ns (path)]];
    }

    static void openDoc (const AppInfo& a)
    {
        NSWorkspace* ws = [NSWorkspace sharedWorkspace];
        NSWorkspaceOpenConfiguration* cfg = [NSWorkspaceOpenConfiguration configuration];
        cfg.activates = NO;
        NSURL* app = a.appPath.empty() ? nil : [NSURL fileURLWithPath: ns (a.appPath)];
        if (! a.doc.empty() && [[NSFileManager defaultManager] fileExistsAtPath: ns (a.doc)])
        {
            NSURL* f = [NSURL fileURLWithPath: ns (a.doc)];
            if (app != nil) [ws openURLs: @[ f ] withApplicationAtURL: app configuration: cfg completionHandler: nil];
            else [ws openURL: f];
        }
        else if (! a.bundleId.empty() && [NSRunningApplication runningApplicationsWithBundleIdentifier: ns (a.bundleId)].count == 0 && app != nil)
            [ws openApplicationAtURL: app configuration: cfg completionHandler: nil];
    }

    // ブラウザで URL を開く。もうそのページのタブがあれば、そのタブへ移るだけ
    static void openTabs (const std::vector<TabInfo>& tabs)
    {
        std::vector<std::string> browsers;
        for (auto& t : tabs) if (std::find (browsers.begin(), browsers.end(), t.bundleId) == browsers.end()) browsers.push_back (t.bundleId);
        for (auto& b : browsers)
        {
            const bool safari = b == "com.apple.Safari";
            NSString* app = b == "com.google.Chrome" ? @"Google Chrome" : safari ? @"Safari" : b == "com.brave.Browser" ? @"Brave Browser" : @"Microsoft Edge";
            NSMutableString* list = [NSMutableString string];
            for (auto& t : tabs)
            {
                if (t.bundleId != b) continue;
                NSString* u = [[ns (t.url) stringByReplacingOccurrencesOfString: @"\\" withString: @"\\\\"] stringByReplacingOccurrencesOfString: @"\"" withString: @"\\\""];
                [list appendFormat: @"%@\"%@\"", list.length > 0 ? @", " : @"", u];
            }
            NSString* src = safari
              ? [NSString stringWithFormat:
                 @"with timeout of 5 seconds\n"
                  "tell application \"Safari\"\n"
                  "repeat with u in {%@}\n"
                  "set found to false\n"
                  "repeat with w in windows\n"
                  "repeat with t in tabs of w\n"
                  "if URL of t is (u as text) then\n"
                  "set current tab of w to t\n"
                  "set found to true\n"
                  "exit repeat\n"
                  "end if\n"
                  "end repeat\n"
                  "if found then exit repeat\n"
                  "end repeat\n"
                  "if not found then open location (u as text)\n"
                  "end repeat\n"
                  "end tell\n"
                  "end timeout", list]
              : [NSString stringWithFormat:
                 @"with timeout of 5 seconds\n"
                  "tell application \"%@\"\n"
                  "repeat with u in {%@}\n"
                  "set found to false\n"
                  "repeat with w in windows\n"
                  "set i to 0\n"
                  "repeat with t in tabs of w\n"
                  "set i to i + 1\n"
                  "if URL of t is (u as text) then\n"
                  "set active tab index of w to i\n"
                  "set found to true\n"
                  "exit repeat\n"
                  "end if\n"
                  "end repeat\n"
                  "if found then exit repeat\n"
                  "end repeat\n"
                  "if not found then open location (u as text)\n"
                  "end repeat\n"
                  "end tell\n"
                  "end timeout", app, list];
            runScript (src);
        }
    }

    void reopen (const std::vector<AppInfo>& apps, const std::vector<TabInfo>& tabs, const std::string& finderPath,
                 const std::string& frontBundle)
    {
        const auto A = apps;
        const auto T = tabs;
        const auto F = finderPath, front = frontBundle;
        dispatch_async (captureQueue(), ^{
            @autoreleasepool
            {
                dispatch_sync (dispatch_get_main_queue(), ^{ for (auto& a : A) openDoc (a); });
                if (! T.empty()) openTabs (T);
                dispatch_async (dispatch_get_main_queue(), ^{
                    if (! F.empty() && [[NSFileManager defaultManager] fileExistsAtPath: ns (F)])
                        [[NSWorkspace sharedWorkspace] selectFile: nil inFileViewerRootedAtPath: ns (F)];
                    if (! front.empty()) activateApp (front);
                });
            }
        });
    }

    //==========================================================================
    bool accessibilityTrusted (bool prompt)
    {
        NSDictionary* o = @{ (__bridge NSString*) kAXTrustedCheckOptionPrompt: @(prompt) };
        return AXIsProcessTrustedWithOptions ((__bridge CFDictionaryRef) o) == YES;
    }

    bool screenTrusted() { return CGPreflightScreenCaptureAccess(); }
    void requestScreen()
    {
        if (! CGRequestScreenCaptureAccess())
            [[NSWorkspace sharedWorkspace] openURL: [NSURL URLWithString: @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
    }

    //==========================================================================
    const std::vector<HotKeyChoice>& hotKeyChoices()
    {
        static const std::vector<HotKeyChoice> c {
            { "⌃⌥P", kVK_ANSI_P, controlKey | optionKey },
            { "⌃⌥R", kVK_ANSI_R, controlKey | optionKey },
            { "⌃⌥⌘P", kVK_ANSI_P, controlKey | optionKey | cmdKey },
            { "⌃⌥⌘R", kVK_ANSI_R, controlKey | optionKey | cmdKey },
            { "F15", kVK_F15, 0 },
            { "F16", kVK_F16, 0 },
            { "なし", -1, 0 },
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
        if (slot >= 0 && slot < 4) dispatch_async (dispatch_get_main_queue(), ^{ if (hotKeyFns[slot]) hotKeyFns[slot](); });
        return noErr;
    }

    bool registerHotKey (int slot, int choice, std::function<void()> fn)
    {
        if (slot < 0 || slot > 3) return false;
        static bool installed = false;
        if (! installed)
        {
            EventTypeSpec spec { kEventClassKeyboard, kEventHotKeyPressed };
            InstallApplicationEventHandler (&hotKeyHandler, 1, &spec, nullptr, nullptr);
            installed = true;
        }
        if (hotKeyRefs[slot] != nullptr) { UnregisterEventHotKey (hotKeyRefs[slot]); hotKeyRefs[slot] = nullptr; }
        hotKeyFns[slot] = std::move (fn);
        const auto& c = hotKeyChoices();
        if (choice < 0 || choice >= (int) c.size() || c[(size_t) choice].keyCode < 0) return true;
        EventHotKeyID hid { 'RsCd', (UInt32) slot + 1 };
        return RegisterEventHotKey ((UInt32) c[(size_t) choice].keyCode, (UInt32) c[(size_t) choice].mods, hid,
                                    GetApplicationEventTarget(), 0, &hotKeyRefs[slot]) == noErr;
    }

    //==========================================================================
    static NSStatusItem* statusItem()
    {
        static NSStatusItem* item = nil;
        if (item == nil)
        {
            item = [[[NSStatusBar systemStatusBar] statusItemWithLength: NSVariableStatusItemLength] retain];
            item.button.toolTip = @"Resume Card";
        }
        return item;
    }

    void setStatusIcon (bool paused)
    {
        NSStatusItem* item = statusItem();
        NSString* name = paused ? @"bookmark.slash" : @"bookmark";
        if (NSImage* img = [NSImage imageWithSystemSymbolName: name accessibilityDescription: @"Resume Card"])
        {
            [img setTemplate: YES];
            item.button.image = img;
            item.button.title = @"";
        }
        else item.button.title = @"RC";
    }

    void setStatusMenu (const std::vector<std::pair<std::string, int>>& items, std::function<void (int)> onPick)
    {
        RCTarget* t = target();
        t->onPick = std::move (onPick);
        NSMenu* menu = [[NSMenu alloc] initWithTitle: @"Resume Card"];
        for (auto& [title, tag] : items)
        {
            if (tag == 0) { [menu addItem: [NSMenuItem separatorItem]]; continue; }
            NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle: ns (title) action: @selector (pick:) keyEquivalent: @""];
            mi.target = t;
            mi.tag = tag;
            if (tag < 0) { mi.enabled = NO; mi.action = nil; }
            [menu addItem: mi];
            RC_RELEASE (mi);
        }
        menu.autoenablesItems = NO;
        statusItem().menu = menu;
        RC_RELEASE (menu);
    }

    void showWindow (void* nsView, bool focus)
    {
        NSView* v = (__bridge NSView*) nsView;
        NSWindow* w = [v window];
        if (w == nil) return;
        w.level = NSFloatingWindowLevel;
        w.collectionBehavior = w.collectionBehavior | NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
        if (focus) { [NSApp activateIgnoringOtherApps: YES]; [w makeKeyAndOrderFront: nil]; }
        else [w orderFrontRegardless];
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
