#pragma once

#include <juce_dsp/juce_dsp.h>
#include "FilterDesign.h"
#include <array>
#include <functional>

namespace tessera
{
struct StaticBand
{
    bool on = false;
    int channel = Stereo;
    BandSettings settings;

    bool operator== (const StaticBand& o) const noexcept
    {
        return on == o.on && channel == o.channel && settings.type == o.settings.type
            && juce::exactlyEqual (settings.freq, o.settings.freq) && juce::exactlyEqual (settings.gainDb, o.settings.gainDb)
            && juce::exactlyEqual (settings.q, o.settings.q) && settings.slope == o.settings.slope;
    }
    bool operator!= (const StaticBand& o) const noexcept { return ! (*this == o); }
};

using BandSnapshot = std::array<StaticBand, numBands>;

/** Builds the 2x2 response matrix of the whole EQ at one frequency, using
    zero-phase (magnitude) band responses. */
Matrix2 responseMatrixAt (const BandSnapshot& bands, double freqHz, double sampleRate);

/**
    Linear-phase processing: a background thread turns the band settings into four
    symmetric FIR filters (L->L, R->L, L->R, R->R) whenever they change; the audio
    thread runs them through partitioned convolution. Latency is firLength / 2.
*/
class LinearPhaseEngine : private juce::Thread
{
public:
    using SnapshotProvider = std::function<void (BandSnapshot&, int& firQuality)>;

    explicit LinearPhaseEngine (SnapshotProvider provider);
    ~LinearPhaseEngine() override;

    /** Audio-thread side. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    /** Starts/stops the background designer. */
    void setActive (bool shouldBeActive);

    /** Latency of the IR currently loaded (samples). */
    int getLatencySamples() const noexcept { return loadedLength.load() / 2; }

    /** Forces a redesign at the next opportunity. */
    void invalidate() noexcept { forceRedesign = true; }

    /** Blocks until at least one IR set has been designed and handed over (for tests). */
    bool waitForFirstDesign (int timeoutMs);

    /** Static helper used by the designer and the tests. Fills four IRs of length n. */
    static void designIRs (const BandSnapshot& bands, double sampleRate, int numChannels, int n,
                           std::array<juce::AudioBuffer<float>, 4>& out);

private:
    void run() override;
    void installPendingIfAny() noexcept;

    SnapshotProvider provider;
    juce::dsp::ConvolutionMessageQueue queue;
    std::array<std::unique_ptr<juce::dsp::Convolution>, 4> convs;

    juce::AudioBuffer<float> inCopy, scratch;
    double sampleRate = 44100.0;
    int numChannels = 2;
    int maxBlock = 512;
    std::atomic<bool> prepared { false };
    std::atomic<bool> active { false };
    std::atomic<bool> forceRedesign { true };

    // hand-over from designer thread to audio thread
    juce::SpinLock pendingLock;
    std::array<juce::AudioBuffer<float>, 4> pending;
    bool hasPending = false;
    int pendingLength = 0;
    std::atomic<int> loadedLength { 0 };
    std::atomic<int> designsCompleted { 0 };
    std::atomic<int> installsCompleted { 0 };

    std::atomic<double> designSampleRate { 44100.0 };
    std::atomic<int> designChannels { 2 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinearPhaseEngine)
};
} // namespace tessera
