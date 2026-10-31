#pragma once

#include "CardWellEditor.h"
#include "InstrumentHost.h"

#include <memory>

namespace andor::analog
{

/**
    The OSCILLATOR WORKBENCH: ANDOR ANALOG's oscillators edited in the card's own dark well,
    in the waveform's place (the shell's in-card editor, opened by the OSCILLATORS heading).

    The pages: BOTH (the first: the two oscillators at once, each its own equal section:
    model, wave and character, pitch and level; side by side in a wide well, two strips in a
    two-card one), OSC 1 and OSC 2 (one oscillator in focus, on one grid: WAVE -> PITCH ->
    SHAPE -> OUTPUT; DETAILED adds a row of its LFO depths and model-specific depth), PAIR
    (INTERACTION and POLY MOD; DETAILED adds VOICES and NOISE; KEY DNA is ANDOR OSP's own
    editor). The focused pages have a depth, QUICK or DETAILED. PIN keeps it open.

    Interface only: every control is one of the instrument's parameters (their attachments:
    automation, undo and the host see what they always did); only the page shown exists, so
    no parameter is ever attached twice. Pictures are computed only when a setting they show
    changes, never from the audio.
*/
std::unique_ptr<osp::plugin::CardWellEditor> createOscillatorWorkbench (osp::plugin::InstrumentHost& host);

/** The pages, in their order (CardWellEditor::ViewState::section). */
enum class WorkbenchSection { both, osc1, osc2, pair, count };

} // namespace andor::analog
