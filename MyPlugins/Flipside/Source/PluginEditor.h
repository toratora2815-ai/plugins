#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FileReverse.h"

// 枠の並び（逆になる枠に ◀、今鳴っている枠に進み具合）
class SlotStrip : public juce::Component
{
public:
    explicit SlotStrip (FlipsideProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    FlipsideProcessor& proc;
};

// ファイルを落とす所 / 逆にしたファイル（ドラッグで Logic へ）
class FileTile : public juce::Component
{
public:
    std::function<void()> onClick;
    std::function<void()> onDragOut;
    juce::String title, sub;
    bool hot = false, isResult = false;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mouseWasDraggedSinceMouseDown() && onClick) onClick(); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (isResult && ! dragging && e.getDistanceFromDragStart() > 6 && onDragOut) { dragging = true; onDragOut(); }
    }
    void mouseDown (const juce::MouseEvent&) override { dragging = false; }
private:
    bool dragging = false;
};

class FlipsideEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit FlipsideEditor (FlipsideProcessor&);
    ~FlipsideEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dropZone.hot = true; dropZone.repaint(); }
    void fileDragExit (const juce::StringArray&) override { dropZone.hot = false; dropZone.repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    void setupKnob (Knob&, const char* id, const char* name, double def);
    void setupCombo (juce::ComboBox&, juce::Label&, const char* name);
    void timerCallback() override;
    void startReverse (const juce::File&);
    void showResult();

    FlipsideProcessor& processor;
    juce::ComboBox gridBox, patternBox;
    juce::Label gridLabel, patternLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> gridAtt, patternAtt;
    Knob fade, mix, output;
    SlotStrip strip { processor };
    juce::Label status;

    FileTile dropZone, resultTile;
    juce::ToggleButton trim { "TRIM SILENCE" };
    juce::TextButton finder { "SHOW IN FINDER" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::atomic<bool> working { false };
    juce::File lastOut;

    juce::Rectangle<int> livePanel, filePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlipsideEditor)
};
