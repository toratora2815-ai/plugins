// Mac 専用: プロセスタップ + 集約デバイスで、Logic とミュージックの音を受け取り出力に鳴らす
// Apple のヘッダーを JUCE より先に読む（Point / Component の名前がぶつかるため）
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#include <objc/message.h>
#include <unistd.h>

#include "Capture.h"

#if JUCE_MAC

#ifndef kAudioAggregateDeviceTapListKey
 #define kAudioAggregateDeviceTapListKey "taps"
#endif
#ifndef kAudioAggregateDeviceTapAutoStartKey
 #define kAudioAggregateDeviceTapAutoStartKey "tapautostart"
#endif
#ifndef kAudioSubTapUIDKey
 #define kAudioSubTapUIDKey "uid"
#endif
#ifndef kAudioSubTapDriftCompensationKey
 #define kAudioSubTapDriftCompensationKey "drift"
#endif

namespace
{
    AudioObjectPropertyAddress addr (AudioObjectPropertySelector sel,
                                     AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
    {
        return { sel, scope, kAudioObjectPropertyElementMain };
    }

    template <typename T>
    bool getValue (AudioObjectID obj, AudioObjectPropertySelector sel, T& out,
                   AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
    {
        auto a = addr (sel, scope);
        UInt32 size = sizeof (T);
        return AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, &out) == noErr;
    }

    std::vector<AudioObjectID> getIDs (AudioObjectID obj, AudioObjectPropertySelector sel,
                                       AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal)
    {
        auto a = addr (sel, scope);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (obj, &a, 0, nullptr, &size) != noErr || size == 0) return {};
        std::vector<AudioObjectID> v (size / sizeof (AudioObjectID));
        if (AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, v.data()) != noErr) return {};
        v.resize (size / sizeof (AudioObjectID));
        return v;
    }

    juce::String getString (AudioObjectID obj, AudioObjectPropertySelector sel)
    {
        CFStringRef s = nullptr;
        auto a = addr (sel);
        UInt32 size = sizeof (s);
        if (AudioObjectGetPropertyData (obj, &a, 0, nullptr, &size, &s) != noErr || s == nullptr) return {};
        auto r = juce::String::fromCFString (s);
        CFRelease (s);
        return r;
    }

    int countChannels (AudioObjectID dev, AudioObjectPropertyScope scope)
    {
        auto a = addr (kAudioDevicePropertyStreamConfiguration, scope);
        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (dev, &a, 0, nullptr, &size) != noErr || size == 0) return 0;
        juce::HeapBlock<char> mem (size);
        auto* list = reinterpret_cast<AudioBufferList*> (mem.get());
        if (AudioObjectGetPropertyData (dev, &a, 0, nullptr, &size, list) != noErr) return 0;
        int n = 0;
        for (UInt32 i = 0; i < list->mNumberBuffers; ++i) n += (int) list->mBuffers[i].mNumberChannels;
        return n;
    }

    AudioObjectID deviceForUID (const juce::String& uid)
    {
        for (auto d : getIDs (kAudioObjectSystemObject, kAudioHardwarePropertyDevices))
            if (getString (d, kAudioDevicePropertyDeviceUID) == uid) return d;
        return kAudioObjectUnknown;
    }

    juce::File logFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/Reference Monitor/log.txt");
    }
}

// ============================================================================
struct CaptureEngine::Impl
{
    explicit Impl (Engine& e) : engine (e)
    {
        for (auto* b : { &mixL, &mixR, &liveL, &liveR, &outL, &outR })
            b->assign (kMaxFrames, 0.0f);
    }

    void log (const juce::String& s)
    {
        const auto line = juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + s;
        logLines.add (line);
        while (logLines.size() > 200) logLines.remove (0);
        auto f = logFile();
        f.getParentDirectory().createDirectory();
        if (f.getSize() > 500000) f.deleteFile();
        f.appendText (line + "\n");
    }

    AudioObjectID createTap (const std::vector<AudioObjectID>& procs, const char* name, juce::String& uidOut)
    {
        @autoreleasepool
        {
            NSMutableArray* arr = [NSMutableArray array];
            for (auto o : procs) [arr addObject:[NSNumber numberWithUnsignedInt:o]];

            Class cls = NSClassFromString (@"CATapDescription");
            if (cls == nil) { log ("CATapDescription not available (needs macOS 14.2+)"); return kAudioObjectUnknown; }

            id d = [cls alloc];
            SEL initSel = NSSelectorFromString (@"initStereoMixdownOfProcesses:");
            if ([d respondsToSelector:initSel])
                d = ((id (*) (id, SEL, id)) objc_msgSend) (d, initSel, arr);
            else
            {
                d = [d init];
                if ([d respondsToSelector:@selector (setProcesses:)])
                    ((void (*) (id, SEL, id)) objc_msgSend) (d, @selector (setProcesses:), arr);
            }

            if ([d respondsToSelector:@selector (setName:)])
                ((void (*) (id, SEL, id)) objc_msgSend) (d, @selector (setName:), [NSString stringWithUTF8String:name]);
            // 2 = 受け取っている間だけ元の音を消す（mutedWhenTapped）
            if ([d respondsToSelector:@selector (setMuteBehavior:)])
                ((void (*) (id, SEL, NSInteger)) objc_msgSend) (d, @selector (setMuteBehavior:), (NSInteger) 2);
            // 他のアプリから見えない、このアプリ専用のタップにする
            if ([d respondsToSelector:NSSelectorFromString (@"setPrivate:")])
                ((void (*) (id, SEL, BOOL)) objc_msgSend) (d, NSSelectorFromString (@"setPrivate:"), YES);
            else if ([d respondsToSelector:NSSelectorFromString (@"setPrivateTap:")])
                ((void (*) (id, SEL, BOOL)) objc_msgSend) (d, NSSelectorFromString (@"setPrivateTap:"), YES);

            AudioObjectID tapID = kAudioObjectUnknown;
            const OSStatus st = AudioHardwareCreateProcessTap (d, &tapID);
            [d release];
            if (st != noErr || tapID == kAudioObjectUnknown)
            {
                log (juce::String ("AudioHardwareCreateProcessTap failed: ") + juce::String ((int) st));
                return kAudioObjectUnknown;
            }
            uidOut = getString (tapID, kAudioTapPropertyUID);
            log (juce::String ("tap created: ") + name + " id=" + juce::String ((int) tapID) + " uid=" + uidOut
                 + " processes=" + juce::String ((int) procs.size()));
            return tapID;
        }
    }

    juce::String start (const juce::String& outputUID,
                        const std::vector<AudioObjectID>& mixProcs, const std::vector<AudioObjectID>& refProcs)
    {
        stop();
        @autoreleasepool
        {
            const AudioObjectID outDev = deviceForUID (outputUID);
            if (outDev == kAudioObjectUnknown) return "Output device not found";

            juce::StringArray tapUIDs;
            mixSlot = refSlot = -1;
            if (! mixProcs.empty())
            {
                juce::String uid;
                const auto t = createTap (mixProcs, "Reference Monitor - Mix", uid);
                if (t != kAudioObjectUnknown && uid.isNotEmpty()) { taps.push_back (t); mixSlot = tapUIDs.size(); tapUIDs.add (uid); }
            }
            if (! refProcs.empty())
            {
                juce::String uid;
                const auto t = createTap (refProcs, "Reference Monitor - Reference", uid);
                if (t != kAudioObjectUnknown && uid.isNotEmpty()) { taps.push_back (t); refSlot = tapUIDs.size(); tapUIDs.add (uid); }
            }
            numTaps = tapUIDs.size();

            NSString* outUID = (NSString*) outputUID.toCFString();
            [outUID autorelease];
            NSMutableArray* tapList = [NSMutableArray array];
            for (auto& u : tapUIDs)
            {
                NSString* s = (NSString*) u.toCFString();
                [s autorelease];
                [tapList addObject:@{ @kAudioSubTapUIDKey: s, @kAudioSubTapDriftCompensationKey: @YES }];
            }

            NSDictionary* desc = @{
                @kAudioAggregateDeviceNameKey: @"Reference Monitor",
                @kAudioAggregateDeviceUIDKey: [[NSUUID UUID] UUIDString],
                @kAudioAggregateDeviceMainSubDeviceKey: outUID,
                @kAudioAggregateDeviceIsPrivateKey: @YES,
                @kAudioAggregateDeviceIsStackedKey: @NO,
                @kAudioAggregateDeviceTapAutoStartKey: @YES,
                @kAudioAggregateDeviceSubDeviceListKey: @[ @{ @kAudioSubDeviceUIDKey: outUID } ],
                @kAudioAggregateDeviceTapListKey: tapList
            };

            OSStatus st = AudioHardwareCreateAggregateDevice ((CFDictionaryRef) desc, &agg);
            if (st != noErr || agg == kAudioObjectUnknown)
            {
                log ("AudioHardwareCreateAggregateDevice failed: " + juce::String ((int) st));
                stop();
                return "Could not create audio device (" + juce::String ((int) st) + ")";
            }

            UInt32 frames = 256;
            auto bufAddr = addr (kAudioDevicePropertyBufferFrameSize);
            AudioObjectSetPropertyData (agg, &bufAddr, 0, nullptr, sizeof (frames), &frames);

            Float64 rate = 48000.0;
            getValue (agg, kAudioDevicePropertyNominalSampleRate, rate);
            sampleRate = rate;
            engine.prepare (rate, kMaxFrames);

            log ("aggregate created: rate=" + juce::String (rate) + " outChannels=" + juce::String (countChannels (agg, kAudioObjectPropertyScopeOutput))
                 + " inChannels=" + juce::String (countChannels (agg, kAudioObjectPropertyScopeInput))
                 + " inStreams=" + juce::String ((int) getIDs (agg, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput).size())
                 + " devInStreams=" + juce::String ((int) getIDs (outDev, kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput).size())
                 + " taps=" + juce::String (numTaps) + " mixSlot=" + juce::String (mixSlot) + " refSlot=" + juce::String (refSlot));

            Impl* self = this;
            st = AudioDeviceCreateIOProcIDWithBlock (&procID, agg, nullptr,
                ^(const AudioTimeStamp*, const AudioBufferList* inData, const AudioTimeStamp*,
                  AudioBufferList* outData, const AudioTimeStamp*)
                {
                    self->render (inData, outData);
                });
            if (st != noErr)
            {
                log ("AudioDeviceCreateIOProcIDWithBlock failed: " + juce::String ((int) st));
                stop();
                return "Could not start audio (" + juce::String ((int) st) + ")";
            }
            st = AudioDeviceStart (agg, procID);
            if (st != noErr)
            {
                log ("AudioDeviceStart failed: " + juce::String ((int) st));
                stop();
                return "Could not start audio (" + juce::String ((int) st) + ")";
            }
            running = true;
            firstRenderLogged = false;
            return {};
        }
    }

    void stop()
    {
        if (agg != kAudioObjectUnknown)
        {
            if (procID != nullptr)
            {
                AudioDeviceStop (agg, procID);
                AudioDeviceDestroyIOProcID (agg, procID);
                procID = nullptr;
            }
            AudioHardwareDestroyAggregateDevice (agg);
            agg = kAudioObjectUnknown;
        }
        for (auto t : taps) AudioHardwareDestroyProcessTap (t);
        if (! taps.empty()) log ("stopped (" + juce::String ((int) taps.size()) + " taps removed)");
        taps.clear();
        numTaps = 0;
        running = false;
    }

    // ---- オーディオスレッド ----
    bool readTap (const AudioBufferList* in, int slot, float* L, float* R, int frames) noexcept
    {
        if (in == nullptr || slot < 0) return false;
        const int bi = (int) in->mNumberBuffers - numTaps + slot;
        if (bi < 0 || bi >= (int) in->mNumberBuffers) return false;
        const AudioBuffer& b = in->mBuffers[bi];
        const int nch = (int) b.mNumberChannels;
        if (b.mData == nullptr || nch == 0) return false;
        const auto* d = static_cast<const float*> (b.mData);
        const int avail = juce::jmin (frames, (int) (b.mDataByteSize / (sizeof (float) * (UInt32) nch)));
        for (int i = 0; i < avail; ++i)
        {
            L[i] = d[i * nch];
            R[i] = nch > 1 ? d[i * nch + 1] : d[i * nch];
        }
        for (int i = avail; i < frames; ++i) L[i] = R[i] = 0.0f;
        return true;
    }

    void render (const AudioBufferList* in, AudioBufferList* out) noexcept
    {
        int frames = 0;
        if (out != nullptr && out->mNumberBuffers > 0 && out->mBuffers[0].mNumberChannels > 0)
            frames = (int) (out->mBuffers[0].mDataByteSize / (sizeof (float) * out->mBuffers[0].mNumberChannels));
        else if (in != nullptr && in->mNumberBuffers > 0 && in->mBuffers[0].mNumberChannels > 0)
            frames = (int) (in->mBuffers[0].mDataByteSize / (sizeof (float) * in->mBuffers[0].mNumberChannels));
        frames = juce::jmin (frames, kMaxFrames);
        if (frames <= 0) return;

        if (! firstRenderLogged)
        {
            firstRenderLogged = true;
            firstInBuffers = in != nullptr ? (int) in->mNumberBuffers : -1;
            firstOutBuffers = out != nullptr ? (int) out->mNumberBuffers : -1;
            firstFrames = frames;
        }

        const bool hasMix = readTap (in, mixSlot, mixL.data(), mixR.data(), frames);
        const bool hasRef = readTap (in, refSlot, liveL.data(), liveR.data(), frames);
        engine.process (hasMix ? mixL.data() : nullptr, hasMix ? mixR.data() : nullptr,
                        hasRef ? liveL.data() : nullptr, hasRef ? liveR.data() : nullptr,
                        outL.data(), outR.data(), frames);

        if (out == nullptr) return;
        int chBase = 0;
        for (UInt32 b = 0; b < out->mNumberBuffers; ++b)
        {
            auto& buf = out->mBuffers[b];
            const int nch = (int) buf.mNumberChannels;
            auto* d = static_cast<float*> (buf.mData);
            if (d == nullptr || nch == 0) { chBase += nch; continue; }
            const int len = juce::jmin (frames, (int) (buf.mDataByteSize / (sizeof (float) * (UInt32) nch)));
            for (int i = 0; i < len; ++i)
                for (int c = 0; c < nch; ++c)
                {
                    const int g = chBase + c;
                    d[i * nch + c] = g == 0 ? outL[(size_t) i] : (g == 1 ? outR[(size_t) i] : 0.0f);
                }
            chBase += nch;
        }
    }

    static constexpr int kMaxFrames = 8192;
    Engine& engine;
    AudioObjectID agg = kAudioObjectUnknown;
    AudioDeviceIOProcID procID = nullptr;
    std::vector<AudioObjectID> taps;
    int numTaps = 0, mixSlot = -1, refSlot = -1;
    std::vector<float> mixL, mixR, liveL, liveR, outL, outR;
    std::atomic<bool> running { false };
    std::atomic<bool> firstRenderLogged { false };
    std::atomic<int> firstInBuffers { 0 }, firstOutBuffers { 0 }, firstFrames { 0 };
    bool firstRenderReported = false;
    double sampleRate = 48000.0;
    juce::StringArray logLines;
};

// ============================================================================
CaptureEngine::CaptureEngine (Engine& e) : impl (std::make_unique<Impl> (e)), engine (e) {}
CaptureEngine::~CaptureEngine() { stop(); }

juce::Array<OutputDevice> CaptureEngine::listOutputDevices()
{
    juce::Array<OutputDevice> result;
    for (auto d : getIDs (kAudioObjectSystemObject, kAudioHardwarePropertyDevices))
    {
        if (countChannels (d, kAudioObjectPropertyScopeOutput) < 1) continue;
        const auto uid = getString (d, kAudioDevicePropertyDeviceUID);
        const auto name = getString (d, kAudioObjectPropertyName);
        if (uid.isEmpty() || name.containsIgnoreCase ("Reference Monitor")) continue;
        result.add ({ uid, name });
    }
    return result;
}

juce::String CaptureEngine::defaultOutputUID()
{
    AudioObjectID d = kAudioObjectUnknown;
    getValue (kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, d);
    return d != kAudioObjectUnknown ? getString (d, kAudioDevicePropertyDeviceUID) : juce::String();
}

std::vector<AppAudioProcess> CaptureEngine::listAudioProcesses()
{
    std::vector<AppAudioProcess> result;
    @autoreleasepool
    {
        const int me = (int) getpid();
        for (auto obj : getIDs (kAudioObjectSystemObject, kAudioHardwarePropertyProcessObjectList))
        {
            AppAudioProcess p;
            pid_t pid = -1;
            getValue (obj, kAudioProcessPropertyPID, pid);
            p.pid = (int) pid;
            if (p.pid == me) continue;
            p.bundleId = getString (obj, kAudioProcessPropertyBundleID);
            UInt32 running = 0;
            getValue (obj, kAudioProcessPropertyIsRunningOutput, running);
            p.runningOutput = running != 0;
            NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
            if (app != nil && app.localizedName != nil)
                p.name = juce::String::fromUTF8 ([app.localizedName UTF8String]);
            if (p.name.isEmpty()) p.name = p.bundleId;
            result.push_back (p);
        }
    }
    return result;
}

static std::vector<AudioObjectID> processObjectsFor (const juce::String& bundle)
{
    std::vector<AudioObjectID> v;
    if (bundle.isEmpty()) return v;
    const int me = (int) getpid();
    for (auto obj : getIDs (kAudioObjectSystemObject, kAudioHardwarePropertyProcessObjectList))
    {
        pid_t pid = -1;
        getValue (obj, kAudioProcessPropertyPID, pid);
        if ((int) pid == me) continue;
        if (CaptureEngine::matches (getString (obj, kAudioProcessPropertyBundleID), bundle))
            v.push_back (obj);
    }
    return v;
}

juce::String CaptureEngine::makeSignature (const juce::String& outputUID, const juce::String& mixBundle, const juce::String& refBundle,
                                           const std::vector<AppAudioProcess>& procs)
{
    juce::StringArray m, r;
    for (auto& p : procs)
    {
        if (matches (p.bundleId, mixBundle)) m.add (juce::String (p.pid));
        if (matches (p.bundleId, refBundle)) r.add (juce::String (p.pid));
    }
    m.sort (false); r.sort (false);
    return outputUID + "|M:" + m.joinIntoString (",") + "|R:" + r.joinIntoString (",");
}

juce::String CaptureEngine::start (const juce::String& outputUID, const juce::String& mixBundle, const juce::String& refBundle)
{
    const auto procs = listAudioProcesses();
    const auto mixObjs = processObjectsFor (mixBundle);
    const auto refObjs = processObjectsFor (refBundle);
    mixCount = (int) mixObjs.size();
    refCount = (int) refObjs.size();
    impl->log ("start: output=" + outputUID + " mix=" + mixBundle + "(" + juce::String (mixCount) + ") ref="
               + refBundle + "(" + juce::String (refCount) + ")");
    const auto err = impl->start (outputUID, mixObjs, refObjs);
    signature = err.isEmpty() ? makeSignature (outputUID, mixBundle, refBundle, procs) : juce::String();
    if (err.isNotEmpty()) impl->log ("start error: " + err);
    return err;
}

void CaptureEngine::stop()
{
    impl->stop();
    signature.clear();
}

bool CaptureEngine::isRunning() const { return impl->running.load(); }
double CaptureEngine::getSampleRate() const { return impl->sampleRate; }

juce::String CaptureEngine::getLog() const
{
    if (impl->firstRenderLogged && ! impl->firstRenderReported)
    {
        impl->firstRenderReported = true;
        impl->log ("first render: inBuffers=" + juce::String (impl->firstInBuffers.load()) + " outBuffers="
                   + juce::String (impl->firstOutBuffers.load()) + " frames=" + juce::String (impl->firstFrames.load()));
    }
    return impl->logLines.joinIntoString ("\n");
}

#endif
