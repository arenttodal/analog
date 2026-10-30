#pragma once

#include "ShapingPopups.h"

#include <memory>

namespace andor::analog
{

/** The settings behind macro `macro` (data order: the first not assigned yet, DRIVE, FILTER,
    MOVEMENT, SPACE, ECHO). FILTER is the instrument's own, built from the shared popover parts
    (MiniPanel, MiniKnob, SegmentedControl, ValueSelector, the look-and-feel's toggle) in the
    macro's identity colour; DRIVE, MOVEMENT, SPACE and ECHO are ANDOR OSP's own popovers
    (MacroPopups). The first has none. */
std::unique_ptr<osp::plugin::MiniPanel> createMacroPopover (int macro, osp::plugin::InstrumentHost& host);
/** The oscillators' settings (models, waves, tuning, levels, LFO depths, the pair, Poly Mod,
    noise, voices, KEY DNA): opened by the card's OSCILLATORS heading. */
std::unique_ptr<osp::plugin::MiniPanel> createOscillatorPopover (osp::plugin::InstrumentHost& host);

} // namespace andor::analog
