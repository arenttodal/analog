#pragma once

#include "ShapingPopups.h"

#include <memory>

namespace andor::analog
{

/** The settings behind macro `macro` (data order: BLEND, DRIVE, TONE, MOVEMENT, SPACE, ECHO).
    BLEND and TONE are the instrument's own, built from the shared popover parts (MiniPanel,
    MiniKnob, SegmentedControl, ValueSelector, the look-and-feel's toggle) in the macro's
    identity colour; DRIVE, MOVEMENT, SPACE and ECHO are ANDOR OSP's own popovers (MacroPopups). */
std::unique_ptr<osp::plugin::MiniPanel> createMacroPopover (int macro, osp::plugin::InstrumentHost& host);

} // namespace andor::analog
