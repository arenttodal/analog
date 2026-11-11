#pragma once

#include "InstrumentHost.h"
#include "MainSections.h"
#include "ShapingPopups.h"
#include "engine/Modulation.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace andor::analog
{

/**
    RHYTHMIC MORPH's controls (docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md, gate 7): card C's
    own, so they live in card C - a small RHYTHM key in its well (while it plays its recording)
    opens this panel beside it. Not a fourth card, not a second page: the SATELLITE / SUB / SHAPER
    DNA popovers' language (a bone card, one dark display, a few keys and knobs).
*/
namespace rhythmview
{
    /** Card C's identity colour (the panel's and the key's accent). */
    juce::Colour accent() noexcept;
    /** The quick routes: C RHYTHM to card A's and card B's LEVEL, the FILTER, MORPH. */
    enum class Target { levelA, levelB, filter, morph };
    constexpr std::array<Target, 4> targets { Target::levelA, Target::levelB, Target::filter, Target::morph };
    const char* targetName (Target t) noexcept;
    /** The route parameter's choice for a target (its destination). */
    int targetChoice (Target t);
    /** The route that takes C RHYTHM to the target (-1: none). */
    int routeTo (osp::plugin::InstrumentHost& host, Target t);
    /**
        Takes C RHYTHM to the target, or (routed) takes the route away: one undo step. A new route
        goes in the first free slot (none free: nothing happens, false) with a musical depth, and
        RHYTHM is switched on and set to feed it (AUDIO becomes BOTH).
    */
    bool toggleRoute (osp::plugin::InstrumentHost& host, Target t);
    /** What the analysis says, in a line (the display's readout; tests). */
    juce::String readout (osp::plugin::InstrumentHost& host);
}

/** The key in card C's well: its light switches RHYTHM, its name opens the panel. */
class RhythmKey final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit RhythmKey (osp::plugin::InstrumentHost& host);
    ~RhythmKey() override;

    static constexpr int preferredWidth = 84, preferredHeight = 22;
    bool isOn() const;
    bool isOpen() const;
    juce::Rectangle<int> lightArea() const { return getLocalBounds().withWidth (getHeight()); }
    void toggle();
    void openPanel();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    void timerCallback() override;
    osp::plugin::InstrumentHost& host;
    bool shownOn = false, shownOpen = false;
};

/**
    The loop as the analysis heard it: its level over its bars (beat and bar lines, the attacks
    as ticks), SENSITIVITY's threshold, the playhead where the audio thread reads it and C
    RHYTHM's value at the right edge - the engine's own telemetry, never an animation.
*/
class RhythmDisplay final : public juce::Component
{
public:
    explicit RhythmDisplay (osp::plugin::InstrumentHost& host);
    void paint (juce::Graphics&) override;
    void refresh();
    /** Tests: where the playhead is drawn (local x; < 0: none) and the meter's value. */
    float playheadX() const noexcept { return shownPlayheadX; }
    float meterValue() const noexcept { return shownValue; }

private:
    osp::plugin::InstrumentHost& host;
    juce::String drawnFor;
    float shownPlayheadX = -1.0f, shownValue = 0.0f;
};

class RhythmPanel final : public osp::plugin::MiniPanel
{
public:
    explicit RhythmPanel (osp::plugin::InstrumentHost& host);
    ~RhythmPanel() override;

    static constexpr int width = 420;
    bool compact() const override { return true; }
    juce::Point<int> cardSize() const override;
    void paint (juce::Graphics&) override;
    void refreshContent() override;
    void settleNow() override { refreshContent(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    RhythmDisplay& display() noexcept { return *loopDisplay; }
    osp::plugin::MacroSwitchLamp& lamp() noexcept { return *onLamp; }
    osp::plugin::SegmentedControl& timingControl() noexcept { return *timing; }
    osp::plugin::SegmentedControl& tempoControl() noexcept { return *tempo; }
    osp::plugin::SegmentedControl& audioControl() noexcept { return *audio; }
    osp::plugin::SegmentedControl& shapeControl() noexcept { return *shape; }
    osp::plugin::MiniKnob& sensitivityKnob() noexcept { return *sensitivity; }
    osp::plugin::MiniKnob& attackKnob() noexcept { return *attack; }
    osp::plugin::MiniKnob& releaseKnob() noexcept { return *release; }
    osp::plugin::MiniKnob& depthKnob() noexcept { return *depth; }
    juce::Rectangle<int> routeKeyArea (rhythmview::Target t) const { return routeKeys[static_cast<std::size_t> (t)]; }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    osp::plugin::InstrumentHost& host;
    std::unique_ptr<osp::plugin::MacroSwitchLamp> onLamp;
    std::unique_ptr<RhythmDisplay> loopDisplay;
    std::unique_ptr<osp::plugin::SegmentedControl> timing, tempo, audio, shape;
    std::unique_ptr<osp::plugin::MiniKnob> sensitivity, attack, release, depth;
    std::array<juce::Rectangle<int>, 4> routeKeys {};
    juce::Rectangle<int> routeCaption;
    int hoveredRoute = -1;
    juce::String routesDrawnFor;
};

} // namespace andor::analog
