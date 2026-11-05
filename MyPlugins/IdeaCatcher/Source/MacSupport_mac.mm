#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
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

    static std::function<void()> hotKeyFn;
    static EventHotKeyRef hotKeyRef = nullptr;

    static OSStatus hotKeyHandler (EventHandlerCallRef, EventRef, void*)
    {
        if (hotKeyFn) dispatch_async (dispatch_get_main_queue(), ^{ if (hotKeyFn) hotKeyFn(); });
        return noErr;
    }

    bool registerHotKey (int choice, std::function<void()> onPress)
    {
        static bool handlerInstalled = false;
        if (! handlerInstalled)
        {
            EventTypeSpec spec { kEventClassKeyboard, kEventHotKeyPressed };
            InstallApplicationEventHandler (&hotKeyHandler, 1, &spec, nullptr, nullptr);
            handlerInstalled = true;
        }
        hotKeyFn = std::move (onPress);
        if (hotKeyRef != nullptr) { UnregisterEventHotKey (hotKeyRef); hotKeyRef = nullptr; }
        const auto& c = hotKeyChoices();
        if (choice < 0 || choice >= (int) c.size()) return false;
        EventHotKeyID hid { 'IdCt', 1 };
        return RegisterEventHotKey ((UInt32) c[(size_t) choice].keyCode, (UInt32) c[(size_t) choice].mods, hid,
                                    GetApplicationEventTarget(), 0, &hotKeyRef) == noErr;
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
}
