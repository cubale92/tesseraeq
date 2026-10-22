#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "../PluginProcessor.h"

namespace tessera
{
struct SpectrumState;

/** Spectrum analyser + EQ curve + draggable band nodes + piano strip. */
class GraphComponent : public juce::Component,
                       private juce::Timer,
                       private juce::Value::Listener
{
public:
    GraphComponent (TesseraProcessor&, juce::Value& selectedBand);
    ~GraphComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Adds a band (first free slot) and returns its index, or -1 if all 24 are in use. */
    static int addBand (TesseraProcessor&, int type, float freq, float gainDb);
    static void setParam (TesseraProcessor&, const juce::String& id, float value);

private:
    void timerCallback() override;
    void valueChanged (juce::Value&) override { repaint(); }

    // coordinate mapping -------------------------------------------------------
    float xForFreq (float f) const noexcept;
    float freqForX (float x) const noexcept;
    float yForDb (float db) const noexcept;
    float dbForY (float y) const noexcept;
    float yForAnalyserDb (float db) const noexcept;
    float rangeDb() const noexcept;
    float maxFreq() const noexcept;

    juce::Point<float> nodePosition (int band) const;
    int nodeAt (juce::Point<float> p) const;
    void showBandMenu (int band);
    void showAddMenu (juce::Point<float> p);
    void processAnalyser (SpectrumState& s, AnalyzerFifo& fifo);

    void drawGrid (juce::Graphics&);
    void drawAnalyser (juce::Graphics&);
    void drawCurves (juce::Graphics&);
    void drawNodes (juce::Graphics&);
    void drawKeyboard (juce::Graphics&);
    void drawReadout (juce::Graphics&);

    TesseraProcessor& processor;
    juce::Value selected;
    BandSnapshot snapshot {};
    std::array<float, numBands> deltas {};

    juce::Rectangle<float> plot, keys;

    int hoverBand = -1, dragBand = -1;
    juce::Point<float> dragStartNode, dragStartMouse;
    bool dragHasGain = false;

    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::unique_ptr<SpectrumState> pre, post;
    std::vector<float> fftBuffer, pullBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphComponent)
};
} // namespace tessera
