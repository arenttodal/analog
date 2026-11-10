#pragma once

#include "InstrumentHost.h"
#include "WellOverlay.h"

#include <memory>

namespace andor::analog
{

/**
    An oscillator's sound design inside its card's dark well, revealed by the pointer (the
    three-engine interface: docs/THREE_ENGINE_ARCHITECTURE.md). One component for OSC 1 (card A),
    OSC 2 (card B) and OSC 3 (card C as an oscillator), bound to that slot's parameters:

    - always shown: the MODEL dropdown (CLASSIC / DCO / WIDE), top right;
    - revealed on hover, on one grid of five cells along the well's foot, the selected model's
      timbre controls only:
      CLASSIC  WAVE, WIDTH, LFO PW;
      DCO      SAW + PULSE (independent), SUB, WIDTH, PWM, PWM SOURCE;
      WIDE     WAVE, SHAPE, SPREAD (adjacent), WIDTH, LFO PW.
      A control the current wave leaves inactive is dimmed in its place (nothing moves).
    - OSC 2 adds its KEY (key tracking) and LOW (low-frequency) switches.

    Pitch and level are the card's own knobs (OCT, TUNE, DETUNE, LEVEL), never here. Remade
    by the shell when the model changes (oscillatorOverlayKey), so its controls always match
    what plays; every model's settings are kept when another plays.
*/
std::unique_ptr<osp::plugin::WellOverlay> createOscillatorOverlay (osp::plugin::InstrumentHost& host, int slot, int layer);
/** What the oscillator overlay depends on (its model): the shell remakes it when this changes. */
juce::String oscillatorOverlayKey (osp::plugin::InstrumentHost& host, int slot);

/**
    Card C's well: its TYPE dropdown (SAMPLE / OSCILLATOR / NOISE / INSTRUMENT PATCH), always
    shown at the top right, and the selected type's own controls revealed on hover:
    OSCILLATOR  OSC 3, as OSC 1 and OSC 2 (its MODEL dropdown beside the type);
    NOISE       COLOR, TONE, MOVEMENT;
    INSTRUMENT  SOUND (the imported sounds, import, remove) and the sound's name;
    SAMPLE      nothing more: ANDOR OSP's own controls (LOOP, GRANULAR, its grains) are the card's.
    Remade when the type (or OSC 3's model) changes.
*/
std::unique_ptr<osp::plugin::WellOverlay> createHybridOverlay (osp::plugin::InstrumentHost& host);
juce::String hybridOverlayKey (osp::plugin::InstrumentHost& host);
/** Cinematic V6 gate 3: which of card C's layers the well and the knob row show (1, or 2 while
    the second layer is on). An interface choice, saved with the session's view. */
int hybridLayerShown (osp::plugin::InstrumentHost& host);
void showHybridLayer (osp::plugin::InstrumentHost& host, int layer);

} // namespace andor::analog
