// Offline checks for Tessera EQ: filter maths, min/linear-phase processing, M/S routing,
// dynamic EQ, latency, and an editor screenshot (pass an output PNG path as argv[1]).

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace tessera;

static int failures = 0;

static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static void setP (TesseraProcessor& p, const juce::String& id, float v)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (v));
}

static void resetBands (TesseraProcessor& p)
{
    for (int b = 0; b < numBands; ++b)
    {
        setP (p, pid::band (b, pid::on), 0.0f);
        setP (p, pid::band (b, pid::dyn), 0.0f);
        setP (p, pid::band (b, pid::channel), 0.0f);
    }
}

static void setBand (TesseraProcessor& p, int b, int type, float f, float g, float q, int ch = Stereo, int slope = 3)
{
    setP (p, pid::band (b, pid::type), (float) type);
    setP (p, pid::band (b, pid::freq), f);
    setP (p, pid::band (b, pid::gain), g);
    setP (p, pid::band (b, pid::q), q);
    setP (p, pid::band (b, pid::slope), (float) slope);
    setP (p, pid::band (b, pid::channel), (float) ch);
    setP (p, pid::band (b, pid::on), 1.0f);
}

/** Runs a sine through the processor and returns the output RMS gain (dB) per channel,
    measured on the last 0.25 s. inL/inR amplitude factors allow M/S tests. */
static std::pair<float, float> measure (TesseraProcessor& p, float freq, float ampDb, float lAmp = 1.0f, float rAmp = 1.0f,
                                        double seconds = 1.0)
{
    const double sr = 48000.0;
    const int block = 512;
    const int total = (int) (seconds * sr);
    const int measureFrom = total - (int) (0.25 * sr);
    const float amp = juce::Decibels::decibelsToGain (ampDb);

    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double phase = 0.0, inSum = 0.0, outL = 0.0, outR = 0.0;
    int counted = 0;

    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const float s = amp * (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * freq / sr;
            buf.setSample (0, i, s * lAmp);
            buf.setSample (1, i, s * rAmp);
            if (pos + i >= measureFrom) inSum += (double) s * s;
        }
        p.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
            if (pos + i >= measureFrom)
            {
                outL += (double) buf.getSample (0, i) * buf.getSample (0, i);
                outR += (double) buf.getSample (1, i) * buf.getSample (1, i);
                ++counted;
            }
    }
    juce::ignoreUnused (counted);
    auto toDb = [&] (double o) { return (float) (10.0 * std::log10 (juce::jmax (1.0e-20, o) / juce::jmax (1.0e-20, inSum))); };
    return { toDb (outL), toDb (outR) };
}

static bool near (float a, float b, float tol) { return std::abs (a - b) <= tol; }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    std::cout << "Filter design" << std::endl;
    {
        const double sr = 48000.0;
        BandSettings s; s.type = Bell; s.freq = 1000.0f; s.gainDb = 6.0f; s.q = 1.0f;
        auto c = designBand (s, sr);
        check (near ((float) juce::Decibels::gainToDecibels (c.magnitude (1000.0, sr)), 6.0f, 0.01f), "bell +6 dB at centre");
        check (near ((float) juce::Decibels::gainToDecibels (c.magnitude (20.0, sr)), 0.0f, 0.1f), "bell flat far away");

        s.type = LowCut; s.freq = 100.0f; s.q = 0.7071f;
        for (int slope = 0; slope < 6; ++slope)
        {
            s.slope = slope;
            auto lc = designBand (s, sr);
            const float atF = (float) juce::Decibels::gainToDecibels (lc.magnitude (100.0, sr));
            const float octBelow = (float) juce::Decibels::gainToDecibels (lc.magnitude (25.0, sr));
            const float expected = -6.02f * slopeOrder (slope) * 2.0f;
            check (near (atF, -3.01f, 0.1f) && near (octBelow, expected, 1.0f),
                   "low cut " + slopeNames()[slope] + ": -3 dB at fc (" + juce::String (atF, 2) + "), 2 oct below "
                   + juce::String (octBelow, 1) + " dB");
        }

        s.type = Tilt; s.freq = 1000.0f; s.gainDb = 6.0f; s.q = 1.0f;
        auto t = designBand (s, sr);
        check (near ((float) juce::Decibels::gainToDecibels (t.magnitude (20.0, sr)), -3.0f, 0.2f)
            && near ((float) juce::Decibels::gainToDecibels (t.magnitude (20000.0, sr)), 3.0f, 0.3f), "tilt pivots +/-3 dB");
    }

    std::cout << "Minimum-phase processing" << std::endl;
    TesseraProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    {
        resetBands (proc);
        setBand (proc, 0, Bell, 1000.0f, 6.0f, 1.0f);
        auto [l, r] = measure (proc, 1000.0f, -20.0f);
        check (near (l, 6.0f, 0.1f) && near (r, 6.0f, 0.1f), "stereo bell +6 dB -> " + juce::String (l, 2) + " / " + juce::String (r, 2));

        auto [l2, r2] = measure (proc, 100.0f, -20.0f);
        check (near (l2, 0.0f, 0.2f), "bell leaves 100 Hz alone -> " + juce::String (l2, 2));

        // Side-only band: correlated (mono) material should pass untouched, anti-phase gets boosted
        resetBands (proc);
        setBand (proc, 0, Bell, 1000.0f, 6.0f, 1.0f, Side);
        auto [ml, mr] = measure (proc, 1000.0f, -20.0f, 1.0f, 1.0f);
        auto [sl, sr_] = measure (proc, 1000.0f, -20.0f, 1.0f, -1.0f);
        check (near (ml, 0.0f, 0.1f) && near (sl, 6.0f, 0.1f) && near (sr_, 6.0f, 0.1f),
               "side band: mid " + juce::String (ml, 2) + " dB, side " + juce::String (sl, 2) + " dB");

        resetBands (proc);
        setBand (proc, 0, Bell, 1000.0f, -9.0f, 1.0f, Left);
        auto [ll, lr] = measure (proc, 1000.0f, -20.0f);
        check (near (ll, -9.0f, 0.1f) && near (lr, 0.0f, 0.1f), "left-only band: L " + juce::String (ll, 2) + " R " + juce::String (lr, 2));

        resetBands (proc);
        setBand (proc, 0, LowCut, 200.0f, 0.0f, 0.7071f, Stereo, 3);
        auto [cl, cr] = measure (proc, 50.0f, -20.0f, 1.0f, 1.0f, 1.5);
        check (cl < -45.0f, "24 dB/oct low cut, 2 oct below -> " + juce::String (cl, 1) + " dB");
        juce::ignoreUnused (cr);
    }

    std::cout << "Dynamic EQ" << std::endl;
    {
        resetBands (proc);
        setP (proc, pid::attack, 2.0f);
        setP (proc, pid::release, 50.0f);
        setBand (proc, 0, Bell, 1000.0f, 0.0f, 1.0f);
        setP (proc, pid::band (0, pid::dyn), 1.0f);
        setP (proc, pid::band (0, pid::thresh), -30.0f);
        setP (proc, pid::band (0, pid::range), -6.0f);

        auto [loud, lr] = measure (proc, 1000.0f, -10.0f);
        check (near (loud, -6.0f, 0.5f), "loud signal over threshold gets full -6 dB range -> " + juce::String (loud, 2));
        auto [quiet, qr] = measure (proc, 1000.0f, -45.0f);
        check (near (quiet, 0.0f, 0.2f), "quiet signal under threshold untouched -> " + juce::String (quiet, 2));
        auto [mid, mr] = measure (proc, 1000.0f, -27.0f);
        check (near (mid, -3.0f, 0.8f), "3 dB over threshold -> about -3 dB -> " + juce::String (mid, 2));
        juce::ignoreUnused (lr, qr, mr);
        setP (proc, pid::band (0, pid::dyn), 0.0f);
    }

    std::cout << "Linear-phase processing" << std::endl;
    {
        // check the IR designer directly: flat EQ must give a centred unit impulse
        BandSnapshot flat {};
        std::array<juce::AudioBuffer<float>, 4> irs;
        LinearPhaseEngine::designIRs (flat, 48000.0, 2, 8192, irs);
        const float centre = irs[0].getSample (0, 4096);
        float maxOther = 0.0f;
        for (int i = 0; i < 8192; ++i)
            if (i != 4096) maxOther = juce::jmax (maxOther, std::abs (irs[0].getSample (0, i)));
        check (near (centre, 1.0f, 1.0e-3f) && maxOther < 1.0e-4f && irs[1].getMagnitude (0, 0, 8192) < 1.0e-5f,
               "flat design = unit impulse at n/2 (peak " + juce::String (centre, 4) + ")");

        resetBands (proc);
        setP (proc, pid::firQuality, 1.0f);
        setP (proc, pid::phase, (float) LinearPhase);
        setBand (proc, 0, Bell, 1000.0f, 6.0f, 1.0f);
        setBand (proc, 1, HighShelf, 8000.0f, -4.0f, 0.7f);
        setBand (proc, 2, Bell, 300.0f, 3.0f, 2.0f, Mid);

        juce::AudioBuffer<float> warm (2, 512); juce::MidiBuffer midi;
        warm.clear();
        for (int k = 0; k < 6; ++k) proc.processBlock (warm, midi); // fade out, switch, activate engine
        check (proc.waitForLinearPhaseReady (5000), "linear-phase filters designed");
        juce::Thread::sleep (100);
        check (proc.getLatencySamples() == 4096 + 512, "latency = " + juce::String (proc.getLatencySamples()) + " samples at 48 kHz / Medium");

        BandSnapshot snap; proc.snapshotBands (snap);
        for (float f : { 100.0f, 300.0f, 1000.0f, 3000.0f, 8000.0f, 15000.0f })
        {
            const float expected = (float) juce::Decibels::gainToDecibels (responseMatrixAt (snap, f, 48000.0).midGain());
            auto [l, r] = measure (proc, f, -20.0f, 1.0f, 1.0f, 1.2);
            check (near (l, expected, 0.15f) && near (r, expected, 0.15f),
                   "linear @" + juce::String (f) + " Hz: " + juce::String (l, 2) + " dB (expect " + juce::String (expected, 2) + ")");
        }

        // linear phase: the impulse response must be symmetric around the latency
        resetBands (proc);
        setBand (proc, 0, Bell, 2000.0f, 9.0f, 3.0f);
        juce::Thread::sleep (200);
        juce::AudioBuffer<float> b (2, 512);
        for (int k = 0; k < 40; ++k) { b.clear(); proc.processBlock (b, midi); } // flush crossfades
        const int lat = proc.getLatencySamples();
        std::vector<float> resp;
        for (int k = 0; k < 20; ++k)
        {
            b.clear();
            if (k == 0) { b.setSample (0, 0, 1.0f); b.setSample (1, 0, 1.0f); }
            proc.processBlock (b, midi);
            for (int i = 0; i < 512; ++i) resp.push_back (b.getSample (0, i));
        }
        float asym = 0.0f, peak = 0.0f;
        for (int i = 1; i < 2000; ++i)
        {
            asym = juce::jmax (asym, std::abs (resp[(size_t) (lat + i)] - resp[(size_t) (lat - i)]));
            peak = juce::jmax (peak, std::abs (resp[(size_t) (lat + i)]));
        }
        check (asym < 1.0e-3f * juce::jmax (peak, 1.0e-3f) + 1.0e-5f,
               "impulse response symmetric around latency (max asym " + juce::String (asym, 6) + ")");

        // dynamic band in linear mode
        resetBands (proc);
        setBand (proc, 0, Bell, 1000.0f, 0.0f, 1.0f);
        setP (proc, pid::band (0, pid::dyn), 1.0f);
        setP (proc, pid::band (0, pid::thresh), -30.0f);
        setP (proc, pid::band (0, pid::range), -6.0f);
        juce::Thread::sleep (150);
        auto [dl, dr] = measure (proc, 1000.0f, -10.0f, 1.0f, 1.0f, 1.2);
        check (near (dl, -6.0f, 0.6f), "dynamic band in linear mode -> " + juce::String (dl, 2) + " dB");
        juce::ignoreUnused (dr);

        // REGRESSION: editing a band in linear mode must not cause dropouts
        {
            resetBands (proc);
            setBand (proc, 0, Bell, 6000.0f, 6.0f, 1.0f);
            setBand (proc, 1, Bell, 1000.0f, 3.0f, 1.0f);
            juce::Thread::sleep (100);
            measure (proc, 1000.0f, -20.0f, 1.0f, 1.0f, 0.5); // settle
            const float expectedPeak = 0.1f * juce::Decibels::decibelsToGain ((float) juce::Decibels::gainToDecibels (
                                           responseMatrixAt ([&] { BandSnapshot s; proc.snapshotBands (s); return s; }(), 1000.0, 48000.0).a));
            juce::AudioBuffer<float> sb (2, 512);
            double ph = 0.0;
            float worst = 0.0f;
            for (int k = 0; k < 300; ++k)
            {
                if (k % 7 == 0) // drag-like edits: a far band changes constantly, the 1 kHz band slightly
                {
                    setP (proc, pid::band (0, pid::gain), (k % 14 == 0) ? 6.0f : -4.0f);
                    setP (proc, pid::band (0, pid::freq), 5000.0f + (float) (k * 13 % 3000));
                }
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.1f * (float) std::sin (ph);
                    ph += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;
                    sb.setSample (0, i, v); sb.setSample (1, i, v);
                }
                proc.processBlock (sb, midi);
                if (k > 20)
                    worst = juce::jmax (worst, std::abs (juce::Decibels::gainToDecibels (sb.getMagnitude (0, 0, 512) / expectedPeak)));
                if (k % 7 == 3) juce::Thread::sleep (2); // let the designer keep up, like a real-time host would
            }
            check (worst < 0.6f, "no dropouts while editing bands in linear mode (worst block deviation "
                                    + juce::String (worst, 2) + " dB)");
        }

        // REGRESSION: switching phase modes fades cleanly (no burst, no long gap, correct afterwards)
        {
            resetBands (proc);
            setBand (proc, 0, Bell, 1000.0f, 6.0f, 1.0f);
            setP (proc, pid::phase, (float) MinimumPhase);
            measure (proc, 1000.0f, -20.0f, 1.0f, 1.0f, 0.5);
            const float steady = 0.2f; // 0.1 * +6 dB
            juce::AudioBuffer<float> sb (2, 512);
            double ph = 0.0;
            float maxPeak = 0.0f;
            int silentBlocks = 0, firstGood = -1;
            for (int k = 0; k < 200; ++k)
            {
                if (k == 10) setP (proc, pid::phase, (float) LinearPhase);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = 0.1f * (float) std::sin (ph);
                    ph += juce::MathConstants<double>::twoPi * 1000.0 / 48000.0;
                    sb.setSample (0, i, v); sb.setSample (1, i, v);
                }
                proc.processBlock (sb, midi);
                const float pk = sb.getMagnitude (0, 0, 512);
                maxPeak = juce::jmax (maxPeak, pk);
                if (k > 10 && pk < 0.01f) ++silentBlocks;
                if (k > 12 && firstGood < 0 && near (pk, steady, 0.004f)) firstGood = k;
                if (k == 6) juce::Thread::sleep (1);
                if (k > 10 && k < 40) juce::Thread::sleep (3); // real time-ish so the designer runs
            }
            const double gapMs = silentBlocks * 512.0 / 48.0;
            check (maxPeak < steady * 1.03f, "mode switch: no level burst (max peak " + juce::String (maxPeak, 3) + ")");
            check (firstGood > 0 && gapMs < 600.0, "mode switch: back to correct output after "
                                                        + juce::String (gapMs, 0) + " ms of fade/mute");

            // bypass keeps timing: an impulse comes out exactly at the reported latency
            const int bypassLatency = proc.getLatencySamples();
            juce::AudioBuffer<float> ib (2, 512);
            int foundAt = -1;
            for (int k = 0; k < 20 && foundAt < 0; ++k)
            {
                ib.clear();
                if (k == 0) { ib.setSample (0, 0, 1.0f); ib.setSample (1, 0, 1.0f); }
                proc.processBlockBypassed (ib, midi);
                for (int i = 0; i < 512; ++i)
                    if (ib.getSample (0, i) > 0.5f) { foundAt = k * 512 + i; break; }
            }
            check (foundAt == bypassLatency, "bypass delays by the plugin latency (" + juce::String (foundAt) + " vs " + juce::String (bypassLatency) + ")");
        }

        setP (proc, pid::phase, (float) MinimumPhase);
        for (int k = 0; k < 6; ++k) { warm.clear(); proc.processBlock (warm, midi); }
        check (proc.getLatencySamples() == 0, "back to minimum phase: zero latency");
    }

    std::cout << "Factory presets" << std::endl;
    {
        const auto& list = factoryPresets();
        check (proc.getNumPrograms() == (int) list.size() && list.size() >= 30,
               juce::String ((int) list.size()) + " presets, exposed to the host as programs");
        int bad = 0;
        juce::String problems;
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& pr = list[(size_t) i];
            if (pr.bands.size() > (size_t) numBands) { ++bad; problems << pr.name << " (too many bands) "; continue; }
            proc.setCurrentProgram (i);
            // every value must survive the parameter ranges exactly (no silent clamping)
            for (int b = 0; b < (int) pr.bands.size(); ++b)
            {
                const auto& pb = pr.bands[(size_t) b];
                auto raw = [&] (const char* n) { return proc.apvts.getRawParameterValue (pid::band (b, n))->load(); };
                const bool ok = (int) raw (pid::type) == pb.type && near (raw (pid::freq), pb.freq, pb.freq * 0.001f)
                             && near (raw (pid::gain), pb.gain, 0.011f) && near (raw (pid::q), pb.q, pb.q * 0.001f)
                             && (int) raw (pid::channel) == pb.channel && raw (pid::on) > 0.5f
                             && (raw (pid::dyn) > 0.5f) == pb.dyn;
                if (! ok) { ++bad; problems << pr.name << " band " << (b + 1) << " "; }
            }
            for (int b = (int) pr.bands.size(); b < numBands; ++b)
                if (proc.apvts.getRawParameterValue (pid::band (b, pid::on))->load() > 0.5f) { ++bad; problems << pr.name << " leftover band "; }
            if (proc.getCurrentProgram() != i || proc.getProgramName (i) != juce::String (pr.name)) { ++bad; problems << pr.name << " program index "; }

            // sanity: pink-ish noise through the preset stays finite and within +/-12 dB of the input level
            juce::Random rnd (i + 1);
            juce::AudioBuffer<float> nb (2, 512); juce::MidiBuffer midi;
            double inE = 0.0, outE = 0.0;
            for (int k = 0; k < 60; ++k)
            {
                for (int s = 0; s < 512; ++s) { const float v = (rnd.nextFloat() - 0.5f) * 0.3f; nb.setSample (0, s, v); nb.setSample (1, s, v); }
                if (k >= 20) for (int s = 0; s < 512; ++s) inE += (double) nb.getSample (0, s) * nb.getSample (0, s);
                proc.processBlock (nb, midi);
                for (int s = 0; s < 512; ++s)
                {
                    const float v = nb.getSample (0, s);
                    if (! std::isfinite (v)) { ++bad; problems << pr.name << " NaN "; k = 60; break; }
                    if (k >= 20) outE += (double) v * v;
                }
            }
            const double changeDb = 10.0 * std::log10 (juce::jmax (1.0e-12, outE) / juce::jmax (1.0e-12, inE));
            if (std::abs (changeDb) > 12.0 && juce::String (pr.category) != "Utility" && ! juce::String (pr.name).contains ("Filtered"))
            { ++bad; problems << pr.name << " level " << juce::String (changeDb, 1) << " dB "; }
        }
        check (bad == 0, "every preset loads exactly, processes cleanly" + (bad ? ": " + problems : juce::String()));

        // spot checks that presets do what their names say
        auto indexOf = [&] (const char* name)
        {
            for (int i = 0; i < (int) list.size(); ++i) if (juce::String (list[(size_t) i].name) == name) return i;
            return -1;
        };
        proc.setCurrentProgram (indexOf ("BB Filtered Bass (Low Pass Loop)"));
        auto [fl, fr] = measure (proc, 2000.0f, -20.0f, 1.0f, 1.0f, 0.6);
        check (fl < -40.0f, "BB Filtered Bass removes 2 kHz (" + juce::String (fl, 1) + " dB)");
        juce::ignoreUnused (fr);
        proc.setCurrentProgram (indexOf ("Mono Lows (Side cut 120 Hz)"));
        auto [ml, mr] = measure (proc, 50.0f, -20.0f, 1.0f, 1.0f, 0.6);
        auto [sl, sr2] = measure (proc, 50.0f, -20.0f, 1.0f, -1.0f, 0.6);
        check (near (ml, 0.0f, 0.2f) && sl < -12.0f, "Mono Lows keeps mid lows, cuts side lows (mid " + juce::String (ml, 1)
                                                         + " dB, side " + juce::String (sl, 1) + " dB)");
        juce::ignoreUnused (mr, sr2);
        proc.setCurrentProgram (0);
        bool allOff = true;
        for (int b = 0; b < numBands; ++b) allOff &= proc.apvts.getRawParameterValue (pid::band (b, pid::on))->load() < 0.5f;
        check (allOff, "Init preset clears all bands");
    }

    std::cout << "State" << std::endl;
    {
        resetBands (proc);
        setBand (proc, 5, HighCut, 12000.0f, 0.0f, 0.9f, Stereo, 5);
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        TesseraProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        auto* f = other.apvts.getRawParameterValue (pid::band (5, pid::freq));
        auto* on = other.apvts.getRawParameterValue (pid::band (5, pid::on));
        auto* sl = other.apvts.getRawParameterValue (pid::band (5, pid::slope));
        check (on->load() > 0.5f && near (f->load(), 12000.0f, 1.0f) && (int) sl->load() == 5, "state save/restore");
    }

    std::cout << "Performance (24 bands, stereo, 48 kHz, 512-sample blocks)" << std::endl;
    for (int mode : { (int) MinimumPhase, (int) LinearPhase })
    {
        TesseraProcessor perf;
        perf.setPlayConfigDetails (2, 2, 48000.0, 512);
        perf.prepareToPlay (48000.0, 512);
        for (int b = 0; b < numBands; ++b)
        {
            setBand (perf, b, b % 3 == 0 ? Bell : (b % 3 == 1 ? HighShelf : LowCut), defaultBandFrequency (b), 3.0f, 1.0f,
                     b % 5 == 0 ? Mid : Stereo);
            setP (perf, pid::band (b, pid::dyn), b % 2 == 0 ? 1.0f : 0.0f);
        }
        setP (perf, pid::phase, (float) mode);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        juce::Random rnd (1);
        buf.clear(); perf.processBlock (buf, midi);
        if (mode == LinearPhase) { perf.waitForLinearPhaseReady (5000); juce::Thread::sleep (200); }
        const int blocks = 48000 * 5 / 512;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < blocks; ++k)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    buf.setSample (ch, i, (rnd.nextFloat() - 0.5f) * 0.5f);
            perf.processBlock (buf, midi);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double cpu = 100.0 * ms / 5000.0;
        check (cpu < 25.0, juce::String (mode == LinearPhase ? "linear phase" : "minimum phase") + ": "
                               + juce::String (cpu, 2) + "% of one core");
    }

    std::cout << "Fuzz (random automation, all sample rates)" << std::endl;
    for (double fsr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        TesseraProcessor fz;
        fz.setPlayConfigDetails (2, 2, fsr, 1024);
        fz.prepareToPlay (fsr, 1024);
        juce::Random rnd (7);
        juce::AudioBuffer<float> buf (2, 1024); juce::MidiBuffer midi;
        bool bad = false;
        juce::String culprit;
        for (int iter = 0; iter < 400 && ! bad; ++iter)
        {
            // randomise every parameter (like pluginval's automation test)
            for (auto* prm : fz.getParameters())
                if (rnd.nextFloat() < 0.3f)
                    prm->setValueNotifyingHost (rnd.nextFloat());
            const int n = 1 + rnd.nextInt (1024);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample (ch, i, rnd.nextFloat() * 2.0f - 1.0f);
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            fz.processBlock (view, midi);
            for (int ch = 0; ch < 2 && ! bad; ++ch)
                for (int i = 0; i < n; ++i)
                    if (! std::isfinite (view.getSample (ch, i)) || std::abs (view.getSample (ch, i)) > 1.0e8f)
                    {
                        bad = true;
                        BandSnapshot s; fz.snapshotBands (s);
                        culprit << "iter " << iter << " phase " << fz.apvts.getRawParameterValue (pid::phase)->load() << "\n";
                        for (int b = 0; b < numBands; ++b)
                            if (s[(size_t) b].on)
                                culprit << "   b" << b << " type " << s[(size_t) b].settings.type << " f " << s[(size_t) b].settings.freq
                                        << " g " << s[(size_t) b].settings.gainDb << " q " << s[(size_t) b].settings.q
                                        << " slope " << s[(size_t) b].settings.slope << " ch " << s[(size_t) b].channel
                                        << " dyn " << fz.apvts.getRawParameterValue (pid::band (b, pid::dyn))->load() << "\n";
                        break;
                    }
        }
        check (! bad, "no NaN/blow-up at " + juce::String (fsr) + " Hz" + (bad ? "\n" + culprit : juce::String()));
    }

    // ---------------------------------------------------------------------------
    if (argc > 1)
    {
        std::cout << "Editor snapshot" << std::endl;
        for (int i = 0; i < proc.getNumPrograms(); ++i)
            if (proc.getProgramName (i) == "NJS Master - Shiny 1990") proc.setCurrentProgram (i);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (1280, 720);

        // feed pink-ish noise so the analyser and meters have something to show
        juce::Random rnd (42);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        float b0 = 0, b1 = 0, b2 = 0;
        for (int frame = 0; frame < 40; ++frame)
        {
            for (int k = 0; k < 3; ++k)
            {
                for (int i = 0; i < 512; ++i)
                {
                    const float w = rnd.nextFloat() * 2.0f - 1.0f;
                    b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                    const float pink = (b0 + b1 + b2 + w * 0.1848f) * 0.12f;
                    buf.setSample (0, i, pink);
                    buf.setSample (1, i, pink * 0.8f + 0.2f * (rnd.nextFloat() * 2.0f - 1.0f) * 0.1f);
                }
                proc.processBlock (buf, midi);
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
        }

        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const juce::File out { juce::String (argv[1]) };
        (void) out.deleteFile();
        juce::FileOutputStream os { out };
        juce::PNGImageFormat png;
        check (png.writeImageToStream (img, os), "wrote " + out.getFullPathName());
        ed.reset();
    }

    std::cout << (failures == 0 ? "\nALL TESTS PASSED" : "\nFAILURES: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
