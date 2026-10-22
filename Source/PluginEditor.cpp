#include "PluginEditor.h"

namespace tessera
{
TesseraEditor::TesseraEditor (TesseraProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      graph (p, selectedBand), strip (p, selectedBand), meters (p)
{

    auto setupBox = [this] (juce::ComboBox& box, juce::Label& label, const juce::String& caption,
                            const juce::String& paramID,
                            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (paramID)))
            box.addItemList (choice->choices, 1);
        addAndMakeVisible (box);
        label.setText (caption, juce::dontSendNotification);
        label.setFont (TesseraLookAndFeel::uiFont (10.5f, true));
        label.setColour (juce::Label::textColourId, colours::textDim);
        label.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (label);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, paramID, box);
    };

    setupBox (phaseBox, phaseLabel, "PHASE", pid::phase, phaseAtt);
    setupBox (qualityBox, qualityLabel, "QUALITY", pid::firQuality, qualityAtt);
    setupBox (rangeBox, rangeLabel, "RANGE", pid::dispRange, rangeAtt);
    setupBox (analyserBox, analyserLabel, "ANALYSER", pid::analyzer, analyserAtt);
    phaseBox.setTooltip ("Minimum phase: zero latency. Linear phase: no phase shift, adds latency.");
    qualityBox.setTooltip ("Linear-phase filter length: longer = better low-frequency accuracy, more latency");

    latencyLabel.setFont (TesseraLookAndFeel::uiFont (11.0f));
    latencyLabel.setColour (juce::Label::textColourId, colours::textDim);
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);

    addAndMakeVisible (graph);
    addAndMakeVisible (strip);
    addAndMakeVisible (meters);

    // set last so every child (including slider text boxes) is rebuilt with our look
    setLookAndFeel (&lnf);

    setResizable (true, true);
    setResizeLimits (980, 620, 2400, 1500);
    setSize (1200, 720);
    startTimerHz (8);
}

TesseraEditor::~TesseraEditor()
{
    setLookAndFeel (nullptr);
}

void TesseraEditor::timerCallback()
{
    const bool linear = phaseBox.getSelectedItemIndex() == LinearPhase;
    qualityBox.setEnabled (linear);
    const int lat = processor.getLatencySamples();
    const double sr = processor.getCurrentSampleRate();
    latencyLabel.setText (lat == 0 ? juce::String ("Latency 0 ms")
                                   : "Latency " + juce::String (1000.0 * lat / sr, 1) + " ms (" + juce::String (lat) + " smp)",
                          juce::dontSendNotification);
}

void TesseraEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    auto header = getLocalBounds().removeFromTop (40);
    g.setColour (colours::panel);
    g.fillRect (header);
    g.setColour (colours::outline);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());

    // wordmark: a small tiled glyph + name
    auto logo = header.reduced (14, 10).removeFromLeft (18).toFloat();
    for (int i = 0; i < 4; ++i)
    {
        auto cell = juce::Rectangle<float> (logo.getX() + (i % 2) * 9.5f, logo.getY() + (i / 2) * 9.5f, 8.0f, 8.0f);
        g.setColour (colours::band (i * 5));
        g.fillRoundedRectangle (cell, 1.5f);
    }
    g.setColour (colours::curve);
    g.setFont (TesseraLookAndFeel::uiFont (17.0f, true));
    g.drawText ("TESSERA", juce::Rectangle<int> (42, 0, 120, 40), juce::Justification::centredLeft);
    g.setColour (colours::accent);
    g.setFont (TesseraLookAndFeel::uiFont (17.0f));
    g.drawText ("EQ", juce::Rectangle<int> (124, 0, 40, 40), juce::Justification::centredLeft);
}

void TesseraEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (40).reduced (10, 8);
    header.removeFromLeft (160);

    auto place = [&header] (juce::Label& l, juce::ComboBox& b, int labelW, int boxW)
    {
        l.setBounds (header.removeFromLeft (labelW));
        header.removeFromLeft (6);
        b.setBounds (header.removeFromLeft (boxW));
        header.removeFromLeft (14);
    };
    place (phaseLabel, phaseBox, 44, 128);
    place (qualityLabel, qualityBox, 56, 84);
    place (rangeLabel, rangeBox, 44, 96);
    place (analyserLabel, analyserBox, 66, 100);
    latencyLabel.setBounds (header);

    meters.setBounds (r.removeFromRight (176));
    const int stripH = 236;
    strip.setBounds (r.removeFromBottom (stripH));
    graph.setBounds (r);
}
} // namespace tessera
