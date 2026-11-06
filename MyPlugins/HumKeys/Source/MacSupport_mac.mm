#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>
#include "MacSupport.h"
#include <unistd.h>

void macsupport::requestMicPermission (std::function<void (bool)> done)
{
    const auto st = [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio];
    if (st == AVAuthorizationStatusAuthorized) { done (true); return; }
    if (st == AVAuthorizationStatusDenied || st == AVAuthorizationStatusRestricted) { done (false); return; }
    auto* copy = new std::function<void (bool)> (std::move (done));
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio completionHandler: ^(BOOL granted) {
        dispatch_async (dispatch_get_main_queue(), ^{ (*copy) (granted == YES); delete copy; });
    }];
}

// Num Deck と同じやり方（ウィンドウを全部のデスクトップ・全画面の補助として出し、手前の高さにする）
static bool floatOn = false;

static void applyFloat (NSWindow* w)
{
    const NSWindowCollectionBehavior extra = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    NSWindowCollectionBehavior b = w.collectionBehavior;
    if (floatOn)
    {
        b &= ~(NSWindowCollectionBehaviorMoveToActiveSpace | NSWindowCollectionBehaviorFullScreenPrimary);
        if ((b & extra) != extra) w.collectionBehavior = b | extra;
        if (w.level < NSFloatingWindowLevel) w.level = NSFloatingWindowLevel;
    }
    else
    {
        if ((b & extra) != 0) w.collectionBehavior = b & ~extra;
        if (w.level == NSFloatingWindowLevel) w.level = NSNormalWindowLevel;
    }
}

void macsupport::setFloatOnTop (bool on)
{
    static id observer = nil;
    const bool changed = floatOn != on;
    floatOn = on;
    if (changed)
    {
        [NSApp setActivationPolicy: on ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular];
        dispatch_async (dispatch_get_main_queue(), ^{
            for (NSWindow* w in [NSApp windows])
                if (w.isVisible) [w orderFrontRegardless];
        });
    }
    for (NSWindow* w in [NSApp windows]) applyFloat (w);
    if (observer != nil) return;
    observer = [[NSNotificationCenter defaultCenter] addObserverForName: NSApplicationDidUpdateNotification object: nil
                                                                  queue: nil usingBlock: ^(NSNotification*) {
        if (! floatOn) return;
        for (NSWindow* w in [NSApp windows]) applyFloat (w);
    }];
   #if ! __has_feature(objc_arc)
    [observer retain];
   #endif
}

struct RemoteCommands::Impl
{
    std::function<void (const std::string&)> callback;
    id observer = nil;
};

RemoteCommands::RemoteCommands (std::function<void (const std::string&)> onCommand)
    : impl (std::make_unique<Impl>())
{
    impl->callback = std::move (onCommand);
    Impl* self = impl.get();
    impl->observer = [[NSDistributedNotificationCenter defaultCenter]
        addObserverForName: @"com.myplugins.humkeys.command"
                     object: nil
                      queue: [NSOperationQueue mainQueue]
                 usingBlock: ^(NSNotification* note)
                 {
                     id obj = note.object;
                     if ([obj isKindOfClass: [NSString class]] && self->callback)
                         if (const char* u = [(NSString*) obj UTF8String])
                             self->callback (std::string (u));
                 }];
}

RemoteCommands::~RemoteCommands()
{
    if (impl->observer != nil)
        [[NSDistributedNotificationCenter defaultCenter] removeObserver: impl->observer];
}

//==============================================================================
// Logic へ MIDI ファイルを読み込む（アクセシビリティでメニューを押し、ファイルの場所を打ち込む）
namespace
{
    NSRunningApplication* logicApp()
    {
        NSArray* a = [NSRunningApplication runningApplicationsWithBundleIdentifier: @"com.apple.logic10"];
        return a.count > 0 ? a.firstObject : nil;
    }

    CFTypeRef axCopy (AXUIElementRef el, CFStringRef attr)
    {
        CFTypeRef v = nullptr;
        if (el == nullptr || AXUIElementCopyAttributeValue (el, attr, &v) != kAXErrorSuccess) return nullptr;
        return v;
    }

    NSString* axTitle (AXUIElementRef el)
    {
        NSString* out = @"";
        if (CFTypeRef t = axCopy (el, kAXTitleAttribute))
        {
            if (CFGetTypeID (t) == CFStringGetTypeID()) out = [(__bridge NSString*) t copy];
            CFRelease (t);
        }
        return out;
    }

    // キーを Logic にだけ送る（ほかのアプリに打ち込まないように）
    void postKey (pid_t pid, CGKeyCode key, CGEventFlags flags)
    {
        CGEventRef d = CGEventCreateKeyboardEvent (nullptr, key, true);
        CGEventRef u = CGEventCreateKeyboardEvent (nullptr, key, false);
        CGEventSetFlags (d, flags); CGEventSetFlags (u, flags);
        CGEventPostToPid (pid, d); usleep (15000); CGEventPostToPid (pid, u);
        CFRelease (d); CFRelease (u);
        usleep (60000);
    }

    void postText (pid_t pid, NSString* text)
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

    // メニューの中から、題名が条件に合う項目を探す（needSub = 下にメニューを持つ項目だけ）
    AXUIElementRef findMenuItem (AXUIElementRef el, int depth, BOOL (^match) (NSString*), bool needSub)
    {
        if (depth > 6) return nullptr;
        CFTypeRef kids = axCopy (el, kAXChildrenAttribute);
        if (kids == nullptr) return nullptr;
        AXUIElementRef found = nullptr;
        for (id k in (__bridge NSArray*) kids)
        {
            AXUIElementRef c = (__bridge AXUIElementRef) k;
            NSString* role = @"";
            if (CFTypeRef r = axCopy (c, kAXRoleAttribute)) { role = [(__bridge NSString*) r copy]; CFRelease (r); }
            if ([role isEqualToString: (__bridge NSString*) kAXMenuItemRole] && match (axTitle (c)))
            {
                bool ok = ! needSub;
                if (needSub)
                    if (CFTypeRef sub = axCopy (c, kAXChildrenAttribute)) { ok = [(__bridge NSArray*) sub count] > 0; CFRelease (sub); }
                if (ok) { found = (AXUIElementRef) CFRetain (c); break; }
            }
            if ((found = findMenuItem (c, depth + 1, match, needSub)) != nullptr) break;
        }
        CFRelease (kids);
        return found;
    }

    AXUIElementRef focusedWindow (AXUIElementRef app) { return (AXUIElementRef) axCopy (app, kAXFocusedWindowAttribute); }

    bool hasSheet (AXUIElementRef w)
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

    // いま前にある Logic の窓（題名とシートの有無）
    struct WinState { NSString* title = @""; bool sheet = false; bool ok = false; };
    WinState frontState (AXUIElementRef ax)
    {
        WinState st;
        if (AXUIElementRef fw = focusedWindow (ax))
        {
            st.title = axTitle (fw); st.sheet = hasSheet (fw); st.ok = true;
            CFRelease (fw);
        }
        return st;
    }
}

namespace macsupport
{
    bool accessibilityTrusted (bool prompt)
    {
        NSDictionary* o = @{ (__bridge NSString*) kAXTrustedCheckOptionPrompt: @(prompt) };
        return AXIsProcessTrustedWithOptions ((__bridge CFDictionaryRef) o) == YES;
    }

    void insertMidiIntoLogic (const std::string& midiPath, int bar, std::function<void (bool, const std::string&, bool)> done)
    {
        auto* cb = new std::function<void (bool, const std::string&, bool)> (std::move (done));
        NSString* path = [NSString stringWithUTF8String: midiPath.c_str()];
        dispatch_async (dispatch_get_global_queue (QOS_CLASS_USER_INITIATED, 0), ^{
            __block bool moved = false;
            auto finish = [cb] (bool ok, std::string why, bool mv)
            {
                dispatch_async (dispatch_get_main_queue(), ^{ (*cb) (ok, why, mv); delete cb; });
            };
            if (! AXIsProcessTrusted()) { finish (false, "accessibility", false); return; }
            __block pid_t pid = 0;
            dispatch_sync (dispatch_get_main_queue(), ^{ if (NSRunningApplication* a = logicApp()) pid = a.processIdentifier; });
            if (pid == 0) { finish (false, "nologic", false); return; }
            AXUIElementRef ax = AXUIElementCreateApplication (pid);

            // 1. Logic を前へ
            __block bool isFront = false;
            dispatch_sync (dispatch_get_main_queue(), ^{
                if (NSRunningApplication* a = logicApp()) [a activateWithOptions: NSApplicationActivateIgnoringOtherApps];
            });
            for (int i = 0; i < 30 && ! isFront; ++i)
            {
                usleep (100000);
                dispatch_sync (dispatch_get_main_queue(), ^{
                    isFront = [[NSWorkspace sharedWorkspace].frontmostApplication.bundleIdentifier isEqualToString: @"com.apple.logic10"];
                });
            }
            const WinState main = frontState (ax);
            if (! isFront || ! main.ok) { CFRelease (ax); finish (false, isFront ? "noproject" : "notfront", false); return; }
            if (main.sheet) { CFRelease (ax); finish (false, "busy", false); return; }
            usleep (250000);

            // 2. 小節が決まっているテイクは、再生ヘッドをそこへ（Logic の初期キー「/」= 位置へ移動）
            if (bar > 0)
            {
                postKey (pid, (CGKeyCode) kVK_ANSI_Slash, 0);
                bool dialog = false;
                for (int i = 0; i < 12 && ! dialog; ++i)
                {
                    usleep (100000);
                    const auto st = frontState (ax);
                    dialog = st.ok && (st.sheet || ! [st.title isEqualToString: main.title]);
                }
                if (dialog)
                {
                    usleep (200000);
                    postText (pid, [NSString stringWithFormat: @"%d 1 1 1", bar]);
                    usleep (150000);
                    postKey (pid, (CGKeyCode) kVK_Return, 0);
                    for (int i = 0; i < 15; ++i)
                    {
                        usleep (100000);
                        const auto st = frontState (ax);
                        if (st.ok && ! st.sheet && [st.title isEqualToString: main.title]) { moved = true; break; }
                    }
                    if (! moved) { postKey (pid, (CGKeyCode) kVK_Escape, 0); usleep (300000); }
                }
            }

            // 3. 「ファイル → 読み込む → MIDI ファイル」をメニューから押す
            AXUIElementRef item = nullptr;
            if (CFTypeRef menubar = axCopy (ax, kAXMenuBarAttribute))
            {
                AXUIElementRef importMenu = findMenuItem ((AXUIElementRef) menubar, 0, ^BOOL (NSString* t) {
                    return [t isEqualToString: @"Import"] || [t hasPrefix: @"読み込"] || [t isEqualToString: @"Import…"];
                }, true);
                if (importMenu != nullptr)
                {
                    item = findMenuItem (importMenu, 0, ^BOOL (NSString* t) { return [t rangeOfString: @"MIDI"].location != NSNotFound; }, false);
                    CFRelease (importMenu);
                }
                CFRelease (menubar);
            }
            if (item == nullptr) { CFRelease (ax); finish (false, "nomenu", moved); return; }
            AXUIElementPerformAction (item, kAXPressAction);
            CFRelease (item);

            // 4. ファイルを選ぶ画面を待って、場所を打ち込む（⇧⌘G → パス → Return → Return）
            bool panel = false;
            for (int i = 0; i < 50 && ! panel; ++i)
            {
                usleep (150000);
                const auto st = frontState (ax);
                panel = st.ok && (st.sheet || ! [st.title isEqualToString: main.title]);
            }
            if (! panel) { CFRelease (ax); finish (false, "nopanel", moved); return; }
            usleep (500000);
            postKey (pid, (CGKeyCode) kVK_ANSI_G, kCGEventFlagMaskCommand | kCGEventFlagMaskShift);
            usleep (900000);
            postText (pid, path);
            usleep (600000);
            postKey (pid, (CGKeyCode) kVK_Return, 0);
            usleep (1200000);
            postKey (pid, (CGKeyCode) kVK_Return, 0);

            // 5. 画面が閉じたら終わり
            bool closed = false;
            for (int i = 0; i < 60 && ! closed; ++i)
            {
                usleep (150000);
                const auto st = frontState (ax);
                closed = st.ok && ! st.sheet && [st.title isEqualToString: main.title];
            }
            CFRelease (ax);
            finish (closed, closed ? "" : "stillopen", moved);
        });
    }
}
