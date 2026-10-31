#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// 選択肢パラメーター用の「横並びボタン」
class Segmented : public juce::Component
{
public:
    Segmented (juce::RangedAudioParameter& p, const juce::StringArray& labels,
               std::vector<juce::Colour> onColours, float fontSize = 13.0f);
    void resized() override;
    std::function<void (int)> onChange;

private:
    void setIndex (int idx);
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};

// 参照曲ファイルの波形とシーク
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (ReferenceProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { seekTo (e); }
    void mouseDrag (const juce::MouseEvent& e) override { seekTo (e); }

private:
    void seekTo (const juce::MouseEvent& e);
    ReferenceProcessor& proc;
};

class ReferenceEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit ReferenceEditor (ReferenceProcessor&);
    ~ReferenceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void chooseFile();
    void refreshDevices();
    void updateSourceVisibility();
    std::vector<float> toBands (const std::vector<double>& power, double sampleRate) const;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawMeters (juce::Graphics&, juce::Rectangle<float>);

    ReferenceProcessor& proc;
    juce::LookAndFeel_V4 lnf;

    Segmented sourceSel, abSel, levelSel, bandSel, stereoSel;
    juce::TextButton resetButton { "RESET METERS" };

    // ファイル
    juce::TextButton loadButton { "LOAD" }, playButton { "PLAY" }, loopButton { "LOOP" }, followButton { "FOLLOW DAW" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt, followAtt;
    WaveformView waveform;
    std::unique_ptr<juce::FileChooser> chooser;

    // ライブ入力
    juce::ComboBox deviceBox;
    juce::TextButton refreshButton { "REFRESH" };
    juce::String liveStatus;
    juce::Colour liveStatusColour;
    double silentSince = 0.0;
    int shownSource = -1;

    juce::Slider trimSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimAtt;

    // 表示
    std::vector<float> centers, mixDb, refDb, diffDb;
    bool mixHas = false, refHas = false;
    float displayTop = -20.0f;
    std::vector<double> snapBuf;

    juce::Rectangle<int> headerArea, sourceArea, specArea, meterArea, bottomArea;
    juce::Rectangle<int> levelLabel, bandLabel, stereoLabel, trimLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReferenceEditor)
};
