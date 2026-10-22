#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace tessera
{
/** All controls for one band, laid out as a vertical column. */
class BandColumn : public juce::Component
{
public:
    BandColumn (TesseraProcessor&, int bandIndex, juce::Value& selectedBand);
    ~BandColumn() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Enables/disables controls depending on the filter type and dynamic switch. */
    void refreshEnablement();

    static constexpr int preferredWidth = 168;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupKnob (juce::Slider&, juce::Label& caption, const juce::String& text);

    TesseraProcessor& processor;
    const int band;
    juce::Value selected;

    juce::TextButton indexButton, removeButton { "x" }, dynButton { "DYN" };
    juce::ComboBox typeBox, channelBox, slopeBox;
    juce::Slider freq, gain, q, thresh, range;
    juce::Label freqLabel, gainLabel, qLabel, threshLabel, rangeLabel;

    std::unique_ptr<ComboAttachment> typeAtt, channelAtt, slopeAtt;
    std::unique_ptr<SliderAttachment> freqAtt, gainAtt, qAtt, threshAtt, rangeAtt;
    std::unique_ptr<ButtonAttachment> dynAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandColumn)
};

/** Horizontally scrolling strip of the active bands, plus an "add band" button. */
class BandStrip : public juce::Component,
                  private juce::Timer,
                  private juce::Value::Listener
{
public:
    BandStrip (TesseraProcessor&, juce::Value& selectedBand);
    ~BandStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void valueChanged (juce::Value&) override;
    void layoutColumns();

    TesseraProcessor& processor;
    juce::Value selected;
    juce::Viewport viewport;
    juce::Component content;
    std::array<std::unique_ptr<BandColumn>, numBands> columns;
    juce::TextButton addButton { "+" };
    juce::Label emptyHint;
    std::array<bool, numBands> lastOn {};
    int lastSelected = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandStrip)
};
} // namespace tessera
