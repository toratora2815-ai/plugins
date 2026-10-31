#pragma once
// 参照曲リストとキュー（目印）、設定の保存
#include <JuceHeader.h>

struct Cue
{
    juce::String name;
    double t = 0.0;       // 参照曲の位置（秒）
    double mixT = -1.0;   // 自分の曲（Logic）の位置（SMPTE の秒）。未登録は -1
};

struct FileItem
{
    juce::String path, name;
    std::vector<Cue> cues;
};

class Library
{
public:
    std::vector<FileItem> files;
    std::map<juce::String, std::vector<Cue>> musicCues;   // ミュージックの曲（persistent ID）ごと
    juce::NamedValueSet settings;

    static juce::File storeFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/Reference Monitor/library.json");
    }

    static void sortCues (std::vector<Cue>& c)
    {
        std::sort (c.begin(), c.end(), [] (const Cue& a, const Cue& b) { return a.t < b.t; });
    }

    static juce::var cuesToVar (const std::vector<Cue>& cues)
    {
        juce::Array<juce::var> arr;
        for (auto& c : cues)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", c.name);
            o->setProperty ("t", c.t);
            o->setProperty ("m", c.mixT);
            arr.add (juce::var (o));
        }
        return arr;
    }

    static std::vector<Cue> cuesFromVar (const juce::var& v)
    {
        std::vector<Cue> out;
        if (auto* arr = v.getArray())
            for (auto& e : *arr)
                out.push_back ({ e.getProperty ("name", "Cue").toString(), (double) e.getProperty ("t", 0.0),
                                 (double) e.getProperty ("m", -1.0) });
        sortCues (out);
        return out;
    }

    void save() const
    {
        auto* root = new juce::DynamicObject();
        juce::Array<juce::var> fl;
        for (auto& f : files)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("path", f.path);
            o->setProperty ("name", f.name);
            o->setProperty ("cues", cuesToVar (f.cues));
            fl.add (juce::var (o));
        }
        root->setProperty ("files", fl);
        auto* mc = new juce::DynamicObject();
        for (auto& [id, cues] : musicCues)
            if (! cues.empty()) mc->setProperty (juce::Identifier (id), cuesToVar (cues));
        root->setProperty ("musicCues", juce::var (mc));
        auto* st = new juce::DynamicObject();
        for (auto& nv : settings) st->setProperty (nv.name, nv.value);
        root->setProperty ("settings", juce::var (st));

        auto f = storeFile();
        f.getParentDirectory().createDirectory();
        f.replaceWithText (juce::JSON::toString (juce::var (root)));
    }

    void load()
    {
        const auto v = juce::JSON::parse (storeFile());
        if (! v.isObject()) return;
        files.clear();
        if (auto* arr = v.getProperty ("files", {}).getArray())
            for (auto& e : *arr)
                files.push_back ({ e.getProperty ("path", "").toString(), e.getProperty ("name", "").toString(),
                                   cuesFromVar (e.getProperty ("cues", {})) });
        musicCues.clear();
        if (auto* o = v.getProperty ("musicCues", {}).getDynamicObject())
            for (auto& nv : o->getProperties())
                musicCues[nv.name.toString()] = cuesFromVar (nv.value);
        settings.clear();
        if (auto* o = v.getProperty ("settings", {}).getDynamicObject())
            for (auto& nv : o->getProperties())
                settings.set (nv.name, nv.value);
    }
};
