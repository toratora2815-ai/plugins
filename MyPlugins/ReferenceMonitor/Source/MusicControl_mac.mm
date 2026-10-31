#include "MusicControl.h"

#if JUCE_MAC
#import <Foundation/Foundation.h>

namespace
{
    // 戻り値: 結果（失敗時 nil）。errOut にエラー番号
    NSAppleEventDescriptor* runScript (NSString* source, int& errOut)
    {
        errOut = 0;
        NSAppleScript* s = [[NSAppleScript alloc] initWithSource:source];
        NSDictionary* err = nil;
        NSAppleEventDescriptor* r = [s executeAndReturnError:&err];
        [r retain];
        [s release];
        if (r == nil && err != nil)
        {
            NSNumber* n = err[NSAppleScriptErrorNumber];
            errOut = n != nil ? n.intValue : -1;
        }
        return [r autorelease];
    }

    juce::String str (NSAppleEventDescriptor* d)
    {
        if (d == nil || d.stringValue == nil) return {};
        return juce::String::fromUTF8 ([d.stringValue UTF8String]);
    }

    double num (NSAppleEventDescriptor* d)
    {
        if (d == nil) return 0.0;
        if (d.stringValue != nil) return juce::String::fromUTF8 ([d.stringValue UTF8String]).replaceCharacter (',', '.').getDoubleValue();
        return 0.0;
    }
}

MusicControl::State MusicControl::poll()
{
    State st;
    @autoreleasepool
    {
        static NSString* src =
            @"if application \"Music\" is running then\n"
             "  with timeout of 2 seconds\n"
             "    tell application \"Music\"\n"
             "      set st to (player state as string)\n"
             "      if st is \"stopped\" then return {st, \"0\", \"\", \"\", \"0\", \"\"}\n"
             "      set t to current track\n"
             "      return {st, (player position as string), (name of t), (artist of t), ((duration of t) as string), (persistent ID of t)}\n"
             "    end tell\n"
             "  end timeout\n"
             "else\n"
             "  return {\"notrunning\", \"0\", \"\", \"\", \"0\", \"\"}\n"
             "end if";
        int err = 0;
        NSAppleEventDescriptor* r = runScript (src, err);
        if (r == nil)
        {
            st.error = err == -1743 ? "not-allowed" : ("error " + juce::String (err));
            return st;
        }
        if (r.numberOfItems < 6) { st.error = "unexpected reply"; return st; }
        const auto state = str ([r descriptorAtIndex:1]);
        st.running  = state != "notrunning";
        st.playing  = state == "playing";
        st.position = num ([r descriptorAtIndex:2]);
        st.title    = str ([r descriptorAtIndex:3]);
        st.artist   = str ([r descriptorAtIndex:4]);
        st.duration = num ([r descriptorAtIndex:5]);
        st.trackId  = str ([r descriptorAtIndex:6]);
    }
    return st;
}

void MusicControl::setPosition (double seconds)
{
    @autoreleasepool
    {
        int err = 0;
        NSString* src = [NSString stringWithFormat:@"tell application \"Music\" to set player position to %.3f", juce::jmax (0.0, seconds)];
        runScript (src, err);
    }
}

void MusicControl::play()
{
    @autoreleasepool { int err = 0; runScript (@"tell application \"Music\" to play", err); }
}

void MusicControl::pause()
{
    @autoreleasepool { int err = 0; runScript (@"tell application \"Music\" to pause", err); }
}

#endif
