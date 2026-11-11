#include "ShaperDnaPanel.h"

#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
#include "OspLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

using namespace osp::plugin;

namespace
{
    float value (InstrumentHost& host, const juce::String& id) { return host.parameterValue (id); }
    juce::RangedAudioParameter& parameter (InstrumentHost& host, const juce::String& id) { return *host.apvts().getParameter (id); }
    bool isBlack (int key) noexcept
    {
        const int n = ((key % 12) + 12) % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
    juce::String noteName (int key) { return juce::MidiMessage::getMidiNoteName (key, true, true, 4); }
}

//==============================================================================
namespace dnaview
{
    juce::Colour profileColour (int profile) noexcept
    {
        switch (profile)
        {
            case 0: return juce::Colour (0xffc9824f);   // copper
            case 1: return juce::Colour (0xff86a0b4);   // slate
            default: return juce::Colour (0xff8fae8b);  // sage
        }
    }

    ShaperDnaSettings settingsOf (InstrumentHost& host)
    {
        using namespace params;
        ShaperDnaSettings d;
        d.enabled = value (host, dnaEnabled) >= 0.5f;
        d.assignment = juce::roundToInt (value (host, dnaAssignment)) == 1 ? ShaperDnaSettings::Assignment::keyZones : ShaperDnaSettings::Assignment::cycle;
        d.profiles = 2 + std::clamp (juce::roundToInt (value (host, dnaProfiles)), 0, 1);
        d.notesPerProfile = 1 + std::clamp (juce::roundToInt (value (host, dnaNotesPerProfile)), 0, 2);
        d.splitAB = juce::roundToInt (value (host, dnaSplitAB));
        d.splitBC = juce::roundToInt (value (host, dnaSplitBC));
        for (int p = 0; p < 3; ++p)
        {
            d.pattern[static_cast<std::size_t> (p)] = juce::roundToInt (value (host, dnaProfileId (p, "pattern"))) - 1;
            d.rate[static_cast<std::size_t> (p)] = juce::roundToInt (value (host, dnaProfileId (p, "rate"))) - 1;
        }
        return d.sanitised();
    }

    osp::ShaperParams shaperOf (InstrumentHost& host)
    {
        osp::ShaperParams s;
        s.pattern = std::clamp (juce::roundToInt (value (host, "movement.shaper.pattern")), 0, osp::RhythmicShaper::patternCount - 1);
        s.rate = static_cast<osp::ShaperRate> (std::clamp (juce::roundToInt (value (host, "movement.shaper.rate")), 0, 5));
        s.smooth = 0.01 * value (host, "movement.shaper.smooth");
        s.custom = value (host, "movement.shaper.custom") >= 0.5f;
        if (s.custom)
            s.customSteps = host.laneShaperPattern (0);
        return s;
    }
} // namespace dnaview

//==============================================================================
ShaperDnaDisplay::ShaperDnaDisplay (InstrumentHost& h)
    : host (h),
      splitABA (parameter (h, params::dnaSplitAB), [this] (float) { repaint(); }, &h.undo()),
      splitBCA (parameter (h, params::dnaSplitBC), [this] (float) { repaint(); }, &h.undo())
{
    setTitle ("Shaper DNA display");
    setTooltip ("Each profile's rhythm over one bar. KEY ZONES: drag the handles to move the splits");
    binding::tag (*this, params::dnaSplitAB);
}

void ShaperDnaDisplay::resized()
{
    auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
    strip = r.removeFromBottom (30.0f);
    r.removeFromBottom (6.0f);
    lanes = r;
}

juce::Rectangle<float> ShaperDnaDisplay::laneArea (int profile) const
{
    const float h = lanes.getHeight() / 3.0f;
    return { lanes.getX(), lanes.getY() + h * static_cast<float> (profile), lanes.getWidth(), h };
}

float ShaperDnaDisplay::keyX (int key) const noexcept
{
    // Semitones spread evenly (a mini keyboard: the zones read as ranges, not as a piano).
    const float t = static_cast<float> (key - firstKey) / static_cast<float> (lastKey - firstKey + 1);
    return strip.getX() + 18.0f + t * (strip.getWidth() - 18.0f);
}

int ShaperDnaDisplay::keyAtX (float x) const noexcept
{
    const float t = (x - strip.getX() - 18.0f) / (strip.getWidth() - 18.0f);
    return std::clamp (firstKey + static_cast<int> (std::floor (t * static_cast<float> (lastKey - firstKey + 1))), 0, 127);
}

juce::Point<float> ShaperDnaDisplay::splitHandle (Split s) const
{
    const auto d = dnaview::settingsOf (host);
    // A split sits on the boundary after its key (A plays up to splitAB inclusive).
    const int key = s == Split::ab ? d.splitAB : d.splitBC;
    const float x = std::clamp (keyX (key + 1), strip.getX() + 18.0f, strip.getRight());
    return { x, strip.getCentreY() };
}

ShaperDnaDisplay::Split ShaperDnaDisplay::splitAt (juce::Point<float> p) const
{
    const auto d = dnaview::settingsOf (host);
    if (d.assignment != ShaperDnaSettings::Assignment::keyZones || ! strip.expanded (0.0f, 6.0f).contains (p))
        return Split::none;
    const float dab = std::abs (p.x - splitHandle (Split::ab).x);
    const float dbc = d.profiles == 3 ? std::abs (p.x - splitHandle (Split::bc).x) : 1.0e9f;
    if (std::min (dab, dbc) > 10.0f)
        return Split::none;
    return dab <= dbc ? Split::ab : Split::bc;
}

juce::String ShaperDnaDisplay::cycleText() const
{
    const auto d = dnaview::settingsOf (host);
    if (d.assignment != ShaperDnaSettings::Assignment::cycle)
        return {};
    juce::String s;
    for (std::uint64_t i = 0; i < 12; ++i)
        s << juce::String::charToString (static_cast<juce::juce_wchar> ('A' + shaperDna::cycleOf (d, i)));
    return s;
}

juce::String ShaperDnaDisplay::stateKey() const
{
    // What the picture depends on: DNA's settings and the SHAPER it follows.
    const auto d = dnaview::settingsOf (host);
    const auto s = dnaview::shaperOf (host);
    juce::String key;
    key << (d.enabled ? 1 : 0) << static_cast<int> (d.assignment) << d.profiles << d.notesPerProfile << ':' << d.splitAB << ':' << d.splitBC;
    for (int p = 0; p < 3; ++p)
        key << ':' << d.pattern[static_cast<std::size_t> (p)] << '/' << d.rate[static_cast<std::size_t> (p)];
    key << '|' << s.pattern << static_cast<int> (s.rate) << ':' << juce::String (s.smooth, 3) << (s.custom ? 1 : 0);
    if (s.custom)
        for (const auto& st : s.customSteps)
            key << juce::String (st.start, 2) << juce::String (st.end, 2) << static_cast<int> (st.shape);
    return key;
}

void ShaperDnaDisplay::refresh()
{
    if (stateKey() != drawnFor)
        repaint();
}

void ShaperDnaDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    constexpr float radius = 7.0f;
    // The well: SUB's and the oscillators' slate, shaded under its top edge.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f2626), 0.0f, r.getY(), juce::Colour (0xff1a1e1f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.3f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f, r.getY() + 6.0f, false));
    g.fillRoundedRectangle (r.withHeight (8.0f), radius);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);

    const auto d = dnaview::settingsOf (host);
    const auto shaper = dnaview::shaperOf (host);
    const float dim = d.enabled ? 1.0f : 0.55f;   // (OFF: the same picture, quieter)

    // The profiles' curves over one bar (four quarter notes), with the bar's beats faintly.
    for (int p = 0; p < 3; ++p)
    {
        const auto lane = laneArea (p).reduced (0.0f, 3.0f);
        const bool used = p < d.profiles;
        const auto colour = dnaview::profileColour (p).withMultipliedAlpha ((used ? 1.0f : 0.28f) * dim);
        g.setColour (colour);
        g.setFont (fonts::label (10.0f));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + p)), lane.withWidth (14.0f), juce::Justification::centred, false);
        const auto plot = lane.withTrimmedLeft (18.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int beat = 1; beat < 4; ++beat)
            g.fillRect (juce::Rectangle<float> (plot.getX() + plot.getWidth() * static_cast<float> (beat) / 4.0f, plot.getY(), 1.0f, plot.getHeight()));
        const auto profile = shaperDna::resolve (d, p, shaper);
        juce::Path curve, fill;
        constexpr int points = 192;
        for (int i = 0; i <= points; ++i)
        {
            const double q = 4.0 * static_cast<double> (i) / points;
            const float v = shaperDna::shapeAt (profile, q, shaper.smooth, 0.0);
            const float x = plot.getX() + plot.getWidth() * static_cast<float> (i) / points;
            const float y = plot.getBottom() - v * plot.getHeight();
            if (i == 0)
            {
                curve.startNewSubPath (x, y);
                fill.startNewSubPath (x, plot.getBottom());
            }
            else
                curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.closeSubPath();
        g.setColour (colour.withMultipliedAlpha (0.14f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Under them: KEY ZONES' keyboard and splits, or CYCLE's sequence.
    if (d.assignment == ShaperDnaSettings::Assignment::keyZones)
    {
        const auto bed = strip.withTrimmedLeft (18.0f).withTrimmedBottom (10.0f);
        for (int k = firstKey; k <= lastKey; ++k)
        {
            const float x0 = keyX (k), x1 = keyX (k + 1);
            const int zone = shaperDna::zoneOf (d, k);
            const auto key = juce::Rectangle<float> (x0, bed.getY(), x1 - x0, bed.getHeight());
            g.setColour (dnaview::profileColour (zone).withMultipliedAlpha ((isBlack (k) ? 0.42f : 0.62f) * dim));
            g.fillRect (isBlack (k) ? key.withTrimmedBottom (0.4f * key.getHeight()) : key);
            if (k % 12 == 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.fillRect (juce::Rectangle<float> (x0, bed.getBottom() - 4.0f, 1.0f, 4.0f));
                g.setFont (fonts::label (8.0f));
                g.drawText (noteName (k), juce::Rectangle<float> (x0 - 2.0f, bed.getBottom() + 1.0f, 24.0f, 9.0f), juce::Justification::centredLeft, false);
            }
        }
        for (const auto s : { Split::ab, Split::bc })
        {
            if (s == Split::bc && d.profiles < 3)
                continue;
            const auto h = splitHandle (s);
            const bool lit = drag == s || hover == s;
            g.setColour (juce::Colour (0xfff0ece4).withAlpha (lit ? 1.0f : 0.85f));
            g.fillRect (juce::Rectangle<float> (h.x - 1.0f, bed.getY() - 3.0f, 2.0f, bed.getHeight() + 6.0f));
            g.fillRoundedRectangle (juce::Rectangle<float> (7.0f, 9.0f).withCentre ({ h.x, bed.getY() - 3.0f }), 2.0f);
            if (lit)
            {
                const int key = s == Split::ab ? d.splitAB : d.splitBC;
                g.setFont (fonts::label (9.0f));
                g.drawText (noteName (key) + " | " + noteName (key + 1), juce::Rectangle<float> (h.x - 40.0f, lanes.getBottom() - 12.0f, 80.0f, 11.0f), juce::Justification::centred, false);
            }
        }
    }
    else
    {
        // CYCLE: the profiles note by note (A A B B C C ...).
        const auto row = strip.withTrimmedLeft (18.0f).withSizeKeepingCentre (strip.getWidth() - 18.0f, 18.0f);
        constexpr int shown = 12;
        const float w = row.getWidth() / static_cast<float> (shown);
        g.setFont (fonts::label (9.5f));
        for (int i = 0; i < shown; ++i)
        {
            const int p = shaperDna::cycleOf (d, static_cast<std::uint64_t> (i));
            const auto cell = juce::Rectangle<float> (row.getX() + w * static_cast<float> (i), row.getY(), w, row.getHeight()).reduced (1.5f, 0.0f);
            g.setColour (dnaview::profileColour (p).withMultipliedAlpha (0.8f * dim));
            g.fillRoundedRectangle (cell, 3.0f);
            g.setColour (juce::Colour (0xff1a1e1f));
            g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + p)), cell, juce::Justification::centred, false);
        }
    }

    drawnFor = stateKey();   // (what this paint showed, for refresh())
}

void ShaperDnaDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto now = splitAt (e.position);
    setMouseCursor (now != Split::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    if (now != hover)
    {
        hover = now;
        repaint();
    }
}

void ShaperDnaDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = splitAt (e.position);
    if (drag == Split::ab)
        splitABA.beginGesture();
    else if (drag == Split::bc)
        splitBCA.beginGesture();
}

void ShaperDnaDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Split::none)
        return;
    const auto d = dnaview::settingsOf (host);
    // The boundary moves to the key boundary nearest the pointer: never crossing the other
    // split (each zone keeps one key at least).
    const int key = keyAtX (e.position.x) - 1;
    if (drag == Split::ab)
    {
        const int hi = d.profiles == 3 ? d.splitBC - 1 : 126;
        splitABA.setValueAsPartOfGesture (static_cast<float> (std::clamp (key, 0, hi)));
    }
    else
        splitBCA.setValueAsPartOfGesture (static_cast<float> (std::clamp (key, d.splitAB + 1, 127)));
    repaint();
}

void ShaperDnaDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drag == Split::ab)
        splitABA.endGesture();
    else if (drag == Split::bc)
        splitBCA.endGesture();
    drag = Split::none;
    repaint();
}

//==============================================================================
ShaperDnaPanel::ShaperDnaPanel (InstrumentHost& h) : MiniPanel ("SHAPER DNA"), host (h)
{
    using namespace params;
    auto& state = host.apvts();
    const auto sage = dnaview::profileColour (2);
    onLamp = std::make_unique<MacroSwitchLamp> (*state.getParameter (dnaEnabled), sage, &host.undo());
    onLamp->setTooltip ("Shaper DNA on / off");
    binding::tag (*onLamp, dnaEnabled);
    addAndMakeVisible (*onLamp);
    dnaDisplay = std::make_unique<ShaperDnaDisplay> (host);
    addAndMakeVisible (*dnaDisplay);
    auto segments = [&] (const juce::String& id, juce::StringArray items, const char* tooltip) {
        auto s = std::make_unique<SegmentedControl> (*state.getParameter (id), std::move (items));
        s->setAccent (sage);
        s->setTooltip (tooltip);
        binding::tag (*s, id);
        addAndMakeVisible (*s);
        return s;
    };
    assignment = segments (dnaAssignment, { "CYCLE", "KEY ZONES" }, "CYCLE: each new note takes the next profile. KEY ZONES: the key decides");
    profiles = segments (dnaProfiles, { "A / B", "A / B / C" }, "Two or three profiles");
    notes = segments (dnaNotesPerProfile, { "1", "2", "3" }, "CYCLE: notes per profile before the next");
    for (int p = 0; p < 3; ++p)
    {
        auto selector = [&] (const char* name, const char* caption, const char* tooltip) {
            const auto id = dnaProfileId (p, name);
            auto v = std::make_unique<ValueSelector> (*state.getParameter (id), caption);
            v->setTooltip (tooltip);
            v->setRackStyle (ValueSelector::RackStyle::option);
            binding::tag (*v, id);
            addAndMakeVisible (*v);
            return v;
        };
        patternSelectors[static_cast<std::size_t> (p)] = selector ("pattern", "PATTERN", "This profile's pattern (FOLLOW: the SHAPER's own, CUSTOM included)");
        rateSelectors[static_cast<std::size_t> (p)] = selector ("rate", "RATE", "This profile's rate (FOLLOW: the SHAPER's own)");
    }
    refreshContent();
}

ShaperDnaPanel::~ShaperDnaPanel() = default;

juce::Point<int> ShaperDnaPanel::cardSize() const
{
    // Header 32, the display 132, the keys 24, three rows of 26, gaps.
    return { width, 32 + 132 + 10 + 24 + 12 + 3 * 26 + 2 * 6 + 12 };
}

void ShaperDnaPanel::layoutContent (juce::Rectangle<int> area)
{
    // The ON light in the header's right corner, on the title's line.
    onLamp->setBounds (juce::Rectangle<int> (26, 26).withCentre ({ area.getRight() - 6, area.getY() - 17 }));
    dnaDisplay->setBounds (area.removeFromTop (132));
    area.removeFromTop (10);
    auto keys = area.removeFromTop (24);
    assignment->setBounds (keys.removeFromLeft (128));
    keys.removeFromLeft (8);
    divider = { keys.getX(), keys.getY() + 4, 1, 16 };
    keys.removeFromLeft (8);
    profiles->setBounds (keys.removeFromLeft (86));
    keys.removeFromLeft (10);
    notesCaption = keys.removeFromLeft (38);
    notes->setBounds (keys);
    area.removeFromTop (12);
    for (int p = 0; p < 3; ++p)
    {
        auto row = area.removeFromTop (26);
        rowBadges[static_cast<std::size_t> (p)] = row.removeFromLeft (22).withSizeKeepingCentre (18, 18);
        row.removeFromLeft (8);
        patternSelectors[static_cast<std::size_t> (p)]->setBounds (row.removeFromLeft (juce::roundToInt (0.58f * static_cast<float> (row.getWidth()))));
        row.removeFromLeft (8);
        rateSelectors[static_cast<std::size_t> (p)]->setBounds (row);
        area.removeFromTop (6);
    }
}

void ShaperDnaPanel::refreshContent()
{
    const auto d = dnaview::settingsOf (host);
    const bool cycle = d.assignment == ShaperDnaSettings::Assignment::cycle;
    notes->setVisible (cycle);
    // C is kept (its settings stay) but quiet while only A / B play.
    patternSelectors[2]->setAlpha (d.profiles == 3 ? 1.0f : 0.4f);
    rateSelectors[2]->setAlpha (d.profiles == 3 ? 1.0f : 0.4f);
    dnaDisplay->refresh();
    repaint (notesCaption);
    for (const auto& b : rowBadges)
        repaint (b);
}

void ShaperDnaPanel::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    const auto d = dnaview::settingsOf (host);
    g.setColour (palette::hairline);
    g.fillRect (divider);
    if (d.assignment == ShaperDnaSettings::Assignment::cycle)
    {
        g.setColour (palette::textDim);
        g.setFont (fonts::label (9.5f));
        g.drawText ("NOTES", notesCaption, juce::Justification::centredLeft, false);
    }
    for (int p = 0; p < 3; ++p)
    {
        const auto b = rowBadges[static_cast<std::size_t> (p)].toFloat();
        const auto colour = dnaview::profileColour (p).withMultipliedAlpha (p < d.profiles ? 1.0f : 0.4f);
        g.setColour (colour);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (juce::Colour (0xfff7f3ec));
        g.setFont (fonts::label (11.0f));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + p)), b, juce::Justification::centred, false);
    }
}

} // namespace andor::analog
