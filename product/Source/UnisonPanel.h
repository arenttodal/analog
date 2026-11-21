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
