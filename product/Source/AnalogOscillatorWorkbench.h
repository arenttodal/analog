#pragma once

#include "CardWellEditor.h"
#include "InstrumentHost.h"

#include <memory>

namespace andor::analog
{

/**
    The OSCILLATOR WORKBENCH: ANDOR ANALOG's oscillators edited in the card's own dark well,
    in the waveform's place (the shell's in-card editor, opened by the OSCILLATORS heading).

      - QUICK: both oscillators side by side, each its model, ON, its waves, a picture of its
        own cycle and its three or four most telling controls (by model).
      - DETAILED: one section at a time, OSC 1 | OSC 2 | PAIR | MORE | KEY DNA: every
        oscillator setting the engine plays, the pair's (SYNC, KEYBOARD, LO FREQ, RING, POLY
        MOD), the noise and the voices, GRANULAR's KEY DNA.
      - WAVE: back to the waveform; PIN: keep it open (after the EQ, when the window opens).

    Interface only: every control is one of the instrument's parameters (their attachments:
    automation, undo and the host see what they always did); only the view being shown
    exists, so no parameter is ever attached twice. Pictures are computed only when a setting
    they show changes, never from the audio.
*/
std::unique_ptr<osp::plugin::CardWellEditor> createOscillatorWorkbench (osp::plugin::InstrumentHost& host);

/** The Detailed sections, in their order (CardWellEditor::ViewState::section). */
enum class WorkbenchSection { osc1, osc2, pair, more, keyDna, count };

} // namespace andor::analog
