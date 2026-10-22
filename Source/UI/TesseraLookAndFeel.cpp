#include "TesseraLookAndFeel.h"

namespace tessera
{
TesseraLookAndFeel::TesseraLookAndFeel()
{
    using namespace juce;
    setColour (ResizableWindow::backgroundColourId, colours::background);
    setColour (ComboBox::backgroundColourId, colours::panelRaised);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::outlineColourId, colours::outline);
    setColour (ComboBox::arrowColourId, colours::textDim);
    setColour (PopupMenu::backgroundColourId, colours::panelRaised);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.25f));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (Label::textColourId, colours::text);
    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, colours::accent.withAlpha (0.4f));
    setColour (TextEditor::backgroundColourId, colours::panelRaised);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::highlightColourId, colours::accent.withAlpha (0.4f));
    setColour (CaretComponent::caretColourId, colours::accent);
    setColour (TextButton::buttonColourId, colours::panelRaised);
    setColour (TextButton::buttonOnColourId, colours::accent);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, colours::background);
    setColour (ScrollBar::thumbColourId, colours::gridStrong);
    setColour (TooltipWindow::backgroundColourId, colours::panelRaised);
    setColour (TooltipWindow::textColourId, colours::text);
}

juce::Font TesseraLookAndFeel::uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

void TesseraLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    using namespace juce;
    auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float lineW = jmax (2.0f, radius * 0.16f);
    const float arcR = radius - lineW * 0.5f;

    const auto accent = slider.findColour (Slider::rotarySliderFillColourId);
    const bool enabled = slider.isEnabled();

    // body
    const float bodyR = radius * 0.70f;
    ColourGradient grad (colours::panelRaised.brighter (0.25f), centre.x, centre.y - bodyR,
                         colours::panel.darker (0.3f), centre.x, centre.y + bodyR, false);
    g.setGradientFill (grad);
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // track
    Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::outline);
    g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

    // value arc (bipolar sliders fill from the centre)
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroAngle = bipolar ? startAngle + (float) slider.valueToProportionOfLength (0.0) * (endAngle - startAngle)
                                    : startAngle;
    const float valueAngle = startAngle + pos * (endAngle - startAngle);
    if (std::abs (valueAngle - zeroAngle) > 0.001f)
    {
        Path arc;
        arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (zeroAngle, valueAngle), jmax (zeroAngle, valueAngle), true);
        g.setColour (enabled ? accent : colours::textDim.withAlpha (0.5f));
        g.strokePath (arc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // pointer
    const auto tip = centre.getPointOnCircumference (bodyR * 0.85f, valueAngle);
    const auto base = centre.getPointOnCircumference (bodyR * 0.25f, valueAngle);
    g.setColour (enabled ? colours::curve : colours::textDim);
    g.drawLine ({ base, tip }, jmax (1.5f, radius * 0.08f));
}

void TesseraLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                           juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    using namespace juce;
    auto r = Rectangle<int> (x, y, width, height).toFloat();
    const float trackX = r.getCentreX();
    g.setColour (colours::outline);
    g.fillRoundedRectangle (trackX - 2.0f, r.getY(), 4.0f, r.getHeight(), 2.0f);

    const float zeroY = (float) slider.getPositionOfValue (0.0);
    g.setColour (colours::accent);
    g.fillRect (Rectangle<float>::leftTopRightBottom (trackX - 2.0f, jmin (zeroY, sliderPos), trackX + 2.0f, jmax (zeroY, sliderPos)));

    auto thumb = Rectangle<float> (0, 0, jmin (26.0f, r.getWidth()), 12.0f).withCentre ({ trackX, sliderPos });
    g.setColour (colours::panelRaised.brighter (0.3f));
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour (colours::curve);
    g.drawLine (thumb.getX() + 4.0f, sliderPos, thumb.getRight() - 4.0f, sliderPos, 1.5f);
}

void TesseraLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace juce;
    auto r = Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent.withAlpha (0.6f) : box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const float arrowX = (float) width - 10.0f, cy = (float) height * 0.5f;
    Path arrow;
    arrow.addTriangle (arrowX - 3.5f, cy - 1.5f, arrowX + 3.5f, cy - 1.5f, arrowX, cy + 2.5f);
    g.setColour (box.isEnabled() ? colours::textDim : colours::outline);
    g.fillPath (arrow);
}

juce::Font TesseraLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return uiFont (juce::jmin (13.0f, (float) box.getHeight() * 0.62f));
}

void TesseraLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 16, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font TesseraLookAndFeel::getPopupMenuFont() { return uiFont (14.0f); }

void TesseraLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& bg,
                                               bool over, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    auto c = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (down)      c = c.brighter (0.15f);
    else if (over) c = c.brighter (0.08f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (button.getToggleState() ? c.brighter (0.2f) : colours::outline);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void TesseraLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (2, 0), juce::Justification::centred, 1);
}

juce::Font TesseraLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return uiFont (juce::jmin (13.0f, (float) h * 0.6f), true);
}

juce::Label* TesseraLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (uiFont (11.5f));
    l->setColour (juce::Label::textColourId, colours::text);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
}

juce::Font TesseraLookAndFeel::getLabelFont (juce::Label& l)
{
    return uiFont (juce::jmin (13.0f, (float) l.getHeight() * 0.75f));
}
} // namespace tessera
