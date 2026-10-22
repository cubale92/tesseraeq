#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace tessera
{
/** Output meters (L, R, Mid, Side), dynamics timing and output gain. */
class MeterPanel : public juce::Component, private juce::Timer
{
public:
    explicit MeterPanel (TesseraProcessor&);
    ~MeterPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override; // click meters to reset peak hold

private:
    void timerCallback() override;
    float yForDb (float db) const noexcept;

    TesseraProcessor& processor;

    struct Meter { float level = -100.0f, hold = -100.0f, maxPeak = -100.0f; int holdFrames = 0; };
    std::array<Meter, 4> meters;
    juce::Rectangle<int> meterArea;

    juce::Slider output, attack, release;
    juce::Label outputLabel, attackLabel, releaseLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAtt, attackAtt, releaseAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterPanel)
};
} // namespace tessera
