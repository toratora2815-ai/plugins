#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// 音の配置図: 左右 = パン、上下 = 音量（OUTPUT）、点の大きさ = 幅。点をドラッグ、ホイール = 幅、ダブルクリック = 元に戻す
class MixField : public juce::Component, private juce::Timer
{
public:
    explicit MixField (ForgeMixerProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    std::vector<ForgeMixerProcessor::Track> shown;
private:
    void timerCallback() override;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (30, 18); }
    float panToX (float p) const { auto r = plot(); return r.getX() + (p + 1.0f) * 0.5f * r.getWidth(); }
    float dbToY (float d) const { auto r = plot(); return r.getY() + (10.0f - juce::jlimit (-40.0f, 10.0f, d)) / 50.0f * r.getHeight(); }
    float xToPan (float x) const { auto r = plot(); return juce::jlimit (-1.0f, 1.0f, (x - r.getX()) / r.getWidth() * 2.0f - 1.0f); }
    float yToDb (float y) const { auto r = plot(); return juce::jlimit (-40.0f, 10.0f, 10.0f - (y - r.getY()) / r.getHeight() * 50.0f); }
    float radius (const ForgeMixerProcessor::Track& t) const { return 12.0f + (t.width + 100.0f) / 200.0f * 18.0f; }
    int hit (juce::Point<float>) const;
    ForgeMixerProcessor& proc;
    int drag = -1; float dragToken = 0;
    ForgeMixerProcessor::Track dragState;
};

class ForgeMixerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ForgeMixerEditor (ForgeMixerProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override { repaint (listArea); }
    ForgeMixerProcessor& processor;
    MixField field { processor };
    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xffffb454), juce::Colour (0xff1d1d22), juce::Colour (0xffe8e4dc) };
    myp::PresetBar presetBar { processor.myPresets, juce::Colour (0xffffb454), juce::Colour (0xff1d1d22), juce::Colour (0xffe8e4dc), true };
    juce::Rectangle<int> listArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeMixerEditor)
};
