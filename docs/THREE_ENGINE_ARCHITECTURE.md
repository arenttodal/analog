# ANDOR Analog — three-engine architecture: source and state mapping

_1 November 2026. Phase 0 of the three-engine specification (A = OSC 1, B = OSC 2,
C = HYBRID). This page is the agreed mapping between what the musician sees and what is
stored, automated and played. Later phases only implement it._

## 1. Audit: what exists (state version 7)

- **One generator layer.** The shell (ANDOR OSP's `andor-shell`) knows a *generator* on its layer 0:
  - the oscillators' card shows the engine's picture;
  - `generatedSource (0)` gives the name and picture;
  - `isLayerOccupied (0)` is always true.
- **One hybrid layer.** It knows a *hybrid* recording on its layer 1 (`ShellProcessor::hybridLayer`):
  - ANDOR OSP's loader, sample store, card and recall;
  - the imported Sample Source plays there too (`Engine::engineSourceName (1)`).
  - Layer 2 is unused by ANDOR Analog.
- **Layer A's card controls drive the oscillator pair:**
  - card A shows DETUNE (`analog.osc2.fine`), TUNE (`layerA.tune`), PAN (`layerA.pan`), LEVEL (`layerA.level`), REIMAGINED;
  - M/S are `layerA.mute` / `layerA.solo`;
  - card A's EQ and DAMPEN (`layerA.eq.*`, `layerA.dampen.*`) process OSC 1 + OSC 2 + noise + ring, summed in each voice before the filter (`Synth::cardA`).
  - `layerA.sourceMode`, `layerA.granular.*`, REIMAGINED and KEY DNA (layer 0) process the same sum.
- **Layer B is ORGANIC:**
  - `layerB.level / tune / start / loop / follow / reverse / mute / solo`, `layerB.eq.*`, `layerB.dampen.*` (`Synth::cardB`);
  - `analog.organic.root`; the `SampleSource` definition (imports) in the session.
- **`ab.blend` (the header MIX)** sets the pair against ORGANIC (equal power; both at full level at the centre); the oscillators' own BLEND sets OSC 1 against OSC 2.
- **Per-oscillator settings already exist:** `analog.osc1.*` / `analog.osc2.*` (model, wave, octave, coarse, width, level, enabled, DCO, WIDE), plus `analog.osc2.fine`. OSC 1 has no fine tune of its own.
- **Pair settings** lived on the workbench's PAIR page: SYNC, RING, POLY MOD, the noise mixer (`analog.noise`, `analog.noise.color`), the voices (polyphony, unison).
- **Mono voices.** Each voice is one mono signal through its filter; PAN places the voice. A per-source pan inside a voice is not supported, and is not offered.

## 2. Logical sources and their storage

| Card (seen) | Logical source | Shell storage layer | Its settings |
|---|---|---|---|
| **A · OSC 1** | oscillator 1 | layer 0 (`layerA.*`) | `analog.osc1.*` |
| **B · OSC 2** | oscillator 2 | layer 2 (`layerC.*`, new to ANDOR Analog) | `analog.osc2.*` |
| **C · HYBRID** | the hybrid slot | layer 1 (`layerB.*`, the shell's hybrid layer: unchanged) | `layerB.*`, `analog.organic.*`, `analog.hybrid.*`, `analog.osc3.*`, the `SampleSource` definition |

- **Letters are presentation only.** The shell gains an opt-in *card order* (storage layers 0, 2, 1 shown as A, B, C), so ORGANIC's IDs, automation, stored recordings, root overrides and imports stay exactly where they are.
- **No ID changes meaning across that line.** `layerB.*` stays the hybrid's, never OSC 2's.

## 3. Card controls

| Card slot | A · OSC 1 | B · OSC 2 | C · HYBRID |
|---|---|---|---|
| 1st knob | DETUNE `analog.osc1.fine` (new) | DETUNE `analog.osc2.fine` | by type: START (sample), DETUNE `analog.osc3.fine` (oscillator), COLOR (noise) |
| 2nd | TUNE `analog.osc1.coarse` | TUNE `analog.osc2.coarse` | TUNE `layerB.tune` (sample, patch); `analog.osc3.coarse` (oscillator) |
| 3rd | OCT `analog.osc1.octave` | OCT `analog.osc2.octave` | PAN `layerB.pan` stays where it was (unbound, as before); OCT `analog.osc3.octave` (oscillator) |
| LEVEL | `analog.osc1.level` | `analog.osc2.level` | `layerB.level` (every type) |
| 5th | REIMAGINED (layer A; it processes the analog pair, OSC 1 + OSC 2) | none (no duplicate) | REIMAGINED where the hybrid supports it |
| M / S | `layerA.mute / solo` → OSC 1 | `layerC.mute / solo` → OSC 2 | `layerB.mute / solo` → the hybrid |
| EQ / DAMPEN | `layerA.eq / dampen` → OSC 1 (+ the noise mixer) | `layerC.eq / dampen` → OSC 2 (+ ring) | `layerB.eq / dampen` → the hybrid |

- **Card overrides.** The shell gains opt-in per-card control overrides: a parameter ID and a caption per knob slot, re-read when the product's revision changes. This generalises today's `generatorStartControl`.
- **Kept as compatibility stages.** `layerA.tune`, `layerA.pan` and `layerA.level` keep their exact legacy meaning, applied to the voice:
  - `layerA.tune` and `layerA.level` to the analog pair;
  - `layerA.pan` to the voice, as before.
  - They are no longer on a card. They stay neutral by default, saved, automatable and modulatable (`levelA`, `panA` and `fineTuneA` keep their meaning).
  - So old automation never silently retargets an oscillator.

## 4. Migration (state version 8)

A version-7 or older session or preset (one combined oscillator card) opens sounding as it did.

- **M/S:** `layerA.mute` muted the pair. If set, `layerC.mute` (OSC 2) is set too. `layerA.solo` → `layerC.solo` likewise.
- **EQ and DAMPEN:** card A's settings processed the pair. They are copied to `layerC.eq.*` / `layerC.dampen.*`.
  - Both oscillators are then processed by the same curve.
  - The curves are linear, so EQ(osc 1) + EQ(osc 2) = EQ(osc 1 + osc 2), within float rounding. Compatibility tests measure it.
  - Nothing is processed twice.
- **`analog.osc1.fine`** (new) starts at 0 (OSC 1 had none). **`analog.hybrid.type`** (new):
  - INSTRUMENT PATCH when the session holds an imported `SampleSource` definition;
  - SAMPLE otherwise, with or without a recording.
- **ORGANIC** is untouched: `layerB.*`, the recording's content-addressed reference, root, loop, EQ, MIX (`ab.blend` keeps its law: the analog side is OSC 1 + OSC 2 at their levels).
- **Interface-only preferences** (the workbench's page, QUICK/DETAILED, PIN) are no longer read. No sound depends on them.

## 5. The hybrid slot

- **`analog.hybrid.type`** = SAMPLE / OSCILLATOR / NOISE / INSTRUMENT PATCH (append only). Only the selected type plays.
- **Dormant sources are kept:**
  - a loaded recording (the shell's layer 1) and an imported definition stay loaded while another type plays;
  - they come back when their type returns;
  - nothing is unloaded silently.
- **OSCILLATOR:**
  - a third model oscillator in each parent voice (`analog.osc3.*`: model, wave, width, DCO, WIDE, octave, coarse, fine);
  - the same `osc::renderModel` code, with no sync or Poly Mod routes;
  - no extra voices.
- **NOISE:** a source of its own (`analog.hybrid.noise.*`), from the voice's deterministic `osp::Prng` noise:
  - COLOR: WHITE / JUNO, the existing algorithms;
  - TONE: a one-pole low-pass;
  - MOVEMENT: a slow seeded amplitude drift.
  - It does not follow the key.
- **INSTRUMENT PATCH:**
  - the existing importer's native sounds (`SampleSource` / `ZonePlayer`); choosing one replaces only the hybrid's definition;
  - importing still writes full presets, but from card C it loads the source only;
  - relink and missing-recording status as before.
- **SAMPLE:** ANDOR OSP's recording, as today: drop / browse, waveform, playheads, LOOP / REVERSE / FOLLOW / GRANULAR, root.
- **MIX** sets the analog pair against whatever the hybrid plays.
- **MORPH** acts only on a SAMPLE with an analysed recording. Other types leave MORPH's settings stored but neutral, and never morph against a dormant recording. Imported patches are not morphable (not validated).

## 6. Signal flow per voice

OSC 1 (level, M/S, card A EQ) + OSC 2 (level, M/S, card B EQ) + noise mixer and ring (legacy) → the analog side. The hybrid (its type, level, M/S, card C EQ) → the hybrid side. Then:

1. MIX (equal power);
2. MORPH where eligible;
3. the shared filter and VCA;
4. the shared effects.

- **16 voices stay the ceiling.** OSC 3 runs inside each voice.
- **The legacy path is untouched while the new controls are neutral:**
  - both EQs off, nothing muted, the hybrid's types unused;
  - so the legacy render hashes hold.

## 7. Where the PAIR page's settings go

The workbench (and its pages) is retired. Its controls move:

- **Each oscillator's timbre:** the inline overlay in that oscillator's well (by model).
- **OSC 2's LOW FREQ and KEY TRACK:** OSC 2's overlay.
- **SYNC, RING, POLY MOD, the noise mixer and VOICES:** the FILTER popover, which already holds the instrument's shared settings. One place, no floating oscillator popover.
