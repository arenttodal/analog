#include "SatellitePanel.h"

#include "AnalogEngine.h"
#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
#include "OspLookAndFeel.h"
#include "ShellProcessor.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace andor::analog
{

using namespace osp::plugin;
namespace sat = osp::satellite;

namespace
{
    MiniKnob::Formatter textOf (juce::RangedAudioParameter& p)
    {
        return [&p] (double v) { return p.getText (p.convertTo0to1 (static_cast<float> (v)), 0); };
    }

    const AnalogEngine* engineOf (InstrumentHost& host)
    {
        auto* shell = dynamic_cast<andor::shell::ShellProcessor*> (&host);
        return shell != nullptr ? dynamic_cast<const AnalogEngine*> (&shell->productEngine()) : nullptr;
    }
}

namespace satview
{
    sat::Settings settingsOf (InstrumentHost& host, int card)
    {
        auto value = [&] (const char* name) { return static_cast<double> (host.parameterValue (params::satelliteId (card, name))); };
        sat::Settings s;
        s.enabled = value ("enabled") >= 0.5;
        s.mode = static_cast<sat::Mode> (juce::roundToInt (value ("mode")));
        s.timing = static_cast<sat::Timing> (juce::roundToInt (value ("sync")));
        s.division = juce::roundToInt (value ("division"));
        s.rateHz = value ("rate");
        s.depth = value ("depth") / 100.0;
        s.width = value ("width") / 100.0;
        s.phase = value ("phase") / 360.0;
        s.offset = value ("offset") / 100.0;
        s.retrigger = static_cast<sat::Retrigger> (juce::roundToInt (value ("retrigger")));
        return s.sanitised();
    }

    juce::Colour cardColour (int card) noexcept
    {
        // The cards' own identity colours (the arpeggiator's PLAYS chips).
        return palette::layer (juce::jlimit (0, 2, card));
    }
}

//==============================================================================
SatelliteDisplay::SatelliteDisplay (InstrumentHost& h, int c) : host (h), card (c)
{
    setInterceptsMouseClicks (false, false);
}

void SatelliteDisplay::setCard (int c)
{
    card = juce::jlimit (0, 2, c);
    drawnFor.clear();
    repaint();
}

void SatelliteDisplay::refresh()
{
    const auto s = satview::settingsOf (host, card);
    juce::String key;
    key << static_cast<int> (s.enabled) << static_cast<int> (s.mode) << static_cast<int> (s.retrigger) << ":" << juce::String (s.depth, 3) << ":"
        << juce::String (s.offset, 3) << ":" << juce::String (s.phase, 3);
    if (const auto* engine = engineOf (host))
    {
        const auto view = engine->satelliteView (card);
        key << (view.moving ? "M" : "-") << juce::String (view.x, 3) << juce::String (view.rear, 3);
    }
    if (key != drawnFor)
    {
        drawnFor = key;
        repaint();
    }
}

void SatelliteDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    // The dark well (the SUB / DNA displays' graphite).
    g.setColour (juce::Colour (0xff1c2021));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto s = satview::settingsOf (host, card);
    const auto colour = satview::cardColour (card);
    const auto area = bounds.reduced (26.0f, 16.0f);
    const float cy = area.getCentreY();
    auto xOf = [&] (double place) { return area.getX() + 0.5f * (static_cast<float> (place) + 1.0f) * area.getWidth(); };

    // The panorama: a hairline from L to R, its centre ticked.
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (area.getX(), cy, area.getRight(), cy, 1.0f);
    g.drawLine (xOf (0.0), cy - 5.0f, xOf (0.0), cy + 5.0f, 1.0f);
    g.setFont (fonts::label (9.5f));
    g.setColour (juce::Colours::white.withAlpha (0.38f));
    g.drawText ("L", juce::Rectangle<float> (16.0f, 14.0f).withCentre ({ bounds.getX() + 13.0f, cy }), juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<float> (16.0f, 14.0f).withCentre ({ bounds.getRight() - 13.0f, cy }), juce::Justification::centred, false);

    const float alpha = s.enabled ? 1.0f : 0.45f;
    const double travel = sat::travel (s);
    const float lo = xOf (s.offset - travel), hi = xOf (s.offset + travel), mid = xOf (s.offset);
    const float ry = 0.42f * area.getHeight();
    if (s.mode == sat::Mode::orbit)
    {
        // The circle: the near half bright, the far half (quieter, darker) dimmer.
        juce::Path nearHalf, farHalf;
        const float rx = std::max (2.0f, 0.5f * (hi - lo));
        nearHalf.addCentredArc (mid, cy, rx, ry, 0.0f, 0.5f * std::numbers::pi_v<float>, 1.5f * std::numbers::pi_v<float>, true);
        farHalf.addCentredArc (mid, cy, rx, ry, 0.0f, -0.5f * std::numbers::pi_v<float>, 0.5f * std::numbers::pi_v<float>, true);
        g.setColour (colour.withAlpha (0.3f * alpha));
        g.strokePath (farHalf, juce::PathStrokeType (1.2f));
        g.setColour (colour.withAlpha (0.85f * alpha));
        g.strokePath (nearHalf, juce::PathStrokeType (1.6f));
    }
    else if (s.mode == sat::Mode::bounce)
    {
        // Two places and the hop between them.
        juce::Path hop;
        hop.startNewSubPath (lo, cy);
        hop.quadraticTo (mid, cy - ry, hi, cy);
        g.setColour (colour.withAlpha (0.4f * alpha));
        g.strokePath (hop, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        for (const float x : { lo, hi })
        {
            g.setColour (colour.withAlpha (0.85f * alpha));
            g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x, cy }), 1.4f);
        }
    }
    else
    {
        // PAN's track: a bar from one end of the travel to the other.
        g.setColour (colour.withAlpha (0.28f * alpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (lo, cy - 3.0f, std::max (2.0f, hi - lo), 6.0f), 3.0f);
        g.setColour (colour.withAlpha (0.85f * alpha));
        g.drawLine (lo, cy - 6.0f, lo, cy + 6.0f, 1.4f);
        g.drawLine (hi, cy - 6.0f, hi, cy + 6.0f, 1.4f);
    }

    // The marker: where the audio has the card now; still, hollow, where its path starts.
    const auto* engine = engineOf (host);
    const auto view = engine != nullptr ? engine->satelliteView (card) : Synth::SatelliteView {};
    live = s.enabled && view.moving;
    double x = 0.0, rear = 0.0;
    if (live)
    {
        x = view.x;
        rear = view.rear;
    }
    else
    {
        const auto start = sat::pointAt (s, s.phase);
        x = s.mode == sat::Mode::bounce && s.retrigger == sat::Retrigger::note ? sat::notePlace (s, 0) : start.x;
        rear = start.rear;
    }
    float y = cy;
    if (s.mode == sat::Mode::orbit && s.depth > 1.0e-3)
    {
        // Behind is up: the circle's far side.
        const double c = std::clamp (1.0 - 2.0 * rear / s.depth, -1.0, 1.0);
        y = cy + ry * static_cast<float> (c);
    }
    marker = { xOf (x), y };
    const auto dot = juce::Rectangle<float> (11.0f, 11.0f).withCentre (marker);
    if (live)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (dot.expanded (4.0f));
        g.setColour (colour.brighter (0.2f));
        g.fillEllipse (dot);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f * alpha));
        g.drawEllipse (dot.reduced (0.7f), 1.4f);
    }
}

//==============================================================================
SatellitePanel::SatellitePanel (InstrumentHost& h, int c) : MiniPanel ("SATELLITE"), host (h), cardIndex (juce::jlimit (0, 2, c))
{
    pathDisplay = std::make_unique<SatelliteDisplay> (host, cardIndex);
    addAndMakeVisible (*pathDisplay);
    build();
}

SatellitePanel::~SatellitePanel() = default;

void SatellitePanel::build()
{
    // The shown card's own controls (rebuilt when another card is shown: each binds its card).
    auto& state = host.apvts();
    const auto colour = satview::cardColour (cardIndex);
    auto id = [this] (const char* name) { return params::satelliteId (cardIndex, name); };
    onLamp = std::make_unique<MacroSwitchLamp> (*state.getParameter (id ("enabled")), colour, &host.undo());
    onLamp->setTooltip (juce::String ("Satellite ") + static_cast<char> ('A' + cardIndex) + " on / off");
    binding::tag (*onLamp, id ("enabled"));
    addAndMakeVisible (*onLamp);
    auto segments = [&] (const juce::String& pid, juce::StringArray items, const char* tooltip) {
        auto s = std::make_unique<SegmentedControl> (*state.getParameter (pid), std::move (items));
        s->setAccent (colour);
        s->setTooltip (tooltip);
        binding::tag (*s, pid);
        addAndMakeVisible (*s);
        return s;
    };
    mode = segments (id ("mode"), { "PAN", "ORBIT", "BOUNCE" }, "PAN: side to side. ORBIT: round, quieter and darker behind. BOUNCE: from side to side");
    timing = segments (id ("sync"), { "SYNC", "FREE" }, "SYNC: to the host's tempo. FREE: its own rate");
    bounce = segments (id ("retrigger"), { "CYCLE", "NOTE" }, "BOUNCE: CYCLE keeps time; NOTE puts each new note on the other side");
    auto knob = [&] (const char* name, const char* caption, bool small) {
        auto* p = state.getParameter (id (name));
        auto k = std::make_unique<MiniKnob> (state, id (name), caption, textOf (*p));
        k->setArcColour (colour);
        k->setSmall (small);
        binding::tag (k->slider, id (name));
        addAndMakeVisible (*k);
        return k;
    };
    division = knob ("division", "RATE", false);
    rate = knob ("rate", "RATE", false);
    depth = knob ("depth", "DEPTH", false);
    spread = knob ("width", "WIDTH", false);
    phase = knob ("phase", "PHASE", true);
    offset = knob ("offset", "OFFSET", true);
    division->slider.setTooltip ("SYNC: one cycle every ...");
    rate->slider.setTooltip ("FREE: cycles a second");
    depth->slider.setTooltip ("How far it travels (0: still at OFFSET)");
    spread->slider.setTooltip ("The card's own stereo spread around its place");
    phase->slider.setTooltip ("Where in its cycle it starts (stagger the cards)");
    offset->slider.setTooltip ("The centre of its travel");
    pathDisplay->setCard (cardIndex);
    refreshContent();
    resized();
}

void SatellitePanel::showCard (int c)
{
    c = juce::jlimit (0, 2, c);
    if (c == cardIndex)
        return;
    cardIndex = c;
    for (auto* child : { static_cast<juce::Component*> (onLamp.get()), static_cast<juce::Component*> (mode.get()), static_cast<juce::Component*> (timing.get()),
                         static_cast<juce::Component*> (bounce.get()), static_cast<juce::Component*> (division.get()), static_cast<juce::Component*> (rate.get()),
                         static_cast<juce::Component*> (depth.get()), static_cast<juce::Component*> (spread.get()), static_cast<juce::Component*> (phase.get()),
                         static_cast<juce::Component*> (offset.get()) })
        if (child != nullptr)
            removeChildComponent (child);
    build();
    repaint();
}

juce::Point<int> SatellitePanel::cardSize() const
{
    // Header 32, the display 104, the keys 24, the knobs 92, gaps.
    return { width, 32 + 104 + 10 + 24 + 8 + 92 + 10 };
}

void SatellitePanel::layoutContent (juce::Rectangle<int> area)
{
    // The header: A / B / C after the title, the shown card's light at the right.
    const int headerY = area.getY() - 17;
    for (int c = 0; c < 3; ++c)
        chips[static_cast<std::size_t> (c)] = juce::Rectangle<int> (24, 20).withCentre ({ area.getX() + 128 + 28 * c, headerY });
    if (onLamp != nullptr)
        onLamp->setBounds (juce::Rectangle<int> (26, 26).withCentre ({ area.getRight() - 6, headerY }));
    pathDisplay->setBounds (area.removeFromTop (104));
    area.removeFromTop (10);
    keysRow = area.removeFromTop (24);
    auto keys = keysRow;
    if (mode != nullptr)
    {
        mode->setBounds (keys.removeFromLeft (168));
        keys.removeFromLeft (10);
        timing->setBounds (keys.removeFromLeft (100));
        keys.removeFromLeft (10);
        bounce->setBounds (keys);
    }
    area.removeFromTop (8);
    if (depth != nullptr)
    {
        auto knobs = area.removeFromTop (92);
        const int w = knobs.getWidth() / 5;
        division->setBounds (knobs.removeFromLeft (w));
        rate->setBounds (division->getBounds());
        depth->setBounds (knobs.removeFromLeft (w));
        spread->setBounds (knobs.removeFromLeft (w));
        phase->setBounds (knobs.removeFromLeft (w));
        offset->setBounds (knobs);
    }
}

void SatellitePanel::refreshContent()
{
    if (depth == nullptr)
        return;
    const auto s = satview::settingsOf (host, cardIndex);
    // Only what this card's settings use: RATE as a division (SYNC) or in Hz (FREE); BOUNCE's
    // CYCLE / NOTE only in BOUNCE.
    division->setVisible (s.timing == sat::Timing::sync);
    rate->setVisible (s.timing == sat::Timing::free);
    bounce->setVisible (s.mode == sat::Mode::bounce);
    pathDisplay->refresh();
    repaint (getLocalBounds().withHeight (40));
}

void SatellitePanel::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    // The card chips (their identity colours; the shown one filled): showing, never switching.
    for (int c = 0; c < 3; ++c)
    {
        const auto chip = chips[static_cast<std::size_t> (c)].toFloat();
        const auto colour = satview::cardColour (c);
        const bool shown = c == cardIndex;
        const bool on = satview::settingsOf (host, c).enabled;
        g.setColour (shown ? colour.withAlpha (0.32f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (chip, 4.0f);
        g.setColour (shown ? colour.darker (0.2f) : palette::hairline);
        g.drawRoundedRectangle (chip.reduced (0.5f), 4.0f, 1.0f);
        g.setFont (fonts::label (11.0f));
        g.setColour (palette::text.withAlpha (shown ? 0.95f : 0.6f));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + c)), chip, juce::Justification::centred, false);
        if (on)
        {
            // (a card that moves: a small light under its letter)
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre ({ chip.getCentreX(), chip.getBottom() - 3.5f }));
        }
    }
}

void SatellitePanel::mouseDown (const juce::MouseEvent& e)
{
    for (int c = 0; c < 3; ++c)
        if (chips[static_cast<std::size_t> (c)].contains (e.getPosition()))
        {
            showCard (c);
            return;
        }
}

} // namespace andor::analog
