#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class VoxLook : public juce::LookAndFeel_V4
{
public:
    VoxLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
};

// SOURCE ページの波形: 元の声（灰色）と作り直した声（オレンジ）、切れ目
class WaveEditor : public juce::Component
{
public:
    explicit WaveEditor (VoxChopProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void resized() override { refresh(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh();
    std::function<void()> onEmptyClick;
    std::function<void (int sliceIndex)> onDragOut;   // -1 = 全体

private:
    struct Peaks { std::vector<float> lo, hi; };
    static Peaks makePeaks (const vox::Buf*, int width);
    juce::Rectangle<float> lane() const { return getLocalBounds().toFloat().reduced (8.0f, 0.0f).withTrimmedTop (22.0f).withTrimmedBottom (6.0f); }
    double xToSample (float x) const;
    float sampleToX (double s) const;
    int sliceAt (float x) const;
    bool edgeAt (float x, int& slice, bool& isEnd) const;
    bool whole() const;

    VoxChopProcessor& proc;
    Peaks srcPeaks, outPeaks;
    int shownAudio = -1, shownWidth = -1;
    long length = 0;
    double fs = 48000.0;
    int dragSlice = -1, pressSlice = -1;
    bool dragEnd = false, pressed = false, draggedOut = false;
};

// VOICE MAP: 横 = 声の高さ（Hz）、縦 = 体の大きさ（響きの山）
class VoiceMap : public juce::Component
{
public:
    explicit VoiceMap (VoxChopProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (34.0f, 26.0f).withTrimmedLeft (8.0f); }
    float hzToX (float hz) const;
    float xToHz (float x) const;
    float bodyToY (float b) const;
    float yToBody (float y) const;
    float srcF0() const;
    void setPoint (float hz, float bodyAbs);
    VoxChopProcessor& proc;
    bool dragging = false;
};

// KEYS ページ: 8×8 のパッド（C1 から）
class PadGrid : public juce::Component
{
public:
    explicit PadGrid (VoxChopProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onPick;
private:
    juce::Rectangle<float> cell (int i) const;
    VoxChopProcessor& proc;
};

// ドラッグで Logic へ出すタイル
class ExportTile : public juce::Component
{
public:
    std::function<void()> onDragOut, onClick;
    juce::String title, sub;
    bool enabled = false;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mouseWasDraggedSinceMouseDown() && onClick) onClick(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (enabled && ! dragging && e.getDistanceFromDragStart() > 6 && onDragOut) { dragging = true; onDragOut(); }
    }
private:
    bool dragging = false;
};

class VoxChopEditor : public juce::AudioProcessorEditor,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    explicit VoxChopEditor (VoxChopProcessor&);
    ~VoxChopEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHot = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropHot = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Box
    {
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    void setupKnob (Knob&, const char* id, const char* name, juce::Colour, const juce::String& tip, bool previewOnRelease);
    void setupBox (Box&, const char* id, const juce::String& tip);
    void styleButton (juce::Button&, juce::Colour textColour);
    void timerCallback() override;
    void showPage (int);
    void openChooser();
    void dragFiles (const juce::StringArray& paths, juce::Component& from, const juce::String& what);
    void dragOutSlice (int index);
    void updateTiles();
    void updateSliceControls();
    void editSelected (std::function<void (VoxSlice&)>);
    void layoutFooter();

    VoxChopProcessor& processor;
    VoxLook look;
    int page = 0;
    juce::TextButton tabs[3];

    // SOURCE
    WaveEditor wave { processor };
    juce::TextButton openButton { "OPEN" }, captureButton { "CAPTURE" }, rechopButton { "RE-CHOP" };
    Box modeBox, chopByBox, gridBox;
    Knob sens;
    ExportTile tileSlice, tileAll, tileMidi, tileWhole;

    // VOICE
    VoiceMap map { processor };
    Knob breath, grit, robot, alien, lowCut, highCut;
    juce::OwnedArray<juce::TextButton> presetTiles;
    juce::TextButton hearButton { "HEAR" };

    // KEYS
    PadGrid pads { processor };
    juce::Slider slicePitch, sliceGain;
    juce::Label slicePitchLabel, sliceGainLabel;
    juce::ToggleButton revToggle { "REVERSE" }, loopToggle { "LOOP" }, oneShotToggle { "ONE SHOT" }, chokeToggle { "CHOKE" };
    Box keyModeBox;
    juce::ToggleButton keepToggle { "FORMANT KEEP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> keepAtt;
    Knob attack, decay, sustain, release, velocity, level;

    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffff7a59), juce::Colour (0xff1d1b22), juce::Colour (0xffeeeaf2) };
    myp::PresetBar presetBar { processor.presets, juce::Colour (0xffff7a59), juce::Colour (0xff1d1b22), juce::Colour (0xffeeeaf2) };
    static constexpr int footerH = 40;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> content, gSlice, gGlobal, gEnv, gMacro, gPresets;
    int seenVersion = -1;
    bool dropHot = false;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxChopEditor)
};
