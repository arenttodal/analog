#pragma once

#include "InstrumentHost.h"
#include "MainSections.h"
#include "ShaperDna.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace andor::analog
{

/**
    SHAPER DNA's panel (docs/SHAPER_DNA_V1.md), opened from the DNA key on the MOVEMENT rack's
    SHAPER display. The SUB / SPACE popovers' language: a compact bone card, one dark display,
    a few keys. Interface only: every control is one of DNA's parameters (host automation and
    undo as any other); TARGET, SMOOTH and DEPTH stay on the rack, once.
*/
namespace dnaview
{
    /** The profiles' colours: the instrument's copper, slate and sage. */
    juce::Colour profileColour (int profile) noexcept;
    /** DNA's settings and lane A's SHAPER as the parameters hold them now. */
    ShaperDnaSettings settingsOf (osp::plugin::InstrumentHost& host);
    osp::ShaperParams shaperOf (osp::plugin::InstrumentHost& host);
} // namespace dnaview

/**
    The dark display: each profile's curve over one bar (drawn from RhythmicShaper::evaluate and
    the real settings: a still picture, never a meter), then, under them, KEY ZONES' keyboard
    with its two split handles (dragged: one host gesture each, never crossing) or CYCLE's
    sequence of profiles, note by note.
*/
class ShaperDnaDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ShaperDnaDisplay (osp::plugin::InstrumentHost& host);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Repaints when what it draws changed (the panel's refresh; no timer of its own). */
    void refresh();

    // Geometry and state (tests, screenshots).
    enum class Split { none, ab, bc };
    juce::Rectangle<float> laneArea (int profile) const;
    juce::Rectangle<float> stripArea() const noexcept { return strip; }
    float keyX (int key) const noexcept;
    int keyAtX (float x) const noexcept;
    juce::Point<float> splitHandle (Split) const;
    Split splitAt (juce::Point<float>) const;
    Split dragged() const noexcept { return drag; }
    /** CYCLE's sequence as drawn ("AABBCC..."); empty in KEY ZONES. */
    juce::String cycleText() const;
    static constexpr int firstKey = 24, lastKey = 108;   ///< C1 .. C8 shown (the splits are stored 0..127)

private:
    juce::String stateKey() const;
    osp::plugin::InstrumentHost& host;
    juce::ParameterAttachment splitABA, splitBCA;
    juce::Rectangle<float> lanes, strip;
    Split drag = Split::none, hover = Split::none;
    juce::String drawnFor;   ///< what the last paint showed (refresh compares)
};

class ShaperDnaPanel final : public osp::plugin::MiniPanel
{
public:
    explicit ShaperDnaPanel (osp::plugin::InstrumentHost& host);
    ~ShaperDnaPanel() override;

    static constexpr int width = 372;
    bool compact() const override { return true; }
    juce::Point<int> cardSize() const override;
    void paint (juce::Graphics&) override;
    void refreshContent() override;
    void settleNow() override { refreshContent(); }

    ShaperDnaDisplay& display() noexcept { return *dnaDisplay; }
    osp::plugin::MacroSwitchLamp& lamp() noexcept { return *onLamp; }
    osp::plugin::SegmentedControl& assignmentControl() noexcept { return *assignment; }
    osp::plugin::SegmentedControl& profilesControl() noexcept { return *profiles; }
    osp::plugin::SegmentedControl& notesControl() noexcept { return *notes; }
    osp::plugin::ValueSelector& patternSelector (int profile) noexcept { return *patternSelectors[static_cast<std::size_t> (profile)]; }
    osp::plugin::ValueSelector& rateSelector (int profile) noexcept { return *rateSelectors[static_cast<std::size_t> (profile)]; }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    osp::plugin::InstrumentHost& host;
    std::unique_ptr<osp::plugin::MacroSwitchLamp> onLamp;
    std::unique_ptr<ShaperDnaDisplay> dnaDisplay;
    std::unique_ptr<osp::plugin::SegmentedControl> assignment, profiles, notes;
    std::array<std::unique_ptr<osp::plugin::ValueSelector>, 3> patternSelectors, rateSelectors;
    std::array<juce::Rectangle<int>, 3> rowBadges;
    juce::Rectangle<int> notesCaption, divider;
};

} // namespace andor::analog
