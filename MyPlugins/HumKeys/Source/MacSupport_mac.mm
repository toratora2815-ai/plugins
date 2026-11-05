#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#include "MacSupport.h"

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
