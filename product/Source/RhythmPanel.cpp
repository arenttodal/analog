#include "RhythmPanel.h"

#include "AnalogEngine.h"
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
namespace mod = osp::mod;

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

    // The well's graphite (AnalogWellOverlays' keys).
    const juce::Colour cellFill { 0xff2b3031 };
    const juce::Colour cellEdge { 0xff3e4446 };
    const juce::Colour cellEdgeHot { 0xff59605f };
    const juce::Colour cream { 0xffeee8dd };

    bool cardPlaysRecording (InstrumentHost& host)
    {
        return static_cast<HybridType> (std::clamp (juce::roundToInt (host.parameterValue (params::hybridType)), 0, 3)) == HybridType::sample;
    }

    void setPlain (InstrumentHost& host, const juce::String& id, float value)
    {
        if (auto* p = host.apvts().getParameter (id))
        {
            const float normalised = p->convertTo0to1 (value);
            if (std::abs (p->getValue() - normalised) < 1.0e-6f)
                return;
            p->beginChangeGesture();
            p->setValueNotifyingHost (normalised);
            p->endChangeGesture();
        }
    }

    int cRhythmChoice() noexcept { return mod::sourceIndex (mod::Source::cRhythm) + 1; }

    double tempoFactorOf (InstrumentHost& host)
    {
        const int t = juce::roundToInt (host.parameterValue (params::rhythmTempo));
        return t == 0 ? 0.5 : (t == 2 ? 2.0 : 1.0);
    }
}

namespace rhythmview
{
    juce::Colour accent() noexcept { return palette::layer (InstrumentHost::shownLayer (1)); }

    const char* targetName (Target t) noexcept
    {
        switch (t)
        {
            case Target::levelA: return "LEVEL A";
            case Target::levelB: return "LEVEL B";
            case Target::filter: return "FILTER";
            case Target::morph: return "MORPH";
        }
        return "";
    }

    int targetChoice (Target t)
    {
        auto product = [] (const juce::String& id) {
            const auto& list = params::modulationDestinations();
            for (std::size_t k = 0; k < list.size(); ++k)
                if (juce::String (list[k].id) == id)
                    return mod::hostDestCount + static_cast<int> (k);
            return -1;
        };
        switch (t)
        {
            case Target::levelA: return mod::choiceOf (mod::Dest::levelA);
            case Target::levelB: return mod::choiceOf (mod::Dest::levelC);   // (card B is the shell's layer C)
            case Target::filter: return product (params::tone);
            case Target::morph: return product (params::morphPosition);
        }
        return -1;
    }

    int routeTo (InstrumentHost& host, Target t)
    {
        const int dest = targetChoice (t);
        for (int r = 0; r < mod::maxRoutes; ++r)
            if (juce::roundToInt (host.parameterValue (InstrumentHost::modRouteId (r, "source"))) == cRhythmChoice()
                && juce::roundToInt (host.parameterValue (InstrumentHost::modRouteId (r, "dest"))) == dest)
                return r;
        return -1;
    }

    bool toggleRoute (InstrumentHost& host, Target t)
    {
        const int dest = targetChoice (t);
        if (dest < 0)
            return false;
        if (const int r = routeTo (host, t); r >= 0)
        {
            host.undo().beginNewTransaction ("C RHYTHM route");
            setPlain (host, InstrumentHost::modRouteId (r, "source"), 0.0f);
            setPlain (host, InstrumentHost::modRouteId (r, "dest"), 0.0f);
            setPlain (host, InstrumentHost::modRouteId (r, "depth"), 0.0f);
            return true;
        }
        int free = -1;
        for (int r = 0; r < mod::maxRoutes && free < 0; ++r)
            if (juce::roundToInt (host.parameterValue (InstrumentHost::modRouteId (r, "source"))) == 0)
                free = r;
        if (free < 0)
            return false;
        // The depths a first listen wants: the levels ducked on the hits (a pump), the filter
        // and MORPH opened by them.
        const float depth = t == Target::filter ? 40.0f : (t == Target::morph ? 50.0f : -60.0f);
        host.undo().beginNewTransaction ("C RHYTHM route");
        setPlain (host, InstrumentHost::modRouteId (free, "source"), static_cast<float> (cRhythmChoice()));
        setPlain (host, InstrumentHost::modRouteId (free, "dest"), static_cast<float> (dest));
        setPlain (host, InstrumentHost::modRouteId (free, "depth"), depth);
        setPlain (host, InstrumentHost::modRouteId (free, "enabled"), 1.0f);
        setPlain (host, params::rhythmEnabled, 1.0f);
        if (juce::roundToInt (host.parameterValue (params::rhythmAudio)) == 0)
            setPlain (host, params::rhythmAudio, 2.0f);   // (AUDIO feeds no route: BOTH)
        return true;
    }

    juce::String readout (InstrumentHost& host)
    {
        const auto* engine = engineOf (host);
        if (engine == nullptr)
            return {};
        if (! cardPlaysRecording (host))
            return "CARD C PLAYS NO RECORDING (TYPE: SAMPLE)";
        if (engine->rhythmAnalysing())
            return juce::String::fromUTF8 ("LISTENING\xe2\x80\xa6");
        const auto loop = engine->rhythmLoop();
        if (loop == nullptr)
            return "NO RECORDING ON CARD C";
        const auto& a = loop->analysis;
        if (a.kind == rhythm::Kind::silent)
            return "SILENT";
        if (! loop->rhythmic())
            return juce::String::fromUTF8 ("NO STEADY PULSE \xc2\xb7 PLAYS FREE");
        const double f = tempoFactorOf (host);
        const double bars = a.bars * f;
        juce::String s;
        s << juce::String (a.bpm * f, 1) << " BPM" << juce::String::fromUTF8 (" \xc2\xb7 ")
          << (std::abs (bars - std::round (bars)) < 1.0e-6 ? juce::String (juce::roundToInt (bars)) : juce::String (bars, 1))
          << (std::abs (bars - 1.0) < 1.0e-6 ? " BAR" : " BARS") << juce::String::fromUTF8 (" \xc2\xb7 ") << a.beatsPerBar << "/4";
        if (juce::roundToInt (host.parameterValue (params::rhythmSync)) == 1)
            s << juce::String::fromUTF8 (" \xc2\xb7 FREE");
        return s;
    }
}

//==============================================================================
RhythmKey::RhythmKey (InstrumentHost& h) : host (h)
{
    setTitle ("Rhythm");
    setDescription ("Card C's recording as a loop on the host's grid, and C RHYTHM: the light switches it, the name opens its panel");
    setTooltip ("RHYTHM: card C's recording loops on the host's tempo and becomes the C RHYTHM modulation source (the light switches it, the name opens it)");
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    binding::tag (*this, params::rhythmEnabled);
    shownOn = isOn();
    startTimerHz (15);
}

RhythmKey::~RhythmKey() = default;

bool RhythmKey::isOn() const { return host.parameterValue (params::rhythmEnabled) >= 0.5f; }

bool RhythmKey::isOpen() const
{
    const auto* owner = findParentComponentOfClass<AnchoredPopoverHost>();
    return owner != nullptr && owner->anchoredPopoverOpenFrom (*this);
}

void RhythmKey::toggle()
{
    host.undo().beginNewTransaction ("Rhythm");
    setPlain (host, params::rhythmEnabled, isOn() ? 0.0f : 1.0f);
    timerCallback();
}

void RhythmKey::openPanel()
{
    auto* owner = findParentComponentOfClass<AnchoredPopoverHost>();
    if (owner == nullptr)
        return;
    if (owner->anchoredPopoverOpenFrom (*this))
        owner->closeAnchoredPopover();   // (the key again closes it)
    else
        owner->openAnchoredPopover (std::make_unique<RhythmPanel> (host), *this);
    timerCallback();
}

void RhythmKey::timerCallback()
{
    const bool on = isOn(), open = isOpen();
    if (on != shownOn || open != shownOpen)
    {
        shownOn = on;
        shownOpen = open;
        repaint();
    }
}

void RhythmKey::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (lightArea().contains (e.getPosition()))
        toggle();
    else
        openPanel();
}

bool RhythmKey::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        toggle();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        openPanel();
        return true;
    }
    return false;
}

void RhythmKey::paint (juce::Graphics& g)
{
    shownOn = isOn();     // (drawn as it is now; the timer only asks for the repaint)
    shownOpen = isOpen();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto colour = rhythmview::accent();
    const bool hover = isMouseOver (true);
    const bool overLight = hover && lightArea().contains (getMouseXYRelative());
    g.setColour (cellFill.withAlpha (hover ? 1.0f : 0.9f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (shownOpen || hasKeyboardFocus (false) ? colour.withAlpha (0.9f) : (hover ? cellEdgeHot : cellEdge));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    // The light: lit in card C's colour, or a dark lens.
    const auto light = juce::Rectangle<float> (7.0f, 7.0f).withCentre (lightArea().toFloat().getCentre().translated (1.0f, 0.0f));
    if (shownOn)
    {
        g.setColour (colour.withAlpha (0.3f));
        g.fillEllipse (light.expanded (3.0f));
        g.setColour (colour.brighter (0.15f));
        g.fillEllipse (light);
    }
    else
    {
        g.setColour (cream.withAlpha (overLight ? 0.2f : 0.08f));
        g.fillEllipse (light);
        g.setColour (cream.withAlpha (overLight ? 0.55f : 0.3f));
        g.drawEllipse (light.reduced (0.5f), 1.0f);
    }
    auto label = r.withTrimmedLeft (static_cast<float> (getHeight()) - 2.0f).withTrimmedRight (14.0f);
    g.setFont (type::popupLabel (11.0f));
    g.setColour (cream.withAlpha (shownOn ? 0.95f : ((hover && ! overLight) ? 0.9f : 0.72f)));
    g.drawText ("RHYTHM", label, juce::Justification::centredLeft, false);
    const float cx = r.getRight() - 8.0f, cy = r.getCentreY();
    juce::Path chevron;
    const float d = shownOpen ? -2.2f : 2.2f;
    chevron.startNewSubPath (cx - 3.5f, cy - d);
    chevron.lineTo (cx, cy + d);
    chevron.lineTo (cx + 3.5f, cy - d);
    g.setColour (cream.withAlpha (0.6f));
    g.strokePath (chevron, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
RhythmDisplay::RhythmDisplay (InstrumentHost& h) : host (h)
{
    setInterceptsMouseClicks (false, false);
}

void RhythmDisplay::refresh()
{
    const auto* engine = engineOf (host);
    juce::String key = rhythmview::readout (host);
    key << ":" << juce::String (host.parameterValue (params::rhythmSensitivity), 1) << ":" << juce::roundToInt (host.parameterValue (params::rhythmShape))
        << ":" << juce::roundToInt (host.parameterValue (params::rhythmEnabled));
    if (engine != nullptr)
    {
        key << ":" << juce::String (engine->rhythmPlayhead(), 3) << ":" << juce::String (engine->rhythmValue(), 2);
        key << ":" << juce::String::toHexString (reinterpret_cast<juce::pointer_sized_int> (engine->rhythmLoop().get()));
    }
    if (key != drawnFor)
    {
        drawnFor = key;
        repaint();
    }
}

void RhythmDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff1c2021));   // (the SUB / DNA / SATELLITE displays' graphite)
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto colour = rhythmview::accent();
    const auto* engine = engineOf (host);
    const bool on = host.parameterValue (params::rhythmEnabled) >= 0.5f;
    auto inner = bounds.reduced (12.0f, 8.0f);
    auto top = inner.removeFromTop (14.0f);
    g.setFont (fonts::label (10.0f));
    g.setColour (cream.withAlpha (0.82f));
    g.drawText (rhythmview::readout (host), top.withTrimmedRight (70.0f), juce::Justification::centredLeft, true);
    g.setColour (cream.withAlpha (0.45f));
    g.drawText ("C RHYTHM", top, juce::Justification::centredRight, false);
    inner.removeFromTop (6.0f);

    // C RHYTHM's value, at the right edge (what the routes read now).
    auto meter = inner.removeFromRight (8.0f);
    inner.removeFromRight (10.0f);
    shownValue = engine != nullptr && on ? std::clamp (engine->rhythmValue(), 0.0f, 1.0f) : 0.0f;
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (meter, 3.0f);
    if (shownValue > 0.0f)
    {
        g.setColour (colour.withAlpha (0.9f));
        g.fillRoundedRectangle (meter.withTop (meter.getBottom() - shownValue * meter.getHeight()), 3.0f);
    }

    shownPlayheadX = -1.0f;
    const auto loop = engine != nullptr ? engine->rhythmLoop() : nullptr;
    if (loop == nullptr || ! cardPlaysRecording (host) || loop->analysis.envelope.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawLine (inner.getX(), inner.getBottom() - 0.5f, inner.getRight(), inner.getBottom() - 0.5f, 1.0f);
        return;
    }
    const auto& a = loop->analysis;
    const double from = loop->rhythmic() ? loop->loopStart / loop->rate : 0.0;
    const double span = loop->rhythmic() ? loop->loopFrames / loop->rate : std::max (1.0e-3, a.durationSeconds);
    auto xOf = [&] (double seconds) { return inner.getX() + static_cast<float> ((seconds - from) / span) * inner.getWidth(); };
    const float alpha = on ? 1.0f : 0.55f;

    // The beat grid (bars stronger), when there is one.
    if (loop->rhythmic())
    {
        const double beat = a.beatSeconds();
        for (int b = 0; b <= a.beats; ++b)
        {
            const float x = xOf (from + b * beat);
            const bool bar = b % std::max (1, a.beatsPerBar) == 0;
            g.setColour (juce::Colours::white.withAlpha ((bar ? 0.2f : 0.07f) * alpha));
            g.drawLine (x, inner.getY(), x, inner.getBottom(), bar ? 1.2f : 1.0f);
        }
    }

    // The level over the loop (the analysis' own envelope, against its loudest).
    const float peak = std::max (a.envelopePeak, 1.0e-9f);
    juce::Path level;
    level.startNewSubPath (inner.getX(), inner.getBottom());
    const int columns = std::max (2, juce::roundToInt (inner.getWidth()));
    for (int c = 0; c <= columns; ++c)
    {
        const double t = from + span * c / columns;
        const auto f = static_cast<std::size_t> (std::clamp (t / a.hopSeconds, 0.0, static_cast<double> (a.envelope.size() - 1)));
        const float v = std::clamp (a.envelope[f] / peak, 0.0f, 1.0f);
        level.lineTo (inner.getX() + inner.getWidth() * c / columns, inner.getBottom() - v * inner.getHeight());
    }
    level.lineTo (inner.getRight(), inner.getBottom());
    level.closeSubPath();
    g.setColour (colour.withAlpha (0.28f * alpha));
    g.fillPath (level);
    g.setColour (colour.withAlpha (0.75f * alpha));
    g.strokePath (level, juce::PathStrokeType (1.0f));

    // The attacks: ticks at the foot, as loud as they are.
    for (const auto& o : a.onsets)
    {
        double t = o.seconds;
        if (loop->rhythmic())
            t = from + std::fmod (std::fmod (t - from, span) + span, span);
        if (t < from || t > from + span)
            continue;
        const float x = xOf (t);
        g.setColour (cream.withAlpha ((0.35f + 0.5f * o.level) * alpha));
        g.drawLine (x, inner.getBottom() - 3.0f - 7.0f * o.level, x, inner.getBottom(), 1.2f);
    }

    // SENSITIVITY's threshold: what C RHYTHM rises above (on the same 0..1 scale it reads).
    const float threshold = 0.6f * (1.0f - host.parameterValue (params::rhythmSensitivity) / 100.0f);
    const float ty = inner.getBottom() - threshold * inner.getHeight();
    juce::Path line, dashes;
    line.startNewSubPath (inner.getX(), ty);
    line.lineTo (inner.getRight(), ty);
    const float dash[] { 3.0f, 3.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashes, line, dash, 2);
    g.setColour (cream.withAlpha (0.35f * alpha));
    g.fillPath (dashes);

    // The playhead: where the audio thread reads the loop now.
    if (engine != nullptr && on)
        if (const double at = engine->rhythmPlayhead(); at >= 0.0)
        {
            double t = at;
            if (loop->rhythmic())
                t = from + std::fmod (std::fmod (at - from, span) + span, span);
            shownPlayheadX = std::clamp (xOf (t), inner.getX(), inner.getRight());
            g.setColour (cream.withAlpha (0.9f));
            g.drawLine (shownPlayheadX, inner.getY(), shownPlayheadX, inner.getBottom(), 1.4f);
        }
}

//==============================================================================
RhythmPanel::RhythmPanel (InstrumentHost& h) : MiniPanel ("RHYTHM"), host (h)
{
    auto& state = host.apvts();
    const auto colour = rhythmview::accent();
    onLamp = std::make_unique<MacroSwitchLamp> (*state.getParameter (params::rhythmEnabled), colour, &host.undo());
    onLamp->setTooltip ("Rhythm on / off");
    binding::tag (*onLamp, params::rhythmEnabled);
    addAndMakeVisible (*onLamp);
    loopDisplay = std::make_unique<RhythmDisplay> (host);
    addAndMakeVisible (*loopDisplay);
    auto segments = [&] (const juce::String& pid, juce::StringArray items, const char* tooltip) {
        auto s = std::make_unique<SegmentedControl> (*state.getParameter (pid), std::move (items));
        s->setAccent (colour);
        s->setTooltip (tooltip);
        binding::tag (*s, pid);
        addAndMakeVisible (*s);
        return s;
    };
    timing = segments (params::rhythmSync, { "SYNC", "FREE" }, "SYNC: each slice of the loop starts on the host's grid, its pitch kept. FREE: it plays at its own speed");
    tempo = segments (params::rhythmTempo, { "HALF", "DETECTED", "DOUBLE" }, "The loop's tempo as detected, or read at half or double (a wrong octave corrected)");
    audio = segments (params::rhythmAudio, { "AUDIO", "MOD", "BOTH" }, "AUDIO: the loop heard. MOD: only felt, through C RHYTHM. BOTH: heard and felt");
    shape = segments (params::rhythmShape, { "ENVELOPE", "TRANSIENT" }, "What C RHYTHM follows: the loop's level, or its attacks");
    auto knob = [&] (const juce::String& pid, const char* caption, const char* tooltip) {
        auto* p = state.getParameter (pid);
        auto k = std::make_unique<MiniKnob> (state, pid, caption, textOf (*p));
        k->setArcColour (colour);
        k->slider.setTooltip (tooltip);
        binding::tag (k->slider, pid);
        addAndMakeVisible (*k);
        return k;
    };
    sensitivity = knob (params::rhythmSensitivity, "SENS", "How much of the loop C RHYTHM hears: up, the quieter parts too");
    attack = knob (params::rhythmAttack, "ATTACK", "How fast C RHYTHM rises");
    release = knob (params::rhythmRelease, "RELEASE", "How slowly it falls");
    depth = knob (params::rhythmDepth, "DEPTH", "C RHYTHM's size (every route from it)");
    refreshContent();
}

RhythmPanel::~RhythmPanel() = default;

juce::Point<int> RhythmPanel::cardSize() const
{
    // Header 32, the display 112, two rows of keys, the knobs 92, the routes.
    return { width, 32 + 112 + 10 + 24 + 8 + 24 + 8 + 92 + 6 + 24 + 10 };
}

void RhythmPanel::layoutContent (juce::Rectangle<int> area)
{
    const int headerY = area.getY() - 17;
    onLamp->setBounds (juce::Rectangle<int> (26, 26).withCentre ({ area.getRight() - 6, headerY }));
    loopDisplay->setBounds (area.removeFromTop (112));
    area.removeFromTop (10);
    {
        auto row = area.removeFromTop (24);
        timing->setBounds (row.removeFromLeft (124));
        row.removeFromLeft (10);
        tempo->setBounds (row);
    }
    area.removeFromTop (8);
    {
        auto row = area.removeFromTop (24);
        audio->setBounds (row.removeFromLeft (170));
        row.removeFromLeft (10);
        shape->setBounds (row);
    }
    area.removeFromTop (8);
    {
        auto knobs = area.removeFromTop (92);
        const int w = knobs.getWidth() / 4;
        sensitivity->setBounds (knobs.removeFromLeft (w));
        attack->setBounds (knobs.removeFromLeft (w));
        release->setBounds (knobs.removeFromLeft (w));
        depth->setBounds (knobs);
    }
    area.removeFromTop (6);
    auto routes = area.removeFromTop (24);
    routeCaption = routes.removeFromLeft (92);
    const int gap = 6, w = (routes.getWidth() - 3 * gap) / 4;
    for (std::size_t i = 0; i < routeKeys.size(); ++i)
    {
        routeKeys[i] = routes.removeFromLeft (w);
        routes.removeFromLeft (gap);
    }
}

void RhythmPanel::refreshContent()
{
    // Only what acts, enabled: SYNC and its tempo for a loop with a pulse (one without plays
    // FREE), HALF / DOUBLE only in SYNC; C RHYTHM's shaping only while something feeds on it
    // (AUDIO alone feeds no route).
    const auto* engine = engineOf (host);
    const auto loop = engine != nullptr ? engine->rhythmLoop() : nullptr;
    const bool rhythmic = loop != nullptr && loop->rhythmic();
    const bool synced = juce::roundToInt (host.parameterValue (params::rhythmSync)) == 0;
    const bool felt = juce::roundToInt (host.parameterValue (params::rhythmAudio)) != 0;
    auto enable = [] (juce::Component& c, bool e) {
        c.setEnabled (e);
        c.setAlpha (e ? 1.0f : 0.4f);
    };
    enable (*timing, rhythmic);
    enable (*tempo, rhythmic && synced);
    for (auto* c : { static_cast<juce::Component*> (shape.get()), static_cast<juce::Component*> (sensitivity.get()), static_cast<juce::Component*> (attack.get()),
                     static_cast<juce::Component*> (release.get()), static_cast<juce::Component*> (depth.get()) })
        enable (*c, felt);
    loopDisplay->refresh();
    juce::String routed;
    for (const auto t : rhythmview::targets)
        routed << (rhythmview::routeTo (host, t) >= 0 ? "1" : "0");
    if (routed != routesDrawnFor)
    {
        routesDrawnFor = routed;
        repaint();
    }
}

void RhythmPanel::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    const auto colour = rhythmview::accent();
    g.setFont (fonts::label (10.0f));
    g.setColour (palette::text.withAlpha (0.6f));
    g.drawText ("C RHYTHM TO", routeCaption, juce::Justification::centredLeft, false);
    for (const auto t : rhythmview::targets)
    {
        const auto i = static_cast<std::size_t> (t);
        const auto r = routeKeys[i].toFloat().reduced (0.5f);
        const bool routed = rhythmview::routeTo (host, t) >= 0;
        const bool over = hoveredRoute == static_cast<int> (i);
        g.setColour (routed ? colour.withAlpha (0.3f) : juce::Colours::white.withAlpha (over ? 0.35f : 0.18f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (routed ? colour.darker (0.2f) : (over ? palette::text.withAlpha (0.4f) : palette::hairline));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (palette::text.withAlpha (routed ? 0.95f : 0.7f));
        g.setFont (fonts::label (10.5f));
        g.drawText (rhythmview::targetName (t), r, juce::Justification::centred, false);
    }
}

void RhythmPanel::mouseDown (const juce::MouseEvent& e)
{
    for (const auto t : rhythmview::targets)
        if (routeKeys[static_cast<std::size_t> (t)].contains (e.getPosition()))
        {
            rhythmview::toggleRoute (host, t);
            refreshContent();
            repaint();
            return;
        }
}

void RhythmPanel::mouseMove (const juce::MouseEvent& e)
{
    MiniPanel::mouseMove (e);
    int now = -1;
    for (std::size_t i = 0; i < routeKeys.size(); ++i)
        if (routeKeys[i].contains (e.getPosition()))
            now = static_cast<int> (i);
    if (now != hoveredRoute)
    {
        hoveredRoute = now;
        setMouseCursor (now >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void RhythmPanel::mouseExit (const juce::MouseEvent&)
{
    if (hoveredRoute >= 0)
    {
        hoveredRoute = -1;
        repaint();
    }
}

} // namespace andor::analog
