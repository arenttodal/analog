#pragma once

#include "InstrumentHost.h"
#include "MainSections.h"
#include "ShapingPopups.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace andor::analog
{

/**
    UNISON (state version 24): one oscillator card's copies, drawn from its real settings. Each copy
    is a bar: across, where its detune puts it (flat left of the centre line, sharp right); its
    height, its level (BLEND); its colour, the side WIDTH sends it to. One copy: a single bar.
*/
class UnisonPicture final : public juce::Component
{
public:
    UnisonPicture (osp::plugin::InstrumentHost& host, int card);
    void paint (juce::Graphics&) override;
    /** Repaints when the settings it draws changed (the panel's refresh). */
    void refresh();

private:
    osp::plugin::InstrumentHost& host;
    int card = 0;
    juce::String drawnFor;
};

/**
    The UNISON fan, over an oscillator card's waveform while its UNISON changes: one line per
    copy, drawn from the engine's own layout (unisonmath::layout). Across, where its detune puts
    it (the outer copies' detune in cents beneath them); its height, its level (one oscillator's
    level reaches the well's half height): BLEND lifts the outer lines and the centre sinks, the
    sum's loudness the same; its colour, the card's for the centre copy, ivory to the left and
    amber to the right as WIDTH sends them. It fades in when VOICES, DETUNE, BLEND, WIDTH, MODE
    or RANGE changes (a hand, a preset, the host) and out about 1.5 s after the last change.
    Presentation only: never takes the mouse, never in a preset.
*/
class UnisonFan final : public juce::Component, private juce::Timer
{
public:
    UnisonFan (osp::plugin::InstrumentHost& host, int card, juce::Colour accent);
    ~UnisonFan() override;
    void paint (juce::Graphics&) override;
    /** Looks for a change of the card's UNISON (the overlay's tick, and its own frames while shown). */
    void poll();

    /** Tests: shown (fading in, held, or fading out) and how opaque. */
    bool showing() const noexcept { return alpha > 0.001f || holdUntil > 0.0; }
    float opacity() const noexcept { return alpha; }
    /** Tests: as if the fade had finished (fully shown while held, or gone after it). */
    void settleForTest();
    /** The caption it shows (tests). */
    juce::String caption() const;

    static constexpr double holdSeconds = 1.2, fadeInSeconds = 0.10, fadeOutSeconds = 0.35;

private:
    void timerCallback() override;
    juce::String keyNow() const;
    osp::plugin::InstrumentHost& host;
    int card = 0;
    juce::Colour accent;
    juce::String seen;
    float alpha = 0.0f;
    double holdUntil = 0.0;
};

/**
    The oscillator card's settings (opened from its voices chip, in the card's well): UNISON's
    copies (VOICES, DETUNE, BLEND, WIDTH, MODE, RANGE) under their picture, and the card's FINE
    tuning (cents; card C as an oscillator also its VELO). The SATELLITE / SUB popovers' language.
*/
class UnisonPanel final : public osp::plugin::MiniPanel
{
public:
    /** card: 0 A (OSC 1), 1 B (OSC 2), 2 C (OSC 3). */
    UnisonPanel (osp::plugin::InstrumentHost& host, int card);
    ~UnisonPanel() override;

    static constexpr int width = 452;
    bool compact() const override { return true; }
    juce::Point<int> cardSize() const override;
    void refreshContent() override;
    void settleNow() override { refreshContent(); }

    int card() const noexcept { return cardIndex; }
    UnisonPicture& picture() noexcept { return *pic; }
    osp::plugin::SegmentedControl& voicesControl() noexcept { return *voices; }
    osp::plugin::SegmentedControl& modeControl() noexcept { return *mode; }
    osp::plugin::MiniKnob& detuneKnob() noexcept { return *detune; }
    osp::plugin::MiniKnob& blendKnob() noexcept { return *blend; }
    osp::plugin::MiniKnob& widthKnob() noexcept { return *spread; }
    osp::plugin::MiniKnob& rangeKnob() noexcept { return *range; }
    osp::plugin::MiniKnob& fineKnob() noexcept { return *fine; }
    osp::plugin::MiniKnob* veloKnob() noexcept { return velo.get(); }

protected:
    void layoutContent (juce::Rectangle<int> area) override;

private:
    osp::plugin::InstrumentHost& host;
    int cardIndex = 0;
    std::unique_ptr<UnisonPicture> pic;
    std::unique_ptr<osp::plugin::SegmentedControl> voices, mode;
    std::unique_ptr<osp::plugin::MiniKnob> detune, blend, spread, range, fine, velo;
};

/**
    The card's voices chip (the well's top right, beside the model): "‹ 4 ›" - the arrows step
    UNISON's VOICES (1..8), the number opens the card's UNISON panel. One voice reads "1" dimmed.
*/
class UnisonChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    UnisonChip (osp::plugin::InstrumentHost& host, int card, juce::Colour accent);
    ~UnisonChip() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    /** The width it wants (px). */
    static constexpr int preferredWidth = 52;

    int voices() const;
    void step (int delta);
    void openPanel();

private:
    void voicesChanged();
    osp::plugin::InstrumentHost& host;
    int card = 0;
    juce::Colour accent;
    /** Follows VOICES (a preset, the host, the panel): repaints only when it changes - no timer. */
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

} // namespace andor::analog
