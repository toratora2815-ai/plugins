#pragma once
// バウンスの一覧（曲ごとの版）と、見張るフォルダ・設定の保存
#include <JuceHeader.h>
#include "Analysis.h"

struct Version
{
    juce::String id;                 // 一意の名前
    juce::String song, name;         // 曲、ファイル名（拡張子なし）
    juce::String sourcePath;         // 元のバウンス
    juce::String archivePath;        // アプリがとっておいたコピー（なければ空）
    juce::int64 fileTime = 0, fileSize = 0;   // 元ファイルの更新時刻（ms）と大きさ
    juce::String note;
    bool keeper = false;
    int wins = 0, rounds = 0;        // ブラインドテストで選ばれた回数 / 出た回数
    mtm::Analysis an;
    bool analysing = false;          // 保存しない

    juce::String playPath() const
    {
        if (archivePath.isNotEmpty() && juce::File (archivePath).existsAsFile()) return archivePath;
        if (! sourceChanged()) return sourcePath;   // 上書きされた元ファイルは、もう別の音
        return {};
    }
    bool sourceChanged() const
    {
        const juce::File f (sourcePath);
        return ! f.existsAsFile() || f.getLastModificationTime().toMilliseconds() != fileTime || f.getSize() != fileSize;
    }
};

class Library
{
public:
    std::vector<Version> versions;
    juce::StringArray roots;                   // 見張るフォルダ
    juce::StringArray baselinedRoots;          // 最初の取り込みが済んだフォルダ（それ以前のファイルはコピーしない）
    std::map<juce::String, juce::String> songOverrides;   // 元ファイル → 曲（手で移したもの）
    juce::StringArray ignored;                 // 一覧から外したもの（"元ファイル|更新時刻"）。同じ名前で書き出し直すとまた出る

    static juce::String ignoreKey (const juce::String& path, juce::int64 time) { return path + "|" + juce::String (time); }
    juce::NamedValueSet settings;

    static juce::File storeFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/Mix Time Machine/library.json");
    }

    static juce::File archiveRoot()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Mix Time Machine");
    }

    Version* find (const juce::String& id)
    {
        for (auto& v : versions) if (v.id == id) return &v;
        return nullptr;
    }

    // その元ファイルの、いちばん新しい版
    Version* latestForSource (const juce::String& path)
    {
        Version* best = nullptr;
        for (auto& v : versions)
            if (v.sourcePath == path && (best == nullptr || v.fileTime > best->fileTime)) best = &v;
        return best;
    }

    juce::StringArray songs() const
    {
        // 新しい版がある曲から順に
        std::map<juce::String, juce::int64> latest;
        for (auto& v : versions) latest[v.song] = juce::jmax (latest[v.song], v.fileTime);
        std::vector<std::pair<juce::String, juce::int64>> s (latest.begin(), latest.end());
        std::sort (s.begin(), s.end(), [] (auto& a, auto& b) { return a.second > b.second; });
        juce::StringArray out;
        for (auto& p : s) out.add (p.first);
        return out;
    }

    std::vector<Version*> versionsOf (const juce::String& song)
    {
        std::vector<Version*> out;
        for (auto& v : versions) if (v.song == song) out.push_back (&v);
        std::sort (out.begin(), out.end(), [] (Version* a, Version* b) { return a->fileTime > b->fileTime; });
        return out;
    }

    // ---- 保存 ----
    static juce::var analysisToVar (const mtm::Analysis& a)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", a.ok);
        o->setProperty ("lufs", a.lufs);
        o->setProperty ("peak", a.peakDb);
        o->setProperty ("plr", a.plr);
        o->setProperty ("lra", a.loudRange);
        o->setProperty ("side", a.sideRatio);
        o->setProperty ("len", a.lengthSec);
        juce::Array<juce::var> b;
        for (float v : a.bands) b.add (juce::roundToInt (v * 100.0f) / 100.0);
        o->setProperty ("bands", b);
        return juce::var (o);
    }

    static mtm::Analysis analysisFromVar (const juce::var& v)
    {
        mtm::Analysis a;
        a.ok = (bool) v.getProperty ("ok", false);
        a.lufs = (float) (double) v.getProperty ("lufs", -200.0);
        a.peakDb = (float) (double) v.getProperty ("peak", -120.0);
        a.plr = (float) (double) v.getProperty ("plr", 0.0);
        a.loudRange = (float) (double) v.getProperty ("lra", 0.0);
        a.sideRatio = (float) (double) v.getProperty ("side", 0.0);
        a.lengthSec = (float) (double) v.getProperty ("len", 0.0);
        if (auto* b = v.getProperty ("bands", {}).getArray())
            for (int i = 0; i < juce::jmin (mtm::kNumBands, b->size()); ++i) a.bands[(size_t) i] = (float) (double) (*b)[i];
        if (a.ok && (v.getProperty ("bands", {}).getArray() == nullptr)) a.ok = false;
        return a;
    }

    void save() const
    {
        auto* root = new juce::DynamicObject();
        juce::Array<juce::var> vl;
        for (auto& v : versions)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", v.id);
            o->setProperty ("song", v.song);
            o->setProperty ("name", v.name);
            o->setProperty ("source", v.sourcePath);
            o->setProperty ("archive", v.archivePath);
            o->setProperty ("time", v.fileTime);
            o->setProperty ("size", v.fileSize);
            o->setProperty ("note", v.note);
            o->setProperty ("keeper", v.keeper);
            o->setProperty ("wins", v.wins);
            o->setProperty ("rounds", v.rounds);
            o->setProperty ("an", analysisToVar (v.an));
            vl.add (juce::var (o));
        }
        root->setProperty ("versions", vl);
        root->setProperty ("roots", juce::var (roots));
        root->setProperty ("baselined", juce::var (baselinedRoots));
        root->setProperty ("ignored", juce::var (ignored));
        juce::Array<juce::var> ov;
        for (auto& [path, song] : songOverrides)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("path", path);
            o->setProperty ("song", song);
            ov.add (juce::var (o));
        }
        root->setProperty ("overrides", ov);
        auto* st = new juce::DynamicObject();
        for (auto& nv : settings) st->setProperty (nv.name, nv.value);
        root->setProperty ("settings", juce::var (st));

        auto f = storeFile();
        f.getParentDirectory().createDirectory();
        // 書きかけで壊れないように、一度別名に書いてから置き換える
        auto tmp = f.getSiblingFile ("library.json.tmp");
        if (tmp.replaceWithText (juce::JSON::toString (juce::var (root))))
            tmp.moveFileTo (f);
    }

    void load()
    {
        const auto v = juce::JSON::parse (storeFile());
        if (! v.isObject()) return;
        versions.clear();
        if (auto* arr = v.getProperty ("versions", {}).getArray())
            for (auto& e : *arr)
            {
                Version x;
                x.id = e.getProperty ("id", "").toString();
                x.song = e.getProperty ("song", "").toString();
                x.name = e.getProperty ("name", "").toString();
                x.sourcePath = e.getProperty ("source", "").toString();
                x.archivePath = e.getProperty ("archive", "").toString();
                x.fileTime = (juce::int64) e.getProperty ("time", 0);
                x.fileSize = (juce::int64) e.getProperty ("size", 0);
                x.note = e.getProperty ("note", "").toString();
                x.keeper = (bool) e.getProperty ("keeper", false);
                x.wins = (int) e.getProperty ("wins", 0);
                x.rounds = (int) e.getProperty ("rounds", 0);
                x.an = analysisFromVar (e.getProperty ("an", {}));
                if (x.id.isNotEmpty()) versions.push_back (x);
            }
        auto strings = [] (const juce::var& a)
        {
            juce::StringArray s;
            if (auto* arr = a.getArray()) for (auto& e : *arr) s.add (e.toString());
            return s;
        };
        roots = strings (v.getProperty ("roots", {}));
        baselinedRoots = strings (v.getProperty ("baselined", {}));
        ignored = strings (v.getProperty ("ignored", {}));
        songOverrides.clear();
        if (auto* arr = v.getProperty ("overrides", {}).getArray())
            for (auto& e : *arr) songOverrides[e.getProperty ("path", "").toString()] = e.getProperty ("song", "").toString();
        settings.clear();
        if (auto* o = v.getProperty ("settings", {}).getDynamicObject())
            for (auto& nv : o->getProperties()) settings.set (nv.name, nv.value);
    }
};
