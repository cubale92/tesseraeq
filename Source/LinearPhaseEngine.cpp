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
    for (auto& c : convs)
        c = std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::NonUniform { 1024 }, queue);
}

LinearPhaseEngine::~LinearPhaseEngine()
{
    stopThread (2000);
}

void LinearPhaseEngine::prepare (double sr, int maxBlockSize, int channels)
{
    sampleRate = sr;
    maxBlock = juce::jmax (1, maxBlockSize);
    numChannels = juce::jlimit (1, 2, channels);
    designSampleRate = sr;
    designChannels = numChannels;

    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 1 };
    for (auto& c : convs)
        c->prepare (spec);

    inCopy.setSize (2, maxBlock, false, false, true);
    scratch.setSize (1, maxBlock, false, false, true);

    prepared = true;
    forceRedesign = true;
}

void LinearPhaseEngine::reset()
{
    for (auto& c : convs)
        c->reset();
}

void LinearPhaseEngine::setActive (bool shouldBeActive)
{
    if (active.exchange (shouldBeActive) == shouldBeActive)
        return;

    if (shouldBeActive)
    {
        forceRedesign = true;
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
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

void LinearPhaseEngine::installPendingIfAny() noexcept
{
    const juce::SpinLock::ScopedTryLockType lock (pendingLock);
    if (! lock.isLocked() || ! hasPending)
        return;

    for (int i = 0; i < 4; ++i)
        convs[(size_t) i]->loadImpulseResponse (std::move (pending[(size_t) i]), sampleRate,
                                                juce::dsp::Convolution::Stereo::no,
                                                juce::dsp::Convolution::Trim::no,
                                                juce::dsp::Convolution::Normalise::no);
    loadedLength = pendingLength;
    hasPending = false;
    ++installsCompleted;
}

void LinearPhaseEngine::process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    if (! prepared)
        return;

    installPendingIfAny();

    const int chans = juce::jmin (buffer.getNumChannels(), numChannels);

    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - pos);

        for (int ch = 0; ch < chans; ++ch)
            inCopy.copyFrom (ch, 0, buffer, ch, pos, n);

        auto runConv = [&] (int convIndex, int srcChannel, int dstChannel, bool add)
        {
            juce::dsp::AudioBlock<const float> in (inCopy.getArrayOfReadPointers() + srcChannel, 1, (size_t) n);
            juce::dsp::AudioBlock<float> out (scratch.getArrayOfWritePointers(), 1, (size_t) n);
            juce::dsp::ProcessContextNonReplacing<float> ctx (in, out);
            convs[(size_t) convIndex]->process (ctx);

            if (add) buffer.addFrom  (dstChannel, pos, scratch, 0, 0, n);
            else     buffer.copyFrom (dstChannel, pos, scratch, 0, 0, n);
        };

        if (chans == 1)
        {
            runConv (0, 0, 0, false);
        }
        else
        {
            runConv (0, 0, 0, false); // L -> L
            runConv (1, 1, 0, true);  // R -> L
            runConv (2, 0, 1, false); // L -> R
            runConv (3, 1, 1, true);  // R -> R
        }
    }
}

//==============================================================================
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

void LinearPhaseEngine::run()
{
    BandSnapshot last {};
    int lastLength = -1;
    double lastRate = 0.0;
    int lastChannels = 0;
    BandSnapshot snap {};

    while (! threadShouldExit())
    {
        if (active.load() && prepared.load())
        {
            int quality = 1;
            provider (snap, quality);
            const double sr = designSampleRate.load();
            const int chans = designChannels.load();
            const int n = firLengthFor (quality, sr);

            const bool changed = forceRedesign.exchange (false) || n != lastLength || ! juce::exactlyEqual (sr, lastRate)
                              || chans != lastChannels || snap != last;
            if (changed)
            {
                std::array<juce::AudioBuffer<float>, 4> irs;
                designIRs (snap, sr, chans, n, irs);

                {
                    const juce::SpinLock::ScopedLockType lock (pendingLock);
                    for (int i = 0; i < 4; ++i)
                        pending[(size_t) i] = std::move (irs[(size_t) i]);
                    pendingLength = n;
                    hasPending = true;
                }
                // whatever is left in irs (moved-from or previously pending) is freed here, off the audio thread
                last = snap; lastLength = n; lastRate = sr; lastChannels = chans;
                ++designsCompleted;
            }
        }
        wait (active.load() ? 25 : 200);
    }
}
} // namespace tessera
