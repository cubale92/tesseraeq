#include "MeterPanel.h"
#include "TesseraLookAndFeel.h"

namespace tessera
{
static constexpr float kMeterTop = 6.0f, kMeterBottom = -60.0f;

MeterPanel::MeterPanel (TesseraProcessor& p) : processor (p)
{
    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& text, juce::Colour c)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 15);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setColour (juce::Slider::rotarySliderFillColourId, c);
        addAndMakeVisible (s);
        l.setText (text, juce::dontSendNotification);
        l.setFont (TesseraLookAndFeel::uiFont (10.0f, true));
        l.setColour (juce::Label::textColourId, colours::textDim);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };
    setupKnob (attack, attackLabel, "ATTACK", colours::textDim);
    setupKnob (release, releaseLabel, "RELEASE", colours::textDim);
    setupKnob (output, outputLabel, "OUTPUT", colours::accent);
    output.setDoubleClickReturnValue (true, 0.0);

    outputAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, pid::output, output);
    attackAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, pid::attack, attack);
    releaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, pid::release, release);

    startTimerHz (30);
}

MeterPanel::~MeterPanel() = default;

float MeterPanel::yForDb (float db) const noexcept
{
    return juce::jmap (juce::jlimit (kMeterBottom, kMeterTop, db), kMeterTop, kMeterBottom,
                       (float) meterArea.getY() + 16.0f, (float) meterArea.getBottom() - 18.0f);
}

void MeterPanel::timerCallback()
{
    for (int i = 0; i < 4; ++i)
    {
        auto& m = meters[(size_t) i];
        const float db = juce::Decibels::gainToDecibels (processor.takePeak (i), -100.0f);
        m.level = db > m.level ? db : juce::jmax (db, m.level - 1.2f);
        if (db >= m.hold) { m.hold = db; m.holdFrames = 45; }
        else if (--m.holdFrames <= 0) m.hold = juce::jmax (m.level, m.hold - 0.8f);
        m.maxPeak = juce::jmax (m.maxPeak, db);
    }
    repaint (meterArea);
}

void MeterPanel::mouseDown (const juce::MouseEvent& e)
{
    if (meterArea.contains (e.getPosition()))
        for (auto& m : meters)
            m.maxPeak = -100.0f;
}

void MeterPanel::paint (juce::Graphics& g)
{
    using namespace juce;
    g.fillAll (colours::panel);
    g.setColour (colours::outline);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());

    // section captions
    g.setFont (TesseraLookAndFeel::uiFont (11.0f, true));
    g.setColour (colours::textDim);
    g.drawText ("OUTPUT LEVEL", Rectangle<int> (0, 6, getWidth(), 14), Justification::centred);
    g.drawText ("DYNAMICS", Rectangle<int> (0, attackLabel.getY() - 20, getWidth(), 14), Justification::centred);

    // scale
    const int numMeters = 4;
    const float mw = 15.0f, gap = 9.0f;
    const float totalW = numMeters * mw + (numMeters - 1) * gap;
    const float x0 = (float) meterArea.getCentreX() - totalW * 0.5f + 10.0f;

    g.setFont (TesseraLookAndFeel::uiFont (9.5f));
    for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const float y = yForDb (db);
        g.setColour (colours::textDim);
        g.drawText (String ((int) db), Rectangle<float> (x0 - 30.0f, y - 6.0f, 24.0f, 12.0f), Justification::right);
        g.setColour (colours::grid);
        g.drawHorizontalLine (roundToInt (y), x0 - 4.0f, x0 + totalW);
    }

    static const char* names[] { "L", "R", "M", "S" };
    for (int i = 0; i < numMeters; ++i)
    {
        const auto& m = meters[(size_t) i];
        const float x = x0 + i * (mw + gap);
        const float top = yForDb (kMeterTop), bottom = yForDb (kMeterBottom);
        auto track = Rectangle<float>::leftTopRightBottom (x, top, x + mw, bottom);
        g.setColour (colours::background);
        g.fillRoundedRectangle (track, 2.0f);

        const float y = yForDb (m.level);
        if (m.level > kMeterBottom)
        {
            ColourGradient grad (Colour (0xffff5a4e), 0.0f, yForDb (3.0f), Colour (0xff3ecf8e), 0.0f, yForDb (-24.0f), false);
            grad.addColour (0.25, Colour (0xffe9c33b));
            g.setGradientFill (grad);
            g.fillRect (Rectangle<float>::leftTopRightBottom (x + 1.0f, y, x + mw - 1.0f, bottom));
        }
        if (m.hold > kMeterBottom)
        {
            g.setColour (m.hold > 0.0f ? Colour (0xffff5a4e) : colours::curve.withAlpha (0.8f));
            g.fillRect (x + 1.0f, yForDb (m.hold) - 1.0f, mw - 2.0f, 2.0f);
        }

        g.setColour (i < 2 ? colours::text : colours::sideCurve);
        g.setFont (TesseraLookAndFeel::uiFont (11.0f, true));
        g.drawText (names[i], Rectangle<float> (x - 4.0f, bottom + 2.0f, mw + 8.0f, 14.0f), Justification::centred);

        g.setFont (TesseraLookAndFeel::uiFont (9.0f));
        g.setColour (m.maxPeak > 0.0f ? Colour (0xffff5a4e) : colours::textDim);
        g.drawText (m.maxPeak <= -99.0f ? String ("-inf") : String (m.maxPeak, 1),
                    Rectangle<float> (x - 6.0f, top - 15.0f, mw + 12.0f, 12.0f), Justification::centred);
    }
}

void MeterPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (16);

    auto bottom = r.removeFromBottom (92);
    outputLabel.setBounds (bottom.removeFromTop (12));
    output.setBounds (bottom.withSizeKeepingCentre (76, 80));

    r.removeFromBottom (10);
    auto dyn = r.removeFromBottom (86);
    auto half = dyn.getWidth() / 2;
    auto a = dyn.removeFromLeft (half), rl = dyn;
    attackLabel.setBounds (a.removeFromTop (12));
    attack.setBounds (a);
    releaseLabel.setBounds (rl.removeFromTop (12));
    release.setBounds (rl);

    r.removeFromBottom (26);
    meterArea = r;
}
} // namespace tessera
