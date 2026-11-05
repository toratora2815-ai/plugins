#pragma once
#include <JuceHeader.h>
#include "SketchEngine.h"

// 音のフォルダを読んで、1 つずつ特徴を覚えておく（裏のスレッドで）。
// 結果は ~/Library/Application Support/Sound Sketch/index.bin に保存し、次からは変わったファイルだけ読み直す。
class Library : private juce::Thread
{
public:
    struct Entry
    {
        juce::String path;
        juce::int64 mtime = 0, size = 0;
        float fileSeconds = 0;     // ファイル全体の長さ
        uint32_t kinds = 0;        // 1 << sketch::Kind
        sketch::Features f;
        juce::String name() const   { return path.fromLastOccurrenceOf ("/", false, false); }
        juce::String folder() const { return path.upToLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("/", false, false); }
    };
    using Entries = std::vector<Entry>;

    static bool isAudio (const juce::File& f)
    {
        static const juce::StringArray ext { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".caf", ".m4a", ".aac" };
        return ext.contains (f.getFileExtension().toLowerCase());
    }

    static juce::File dataDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support").getChildFile ("Sound Sketch");
    }

    Library() : juce::Thread ("Sound Sketch scan") { load(); }
    ~Library() override { stopThread (4000); }

    // 読むフォルダを決めて読み直す（変わっていないファイルは前の結果を使う）
    void scan (const juce::StringArray& folderPaths)
    {
        stopThread (4000);
        { const juce::ScopedLock sl (lock); folders = folderPaths; }
        done = 0; total = 0;
        startThread (juce::Thread::Priority::low);
    }

    bool scanning() const { return isThreadRunning(); }
    int progressDone() const { return done.load(); }
    int progressTotal() const { return total.load(); }
    // 読み直しのたびに増える（画面がこれを見て並べ直す）
    int generation() const { return gen.load(); }

    std::shared_ptr<const Entries> entries() const
    {
        const juce::ScopedLock sl (lock);
        return current;
    }

    // ファイルを 1 つ読んで、頭から最大 6 秒をモノラルにする
    static bool readMono (juce::AudioFormatManager& fm, const juce::File& file, std::vector<float>& mono, double& fs, double& fileSeconds)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
        if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate <= 0) return false;
        fs = r->sampleRate;
        fileSeconds = (double) r->lengthInSamples / r->sampleRate;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * sketch::kMaxRead));
        const int ch = (int) juce::jlimit (1u, 2u, r->numChannels);
        juce::AudioBuffer<float> b (ch, n);
        if (! r->read (&b, 0, n, 0, true, ch > 1)) return false;
        mono.assign ((size_t) n, 0.0f);
        for (int c = 0; c < ch; ++c)
        {
            const float* p = b.getReadPointer (c);
            for (int i = 0; i < n; ++i) mono[(size_t) i] += p[i] / (float) ch;
        }
        return true;
    }

    static Entry analyseFile (juce::AudioFormatManager& fm, const juce::File& file)
    {
        Entry e;
        e.path = file.getFullPathName();
        e.mtime = file.getLastModificationTime().toMilliseconds();
        e.size = file.getSize();
        std::vector<float> mono; double fs = 0, secs = 0;
        if (readMono (fm, file, mono, fs, secs))
        {
            e.f = sketch::analyse (mono.data(), (int) mono.size(), fs);
            e.fileSeconds = (float) secs;
        }
        e.kinds = sketch::guessKinds (e.path.toLowerCase().toStdString(), std::max (e.f.seconds, e.fileSeconds > 4.5f ? e.fileSeconds : 0.0f));
        return e;
    }

private:
    mutable juce::CriticalSection lock;
    juce::StringArray folders;
    std::shared_ptr<const Entries> current = std::make_shared<Entries>();
    std::atomic<int> done { 0 }, total { 0 }, gen { 0 };

    void publish (Entries e)
    {
        auto p = std::make_shared<const Entries> (std::move (e));
        { const juce::ScopedLock sl (lock); current = p; }
        ++gen;
    }

    void run() override
    {
        juce::StringArray dirs;
        { const juce::ScopedLock sl (lock); dirs = folders; }

        // 1. ファイルを集める
        juce::Array<juce::File> files;
        for (auto& d : dirs)
        {
            const juce::File dir (d);
            if (! dir.isDirectory()) continue;
            for (const auto& it : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
            {
                if (threadShouldExit()) return;
                const auto& f = it.getFile();
                if (! it.isHidden() && isAudio (f)) files.add (f);
            }
        }
        total = files.size();

        // 2. 前の結果と同じ（場所・日時・大きさ）なら使い回す
        std::unordered_map<std::string, const Entry*> old;
        auto prev = entries();
        for (auto& e : *prev) old[e.path.toStdString()] = &e;

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        Entries out;
        out.reserve ((size_t) files.size());
        int sinceSave = 0;
        for (int i = 0; i < files.size(); ++i)
        {
            if (threadShouldExit()) break;
            const auto& f = files.getReference (i);
            auto it = old.find (f.getFullPathName().toStdString());
            if (it != old.end() && it->second->mtime == f.getLastModificationTime().toMilliseconds() && it->second->size == f.getSize())
                out.push_back (*it->second);
            else
            {
                out.push_back (analyseFile (fm, f));
                ++sinceSave;
            }
            done = i + 1;
            // 読みながらでも探せるように、ときどき途中の結果を出す
            if (sinceSave >= 300) { sinceSave = 0; auto tmp = out; mergeUnscanned (tmp, *prev, dirs); publish (std::move (tmp)); }
        }
        if (threadShouldExit())
        {
            mergeUnscanned (out, *prev, dirs);   // 途中でやめたときは、まだ読んでいない古い結果も残す
            publish (std::move (out));
            save();
            return;
        }
        publish (std::move (out));
        save();
    }

    // out に入っていない前の結果のうち、まだ読むフォルダの中にあるものを足す
    static void mergeUnscanned (Entries& out, const Entries& prev, const juce::StringArray& dirs)
    {
        std::unordered_set<std::string> have;
        for (auto& e : out) have.insert (e.path.toStdString());
        for (auto& e : prev)
        {
            if (have.count (e.path.toStdString())) continue;
            for (auto& d : dirs) if (e.path.startsWith (d)) { out.push_back (e); break; }
        }
    }

    //==========================================================================
    static constexpr juce::uint32 kMagic = 0x534b5431;   // "SKT1"

    juce::File cacheFile() const { return dataDir().getChildFile ("index.bin"); }

    void save()
    {
        auto e = entries();
        dataDir().createDirectory();
        juce::TemporaryFile tmp (cacheFile());
        {
            juce::FileOutputStream os (tmp.getFile());
            if (! os.openedOk()) return;
            os.writeInt ((int) kMagic);
            os.writeInt (sketch::kDims);
            os.writeInt ((int) e->size());
            for (auto& x : *e)
            {
                os.writeString (x.path);
                os.writeInt64 (x.mtime);
                os.writeInt64 (x.size);
                os.writeFloat (x.fileSeconds);
                os.writeInt ((int) x.kinds);
                os.writeBool (x.f.valid);
                os.writeFloat (x.f.seconds);
                for (float v : x.f.v) os.writeFloat (v);
            }
        }
        tmp.overwriteTargetFileWithTemporary();
    }

    void load()
    {
        juce::FileInputStream in (cacheFile());
        if (! in.openedOk()) return;
        if ((juce::uint32) in.readInt() != kMagic || in.readInt() != sketch::kDims) return;
        const int n = in.readInt();
        if (n < 0 || n > 2000000) return;
        Entries out;
        out.reserve ((size_t) n);
        for (int i = 0; i < n && ! in.isExhausted(); ++i)
        {
            Entry x;
            x.path = in.readString();
            x.mtime = in.readInt64();
            x.size = in.readInt64();
            x.fileSeconds = in.readFloat();
            x.kinds = (uint32_t) in.readInt();
            x.f.valid = in.readBool();
            x.f.seconds = in.readFloat();
            for (float& v : x.f.v) v = in.readFloat();
            out.push_back (std::move (x));
        }
        publish (std::move (out));
    }
};
