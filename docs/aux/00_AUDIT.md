# D / AUX: gate 0 audit

Branch: `feature/aux-d` in both repositories. It is stacked on `feature/kontakt-external-morph`, which holds the Kontakt host, RHYTHMIC MORPH's loop player, SATELLITE, USER MACROS, the destination-first macros and the card C fixes. Nothing here is merged into `main` or the 1.0.0 release candidate.

| | SHA |
|---|---|
| analog (start of this work) | `614c99ceb2a2db7795d4ce8198f0ce6eda495519` |
| OSP, pinned submodule | `0f5fc87b6dcc4ff36ab32c2efd2a631841b04d3c` |

No dependency was updated.

## Experiments found (and their state)

| Experiment | Branch | State | Reused for D |
|---|---|---|---|
| Kontakt as card C's EXTERNAL source + GLOBAL MORPH | `feature/kontakt-external-morph` (pushed) | Surrogate-validated VST3 host (`product/Source/ExternalInstrument.*`): one instance, message-thread load/unload/editor/state, audio-thread `process` with `ModelExchange`, retire fades, latency read. Real Kontakt and DAWs are **unverified** (BLOCKED in its doc) | Yes: D KONTAKT is a second `ExternalInstrument`. C's EXTERNAL type stays as it is |
| RHYTHMIC MORPH (card C's recording as a tempo-synced loop) | same branch | `dsp/rhythm/RhythmAnalysis`, `RhythmLoop` (slices on the grid, no stretch, zero latency), loader-thread analysis, `ModelExchange` hand-over; TUNE/OCT and FILTER on its loop | Yes: D RHYTHM uses `prepareLoop` + its own `LoopPlayer` with its own recording. C's RHYTHM stays as it is |
| The cards' FILTER on a stereo bus | same branch | `Synth::ExternalFilter` + `configureBusFilter` + `filterBus` (ladder / OTA, resonance, drive, high-pass, TONE and what moves it, key tracking, an envelope) | Yes: D's AUX filter is a third instance |
| andor-modular `SourceNodes` / `ExternalHost` | not in this session's repositories | Not available to inspect | Not reused |

## DSP entry points

`Synth::renderChunk` (dsp/Synth.cpp), per piece of a block between MIDI events:

1. The voices render at the internal rate (×1/×2/×4), each with the per-voice MIX / MORPH / FILTER / AMP. Card sends are tapped pre-voice-filter for MAIN + SEND.
2. They are decimated to the host rate.
3. `mixRhythm`: C's loop (and its GLOBAL MORPH).
4. `mixExternal`: C EXTERNAL's bus (and its GLOBAL MORPH; voices delayed by its latency).
5. Juno chorus, KALEIDOSCOPE / REIMAGINED post.
6. `fx::FxEngine`: CLASSIC or CUSTOM route, MAIN + SEND.
7. SUB: mixed after the effects in most routes, deliberately dry. **D does not copy this.**

**D joins after step 4, before step 5.** It is mixed into the same host-rate stereo bus as the voices, so the chorus, KALEIDOSCOPE, DRIVE, MOVEMENT, ECHO, SPACE and any CUSTOM route process it as they process A/B/C.

## FILTER

- The big FILTER macro is `analog.tone` (`params::tone`): `Synth::toneNote`, smoothed, read per voice.
- The FILTER rack sets resonance, drive, LADDER / OTA (and its voicing), the high-pass, key tracking and the envelope.
- `configureBusFilter` already maps these onto a stereo bus filter at the host rate, exactly as for C's loop and Kontakt.
- What a bus cannot take literally:
  - per-voice key tracking (it uses the newest note, or none);
  - each voice's own filter envelope (D NOISE's gate envelope stands in; RHYTHM and KONTAKT use none by default);
  - per-voice poly-mod, drift and modulated cutoff (the bus takes the average of the cards' moved cutoff, as SUB does);
  - oversampling (the bus runs at the host rate).

## Mute / solo

- Mute and solo are computed once, in the parameter reader (`AnalogParameters.cpp`):
  ```
  anySolo = soloA || soloB || soloC
  osc1Heard = anySolo ? soloA : !muteA
  ...
  ```
- D's solo joins `anySolo` only while D is powered, so a powered-off D can never silence A/B/C.

## Layout (shared shell)

- `frame::place` (`andor-ui/InstrumentFrame.cpp`) sets everything under the cards by subtracting one value, `sourceTrim`: the macros at 719/818, the modulation panel at 688, the rack at `rackTop` (908), the arp panel and the keyboard row.
- `frame::paint` draws the macro panel and keybed with the same value.
- The cards' own area is `frame::sourceArea (trim)`: `layout::sources` = {35, 127, 1379, 525}, so the cards end at 652. The macro panel starts at 682.
- `frame::instrumentHeight (arp, rackShift, trim)`, 1086 at the reference.
- **D's strip uses this:**
  - an opt-in `auxShift` lowers everything under the cards (trim − auxShift), while the cards keep `sourceArea (trim)`;
  - the editor grows by the same amount, the strip sits between them, and hiding it returns every place exactly.

## View state

- `ShellProcessor` saves view state in its `<View>` element (arpOpen, shaperLarge, rack) with the session.
- `loadPreset` goes through `applyStateXml`, which also restores `<View>`.
- AUX visibility is stored there (`auxShown`), and a preset load keeps the current value: presets never show or hide AUX.

## MIDI

- `AnalogEngine::process` has the host's MIDI (`midi`) and, from the shell, the played keys before the per-card arpeggiator (`playedMidi`, as SUB reads them).
- D NOISE's gate and D KONTAKT use the played keys, normalised to channel 1. The arpeggiators' card-coded channels never reach Kontakt.

## Parameters

- The last version hint is 36 (RHYTHMIC MORPH); D's parameters use **37**, appended. The state version goes 22 → 23 (nothing to migrate: absent means D off).
- The reader table holds up to 512 values (raised from 448 for card C's GRANULAR).

## Compatibility hazards

| Hazard | Guard |
|---|---|
| Legacy renders | With D off, `mixAux` returns before touching a sample (as `mixExternal` does). The legacy golden hash and the product's 1.0 fixtures must stay bit-exact |
| Parameter golden | It gains exactly D's lines |
| Automation list | The automatable list gains D's parameters (all read by the engine) |
| The bindings test | Every enabled control must move the sound: D's parameters need D on, a mode and, for RHYTHM, a loop |
| Solo | Only a powered D's solo counts |
| Latency | D KONTAKT's latency delays everything before it in series (the voices and C EXTERNAL's bus), so the reported latency is C's plus D's |
| MORPH | Unchanged: per-voice A/B ↔ C MORPH, and C's GLOBAL MORPHs (EXTERNAL, RHYTHM) never take D. See `MORPH_DECISION.md` |
