#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// つまみ（真ん中が 0 のものは、真ん中から左右へ色をつける）
class SculptLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float start, float end, juce::Slider&) override;
};

// 波形: 上 = 元のファイル（START / END をドラッグ）、下 = できた音（クリックで試し聴き）
class WaveView : public juce::Component
{
public:
    explicit WaveView (SampleSculptorProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void refresh();                    // 音が変わったら、描く形を作り直す
    void resized() override { refresh(); }
    std::function<void()> onEmptyClick;

private:
    struct Peaks { std::vector<float> lo, hi; };
    static Peaks makePeaks (const SampleAudio*, int width);
    juce::Rectangle<float> srcLane() const;
    juce::Rectangle<float> outLane() const;
    float xToFrac (float x) const;
    float fracToX (float f) const;

    SampleSculptorProcessor& proc;
    Peaks srcPeaks, outPeaks;
    double srcSec = 0, outSec = 0;
    const char* dragId = nullptr;
    bool hasSource = false;
};

// できた音（ドラッグで Logic へ）
class ExportTile : public juce::Component
{
public:
    std::function<void()> onDragOut;
    juce::String title, sub;
    bool enabled = false;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (enabled && ! dragging && e.getDistanceFromDragStart() > 6 && onDragOut) { dragging = true; onDragOut(); }
    }
private:
    bool dragging = false;
};

class SampleSculptorEditor : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer
{
public:
    explicit SampleSculptorEditor (SampleSculptorProcessor&);
    ~SampleSculptorEditor() override;

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
    void setupKnob (Knob&, const char* id, const char* name, bool bipolar, double def, juce::Colour);
    void timerCallback() override;
    void openChooser();

    SampleSculptorProcessor& processor;
    SculptLook look;
    WaveView wave { processor };
    Knob fadeIn, fadeOut, pitch, attack, snap, length, room, size, tone, drive, gain;
    juce::ComboBox reverseBox;
    juce::Label reverseLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverseAtt;
    juce::TextButton openButton { "OPEN" }, playButton { "PLAY" }, saveButton { "SAVE WAV" }, resetButton { "RESET" };
    juce::ToggleButton normalize { "NORMALIZE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> normalizeAtt;
    ExportTile exportTile;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> gEdit, gShape, gRoom, gOut;
    int seenVersion = -1;
    bool dropHot = false;
    juce::String status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSculptorEditor)
};
