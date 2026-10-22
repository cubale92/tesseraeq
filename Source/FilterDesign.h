#pragma once

#include "Parameters.h"
#include <cmath>
#include <complex>

namespace tessera
{
/** One second-order (or first-order, with b2 = a2 = 0) section, a0 normalised to 1. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /** |H(e^jw)|^2 at normalised angular frequency w (radians/sample). */
    double magnitudeSquared (double w) const noexcept
    {
        const double c1 = std::cos (w), c2 = std::cos (2.0 * w);
        const double num = b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * c1 + 2.0 * b0 * b2 * c2;
        const double den = 1.0 + a1 * a1 + a2 * a2 + 2.0 * (a1 + a1 * a2) * c1 + 2.0 * a2 * c2;
        return juce::jmax (0.0, num) / juce::jmax (den, 1.0e-30); // num can dip below 0 by rounding at a zero
    }
};

/** A band's full filter: up to 4 cascaded sections (48 dB/oct). */
struct BandCoeffs
{
    static constexpr int maxSections = 4;
    Biquad sections[maxSections];
    int numSections = 0;

    double magnitude (double freqHz, double sampleRate) const noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        double m2 = 1.0;
        for (int i = 0; i < numSections; ++i)
            m2 *= sections[i].magnitudeSquared (w);
        return std::sqrt (m2);
    }
};

struct BandSettings
{
    int type = Bell;
    float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
    int slope = 1;
};

namespace design
{
    inline double clampFreq (double f, double sr) { return juce::jlimit (5.0, sr * 0.49, f); }

    // RBJ audio-EQ-cookbook designs, bilinear transform with pre-warping.
    inline Biquad peaking (double f, double gainDb, double q, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * clampFreq (f, sr) / sr;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cw = std::cos (w0);
        const double a0 = 1.0 + alpha / A;
        return { (1.0 + alpha * A) / a0, (-2.0 * cw) / a0, (1.0 - alpha * A) / a0,
                 (-2.0 * cw) / a0, (1.0 - alpha / A) / a0 };
    }

    inline Biquad lowShelf (double f, double gainDb, double q, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * clampFreq (f, sr) / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double sA = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) + (A - 1.0) * cw + sA;
        return { A * ((A + 1.0) - (A - 1.0) * cw + sA) / a0,
                 2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0,
                 A * ((A + 1.0) - (A - 1.0) * cw - sA) / a0,
                 -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0,
                 ((A + 1.0) + (A - 1.0) * cw - sA) / a0 };
    }

    inline Biquad highShelf (double f, double gainDb, double q, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * clampFreq (f, sr) / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double sA = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1.0) - (A - 1.0) * cw + sA;
        return { A * ((A + 1.0) + (A - 1.0) * cw + sA) / a0,
                 -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0,
                 A * ((A + 1.0) + (A - 1.0) * cw - sA) / a0,
                 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0,
                 ((A + 1.0) - (A - 1.0) * cw - sA) / a0 };
    }

    inline Biquad lowPass (double f, double q, double sr)
    {
        const double w0 = juce::MathConstants<double>::twoPi * clampFreq (f, sr) / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        return { (1.0 - cw) * 0.5 / a0, (1.0 - cw) / a0, (1.0 - cw) * 0.5 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    inline Biquad highPass (double f, double q, double sr)
    {
        const double w0 = juce::MathConstants<double>::twoPi * clampFreq (f, sr) / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        return { (1.0 + cw) * 0.5 / a0, -(1.0 + cw) / a0, (1.0 + cw) * 0.5 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    inline Biquad bandPass (double f, double q, double sr) // 0 dB peak gain
    {
        const double w0 = juce::MathConstants<double>::twoPi * clampFreq (f, sr) / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        return { alpha / a0, 0.0, -alpha / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    inline Biquad notch (double f, double q, double sr)
    {
        const double w0 = juce::MathConstants<double>::twoPi * clampFreq (f, sr) / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        return { 1.0 / a0, -2.0 * cw / a0, 1.0 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
    }

    inline Biquad firstOrderLowPass (double f, double sr)
    {
        const double K = std::tan (juce::MathConstants<double>::pi * clampFreq (f, sr) / sr);
        const double b0 = K / (1.0 + K);
        return { b0, b0, 0.0, (K - 1.0) / (K + 1.0), 0.0 };
    }

    inline Biquad firstOrderHighPass (double f, double sr)
    {
        const double K = std::tan (juce::MathConstants<double>::pi * clampFreq (f, sr) / sr);
        const double b0 = 1.0 / (1.0 + K);
        return { b0, -b0, 0.0, (K - 1.0) / (K + 1.0), 0.0 };
    }

    /** Butterworth cut filter of the given order; the user Q scales the resonance of the
        sharpest section (Q = 0.707 gives a plain Butterworth response). */
    inline void cut (BandCoeffs& c, bool isHigh, double f, double q, int order, double sr)
    {
        c.numSections = 0;
        const double resonance = q * juce::MathConstants<double>::sqrt2; // q / 0.7071
        const bool odd = (order % 2) == 1;
        const double pi = juce::MathConstants<double>::pi;

        if (odd)
            c.sections[c.numSections++] = isHigh ? firstOrderHighPass (f, sr) : firstOrderLowPass (f, sr);

        const int pairs = order / 2;
        for (int k = 0; k < pairs && c.numSections < BandCoeffs::maxSections; ++k)
        {
            // Butterworth pole-pair Qs (ascending with k)
            double sq = odd ? 1.0 / (2.0 * std::cos (pi * (k + 1.0) / order))
                            : 1.0 / (2.0 * std::cos (pi * (2.0 * k + 1.0) / (2.0 * order)));
            if (k == pairs - 1)
                sq *= resonance;
            sq = juce::jlimit (0.1, 60.0, sq);
            c.sections[c.numSections++] = isHigh ? highPass (f, sq, sr) : lowPass (f, sq, sr);
        }
    }
} // namespace design

/** Designs the full set of sections for a band. gainDb already includes any dynamic offset. */
inline BandCoeffs designBand (const BandSettings& s, double sr)
{
    BandCoeffs c;
    const double f = s.freq, g = s.gainDb, q = juce::jmax (0.05, (double) s.q);

    switch (s.type)
    {
        case Bell:      c.sections[0] = design::peaking (f, g, q, sr);   c.numSections = 1; break;
        case LowShelf:  c.sections[0] = design::lowShelf (f, g, juce::jmin (q, 4.0), sr);  c.numSections = 1; break;
        case HighShelf: c.sections[0] = design::highShelf (f, g, juce::jmin (q, 4.0), sr); c.numSections = 1; break;
        case Notch:     c.sections[0] = design::notch (f, q, sr);        c.numSections = 1; break;
        case BandPass:  c.sections[0] = design::bandPass (f, q, sr);     c.numSections = 1; break;
        case LowCut:    design::cut (c, true,  f, q, slopeOrder (s.slope), sr); break;
        case HighCut:   design::cut (c, false, f, q, slopeOrder (s.slope), sr); break;
        case Tilt:
        {
            // High shelf of full gain, scaled so the response pivots: -g/2 below, +g/2 above.
            auto bq = design::highShelf (f, g, 0.5, sr);
            const double scale = std::pow (10.0, -g / 40.0);
            bq.b0 *= scale; bq.b1 *= scale; bq.b2 *= scale;
            c.sections[0] = bq; c.numSections = 1;
            break;
        }
        default: break;
    }
    return c;
}

/** Filter used by the dynamic detector to listen to the band's region. */
inline BandCoeffs designDetector (const BandSettings& s, double sr)
{
    BandCoeffs c;
    const double q = juce::jlimit (0.3, 8.0, (double) s.q);
    switch (s.type)
    {
        case LowShelf:  c.sections[0] = design::lowPass (s.freq, 0.707, sr);  c.numSections = 1; break;
        case HighShelf: c.sections[0] = design::highPass (s.freq, 0.707, sr); c.numSections = 1; break;
        case Tilt:      c.numSections = 0; break; // full band
        default:        c.sections[0] = design::bandPass (s.freq, q, sr);     c.numSections = 1; break;
    }
    return c;
}

/** Real 2x2 gain matrix acting on (L, R). Used for display and for the linear-phase FIR design. */
struct Matrix2
{
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0; // outL = a*L + b*R ; outR = c*L + d*R

    /** this = band * this  (band applied after what we already have) */
    void applyBand (double m, int mode) noexcept
    {
        double p, q, r, s; // band matrix [[p,q],[r,s]]
        switch (mode)
        {
            case Left:  p = m;   q = 0.0; r = 0.0; s = 1.0; break;
            case Right: p = 1.0; q = 0.0; r = 0.0; s = m;   break;
            case Mid:   p = 0.5 * (m + 1.0); q = 0.5 * (m - 1.0); r = q; s = p; break;
            case Side:  p = 0.5 * (1.0 + m); q = 0.5 * (1.0 - m); r = q; s = p; break;
            default:    p = m;   q = 0.0; r = 0.0; s = m;   break;
        }
        const double na = p * a + q * c, nb = p * b + q * d;
        const double nc = r * a + s * c, nd = r * b + s * d;
        a = na; b = nb; c = nc; d = nd;
    }

    double midGain()   const noexcept { return 0.5 * (a + b + c + d); }  // mid in -> mid out
    double sideGain()  const noexcept { return 0.5 * (a - b - c + d); }  // side in -> side out
    double leftGain()  const noexcept { return a; }
    double rightGain() const noexcept { return d; }
};
} // namespace tessera
