# Tessera EQ

A 24-band parametric EQ plugin (VST3, plus AU on macOS) built with JUCE 8. It has mid/side and left/right processing on each band, dynamic EQ, and a choice of minimum-phase or linear-phase processing.

## Features

- **24 bands.** Shapes: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass and Tilt. The cut filters have slopes from 6 to 48 dB/oct, and their Q sets the resonance (0.71 gives a plain Butterworth response).
- **Per-band routing.** Each band processes Stereo, Mid, Side, Left or Right. When bands use different routings, the graph draws separate M/S (or L/R) curves.
- **Dynamic EQ.** Any Bell, Shelf or Tilt band can be made dynamic. The band listens to its own frequency region. When that region goes over THRESH, the band moves by up to RANGE dB: a negative range cuts (a de-esser or resonance tamer) and a positive range boosts (upward expansion). Attack and release are global settings.
- **Phase modes.**
  - *Minimum phase*: zero latency, with smoothed coefficients so you can automate it.
  - *Linear phase*: the whole EQ, including the M/S matrix, is turned into symmetric FIR filters. These run in four paths (L→L, R→L, L→R and R→R) through a custom partitioned convolution engine. All filter versions share one input history, so editing bands while audio plays crossfades smoothly with no dropouts. Latency is half the filter length plus one 512-sample block:

    | Quality | FIR length at 44.1/48 kHz | Latency at 48 kHz |
    |---|---|---|
    | Low | 4096 | 53 ms |
    | Medium | 8192 | 96 ms |
    | High | 16384 | 181 ms |
    | Max | 32768 | 352 ms |

    The FIR length doubles at 88.2/96 kHz. Switching phase mode or quality fades out, switches, and fades back in once the new filters are primed (about 0.2 s of quiet). Host bypass is delayed by the same latency so the track never jumps in time. In this mode the dynamic part of a band runs as a matched minimum-phase filter after the FIR, and its detector is delayed so it stays aligned with the audio.
- **Display.** An FFT analyser (post, or pre and post) with a 4.5 dB/oct display tilt, a piano strip, a readout of the note under each band, and output meters for L, R, M and S with peak hold.

## Using it

| Action | How |
|---|---|
| Add a band | Double-click the graph, right-click for a shape menu, or press **+** in the band strip |
| Move a band | Drag its node. Hold Shift for fine control, Ctrl/Cmd to lock to one axis |
| Change Q | Mouse wheel over a node |
| Remove a band | Double-click its node, or use the **x** in its column |
| More options | Right-click a node (shape, slope, channel, dynamic, reset) |
| Reset a knob | Double-click it |

## Building

You need CMake 3.22+ and a C++17 compiler. If you don't pass `-DJUCE_DIR=...`, JUCE 8.0.4 is downloaded automatically.

```bash
# Windows (Visual Studio 2022)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DTESSERA_BUILD_TESTS=OFF
cmake --build build --config Release --target TesseraEQ_VST3
# -> build/TesseraEQ_artefacts/Release/VST3/Tessera EQ.vst3

# macOS
cmake -B build -G Xcode -DTESSERA_FORMATS="VST3;AU" -DTESSERA_BUILD_TESTS=OFF
cmake --build build --config Release

# Linux (also builds the test runner)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/TesseraTests_artefacts/Release/TesseraTests   # optional: pass a .png path to save an editor screenshot
```

To install on Windows, copy `Tessera EQ.vst3` into `C:\Program Files\Common Files\VST3\` and rescan plugins in your DAW.

**CI:** `.github/workflows/build.yml` runs on every push. It builds the plugin on Windows, macOS and Linux, runs the test suite on Windows and Linux, and runs `pluginval` (strictness 10) against the actual Windows, macOS and Linux binaries. You can download the plugins from the run's **Artifacts** section.

## Tests

`Tests/TestMain.cpp` checks:

- Filter maths: −3 dB at the cutoff and the correct slope for every cut order.
- Min-phase gains, plus Side-only and Left-only routing.
- Dynamic EQ threshold and range.
- That the linear-phase response matches the design at six frequencies, has the right latency, and has a symmetric impulse response.
- That editing bands in linear phase causes no dropouts, that switching phase modes fades cleanly with no level jump, and that bypass keeps the latency.
- Saving and restoring state.
- CPU use.
- 400 blocks of random automation at 44.1 to 192 kHz with no NaNs.

The plugin also passes `pluginval` at strictness level 10.

## Layout

```
Source/
  Parameters.h          parameter IDs, ranges, layout
  FilterDesign.h        biquad designs, magnitude responses, 2x2 M/S/L/R response matrix
  LinearPhaseEngine.*   background FIR designer + 4-path convolution
  PluginProcessor.*     min-phase band chain, dynamic detectors, meters, analyser feed
  PluginEditor.*        window + header controls
  UI/                   graph (analyser/curves/nodes/piano), band strip, meters, look and feel
Tests/TestMain.cpp      offline test runner
```

## Known limitations

- Filters use the standard RBJ bilinear designs, so bells and shelves get slightly narrower close to Nyquist. Adding oversampling or matched-z designs would fix this.
- Changing the phase mode or the linear-phase quality changes the plugin's latency. Some DAWs only pick up a new latency after you stop and restart playback.
