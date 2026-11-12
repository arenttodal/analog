#include "SubPanel.h"

#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
#include "OspLookAndFeel.h"
#include "ShellProcessor.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

using namespace osp::plugin;

//==============================================================================
namespace subview
{
    double relativeLossDb (int key, bool taper, int taperStart, double slopeDbPerOctave) noexcept
    {
        // SubOsc::keyGain: TAPER, above its start, loses slope x octaves (LEVEL is common to
        // every key, so it is not part of the shape).
        if (! taper || key <= taperStart)
            return 0.0;
        return std::max (0.0, slopeDbPerOctave) * static_cast<double> (key - taperStart) / 12.0;
    }

    bool isBlack (int key) noexcept
    {
        const int n = ((key % 12) + 12) % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    Viewport fit (int lowKey, int highKey) noexcept
    {
        lowKey = std::clamp (lowKey, 0, 127);
        highKey = std::clamp (highKey, lowKey, 127);
        // C1..C8 (85 keys) when the range is inside it; else the same width shifted by whole
        // octaves (from the C nearest C1 that takes both ends in); at the top, G2..G9 (to MIDI 127);
        // a range wider than that: the whole MIDI range. A saved key is never clamped to it.
        if (lowKey >= 24 && highKey <= 108)
            return {};
        if (highKey - lowKey <= 84)
        {
            for (const int first : { 12, 36, 0 })
                if (first <= lowKey && first + 84 >= highKey)
                    return { first, first + 84 };
            if (lowKey >= 43)
                return { 43, 127 };
        }
        return { 0, 127 };
    }

    juce::String noteName (int key)
    {
        return juce::MidiMessage::getMidiNoteName (key, true, true, 4);
    }
} // namespace subview

namespace
{
    const juce::Colour curveColour { 0xffdfe3d2 };
    const juce::Colour waveColour { 0xff8fa39b };

    float value (InstrumentHost& host, const juce::String& id) { return host.parameterValue (id); }

    MiniKnob::Formatter textOf (juce::RangedAudioParameter& p)
    {
        return [&p] (double v) { return p.getText (p.convertTo0to1 (static_cast<float> (v)), 0); };
    }
} // namespace

//==============================================================================
SubResponseDisplay::SubResponseDisplay (InstrumentHost& h, juce::Colour accentColour)
    : host (h), accent (accentColour),
      lowA (parameter (params::subLowKey), [this] (float) { repaint(); }, &h.undo()),
      highA (parameter (params::subHighKey), [this] (float) { repaint(); }, &h.undo()),
      startA (parameter (params::subTaperStart), [this] (float) { repaint(); }, &h.undo()),
      slopeA (parameter (params::subTaperSlope), [this] (float) { repaint(); }, &h.undo()),
      responseA (parameter (params::subResponse), [this] (float) { repaint(); }, &h.undo()),
      waveA (parameter (params::subWave), [this] (float) {
          rebuildWave();
          repaint();
      }, &h.undo()),
      enabledA (parameter (params::subEnabled), [this] (float) { repaint(); }, &h.undo())
{
    setTitle ("Sub range and response");
    setDescription ("SUB's keys and how loud each one plays");
    setTooltip ("The keys SUB plays: drag either end, or the lit span to move it (arrow keys: the low end; "
                "with Shift, the high end). With TAPER: drag the first point for FROM, the second up or down for the slope");
    setWantsKeyboardFocus (true);
    binding::tag (*this, params::subLowKey);
    startTimerHz (30);   // (the keys held: a few ms behind the playing is enough to read)
}

SubResponseDisplay::~SubResponseDisplay()
{
    stopTimer();
    endDrag();   // (a popover closed mid-drag still ends its host gesture)
}

void SubResponseDisplay::pollHeldKeys()
{
    std::bitset<128> now;
    if (isKeyDown != nullptr)
        for (int k = 0; k < 128; ++k)
            now[static_cast<std::size_t> (k)] = isKeyDown (k);
    if (now != held)
    {
        held = now;
        repaint();
    }
}

juce::RangedAudioParameter& SubResponseDisplay::parameter (const juce::String& id) const
{
    auto* p = host.apvts().getParameter (id);
    jassert (p != nullptr);
    return *p;
}

int SubResponseDisplay::lowKey() const { return juce::roundToInt (value (host, params::subLowKey)); }
int SubResponseDisplay::highKey() const { return std::max (lowKey(), juce::roundToInt (value (host, params::subHighKey))); }
int SubResponseDisplay::taperStart() const { return juce::roundToInt (value (host, params::subTaperStart)); }
double SubResponseDisplay::slope() const { return static_cast<double> (value (host, params::subTaperSlope)); }
bool SubResponseDisplay::taper() const { return juce::roundToInt (value (host, params::subResponse)) == 1; }
int SubResponseDisplay::wave() const { return juce::roundToInt (value (host, params::subWave)); }

subview::Viewport SubResponseDisplay::viewport() const
{
    return drag != Handle::none ? frozenView : subview::fit (lowKey(), highKey());
}

double SubResponseDisplay::currentScale() const
{
    // The graph spans the largest loss it shows, in 6 dB steps (3 dB at least): honest, never
    // a steep picture of a gentle slope.
    const auto v = viewport();
    const double most = subview::relativeLossDb (v.last, taper(), taperStart(), slope());
    return std::clamp (std::ceil (most / 6.0) * 6.0, 3.0, 96.0);
}

double SubResponseDisplay::scaleDb() const
{
    return (drag == Handle::slope || drag == Handle::from) ? frozenScale : currentScale();
}

void SubResponseDisplay::resized()
{
    const auto r = getLocalBounds().toFloat();
    const auto inner = r.reduced (12.0f, 0.0f);
    labels = inner.withHeight (13.0f).withBottomY (r.getBottom() - 4.0f);
    bed = inner.withHeight (std::round (0.235f * r.getHeight())).withBottomY (labels.getY() - 1.0f);
    graph = inner.withTop (r.getY() + 12.0f).withBottom (bed.getY() - 14.0f);
    rebuildWave();
}

void SubResponseDisplay::rebuildWave()
{
    // The selected wave's shape, as SubOsc draws it (SAW rises; SQUARE starts high; TRIANGLE
    // starts at its peak): three cycles, a still picture behind the curve.
    wavePath.clear();
    if (graph.isEmpty())
        return;
    const float cycles = 3.0f, amp = 0.33f * graph.getHeight();
    const float mid = graph.getCentreY() + 0.04f * graph.getHeight();
    const int n = std::max (64, juce::roundToInt (graph.getWidth() * 1.5f));
    const int w = wave();
    auto shape = [w] (double phase) {
        phase -= std::floor (phase);
        switch (w)
        {
            case 1: return 4.0 * std::abs (phase - 0.5) - 1.0;    // TRIANGLE
            case 2: return phase < 0.5 ? 1.0 : -1.0;              // SQUARE
            case 3: return 2.0 * phase - 1.0;                     // SAW
            default: return std::sin (juce::MathConstants<double>::twoPi * phase);
        }
    };
    const bool edges = w == 2 || w == 3;
    for (int i = 0; i <= n; ++i)
    {
        const double t = static_cast<double> (i) / n;
        const double phase = t * cycles;
        const float x = graph.getX() + static_cast<float> (t) * graph.getWidth();
        const float y = mid - amp * static_cast<float> (shape (phase));
        if (i == 0)
            wavePath.startNewSubPath (x, y);
        else
        {
            // Vertical edges stay vertical (no slanted steps between samples).
            if (edges && i > 0)
            {
                const double prev = static_cast<double> (i - 1) / n * cycles;
                if (std::floor (prev * (w == 2 ? 2.0 : 1.0)) != std::floor (phase * (w == 2 ? 2.0 : 1.0)))
                    wavePath.lineTo (x, mid - amp * static_cast<float> (shape (prev)));
            }
            wavePath.lineTo (x, y);
        }
    }
}

juce::Rectangle<float> SubResponseDisplay::keyArea (int key) const
{
    const auto v = viewport();
    if (key < v.first || key > v.last)
        return {};
    int whites = 0, before = 0;
    for (int k = v.first; k <= v.last; ++k)
        if (! subview::isBlack (k))
        {
            if (k < key)
                ++before;
            ++whites;
        }
    const float w = bed.getWidth() / static_cast<float> (std::max (1, whites));
    if (! subview::isBlack (key))
        return { bed.getX() + static_cast<float> (before) * w, bed.getY(), w, bed.getHeight() };
    return { bed.getX() + static_cast<float> (before) * w - 0.32f * w, bed.getY(), 0.64f * w, 0.6f * bed.getHeight() };
}

float SubResponseDisplay::keyCentre (int key) const
{
    const auto v = viewport();
    if (key < v.first)
        return bed.getX();
    if (key > v.last)
        return bed.getRight();
    return keyArea (key).getCentreX();
}

float SubResponseDisplay::lossToY (double lossDb, double scale) const
{
    // 0 dB sits a fifth down the graph (room for the handles), the scale's loss at its foot.
    const float top = graph.getY() + 0.2f * graph.getHeight(), bottom = graph.getBottom() - 2.0f;
    return top + static_cast<float> (std::clamp (lossDb / scale, 0.0, 1.0)) * (bottom - top);
}

double SubResponseDisplay::yToLoss (float y, double scale) const
{
    const float top = graph.getY() + 0.2f * graph.getHeight(), bottom = graph.getBottom() - 2.0f;
    return std::clamp (static_cast<double> ((y - top) / std::max (1.0f, bottom - top)), 0.0, 1.0) * scale;
}

float SubResponseDisplay::curveY (int key) const
{
    return lossToY (subview::relativeLossDb (key, taper(), taperStart(), slope()), scaleDb());
}

juce::Point<float> SubResponseDisplay::handlePosition (Handle h) const
{
    const auto v = viewport();
    switch (h)
    {
        case Handle::from:
            return { juce::jlimit (graph.getX(), graph.getRight(), keyCentre (taperStart())), lossToY (0.0, scaleDb()) };
        case Handle::slope:
        {
            // An octave above FROM where that is shown; else the display's edge (its readout
            // names the slope, not a key).
            const int key = std::clamp (taperStart() + 12, v.first, v.last);
            return { keyCentre (key), lossToY (subview::relativeLossDb (key, true, taperStart(), slope()), scaleDb()) };
        }
        case Handle::low:
            return { lowKey() < v.first ? bed.getX() : keyArea (lowKey()).getX(), bed.getY() };
        case Handle::high:
            return { highKey() > v.last ? bed.getRight() : keyArea (highKey()).getRight(), bed.getY() };
        case Handle::span:
        case Handle::none:
            break;
    }
    return {};
}

SubResponseDisplay::Handle SubResponseDisplay::handleAt (juce::Point<float> p) const
{
    // The curve's handles (TAPER only), with a 12-unit target; the keyboard below for the range.
    if (taper())
    {
        const auto f = handlePosition (Handle::from), s = handlePosition (Handle::slope);
        const float df = p.getDistanceFrom (f), ds = p.getDistanceFrom (s);
        const bool slopeShown = taperStart() + 1 <= viewport().last;
        if (df <= 12.0f && (! slopeShown || df <= ds))
            return Handle::from;
        if (slopeShown && ds <= 12.0f)
            return Handle::slope;
    }
    if (p.y < bed.getY() - 12.0f || p.y > labels.getBottom() || p.x < bed.getX() - 8.0f || p.x > bed.getRight() + 8.0f)
        return Handle::none;
    const float lx = handlePosition (Handle::low).x, hx = handlePosition (Handle::high).x;
    const float dl = std::abs (p.x - lx), dh = std::abs (p.x - hx);
    constexpr float band = 7.0f;
    if (dl <= band || dh <= band)
        return dl <= dh ? Handle::low : Handle::high;
    if (p.x > lx && p.x < hx)
        return Handle::span;
    return dl <= dh ? Handle::low : Handle::high;   // (a press beside the range takes the nearer end)
}

int SubResponseDisplay::keyAtX (float x) const
{
    const auto v = viewport();
    for (int k = v.first; k <= v.last; ++k)
        if (subview::isBlack (k))
            if (const auto a = keyArea (k); x >= a.getX() && x < a.getRight())
                return k;
    for (int k = v.first; k <= v.last; ++k)
        if (! subview::isBlack (k) && x < keyArea (k).getRight())
            return k;
    return v.last;
}

juce::String SubResponseDisplay::readout() const
{
    switch (drag)
    {
        case Handle::from: return "FROM " + subview::noteName (taperStart());
        case Handle::slope: return parameter (params::subTaperSlope).getCurrentValueAsText();
        case Handle::low:
        case Handle::high:
        case Handle::span: return subview::noteName (lowKey()) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + subview::noteName (highKey());
        case Handle::none: break;
    }
    return {};
}

//==============================================================================
void SubResponseDisplay::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    constexpr float radius = 7.0f;
    // The well: the oscillator / SPACE display's slate, shaded under its top edge.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f2626), 0.0f, r.getY(), juce::Colour (0xff1a1e1f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.3f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f, r.getY() + 6.0f, false));
    g.fillRoundedRectangle (r.withHeight (8.0f), radius);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (getLocalBounds().reduced (1));
    const auto v = viewport();
    const bool on = value (host, params::subEnabled) >= 0.5f;
    const bool tapered = taper();
    const int lo = lowKey(), hi = highKey();
    const double scale = scaleDb();

    // Grid: the octaves (at each C) and the loss scale's quarters.
    g.setColour (colour::wellGrid.withAlpha (0.7f));
    for (int k = v.first; k <= v.last; ++k)
        if (k % 12 == 0)
            g.fillRect (juce::Rectangle<float> (keyArea (k).getX(), graph.getY(), 1.0f, graph.getHeight()));
    for (int i = 0; i <= 4; ++i)
        g.fillRect (juce::Rectangle<float> (graph.getX(), lossToY (scale * i / 4.0, scale), graph.getWidth(), 1.0f));

    // The wave's shape, quiet behind everything.
    {
        const float alpha = on ? 1.0f : 0.55f;
        const float mid = graph.getCentreY() + 0.04f * graph.getHeight();
        juce::Path fill (wavePath);
        fill.lineTo (graph.getRight(), mid);
        fill.lineTo (graph.getX(), mid);
        fill.closeSubPath();
        g.setColour (waveColour.withAlpha (0.08f * alpha));
        g.fillPath (fill);
        g.setColour (waveColour.withAlpha (0.34f * alpha));
        g.strokePath (wavePath, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The playable range: a faint column over its keys.
    const float rangeX0 = handlePosition (Handle::low).x, rangeX1 = handlePosition (Handle::high).x;
    g.setColour (accent.withAlpha (0.08f));
    g.fillRect (juce::Rectangle<float> (rangeX0, graph.getY(), rangeX1 - rangeX0, bed.getY() - graph.getY()));

    // The response: each shown key's loss; bright where SUB plays, faint and dashed beyond.
    {
        juce::Path below, inside, aboveRange;
        bool startedBelow = false, startedInside = false, startedAbove = false;
        auto add = [] (juce::Path& p, bool& started, float x, float y) {
            if (! started)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
            started = true;
        };
        for (int k = v.first; k <= v.last; ++k)
        {
            const float x = keyCentre (k), y = curveY (k);
            if (k <= lo)
                add (below, startedBelow, x, y);
            if (k >= lo && k <= hi)
                add (inside, startedInside, x, y);
            if (k >= hi)
                add (aboveRange, startedAbove, x, y);
        }
        // The ends of the range meet the line at their edges (where the keyboard's caps stand).
        juce::Path area;
        if (startedInside)
        {
            area = inside;
            area.lineTo (keyCentre (std::min (hi, v.last)), graph.getBottom());
            area.lineTo (keyCentre (std::max (lo, v.first)), graph.getBottom());
            area.closeSubPath();
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (on ? 0.30f : 0.16f), 0.0f, lossToY (0.0, scale), accent.withAlpha (0.02f), 0.0f,
                                                     graph.getBottom(), false));
            g.fillPath (area);
        }
        juce::Path dashed;
        const float dashes[] = { 3.0f, 3.0f };
        for (auto* p : { &below, &aboveRange })
        {
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, *p, dashes, 2);
            g.setColour (curveColour.withAlpha (0.28f));
            g.fillPath (dashed);
        }
        if (startedInside)
        {
            g.setColour (curveColour.withAlpha (on ? 0.95f : 0.6f));
            g.strokePath (inside, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (lo == hi && lo >= v.first && lo <= v.last)   // one key: its point
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ keyCentre (lo), curveY (lo) }));
        }
    }

    // The handles (TAPER): FROM on the flat line, the slope an octave above it.
    if (tapered)
    {
        const auto f = handlePosition (Handle::from);
        const bool fromShown = taperStart() >= v.first && taperStart() <= v.last;
        const float guide[] = { 2.0f, 3.0f };
        juce::Path line, dashed;
        line.startNewSubPath (f.x, f.y + 5.0f);
        line.lineTo (f.x, bed.getY() - 2.0f);
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, guide, 2);
        g.setColour (curveColour.withAlpha (0.22f));
        g.fillPath (dashed);
        auto knob = [&] (juce::Point<float> c, Handle which, bool solid) {
            const bool lit = drag == which || (drag == Handle::none && hover == which);
            const float rr = lit ? 5.5f : 4.5f;
            if (lit)
            {
                g.setColour (accent.withAlpha (0.35f));
                g.fillEllipse (juce::Rectangle<float> (2.0f * rr + 6.0f, 2.0f * rr + 6.0f).withCentre (c));
            }
            g.setColour (juce::Colour (0xff1f2626));
            g.fillEllipse (juce::Rectangle<float> (2.0f * rr, 2.0f * rr).withCentre (c));
            g.setColour (curveColour.withAlpha (solid ? 1.0f : 0.5f));
            g.drawEllipse (juce::Rectangle<float> (2.0f * rr, 2.0f * rr).withCentre (c), 1.5f);
        };
        if (taperStart() + 1 <= v.last)
            knob (handlePosition (Handle::slope), Handle::slope, true);
        knob (f, Handle::from, fromShown);
    }

    // The keyboard: ivory and graphite, the range lit, the rest dimmed; its ends' caps.
    for (int k = v.first; k <= v.last; ++k)
    {
        if (subview::isBlack (k))
            continue;
        const auto a = keyArea (k).reduced (0.5f, 0.0f);
        const bool in = k >= lo && k <= hi;
        g.setColour (in ? accent.interpolatedWith (juce::Colours::white, 0.62f) : juce::Colour (0xffe8e3d9));
        g.fillRoundedRectangle (a, 1.5f);
        if (! in)
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));   // (outside the range: quieter, still the ivory keyboard)
            g.fillRoundedRectangle (a, 1.5f);
        }
    }
    for (int k = v.first; k <= v.last; ++k)
        if (subview::isBlack (k))
        {
            const bool in = k >= lo && k <= hi;
            g.setColour (in ? accent.darker (0.55f) : juce::Colour (0xff2a2927));
            g.fillRoundedRectangle (keyArea (k), 1.0f);
        }
    // The keys being played: lit in the accent (a quieter tint outside SUB's range, where it is
    // silent), with their place on the curve - where the playing sits in the range and response.
    if (held.any())
    {
        const auto keyLit = [&] (int k) {
            const bool in = k >= lo && k <= hi, black = subview::isBlack (k);
            const auto a = black ? keyArea (k) : keyArea (k).reduced (0.5f, 0.0f);
            // (darker than the range's pale tint on ivory, brighter than graphite on black)
            const auto lit = black ? accent.brighter (0.5f) : accent.darker (0.2f);
            g.setColour (in ? lit : lit.withAlpha (0.5f));
            g.fillRoundedRectangle (a, black ? 1.0f : 1.5f);
            // A small lamp at the key's foot: readable at a glance, whatever the tint around it.
            const float d = std::min (4.0f, a.getWidth() - 2.0f);
            g.setColour (juce::Colour (0xfffff1e2).withAlpha (in ? 0.95f : 0.6f));
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ a.getCentreX(), a.getBottom() - d - 1.5f }));
        };
        for (int k = v.first; k <= v.last; ++k)
            if (held[static_cast<std::size_t> (k)] && ! subview::isBlack (k))
                keyLit (k);
        for (int k = v.first; k <= v.last; ++k)
            if (held[static_cast<std::size_t> (k)] && subview::isBlack (k))
                keyLit (k);
        for (int k = v.first; k <= v.last; ++k)
            if (held[static_cast<std::size_t> (k)] && k >= lo && k <= hi)
            {
                const juce::Point<float> c { keyCentre (k), curveY (k) };
                g.setColour (accent.withAlpha (0.35f));
                g.fillRect (juce::Rectangle<float> (c.x - 0.5f, c.y, 1.0f, bed.getY() - c.y));
                g.setColour (accent.brighter (0.35f));
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (c));
            }
    }
    g.setFont (fonts::label (8.5f));
    g.setColour (colour::wellText);
    for (int k = v.first; k <= v.last; ++k)
        if (k % 12 == 0)
            g.drawText (subview::noteName (k), keyArea (k).withY (labels.getY()).withHeight (labels.getHeight()).expanded (8.0f, 0.0f),
                        juce::Justification::centred, false);
    for (const auto which : { Handle::low, Handle::high })
    {
        const float x = handlePosition (which).x;
        const bool lit = drag == which || drag == Handle::span || (drag == Handle::none && hover == which);
        g.setColour (accent.brighter (lit ? 0.35f : 0.15f));
        g.fillRoundedRectangle (juce::Rectangle<float> (x - 1.25f, bed.getY() - 3.0f, 2.5f, bed.getHeight() + 3.0f), 1.0f);
        g.fillRoundedRectangle (juce::Rectangle<float> (which == Handle::low ? x - 1.25f : x - 7.25f, bed.getY() - 7.0f, 8.5f, 4.5f), 1.5f);
    }

    // While a handle moves: one small readout above it (and the graph's scale, quietly).
    if (const auto text = readout(); text.isNotEmpty())
    {
        const auto at = drag == Handle::from || drag == Handle::slope ? handlePosition (drag)
                                                                     : juce::Point<float> (0.5f * (rangeX0 + rangeX1), bed.getY() - 10.0f);
        g.setFont (fonts::label (10.0f));
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 14.0f;
        auto pill = juce::Rectangle<float> (w, 17.0f).withCentre ({ at.x, at.y - 16.0f });
        pill = pill.constrainedWithin (r.reduced (4.0f));
        g.setColour (juce::Colour (0xf02c3434));
        g.fillRoundedRectangle (pill, 8.5f);
        g.setColour (curveColour);
        g.drawText (text, pill, juce::Justification::centred, false);
    }
    if (drag == Handle::slope || drag == Handle::from || hover == Handle::slope || hover == Handle::from)
    {
        g.setFont (fonts::label (8.0f));
        g.setColour (colour::wellText.withAlpha (0.8f));
        g.drawText ("-" + juce::String (juce::roundToInt (scale)) + " dB", graph.withTrimmedBottom (1.0f).removeFromBottom (11.0f).removeFromRight (44.0f),
                    juce::Justification::centredRight, false);
    }
    if (hasKeyboardFocus (false))
    {
        g.setColour (accent.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (1.5f), radius - 1.0f, 1.0f);
    }
}

//==============================================================================
void SubResponseDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = handleAt (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h == Handle::slope ? juce::MouseCursor::UpDownResizeCursor
                        : h == Handle::span ? juce::MouseCursor::DraggingHandCursor
                        : h == Handle::none ? juce::MouseCursor::NormalCursor
                                            : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void SubResponseDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hover != Handle::none)
    {
        hover = Handle::none;
        repaint();
    }
}

void SubResponseDisplay::beginDrag (Handle h, juce::Point<float> at)
{
    frozenView = subview::fit (lowKey(), highKey());
    frozenScale = currentScale();
    drag = h;
    pressAt = at;
    switch (h)
    {
        case Handle::low: lowA.beginGesture(); break;
        case Handle::high: highA.beginGesture(); break;
        case Handle::span:
            spanLow0 = lowKey();
            spanHigh0 = highKey();
            spanKey0 = keyAtX (at.x);
            lowA.beginGesture();
            highA.beginGesture();
            break;
        case Handle::from: startA.beginGesture(); break;
        case Handle::slope: slopeA.beginGesture(); break;
        case Handle::none: break;
    }
}

void SubResponseDisplay::endDrag()
{
    switch (drag)
    {
        case Handle::low: lowA.endGesture(); break;
        case Handle::high: highA.endGesture(); break;
        case Handle::span:
            lowA.endGesture();
            highA.endGesture();
            break;
        case Handle::from: startA.endGesture(); break;
        case Handle::slope: slopeA.endGesture(); break;
        case Handle::none: break;
    }
    drag = Handle::none;
    coincident = false;
}

void SubResponseDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    auto h = handleAt (e.position);
    if (h == Handle::none)
        return;
    // Two ends drawn together (one key): the drag's direction says which one moves.
    if ((h == Handle::low || h == Handle::high) && lowKey() == highKey())
    {
        coincident = true;
        pressAt = e.position;
        frozenView = subview::fit (lowKey(), highKey());
        frozenScale = currentScale();
        repaint();
        return;
    }
    beginDrag (h, e.position);
    mouseDrag (e);
}

void SubResponseDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (coincident && drag == Handle::none)
    {
        if (std::abs (e.position.x - pressAt.x) < 2.0f)
            return;
        beginDrag (e.position.x < pressAt.x ? Handle::low : Handle::high, pressAt);
        coincident = false;
    }
    switch (drag)
    {
        case Handle::low:
            lowA.setValueAsPartOfGesture (static_cast<float> (std::min (keyAtX (e.position.x), highKey())));
            break;
        case Handle::high:
            highA.setValueAsPartOfGesture (static_cast<float> (std::max (keyAtX (e.position.x), lowKey())));
            break;
        case Handle::span:
        {
            // The whole span, its width kept, one semitone at a time, inside 0..127.
            const int width = spanHigh0 - spanLow0;
            const int newLow = std::clamp (spanLow0 + keyAtX (e.position.x) - spanKey0, 0, 127 - width);
            if (newLow >= lowKey())
            {
                highA.setValueAsPartOfGesture (static_cast<float> (newLow + width));
                lowA.setValueAsPartOfGesture (static_cast<float> (newLow));
            }
            else
            {
                lowA.setValueAsPartOfGesture (static_cast<float> (newLow));
                highA.setValueAsPartOfGesture (static_cast<float> (newLow + width));
            }
            break;
        }
        case Handle::from:
        {
            const auto v = frozenView;
            int key = keyAtX (e.position.x);
            if (e.position.x < bed.getX())
                key = v.first;
            startA.setValueAsPartOfGesture (static_cast<float> (std::clamp (key, 0, 127)));
            break;
        }
        case Handle::slope:
        {
            // Down: steeper; up: gentler. The loss at the handle's key, read from the pointer
            // on the scale the drag began with.
            const auto v = frozenView;
            const int distance = std::clamp (taperStart() + 12, v.first, v.last) - taperStart();
            if (distance > 0)
            {
                const double loss = yToLoss (e.position.y, frozenScale);
                const double newSlope = std::clamp (loss * 12.0 / distance, 0.0, 12.0);
                slopeA.setValueAsPartOfGesture (static_cast<float> (std::round (newSlope * 10.0) / 10.0));
            }
            break;
        }
        case Handle::none: break;
    }
    repaint();
}

void SubResponseDisplay::mouseUp (const juce::MouseEvent&)
{
    endDrag();
    repaint();
}

void SubResponseDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    // A curve handle's double-click resets that setting (as a knob's does).
    const auto h = handleAt (e.position);
    auto reset = [] (juce::ParameterAttachment& a, juce::RangedAudioParameter& p) { a.setValueAsCompleteGesture (p.convertFrom0to1 (p.getDefaultValue())); };
    if (h == Handle::from)
        reset (startA, parameter (params::subTaperStart));
    else if (h == Handle::slope)
        reset (slopeA, parameter (params::subTaperSlope));
}

bool SubResponseDisplay::keyPressed (const juce::KeyPress& k)
{
    // (By key code: KeyPress's == against a code is false whenever a modifier is held, which
    // made the old keybed's Shift + arrow unreachable.)
    const int code = k.getKeyCode();
    const int step = code == juce::KeyPress::leftKey ? -1 : code == juce::KeyPress::rightKey ? 1 : 0;
    if (step == 0 || k.getModifiers().isCommandDown() || k.getModifiers().isAltDown())
        return false;
    if (k.getModifiers().isShiftDown())
        highA.setValueAsCompleteGesture (static_cast<float> (std::clamp (highKey() + step, lowKey(), 127)));
    else
        lowA.setValueAsCompleteGesture (static_cast<float> (std::clamp (lowKey() + step, 0, highKey())));
    return true;
}

//==============================================================================
SubGritValve::SubGritValve()
{
    setInterceptsMouseClicks (false, false);
    setTitle ("GRIT");
}

void SubGritValve::setAmount (float grit, bool enabled)
{
    grit = std::clamp (grit, 0.0f, 1.0f);
    if (juce::exactlyEqual (grit, gritNow) && enabled == on)
        return;
    gritNow = grit;
    on = enabled;
    glowNow = std::pow (grit, 0.8f) * (on ? 1.0f : 0.35f);
    repaint();
}

void SubGritValve::paint (juce::Graphics& g)
{
    // A small valve drawn in the instrument's materials: a dark glass envelope, its plates and
    // support wires, three filaments whose warmth follows GRIT, a soft glow inside the glass,
    // a graphite base. Static: it changes with the parameter, never by itself.
    const auto r = getLocalBounds().toFloat();
    const float alpha = on ? 1.0f : 0.5f;
    const float b = glowNow;
    const float w = std::min (r.getWidth() * 0.82f, r.getHeight() * 0.36f);
    const auto base = juce::Rectangle<float> (w * 0.92f, r.getHeight() * 0.16f).withCentre ({ r.getCentreX(), r.getBottom() - r.getHeight() * 0.1f });
    auto glass = juce::Rectangle<float> (r.getCentreX() - 0.5f * w, r.getY() + r.getHeight() * 0.06f, w, base.getY() - r.getY() - r.getHeight() * 0.04f);

    // The socket's recess in the ceramic (the approved composition's pocket, quietly).
    {
        const auto pocket = r.reduced (1.0f);
        g.setColour (juce::Colour (0x16000000));
        g.fillRoundedRectangle (pocket, 0.5f * pocket.getWidth());
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawRoundedRectangle (pocket.translated (0.0f, 0.5f), 0.5f * pocket.getWidth(), 0.8f);
        g.setColour (juce::Colour (0x22000000));
        g.drawRoundedRectangle (pocket, 0.5f * pocket.getWidth(), 0.8f);
    }

    juce::Path envelope;
    envelope.addRoundedRectangle (glass.getX(), glass.getY(), glass.getWidth(), glass.getHeight(), 0.5f * w, 0.5f * w, true, true, false, false);
    // Glass: smoked, the glow behind it.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3632).withAlpha (alpha), glass.getX(), 0.0f, juce::Colour (0xff1d1b19).withAlpha (alpha),
                                             glass.getRight(), 0.0f, false));
    g.fillPath (envelope);
    if (b > 0.0f)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (envelope);
        const auto centre = glass.getCentre().translated (0.0f, glass.getHeight() * 0.12f);
        juce::ColourGradient halo (juce::Colour (0xffffa850).withAlpha ((0.35f + 0.45f * b) * b * alpha), centre.x, centre.y, juce::Colour (0x00ff8a30),
                                   centre.x + (0.6f + 0.35f * b) * w, centre.y, true);
        g.setGradientFill (halo);
        g.fillRect (glass);
    }
    // Plates and wires.
    const auto plates = glass.reduced (w * 0.26f, glass.getHeight() * 0.22f).translated (0.0f, glass.getHeight() * 0.08f);
    g.setColour (juce::Colour (0xff4a4440).withAlpha (0.9f * alpha));
    g.fillRect (plates.withWidth (w * 0.07f));
    g.fillRect (plates.withLeft (plates.getRight() - w * 0.07f));
    g.setColour (juce::Colour (0xff8a8178).withAlpha (0.55f * alpha));
    for (float t : { 0.32f, 0.68f })
        g.fillRect (juce::Rectangle<float> (glass.getX() + t * w - 0.4f, glass.getY() + glass.getHeight() * 0.18f, 0.8f, plates.getBottom() - glass.getY() - glass.getHeight() * 0.18f));
    for (float t : { 0.38f, 0.62f })
        g.fillRect (juce::Rectangle<float> (plates.getX(), plates.getY() + t * plates.getHeight(), plates.getWidth(), 0.7f));
    // Filaments: dark copper at rest, amber as GRIT rises.
    const auto cold = juce::Colour (0xff6b4a35), hot = juce::Colour (0xffffb35c);
    for (int i = 0; i < 3; ++i)
    {
        const float x = plates.getCentreX() + (static_cast<float> (i) - 1.0f) * w * 0.12f;
        const auto line = juce::Rectangle<float> (x - 0.6f, plates.getY() + plates.getHeight() * 0.18f, 1.2f, plates.getHeight() * 0.64f);
        if (b > 0.02f)
        {
            g.setColour (hot.withAlpha (0.28f * b * alpha));
            g.fillRoundedRectangle (line.expanded (1.6f, 1.0f), 1.6f);
        }
        g.setColour (cold.interpolatedWith (hot, b).withAlpha ((0.55f + 0.45f * b) * alpha));
        g.fillRoundedRectangle (line, 0.6f);
    }
    // The glass's edge and its light.
    g.setColour (juce::Colours::white.withAlpha (0.10f * alpha));
    g.strokePath (envelope, juce::PathStrokeType (0.9f));
    g.setColour (juce::Colours::white.withAlpha (0.16f * alpha));
    g.fillRoundedRectangle (juce::Rectangle<float> (glass.getX() + w * 0.16f, glass.getY() + w * 0.32f, w * 0.1f, glass.getHeight() * 0.5f), w * 0.05f);
    // The base: graphite with a lighter rim.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4b4743).withAlpha (alpha), 0.0f, base.getY(), juce::Colour (0xff23211f).withAlpha (alpha), 0.0f,
                                             base.getBottom(), false));
    g.fillRoundedRectangle (base, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.12f * alpha));
    g.drawHorizontalLine (juce::roundToInt (base.getY() + 1.0f), base.getX() + 2.0f, base.getRight() - 2.0f);
}

//==============================================================================
SubPanel::SubPanel (InstrumentHost& h) : MiniPanel ("SUB"), host (h), accent (design::colour::macro (2))
{
    using namespace params;
    auto& state = host.apvts();
    auto segments = [&] (const juce::String& id, juce::StringArray items, const char* tooltip) {
        auto s = std::make_unique<SegmentedControl> (*state.getParameter (id), std::move (items));
        s->setAccent (accent);
        s->setTooltip (tooltip);
        binding::tag (*s, id);
        addAndMakeVisible (*s);
        return s;
    };
    waveSegments = segments (subWave, { "SINE", "TRI", "SQUARE", "SAW" }, "SUB's wave (band-limited)");
    octaveSegments = segments (subOctave, { "-1", "-2" }, "One or two octaves below the key");
    responseSegments = segments (subResponse, { "FLAT", "TAPER" }, "FLAT: every key the same. TAPER: quieter above FROM (the low end stays full)");
    auto knob = [&] (const juce::String& id, const char* caption, bool small) {
        auto* p = state.getParameter (id);
        auto k = std::make_unique<MiniKnob> (state, id, caption, textOf (*p));
        k->setArcColour (accent);
        k->setSmall (small);
        addAndMakeVisible (*k);
        return k;
    };
    grit = knob (subGrit, "GRIT", false);
    level = knob (subLevel, "LEVEL", false);
    from = knob (subTaperStart, "FROM", true);
    slopeKnobPtr = knob (subTaperSlope, "dB / OCT", true);
    responseDisplay = std::make_unique<SubResponseDisplay> (host, accent);
    if (auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host))
        responseDisplay->isKeyDown = [shell] (int key) { return shell->keyboardState.isNoteOnForChannels (0xffff, key); };
    addAndMakeVisible (*responseDisplay);
    gritValve = std::make_unique<SubGritValve>();
    addAndMakeVisible (*gritValve);
    // The valve and FLAT's dimming follow the parameters (message thread; no polling).
    auto follow = [this] (const juce::String& id) {
        auto a = std::make_unique<juce::ParameterAttachment> (*host.apvts().getParameter (id), [this] (float) { updateDependents(); }, nullptr);
        return a;
    };
    gritA = follow (subGrit);
    enabledA = follow (subEnabled);
    responseA = follow (subResponse);
    updateDependents();
}

SubPanel::~SubPanel() = default;

void SubPanel::updateDependents()
{
    if (gritValve == nullptr || from == nullptr)
        return;
    gritValve->setAmount (host.parameterValue (params::subGrit) / 100.0f, host.parameterValue (params::subEnabled) >= 0.5f);
    // FLAT: FROM and dB / OCT stay in place, readable and editable, dimmed (their values kept).
    const bool tapered = juce::roundToInt (host.parameterValue (params::subResponse)) == 1;
    from->setAlpha (tapered ? 1.0f : 0.42f);
    slopeKnobPtr->setAlpha (tapered ? 1.0f : 0.42f);
}

void SubPanel::refreshContent()
{
    updateDependents();
}

juce::Point<int> SubPanel::cardSize() const
{
    // Header 32, WAVE / OCT 24, display 158, the controls 94, gaps; the same in FLAT and TAPER.
    return { width, 32 + 24 + 10 + 158 + 12 + 94 + 10 };
}

void SubPanel::layoutContent (juce::Rectangle<int> area)
{
    // Top: WAVE (most of the width) and a compact OCT, one baseline.
    auto top = area.removeFromTop (24);
    waveCaption = top.removeFromLeft (40);
    auto octave = top.removeFromRight (84);
    octCaption = top.removeFromRight (30);
    top.removeFromRight (8);
    topDivider = juce::Rectangle<float> (static_cast<float> (top.getRight()) - 0.5f, static_cast<float> (top.getY()) + 4.0f, 1.0f, 16.0f);
    top.removeFromRight (9);
    waveSegments->setBounds (top);
    octaveSegments->setBounds (octave);
    area.removeFromTop (10);
    responseDisplay->setBounds (area.removeFromTop (158));
    area.removeFromTop (12);

    // Bottom: the valve, GRIT and LEVEL; RESPONSE over FROM and dB / OCT.
    auto bottom = area.removeFromTop (94);
    auto valveArea = bottom.removeFromLeft (38);
    gritValve->setBounds (valveArea.withSizeKeepingCentre (32, 76));
    bottom.removeFromLeft (8);
    leftDivider = juce::Rectangle<float> (static_cast<float> (bottom.getX()), static_cast<float> (bottom.getY()) + 6.0f, 1.0f, static_cast<float> (bottom.getHeight()) - 12.0f);
    bottom.removeFromLeft (6);
    auto knobs = bottom.removeFromLeft (174);
    grit->setBounds (knobs.removeFromLeft (87).withSizeKeepingCentre (87, 86));
    level->setBounds (knobs.withSizeKeepingCentre (87, 86));
    bottom.removeFromLeft (6);
    rightDivider = juce::Rectangle<float> (static_cast<float> (bottom.getX()), static_cast<float> (bottom.getY()) + 6.0f, 1.0f, static_cast<float> (bottom.getHeight()) - 12.0f);
    bottom.removeFromLeft (12);
    responseCaption = bottom.removeFromTop (13);
    responseSegments->setBounds (bottom.removeFromTop (22));
    bottom.removeFromTop (3);
    const int half = bottom.getWidth() / 2;
    from->setBounds (bottom.removeFromLeft (half));
    knobDivider = juce::Rectangle<float> (static_cast<float> (bottom.getX()), static_cast<float> (bottom.getY()) + 10.0f, 1.0f, static_cast<float> (bottom.getHeight()) - 20.0f);
    slopeKnobPtr->setBounds (bottom);
}

void SubPanel::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    g.setColour (palette::textDim);
    g.setFont (fonts::label (9.5f));
    g.drawText ("WAVE", waveCaption.withTrimmedLeft (2), juce::Justification::centredLeft, false);
    g.drawText ("OCT", octCaption, juce::Justification::centredLeft, false);
    g.drawText ("RESPONSE", responseCaption.withTrimmedLeft (2), juce::Justification::centredLeft, false);
    g.setColour (palette::hairline);
    for (const auto& d : { topDivider, leftDivider, rightDivider, knobDivider })
        g.fillRect (d);
}

} // namespace andor::analog
