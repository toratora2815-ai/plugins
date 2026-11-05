#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class VelvetTamerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VelvetTamerEditor (VelvetTamerProcessor&);
    ~VelvetTamerEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    // グラフ上のドラッグ（FOCUS の境目・帯域ごと、WEIGHT の点）
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    struct Tile
    {
        explicit Tile (const juce::String& t) : title (t) {}
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void setupKnob (Knob&, const char* id, const char* name, bool big = false);
    void setupToggle (Toggle&, const char* id, const juce::String& text);
    void placeKnob (Knob&, juce::Rectangle<int>);
    juce::Rectangle<int> tileContent (const Tile&) const;
    void drawDisplay (juce::Graphics&, juce::Rectangle<float>);
    float freqToX (float hz, juce::Rectangle<float> r) const;
    float xToFreq (float x, juce::Rectangle<float> r) const;
    float weightToY (float w, juce::Rectangle<float> r) const;
    float yToWeight (float y, juce::Rectangle<float> r) const;
    juce::Rectangle<float> plotArea() const;

    // つかめる物：-1 なし、0..6 WEIGHT の点、それ以外は下の値
    enum { grabNone = -1, grabLow = 100, grabHigh, grabBand };
    int findGrab (juce::Point<float>) const;
    std::vector<juce::RangedAudioParameter*> paramsFor (int grab) const;
    float param (const juce::String& id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float value);

    VelvetTamerProcessor& processor;

    Knob depth, sharpness, selectivity, attack, release, low, high, mix, output;
    Toggle delta, link, lowLatency, sidechain, depthFollow;
    juce::ComboBox presetBox;
    plink::LinkBar linkBar { processor.apvts, juce::Colour (0xff6fd6c9), juce::Colour (0xff1b1621), juce::Colour (0xffefe9f3) };

    int hover = grabNone, grab = grabNone;
    float grabLowHz = 0.0f, grabHighHz = 0.0f, grabFreq = 0.0f;

    Tile tDisplay { "RESONANCE" }, tAmount { "AMOUNT" }, tDetect { "DETECT" }, tTime { "TIME" },
         tFocus { "FOCUS" }, tOut { "OUTPUT" };
    juce::Rectangle<int> displayArea;

    float spec[vt::TamerEngine::displayPoints] {}, gr[vt::TamerEngine::displayPoints] {};
    float shownSpec[vt::TamerEngine::displayPoints] {}, shownGr[vt::TamerEngine::displayPoints] {};
    float shownMax = 0.0f;
    float scSpec[vt::TamerEngine::displayPoints] {}, shownSc[vt::TamerEngine::displayPoints] {};
    bool scOn = false, scSignal = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VelvetTamerEditor)
};
