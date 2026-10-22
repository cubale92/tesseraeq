#include "BandStrip.h"
#include "GraphComponent.h"
#include "TesseraLookAndFeel.h"

namespace tessera
{
BandColumn::BandColumn (TesseraProcessor& p, int b, juce::Value& selectedBand)
    : processor (p), band (b)
{
    selected.referTo (selectedBand);
    const auto colour = colours::band (b);

    indexButton.setButtonText (juce::String (b + 1));
    indexButton.setColour (juce::TextButton::buttonColourId, colour);
    indexButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    indexButton.onClick = [this] { selected = band; };
    indexButton.setTooltip ("Select band " + juce::String (b + 1));
    addAndMakeVisible (indexButton);

    removeButton.setTooltip ("Remove band");
    removeButton.onClick = [this]
    {
        GraphComponent::setParam (processor, pid::band (band, pid::on), 0.0f);
        if ((int) selected.getValue() == band) selected = -1;
    };
    addAndMakeVisible (removeButton);

    typeBox.addItemList (filterTypeNames(), 1);
    channelBox.addItemList (channelModeNames(), 1);
    slopeBox.addItemList (slopeNames(), 1);
    for (auto* c : { &typeBox, &channelBox, &slopeBox })
        addAndMakeVisible (*c);
    typeBox.setTooltip ("Filter shape");
    channelBox.setTooltip ("Which part of the signal this band processes");
    slopeBox.setTooltip ("Cut filter slope");

    setupKnob (freq, freqLabel, "FREQ");
    setupKnob (gain, gainLabel, "GAIN");
    setupKnob (q, qLabel, "Q");
    setupKnob (thresh, threshLabel, "THRESH");
    setupKnob (range, rangeLabel, "RANGE");
    for (auto* s : { &freq, &gain, &q, &thresh, &range })
        s->setColour (juce::Slider::rotarySliderFillColourId, colour);

    dynButton.setClickingTogglesState (true);
    dynButton.setColour (juce::TextButton::buttonOnColourId, colour);
    dynButton.setTooltip ("Dynamic EQ: the band moves by up to RANGE dB when its frequency region crosses THRESH");
    addAndMakeVisible (dynButton);

    auto& s = processor.apvts;
    typeAtt    = std::make_unique<ComboAttachment>  (s, pid::band (b, pid::type), typeBox);
    channelAtt = std::make_unique<ComboAttachment>  (s, pid::band (b, pid::channel), channelBox);
    slopeAtt   = std::make_unique<ComboAttachment>  (s, pid::band (b, pid::slope), slopeBox);
    freqAtt    = std::make_unique<SliderAttachment> (s, pid::band (b, pid::freq), freq);
    gainAtt    = std::make_unique<SliderAttachment> (s, pid::band (b, pid::gain), gain);
    qAtt       = std::make_unique<SliderAttachment> (s, pid::band (b, pid::q), q);
    threshAtt  = std::make_unique<SliderAttachment> (s, pid::band (b, pid::thresh), thresh);
    rangeAtt   = std::make_unique<SliderAttachment> (s, pid::band (b, pid::range), range);
    dynAtt     = std::make_unique<ButtonAttachment> (s, pid::band (b, pid::dyn), dynButton);

    gain.setDoubleClickReturnValue (true, 0.0);
    q.setDoubleClickReturnValue (true, 1.0);
    range.setDoubleClickReturnValue (true, -6.0);

    refreshEnablement();
}

BandColumn::~BandColumn() = default;

void BandColumn::setupKnob (juce::Slider& s, juce::Label& caption, const juce::String& text)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 15);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity (180);
    s.setVelocityBasedMode (false);
    addAndMakeVisible (s);

    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setFont (TesseraLookAndFeel::uiFont (10.0f, true));
    caption.setColour (juce::Label::textColourId, colours::textDim);
    caption.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (caption);
}

void BandColumn::refreshEnablement()
{
    const int type = typeBox.getSelectedItemIndex();
    const bool hasGain = typeHasGain (type);
    const bool dynOn = dynButton.getToggleState();

    gain.setEnabled (hasGain);
    slopeBox.setEnabled (typeHasSlope (type));
    dynButton.setEnabled (hasGain);
    thresh.setEnabled (hasGain && dynOn);
    range.setEnabled (hasGain && dynOn);

    const bool isSel = (int) selected.getValue() == band;
    indexButton.setToggleState (isSel, juce::dontSendNotification);
    indexButton.setColour (juce::TextButton::buttonOnColourId, colours::band (band).brighter (0.3f));
}

void BandColumn::paint (juce::Graphics& g)
{
    const bool isSel = (int) selected.getValue() == band;
    auto r = getLocalBounds().toFloat().reduced (2.0f, 0.0f);
    g.setColour (isSel ? colours::panelRaised : colours::panel);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (colours::band (band).withAlpha (isSel ? 1.0f : 0.55f));
    g.fillRoundedRectangle (r.removeFromTop (3.0f), 1.5f);
    if (isSel)
    {
        g.setColour (colours::band (band).withAlpha (0.5f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.5f, 0.5f), 4.0f, 1.0f);
    }

    // divider above the dynamics row
    g.setColour (colours::outline);
    g.drawHorizontalLine (dynButton.getY() - 6, 10.0f, (float) getWidth() - 10.0f);
}

void BandColumn::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (8);

    auto row = r.removeFromTop (20);
    indexButton.setBounds (row.removeFromLeft (26));
    removeButton.setBounds (row.removeFromRight (20));
    row.removeFromLeft (4); row.removeFromRight (4);
    typeBox.setBounds (row);

    r.removeFromTop (4);
    row = r.removeFromTop (20);
    const int half = (row.getWidth() - 4) / 2;
    channelBox.setBounds (row.removeFromLeft (half));
    slopeBox.setBounds (row.removeFromRight (half));

    r.removeFromTop (6);
    auto knobRow = [] (juce::Rectangle<int> area, std::initializer_list<std::pair<juce::Slider*, juce::Label*>> items)
    {
        const int w = area.getWidth() / (int) items.size();
        for (auto& [s, l] : items)
        {
            auto cell = area.removeFromLeft (w);
            l->setBounds (cell.removeFromTop (12));
            s->setBounds (cell);
        }
    };
    knobRow (r.removeFromTop (74), { { &freq, &freqLabel }, { &gain, &gainLabel }, { &q, &qLabel } });

    r.removeFromTop (12);
    auto dynRow = r.removeFromTop (74);
    auto left = dynRow.removeFromLeft (dynRow.getWidth() / 3);
    dynButton.setBounds (left.withSizeKeepingCentre (left.getWidth() - 8, 20).withY (left.getY() + 22));
    knobRow (dynRow, { { &thresh, &threshLabel }, { &range, &rangeLabel } });
}

void BandColumn::mouseDown (const juce::MouseEvent&)
{
    selected = band;
}

//==============================================================================
BandStrip::BandStrip (TesseraProcessor& p, juce::Value& selectedBand)
    : processor (p)
{
    selected.referTo (selectedBand);
    selected.addListener (this);

    for (int b = 0; b < numBands; ++b)
    {
        columns[(size_t) b] = std::make_unique<BandColumn> (processor, b, selectedBand);
        content.addChildComponent (*columns[(size_t) b]);
    }

    addButton.setTooltip ("Add a band (or double-click the graph)");
    addButton.onClick = [this]
    {
        // place new bands in a sensible spot: an octave above the highest active band
        float f = 100.0f;
        for (int b = 0; b < numBands; ++b)
            if (processor.apvts.getRawParameterValue (pid::band (b, pid::on))->load() > 0.5f)
                f = juce::jmax (f, processor.apvts.getRawParameterValue (pid::band (b, pid::freq))->load() * 2.0f);
        const int nb = GraphComponent::addBand (processor, Bell, juce::jmin (f, 16000.0f), 0.0f);
        if (nb >= 0) selected = nb;
    };
    content.addAndMakeVisible (addButton);

    emptyHint.setText ("Double-click the graph or press + to add a band", juce::dontSendNotification);
    emptyHint.setColour (juce::Label::textColourId, colours::textDim);
    emptyHint.setJustificationType (juce::Justification::centredLeft);
    content.addAndMakeVisible (emptyHint);

    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    startTimerHz (10);
}

BandStrip::~BandStrip()
{
    selected.removeListener (this);
}

void BandStrip::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void BandStrip::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutColumns();
}

void BandStrip::layoutColumns()
{
    const int h = juce::jmax (100, getHeight() - viewport.getScrollBarThickness() - 2);
    int x = 0, active = 0;
    for (int b = 0; b < numBands; ++b)
    {
        auto& c = *columns[(size_t) b];
        const bool on = processor.apvts.getRawParameterValue (pid::band (b, pid::on))->load() > 0.5f;
        lastOn[(size_t) b] = on;
        c.setVisible (on);
        if (on)
        {
            c.setBounds (x, 0, BandColumn::preferredWidth, h);
            x += BandColumn::preferredWidth;
            ++active;
        }
    }
    addButton.setBounds (x + 6, 8, 36, 36);
    addButton.setVisible (active < numBands);
    emptyHint.setVisible (active == 0);
    emptyHint.setBounds (x + 52, 8, 400, 36);
    content.setSize (juce::jmax (x + (active == 0 ? 460 : 50), viewport.getMaximumVisibleWidth()), h);
}

void BandStrip::timerCallback()
{
    bool changed = false;
    for (int b = 0; b < numBands; ++b)
        changed |= lastOn[(size_t) b] != (processor.apvts.getRawParameterValue (pid::band (b, pid::on))->load() > 0.5f);
    if (changed)
        layoutColumns();

    for (auto& c : columns)
        if (c->isVisible())
            c->refreshEnablement();
}

void BandStrip::valueChanged (juce::Value&)
{
    const int sel = (int) selected.getValue();
    if (sel != lastSelected)
    {
        lastSelected = sel;
        if (sel >= 0 && sel < numBands)
        {
            layoutColumns();
            auto& c = *columns[(size_t) sel];
            const auto view = viewport.getViewArea();
            if (c.getX() < view.getX())
                viewport.setViewPosition (c.getX(), 0);
            else if (c.getRight() > view.getRight())
                viewport.setViewPosition (c.getRight() - view.getWidth(), 0);
        }
        for (auto& c : columns)
        {
            c->refreshEnablement();
            c->repaint();
        }
    }
}
} // namespace tessera
