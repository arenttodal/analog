# Cinematic V6 — Gate 3: card C's second layer (C2)

Card C can hold two layers. C2 is "just another layer in C" (the owner's direction), so you
can layer two instruments. Everything after the layers is card C's and is shared:

- card C's M / S;
- its LOOP / FOLLOW modifiers;
- its EQ and DAMPEN;
- its arpeggiator lane;
- MORPH;
- the filter and every effect and route.

There is no fourth card and no fourth arpeggiator lane.

## Decisions (owner's)

- **C2 is a layer, not a source with its own routing.** It is summed into card C's own part
  inside each voice, before card C's EQ/DAMPEN, MORPH, the filter and the effects. So the
  spec's per-source items are not built here:
  - a separate arpeggiator lane for C2;
  - a separate send for C2;
  - C2's own EQ;
  - a MORPH target selector (C1 or C2). MORPH hears card C as a whole: C1 and C2 together.
- **C2's SAMPLE is a simple player.** A dropped or chosen recording becomes one zone over every
  key, rooted at its pitch as ANDOR OSP's analysis finds it (else C4). It plays through the
  instrument-patch zone player, so it gets LOOP and FOLLOW from card C but no OSP
  continuation or granular (C1 keeps those). It cannot play REVERSE.

## Engine (`dsp/Synth`, `Patch::hybrid2`)

- **What C2 plays:** OSCILLATOR (the slots' CLASSIC / DCO / WIDE models), NOISE (card C's
  noise), or its own loaded source (SAMPLE: a recording; INSTRUMENT PATCH: an imported sound).
  Both loaded types play the one source slot C2 has.
- **C2's own settings:** LEVEL (−48..+6 dB), OCT, TUNE, DETUNE and VELO, plus its type's
  settings.
- **What it shares with C1:** card C's M / S, its share of the A / C mix, its lane (bit 2 of
  the card mask) and LOOP / FOLLOW. FOLLOW off lets C2's zones play out, as C1's do.
- **Separate per-voice state.** C2 has its own zone players, oscillator and noise.
  - These are fixed arrays: no allocation.
  - Swapping its source mid-note fades the old zones from their tails (`setSampleSource2`).
  - C2 never shares a `SampleSource` pointer with C1.
- **Off is exact.** With C2 off, nothing of it is heard or computed. Every older render is
  bit-identical (`[v6-baseline]`, `[legacy]`, `[fx]` unchanged).
- **The A / C mix.** Once C2 sounds, card C sounds, so the A / C mix (ANALOG's BLEND law)
  applies, just as it does when C1 plays an oscillator.

## Parameters and state

- **State version 16.** 22 `analog.hybrid2.*` parameters are appended (version hint 31). C2
  is off by default.
- **C2's source** is saved as the engine's `Hybrid2Source` state child, which holds its
  `SampleSource` definition. Its recording is copied into the instrument's sample store under
  a content name, as imported sounds are.
- **Nothing to migrate.**

## Interface

- **The layer keys** are small graphite keys at the top left of card C's well, under the card's
  own modifier keys.
  - With one layer there is only **+** ("Add Hybrid layer"). It is revealed on hover, so card C
    looks exactly as before.
  - With two layers, **1** and **2** choose which layer the well and the knob row show, and
    **×** (shown on 2) removes the second layer. C2's settings are kept for next time.
- **With 2 shown, card C is C2's:**
  - **Well:** its TYPE dropdown and the type's controls (an oscillator's model controls, NOISE's
    COLOR, SOUNDS, or the recording's ADD SAMPLE / Replace / Remove).
  - **Card:** its picture, its heading (OSC, NOISE, or the sound's name) and Remove.
  - **Knob row:** DETUNE, TUNE, OCT, LEVEL, VELO (NOISE: TONE, MOVEMENT, KEYS, LEVEL, VELO).
  - **Modifier keys:** card C's LINK, LOOP and FOLLOW.
- **The choice of layer shown** is an interface choice saved with the session's view (state
  property `analogHybridLayer`), not a parameter.
- **SOUNDS for C2** lists the imported sounds. Picking one never replaces C1's.

## Tests

| Spec | Test | Checks |
|---|---|---|
| C2-03 | `[hybrid2][v6]`, `[v6][hybrid2]` | C2 off renders exactly as before, whatever C1's type, C2's settings or its loaded source. Card C's overlay key and knob row are unchanged. |
| C2-01 | `[hybrid2][v6]` | C2 as an oscillator plays at its own pitch (OCT, TUNE). LEVEL −48 dB is silent. VELO works. NOISE plays. Its loaded source plays at the zone's root, the same for SAMPLE and INSTRUMENT PATCH. Layered with C1, the powers add. Card C's M / S and lane (a note without card C leaves C2 out) apply. FOLLOW off: C2's zones play out after the key is released. |
| C2-02 | `[hybrid2][v6]` | C2's source replaced mid-chord: no click (steps within the steady signal's), no crash; the next notes play the new one. Removed: nothing stuck. |
| MORPH-01 | `[hybrid2][v6][morph]` | C2 alone (C1 empty) is morphed. C2 off with MORPH on is exactly as before. |
| RT | `[realtime][hybrid2]` | Types, models, MORPH and C2's source swapped mid-note: zero allocations. |
| UI | `[v6][hybrid2]` | + adds and shows C2; the knob row becomes C2's (or NOISE's); 1 returns exactly C1's row; × removes C2 and keeps its settings. |
| State | `[v6][hybrid2]` | A recording becomes a one-zone source rooted at its pitch (A3 found within 30 cents) and stored by content. C2 plays it, and C1's source is untouched. Its definition survives save and reopen. |
| Bindings | `analog: every bound parameter` | Every `analog.hybrid2.*` parameter moves the sound in its context. |

Screenshots at 1448 and 869 px, the full window plus a 2× crop of card C:
`docs/screenshots/v6-gate3` (`[screenshots-v6-g3]`).
