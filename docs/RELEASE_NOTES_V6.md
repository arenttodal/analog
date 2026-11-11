# ANDOR Analog — Cinematic update (V6), release notes draft

Everything you made with 1.0 opens and sounds exactly as it did, except five factory sounds
that use REIMAGINED (see the first item). Every new feature starts switched off.

## New

- **Each oscillator has its own REIMAGINED.** OSC 1 and OSC 2 each have a REIMAGINED knob as
  their fifth control, with its own mode (KALEIDOSCOPE, TAPE FRAME, TOYBOX, MIRAGE) and
  settings, transforming only that card. OSC 2's VELO is now in its card's well.
  - Older patches give both cards the old amount.
  - Five factory sounds (Grain Cloud, Kaleidoscope Pad, Tape Frame Keys, Toybox Chime, Mirage
    Bass) sound a little different as a result.
- **SUB.** A polyphonic sub bass under the arpeggiator: the housing by the keyboard now has
  `arp` and `sub`.
  - SUB plays the keys you hold, not the arpeggio, so a chord can sustain underneath while
    the cards arpeggiate.
  - Its settings: WAVE, an octave or two down, GRIT, LEVEL, a key RANGE you drag on a small
    keyboard, and FLAT or TAPER (quieter high notes).
  - It sits under the same FILTER as everything else (cutoff and filter envelope, no
    resonance) and follows MOVEMENT's PULSE and SHAPER, so it closes and gates with the cards.
  - No ECHO or SPACE ever reaches it.
- **A second layer in card C.** Hover card C's screen and press **+** to add a layer (an
  oscillator, noise, a recording or an instrument patch) and stack two instruments in one
  card. **1** and **2** choose which one you're editing; **×** removes the second.
  - Both layers share card C's mute and solo, loops, EQ, MORPH and effects.
- **MAIN + SEND routing** (FX ROUTING, gear menu: *Use MAIN + SEND…*). Choose which cards go to
  the reverb and echo, and keep the rest dry.
  - MAIN holds your insert effects.
  - SEND holds ECHO, SPACE and CONVOLUTION, fed by each card's SEND knob (in the routing
    view's mixer) and returning only the wet sound.
  - SUB joins MAIN and never reaches SEND.
  - MAIN, SEND and OVERVIEW tabs show each path. *Back to one path* returns to a single
    chain, and both steps can be undone.

## Unchanged

- The three cards, six macros, racks and their designs.
- Every saved session, preset and automation, apart from the five factory sounds above.
- CLASSIC and custom FX routes, which are never converted automatically.
