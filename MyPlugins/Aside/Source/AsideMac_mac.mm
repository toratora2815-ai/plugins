#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>
#import <EventKit/EventKit.h>
#import <Carbon/Carbon.h>
#import <ServiceManagement/ServiceManagement.h>
#include <atomic>
#include "AsideMac.h"

// ARC なしでビルドする（JUCE の標準）
#if __has_feature(objc_arc)
 #define AS_RELEASE(x)
#else
 #define AS_RELEASE(x) [x release]
#endif

namespace { std::string str (NSString* s) { return s != nil && [s UTF8String] != nullptr ? std::string ([s UTF8String]) : std::string(); } }

//==============================================================================
// マイク → 16 kHz モノラル → 音声認識（Mac の中だけ）。話し終わり（約 0.9 秒の無音）ごとに確定させる
@interface AsideListener : NSObject
{
@public
    AVAudioEngine* engine;
    SFSpeechRecognizer* recognizer;
    SFSpeechAudioBufferRecognitionRequest* request;
    SFSpeechRecognitionTask* task;
    AVAudioConverter* converter;
    AVAudioFormat* outFormat;
    NSLock* lock;
    NSTimer* timer;
    std::atomic<double> lastVoice;
    std::atomic<bool> voiced;
    std::atomic<float> level;
    double requestStart;
    float noiseFloor;
    bool running;
    std::function<void (const std::string&, bool)> onText;
    std::function<void (float)> onLevel;
    std::function<void (const std::string&)> onState;
}
- (void) start;
- (void) stop;
@end

@implementation AsideListener

- (instancetype) init
{
    if ((self = [super init]))
    {
        lock = [[NSLock alloc] init];
        lastVoice = 0; voiced = false; level = 0;
        noiseFloor = -60.0f;
        running = false;
    }
    return self;
}

static double nowSec() { return [NSDate timeIntervalSinceReferenceDate]; }

- (void) newRequest
{
    // 前のものは「終わり」と伝えるだけ（確定した結果は前のタスクから届く）
    [lock lock];
    SFSpeechAudioBufferRecognitionRequest* old = request;
    SFSpeechRecognitionTask* oldTask = task;
    request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
    request.shouldReportPartialResults = YES;
    request.requiresOnDeviceRecognition = YES;
    if (@available (macOS 13.0, *)) request.addsPunctuation = YES;
    request.contextualStrings = @[ @"あとで", @"しなきゃ", @"忘れないように", @"リマインド", @"打ち合わせ" ];
    [lock unlock];
    requestStart = nowSec();
    if (old != nil) { [old endAudio]; AS_RELEASE (old); }
    if (oldTask != nil) AS_RELEASE (oldTask);

    AsideListener* me = self;
    task = [[recognizer recognitionTaskWithRequest: request resultHandler: ^(SFSpeechRecognitionResult* result, NSError* error) {
        if (result == nil) return;
        NSString* text = result.bestTranscription.formattedString;
        const bool fin = result.isFinal;
        if (text.length == 0) return;
        const std::string s = str (text);
        dispatch_async (dispatch_get_main_queue(), ^{ if (me->onText) me->onText (s, fin); });
        (void) error;
    }] retain];
}

- (void) tick: (NSTimer*) t
{
    if (onLevel) onLevel (level.load());
    level = level.load() * 0.7f;
    const double now = nowSec();
    if (voiced && now - lastVoice > 0.9)
    {
        voiced = false;
        [self newRequest];          // 話し終わった → 確定させて次へ
    }
    else if (! voiced && now - requestStart > 50.0)
    {
        [lock lock];
        SFSpeechRecognitionTask* old = task;
        [lock unlock];
        [old cancel];               // 1 回の認識は約 1 分までなので、黙っている間に取り替える
        [self newRequest];
    }
}

- (void) configChanged: (NSNotification*) n
{
    if (! running) return;
    dispatch_async (dispatch_get_main_queue(), ^{ [self stop]; [self start]; });
}

- (void) start
{
    if (running) return;
    recognizer = [[SFSpeechRecognizer alloc] initWithLocale: [NSLocale localeWithLocaleIdentifier: @"ja-JP"]];
    if (recognizer == nil || ! recognizer.supportsOnDeviceRecognition)
    {
        if (onState) onState ("no-ondevice");
        return;
    }
    engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = engine.inputNode;
    AVAudioFormat* inFormat = [input outputFormatForBus: 0];
    if (inFormat.sampleRate <= 0 || inFormat.channelCount == 0) { if (onState) onState ("error:no-input"); return; }
    outFormat = [[AVAudioFormat alloc] initWithCommonFormat: AVAudioPCMFormatFloat32 sampleRate: 16000 channels: 1 interleaved: NO];
    converter = [[AVAudioConverter alloc] initFromFormat: inFormat toFormat: outFormat];
    converter.downmix = YES;

    AsideListener* me = self;
    [input installTapOnBus: 0 bufferSize: 2048 format: inFormat block: ^(AVAudioPCMBuffer* buf, AVAudioTime*) {
        const AVAudioFrameCount cap = (AVAudioFrameCount) (buf.frameLength * 16000.0 / buf.format.sampleRate) + 64;
        AVAudioPCMBuffer* out = [[AVAudioPCMBuffer alloc] initWithPCMFormat: me->outFormat frameCapacity: cap];
        __block BOOL fed = NO;
        NSError* err = nil;
        [me->converter convertToBuffer: out error: &err withInputFromBlock: ^AVAudioBuffer* (AVAudioPacketCount, AVAudioConverterInputStatus* st) {
            if (fed) { *st = AVAudioConverterInputStatus_NoDataNow; return nil; }
            fed = YES; *st = AVAudioConverterInputStatus_HaveData; return buf;
        }];
        if (err == nil && out.frameLength > 0)
        {
            // 声があるか（まわりの音より 12 dB 以上大きいか）
            const float* x = out.floatChannelData[0];
            double e = 0;
            for (AVAudioFrameCount i = 0; i < out.frameLength; ++i) e += (double) x[i] * x[i];
            const float db = (float) (10.0 * log10 (e / out.frameLength + 1e-12));
            float& nf = me->noiseFloor;
            nf = db < nf ? nf + 0.3f * (db - nf) : nf + 0.004f * (db - nf);
            if (db > nf + 12.0f && db > -52.0f) { me->lastVoice = nowSec(); me->voiced = true; }
            me->level = std::max (me->level.load(), std::min (1.0f, std::max (0.0f, (db + 60.0f) / 50.0f)));
            [me->lock lock];
            [me->request appendAudioPCMBuffer: out];
            [me->lock unlock];
        }
        AS_RELEASE (out);
    }];

    [self newRequest];
    [engine prepare];
    NSError* err = nil;
    if (! [engine startAndReturnError: &err])
    {
        [input removeTapOnBus: 0];
        if (onState) onState ("error:" + str (err.localizedDescription));
        return;
    }
    running = true;
    timer = [[NSTimer scheduledTimerWithTimeInterval: 0.1 target: self selector: @selector (tick:) userInfo: nil repeats: YES] retain];
    [[NSNotificationCenter defaultCenter] addObserver: self selector: @selector (configChanged:)
                                                 name: AVAudioEngineConfigurationChangeNotification object: engine];
    if (onState) onState ("listening");
}

- (void) stop
{
    if (! running) return;
    running = false;
    [[NSNotificationCenter defaultCenter] removeObserver: self name: AVAudioEngineConfigurationChangeNotification object: engine];
    [timer invalidate]; AS_RELEASE (timer); timer = nil;
    [engine.inputNode removeTapOnBus: 0];
    [engine stop];
    [lock lock];
    [request endAudio];
    [task finish];
    AS_RELEASE (request); request = nil;
    AS_RELEASE (task); task = nil;
    [lock unlock];
    AS_RELEASE (engine); engine = nil;
    AS_RELEASE (converter); converter = nil;
    AS_RELEASE (outFormat); outFormat = nil;
    AS_RELEASE (recognizer); recognizer = nil;
    voiced = false;
}
@end

//==============================================================================
@interface AsideCommandListener : NSObject
{
@public
    std::function<void (const std::string&)> onCommand;
}
- (void) got: (NSNotification*) n;
@end
@implementation AsideCommandListener
- (void) got: (NSNotification*) n
{
    NSString* s = [n object];
    if (onCommand && s != nil) onCommand (str (s));
}
@end

@interface AsideMenuTarget : NSObject
{
@public
    std::function<void (int)> onPick;
}
- (void) pick: (id) sender;
@end
@implementation AsideMenuTarget
- (void) pick: (id) sender { if (onPick) onPick ((int) [(NSMenuItem*) sender tag]); }
@end

namespace aside::mac
{
    static NSString* ns (const std::string& s) { return [NSString stringWithUTF8String: s.c_str()] ?: @""; }

    static AsideListener* listener()
    {
        static AsideListener* l = [[AsideListener alloc] init];
        return l;
    }

    void startListening (std::function<void (const std::string&, bool)> onText, std::function<void (float)> onLevel,
                         std::function<void (const std::string&)> onState)
    {
        AsideListener* l = listener();
        l->onText = std::move (onText);
        l->onLevel = std::move (onLevel);
        l->onState = std::move (onState);
        if (l->running) return;

        auto startIfAllowed = [l]
        {
            [SFSpeechRecognizer requestAuthorization: ^(SFSpeechRecognizerAuthorizationStatus st) {
                dispatch_async (dispatch_get_main_queue(), ^{
                    if (st != SFSpeechRecognizerAuthorizationStatusAuthorized) { if (l->onState) l->onState ("denied-speech"); return; }
                    [l start];
                });
            }];
        };
        const auto mic = [AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio];
        if (mic == AVAuthorizationStatusAuthorized) { startIfAllowed(); return; }
        if (mic == AVAuthorizationStatusDenied || mic == AVAuthorizationStatusRestricted) { if (l->onState) l->onState ("denied-mic"); return; }
        [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio completionHandler: ^(BOOL ok) {
            dispatch_async (dispatch_get_main_queue(), ^{
                if (ok) startIfAllowed();
                else if (l->onState) l->onState ("denied-mic");
            });
        }];
    }

    void stopListening() { [listener() stop]; }
    bool listening() { return listener()->running; }

    //==========================================================================
    static EKEventStore* store()
    {
        static EKEventStore* s = [[EKEventStore alloc] init];
        return s;
    }

    static void requestAccess (bool events, void (^done)(BOOL))
    {
        if (@available (macOS 14.0, *))
        {
            if (events) [store() requestFullAccessToEventsWithCompletion: ^(BOOL ok, NSError*) { done (ok); }];
            else [store() requestFullAccessToRemindersWithCompletion: ^(BOOL ok, NSError*) { done (ok); }];
        }
        else
            [store() requestAccessToEntityType: events ? EKEntityTypeEvent : EKEntityTypeReminder completion: ^(BOOL ok, NSError*) { done (ok); }];
    }

    static NSDateComponents* comps (const Item& it)
    {
        NSDateComponents* c = [[[NSDateComponents alloc] init] autorelease];
        c.calendar = [NSCalendar currentCalendar];
        c.year = it.y; c.month = it.m; c.day = it.d;
        if (it.hasTime) { c.hour = it.hh; c.minute = it.mm; }
        return c;
    }

    static EKCalendar* reminderList (const std::string& name)
    {
        EKEventStore* s = store();
        NSString* n = ns (name.empty() ? std::string ("ひとりごと") : name);
        for (EKCalendar* c in [s calendarsForEntityType: EKEntityTypeReminder])
            if ([c.title isEqualToString: n]) return c;
        EKCalendar* def = s.defaultCalendarForNewReminders;
        EKCalendar* c = [EKCalendar calendarForEntityType: EKEntityTypeReminder eventStore: s];
        c.title = n;
        c.source = def != nil ? def.source : nil;
        NSError* err = nil;
        if (c.source != nil && [s saveCalendar: c commit: YES error: &err]) return c;
        return def;
    }

    void addItem (const Item& item, std::function<void (const std::string&, const std::string&)> done)
    {
        auto* cb = new std::function<void (const std::string&, const std::string&)> (std::move (done));
        const Item it = item;
        requestAccess (it.event, ^(BOOL ok) {
            dispatch_async (dispatch_get_main_queue(), ^{
                auto finish = [cb] (const std::string& id, const std::string& e) { (*cb) (id, e); delete cb; };
                if (! ok) { finish ({}, it.event ? "calendar-denied" : "reminders-denied"); return; }
                EKEventStore* s = store();
                NSError* err = nil;
                if (it.event)
                {
                    EKEvent* e = [EKEvent eventWithEventStore: s];
                    e.title = ns (it.title);
                    e.notes = ns (it.notes);
                    NSDate* start = [[NSCalendar currentCalendar] dateFromComponents: comps (it)];
                    e.startDate = start;
                    e.endDate = [start dateByAddingTimeInterval: 3600];
                    e.calendar = s.defaultCalendarForNewEvents;
                    [e addAlarm: [EKAlarm alarmWithRelativeOffset: -600]];
                    if ([s saveEvent: e span: EKSpanThisEvent commit: YES error: &err]) finish (str (e.eventIdentifier), {});
                    else finish ({}, str (err.localizedDescription));
                    return;
                }
                EKReminder* r = [EKReminder reminderWithEventStore: s];
                r.title = ns (it.title);
                r.notes = ns (it.notes);
                r.calendar = reminderList (it.listName);
                if (it.hasDate)
                {
                    r.dueDateComponents = comps (it);
                    if (it.hasTime)
                        if (NSDate* at = [[NSCalendar currentCalendar] dateFromComponents: comps (it)])
                            [r addAlarm: [EKAlarm alarmWithAbsoluteDate: at]];
                }
                if ([s saveReminder: r commit: YES error: &err]) finish (str (r.calendarItemIdentifier), {});
                else finish ({}, str (err.localizedDescription));
            });
        });
    }

    void removeItem (const std::string& id, bool event)
    {
        EKEventStore* s = store();
        NSError* err = nil;
        if (event)
        {
            if (EKEvent* e = [s eventWithIdentifier: ns (id)]) [s removeEvent: e span: EKSpanThisEvent commit: YES error: &err];
        }
        else if (EKCalendarItem* r = [s calendarItemWithIdentifier: ns (id)])
            if ([r isKindOfClass: [EKReminder class]]) [s removeReminder: (EKReminder*) r commit: YES error: &err];
    }

    void openReminders()
    {
        NSURL* url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier: @"com.apple.reminders"];
        if (url != nil) [[NSWorkspace sharedWorkspace] openApplicationAtURL: url configuration: [NSWorkspaceOpenConfiguration configuration] completionHandler: nil];
    }

    void listenCommands (std::function<void (const std::string&)> onCommand)
    {
        static AsideCommandListener* l = nil;
        if (l == nil)
        {
            l = [[AsideCommandListener alloc] init];
            [[NSDistributedNotificationCenter defaultCenter] addObserver: l selector: @selector (got:)
                                                                    name: @"com.myplugins.aside.command" object: nil
                                                      suspensionBehavior: NSNotificationSuspensionBehaviorDeliverImmediately];
        }
        l->onCommand = std::move (onCommand);
    }

    std::string frontBundle() { return str ([NSWorkspace sharedWorkspace].frontmostApplication.bundleIdentifier); }

    //==========================================================================
    const std::vector<HotKeyChoice>& hotKeyChoices()
    {
        static const std::vector<HotKeyChoice> c {
            { "⌃⌥M",      kVK_ANSI_M, controlKey | optionKey },
            { "⌃⌥Space",  kVK_Space,  controlKey | optionKey },
            { "⌃⌥⌘M",     kVK_ANSI_M, controlKey | optionKey | cmdKey },
            { "F14",      kVK_F14,    0 },
            { "なし",     -1,         0 },
        };
        return c;
    }

    static std::function<void()> hotKeyFn;
    static EventHotKeyRef hotKeyRef = nullptr;

    static OSStatus hotKeyHandler (EventHandlerCallRef, EventRef, void*)
    {
        dispatch_async (dispatch_get_main_queue(), ^{ if (hotKeyFn) hotKeyFn(); });
        return noErr;
    }

    bool registerHotKey (int choice, std::function<void()> onPress)
    {
        static bool installed = false;
        if (! installed)
        {
            EventTypeSpec spec { kEventClassKeyboard, kEventHotKeyPressed };
            InstallApplicationEventHandler (&hotKeyHandler, 1, &spec, nullptr, nullptr);
            installed = true;
        }
        if (hotKeyRef != nullptr) { UnregisterEventHotKey (hotKeyRef); hotKeyRef = nullptr; }
        hotKeyFn = std::move (onPress);
        const auto& c = hotKeyChoices();
        if (choice < 0 || choice >= (int) c.size() || c[(size_t) choice].keyCode < 0) return true;
        EventHotKeyID hid { 'Asid', 1 };
        return RegisterEventHotKey ((UInt32) c[(size_t) choice].keyCode, (UInt32) c[(size_t) choice].mods, hid,
                                    GetApplicationEventTarget(), 0, &hotKeyRef) == noErr;
    }

    //==========================================================================
    static NSStatusItem* statusItem()
    {
        static NSStatusItem* item = nil;
        if (item == nil)
        {
            item = [[[NSStatusBar systemStatusBar] statusItemWithLength: NSVariableStatusItemLength] retain];
            item.button.toolTip = @"Aside";
        }
        return item;
    }

    void setStatusIcon (int state)
    {
        NSStatusItem* item = statusItem();
        NSString* name = state == 0 ? @"mic.slash" : state == 3 ? @"square.and.pencil" : state == 2 ? @"waveform" : @"text.bubble";
        if (NSImage* img = [NSImage imageWithSystemSymbolName: name accessibilityDescription: @"Aside"])
        {
            [img setTemplate: YES];
            item.button.image = img;
            item.button.title = @"";
            item.button.contentTintColor = state == 3 ? [NSColor systemOrangeColor] : nil;
        }
        else item.button.title = state == 0 ? @"Aside -" : @"Aside";
    }

    void setStatusMenu (const std::vector<std::pair<std::string, int>>& items, std::function<void (int)> onPick)
    {
        static AsideMenuTarget* t = [[AsideMenuTarget alloc] init];
        t->onPick = std::move (onPick);
        NSMenu* menu = [[NSMenu alloc] initWithTitle: @"Aside"];
        menu.autoenablesItems = NO;
        for (auto& [title, tag] : items)
        {
            if (tag == 0) { [menu addItem: [NSMenuItem separatorItem]]; continue; }
            NSMenuItem* mi = [[NSMenuItem alloc] initWithTitle: ns (title) action: @selector (pick:) keyEquivalent: @""];
            mi.target = t;
            mi.tag = tag;
            if (tag < 0) mi.enabled = NO;
            [menu addItem: mi];
            AS_RELEASE (mi);
        }
        statusItem().menu = menu;
        AS_RELEASE (menu);
    }

    //==========================================================================
    void makeHud (void* nsView, bool clickThrough)
    {
        NSWindow* w = [(__bridge NSView*) nsView window];
        if (w == nil) return;
        w.ignoresMouseEvents = clickThrough;
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
        w.collectionBehavior = w.collectionBehavior | NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
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
