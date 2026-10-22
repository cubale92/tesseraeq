#include "LinearPhaseEngine.h"

namespace tessera
{
Matrix2 responseMatrixAt (const BandSnapshot& bands, double freqHz, double sampleRate)
{
    Matrix2 m;
    for (const auto& b : bands)
    {
        if (! b.on)
            continue;
        const auto coeffs = designBand (b.settings, sampleRate);
        m.applyBand (coeffs.magnitude (freqHz, sampleRate), b.channel);
    }
    return m;
}

//==============================================================================
LinearPhaseEngine::LinearPhaseEngine (SnapshotProvider p)
    : juce::Thread ("Tessera linear-phase designer"), provider (std::move (p))
{
    // Started here (message thread), never from the audio thread. It idles while inactive.
    startThread (juce::Thread::Priority::low);
}

LinearPhaseEngine::~LinearPhaseEngine()
{
    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

void LinearPhaseEngine::prepare (double sr, int /*maxBlockSize*/, int channels)
{
    prepared = false;
    const juce::SpinLock::ScopedLockType lock (handoffLock);

    sampleRate = sr;
    numChannels = juce::jlimit (1, 2, channels);
    const int rateFactor = sr > 100000.0 ? 4 : (sr > 50000.0 ? 2 : 1);
    blockSize = 512 * rateFactor;
    bins = blockSize + 1;
    maxPartitions = firLengthFor (3, sr) / blockSize;
    crossfadeLength = blockSize * 2;

    fft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (2.0 * blockSize)));
    fftWork.assign ((size_t) (4 * blockSize), 0.0f);

    for (int ch = 0; ch < 2; ++ch)
    {
        fdl[(size_t) ch].assign ((size_t) (maxPartitions * bins), Complex());
        inFifo[(size_t) ch].assign ((size_t) blockSize, 0.0f);
        outFifo[(size_t) ch].assign ((size_t) blockSize, 0.0f);
        prevInput[(size_t) ch].assign ((size_t) blockSize, 0.0f);
        accCur[(size_t) ch].assign ((size_t) bins, Complex());
        accOld[(size_t) ch].assign ((size_t) bins, Complex());
        blockOutCur[(size_t) ch].assign ((size_t) blockSize, 0.0f);
        blockOutOld[(size_t) ch].assign ((size_t) blockSize, 0.0f);
    }
    fdlPos = 0;
    fifoPos = 0;

    // filter sets built for another rate/layout are useless now
    current.reset(); previous.reset(); pending.reset();
    for (auto& r : retired) r.reset();
    ++generation;
    installedLength = 0;
    crossfadePos = 0;

    designSampleRate = sr;
    designChannels = numChannels;
    forceRedesign = true;
    prepared = true;
    notify();
}

void LinearPhaseEngine::reset() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (fdl[(size_t) ch].begin(), fdl[(size_t) ch].end(), Complex());
        std::fill (inFifo[(size_t) ch].begin(), inFifo[(size_t) ch].end(), 0.0f);
        std::fill (outFifo[(size_t) ch].begin(), outFifo[(size_t) ch].end(), 0.0f);
        std::fill (prevInput[(size_t) ch].begin(), prevInput[(size_t) ch].end(), 0.0f);
    }
    fifoPos = 0;
}

void LinearPhaseEngine::setActive (bool shouldBeActive) noexcept
{
    if (active.exchange (shouldBeActive) != shouldBeActive && shouldBeActive)
    {
        forceRedesign = true;
        notify();
    }
}

bool LinearPhaseEngine::waitForFirstDesign (int timeoutMs)
{
    const auto start = juce::Time::getMillisecondCounter();
    while (designsCompleted.load() == 0)
    {
        if (juce::Time::getMillisecondCounter() - start > (juce::uint32) timeoutMs)
            return false;
        juce::Thread::sleep (5);
    }
    return true;
}

//==============================================================================
bool LinearPhaseEngine::retire (std::unique_ptr<FilterSet>& p) noexcept
{
    if (p == nullptr)
        return true;
    for (auto& slot : retired)
        if (slot == nullptr) { slot = std::move (p); return true; }
    return false;
}

void LinearPhaseEngine::tryInstallPending() noexcept
{
    const bool hard = hardSwapRequested.load();
    if (previous != nullptr && ! hard)
        return; // still crossfading

    const juce::SpinLock::ScopedTryLockType lock (handoffLock);
    if (! lock.isLocked() || pending == nullptr)
        return;

    int freeSlots = 0;
    for (auto& slot : retired)
        freeSlots += slot == nullptr ? 1 : 0;

    // A set designed for a different configuration (before a re-prepare) is useless.
    if (pending->generation != generation.load() || pending->block != blockSize)
    {
        if (retire (pending)) forceRedesign = true;
        return;
    }

    if (hard || current == nullptr)
    {
        if (freeSlots < 2) return;
        retire (previous);
        retire (current);
        current = std::move (pending);
        hardSwapRequested = false;
    }
    else
    {
        previous = std::move (current);
        current = std::move (pending);
    }
    crossfadePos = 0;
    installedLength = current->length;
}

void LinearPhaseEngine::accumulate (const FilterSet& set, int path, int inputChannel, Complex* acc) const noexcept
{
    const auto& h = set.parts[(size_t) path];
    const auto& x = fdl[(size_t) inputChannel];
    for (int p = 0; p < set.numPartitions; ++p)
    {
        int slot = fdlPos - p;
        if (slot < 0) slot += maxPartitions;
        const Complex* xs = x.data() + (size_t) slot * (size_t) bins;
        const Complex* hs = h.data() + (size_t) p * (size_t) bins;
        for (int k = 0; k < bins; ++k)
            acc[k] += xs[k] * hs[k];
    }
}

void LinearPhaseEngine::processPartitionBlock() noexcept
{
    const int B = blockSize;

    // 1. transform the newest input frame [previous B | current B] into the delay line
    fdlPos = (fdlPos + 1) % maxPartitions;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* w = fftWork.data();
        std::copy (prevInput[(size_t) ch].begin(), prevInput[(size_t) ch].end(), w);
        std::copy (inFifo[(size_t) ch].begin(), inFifo[(size_t) ch].end(), w + B);
        std::fill (w + 2 * B, w + 4 * B, 0.0f);
        fft->performRealOnlyForwardTransform (w, true);
        auto* dst = fdl[(size_t) ch].data() + (size_t) fdlPos * (size_t) bins;
        std::copy (w, w + 2 * bins, reinterpret_cast<float*> (dst));
        std::copy (inFifo[(size_t) ch].begin(), inFifo[(size_t) ch].end(), prevInput[(size_t) ch].begin());
    }

    auto render = [&] (const FilterSet* set, std::array<std::vector<Complex>, 2>& acc, std::array<std::vector<float>, 2>& out)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* a = acc[(size_t) ch].data();
            std::fill (a, a + bins, Complex());
            if (set != nullptr)
            {
                if (numChannels == 1)
                    accumulate (*set, 0, 0, a);
                else if (ch == 0) { accumulate (*set, 0, 0, a); accumulate (*set, 1, 1, a); } // L = a*L + b*R
                else              { accumulate (*set, 2, 0, a); accumulate (*set, 3, 1, a); } // R = c*L + d*R
            }
            float* w = fftWork.data();
            std::copy (reinterpret_cast<const float*> (a), reinterpret_cast<const float*> (a) + 2 * bins, w);
            std::fill (w + 2 * bins, w + 4 * B, 0.0f);
            fft->performRealOnlyInverseTransform (w);
            std::copy (w + B, w + 2 * B, out[(size_t) ch].begin()); // overlap-save: keep the second half
        }
    };

    render (current.get(), accCur, blockOutCur);

    if (previous != nullptr)
    {
        render (previous.get(), accOld, blockOutOld);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& o = outFifo[(size_t) ch];
            for (int i = 0; i < B; ++i)
            {
                const float t = juce::jmin (1.0f, (float) (crossfadePos + i) / (float) crossfadeLength);
                o[(size_t) i] = blockOutOld[(size_t) ch][(size_t) i] * (1.0f - t) + blockOutCur[(size_t) ch][(size_t) i] * t;
            }
        }
        crossfadePos += B;
        if (crossfadePos >= crossfadeLength)
        {
            const juce::SpinLock::ScopedTryLockType lock (handoffLock);
            if (lock.isLocked())
                retire (previous);  // if no slot is free we simply try again next block (t is already 1)
        }
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
            std::copy (blockOutCur[(size_t) ch].begin(), blockOutCur[(size_t) ch].end(), outFifo[(size_t) ch].begin());
    }
}

void LinearPhaseEngine::process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (! prepared.load())
        return;

    tryInstallPending();

    const int chans = juce::jmin (buffer.getNumChannels(), numChannels);
    float* data[2] { buffer.getWritePointer (0), chans > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < chans; ++ch)
        {
            inFifo[(size_t) ch][(size_t) fifoPos] = data[ch][i];
            data[ch][i] = outFifo[(size_t) ch][(size_t) fifoPos];
        }
        if (++fifoPos == blockSize)
        {
            fifoPos = 0;
            processPartitionBlock();
            if (previous == nullptr || hardSwapRequested.load())
                tryInstallPending();
        }
    }
}

//==============================================================================
std::unique_ptr<LinearPhaseEngine::FilterSet>
LinearPhaseEngine::buildFilterSet (const BandSnapshot& snap, double sr, int chans, int n) const
{
    std::array<juce::AudioBuffer<float>, 4> irs;
    designIRs (snap, sr, chans, n, irs);

    const int B = blockSize;   // read once: prepare() may change it concurrently (the set is then
    const int nb = B + 1;      // rejected at install by its generation/block tag)
    auto set = std::make_unique<FilterSet>();
    set->length = n;
    set->block = B;
    set->numPartitions = n / B;

    juce::dsp::FFT f (juce::roundToInt (std::log2 (2.0 * B)));
    std::vector<float> w ((size_t) (4 * B));
    for (int path = 0; path < 4; ++path)
    {
        auto& parts = set->parts[(size_t) path];
        parts.assign ((size_t) (set->numPartitions * nb), Complex());
        if (chans == 1 && path > 0)
            continue;
        const float* h = irs[(size_t) path].getReadPointer (0);
        for (int p = 0; p < set->numPartitions; ++p)
        {
            std::fill (w.begin(), w.end(), 0.0f);
            std::copy (h + p * B, h + (p + 1) * B, w.begin()); // segment, zero-padded to 2B
            f.performRealOnlyForwardTransform (w.data(), true);
            std::copy (w.data(), w.data() + 2 * nb, reinterpret_cast<float*> (parts.data() + (size_t) p * (size_t) nb));
        }
    }
    return set;
}

void LinearPhaseEngine::run()
{
    BandSnapshot last {};
    int lastLength = -1;
    double lastRate = 0.0;
    int lastChannels = 0;
    BandSnapshot snap {};

    while (! threadShouldExit())
    {
        // free whatever the audio thread has retired
        std::array<std::unique_ptr<FilterSet>, 3> toFree;
        {
            const juce::SpinLock::ScopedLockType lock (handoffLock);
            for (size_t i = 0; i < retired.size(); ++i)
                toFree[i] = std::move (retired[i]);
        }
        for (auto& f : toFree)
            f.reset();

        if (active.load() && prepared.load())
        {
            int quality = 1;
            provider (snap, quality);
            const int gen = generation.load();
            const double sr = designSampleRate.load();
            const int chans = designChannels.load();
            const int n = juce::jmin (firLengthFor (quality, sr), maxPartitions * blockSize);

            const bool changed = forceRedesign.exchange (false) || n != lastLength || ! juce::exactlyEqual (sr, lastRate)
                              || chans != lastChannels || snap != last;
            if (changed)
            {
                auto set = buildFilterSet (snap, sr, chans, n);
                set->generation = gen;
                std::unique_ptr<FilterSet> stale;
                {
                    const juce::SpinLock::ScopedLockType lock (handoffLock);
                    stale = std::move (pending);    // a newer design supersedes one not yet installed
                    pending = std::move (set);
                }
                stale.reset();
                last = snap; lastLength = n; lastRate = sr; lastChannels = chans;
                ++designsCompleted;
            }
        }
        wait (active.load() ? 15 : 200);
    }
}

void LinearPhaseEngine::designIRs (const BandSnapshot& bands, double sr, int channels, int n,
                                   std::array<juce::AudioBuffer<float>, 4>& out)
{
    const int order = juce::roundToInt (std::log2 ((double) n));
    jassert ((1 << order) == n);
    juce::dsp::FFT fft (order);

    // Pre-design each active band once.
    struct Active { BandCoeffs c; int channel; };
    std::vector<Active> active;
    for (const auto& b : bands)
        if (b.on)
            active.push_back ({ designBand (b.settings, sr), b.channel });

    const int bins = n / 2 + 1;
    std::array<std::vector<float>, 4> spec;
    for (auto& s : spec)
        s.assign ((size_t) (2 * n), 0.0f);

    for (int k = 0; k < bins; ++k)
    {
        const double f = (double) k * sr / (double) n;
        Matrix2 m;
        for (const auto& a : active)
            m.applyBand (a.c.magnitude (juce::jmax (f, 0.01), sr), a.channel);

        double g[4] { m.a, m.b, m.c, m.d };
        if (channels == 1)
        {
            g[0] = m.a + m.b; // mono: L == R
            g[1] = g[2] = g[3] = 0.0;
        }

        // Zero-phase spectrum delayed by n/2 samples: multiply by e^{-j*pi*k} = (-1)^k
        const float sign = (k % 2 == 0) ? 1.0f : -1.0f;
        for (int i = 0; i < 4; ++i)
        {
            spec[(size_t) i][(size_t) (2 * k)]     = (float) g[i] * sign;
            spec[(size_t) i][(size_t) (2 * k + 1)] = 0.0f;
        }
    }

    // Window (Blackman-Harris-ish via JUCE's Blackman) to tame truncation.
    // n + 1 points so the window is centred exactly on sample n/2 (the IR peak).
    std::vector<float> window ((size_t) n + 1);
    juce::dsp::WindowingFunction<float>::fillWindowingTables (window.data(), (size_t) n + 1,
                                                              juce::dsp::WindowingFunction<float>::blackman, false);

    for (int i = 0; i < 4; ++i)
    {
        auto& s = spec[(size_t) i];
        fft.performRealOnlyInverseTransform (s.data());

        out[(size_t) i].setSize (1, n, false, false, false);
        auto* dst = out[(size_t) i].getWritePointer (0);
        for (int t = 0; t < n; ++t)
            dst[t] = s[(size_t) t] * window[(size_t) t];
    }
}
} // namespace tessera
