#pragma once
// オーディオファイルを丸ごと逆向きにして WAV で保存する（~/Music/Flipside）
#include <JuceHeader.h>

namespace fl
{
    struct ReverseResult
    {
        bool ok = false;
        juce::File out;
        juce::String message;
    };

    inline juce::File outputFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Flipside");
    }

    // trim = 頭と終わりの無音（-60 dB 未満）を切る。逆にすると元の終わりの無音が頭に来て、鳴り出しが遅れるため
    inline ReverseResult reverseFile (const juce::File& in, bool trim)
    {
        ReverseResult r;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (in));
        if (reader == nullptr) { r.message = "Can't read this file (WAV / AIFF / FLAC / MP3 / M4A)"; return r; }
        const double sr = reader->sampleRate;
        if (sr <= 0 || reader->lengthInSamples <= 0) { r.message = "The file is empty"; return r; }
        if (reader->lengthInSamples > (juce::int64) (sr * 60 * 20)) { r.message = "Too long (up to 20 minutes)"; return r; }

        const int ch = (int) juce::jlimit (1u, 8u, reader->numChannels);
        const int n = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> buf (ch, n);
        reader->read (&buf, 0, n, 0, true, ch > 1);

        int first = 0, last = n - 1;
        if (trim)
        {
            const float thr = juce::Decibels::decibelsToGain (-60.0f);
            auto loud = [&] (int i) { for (int c = 0; c < ch; ++c) if (std::abs (buf.getSample (c, i)) > thr) return true; return false; };
            while (first < n && ! loud (first)) ++first;
            while (last > first && ! loud (last)) --last;
            if (first >= n) { r.message = "The file is silent"; return r; }
            first = juce::jmax (0, first - (int) (0.002 * sr));
            last = juce::jmin (n - 1, last + (int) (0.002 * sr));
        }
        const int len = last - first + 1;
        juce::AudioBuffer<float> outBuf (ch, len);
        for (int c = 0; c < ch; ++c)
        {
            const float* s = buf.getReadPointer (c);
            float* d = outBuf.getWritePointer (c);
            for (int i = 0; i < len; ++i) d[i] = s[last - i];
        }
        // 端のプチッを防ぐ短いフェード
        const int fade = juce::jmin (len / 2, (int) (0.003 * sr));
        if (fade > 0)
        {
            outBuf.applyGainRamp (0, fade, 0.0f, 1.0f);
            outBuf.applyGainRamp (len - fade, fade, 1.0f, 0.0f);
        }

        auto folder = outputFolder();
        folder.createDirectory();
        auto out = folder.getChildFile (in.getFileNameWithoutExtension() + " reversed.wav").getNonexistentSibling();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (out.createOutputStream());
        if (os == nullptr) { r.message = "Can't write to " + folder.getFullPathName(); return r; }
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned int) ch, 24, {}, 0));
        if (w == nullptr) { r.message = "Can't make the WAV file"; return r; }
        os.release();
        w->writeFromAudioSampleBuffer (outBuf, 0, len);
        w.reset();

        r.ok = true;
        r.out = out;
        r.message = juce::String (len / sr, 2) + " s, " + (ch == 1 ? "mono" : juce::String (ch) + " ch");
        return r;
    }
}
