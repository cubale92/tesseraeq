#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "FilterDesign.h"
#include "LinearPhaseEngine.h"

namespace tessera
{
/** Lock-free single-producer FIFO of mono samples feeding the analyser. */
class AnalyzerFifo
{
public:
    AnalyzerFifo() : fifo (1 << 15) { data.resize (1 << 15); }

    void push (const float* samples, int num) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (num, s1, n1, s2, n2);
        if (n1 > 0) std::copy (samples, samples + n1, data.begin() + s1);
        if (n2 > 0) std::copy (samples + n1, samples + n1 + n2, data.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }

    int pull (float* dest, int maxNum) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxNum, s1, n1, s2, n2);
        if (n1 > 0) std::copy (data.begin() + s1, data.begin() + s1 + n1, dest);
        if (n2 > 0) std::copy (data.begin() + s2, data.begin() + s2 + n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

private:
    juce::AbstractFifo fifo;
    std::vector<float> data;
};

class TesseraProcessor : public juce::AudioProcessor
{
public:
    TesseraProcessor();
    ~TesseraProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Tessera EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    /** Reads the current static band settings (thread-safe, any thread). */
    void snapshotBands (BandSnapshot& out) const noexcept;

    double getCurrentSampleRate() const noexcept { return currentSampleRate.load(); }

    /** Live dynamic gain offset per band in dB (for the display). */
    float getDynamicDelta (int band) const noexcept { return dynDeltaDisplay[(size_t) band].load(); }

    /** Output peak levels (linear) since last read: L, R, M, S. */
    float takePeak (int index) noexcept { return peaks[(size_t) index].exchange (0.0f); }

    AnalyzerFifo preFifo, postFifo;

    /** For tests: wait until the linear-phase engine has designed its filters. */
    bool waitForLinearPhaseReady (int timeoutMs) { return linear.waitForFirstDesign (timeoutMs); }

private:
    struct BandParams
    {
        std::atomic<float>* on = nullptr; std::atomic<float>* type = nullptr;  std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr; std::atomic<float>* q = nullptr;   std::atomic<float>* slope = nullptr;
        std::atomic<float>* channel = nullptr; std::atomic<float>* dyn = nullptr;
        std::atomic<float>* thresh = nullptr; std::atomic<float>* range = nullptr;
    };

    /** Per-band DSP state. Filter state is kept for 2 channels x up to 4 sections. */
    struct BandRuntime
    {
        bool initialised = false;
        bool wasOn = false;
        float freq = 1000.0f, gain = 0.0f, q = 1.0f;          // smoothed
        float appliedFreq = -1.0f, appliedGain = 1.0e9f, appliedQ = -1.0f;
        int appliedType = -1, appliedSlope = -1;
        BandCoeffs coeffs;
        double z[2][BandCoeffs::maxSections][2] {};

        // dynamic section
        BandCoeffs detCoeffs;
        float detFreq = -1.0f, detQ = -1.0f; int detType = -1;
        double detZ[2][2] {};
        float env = 0.0f;
        float delta = 0.0f;

        // linear-phase mode: dynamic offset applied as a separate minimum-phase filter
        BandCoeffs dynCoeffs;
        float dynAppliedDelta = 1.0e9f, dynAppliedFreq = -1.0f, dynAppliedQ = -1.0f; int dynAppliedType = -1;
        double dz[2][BandCoeffs::maxSections][2] {};

        void clearState() noexcept
        {
            std::memset (z, 0, sizeof (z));
            std::memset (detZ, 0, sizeof (detZ));
            std::memset (dz, 0, sizeof (dz));
            env = 0.0f; delta = 0.0f;
        }
    };

    static void runSections (const BandCoeffs& c, double (*state)[2], float* data, int num) noexcept;
    static float runDetector (const BandCoeffs& c, double* state, float x) noexcept;
    void applyToChannels (const BandCoeffs& c, double (*z)[BandCoeffs::maxSections][2], int mode,
                          float* const* chans, int numChans, int start, int num) noexcept;
    float detectBand (int b, BandRuntime& rt, const float* const* in, int numChans, int start, int num,
                      float attackCoeff, float releaseCoeff) noexcept;

    std::array<BandParams, numBands> bandParams;
    std::array<BandRuntime, numBands> runtime;
    std::array<std::atomic<float>, numBands> dynDeltaDisplay;
    std::array<std::atomic<float>, 4> peaks;

    std::atomic<float>* phaseParam = nullptr;
    std::atomic<float>* qualityParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;

    LinearPhaseEngine linear;
    juce::AudioBuffer<float> inputCopy;       // detector input (min-phase mode)
    juce::AudioBuffer<float> detDelay;        // circular delay line for the detector in linear mode
    int detDelayWrite = 0;
    std::vector<float> scratchA, scratchB, analyserScratch;

    std::atomic<double> currentSampleRate { 44100.0 };
    int lastReportedLatency = -1;

    // Phase-mode / quality transitions: fade out, switch, wait for the new path to be valid, fade in.
    enum class Transition { running, fadingOut, waiting, fadingIn };
    Transition transition = Transition::running;
    int activePhase = -1, activeQuality = -1;
    float transitionGain = 1.0f;
    int waitSamples = 0;
    bool waitingForFilters = false;
    void switchMode (int phase, int quality) noexcept;
    void applyTransitionGain (float* const* chans, int numChans, int num) noexcept;

    juce::AudioBuffer<float> bypassDelay;     // keeps timing when the host bypasses us in linear mode
    int bypassWrite = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;

    static constexpr int subBlock = 32;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TesseraProcessor)
};
} // namespace tessera
