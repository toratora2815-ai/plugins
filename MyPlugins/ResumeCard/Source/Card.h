#pragma once
#include <JuceHeader.h>
#include "MacCapture.h"

// 1 枚のカード = 離れる前に「どこで何をしていたか」
struct Card
{
    juce::String id;           // 例 20261106-143205
    juce::Time when;           // 取った時刻（離れる直前に使っていた時）
    juce::Time leftAt;         // 離れた時刻
    juce::String why;          // lock / sleep / idle / screensaver / manual
    rc::Capture cap;
    juce::String note;         // 次の自分へのメモ
    bool hasShot = false;

    bool valid() const { return id.isNotEmpty(); }

    static juce::File root()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support").getChildFile ("Resume Card");
    }
    static juce::File cardsDir() { return root().getChildFile ("cards"); }
    juce::File jsonFile() const { return cardsDir().getChildFile (id + ".json"); }
    juce::File shotFile() const { return cardsDir().getChildFile (id + ".jpg"); }

    static juce::String newId (juce::Time t) { return t.formatted ("%Y%m%d-%H%M%S"); }

    juce::String whyText() const
    {
        if (why == "lock") return juce::String::fromUTF8 ("画面ロック");
        if (why == "sleep") return juce::String::fromUTF8 ("スリープ");
        if (why == "idle") return juce::String::fromUTF8 ("しばらく操作なし");
        if (why == "screensaver") return juce::String::fromUTF8 ("スクリーンセーバー");
        if (why == "manual") return juce::String::fromUTF8 ("自分で中断");
        return why;
    }

    //==========================================================================
    static juce::var appVar (const rc::AppInfo& a)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String::fromUTF8 (a.name.c_str()));
        o->setProperty ("bundle", juce::String::fromUTF8 (a.bundleId.c_str()));
        o->setProperty ("appPath", juce::String::fromUTF8 (a.appPath.c_str()));
        o->setProperty ("window", juce::String::fromUTF8 (a.window.c_str()));
        o->setProperty ("doc", juce::String::fromUTF8 (a.doc.c_str()));
        return o;
    }
    static rc::AppInfo varApp (const juce::var& v)
    {
        rc::AppInfo a;
        a.name = v["name"].toString().toStdString();
        a.bundleId = v["bundle"].toString().toStdString();
        a.appPath = v["appPath"].toString().toStdString();
        a.window = v["window"].toString().toStdString();
        a.doc = v["doc"].toString().toStdString();
        return a;
    }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", id);
        o->setProperty ("when", when.toMilliseconds());
        o->setProperty ("leftAt", leftAt.toMilliseconds());
        o->setProperty ("why", why);
        o->setProperty ("note", note);
        o->setProperty ("shot", hasShot);
        o->setProperty ("front", appVar (cap.front));
        o->setProperty ("typed", juce::String::fromUTF8 (cap.typed.c_str()));
        o->setProperty ("selected", juce::String::fromUTF8 (cap.selected.c_str()));
        o->setProperty ("finder", juce::String::fromUTF8 (cap.finderPath.c_str()));
        juce::Array<juce::var> others, tabs;
        for (auto& a : cap.others) others.add (appVar (a));
        for (auto& t : cap.tabs)
        {
            auto* x = new juce::DynamicObject();
            x->setProperty ("browser", juce::String::fromUTF8 (t.browser.c_str()));
            x->setProperty ("bundle", juce::String::fromUTF8 (t.bundleId.c_str()));
            x->setProperty ("title", juce::String::fromUTF8 (t.title.c_str()));
            x->setProperty ("url", juce::String::fromUTF8 (t.url.c_str()));
            tabs.add (x);
        }
        o->setProperty ("others", others);
        o->setProperty ("tabs", tabs);
        return o;
    }

    static Card fromVar (const juce::var& v)
    {
        Card c;
        c.id = v["id"].toString();
        c.when = juce::Time ((juce::int64) v["when"]);
        c.leftAt = juce::Time ((juce::int64) v["leftAt"]);
        c.why = v["why"].toString();
        c.note = v["note"].toString();
        c.hasShot = (bool) v["shot"];
        c.cap.front = varApp (v["front"]);
        c.cap.typed = v["typed"].toString().toStdString();
        c.cap.selected = v["selected"].toString().toStdString();
        c.cap.finderPath = v["finder"].toString().toStdString();
        if (auto* a = v["others"].getArray()) for (auto& x : *a) c.cap.others.push_back (varApp (x));
        if (auto* a = v["tabs"].getArray())
            for (auto& x : *a)
                c.cap.tabs.push_back ({ x["browser"].toString().toStdString(), x["bundle"].toString().toStdString(),
                                        x["title"].toString().toStdString(), x["url"].toString().toStdString() });
        return c;
    }

    bool save() const
    {
        cardsDir().createDirectory();
        return jsonFile().replaceWithText (juce::JSON::toString (toVar()));
    }
};

// 保存してあるカード（新しい順）。多すぎたら古いものを消す
struct CardStore
{
    static constexpr int keep = 60;

    static juce::Array<Card> loadAll()
    {
        juce::Array<Card> out;
        auto files = Card::cardsDir().findChildFiles (juce::File::findFiles, false, "*.json");
        files.sort();
        for (int i = files.size(); --i >= 0;)
        {
            auto c = Card::fromVar (juce::JSON::parse (files[i]));
            if (c.valid()) out.add (c);
            if (out.size() >= keep) break;
        }
        // 多すぎる分は消す
        for (int i = 0; i < files.size() - keep; ++i)
        {
            files[i].withFileExtension ("jpg").deleteFile();
            files[i].deleteFile();
        }
        return out;
    }

    static void remove (const Card& c)
    {
        c.jsonFile().deleteFile();
        c.shotFile().deleteFile();
    }
};
