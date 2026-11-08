#pragma once
// SCORE ページ: タブ譜を見ながら 1 音ずつ奏法・弦を付ける。
//   FOLLOW LOGIC … Logic の MIDI を鳴らし、再生のたびに譜面を更新。付けた奏法は次の再生からすぐ効く
//   PLAY IN SYNC … プラグインの譜面を Logic の再生位置に合わせて鳴らす（Ample Guitar 型。MIDI を開く／ドロップ）
// どちらも「DRAG TO LOGIC」で奏法入りの MIDI を Logic に戻せる。
#include "PluginProcessor.h"
#include <set>

namespace ScoreUI
{
    const juce::Colour bg { 0xff15161a }, panel { 0xff22242b }, panelHi { 0xff2c2f38 }, text { 0xffe8e6e1 }, dim { 0xff8b8d96 },
                       accent { 0xffe8a33d }, plugin { 0xffff7a45 }, selCol { 0xff5b8def }, play { 0xff4cc46a };
    inline juce::String jp (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

    inline const char* glyph (int art)
    {
        static const char* g[] { "", "P.M.", "1/2", "full", "1 1/2", "B&R", "PB&R", "~", "~~", "/", "\\", "x" };
        return juce::isPositiveAndBelow (art, (int) numArticulations) ? g[art] : "";
    }
}

//==============================================================================
class TabView : public juce::Component, private juce::ScrollBar::Listener
{
public:
    explicit TabView (AIOGuitarProcessor& p) : proc (p)
    {
        addAndMakeVisible (scroll);
        scroll.setAutoHide (false);
        scroll.addListener (this);
        setWantsKeyboardFocus (true);
    }

    std::set<Score::Key> selection;
    std::function<void()> onSelectionChanged;
    double pxPerBeat = 60.0, leftPpq = 0.0;
    bool follow = true;

    void refresh()
    {
        auto& sc = proc.getScore();
        double end = 16.0;
        for (auto& n : sc.notes) end = juce::jmax (end, (double) (n.tick + n.length) / Score::kTicks + 4.0);
        totalBeats = end;
        updateScroll();
        // forget selected notes that are gone
        for (auto it = selection.begin(); it != selection.end();)
            if (sc.findNote (it->first, it->second) == nullptr) it = selection.erase (it); else ++it;
        repaint();
    }

    void setPlayhead (double ppq, bool moving)
    {
        if (std::abs (ppq - playhead) < 1.0e-4 && moving == playheadMoving) return;
        playhead = ppq; playheadMoving = moving;
        if (follow && moving)
        {
            const double visible = notesArea().getWidth() / pxPerBeat;
            if (ppq < leftPpq || ppq > leftPpq + visible * 0.9) { leftPpq = juce::jmax (0.0, ppq - visible * 0.1); updateScroll(); }
        }
        repaint();
    }

    void setZoom (double px) { pxPerBeat = juce::jlimit (12.0, 400.0, px); updateScroll(); repaint(); }

    void resized() override
    {
        scroll.setBounds (getLocalBounds().removeFromBottom (12));
        updateScroll();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ScoreUI;
        auto& sc = proc.getScore();
        g.fillAll (bg);
        auto area = notesArea();
        const float laneH = stringLaneH();
        const double beatsPerBar = sc.numerator * 4.0 / juce::jmax (1, sc.denominator);

        // lane labels
        g.setFont (juce::FontOptions (11.0f));
        g.setColour (dim);
        g.drawText ("BRUSH", juce::Rectangle<float> (0, (float) brushLane().getY(), labelW - 4.0f, (float) brushLane().getHeight()), juce::Justification::centredRight);
        for (int s = 1; s <= 6; ++s)
            g.drawText (juce::String (s) + " " + juce::MidiMessage::getMidiNoteName (kOpenString[s], false, false, 3),
                        juce::Rectangle<float> (0, stringY (s) - 8.0f, labelW - 4.0f, 16.0f), juce::Justification::centredRight);

        g.saveState();
        g.reduceClipRegion (area.withTop (0));

        // bars and beats
        const double firstBeat = std::floor (leftPpq);
        for (double b = firstBeat; b < leftPpq + area.getWidth() / pxPerBeat + 1.0; b += 1.0)
        {
            const float x = xOf (b);
            const bool bar = std::fmod (b, beatsPerBar) < 1.0e-6;
            g.setColour (bar ? panelHi.brighter (0.25f) : panelHi);
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
            if (bar)
            {
                g.setColour (dim);
                g.drawText (juce::String ((int) (b / beatsPerBar) + 1), juce::Rectangle<float> (x + 3.0f, 0.0f, 40.0f, (float) rulerH), juce::Justification::centredLeft);
            }
        }
        // strings
        for (int s = 1; s <= 6; ++s)
        {
            g.setColour (dim.withAlpha (0.45f));
            g.fillRect ((float) area.getX(), stringY (s) - 0.5f * (0.6f + 0.25f * s), (float) area.getWidth(), 0.6f + 0.25f * s);
        }

        // notes
        for (auto& n : sc.notes)
        {
            const double a = (double) n.tick / Score::kTicks, e = (double) (n.tick + n.length) / Score::kTicks;
            if (e < leftPpq || a > leftPpq + area.getWidth() / pxPerBeat) continue;
            const auto o = sc.overrideFor (n);
            const int art = sc.effectiveArt (n);
            int s = sc.effectiveString (n);
            if (dragging && selection.count ({ n.tick, n.note }) && dragTarget >= 1) s = dragTarget;
            const bool brush = n.note == Kit::kBrushDownNote || n.note == Kit::kBrushUpNote;
            const bool sel = selection.count ({ n.tick, n.note }) > 0;
            const float x0 = xOf (a), x1 = juce::jmax (x0 + 14.0f, xOf (e));
            juce::Rectangle<float> box;
            juce::String label;
            if (brush || s < 1)
            {
                box = { x0, (float) brushLane().getY() + 4.0f, juce::jmax (14.0f, x1 - x0), (float) brushLane().getHeight() - 8.0f };
                label = brush ? juce::String (n.note == Kit::kBrushDownNote ? "v" : "^") : "?" + juce::MidiMessage::getMidiNoteName (n.note, true, true, 3);
            }
            else
            {
                const float h = juce::jmin (18.0f, laneH * 0.7f);
                box = { x0, stringY (s) - h * 0.5f, x1 - x0, h };
                label = juce::String (StringAllocator::fretOn (s, n.note));
            }
            // length bar + fret number box
            g.setColour ((sel ? selCol : accent).withAlpha (0.28f));
            g.fillRoundedRectangle (box, 3.0f);
            auto head = box.withWidth (juce::jmax (16.0f, juce::jmin (box.getWidth(), 22.0f)));
            g.setColour (sel ? selCol : (o.isEmpty() ? panel.brighter (0.15f) : plugin.withAlpha (0.35f)));
            g.fillRoundedRectangle (head, 3.0f);
            g.setColour (o.isEmpty() ? accent.withAlpha (0.7f) : plugin);
            g.drawRoundedRectangle (box, 3.0f, o.isEmpty() ? 1.0f : 1.6f);
            g.setColour (text);
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (label, head, juce::Justification::centred);
            if (art != artNormal)
            {
                g.setColour (o.art >= 0 ? plugin : accent);
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText (glyph (art), juce::Rectangle<float> (x0, box.getY() - 13.0f, 60.0f, 12.0f), juce::Justification::centredLeft);
            }
        }

        // rubber band
        if (banding)
        {
            g.setColour (selCol.withAlpha (0.15f));
            g.fillRect (band);
            g.setColour (selCol);
            g.drawRect (band, 1.0f);
        }
        // playhead
        if (playhead >= leftPpq)
        {
            g.setColour (playheadMoving ? play : dim);
            g.fillRect (xOf (playhead) - 1.0f, 0.0f, 2.0f, (float) area.getBottom());
        }
        g.restoreState();

        if (sc.notes.empty())
        {
            g.setColour (dim);
            g.setFont (juce::FontOptions (14.0f));
            const bool sync = proc.apvts.getRawParameterValue ("scoreMode")->load() > 0.5f;
            g.drawFittedText (sync ? jp (u8"「Open MIDI...」か、.mid ファイルをここにドロップ。Logic を再生すると、この譜面を Logic に合わせて鳴らします")
                                   : jp (u8"Logic を再生すると、流れてきた MIDI がここにタブ譜で出ます。音をクリックして奏法や弦を付けると、次の再生からすぐ効きます"),
                              area.reduced (40, 0), juce::Justification::centred, 3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto* hit = noteAt (e.position);
        if (hit != nullptr)
        {
            const Score::Key k { hit->tick, hit->note };
            if (e.mods.isShiftDown() || e.mods.isCommandDown())
            {
                if (selection.count (k)) selection.erase (k); else selection.insert (k);
            }
            else if (! selection.count (k)) { selection.clear(); selection.insert (k); }
            dragging = true; dragStartY = e.position.y; dragTarget = -1;
            if (e.getNumberOfClicks() >= 2)
            {
                const int s = proc.getScore().effectiveString (*hit);
                proc.previewNote (hit->note, s, true);
                auditioning = hit->note; auditionString = s;
            }
        }
        else
        {
            if (! e.mods.isShiftDown()) selection.clear();
            banding = true; bandStart = e.position; band = {};
        }
        if (onSelectionChanged) onSelectionChanged();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (banding)
        {
            band = juce::Rectangle<float> (bandStart, e.position);
            repaint();
        }
        else if (dragging && std::abs (e.position.y - dragStartY) > 6.0f)
        {
            dragTarget = stringAtY (e.position.y);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (auditioning >= 0) { proc.previewNote (auditioning, auditionString, false); auditioning = -1; }
        auto& sc = proc.getScore();
        if (banding)
        {
            for (auto& n : sc.notes)
            {
                const int s = sc.effectiveString (n);
                const bool brush = n.note == Kit::kBrushDownNote || n.note == Kit::kBrushUpNote;
                const float y = brush || s < 1 ? (float) brushLane().getCentreY() : stringY (s);
                if (band.contains (xOf ((double) n.tick / Score::kTicks) + 2.0f, y)) selection.insert ({ n.tick, n.note });
            }
            banding = false;
            if (onSelectionChanged) onSelectionChanged();
        }
        else if (dragging && dragTarget >= 1)
        {
            proc.pushUndo();
            for (auto& k : selection)
                if (auto* n = sc.findNote (k.first, k.second))
                {
                    const int f = StringAllocator::fretOn (dragTarget, n->note);
                    if (f < 0 || f > 24) continue;   // the note does not exist on that string
                    auto o = sc.overrideFor (*n);
                    o.string = dragTarget;
                    sc.setOverride (*n, o);
                }
            proc.scoreEdited();
            if (onSelectionChanged) onSelectionChanged();
        }
        dragging = false; dragTarget = -1;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown()) { setZoom (pxPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15)); return; }
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        leftPpq = juce::jlimit (0.0, juce::jmax (0.0, totalBeats - 4.0), leftPpq - d * 400.0 / pxPerBeat);
        follow = false;
        updateScroll();
        repaint();
    }

private:
    static constexpr int rulerH = 20, brushH = 30;
    static constexpr float labelW = 44.0f;

    juce::Rectangle<int> notesArea() const { return getLocalBounds().withTrimmedBottom (14).withTrimmedLeft ((int) labelW).withTrimmedTop (rulerH); }
    juce::Rectangle<int> brushLane() const { auto a = notesArea(); return a.withHeight (brushH); }
    float stringLaneH() const { auto a = notesArea(); return (float) (a.getHeight() - brushH) / 6.0f; }
    float stringY (int s) const { auto a = notesArea(); return (float) a.getY() + brushH + stringLaneH() * ((float) s - 0.5f); }
    int stringAtY (float y) const { return juce::jlimit (1, 6, 1 + (int) ((y - (float) notesArea().getY() - brushH) / stringLaneH())); }
    float xOf (double ppq) const { return (float) notesArea().getX() + (float) ((ppq - leftPpq) * pxPerBeat); }

    const Score::Note* noteAt (juce::Point<float> p) const
    {
        auto& sc = proc.getScore();
        const float laneH = stringLaneH();
        for (auto& n : sc.notes)
        {
            const int s = sc.effectiveString (n);
            const bool brush = n.note == Kit::kBrushDownNote || n.note == Kit::kBrushUpNote;
            const float y = brush || s < 1 ? (float) brushLane().getCentreY() : stringY (s);
            const float x0 = xOf ((double) n.tick / Score::kTicks), x1 = juce::jmax (x0 + 14.0f, xOf ((double) (n.tick + n.length) / Score::kTicks));
            if (p.x >= x0 && p.x <= x1 && std::abs (p.y - y) < juce::jmin (12.0f, laneH * 0.45f)) return &n;
        }
        return nullptr;
    }

    void updateScroll()
    {
        const double visible = juce::jmax (1.0, notesArea().getWidth() / pxPerBeat);
        scroll.setRangeLimits (0.0, juce::jmax (totalBeats, leftPpq + visible));
        scroll.setCurrentRange (leftPpq, visible, juce::dontSendNotification);
    }

    void scrollBarMoved (juce::ScrollBar*, double start) override { leftPpq = start; follow = false; repaint(); }

    AIOGuitarProcessor& proc;
    juce::ScrollBar scroll { false };
    double totalBeats = 16.0, playhead = 0.0;
    bool playheadMoving = false, dragging = false, banding = false;
    float dragStartY = 0.0f;
    int dragTarget = -1, auditioning = -1, auditionString = 0;
    juce::Point<float> bandStart;
    juce::Rectangle<float> band;
};

//==============================================================================
// The "drag me into Logic" tile
class DragToLogic : public juce::Component
{
public:
    explicit DragToLogic (AIOGuitarProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        using namespace ScoreUI;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (accent.withAlpha (0.18f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (accent);
        g.drawRoundedRectangle (r, 6.0f, 1.4f);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("DRAG TO LOGIC", r.removeFromTop (r.getHeight() * 0.55f), juce::Justification::centredBottom);
        g.setColour (dim);
        g.setFont (juce::FontOptions (11.0f));
        int bar = 1;
        auto& sc = proc.getScore();
        if (! sc.notes.empty())
        {
            const juce::int64 barTicks = (juce::int64) Score::kTicks * sc.numerator * 4 / juce::jmax (1, sc.denominator);
            bar = (int) (sc.notes.front().tick / barTicks) + 1;
        }
        g.drawText (jp (u8"奏法入り MIDI ・ ") + juce::String (bar) + jp (u8" 小節目にドロップ"), r, juce::Justification::centredTop);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < 6 || proc.getScore().notes.empty()) return;
        started = true;
        int firstBar = 1;
        const int stringMode = (int) proc.apvts.getRawParameterValue ("stringMode")->load();
        auto mf = proc.getScore().exportMidi (stringMode, firstBar);
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("AIO Guitar").getChildFile ("Exports");
        dir.createDirectory();
        auto f = dir.getChildFile ("AIO Guitar bar " + juce::String (firstBar) + ".mid");
        f.deleteFile();
        {
            juce::FileOutputStream out (f);
            if (! out.openedOk()) return;
            mf.writeTo (out);
        }
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
    }
    void mouseUp (const juce::MouseEvent&) override { started = false; }

private:
    using juce::Component::paint;
    static juce::String jp (const char* s) { return ScoreUI::jp (s); }
    AIOGuitarProcessor& proc;
    bool started = false;
};

//==============================================================================
class ScorePage : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ScorePage (AIOGuitarProcessor& p) : proc (p), tab (p), dragTile (p)
    {
        using namespace ScoreUI;
        mode.addItemList ({ "FOLLOW LOGIC", "PLAY IN SYNC" }, 1);
        modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "scoreMode", mode);
        mode.setTooltip (jp (u8"FOLLOW LOGIC = Logic の MIDI を鳴らし、譜面は再生のたびに更新 / PLAY IN SYNC = この譜面を Logic の再生に合わせて鳴らす（Logic 側に MIDI はいらない）"));
        chToggle.setColour (juce::ToggleButton::tickColourId, accent);
        chAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "stringMode", chToggle);
        chToggle.setTooltip (jp (u8"弦を MIDI チャンネル 1〜6 で書き出し・受け取る（String choice が「MIDI Ch 1-6 = String」になる）。Logic に戻した後も弦の指定が残る"));

        playButton.onClick = [this]
        {
            if (proc.uiPreview.load()) proc.stopPreview();
            else
            {
                auto& sc = proc.getScore();
                double from = 0.0;
                if (! tab.selection.empty()) from = (double) tab.selection.begin()->first / Score::kTicks;
                else if (! sc.notes.empty()) from = juce::jmax ((double) sc.notes.front().tick / Score::kTicks, tab.leftPpq);
                proc.startPreview (from);
                tab.follow = true;
            }
        };
        playButton.setTooltip (jp (u8"プラグインの中だけで試聴（選んだ音、または表示している所から）"));
        openButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Open MIDI", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.mid;*.midi");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) importFile (fc.getResult()); });
        };
        followToggle.setToggleState (true, juce::dontSendNotification);
        followToggle.setColour (juce::ToggleButton::tickColourId, play);
        followToggle.onClick = [this] { tab.follow = followToggle.getToggleState(); };
        zoom.setSliderStyle (juce::Slider::LinearHorizontal);
        zoom.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        zoom.setRange (12.0, 400.0);
        zoom.setSkewFactorFromMidPoint (60.0);
        zoom.setValue (60.0, juce::dontSendNotification);
        zoom.onValueChange = [this] { tab.setZoom (zoom.getValue()); };
        undoButton.onClick = [this] { if (proc.undo()) tab.refresh(); };
        redoButton.onClick = [this] { if (proc.redo()) tab.refresh(); };
        orphanButton.onClick = [this] { proc.pushUndo(); proc.getScore().removeOrphans(); proc.scoreEdited(); };
        orphanButton.setTooltip (jp (u8"Logic で音を動かした・消したために、当てる音がなくなった奏法の指定を消す"));

        for (int a = 0; a < numArticulations; ++a)
        {
            auto* b = artButtons.add (new juce::TextButton (articulationName (a)));
            b->setTooltip (jp (u8"選んだ音をこの奏法にする（キースイッチ ") + juce::MidiMessage::getMidiNoteName (kKeyswitchLow + a, true, true, 3) + ")");
            b->onClick = [this, a] { applyArt (a); };
            b->setColour (juce::TextButton::buttonColourId, panel);
            addAndMakeVisible (b);
        }
        resetButton.setTooltip (jp (u8"選んだ音の、プラグインでの指定（奏法・弦）を消して Logic のままに戻す"));
        resetButton.onClick = [this] { applyReset(); };

        for (juce::Component* c : std::initializer_list<juce::Component*> { &mode, &chToggle, &playButton, &openButton, &followToggle, &zoom, &undoButton, &redoButton,
                                                                            &orphanButton, &dragTile, &resetButton, &tab })
            addAndMakeVisible (*c);
        tab.onSelectionChanged = [this] { updateInfo(); };
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { undoButton.triggerClick(); return true; }
        if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { redoButton.triggerClick(); return true; }
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
        {
            for (auto& n : proc.getScore().notes) tab.selection.insert ({ n.tick, n.note });
            updateInfo(); tab.repaint(); return true;
        }
        if (k == juce::KeyPress::backspaceKey || k == juce::KeyPress::deleteKey) { applyReset(); return true; }
        return false;
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (juce::File (f).hasFileExtension ("mid;midi")) return true;
        return false;
    }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (auto& f : files) if (juce::File (f).hasFileExtension ("mid;midi")) { importFile (juce::File (f)); break; }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace ScoreUI;
        auto r = getLocalBounds().toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r.withHeight (86.0f), 10.0f);
        g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() - 30.0f), 8.0f);
        g.setColour (text);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (info, getLocalBounds().removeFromBottom (30).reduced (12, 0), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (86).reduced (10, 8);
        auto row1 = top.removeFromTop (30);
        mode.setBounds (row1.removeFromLeft (150)); row1.removeFromLeft (8);
        chToggle.setBounds (row1.removeFromLeft (120)); row1.removeFromLeft (8);
        playButton.setBounds (row1.removeFromLeft (90)); row1.removeFromLeft (6);
        openButton.setBounds (row1.removeFromLeft (110)); row1.removeFromLeft (12);
        followToggle.setBounds (row1.removeFromLeft (90)); row1.removeFromLeft (6);
        zoom.setBounds (row1.removeFromLeft (120)); row1.removeFromLeft (12);
        undoButton.setBounds (row1.removeFromLeft (60)); row1.removeFromLeft (4);
        redoButton.setBounds (row1.removeFromLeft (60)); row1.removeFromLeft (12);
        dragTile.setBounds (row1.removeFromRight (190).withTrimmedTop (-4).withHeight (38));
        row1.removeFromRight (8);
        orphanButton.setBounds (row1);
        top.removeFromTop (8);
        const int n = numArticulations + 1, w = top.getWidth() / n;
        for (int a = 0; a < numArticulations; ++a) artButtons[a]->setBounds (top.removeFromLeft (w).reduced (2, 0));
        resetButton.setBounds (top.reduced (2, 0));
        r.removeFromBottom (30);
        r.removeFromTop (6); r.removeFromBottom (6);
        tab.setBounds (r);
    }

private:
    void timerCallback() override
    {
        if (shownVersion != proc.getScoreVersion())
        {
            shownVersion = proc.getScoreVersion();
            tab.refresh();
            updateInfo();
            dragTile.repaint();
        }
        const bool moving = proc.uiPreview.load() || proc.uiHostPlaying.load();
        tab.setPlayhead (proc.uiPpq.load(), moving);
        playButton.setButtonText (proc.uiPreview.load() ? "STOP" : "PREVIEW");
        undoButton.setEnabled (proc.canUndo());
        redoButton.setEnabled (proc.canRedo());
        if (followToggle.getToggleState() != tab.follow) followToggle.setToggleState (tab.follow, juce::dontSendNotification);
    }

    void importFile (const juce::File& f)
    {
        proc.pushUndo();
        auto& sc = proc.getScore();
        sc.numerator = proc.hostNum.load(); sc.denominator = proc.hostDen.load();
        if (sc.importMidi (f, 0))
        {
            // the plugin owns this score now: play it in sync with Logic
            if (auto* p = proc.apvts.getParameter ("scoreMode")) p->setValueNotifyingHost (1.0f);
            // strings chosen like the player would
            StringAllocator alloc; alloc.reset();
            for (auto& n : sc.notes) { n.autoString = alloc.choose (n.channel, n.note, (int) proc.apvts.getRawParameterValue ("stringMode")->load(), n.tick * 48); alloc.picked (n.autoString, n.note, n.tick * 48); }
            tab.selection.clear();
            tab.leftPpq = sc.notes.empty() ? 0.0 : (double) sc.notes.front().tick / Score::kTicks;
        }
        proc.scoreEdited();
    }

    void applyArt (int art)
    {
        auto& sc = proc.getScore();
        if (tab.selection.empty()) { info = ScoreUI::jp (u8"先に音をクリックして選んでください（Shift で複数、ドラッグで範囲）"); repaint(); return; }
        proc.pushUndo();
        for (auto& k : tab.selection)
            if (auto* n = sc.findNote (k.first, k.second))
            {
                auto o = sc.overrideFor (*n);
                o.art = art;
                sc.setOverride (*n, o);
            }
        proc.scoreEdited();
    }

    void applyReset()
    {
        auto& sc = proc.getScore();
        if (tab.selection.empty()) return;
        proc.pushUndo();
        for (auto& k : tab.selection)
            if (auto* n = sc.findNote (k.first, k.second)) sc.setOverride (*n, {});
        proc.scoreEdited();
    }

    void updateInfo()
    {
        using ScoreUI::jp;
        auto& sc = proc.getScore();
        const int pending = sc.pendingCount(), orphans = sc.orphaned;
        orphanButton.setButtonText (orphans > 0 ? jp (u8"外れた奏法 ") + juce::String (orphans) + jp (u8" 個を消す") : jp (u8"外れた奏法なし"));
        orphanButton.setEnabled (orphans > 0);
        if (tab.selection.size() == 1)
        {
            if (auto* n = sc.findNote (tab.selection.begin()->first, tab.selection.begin()->second))
            {
                const int s = sc.effectiveString (*n), art = sc.effectiveArt (*n);
                const auto o = sc.overrideFor (*n);
                info = (s >= 1 ? juce::String (s) + jp (u8"弦 ") + juce::String (StringAllocator::fretOn (s, n->note)) + "F  " : juce::String())
                     + juce::MidiMessage::getMidiNoteName (n->note, true, true, 3) + "  vel " + juce::String (n->velocity)
                     + jp (u8"   奏法: ") + articulationName (art)
                     + (o.isEmpty() ? jp (u8"（Logic のとおり）") : jp (u8"（プラグインで指定。Logic にはまだ入っていない → DRAG TO LOGIC で戻すと確定）"));
            }
        }
        else if (! tab.selection.empty())
            info = juce::String ((int) tab.selection.size()) + jp (u8" 音を選択中。上のボタンで奏法、上下にドラッグで弦を変更、Delete で Logic のままに戻す");
        else
            info = juce::String ((int) sc.notes.size()) + jp (u8" 音") + "   "
                 + (pending > 0 ? jp (u8"Logic にまだ入っていない指定 ") + juce::String (pending) + jp (u8" 個（オレンジ）") : jp (u8"プラグインでの指定なし"))
                 + jp (u8"   ｜ クリックで選択、ダブルクリックで試聴、⌘+ホイールで拡大");
        repaint();
    }

    AIOGuitarProcessor& proc;
    TabView tab;
    DragToLogic dragTile;
    juce::ComboBox mode;
    juce::ToggleButton chToggle { "CH = STRING" }, followToggle { "Follow" };
    juce::TextButton playButton { "PREVIEW" }, openButton { "Open MIDI..." }, undoButton { "Undo" }, redoButton { "Redo" },
                     orphanButton { "-" }, resetButton { "Logic" };
    juce::OwnedArray<juce::TextButton> artButtons;
    juce::Slider zoom;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String info;
    int shownVersion = -1;
};
