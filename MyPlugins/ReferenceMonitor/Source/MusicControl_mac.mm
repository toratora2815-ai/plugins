#import <Foundation/Foundation.h>
#include "MusicControl.h"

#if JUCE_MAC

namespace
{
    void logLine (const juce::String& s)
    {
        auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                     .getChildFile ("Application Support/Reference Monitor/log.txt");
        f.getParentDirectory().createDirectory();
        f.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  [music] " + s + "\n");
    }

    // 戻り値: 結果（失敗時 nil）。errOut にエラー番号、msgOut にエラー文
    NSAppleEventDescriptor* runScript (NSAppleScript* s, int& errOut, juce::String& msgOut)
    {
        errOut = 0;
        NSDictionary* err = nil;
        NSAppleEventDescriptor* r = [s executeAndReturnError:&err];
        if (r == nil && err != nil)
        {
            NSNumber* n = err[NSAppleScriptErrorNumber];
            errOut = n != nil ? n.intValue : -1;
            NSString* m = err[NSAppleScriptErrorMessage];
            if (m != nil) msgOut = juce::String::fromUTF8 ([m UTF8String]);
        }
        return r;
    }

    void runOnce (NSString* source)
    {
        NSAppleScript* s = [[NSAppleScript alloc] initWithSource:source];
        int err = 0; juce::String msg;
        runScript (s, err, msg);
        if (err != 0) logLine ("command failed (" + juce::String (err) + "): " + msg);
        [s release];
    }

    juce::String str (NSAppleEventDescriptor* d)
    {
        if (d == nil || d.stringValue == nil) return {};
        return juce::String::fromUTF8 ([d.stringValue UTF8String]);
    }

    double num (NSAppleEventDescriptor* d)
    {
        return str (d).replaceCharacter (',', '.').getDoubleValue();
    }
}

MusicControl::State MusicControl::poll()
{
    State st;
    @autoreleasepool
    {
        // 曲の情報は1つずつ try で取る（ストリーミングの曲は取れない項目があるため）
        static NSAppleScript* script = [[NSAppleScript alloc] initWithSource:
            @"if application \"Music\" is running then\n"
             "  with timeout of 2 seconds\n"
             "    tell application \"Music\"\n"
             "      set st to (player state as string)\n"
             "      set pos to \"0\"\n"
             "      set nm to \"\"\n"
             "      set ar to \"\"\n"
             "      set du to \"0\"\n"
             "      set pid to \"\"\n"
             "      try\n"
             "        set pos to (player position as string)\n"
             "      end try\n"
             "      try\n"
             "        set t to current track\n"
             "        try\n"
             "          set nm to (name of t)\n"
             "        end try\n"
             "        try\n"
             "          set ar to (artist of t)\n"
             "        end try\n"
             "        try\n"
             "          set du to ((duration of t) as string)\n"
             "        end try\n"
             "        try\n"
             "          set pid to (persistent ID of t)\n"
             "        end try\n"
             "      end try\n"
             "      if nm is \"\" then\n"
             "        try\n"
             "          set nm to (current stream title)\n"
             "        end try\n"
             "      end if\n"
             "      return {st, pos, nm, ar, du, pid}\n"
             "    end tell\n"
             "  end timeout\n"
             "else\n"
             "  return {\"notrunning\", \"0\", \"\", \"\", \"0\", \"\"}\n"
             "end if"];

        int err = 0; juce::String msg;
        NSAppleEventDescriptor* r = runScript (script, err, msg);
        if (r == nil)
        {
            st.error = err == -1743 ? juce::String ("not-allowed") : ("error " + juce::String (err) + ": " + msg);
            if (st.error != lastError) logLine ("poll failed: " + st.error);
            lastError = st.error;
            return st;
        }
        if (r.numberOfItems < 6)
        {
            st.error = "unexpected reply";
            return st;
        }
        const auto state = str ([r descriptorAtIndex:1]);
        st.running  = state != "notrunning";
        st.playing  = state == "playing";
        st.position = num ([r descriptorAtIndex:2]);
        st.title    = str ([r descriptorAtIndex:3]);
        st.artist   = str ([r descriptorAtIndex:4]);
        st.duration = num ([r descriptorAtIndex:5]);
        const auto pid = str ([r descriptorAtIndex:6]);
        // 曲を見分ける名前（キューの保存先）。ID が取れない曲は「曲名|アーティスト」で見分ける
        st.trackId = pid.isNotEmpty() ? pid
                   : (st.title.isNotEmpty() ? "t:" + st.title + "|" + st.artist : juce::String());

        const auto summary = state + " id=" + (pid.isNotEmpty() ? "yes" : "no") + " title=" + (st.title.isNotEmpty() ? "yes" : "no")
                           + " dur=" + juce::String (st.duration, 1);
        if (summary != lastSummary) { logLine ("state: " + summary); lastSummary = summary; }
        lastError.clear();
    }
    return st;
}

void MusicControl::setPosition (double seconds)
{
    @autoreleasepool
    {
        runOnce ([NSString stringWithFormat:@"tell application \"Music\" to set player position to %.3f", juce::jmax (0.0, seconds)]);
    }
}

void MusicControl::play()      { @autoreleasepool { runOnce (@"tell application \"Music\" to play"); } }
void MusicControl::pause()     { @autoreleasepool { runOnce (@"tell application \"Music\" to pause"); } }
void MusicControl::playPause() { @autoreleasepool { runOnce (@"tell application \"Music\" to playpause"); } }

#endif
