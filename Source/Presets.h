#pragma once

#include "Parameters.h"
#include <vector>

namespace tessera
{
/** One band inside a factory preset. */
struct PresetBand
{
    int type;
    float freq, gain, q;
    int slope = 3;            // index into slopeNames() (3 = 24 dB/oct)
    int channel = Stereo;
    bool dyn = false;
    float thresh = -24.0f, range = -6.0f;
};

struct FactoryPreset
{
    const char* category;
    const char* name;
    std::vector<PresetBand> bands;
    float outputDb = 0.0f;
};

// Small helpers so the tables read like an engineer's notes.
inline PresetBand bell  (float f, float g, float q, int ch = Stereo)            { return { Bell, f, g, q, 1, ch }; }
inline PresetBand lowShelf  (float f, float g, float q = 0.7f, int ch = Stereo) { return { LowShelf, f, g, q, 1, ch }; }
inline PresetBand highShelf (float f, float g, float q = 0.7f, int ch = Stereo) { return { HighShelf, f, g, q, 1, ch }; }
inline PresetBand lowCut  (float f, int slope = 3, float q = 0.71f, int ch = Stereo) { return { LowCut, f, 0.0f, q, slope, ch }; }
inline PresetBand highCut (float f, int slope = 1, float q = 0.71f, int ch = Stereo) { return { HighCut, f, 0.0f, q, slope, ch }; }
inline PresetBand notch (float f, float q)                                      { return { Notch, f, 0.0f, q, 1, Stereo }; }
inline PresetBand tilt  (float f, float g)                                      { return { Tilt, f, g, 1.0f, 1, Stereo }; }
/** Dynamic bell: static gain g, moves by `range` dB when the region crosses `thr`. */
inline PresetBand dynBell (float f, float g, float q, float thr, float range, int ch = Stereo)
{
    PresetBand b { Bell, f, g, q, 1, ch };
    b.dyn = true; b.thresh = thr; b.range = range;
    return b;
}
inline PresetBand dynShelf (int type, float f, float g, float thr, float range)
{
    PresetBand b { type, f, g, 0.7f, 1, Stereo };
    b.dyn = true; b.thresh = thr; b.range = range;
    return b;
}

// slope indices: 0 = 6, 1 = 12, 2 = 18, 3 = 24, 4 = 36, 5 = 48 dB/oct

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets
    {
        { "Default", "Init (Flat)", {} },

        //======================================================================
        // 90s NY BOOM BAP — dusty, punchy, mid-forward, rolled-off tops
        //======================================================================
        { "90s NY Boom Bap", "BB Kick - Knock", {
            lowCut (30.0f, 3),
            bell (62.0f, 4.0f, 1.2f),                  // chest thump
            bell (260.0f, -4.0f, 1.6f),                // take out the cardboard
            bell (3500.0f, 3.0f, 1.4f),                // beater knock that cuts through samples
            highCut (9000.0f, 1) } },                   // keep it dusty

        { "90s NY Boom Bap", "BB Kick - Dusty Sub", {
            lowCut (28.0f, 4),
            lowShelf (70.0f, 3.5f, 0.8f),
            bell (180.0f, -2.5f, 1.2f),
            bell (1100.0f, 1.5f, 1.0f),
            highCut (6000.0f, 2) } },

        { "90s NY Boom Bap", "BB Snare - Crack", {
            lowCut (110.0f, 1),
            bell (200.0f, 2.5f, 1.3f),                 // body
            bell (480.0f, -3.0f, 2.0f),                // boxy ring
            bell (2400.0f, 3.5f, 1.0f),                // crack
            dynBell (5200.0f, 0.0f, 2.5f, -26.0f, -4.0f), // tame harsh rimshots only when they hit
            highShelf (9000.0f, -2.0f) } },

        { "90s NY Boom Bap", "BB Snare - Fat Backbeat", {
            lowCut (90.0f, 1),
            bell (180.0f, 3.5f, 1.0f),
            bell (900.0f, -2.0f, 1.8f),
            bell (3200.0f, 2.0f, 0.9f),
            highCut (11000.0f, 1) } },

        { "90s NY Boom Bap", "BB Hats & Cymbals - Crunch", {
            lowCut (320.0f, 3),
            bell (900.0f, -2.5f, 1.2f),
            bell (6000.0f, 2.0f, 1.0f),
            highCut (13000.0f, 1) } },

        { "90s NY Boom Bap", "BB Drum Bus - SP Dust", {
            lowCut (35.0f, 3),
            lowShelf (90.0f, 2.5f, 0.7f),
            bell (420.0f, -2.0f, 1.0f),
            bell (2800.0f, 1.5f, 0.8f),
            highCut (12000.0f, 1),                      // 12-bit-era top end
            lowCut (150.0f, 1, 0.71f, Side) } },        // mono low end

        { "90s NY Boom Bap", "BB Drum Bus - Basement Punch", {
            lowCut (32.0f, 3),
            bell (75.0f, 3.0f, 1.1f),
            bell (350.0f, -3.0f, 1.2f),
            bell (1600.0f, 2.0f, 1.0f),
            highShelf (8000.0f, -1.5f),
            lowCut (180.0f, 1, 0.71f, Side) } },

        { "90s NY Boom Bap", "BB Sample Chop - Vinyl Loop", {
            lowCut (75.0f, 2),
            bell (300.0f, -2.0f, 1.0f),
            bell (1200.0f, 2.0f, 0.7f),                 // vinyl mid honk
            notch (60.0f, 6.0f),                        // turntable hum
            highCut (8000.0f, 3) } },

        { "90s NY Boom Bap", "BB Filtered Bass (Low Pass Loop)", {
            lowCut (38.0f, 1),
            bell (90.0f, 3.0f, 1.0f),
            highCut (240.0f, 3, 1.1f) } },               // classic "filter the loop down to the bassline"

        { "90s NY Boom Bap", "BB Jazz Upright Bass", {
            lowCut (35.0f, 3),
            bell (85.0f, 2.0f, 1.0f),
            bell (250.0f, -2.0f, 1.3f),
            bell (750.0f, 2.0f, 1.2f),                  // finger growl
            highCut (4500.0f, 1) } },

        { "90s NY Boom Bap", "BB Horn Stab - Mid Bite", {
            lowCut (140.0f, 2),
            bell (350.0f, -2.0f, 1.0f),
            bell (1500.0f, 3.0f, 0.9f),
            highCut (9000.0f, 1) } },

        { "90s NY Boom Bap", "BB Rap Vocal - Upfront", {
            lowCut (90.0f, 2),
            bell (250.0f, -3.0f, 1.4f),                 // mud
            bell (3000.0f, 2.5f, 1.0f),                 // presence
            dynBell (6500.0f, 0.0f, 3.0f, -28.0f, -5.0f), // de-ess
            highShelf (12000.0f, 1.5f) } },

        { "90s NY Boom Bap", "BB Rap Vocal - Gritty Mic", {
            lowCut (120.0f, 3),
            bell (400.0f, -2.0f, 1.2f),
            bell (1800.0f, 3.0f, 1.2f),
            dynBell (5500.0f, 0.0f, 2.5f, -26.0f, -4.0f),
            highCut (10000.0f, 1) } },

        { "90s NY Boom Bap", "BB Scratch & Cuts", {
            lowCut (200.0f, 3),
            bell (1000.0f, 2.5f, 1.0f),
            highCut (7000.0f, 2) } },

        { "90s NY Boom Bap", "BB Master - 1994", {
            lowCut (25.0f, 3),
            lowShelf (100.0f, 1.5f, 0.7f),
            bell (350.0f, -1.5f, 0.8f),
            bell (3200.0f, 1.0f, 0.7f),
            highShelf (12000.0f, -1.0f),                // warm, not bright
            lowCut (120.0f, 1, 0.71f, Side) } },

        //======================================================================
        // NEW JACK SWING — tight, bright, gated drums, digital synths, shiny vocals
        //======================================================================
        { "New Jack Swing", "NJS Kick - Machine Punch", {
            lowCut (28.0f, 3),
            bell (55.0f, 3.0f, 1.5f),
            bell (350.0f, -3.0f, 1.2f),
            bell (4500.0f, 3.5f, 1.2f) } },              // the click that swings

        { "New Jack Swing", "NJS Snare - Gated Smack", {
            lowCut (140.0f, 2),
            bell (220.0f, 3.0f, 1.4f),
            bell (1000.0f, -2.0f, 1.5f),
            bell (5000.0f, 4.0f, 0.9f),
            highShelf (10000.0f, 2.0f) } },

        { "New Jack Swing", "NJS Snare - Big Room Clap", {
            lowCut (180.0f, 3),
            bell (700.0f, -2.5f, 1.2f),
            bell (2200.0f, 3.0f, 1.0f),
            highShelf (8000.0f, 3.0f),
            highShelf (5000.0f, 1.5f, 0.7f, Side) } },   // wide reverb tail

        { "New Jack Swing", "NJS Hats - Tight & Bright", {
            lowCut (450.0f, 3),
            bell (3500.0f, -1.5f, 1.5f),
            highShelf (9000.0f, 3.0f) } },

        { "New Jack Swing", "NJS Swing Drums Bus", {
            lowCut (30.0f, 3),
            lowShelf (80.0f, 2.0f, 0.7f),
            bell (300.0f, -2.5f, 1.0f),
            bell (5000.0f, 2.0f, 0.8f),
            highShelf (11000.0f, 2.5f),
            lowCut (180.0f, 1, 0.71f, Side) } },

        { "New Jack Swing", "NJS Synth Bass - FM Slap", {
            lowCut (35.0f, 3),
            bell (70.0f, 2.5f, 1.0f),
            bell (220.0f, -2.5f, 1.4f),
            bell (1500.0f, 3.0f, 1.6f),                 // pluck / slap attack
            highCut (6000.0f, 1) } },

        { "New Jack Swing", "NJS Synth Bass - Round Sub", {
            lowCut (30.0f, 4),
            lowShelf (60.0f, 2.5f, 0.8f),
            bell (180.0f, -2.0f, 1.0f),
            highCut (2500.0f, 2) } },

        { "New Jack Swing", "NJS Stabs & Keys - DX Shine", {
            lowCut (150.0f, 2),
            bell (400.0f, -2.5f, 1.0f),
            bell (2500.0f, 2.0f, 1.0f),
            highShelf (9000.0f, 3.0f),
            highShelf (6000.0f, 2.0f, 0.7f, Side) } },   // widen the stabs

        { "New Jack Swing", "NJS Electric Piano - Silky", {
            lowCut (90.0f, 2),
            bell (300.0f, -1.5f, 1.0f),
            bell (1200.0f, 1.5f, 0.8f),
            highShelf (7000.0f, 2.0f),
            dynBell (2800.0f, 0.0f, 2.0f, -24.0f, -3.0f) } }, // smooth the tine bark

        { "New Jack Swing", "NJS Lead Vocal - Silky", {
            lowCut (100.0f, 2),
            bell (220.0f, -2.0f, 1.2f),
            bell (1800.0f, 1.0f, 0.8f),
            dynBell (7000.0f, 0.0f, 2.5f, -30.0f, -5.0f), // de-ess
            highShelf (10000.0f, 3.5f) } },               // air

        { "New Jack Swing", "NJS BG Vocals - Airy Stack", {
            lowCut (180.0f, 3),
            bell (350.0f, -3.0f, 1.0f),
            highShelf (8000.0f, 4.0f),
            bell (3000.0f, 2.0f, 0.7f, Side),
            dynBell (6500.0f, 0.0f, 2.5f, -30.0f, -4.0f) } },

        { "New Jack Swing", "NJS Master - Shiny 1990", {
            lowCut (25.0f, 3),
            lowShelf (60.0f, 1.5f, 0.7f),
            bell (250.0f, -1.5f, 0.8f),
            bell (4000.0f, 1.5f, 0.6f),
            highShelf (12000.0f, 2.0f),
            highShelf (8000.0f, 1.5f, 0.7f, Side),
            lowCut (120.0f, 1, 0.71f, Side) } },

        //======================================================================
        // UTILITY
        //======================================================================
        { "Utility", "Rumble Cut 30 Hz", { lowCut (30.0f, 4) } },
        { "Utility", "Mono Lows (Side cut 120 Hz)", { lowCut (120.0f, 2, 0.71f, Side) } },
        { "Utility", "Mud Remover", { bell (300.0f, -3.0f, 1.2f) } },
        { "Utility", "Air Lift", { highShelf (12000.0f, 3.0f, 0.6f) } },
        { "Utility", "De-Esser (Dynamic)", { dynBell (6500.0f, 0.0f, 2.5f, -30.0f, -6.0f) } },
        { "Utility", "Telephone", { lowCut (400.0f, 4), bell (1500.0f, 4.0f, 1.0f), highCut (3200.0f, 4) } },
        { "Utility", "AM Radio / Lo-Fi", { lowCut (250.0f, 3), bell (1000.0f, 3.0f, 0.8f), highCut (4500.0f, 3) } },
        { "Utility", "Warm Tilt", { tilt (900.0f, -3.0f) } },
        { "Utility", "Bright Tilt", { tilt (900.0f, 3.0f) } },
    };
    return presets;
}
} // namespace tessera
