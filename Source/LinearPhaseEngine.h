#pragma once

#include <juce_dsp/juce_dsp.h>
#include "FilterDesign.h"
#include <array>
#include <complex>
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
    Linear-phase processing.

    A background thread turns the band settings into four symmetric FIR filters
    (L->L, R->L, L->R, R->R) whenever they change, and pre-transforms them into
    frequency-domain partitions. The audio thread runs a uniformly partitioned
    overlap-save convolution whose input history (the frequency-domain delay line)
    is shared by every filter set, so a new filter set produces correct output from
    its very first block and can be crossfaded in without dropouts.

    Latency = firLength / 2 + blockSize.
*/
class LinearPhaseEngine : private juce::Thread
{
public:
    using SnapshotProvider = std::function<void (BandSnapshot&, int& firQuality)>;

    explicit LinearPhaseEngine (SnapshotProvider provider);
    ~LinearPhaseEngine() override;

    /** Message/prepare thread. Allocates everything. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** Audio thread. Clears the input history and the I/O FIFOs. */
    void reset() noexcept;

    /** Audio thread. Processes in place (1 or 2 channels). */
    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    /** Turns the background designer on/off (cheap, any thread). */
    void setActive (bool shouldBeActive) noexcept;

    /** The next installed filter set will replace the current one instantly instead of
        crossfading (use while the output is muted, e.g. when the latency changes). */
    void requestHardSwap() noexcept { hardSwapRequested = true; }

    /** FIR length of the filter set currently in use (0 if none). */
    int getInstalledLength() const noexcept { return installedLength; }

    /** Partition size = the engine's own buffering latency. */
    int getBlockSize() const noexcept { return blockSize; }

    int latencyForLength (int firLength) const noexcept { return firLength / 2 + blockSize; }

    /** Forces a redesign at the next opportunity. */
    void invalidate() noexcept { forceRedesign = true; notify(); }

    /** Blocks until at least one filter set has been designed (for tests). */
    bool waitForFirstDesign (int timeoutMs);

    /** Static helper used by the designer and the tests. Fills four IRs of length n. */
    static void designIRs (const BandSnapshot& bands, double sampleRate, int numChannels, int n,
                           std::array<juce::AudioBuffer<float>, 4>& out);

private:
    using Complex = std::complex<float>;

    /** Frequency-domain partitions of the four FIR paths. */
    struct FilterSet
    {
        int length = 0, numPartitions = 0, block = 0, generation = 0;
        std::array<std::vector<Complex>, 4> parts; // [partition * bins + bin]
    };

    void run() override;
    std::unique_ptr<FilterSet> buildFilterSet (const BandSnapshot&, double sr, int chans, int n) const;
    void tryInstallPending() noexcept;
    bool retire (std::unique_ptr<FilterSet>& p) noexcept; // call with handoffLock held
    void processPartitionBlock() noexcept;
    void accumulate (const FilterSet& set, int path, int inputChannel, Complex* acc) const noexcept;

    SnapshotProvider provider;

    // configuration (set in prepare)
    double sampleRate = 44100.0;
    int numChannels = 2;
    int blockSize = 512;          // B
    int bins = 257;               // B + 1
    int maxPartitions = 64;
    std::unique_ptr<juce::dsp::FFT> fft; // size 2B

    // audio-thread state
    std::array<std::vector<Complex>, 2> fdl;   // frequency-domain delay line [slot * bins + bin]
    int fdlPos = 0;
    std::array<std::vector<float>, 2> inFifo, outFifo, prevInput;
    int fifoPos = 0;
    std::vector<float> fftWork;                // 2 * 2B floats
    std::array<std::vector<Complex>, 2> accCur, accOld;
    std::array<std::vector<float>, 2> blockOutCur, blockOutOld;

    std::unique_ptr<FilterSet> current, previous;
    int crossfadePos = 0, crossfadeLength = 1024;
    int installedLength = 0;
    std::atomic<bool> hardSwapRequested { false };

    // hand-over between designer and audio thread
    juce::SpinLock handoffLock;
    std::unique_ptr<FilterSet> pending;
    std::array<std::unique_ptr<FilterSet>, 3> retired;     // freed by the designer thread
    std::atomic<int> generation { 0 };

    std::atomic<bool> prepared { false };
    std::atomic<bool> active { false };
    std::atomic<bool> forceRedesign { true };
    std::atomic<int> designsCompleted { 0 };
    std::atomic<double> designSampleRate { 44100.0 };
    std::atomic<int> designChannels { 2 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LinearPhaseEngine)
};
} // namespace tessera
