#include "GraphComponent.h"
#include "TesseraLookAndFeel.h"

namespace tessera
{
struct SpectrumState
{
    explicit SpectrumState (int size) : ring ((size_t) size, 0.0f), db ((size_t) size / 2 + 1, -120.0f) {}
    std::vector<float> ring;
    int writePos = 0;
    std::vector<float> db;
};

static constexpr float kMinFreq = 10.0f;
static constexpr float kAnalyserTop = 6.0f, kAnalyserBottom = -96.0f;

GraphComponent::GraphComponent (TesseraProcessor& p, juce::Value& selectedBand)
    : processor (p)
{
    selected.referTo (selectedBand);
    selected.addListener (this);
    pre  = std::make_unique<SpectrumState> (fftSize);
    post = std::make_unique<SpectrumState> (fftSize);
    fftBuffer.assign ((size_t) fftSize * 2, 0.0f);
    pullBuffer.assign (4096, 0.0f);
    setOpaque (true);
    startTimerHz (30);
}

GraphComponent::~GraphComponent()
{
    selected.removeListener (this);
}

void GraphComponent::resized()
{
    auto r = getLocalBounds().toFloat();
    keys = r.removeFromBottom (20.0f);
    r.removeFromRight (34.0f);   // dB labels
    plot = r.reduced (0.0f, 6.0f);
}

//==============================================================================
float GraphComponent::maxFreq() const noexcept
{
    return (float) juce::jmin (30000.0, processor.getCurrentSampleRate() * 0.5);
}

float GraphComponent::xForFreq (float f) const noexcept
{
    return plot.getX() + plot.getWidth() * std::log (f / kMinFreq) / std::log (maxFreq() / kMinFreq);
}

float GraphComponent::freqForX (float x) const noexcept
{
    return kMinFreq * std::pow (maxFreq() / kMinFreq, (x - plot.getX()) / plot.getWidth());
}

float GraphComponent::rangeDb() const noexcept
{
    return displayRangeDb ((int) processor.apvts.getRawParameterValue (pid::dispRange)->load());
}

float GraphComponent::yForDb (float db) const noexcept
{
    const float r = rangeDb();
    return juce::jmap (db, r, -r, plot.getY(), plot.getBottom());
}

float GraphComponent::dbForY (float y) const noexcept
{
    const float r = rangeDb();
    return juce::jmap (y, plot.getY(), plot.getBottom(), r, -r);
}

float GraphComponent::yForAnalyserDb (float db) const noexcept
{
    return juce::jmap (db, kAnalyserTop, kAnalyserBottom, plot.getY(), plot.getBottom());
}

//==============================================================================
void GraphComponent::processAnalyser (SpectrumState& s, AnalyzerFifo& fifo)
{
    int pulledTotal = 0;
    for (;;)
    {
        const int n = fifo.pull (pullBuffer.data(), (int) pullBuffer.size());
        if (n <= 0) break;
        pulledTotal += n;
        for (int i = 0; i < n; ++i)
        {
            s.ring[(size_t) s.writePos] = pullBuffer[(size_t) i];
            if (++s.writePos == fftSize) s.writePos = 0;
        }
    }

    if (pulledTotal == 0)
    {
        for (auto& v : s.db)
            v = juce::jmax (-120.0f, v - 2.0f);
        return;
    }

    for (int i = 0; i < fftSize; ++i)
        fftBuffer[(size_t) i] = s.ring[(size_t) ((s.writePos + i) % fftSize)];
    std::fill (fftBuffer.begin() + fftSize, fftBuffer.end(), 0.0f);

    window.multiplyWithWindowingTable (fftBuffer.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const double sr = processor.getCurrentSampleRate();
    const float norm = 4.0f / (float) fftSize; // hann coherent gain 0.5, one-sided x2
    for (size_t k = 1; k < s.db.size(); ++k)
    {
        const float f = (float) ((double) k * sr / fftSize);
        // +4.5 dB/oct display tilt around 1 kHz so pink-ish material reads flat
        const float tilt = 4.5f * std::log2 (juce::jmax (f, 1.0f) / 1000.0f);
        const float newDb = juce::Decibels::gainToDecibels (fftBuffer[k] * norm, -140.0f) + tilt;
        auto& v = s.db[k];
        v = newDb > v ? v + 0.6f * (newDb - v) : juce::jmax (newDb, v - 1.6f);
    }
}

void GraphComponent::timerCallback()
{
    const int mode = (int) processor.apvts.getRawParameterValue (pid::analyzer)->load();
    if (mode >= 1) processAnalyser (*post, processor.postFifo);
    if (mode >= 2) processAnalyser (*pre, processor.preFifo);

    processor.snapshotBands (snapshot);
    for (int b = 0; b < numBands; ++b)
        deltas[(size_t) b] = processor.getDynamicDelta (b);

    repaint();
}

//==============================================================================
void GraphComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    juce::ColourGradient bg (colours::panel.brighter (0.03f), 0.0f, plot.getY(),
                             colours::background, 0.0f, plot.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRect (plot);

    drawGrid (g);
    drawAnalyser (g);
    drawCurves (g);
    drawNodes (g);
    drawKeyboard (g);
    drawReadout (g);
}

void GraphComponent::drawGrid (juce::Graphics& g)
{
    using namespace juce;
    const float freqs[] { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                          1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 };
    for (auto f : freqs)
    {
        if (f > maxFreq()) continue;
        const bool major = juce::exactlyEqual (f, 100.0f) || juce::exactlyEqual (f, 1000.0f) || juce::exactlyEqual (f, 10000.0f);
        g.setColour (major ? colours::gridStrong : colours::grid);
        g.drawVerticalLine (roundToInt (xForFreq (f)), plot.getY(), plot.getBottom());
    }

    g.setFont (TesseraLookAndFeel::uiFont (10.5f));
    g.setColour (colours::textDim);
    const float labelled[] { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (auto f : labelled)
    {
        if (f > maxFreq()) continue;
        const String txt = f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f);
        g.drawText (txt, Rectangle<float> (xForFreq (f) + 3.0f, plot.getBottom() - 15.0f, 40.0f, 13.0f), Justification::left);
    }

    const float r = rangeDb();
    const float step = r <= 3 ? 1.0f : r <= 6 ? 2.0f : r <= 12 ? 3.0f : r <= 24 ? 6.0f : 12.0f;
    for (float db = -r; db <= r + 0.01f; db += step)
    {
        const float y = yForDb (db);
        g.setColour (std::abs (db) < 0.01f ? colours::gridStrong.brighter (0.15f) : colours::grid);
        g.drawHorizontalLine (roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (colours::textDim);
        g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (plot.getRight() + 4.0f, y - 7.0f, 28.0f, 14.0f),
                    Justification::left);
    }
}

void GraphComponent::drawAnalyser (juce::Graphics& g)
{
    using namespace juce;
    const int mode = (int) processor.apvts.getRawParameterValue (pid::analyzer)->load();
    if (mode == 0) return;

    const double sr = processor.getCurrentSampleRate();
    auto buildPath = [&] (const SpectrumState& s, bool closed)
    {
        Path p;
        const float step = 1.5f;
        bool started = false;
        for (float x = plot.getX(); x <= plot.getRight(); x += step)
        {
            const float f0 = freqForX (x), f1 = freqForX (x + step);
            const double k0 = f0 * fftSize / sr, k1 = f1 * fftSize / sr;
            float db;
            if (k1 - k0 < 1.0)
            {
                const int i0 = jlimit (1, (int) s.db.size() - 2, (int) k0);
                const float frac = (float) (k0 - i0);
                db = s.db[(size_t) i0] + frac * (s.db[(size_t) i0 + 1] - s.db[(size_t) i0]);
            }
            else
            {
                db = -140.0f;
                for (int k = jmax (1, (int) k0); k <= jmin ((int) s.db.size() - 1, (int) k1); ++k)
                    db = jmax (db, s.db[(size_t) k]);
            }
            const float y = jlimit (plot.getY(), plot.getBottom(), yForAnalyserDb (db));
            if (! started) { p.startNewSubPath (x, closed ? plot.getBottom() : y); if (closed) p.lineTo (x, y); started = true; }
            else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
        return p;
    };

    if (mode >= 2)
    {
        g.setColour (colours::analyserPre.withAlpha (0.10f));
        g.fillPath (buildPath (*pre, true));
    }

    auto fill = buildPath (*post, true);
    ColourGradient grad (colours::analyserPost.withAlpha (0.38f), 0.0f, plot.getY() + plot.getHeight() * 0.25f,
                         colours::analyserPost.withAlpha (0.04f), 0.0f, plot.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (fill);
    g.setColour (colours::analyserPost.brighter (0.4f).withAlpha (0.55f));
    g.strokePath (buildPath (*post, false), PathStrokeType (1.0f));
}

void GraphComponent::drawCurves (juce::Graphics& g)
{
    using namespace juce;
    const double sr = processor.getCurrentSampleRate();
    const int sel = (int) selected.getValue();

    struct Designed { BandCoeffs stat, live; int channel; int index; };
    std::vector<Designed> bands;
    bool hasMS = false, hasLR = false;
    for (int b = 0; b < numBands; ++b)
    {
        const auto& s = snapshot[(size_t) b];
        if (! s.on) continue;
        Designed d;
        d.stat = designBand (s.settings, sr);
        auto liveSettings = s.settings;
        liveSettings.gainDb += deltas[(size_t) b];
        d.live = std::abs (deltas[(size_t) b]) > 0.01f ? designBand (liveSettings, sr) : d.stat;
        d.channel = s.channel; d.index = b;
        hasMS |= (s.channel == Mid || s.channel == Side);
        hasLR |= (s.channel == Left || s.channel == Right);
        bands.push_back (d);
    }

    // individual band shapes
    for (const auto& d : bands)
    {
        const bool isSel = d.index == sel;
        Path p;
        const float zeroY = yForDb (0.0f);
        p.startNewSubPath (plot.getX(), zeroY);
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const float db = Decibels::gainToDecibels ((float) d.stat.magnitude (freqForX (x), sr), -200.0f);
            p.lineTo (x, jlimit (plot.getY() - 2.0f, plot.getBottom() + 2.0f, yForDb (db)));
        }
        p.lineTo (plot.getRight(), zeroY);
        p.closeSubPath();
        const auto c = colours::band (d.index);
        g.setColour (c.withAlpha (isSel ? 0.22f : 0.06f));
        g.fillPath (p);
        g.setColour (c.withAlpha (isSel ? 0.85f : 0.28f));
        g.strokePath (p, PathStrokeType (isSel ? 1.4f : 1.0f));
    }

    // composite curves
    const int w = (int) plot.getWidth();
    std::vector<Matrix2> stat ((size_t) w + 1), live ((size_t) w + 1);
    bool anyDynamic = false;
    for (int i = 0; i <= w; ++i)
    {
        const double f = freqForX (plot.getX() + (float) i);
        for (const auto& d : bands)
        {
            stat[(size_t) i].applyBand (d.stat.magnitude (f, sr), d.channel);
            live[(size_t) i].applyBand (d.live.magnitude (f, sr), d.channel);
        }
    }
    for (const auto& d : bands)
        anyDynamic |= std::abs (deltas[(size_t) d.index]) > 0.01f;

    auto curvePath = [&] (const std::vector<Matrix2>& m, int which)
    {
        Path p;
        for (int i = 0; i <= w; ++i)
        {
            const auto& mm = m[(size_t) i];
            const double v = which == 0 ? mm.a : which == 1 ? mm.midGain() : which == 2 ? mm.sideGain()
                           : which == 3 ? mm.leftGain() : mm.rightGain();
            const float db = Decibels::gainToDecibels ((float) std::abs (v), -200.0f);
            const float y = jlimit (plot.getY() - 2.0f, plot.getBottom() + 2.0f, yForDb (db));
            if (i == 0) p.startNewSubPath (plot.getX(), y); else p.lineTo (plot.getX() + (float) i, y);
        }
        return p;
    };

    auto drawSet = [&] (int which, Colour c, const String& tag, bool dashed)
    {
        if (anyDynamic)
        {
            g.setColour (c.withAlpha (0.35f));
            g.strokePath (curvePath (stat, which), PathStrokeType (1.0f));
        }
        auto p = curvePath (live, which);
        g.setColour (c);
        if (dashed)
        {
            Path d;
            const float dashes[] { 6.0f, 4.0f };
            PathStrokeType (2.0f).createDashedStroke (d, p, dashes, 2);
            g.fillPath (d);
        }
        else
            g.strokePath (p, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

        if (tag.isNotEmpty())
        {
            const auto end = p.getCurrentPosition();
            g.setFont (TesseraLookAndFeel::uiFont (11.0f, true));
            g.drawText (tag, Rectangle<float> (end.x - 16.0f, end.y - 16.0f, 14.0f, 12.0f), Justification::centred);
        }
    };

    if (! hasMS && ! hasLR)
        drawSet (0, colours::curve, {}, false);
    if (hasMS)
    {
        drawSet (2, colours::sideCurve, "S", true);
        drawSet (1, colours::curve, "M", false);
    }
    if (hasLR)
    {
        drawSet (4, colours::accent.withAlpha (0.9f), "R", true);
        drawSet (3, colours::accent.brighter (0.4f), "L", false);
    }
}

juce::Point<float> GraphComponent::nodePosition (int band) const
{
    const auto& s = snapshot[(size_t) band];
    const float x = xForFreq (juce::jlimit (kMinFreq, maxFreq(), s.settings.freq));
    const float y = typeHasGain (s.settings.type) ? yForDb (juce::jlimit (-rangeDb(), rangeDb(), s.settings.gainDb))
                                                  : yForDb (0.0f);
    return { x, y };
}

int GraphComponent::nodeAt (juce::Point<float> p) const
{
    const int sel = (int) selected.getValue();
    if (sel >= 0 && sel < numBands && snapshot[(size_t) sel].on && nodePosition (sel).getDistanceFrom (p) < 10.0f)
        return sel;
    int best = -1; float bestD = 10.0f;
    for (int b = 0; b < numBands; ++b)
    {
        if (! snapshot[(size_t) b].on) continue;
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

void GraphComponent::drawNodes (juce::Graphics& g)
{
    using namespace juce;
    const int sel = (int) selected.getValue();
    g.setFont (TesseraLookAndFeel::uiFont (10.0f, true));

    for (int b = 0; b < numBands; ++b)
    {
        const auto& s = snapshot[(size_t) b];
        if (! s.on) continue;
        const auto pos = nodePosition (b);
        const auto c = colours::band (b);
        const bool isSel = b == sel, isHover = b == hoverBand;

        if (typeHasGain (s.settings.type) && std::abs (deltas[(size_t) b]) > 0.05f)
        {
            const float y2 = yForDb (jlimit (-rangeDb(), rangeDb(), s.settings.gainDb + deltas[(size_t) b]));
            g.setColour (c.withAlpha (0.8f));
            g.drawLine (pos.x, pos.y, pos.x, y2, 3.0f);
            g.drawEllipse (pos.x - 4.0f, y2 - 4.0f, 8.0f, 8.0f, 1.5f);
        }

        const float r = isSel ? 9.0f : 7.5f;
        if (isSel || isHover)
        {
            g.setColour (c.withAlpha (0.25f));
            g.fillEllipse (pos.x - r - 4.0f, pos.y - r - 4.0f, (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f);
        }
        g.setColour (c);
        g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
        g.setColour (isSel ? Colours::white : Colours::black.withAlpha (0.6f));
        g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, isSel ? 1.6f : 1.0f);
        g.setColour (Colours::black.withAlpha (0.85f));
        g.drawText (String (b + 1), Rectangle<float> (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f), Justification::centred);

        if (s.channel != Stereo)
        {
            static const char* tags[] { "", "M", "S", "L", "R" };
            g.setColour (c);
            g.drawText (tags[s.channel], Rectangle<float> (pos.x + r, pos.y - r - 10.0f, 12.0f, 11.0f), Justification::left);
        }
    }
}

void GraphComponent::drawKeyboard (juce::Graphics& g)
{
    using namespace juce;
    g.setColour (colours::panel);
    g.fillRect (keys);

    auto isBlack = [] (int note) { const int pc = note % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; };
    auto noteFreq = [] (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); };
    const float top = keys.getY() + 2.0f, bottom = keys.getBottom() - 2.0f;

    // white keys: each spans from the neighbouring black key (or half-semitone) on either side
    for (int note = 12; note < 136; ++note)
    {
        if (isBlack (note)) continue;
        const float lo = noteFreq ((float) note - (isBlack (note - 1) ? 1.0f : 0.5f));
        const float hi = noteFreq ((float) note + (isBlack (note + 1) ? 1.0f : 0.5f));
        if (hi < kMinFreq || lo > maxFreq()) continue;
        const float x0 = jmax (plot.getX(), xForFreq (lo)), x1 = jmin (plot.getRight(), xForFreq (hi));
        if (x1 <= x0) continue;
        g.setColour (Colour (0xffb4b9c4));
        g.fillRect (Rectangle<float>::leftTopRightBottom (x0, top, x1, bottom));
        g.setColour (Colour (0xff5d6372));
        g.drawVerticalLine (roundToInt (x0), top, bottom);
    }

    // black keys: narrower and shorter, drawn on top
    for (int note = 12; note < 136; ++note)
    {
        if (! isBlack (note)) continue;
        const float lo = noteFreq ((float) note - 0.32f), hi = noteFreq ((float) note + 0.32f);
        if (hi < kMinFreq || lo > maxFreq()) continue;
        const float x0 = jmax (plot.getX(), xForFreq (lo)), x1 = jmin (plot.getRight(), xForFreq (hi));
        if (x1 <= x0) continue;
        g.setColour (Colour (0xff1c1f26));
        g.fillRect (Rectangle<float>::leftTopRightBottom (x0, top, x1, top + (bottom - top) * 0.6f));
    }

    // C labels
    g.setFont (TesseraLookAndFeel::uiFont (8.5f, true));
    g.setColour (Colour (0xff3a3f4b));
    for (int oct = 0; oct <= 9; ++oct)
    {
        const int note = 12 * (oct + 1);
        const float x = xForFreq (noteFreq ((float) note));
        if (x < plot.getX() || x > plot.getRight() - 14.0f) continue;
        g.drawText ("C" + String (oct), Rectangle<float> (x + 1.0f, bottom - 9.0f, 16.0f, 9.0f), Justification::left);
    }

    // band markers on the keyboard
    for (int b = 0; b < numBands; ++b)
    {
        if (! snapshot[(size_t) b].on) continue;
        const float x = xForFreq (snapshot[(size_t) b].settings.freq);
        g.setColour (colours::band (b));
        g.fillEllipse (x - 3.5f, keys.getY() + 2.0f, 7.0f, 7.0f);
        g.setColour (Colours::black.withAlpha (0.6f));
        g.drawEllipse (x - 3.5f, keys.getY() + 2.0f, 7.0f, 7.0f, 1.0f);
    }
}

static juce::String noteName (float f)
{
    const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
    const int n = juce::roundToInt (midi);
    const int cents = juce::roundToInt ((midi - (float) n) * 100.0f);
    return juce::MidiMessage::getMidiNoteName (n, true, true, 4) + (cents >= 0 ? " +" : " ") + juce::String (cents) + "c";
}

void GraphComponent::drawReadout (juce::Graphics& g)
{
    using namespace juce;
    const int b = dragBand >= 0 ? dragBand : hoverBand;
    if (b < 0 || ! snapshot[(size_t) b].on) return;

    const auto& s = snapshot[(size_t) b].settings;
    String txt = filterTypeNames()[s.type] + "  " + String (s.freq >= 1000 ? String (s.freq / 1000.0f, 2) + " kHz" : String (s.freq, 1) + " Hz");
    if (typeHasGain (s.type)) txt << "  " << (s.gainDb > 0 ? "+" : "") << String (s.gainDb, 1) << " dB";
    txt << "  Q " << String (s.q, 2) << "   " << noteName (s.freq);

    const auto pos = nodePosition (b);
    g.setFont (TesseraLookAndFeel::uiFont (12.0f));
    const float tw = (float) GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 16.0f;
    auto box = Rectangle<float> (pos.x + 14.0f, pos.y - 30.0f, tw, 20.0f);
    if (box.getRight() > plot.getRight()) box.setX (pos.x - 14.0f - tw);
    if (box.getY() < plot.getY()) box.setY (pos.y + 14.0f);
    g.setColour (colours::panelRaised.withAlpha (0.92f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (colours::band (b).withAlpha (0.8f));
    g.drawRoundedRectangle (box, 4.0f, 1.0f);
    g.setColour (colours::text);
    g.drawText (txt, box, Justification::centred);
}

//==============================================================================
void GraphComponent::setParam (TesseraProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.apvts.getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

int GraphComponent::addBand (TesseraProcessor& p, int type, float freq, float gainDb)
{
    for (int b = 0; b < numBands; ++b)
    {
        if (p.apvts.getRawParameterValue (pid::band (b, pid::on))->load() > 0.5f)
            continue;
        setParam (p, pid::band (b, pid::type), (float) type);
        setParam (p, pid::band (b, pid::freq), freq);
        setParam (p, pid::band (b, pid::gain), typeHasGain (type) ? gainDb : 0.0f);
        setParam (p, pid::band (b, pid::q), type == LowCut || type == HighCut ? 0.707f : (type == Notch ? 4.0f : 1.0f));
        setParam (p, pid::band (b, pid::slope), 3.0f); // 24 dB/oct default for cuts
        setParam (p, pid::band (b, pid::channel), (float) Stereo);
        setParam (p, pid::band (b, pid::dyn), 0.0f);
        setParam (p, pid::band (b, pid::on), 1.0f);
        return b;
    }
    return -1;
}

void GraphComponent::mouseMove (const juce::MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hoverBand) { hoverBand = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void GraphComponent::mouseExit (const juce::MouseEvent&)
{
    hoverBand = -1;
    repaint();
}

void GraphComponent::mouseDown (const juce::MouseEvent& e)
{
    const int b = nodeAt (e.position);

    if (e.mods.isPopupMenu())
    {
        if (b >= 0) { selected = b; showBandMenu (b); }
        else if (plot.contains (e.position)) showAddMenu (e.position);
        return;
    }

    if (b < 0) return;

    selected = b;
    dragBand = b;
    dragStartNode = nodePosition (b);
    dragStartMouse = e.position;
    dragHasGain = typeHasGain (snapshot[(size_t) b].settings.type);

    processor.apvts.getParameter (pid::band (b, pid::freq))->beginChangeGesture();
    if (dragHasGain)
        processor.apvts.getParameter (pid::band (b, pid::gain))->beginChangeGesture();
}

void GraphComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;

    const float scale = e.mods.isShiftDown() ? 0.15f : 1.0f;
    auto delta = (e.position - dragStartMouse) * scale;
    // Ctrl/Cmd constrains to one axis
    if (e.mods.isCommandDown())
        (std::abs (delta.x) > std::abs (delta.y) ? delta.y : delta.x) = 0.0f;

    const auto target = dragStartNode + delta;
    const float f = juce::jlimit (kMinFreq, juce::jmin (maxFreq(), 30000.0f), freqForX (juce::jlimit (plot.getX(), plot.getRight(), target.x)));
    setParam (processor, pid::band (dragBand, pid::freq), f);
    if (dragHasGain)
        setParam (processor, pid::band (dragBand, pid::gain), juce::jlimit (-30.0f, 30.0f, dbForY (target.y)));

    processor.snapshotBands (snapshot);
    repaint();
}

void GraphComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0)
    {
        processor.apvts.getParameter (pid::band (dragBand, pid::freq))->endChangeGesture();
        if (dragHasGain)
            processor.apvts.getParameter (pid::band (dragBand, pid::gain))->endChangeGesture();
    }
    dragBand = -1;
}

void GraphComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = nodeAt (e.position);
    if (b >= 0)
    {
        setParam (processor, pid::band (b, pid::on), 0.0f);
        if ((int) selected.getValue() == b) selected = -1;
        hoverBand = -1;
        return;
    }
    if (! plot.contains (e.position)) return;

    const float f = freqForX (e.position.x);
    const int nb = addBand (processor, Bell, f, juce::jlimit (-30.0f, 30.0f, dbForY (e.position.y)));
    if (nb >= 0) selected = nb;
}

void GraphComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int b = nodeAt (e.position);
    if (b < 0) b = (int) selected.getValue();
    if (b < 0 || b >= numBands || ! snapshot[(size_t) b].on) return;

    auto* param = processor.apvts.getParameter (pid::band (b, pid::q));
    const float q = snapshot[(size_t) b].settings.q * std::exp (w.deltaY * (e.mods.isShiftDown() ? 0.3f : 1.5f));
    param->beginChangeGesture();
    setParam (processor, pid::band (b, pid::q), juce::jlimit (0.1f, 40.0f, q));
    param->endChangeGesture();
}

void GraphComponent::showBandMenu (int b)
{
    using namespace juce;
    const auto& s = snapshot[(size_t) b];
    PopupMenu typeMenu, chMenu, slopeMenu;
    for (int i = 0; i < numFilterTypes; ++i) typeMenu.addItem (100 + i, filterTypeNames()[i], true, s.settings.type == i);
    for (int i = 0; i < numChannelModes; ++i) chMenu.addItem (200 + i, channelModeNames()[i], true, s.channel == i);
    for (int i = 0; i < slopeNames().size(); ++i) slopeMenu.addItem (300 + i, slopeNames()[i], true, s.settings.slope == i);

    const bool dynOn = processor.apvts.getRawParameterValue (pid::band (b, pid::dyn))->load() > 0.5f;

    PopupMenu m;
    m.addSectionHeader ("Band " + String (b + 1));
    m.addSubMenu ("Shape", typeMenu);
    m.addSubMenu ("Slope", slopeMenu, typeHasSlope (s.settings.type));
    m.addSubMenu ("Channel", chMenu);
    m.addItem (2, "Dynamic", typeHasGain (s.settings.type), dynOn);
    m.addItem (3, "Reset gain");
    m.addSeparator();
    m.addItem (1, "Remove band");

    SafePointer<GraphComponent> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [safe, b, dynOn] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& p = safe->processor;
            if (r == 1) { setParam (p, pid::band (b, pid::on), 0.0f); safe->selected = -1; }
            else if (r == 2) setParam (p, pid::band (b, pid::dyn), dynOn ? 0.0f : 1.0f);
            else if (r == 3) setParam (p, pid::band (b, pid::gain), 0.0f);
            else if (r >= 300) setParam (p, pid::band (b, pid::slope), (float) (r - 300));
            else if (r >= 200) setParam (p, pid::band (b, pid::channel), (float) (r - 200));
            else if (r >= 100) setParam (p, pid::band (b, pid::type), (float) (r - 100));
        });
}

void GraphComponent::showAddMenu (juce::Point<float> pos)
{
    using namespace juce;
    PopupMenu m;
    m.addSectionHeader ("Add band at " + String (roundToInt (freqForX (pos.x))) + " Hz");
    for (int i = 0; i < numFilterTypes; ++i)
        m.addItem (100 + i, filterTypeNames()[i]);

    const float f = freqForX (pos.x), db = dbForY (pos.y);
    SafePointer<GraphComponent> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [safe, f, db] (int r)
        {
            if (safe == nullptr || r < 100) return;
            const int nb = addBand (safe->processor, r - 100, f, jlimit (-30.0f, 30.0f, db));
            if (nb >= 0) safe->selected = nb;
        });
}
} // namespace tessera
