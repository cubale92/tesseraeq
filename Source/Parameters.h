#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tessera
{
constexpr int numBands = 24;

enum FilterType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, Tilt, numFilterTypes };
enum ChannelMode { Stereo = 0, Mid, Side, Left, Right, numChannelModes };
enum PhaseMode { MinimumPhase = 0, LinearPhase };

inline const juce::StringArray& filterTypeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt" };
    return names;
}

inline const juce::StringArray& slopeNames()
{
    static const juce::StringArray names { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    return names;
}

inline int slopeOrder (int slopeIndex)
{
    static const int orders[] { 1, 2, 3, 4, 6, 8 };
    return orders[juce::jlimit (0, 5, slopeIndex)];
}

inline const juce::StringArray& channelModeNames()
{
    static const juce::StringArray names { "Stereo", "Mid", "Side", "Left", "Right" };
    return names;
}

inline bool typeHasGain (int type)
{
    return type == Bell || type == LowShelf || type == HighShelf || type == Tilt;
}

inline bool typeHasSlope (int type) { return type == LowCut || type == HighCut; }

// Parameter IDs -----------------------------------------------------------------
namespace pid
{
    inline juce::String band (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }

    static constexpr const char* on      = "on";
    static constexpr const char* type    = "type";
    static constexpr const char* freq    = "freq";
    static constexpr const char* gain    = "gain";
    static constexpr const char* q       = "q";
    static constexpr const char* slope   = "slope";
    static constexpr const char* channel = "ch";
    static constexpr const char* dyn     = "dyn";
    static constexpr const char* thresh  = "thr";
    static constexpr const char* range   = "rng";

    static constexpr const char* phase      = "phase";
    static constexpr const char* firQuality = "firq";
    static constexpr const char* output     = "out";
    static constexpr const char* attack     = "attack";
    static constexpr const char* release    = "release";
    static constexpr const char* dispRange  = "disprange";
    static constexpr const char* analyzer   = "analyzer";
}

inline float defaultBandFrequency (int band)
{
    // spread the 24 bands from ~30 Hz to ~16 kHz
    return 30.0f * std::pow (16000.0f / 30.0f, (float) band / (float) (numBands - 1));
}

inline juce::NormalisableRange<float> logRange (float minV, float maxV)
{
    return { minV, maxV,
             [] (float start, float end, float n)  { return start * std::pow (end / start, n); },
             [] (float start, float end, float v)  { return std::log (v / start) / std::log (end / start); },
             [] (float start, float end, float v)  { return juce::jlimit (start, end, v); } };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };
    auto hzText = [] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                            : String (v, v < 100.0f ? 1 : 0) + " Hz";
    };
    auto msText = [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; };

    for (int b = 0; b < numBands; ++b)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), "|");
        auto name = [b] (const char* n) { return "Band " + String (b + 1) + " " + n; };

        group->addChild (std::make_unique<AudioParameterBool>   (ParameterID { pid::band (b, pid::on), 1 }, name ("On"), false));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid::band (b, pid::type), 1 }, name ("Type"), filterTypeNames(), Bell));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { pid::band (b, pid::freq), 1 }, name ("Freq"),
                                                                 logRange (10.0f, 30000.0f), defaultBandFrequency (b),
                                                                 AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText)));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { pid::band (b, pid::gain), 1 }, name ("Gain"),
                                                                 NormalisableRange<float> (-30.0f, 30.0f, 0.01f), 0.0f,
                                                                 AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { pid::band (b, pid::q), 1 }, name ("Q"),
                                                                 logRange (0.1f, 40.0f), 1.0f,
                                                                 AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid::band (b, pid::slope), 1 }, name ("Slope"), slopeNames(), 1));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid::band (b, pid::channel), 1 }, name ("Channel"), channelModeNames(), Stereo));
        group->addChild (std::make_unique<AudioParameterBool>   (ParameterID { pid::band (b, pid::dyn), 1 }, name ("Dynamic"), false));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { pid::band (b, pid::thresh), 1 }, name ("Threshold"),
                                                                 NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -24.0f,
                                                                 AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { pid::band (b, pid::range), 1 }, name ("Dyn Range"),
                                                                 NormalisableRange<float> (-30.0f, 30.0f, 0.1f), -6.0f,
                                                                 AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
        layout.add (std::move (group));
    }

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::phase, 1 }, "Phase Mode", StringArray { "Minimum Phase", "Linear Phase" }, MinimumPhase));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::firQuality, 1 }, "Linear Quality", StringArray { "Low", "Medium", "High", "Max" }, 1));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid::output, 1 }, "Output Gain",
                                                        NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                                        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid::attack, 1 }, "Dyn Attack",
                                                        logRange (0.1f, 200.0f), 5.0f,
                                                        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { pid::release, 1 }, "Dyn Release",
                                                        logRange (5.0f, 2000.0f), 120.0f,
                                                        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::dispRange, 1 }, "Display Range",
                                                        StringArray { "+/- 3 dB", "+/- 6 dB", "+/- 12 dB", "+/- 24 dB", "+/- 36 dB" }, 2,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::analyzer, 1 }, "Analyzer",
                                                        StringArray { "Off", "Post", "Pre + Post" }, 2,
                                                        AudioParameterChoiceAttributes().withAutomatable (false)));
    return layout;
}

inline float displayRangeDb (int index)
{
    static const float r[] { 3.0f, 6.0f, 12.0f, 24.0f, 36.0f };
    return r[juce::jlimit (0, 4, index)];
}

/** FIR length for the linear-phase engine, scaled with the sample rate. */
inline int firLengthFor (int quality, double sampleRate)
{
    static const int base[] { 4096, 8192, 16384, 32768 };
    int len = base[juce::jlimit (0, 3, quality)];
    if (sampleRate > 50000.0)  len *= 2;
    if (sampleRate > 100000.0) len *= 2;
    return juce::jmin (len, 131072);
}
} // namespace tessera
