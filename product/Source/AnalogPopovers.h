#pragma once

#include "ShapingPopups.h"

#include <memory>

namespace andor::analog
{

/** The settings behind macro `macro` (data order: BLEND, DRIVE, TONE, MOTION, SPACE, ECHO),
    built from the shared popover parts (MiniPanel, MiniKnob, SegmentedControl, ValueSelector,
    the look-and-feel's toggle) in the macro's identity colour. */
std::unique_ptr<osp::plugin::MiniPanel> createMacroPopover (int macro, osp::plugin::InstrumentHost& host);

} // namespace andor::analog
