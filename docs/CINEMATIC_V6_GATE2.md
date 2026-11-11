# Cinematic V6 — Gate 2: SUB

SUB is an independent polyphonic sub-bass instrument. It plays the keys the musician holds,
taken from the MIDI before the arpeggiator, so it can sustain a chord while the cards
arpeggiate. It joins the output after every effect.

## Engine (`dsp/SubOsc.h`)

- **Voices.** Eight, its own pool, separate from the synth's.
  - Voice order: a key already sounding takes its voice back without a new attack. Then a
    free voice, then the oldest released one, then the oldest held one.
- **Wave.** SINE, TRIANGLE, SQUARE or SAW. SQUARE and SAW are PolyBLEP band-limited.
- **Pitch.** One or two octaves below the key (OCT, default −2), centred (mono). It follows
  the pitch bend at the instrument's bend range.
- **Envelope.** 12 ms anti-click attack, a 220 ms release.
- **GRIT.** Soft saturation measured against LEVEL, so a single key at full velocity is
  driven as hard as GRIT says and a quiet one hardly at all. Its new harmonics are tamed by a
  low-pass that moves from 9 kHz (a touch of GRIT) to 2.2 kHz (all of it).
- **Subsonic guard.** Two one-pole high-passes at 25 Hz, which also remove DC.
- **Level.** LEVEL × the key's response × velocity (0.6 + 0.4 × velocity).
  - FLAT: every key the same.
  - TAPER: quieter above its start (C4) at its slope (6 dB per octave).
  - Range, response and level changes glide over 12 ms.
- **Range.** The played keys SUB answers (default C3 to C6, sounding C1 to C4).
  - Widening the range while keys are held starts them, with an attack.
  - Narrowing it releases the keys that fall outside, with the release fade.
  - Turning SUB on while keys are held starts them; turning it off releases them.
  - Nothing is stuck: a key released while outside the range does not return.
- **MIDI.**
  - Sustain pedal (CC 64): released keys are held until the pedal comes up.
  - Repeated notes are counted, so a key stays held until its last note-off.
  - CC 123 (ALL NOTES OFF) releases everything with the normal fade.
  - CC 120 (ALL SOUND OFF) silences everything within 5 ms.
  - CC 121 resets the bend and lifts the pedal.
- **Real-time safety.** Fixed arrays only: no allocation, locks or I/O. Denormals are flushed.

## The dry guarantee, and what SUB follows

In the synth, SUB is added after `router.process` (CLASSIC's effects, or the CUSTOM route) and
before the output gain, DC blocker and soft guard. It is never any effect's input: no edge
exists by which ECHO, SPACE or any other effect could hear it. When SUB is silent, not a
sample is touched.

**FILTER and MOVEMENT reach it anyway (owner's request, 11 Nov 2026).** Otherwise SUB stays full
and loud while everything else is filtered down or gated. ECHO and SPACE still never reach it.

- **FILTER.** Each SUB voice has a flat 24 dB/octave low-pass with no resonance (a fourth-order
  Butterworth: two TPT state-variable sections). Its cutoff is the cards' own:
  - TONE, as smoothed for the cards;
  - the cards' movement of their cutoff beyond TONE and tracking (modulation routes, LFO,
    drift), averaged over the sounding cards;
  - keyboard tracking around C4 for the played key;
  - the filter envelope (the AMP envelope with ENV LINK), retriggered by each SUB key and
    scaled by velocity as the cards scale it.

  RESONANCE stays the cards' alone, so the sub never booms or whistles.
- **MOVEMENT.** PULSE's tremolo and SHAPER's VOL multiply SUB, sample by sample, in step with
  the cards. OSP's `PostProcessor::beginMovementTrace` records MOVEMENT's level gain (both
  lanes) in the same pass that processes the cards. The gain is applied before the delay that
  aligns SUB with a latent route.
  - DRIFT, TAPE and CHORUS move pitch and time, not level, so they never reach SUB.
  - SHAPER's FILTER target closes no lower than about 380 Hz, so it is not applied.
- **MAIN + SEND.** SUB goes through MAIN's inserts as before, so MOVEMENT there already reaches
  it. FILTER follows in every route.

## MIDI path

`ShellProcessor` already hands the engine the musician's raw input each block
(`setPlayedKeys`) before `process`. `AnalogEngine` keeps that buffer for the block and merges
it with the notes it is given, in time order, on their own samples. Where both streams land on
the same sample, the played event goes first. SUB gets the played events
(`Synth::handleSubMidi`) and the voices get the notes. Without an arpeggiator the two streams
are the same buffer. ANDOR OSP's engine API is unchanged.

## Parameters and state

- **State version 15.** Ten `analog.sub.*` parameters are appended after the FX library's,
  with version hint 30. They are listed in `docs/PARAMETER_REGISTRY.md`.
- **Nothing to migrate.** SUB is off by default, so every older session and preset plays
  exactly as before.

## Interface

- **The housing.** The arpeggiator's housing (105 × 64 at the reference size) now holds two
  rows of the same height:
  - LED + `arp` + chevron;
  - LED + `sub` + chevron.

  The pattern and rate inset is gone from the collapsed housing. Each light switches its own
  instrument. The rest of the `arp` row opens the arpeggiator's editor, as before. The rest of
  the `sub` row opens SUB's popover above the housing, and its chevron turns up while the
  popover is open. No new permanent labels or toolbars.
- **OSP's shared code, opt-in.** `ProductInfo::arpCompanion` (name, enabled parameter,
  popover) and `ArpControl::setSecondRow`. ANDOR OSP's housing is unchanged, and the visual
  regression's parity render keeps it (`ShellProcessor::arpCompanionShown`).
- **SUB's popover.** ANALOG's macro-popover style (`AnalogPanel`), with:
  - WAVE (SINE, TRI, SQUARE, SAW);
  - OCT (−1 / −2), GRIT and LEVEL;
  - RANGE: a C1 to C8 mini keybed with two draggable handles. The range is lit and the caption
    shows its two keys. Arrow keys move the low end, and Shift with an arrow moves the high
    end. The handles never cross.
  - FLAT / TAPER. TAPER shows its FROM and dB / OCT knobs.

## Tests

| Spec | Test | Checks |
|---|---|---|
| SUB-01 | `[sub][v6]` (DSP), `[v6][sub]` (plugin) | The held chord is sustained while the arpeggiator plays other notes, through the real plugin path. Pitch is two octaves down. Pedal, repeated notes, CC 120 and 123 behave. |
| SUB-02 | `[sub][v6]` | Widening, narrowing and restoring the range while keys are held: no clicks (largest sample step < 0.02), nothing stuck. SUB on and off while held. |
| SUB-03 | `[sub][v6]` | TAPER is exactly 6 and 12 dB down at C5 and C6, falls monotonically above C4, and is flat below. Every wave × GRIT × octave, at 0 dB with 13 keys and a full bend: finite and ≤ 1.0. The waves differ, and GRIT adds harmonics. |
| SUB-04 | `[sub][v6]` | Oscillators muted, SUB played with SPACE and ECHO at full, feedback 0.95 and DRIVE at full: identical to fully dry (< 1e-5). The tail after the release is silent. (MOVEMENT is left out: SUB follows it, SUB-09.) |
| SUB-08 | `[sub][v6]` | SUB follows FILTER. TONE at 0.1 halves its level and more. The filter envelope opens it at the attack and closes it as it decays. Resonance changes nothing on SUB (< 2 %). |
| SUB-09 | `[sub][v6]` | SUB follows MOVEMENT. PULSE at full drops its 10 ms level below 30 % of the peak (steady above 80 % without MOVEMENT). SHAPER gates it. CHORUS leaves its level steady. |
| SUB-05 | `[sub][v6]` | CLASSIC with SPACE and ECHO on: with SUB off, the musician's keys reaching SUB change nothing (bit-identical). With SUB on, the output minus the old signal equals SUB alone (< 2e-6). |
| RT-01 | `[realtime][sub][v6]` | 300 blocks of keys, range, wave, GRIT, pedal, bend and CC 120/123 changes: zero allocations, finite output, no denormals. |
| UI | `[v6][sub]` | The lower light switches only SUB, and the upper light still switches the arpeggiator. The row opens the popover above the housing and closes it again. Dragging the RANGE handles moves `lowKey` and never crosses the high key. The reference render has no second row. |
| Goldens | `[v6-baseline]`, `[legacy]`, `[fx]` | Unchanged. `parameter-schema.txt` has the 10 IDs appended. |

Screenshots at 1448 and 869 px, the full window plus a 3× crop of the housing:
`docs/screenshots/v6-gate2` (`[screenshots-v6-g2]`).
