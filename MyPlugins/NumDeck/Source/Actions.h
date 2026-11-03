#pragma once
#include <JuceHeader.h>
#include "KeyTap.h"

// テンキーに割り当てられる動作（Logic Pro の初期設定のキーコマンド）
namespace numdeck
{
    enum class Icon { None, Play, Record, Rewind, Forward, ToStart, Cycle, Metronome, Undo, Redo, Split, Capture };
    enum class Cat { Transport, Edit, Track, Window, File, RefMon, General, Browser, Custom, App, Link };

    struct ActionDef
    {
        const char* id;
        const char* name;   // UTF-8
        Cat cat;
        int keyCode;        // Mac の仮想キーコード
        int mods;
        bool repeat;        // 押しっぱなしで連打するか
        Icon icon;
        const char* glyph;  // アイコンがないときに大きく出す文字
        const char* remote = nullptr; // Reference Monitor への命令（keyCode の代わり）
    };

    // キーコード（US 配列の位置。日本語配列でも英字・記号の位置は同じ）
    namespace vk
    {
        enum { A = 0, S = 1, Z = 6, X = 7, C = 8, B = 11, E = 14, R = 15, Y = 16, T = 17, O = 31, U = 32,
               I = 34, P = 35, Return = 36, J = 38, K = 40, Comma = 43, M = 46, Period = 47, Space = 49,
               Delete = 51, G = 5, D = 2, Left = 123, Right = 124,
               V = 9, F = 3, W = 13, L = 37, Tab = 48, LBracket = 33, RBracket = 30, Four = 21 };
    }

    inline const std::vector<ActionDef>& catalog()
    {
        static const std::vector<ActionDef> list = {
            // 再生・録音
            { "play",      "再生 / 停止",       Cat::Transport, vk::Space,  0, false, Icon::Play,      "" },
            { "record",    "録音",              Cat::Transport, vk::R,      0, false, Icon::Record,    "" },
            { "capture",   "キャプチャ録音",     Cat::Transport, vk::R,      kShift, false, Icon::Capture, "" },
            { "rewind",    "巻き戻し",          Cat::Transport, vk::Comma,  0, true,  Icon::Rewind,    "" },
            { "forward",   "早送り",            Cat::Transport, vk::Period, 0, true,  Icon::Forward,   "" },
            { "start",     "先頭へ",            Cat::Transport, vk::Return, 0, false, Icon::ToStart,   "" },
            { "cycle",     "サイクル",          Cat::Transport, vk::C,      0, false, Icon::Cycle,     "" },
            { "metronome", "メトロノーム",       Cat::Transport, vk::K,      0, false, Icon::Metronome, "" },
            { "locators",  "選択範囲をサイクルに", Cat::Transport, vk::U,    kCmd, false, Icon::None,  "U" },
            // 編集
            { "undo",      "元に戻す",          Cat::Edit, vk::Z,      kCmd,          true,  Icon::Undo,  "" },
            { "redo",      "やり直す",          Cat::Edit, vk::Z,      kCmd | kShift, true,  Icon::Redo,  "" },
            { "split",     "再生位置で分割",     Cat::Edit, vk::T,      kCmd,          false, Icon::Split, "" },
            { "join",      "結合",              Cat::Edit, vk::J,      kCmd,          false, Icon::None,  "J" },
            { "repeat",    "リピート（複製）",   Cat::Edit, vk::R,      kCmd,          false, Icon::None,  "x2" },
            { "delete",    "削除",              Cat::Edit, vk::Delete, 0,             false, Icon::None,  "DEL" },
            { "zoom",      "選択をズーム",       Cat::Edit, vk::Z,      0,             false, Icon::None,  "Z" },
            { "nudgeL",    "少し左へ",          Cat::Edit, vk::Left,   kOpt,          true,  Icon::None,  "<" },
            { "nudgeR",    "少し右へ",          Cat::Edit, vk::Right,  kOpt,          true,  Icon::None,  ">" },
            { "regionMute","リージョンをミュート", Cat::Edit, vk::M,    kCtrl,         false, Icon::None,  "RM" },
            { "bounceIP",  "その場でバウンス",   Cat::Edit, vk::B,      kCtrl,         false, Icon::None,  "BIP" },
            // トラック
            { "mute",      "トラックをミュート", Cat::Track, vk::M, 0,             false, Icon::None, "M" },
            { "solo",      "トラックをソロ",     Cat::Track, vk::S, 0,             false, Icon::None, "S" },
            { "automation","オートメーション表示", Cat::Track, vk::A, 0,           false, Icon::None, "A" },
            // Logic に最初から割り当てがない操作。Logic のキーコマンドで「トラックのフリーズを切り替え」にこのキーを覚えさせる
            { "freeze",    "トラックをフリーズ", Cat::Track, vk::F, kCmd | kOpt | kCtrl | kShift, false, Icon::None, "FRZ" },
            { "inputMon",  "入力モニタリング",   Cat::Track, vk::I, kCmd | kOpt | kCtrl | kShift, false, Icon::None, "I" },
            { "newAudio",  "新規オーディオトラック", Cat::Track, vk::A, kCmd | kOpt, false, Icon::None, "+AU" },
            { "newInst",   "新規ソフト音源トラック", Cat::Track, vk::S, kCmd | kOpt, false, Icon::None, "+INST" },
            { "dupTrack",  "トラックを複製",     Cat::Track, vk::D, kCmd,          false, Icon::None, "+DUP" },
            // ウィンドウ
            { "mixer",     "ミキサー",          Cat::Window, vk::X, 0, false, Icon::None, "MIX" },
            { "editor",    "エディタ",          Cat::Window, vk::E, 0, false, Icon::None, "EDIT" },
            { "pianoRoll", "ピアノロール",       Cat::Window, vk::P, 0, false, Icon::None, "PR" },
            { "library",   "ライブラリ",        Cat::Window, vk::Y, 0, false, Icon::None, "LIB" },
            { "loops",     "ループブラウザ",     Cat::Window, vk::O, 0, false, Icon::None, "LOOP" },
            { "smart",     "スマートコントロール", Cat::Window, vk::B, 0, false, Icon::None, "SC" },
            { "inspector", "インスペクタ",       Cat::Window, vk::I, 0, false, Icon::None, "INSP" },
            { "global",    "グローバルトラック", Cat::Window, vk::G, 0, false, Icon::None, "GLB" },
            // ファイル
            { "save",      "保存",              Cat::File, vk::S, kCmd, false, Icon::None, "SAVE" },
            { "bounce",    "バウンス",          Cat::File, vk::B, kCmd, false, Icon::None, "BNC" },
            // Reference Monitor（単体アプリ）への操作
            { "rmAB",      "A/B 切替",          Cat::RefMon, -1, 0, false, Icon::None, "A/B",  "ab" },
            { "rmMix",     "自分の曲（A）",      Cat::RefMon, -1, 0, false, Icon::None, "A",    "mix" },
            { "rmRef",     "参照曲（B）",        Cat::RefMon, -1, 0, false, Icon::None, "B",    "ref" },
            { "rmPlay",    "再生 / 一時停止",    Cat::RefMon, -1, 0, false, Icon::Play, "",     "play" },
            { "rmLoop",    "ループ",            Cat::RefMon, -1, 0, false, Icon::None, "LOOP", "loop" },
            { "rmCue1",    "キュー 1",          Cat::RefMon, -1, 0, false, Icon::None, "Q1",   "cue1" },
            { "rmCue2",    "キュー 2",          Cat::RefMon, -1, 0, false, Icon::None, "Q2",   "cue2" },
            { "rmCue3",    "キュー 3",          Cat::RefMon, -1, 0, false, Icon::None, "Q3",   "cue3" },
            { "rmCue4",    "キュー 4",          Cat::RefMon, -1, 0, false, Icon::None, "Q4",   "cue4" },
            { "rmCue5",    "キュー 5",          Cat::RefMon, -1, 0, false, Icon::None, "Q5",   "cue5" },
            { "rmAddCue",  "キューを追加",       Cat::RefMon, -1, 0, false, Icon::None, "+Q",   "addCue" },
            { "rmSetMix",  "SET MIX",           Cat::RefMon, -1, 0, false, Icon::None, "SET",  "setMix" },
            { "rmMono",    "モノラル",          Cat::RefMon, -1, 0, false, Icon::None, "MONO", "mono" },
            { "rmDim",     "DIM",               Cat::RefMon, -1, 0, false, Icon::None, "DIM",  "dim" },
            { "rmMute",    "ミュート",          Cat::RefMon, -1, 0, false, Icon::None, "MUTE", "mute" },
            { "rmListen",  "聞こえ方を切替",     Cat::RefMon, -1, 0, false, Icon::None, "EAR",  "listen" },
            { "rmListenOff","聞こえ方 OFF",     Cat::RefMon, -1, 0, false, Icon::None, "FLAT", "listenOff" },
            { "rmReset",   "メーターをリセット", Cat::RefMon, -1, 0, false, Icon::None, "RST",  "reset" },
            { "rmOut",     "出力 A/B 切替",     Cat::RefMon, -1, 0, false, Icon::None, "OUT",  "out" },
            { "rmOutA",    "出力 A",            Cat::RefMon, -1, 0, false, Icon::None, "OUT A","outA" },
            { "rmOutB",    "出力 B",            Cat::RefMon, -1, 0, false, Icon::None, "OUT B","outB" },
            // どのアプリでも
            { "copy",      "コピー",            Cat::General, vk::C, kCmd,          false, Icon::None, "COPY" },
            { "paste",     "貼り付け",          Cat::General, vk::V, kCmd,          false, Icon::None, "PASTE" },
            { "cut",       "切り取り",          Cat::General, vk::X, kCmd,          false, Icon::None, "CUT" },
            { "pastePlain","書式なしで貼り付け", Cat::General, vk::V, kCmd | kOpt | kShift, false, Icon::None, "PLAIN" },
            { "selectAll", "すべて選択",        Cat::General, vk::A, kCmd,          false, Icon::None, "ALL" },
            { "find",      "検索",              Cat::General, vk::F, kCmd,          false, Icon::None, "FIND" },
            { "screenshot","範囲スクリーンショット", Cat::General, vk::Four, kCmd | kShift, false, Icon::None, "SHOT" },
            // ブラウザ（Chrome など）
            { "newTab",    "新しいタブ",        Cat::Browser, vk::T, kCmd,          false, Icon::None, "+TAB" },
            { "closeTab",  "タブを閉じる",      Cat::Browser, vk::W, kCmd,          false, Icon::None, "xTAB" },
            { "reopenTab", "閉じたタブを開く",  Cat::Browser, vk::T, kCmd | kShift, false, Icon::None, "REOPEN" },
            { "nextTab",   "次のタブ",          Cat::Browser, vk::Tab, kCtrl,       true,  Icon::None, "TAB>" },
            { "prevTab",   "前のタブ",          Cat::Browser, vk::Tab, kCtrl | kShift, true, Icon::None, "<TAB" },
            { "reload",    "再読み込み",        Cat::Browser, vk::R, kCmd,          false, Icon::None, "RELOAD" },
            { "back",      "戻る",              Cat::Browser, vk::LBracket, kCmd,   true,  Icon::None, "BACK" },
            { "fwd",       "進む",              Cat::Browser, vk::RBracket, kCmd,   true,  Icon::None, "FWD" },
            { "address",   "アドレスバー",      Cat::Browser, vk::L, kCmd,          false, Icon::None, "URL" },
        };
        return list;
    }

    inline const ActionDef* findAction (const juce::String& id)
    {
        for (auto& a : catalog())
            if (id == a.id) return &a;
        return nullptr;
    }

    inline juce::String jp (const char* s) { return juce::String::fromUTF8 (s); }

    inline juce::String catName (Cat c)
    {
        switch (c)
        {
            case Cat::Transport: return jp ("再生・録音");
            case Cat::Edit:      return jp ("編集");
            case Cat::Track:     return jp ("トラック");
            case Cat::Window:    return jp ("ウィンドウ");
            case Cat::File:      return jp ("ファイル");
            case Cat::RefMon:    return jp ("Reference Monitor");
            case Cat::General:   return jp ("どのアプリでも");
            case Cat::Browser:   return jp ("ブラウザ");
            case Cat::Custom:    return jp ("自分で決めたキー");
            case Cat::App:       return jp ("アプリを開く");
            case Cat::Link:      return jp ("ページを開く");
        }
        return {};
    }

    inline juce::Colour catColour (Cat c)
    {
        switch (c)
        {
            case Cat::Transport: return juce::Colour (0xff3aa6ff);
            case Cat::Edit:      return juce::Colour (0xffff9a3c);
            case Cat::Track:     return juce::Colour (0xff5fe08f);
            case Cat::Window:    return juce::Colour (0xffb58cff);
            case Cat::File:      return juce::Colour (0xffffd34d);
            case Cat::RefMon:    return juce::Colour (0xffff7f5c);
            case Cat::General:   return juce::Colour (0xff4fd6c8);
            case Cat::Browser:   return juce::Colour (0xff6f9dff);
            case Cat::Custom:    return juce::Colour (0xffff6fae);
            case Cat::App:       return juce::Colour (0xffe8eef5);
            case Cat::Link:      return juce::Colour (0xff8fd14f);
        }
        return juce::Colours::grey;
    }

    // キーコード → 表示名
    inline juce::String keyName (int code)
    {
        static const std::map<int, const char*> names = {
            { 0, "A" }, { 11, "B" }, { 8, "C" }, { 2, "D" }, { 14, "E" }, { 3, "F" }, { 5, "G" }, { 4, "H" },
            { 34, "I" }, { 38, "J" }, { 40, "K" }, { 37, "L" }, { 46, "M" }, { 45, "N" }, { 31, "O" }, { 35, "P" },
            { 12, "Q" }, { 15, "R" }, { 1, "S" }, { 17, "T" }, { 32, "U" }, { 9, "V" }, { 13, "W" }, { 7, "X" },
            { 16, "Y" }, { 6, "Z" },
            { 18, "1" }, { 19, "2" }, { 20, "3" }, { 21, "4" }, { 23, "5" }, { 22, "6" }, { 26, "7" }, { 28, "8" },
            { 25, "9" }, { 29, "0" }, { 27, "-" }, { 24, "=" }, { 33, "[" }, { 30, "]" }, { 42, "\\" }, { 41, ";" },
            { 39, "'" }, { 43, "," }, { 47, "." }, { 44, "/" }, { 50, "`" }, { 93, "\xc2\xa5" }, { 94, "_" },
            { 49, "Space" }, { 36, "Return" }, { 48, "Tab" }, { 51, "Delete" }, { 117, "Fwd Del" }, { 53, "Esc" },
            { 123, "\xe2\x86\x90" }, { 124, "\xe2\x86\x92" }, { 125, "\xe2\x86\x93" }, { 126, "\xe2\x86\x91" },
            { 115, "Home" }, { 119, "End" }, { 116, "PgUp" }, { 121, "PgDn" },
            { 122, "F1" }, { 120, "F2" }, { 99, "F3" }, { 118, "F4" }, { 96, "F5" }, { 97, "F6" }, { 98, "F7" },
            { 100, "F8" }, { 101, "F9" }, { 109, "F10" }, { 103, "F11" }, { 111, "F12" },
        };
        auto it = names.find (code);
        return it != names.end() ? jp (it->second) : "#" + juce::String (code);
    }

    inline juce::String shortcutText (int code, int mods)
    {
        juce::String s;
        if (mods & kCtrl)  s += jp ("\xe2\x8c\x83");  // ⌃
        if (mods & kOpt)   s += jp ("\xe2\x8c\xa5");  // ⌥
        if (mods & kShift) s += jp ("\xe2\x87\xa7");  // ⇧
        if (mods & kCmd)   s += jp ("\xe2\x8c\x98");  // ⌘
        return s + keyName (code);
    }
}
