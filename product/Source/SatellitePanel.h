#pragma once

#include "InstrumentHost.h"
#include "MainSections.h"
#include "ShapingPopups.h"
#include "engine/SatelliteMotion.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace andor::analog
{

/**
    SATELLITE's panel (docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md), opened from the SATELLITE
    key on the per-card arpeggiator's title row. The SUB / SHAPER DNA popovers' language: a compact
    bone card, one dark display, a few keys and knobs. A / B / C choose whose settings it shows
    (choosing never switches a card); the light on the right switches the shown card.
*/
namespace satview
{
    osp::satellite::Settings settingsOf (osp::plugin::InstrumentHost& host, int card);
    juce::Colour cardColour (int card) noexcept;
}

/**
    The path, from the card's real settings: PAN's track, ORBIT's circle (its far half quieter),
    BOUNCE's two places. The marker is where the audio has the card now (the engine's own
    telemetry, not an animation); with nothing moving it rests, hollow, where the path starts.
*/
class SatelliteDisplay final : public juce::Component
{
public:
    SatelliteDisplay (osp::plugin::InstrumentHost& host, int card);
    void setCard (int card);
    void paint (juce::Graphics&) override;
    /** Repaints when the path or the marker moved (the panel's refresh). */
    void refresh();
    /** Where the marker is drawn (tests: the live place, in the display's coordinates). */
    juce::Point<float> markerAt() const noexcept { return marker; }
    bool markerLive() const noexcept { return live; }

private:
    osp::plugin::InstrumentHost& host;
    int card = 0;
    juce::String drawnFor;
    juce::Point<float> marker;
    bool live = false;
};

class SatellitePanel final : public osp::plugin::MiniPanel
{
public:
    SatellitePanel (osp::plugin::InstrumentHost& host, int card);
    ~SatellitePanel() override;

    static constexpr int width = 420;
    bool compact() const override { return true; }
    juce::Point<int> cardSize() const override;
    void paint (juce::Graphics&) override;
    void refreshContent() override;
    void settleNow() override { refreshContent(); }
    void mouseDown (const juce::MouseEvent&) override;

    /** The card shown (0 A .. 2 C): its settings; never switches it. */
    void showCard (int card);
    int shownCard() const noexcept { return cardIndex; }
    juce::Rectangle<int> cardChipArea (int c) const { return chips[static_cast<std::size_t> (juce::jlimit (0, 2, c))]; }

    SatelliteDisplay& display() noexcept { return *pathDisplay; }
    osp::plugin::MacroSwitchLamp& lamp() noexcept { return *onLamp; }
    osp::plugin::SegmentedControl& modeControl() noexcept { return *mode; }
    osp::plugin::SegmentedControl& timingControl() noexcept { return *timing; }
    osp::plugin::SegmentedControl& bounceControl() noexcept { return *bounce; }
    osp::plugin::MiniKnob& divisionKnob() noexcept { return *division; }
    osp::plugin::MiniKnob& rateKnob() noexcept { return *rate; }
    osp::plugin::MiniKnob& depthKnob() noexcept { return *depth; }
    osp::plugin::MiniKnob& widthKnob() noexcept { return *spread; }
    osp::plugin::MiniKnob& phaseKnob() noexcept { return *phase; }
    osp::plugin::MiniKnob& offsetKnob() noexcept { return *offset; }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    void build();
    osp::plugin::InstrumentHost& host;
    int cardIndex = 0;
    std::unique_ptr<osp::plugin::MacroSwitchLamp> onLamp;
    std::unique_ptr<SatelliteDisplay> pathDisplay;
    std::unique_ptr<osp::plugin::SegmentedControl> mode, timing, bounce;
    std::unique_ptr<osp::plugin::MiniKnob> division, rate, depth, spread, phase, offset;
    std::array<juce::Rectangle<int>, 3> chips {};
    juce::Rectangle<int> keysRow, bounceCaption;
};

} // namespace andor::analog
