#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/TesseraLookAndFeel.h"
#include "UI/GraphComponent.h"
#include "UI/BandStrip.h"
#include "UI/MeterPanel.h"

namespace tessera
{
class TesseraEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TesseraEditor (TesseraProcessor&);
    ~TesseraEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    TesseraProcessor& processor;
    TesseraLookAndFeel lnf;
    juce::Value selectedBand { juce::var (-1) };

    juce::ComboBox phaseBox, qualityBox, rangeBox, analyserBox;
    juce::Label phaseLabel, qualityLabel, rangeLabel, analyserLabel, latencyLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> phaseAtt, qualityAtt, rangeAtt, analyserAtt;

    GraphComponent graph;
    BandStrip strip;
    MeterPanel meters;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TesseraEditor)
};
} // namespace tessera
