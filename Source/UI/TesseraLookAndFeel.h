#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tessera
{
namespace colours
{
    const juce::Colour background   { 0xff121419 };
    const juce::Colour panel        { 0xff1a1d24 };
    const juce::Colour panelRaised  { 0xff222631 };
    const juce::Colour outline      { 0xff2c313d };
    const juce::Colour grid         { 0xff252a34 };
    const juce::Colour gridStrong   { 0xff323946 };
    const juce::Colour text         { 0xffc8cdd8 };
    const juce::Colour textDim      { 0xff7c8494 };
    const juce::Colour accent       { 0xffe9a23b };
    const juce::Colour curve        { 0xfff1f3f7 };
    const juce::Colour sideCurve    { 0xff6cc6ff };
    const juce::Colour analyserPost { 0xff4a7dff };
    const juce::Colour analyserPre  { 0xff8a93a6 };

    /** Distinct, evenly spread colour per band. */
    inline juce::Colour band (int index)
    {
        const float hue = std::fmod (0.02f + (float) index * 0.381966f, 1.0f); // golden-ratio spacing
        return juce::Colour::fromHSV (hue, 0.62f, 0.96f, 1.0f);
    }
}

class TesseraLookAndFeel : public juce::LookAndFeel_V4
{
public:
    TesseraLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;

    static juce::Font uiFont (float height, bool bold = false);
};
} // namespace tessera
