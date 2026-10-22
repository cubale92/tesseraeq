#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace tessera
{
TesseraProcessor::TesseraProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TesseraEQ", createParameterLayout()),
      linear ([this] (BandSnapshot& snap, int& quality)
              {
                  snapshotBands (snap);
                  quality = (int) qualityParam->load();
              })
{
    for (int b = 0; b < numBands; ++b)
    {
        auto& p = bandParams[(size_t) b];
        auto get = [this, b] (const char* name) { return apvts.getRawParameterValue (pid::band (b, name)); };
        p.on = get (pid::on);         p.type = get (pid::type);       p.freq = get (pid::freq);
        p.gain = get (pid::gain);     p.q = get (pid::q);             p.slope = get (pid::slope);
        p.channel = get (pid::channel); p.dyn = get (pid::dyn);
        p.thresh = get (pid::thresh); p.range = get (pid::range);
        dynDeltaDisplay[(size_t) b] = 0.0f;
    }
    for (auto& pk : peaks)
        pk = 0.0f;

    phaseParam   = apvts.getRawParameterValue (pid::phase);
    qualityParam = apvts.getRawParameterValue (pid::firQuality);
    outputParam  = apvts.getRawParameterValue (pid::output);
    attackParam  = apvts.getRawParameterValue (pid::attack);
    releaseParam = apvts.getRawParameterValue (pid::release);
}

TesseraProcessor::~TesseraProcessor() = default;

bool TesseraProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void TesseraProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int capacity = juce::jmax (samplesPerBlock, 2048);
    const int chans = juce::jlimit (1, 2, getTotalNumOutputChannels());

    inputCopy.setSize (2, capacity, false, true, true);
    detDelay.setSize (2, 262144, false, true, true);
    detDelayWrite = 0;
    scratchA.assign ((size_t) subBlock, 0.0f);
    scratchB.assign ((size_t) subBlock, 0.0f);
    analyserScratch.assign ((size_t) capacity, 0.0f);

    for (auto& rt : runtime)
    {
        rt.clearState();
        rt.initialised = false;
        rt.appliedType = rt.detType = rt.dynAppliedType = -1;
    }

    linear.prepare (sampleRate, capacity, chans);
    linear.reset();

    outputGain.reset (sampleRate, 0.03);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    lastPhaseMode = -1;
    lastReportedLatency = -1;
}

void TesseraProcessor::snapshotBands (BandSnapshot& out) const noexcept
{
    for (int b = 0; b < numBands; ++b)
    {
        const auto& p = bandParams[(size_t) b];
        auto& s = out[(size_t) b];
        s.on = p.on->load() > 0.5f;
        s.channel = (int) p.channel->load();
        s.settings.type = (int) p.type->load();
        s.settings.freq = p.freq->load();
        s.settings.gainDb = p.gain->load();
        s.settings.q = p.q->load();
        s.settings.slope = (int) p.slope->load();
    }
}

//==============================================================================
void TesseraProcessor::runSections (const BandCoeffs& c, double (*state)[2], float* data, int num) noexcept
{
    for (int s = 0; s < c.numSections; ++s)
    {
        const auto& q = c.sections[s];
        double z1 = state[s][0], z2 = state[s][1];
        for (int i = 0; i < num; ++i)
        {
            const double x = data[i];
            const double y = q.b0 * x + z1;
            z1 = q.b1 * x - q.a1 * y + z2;
            z2 = q.b2 * x - q.a2 * y;
            data[i] = (float) y;
        }
        state[s][0] = z1;
        state[s][1] = z2;
    }
}

float TesseraProcessor::runDetector (const BandCoeffs& c, double* state, float x) noexcept
{
    if (c.numSections == 0)
        return x;
    const auto& q = c.sections[0];
    const double y = q.b0 * x + state[0];
    state[0] = q.b1 * x - q.a1 * y + state[1];
    state[1] = q.b2 * x - q.a2 * y;
    return (float) y;
}

void TesseraProcessor::applyToChannels (const BandCoeffs& c, double (*z)[BandCoeffs::maxSections][2], int mode,
                                        float* const* chans, int numChans, int start, int num) noexcept
{
    if (numChans == 1)
    {
        if (mode == Stereo || mode == Left || mode == Mid)
            runSections (c, z[0], chans[0] + start, num);
        return;
    }

    float* L = chans[0] + start;
    float* R = chans[1] + start;

    switch (mode)
    {
        case Left:  runSections (c, z[0], L, num); break;
        case Right: runSections (c, z[1], R, num); break;
        case Mid:
        case Side:
        {
            float* m = scratchA.data();
            float* s = scratchB.data();
            for (int i = 0; i < num; ++i) { m[i] = 0.5f * (L[i] + R[i]); s[i] = 0.5f * (L[i] - R[i]); }
            runSections (c, z[0], mode == Mid ? m : s, num);
            for (int i = 0; i < num; ++i) { L[i] = m[i] + s[i]; R[i] = m[i] - s[i]; }
            break;
        }
        default:
            runSections (c, z[0], L, num);
            runSections (c, z[1], R, num);
            break;
    }
}

float TesseraProcessor::detectBand (int b, BandRuntime& rt, const float* const* in, int numChans, int start, int num,
                                    float attackCoeff, float releaseCoeff) noexcept
{
    const auto& p = bandParams[(size_t) b];
    const int type = (int) p.type->load();
    const int mode = (int) p.channel->load();

    if (type != rt.detType || std::abs (rt.freq - rt.detFreq) > 1.0e-3f * rt.freq || std::abs (rt.q - rt.detQ) > 1.0e-3f)
    {
        BandSettings s; s.type = type; s.freq = rt.freq; s.q = rt.q;
        rt.detCoeffs = designDetector (s, getCurrentSampleRate());
        if (type != rt.detType)
            std::memset (rt.detZ, 0, sizeof (rt.detZ));
        rt.detType = type; rt.detFreq = rt.freq; rt.detQ = rt.q;
    }

    float env = rt.env;
    for (int i = 0; i < num; ++i)
    {
        const float l = in[0][start + i];
        const float r = numChans > 1 ? in[1][start + i] : l;
        float level;
        switch (numChans > 1 ? mode : Left)
        {
            case Left:  level = std::abs (runDetector (rt.detCoeffs, rt.detZ[0], l)); break;
            case Right: level = std::abs (runDetector (rt.detCoeffs, rt.detZ[1], r)); break;
            case Mid:   level = std::abs (runDetector (rt.detCoeffs, rt.detZ[0], 0.5f * (l + r))); break;
            case Side:  level = std::abs (runDetector (rt.detCoeffs, rt.detZ[0], 0.5f * (l - r))); break;
            default:    level = juce::jmax (std::abs (runDetector (rt.detCoeffs, rt.detZ[0], l)),
                                            std::abs (runDetector (rt.detCoeffs, rt.detZ[1], r))); break;
        }
        const float coeff = level > env ? attackCoeff : releaseCoeff;
        env = level + coeff * (env - level);
    }
    rt.env = env;

    const float envDb = juce::Decibels::gainToDecibels (env, -120.0f);
    const float over = envDb - p.thresh->load();
    const float range = p.range->load();
    float delta = 0.0f;
    if (over > 0.0f)
        delta = range < 0.0f ? juce::jmax (range, -over) : juce::jmin (range, over);
    rt.delta = delta;
    return delta;
}

//==============================================================================
void TesseraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalIn = getTotalNumInputChannels(), totalOut = getTotalNumOutputChannels();
    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const int numChans = juce::jlimit (1, 2, juce::jmin (totalIn, buffer.getNumChannels()));
    const int numSamples = buffer.getNumSamples();
    const double sr = getCurrentSampleRate();

    const int phase = (int) phaseParam->load();
    if (phase != lastPhaseMode)
    {
        for (auto& rt : runtime)
        {
            rt.clearState();
            rt.appliedType = rt.detType = rt.dynAppliedType = -1;
        }
        linear.setActive (phase == LinearPhase);
        linear.reset();
        detDelay.clear();
        detDelayWrite = 0;
        lastPhaseMode = phase;
    }

    const int latency = phase == LinearPhase ? firLengthFor ((int) qualityParam->load(), sr) / 2 : 0;
    if (latency != lastReportedLatency)
    {
        setLatencySamples (latency);
        lastReportedLatency = latency;
    }

    const float attackCoeff  = std::exp (-1.0f / (float) (attackParam->load()  * 0.001 * sr));
    const float releaseCoeff = std::exp (-1.0f / (float) (releaseParam->load() * 0.001 * sr));
    const float smoothK = 1.0f - std::exp (-(float) subBlock / (float) (0.02 * sr)); // ~20 ms parameter glide

    const int capacity = inputCopy.getNumSamples();

    for (int chunkStart = 0; chunkStart < numSamples; chunkStart += capacity)
    {
        const int chunk = juce::jmin (capacity, numSamples - chunkStart);
        float* chans[2] { buffer.getWritePointer (0, chunkStart),
                          numChans > 1 ? buffer.getWritePointer (1, chunkStart) : nullptr };

        // --- analyser (pre) ------------------------------------------------------
        for (int i = 0; i < chunk; ++i)
            analyserScratch[(size_t) i] = numChans > 1 ? 0.5f * (chans[0][i] + chans[1][i]) : chans[0][i];
        preFifo.push (analyserScratch.data(), chunk);

        // --- detector input ------------------------------------------------------
        if (phase == LinearPhase)
        {
            // Delay the detector feed by the FIR latency so it lines up with the processed signal.
            const int dlen = detDelay.getNumSamples();
            const int d = juce::jmin (latency, dlen - 1);
            for (int ch = 0; ch < numChans; ++ch)
            {
                auto* ring = detDelay.getWritePointer (ch);
                auto* dst = inputCopy.getWritePointer (ch);
                int w = detDelayWrite;
                for (int i = 0; i < chunk; ++i)
                {
                    ring[w] = chans[ch][i];
                    int r = w - d; if (r < 0) r += dlen;
                    dst[i] = ring[r];
                    if (++w == dlen) w = 0;
                }
            }
            detDelayWrite = (detDelayWrite + chunk) % dlen;
        }
        else
        {
            for (int ch = 0; ch < numChans; ++ch)
                inputCopy.copyFrom (ch, 0, chans[ch], chunk);
        }

        const float* detIn[2] { inputCopy.getReadPointer (0), inputCopy.getReadPointer (numChans > 1 ? 1 : 0) };

        // --- static EQ (linear-phase FIR) ---------------------------------------
        if (phase == LinearPhase)
        {
            juce::AudioBuffer<float> view (chans, numChans, chunk);
            linear.process (view, chunk);
        }

        // --- per-band IIR / dynamic processing in sub-blocks ---------------------
        for (int start = 0; start < chunk; start += subBlock)
        {
            const int num = juce::jmin (subBlock, chunk - start);

            for (int b = 0; b < numBands; ++b)
            {
                auto& p = bandParams[(size_t) b];
                auto& rt = runtime[(size_t) b];

                if (p.on->load() < 0.5f)
                {
                    if (rt.wasOn) { rt.clearState(); rt.wasOn = false; rt.initialised = false; }
                    dynDeltaDisplay[(size_t) b].store (0.0f, std::memory_order_relaxed);
                    continue;
                }
                if (! rt.wasOn) { rt.clearState(); rt.wasOn = true; rt.appliedType = -1; rt.dynAppliedType = -1; }

                const int type  = (int) p.type->load();
                const int slope = (int) p.slope->load();
                const int mode  = (int) p.channel->load();
                const float tf = p.freq->load(), tg = p.gain->load(), tq = p.q->load();

                if (! rt.initialised)
                {
                    rt.freq = tf; rt.gain = tg; rt.q = tq; rt.initialised = true;
                }
                else
                {
                    rt.freq = std::exp (std::log (rt.freq) + smoothK * (std::log (tf) - std::log (rt.freq)));
                    rt.q    = std::exp (std::log (rt.q)    + smoothK * (std::log (tq) - std::log (rt.q)));
                    rt.gain += smoothK * (tg - rt.gain);
                    if (std::abs (rt.freq - tf) < 1.0e-4f * tf) rt.freq = tf;
                    if (std::abs (rt.q - tq) < 1.0e-4f * tq)    rt.q = tq;
                    if (std::abs (rt.gain - tg) < 1.0e-4f)      rt.gain = tg;
                }

                const bool dynamic = p.dyn->load() > 0.5f && typeHasGain (type);
                float delta = 0.0f;
                if (dynamic)
                    delta = detectBand (b, rt, detIn, numChans, start, num, attackCoeff, releaseCoeff);
                else if (! juce::exactlyEqual (rt.env, 0.0f) || ! juce::exactlyEqual (rt.delta, 0.0f))
                {
                    rt.env = 0.0f; rt.delta = 0.0f;
                    std::memset (rt.dz, 0, sizeof (rt.dz));
                }
                dynDeltaDisplay[(size_t) b].store (delta, std::memory_order_relaxed);

                if (phase == MinimumPhase)
                {
                    const float effGain = rt.gain + delta;
                    if (type != rt.appliedType || slope != rt.appliedSlope
                        || std::abs (rt.freq - rt.appliedFreq) > 1.0e-5f * rt.freq
                        || std::abs (effGain - rt.appliedGain) > 1.0e-3f
                        || std::abs (rt.q - rt.appliedQ) > 1.0e-5f * rt.q)
                    {
                        if (type != rt.appliedType || slope != rt.appliedSlope)
                            std::memset (rt.z, 0, sizeof (rt.z));
                        BandSettings s { type, rt.freq, effGain, rt.q, slope };
                        rt.coeffs = designBand (s, sr);
                        rt.appliedType = type; rt.appliedSlope = slope;
                        rt.appliedFreq = rt.freq; rt.appliedGain = effGain; rt.appliedQ = rt.q;
                    }
                    applyToChannels (rt.coeffs, rt.z, mode, chans, numChans, start, num);
                }
                else if (dynamic)
                {
                    // Linear-phase mode: static part lives in the FIR, the dynamic offset is
                    // applied by an extra minimum-phase filter of the same shape.
                    if (type != rt.dynAppliedType || std::abs (delta - rt.dynAppliedDelta) > 1.0e-3f
                        || std::abs (rt.freq - rt.dynAppliedFreq) > 1.0e-5f * rt.freq
                        || std::abs (rt.q - rt.dynAppliedQ) > 1.0e-5f * rt.q)
                    {
                        if (type != rt.dynAppliedType)
                            std::memset (rt.dz, 0, sizeof (rt.dz));
                        BandSettings s { type, rt.freq, delta, rt.q, slope };
                        if (type == Tilt) s.gainDb = delta; // tilt offset keeps its pivot
                        rt.dynCoeffs = designBand (s, sr);
                        rt.dynAppliedType = type; rt.dynAppliedDelta = delta;
                        rt.dynAppliedFreq = rt.freq; rt.dynAppliedQ = rt.q;
                    }
                    applyToChannels (rt.dynCoeffs, rt.dz, mode, chans, numChans, start, num);
                }
            }
        }

        // --- output gain, meters, analyser (post) ---------------------------------
        outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
        if (outputGain.isSmoothing() || std::abs (outputGain.getCurrentValue() - 1.0f) > 1.0e-6f)
        {
            for (int i = 0; i < chunk; ++i)
            {
                const float g = outputGain.getNextValue();
                for (int ch = 0; ch < numChans; ++ch)
                    chans[ch][i] *= g;
            }
        }

        float pk[4] {};
        for (int i = 0; i < chunk; ++i)
        {
            const float l = chans[0][i];
            const float r = numChans > 1 ? chans[1][i] : l;
            pk[0] = juce::jmax (pk[0], std::abs (l));
            pk[1] = juce::jmax (pk[1], std::abs (r));
            pk[2] = juce::jmax (pk[2], std::abs (0.5f * (l + r)));
            pk[3] = juce::jmax (pk[3], std::abs (0.5f * (l - r)));
            analyserScratch[(size_t) i] = 0.5f * (l + r);
        }
        for (int k = 0; k < 4; ++k)
            if (pk[k] > peaks[(size_t) k].load (std::memory_order_relaxed))
                peaks[(size_t) k].store (pk[k], std::memory_order_relaxed);

        postFifo.push (analyserScratch.data(), chunk);
    }
}

//==============================================================================
void TesseraProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TesseraProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* TesseraProcessor::createEditor()
{
    return new TesseraEditor (*this);
}
} // namespace tessera

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tessera::TesseraProcessor();
}
