#include "UnisonPanel.h"

#include "AnalogParameters.h"
#include "Binding.h"
#include "Design.h"
#include "OspLookAndFeel.h"
#include "Unison.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

using namespace osp::plugin;

namespace
{
    MiniKnob::Formatter textOf (juce::RangedAudioParameter& p)
    {
        return [&p] (double v) { return p.getText (p.convertTo0to1 (static_cast<float> (v)), 0); };
    }

    juce::Colour cardColour (int card) noexcept { return palette::layer (juce::jlimit (0, 2, card)); }

    /** The card's UNISON as its parameters hold it. */
    UnisonSettings settingsOf (InstrumentHost& host, int card)
    {
        auto value = [&] (const char* name) { return static_cast<double> (host.parameterValue (params::unisonId (card, name))); };
        UnisonSettings u;
        u.voices = 1 + juce::jlimit (0, unisonmath::maxVoices - 1, juce::roundToInt (value ("voices")));
        u.detune = value ("detune") / 100.0;
        u.blend = value ("blend") / 100.0;
        u.width = value ("width") / 100.0;
        u.spread = static_cast<UnisonSpread> (juce::jlimit (0, 4, juce::roundToInt (value ("mode"))));
        u.rangeSemitones = value ("range");
        return u;
    }

    const juce::String& fineIdOf (int card) { return card == 0 ? params::osc1Fine : (card == 1 ? params::osc2Fine : params::osc3Fine); }
}

//==============================================================================
UnisonPicture::UnisonPicture (InstrumentHost& h, int c) : host (h), card (juce::jlimit (0, 2, c))
{
    setInterceptsMouseClicks (false, false);
}

void UnisonPicture::refresh()
{
    const auto u = settingsOf (host, card);
    juce::String key;
    key << u.voices << ":" << juce::String (u.detune, 3) << ":" << juce::String (u.blend, 3) << ":" << juce::String (u.width, 3) << ":"
        << static_cast<int> (u.spread) << ":" << juce::String (u.rangeSemitones, 3);
    if (key != drawnFor)
    {
        drawnFor = key;
        repaint();
    }
}

void UnisonPicture::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    // The dark well (the SUB / SATELLITE displays' graphite).
    g.setColour (juce::Colour (0xff1c2021));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto u = settingsOf (host, card);
    const auto colour = cardColour (card);
    const auto area = bounds.reduced (30.0f, 14.0f).withTrimmedBottom (12.0f);
    const float base = area.getBottom(), cx = area.getCentreX();
    // RANDOM: an illustration (each note draws its own spread from its seed).
    const std::array<float, unisonmath::maxVoices> illustration { 0.83f, 0.31f, 0.57f, 0.12f, 0.69f, 0.44f, 0.95f, 0.22f };
    const auto l = unisonmath::layout (u, illustration);
    // Across: the outer copies' detune at the edges (at least a quarter tone, so a fine detune
    // still reads as spread); DETUNE 0 stacks every copy on the centre line.
    const double outer = unisonmath::detuneSemitones (u.detune, u.rangeSemitones);
    const double span = std::max (0.25, outer);
    float top = 0.0f;
    for (int i = 0; i < l.count; ++i)
        top = std::max (top, l.gain[static_cast<std::size_t> (i)]);
    top = std::max (top, 1.0e-3f);

    // The centre line (the note) and the baseline; the edges' pitch.
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (area.getX(), base, area.getRight(), base, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawLine (cx, area.getY(), cx, base, 1.0f);
    g.setFont (fonts::label (9.5f));
    g.setColour (juce::Colours::white.withAlpha (0.38f));
    const auto foot = juce::Rectangle<float> (bounds.getX() + 8.0f, base + 1.0f, bounds.getWidth() - 16.0f, 13.0f);
    if (u.voices > 1)
    {
        const auto cents = juce::String (juce::roundToInt (100.0 * outer)) + " ct";
        g.drawText ("-" + cents, foot, juce::Justification::centredLeft, false);
        g.drawText ("+" + cents, foot, juce::Justification::centredRight, false);
    }
    g.drawText (u.voices > 1 ? juce::String (u.voices) + " VOICES" : juce::String ("1 VOICE"), foot, juce::Justification::centred, false);

    const float barWidth = juce::jlimit (4.0f, 12.0f, area.getWidth() / static_cast<float> (3 * std::max (2, l.count)));
    for (int i = 0; i < l.count; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        const float x = cx + static_cast<float> (l.semitones[k] / span) * 0.5f * area.getWidth();
        const float h = std::max (3.0f, l.gain[k] / top * area.getHeight());
        const auto bar = juce::Rectangle<float> (barWidth, h).withCentre ({ x, base - 0.5f * h });
        // Its side: left copies cooler, right ones warmer, the centre the card's own colour.
        const float side = 2.0f * l.place[k] - 1.0f;
        const auto ink = side < -0.01f ? colour.interpolatedWith (juce::Colour (0xff9cc3e6), 0.85f * -side)
                                       : colour.interpolatedWith (juce::Colour (0xfff0c48a), 0.85f * side);
        g.setColour (ink.withAlpha (0.85f));
        g.fillRoundedRectangle (bar, 2.0f);
        if (std::abs (side) > 0.01f)
        {
            // (a small tick above the bar, leaning to its side)
            const float lean = 5.0f * side;
            g.setColour (ink.withAlpha (0.55f));
            g.drawLine (x, bar.getY() - 3.0f, x + lean, bar.getY() - 7.0f, 1.2f);
        }
    }
}

//==============================================================================
UnisonPanel::UnisonPanel (InstrumentHost& h, int c)
    : MiniPanel ("UNISON", juce::String ("CARD ") + static_cast<char> ('A' + juce::jlimit (0, 2, c)) + juce::String::fromUTF8 (" Â· OSC ") + juce::String (juce::jlimit (0, 2, c) + 1)),
      host (h), cardIndex (juce::jlimit (0, 2, c))
{
    auto& state = host.apvts();
    const auto colour = cardColour (cardIndex);
    pic = std::make_unique<UnisonPicture> (host, cardIndex);
    addAndMakeVisible (*pic);
    auto segments = [&] (const juce::String& pid, juce::StringArray items, const char* tooltip) {
        auto s = std::make_unique<SegmentedControl> (*state.getParameter (pid), std::move (items));
        s->setAccent (colour);
        s->setTooltip (tooltip);
        binding::tag (*s, pid);
        addAndMakeVisible (*s);
        return s;
    };
    auto id = [this] (const char* name) { return params::unisonId (cardIndex, name); };
    voices = segments (id ("voices"), { "1", "2", "3", "4", "5", "6", "7", "8" },
                       "How many copies of the oscillator each note plays (1: the oscillator alone). Every note keeps its own copies and envelopes");
    mode = segments (id ("mode"), { "LINEAR", "SUPER", "EXP", "INV", "RANDOM" },
                     "Where the inner copies sit between the outer two: evenly (LINEAR), drawn to the centre (SUPER, EXP), pushed out (INV), or each note its own (RANDOM)");
    auto knob = [&] (const juce::String& pid, const char* caption, const char* tooltip) {
        auto* p = state.getParameter (pid);
        auto k = std::make_unique<MiniKnob> (state, pid, caption, textOf (*p));
        k->setArcColour (colour);
        k->slider.setTooltip (tooltip);
        binding::tag (k->slider, pid);
        addAndMakeVisible (*k);
        return k;
    };
    detune = knob (id ("detune"), "DETUNE", "How far the outer copies are detuned (most of the knob is fine detune)");
    blend = knob (id ("blend"), "BLEND", "The outer copies against the centre: faint at 0, louder near the top. The loudness stays the same");
    spread = knob (id ("width"), "WIDTH", "The copies left and right by turns (0: mono)");
    range = knob (id ("range"), "RANGE", "The outer copies' detune at DETUNE 100 % (semitones)");
    fine = knob (fineIdOf (cardIndex), "FINE", "The oscillator's fine tuning (cents)");
    if (cardIndex == 2)
        velo = knob (params::hybridVelo, "VELO", "How much card C's level follows velocity");
    refreshContent();
}

UnisonPanel::~UnisonPanel() = default;

juce::Point<int> UnisonPanel::cardSize() const
{
    // Header 32, the picture 96, VOICES 24, MODE 24, the knobs 92, gaps.
    return { width, 32 + 96 + 10 + 24 + 8 + 24 + 8 + 92 + 10 };
}

void UnisonPanel::layoutContent (juce::Rectangle<int> area)
{
    pic->setBounds (area.removeFromTop (96));
    area.removeFromTop (10);
    voices->setBounds (area.removeFromTop (24));
    area.removeFromTop (8);
    mode->setBounds (area.removeFromTop (24));
    area.removeFromTop (8);
    auto knobs = area.removeFromTop (92);
    const int count = velo != nullptr ? 6 : 5;
    const int w = knobs.getWidth() / count;
    detune->setBounds (knobs.removeFromLeft (w));
    blend->setBounds (knobs.removeFromLeft (w));
    spread->setBounds (knobs.removeFromLeft (w));
    range->setBounds (knobs.removeFromLeft (w));
    if (velo != nullptr)
    {
        fine->setBounds (knobs.removeFromLeft (w));
        velo->setBounds (knobs);
    }
    else
        fine->setBounds (knobs);
}

void UnisonPanel::refreshContent()
{
    // One copy: the copies' controls rest (they act from two copies on); FINE always acts.
    const bool on = settingsOf (host, cardIndex).on();
    for (auto* c : { static_cast<juce::Component*> (mode.get()), static_cast<juce::Component*> (detune.get()), static_cast<juce::Component*> (blend.get()),
                     static_cast<juce::Component*> (spread.get()), static_cast<juce::Component*> (range.get()) })
        c->setAlpha (on ? 1.0f : 0.45f);
    pic->refresh();
}

//==============================================================================
UnisonChip::UnisonChip (InstrumentHost& h, int c, juce::Colour a) : host (h), card (juce::jlimit (0, 2, c)), accent (a)
{
    setWantsKeyboardFocus (true);
    setTitle (juce::String ("Unison ") + static_cast<char> ('A' + card) + " voices");
    if (auto* p = host.apvts().getParameter (params::unisonId (card, "voices")))
        attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float) { voicesChanged(); }, &host.undo());
    voicesChanged();
}

UnisonChip::~UnisonChip() = default;

int UnisonChip::voices() const
{
    return 1 + juce::jlimit (0, unisonmath::maxVoices - 1, juce::roundToInt (host.parameterValue (params::unisonId (card, "voices"))));
}

void UnisonChip::step (int delta)
{
    auto* p = host.apvts().getParameter (params::unisonId (card, "voices"));
    if (p == nullptr)
        return;
    const int next = juce::jlimit (1, unisonmath::maxVoices, voices() + delta);
    if (next == voices())
        return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (next - 1)));
    p->endChangeGesture();
    repaint();
}

void UnisonChip::openPanel()
{
    auto* owner = findParentComponentOfClass<AnchoredPopoverHost>();
    if (owner == nullptr)
        return;
    if (owner->anchoredPopoverOpenFrom (*this))
        owner->closeAnchoredPopover();   // (the chip again closes it)
    else
        owner->openAnchoredPopover (std::make_unique<UnisonPanel> (host, card), *this);
    repaint();
}

void UnisonChip::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const int n = voices();
    const bool on = n > 1;
    // The model dropdown's language: plain on the dark well, the accent once there are copies.
    g.setColour (on ? accent.withAlpha (0.16f) : juce::Colours::white.withAlpha (0.04f));
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (on ? accent.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.14f));
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    const auto ink = on ? accent.brighter (0.35f) : juce::Colours::white.withAlpha (0.5f);
    g.setFont (fonts::label (13.0f));
    const float third = b.getWidth() / 3.0f;
    g.setColour (ink.withMultipliedAlpha (n > 1 ? 0.9f : 0.35f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x80\xb9"), b.withWidth (third), juce::Justification::centred, false);   // ‹
    g.setColour (ink.withMultipliedAlpha (n < unisonmath::maxVoices ? 0.9f : 0.35f));
    g.drawText (juce::String::fromUTF8 ("\xe2\x80\xba"), b.withTrimmedLeft (2.0f * third), juce::Justification::centred, false);   // ›
    g.setColour (ink);
    g.setFont (fonts::label (11.5f));
    g.drawText (juce::String (n), b.withTrimmedLeft (third).withWidth (third), juce::Justification::centred, false);
    if (hasKeyboardFocus (false))
    {
        g.setColour (accent.withAlpha (0.8f));
        g.drawRoundedRectangle (b.expanded (1.0f), 5.0f, 1.2f);
    }
}

void UnisonChip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    const float third = static_cast<float> (getWidth()) / 3.0f;
    if (static_cast<float> (e.x) < third)
        step (-1);
    else if (static_cast<float> (e.x) > 2.0f * third)
        step (1);
    else
        openPanel();
}

void UnisonChip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY != 0.0f)
        step (wheel.deltaY > 0.0f ? 1 : -1);
}

bool UnisonChip::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey)
        step (-1);
    else if (key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey)
        step (1);
    else if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
        openPanel();
    else
        return false;
    return true;
}

void UnisonChip::voicesChanged()
{
    const int n = voices();
    setTooltip (juce::String ("UNISON: ") + (n > 1 ? juce::String (n) + " copies of the oscillator in every note" : juce::String ("one voice (off)"))
                + juce::String::fromUTF8 (". \xe2\x80\xb9 \xe2\x80\xba step it; the number opens the card's UNISON (DETUNE, BLEND, WIDTH, MODE, RANGE, FINE)"));
    repaint();
}

} // namespace andor::analog
