# ANDOR ANALOG — unified expansion plan (oscillator models, Juno/Jupiter, ORGANIC, MORPH)

One coordinated program with one shared setup: **state version 4**. It supersedes the ordering in
`JUNO_ENGINE_SPEC.md` and `V3_ORGANIC_MORPH_SPEC.md`. Their functional requirements stay, except
where this file logs a conflict. "Organic Morph V3" is a project milestone name; it is unrelated to
the saved-state version, which goes 3 → 4 once for everything here.

## Baseline (phase 0, 30 October 2026)

| | |
|---|---|
| ANALOG | `main` f962632 (PR arenttodal/analog#4 merged), branch `claude/upbeat-allen-3i0jey` restarted from it |
| OSP submodule | 64d84fd (arenttodal/OSP#4, merged into its default branch `claude/charming-lovelace-n25fte` as 7a81451) |
| State | `stateVersion = 3`. BLEND was folded into the levels; `analog.blend` is legacy, rests at 50 %, never repurposed. 34 factory patches, factory set `analog-factory-3` |
| Engine | 16 voices, 4x oversampling, latency 22 samples (48 kHz), seeded per-slot tolerances, Poly Mod, sync, OSP GRANULAR/REIMAGINED/DRIVE/MOVEMENT/SPACE/ECHO, sample-accurate events |
| Legacy guard | `tests/LegacyTests.cpp` + `tests/golden/legacy-renders.txt`. Covers 34 factory sounds and 6 edge patches (sync + Poly Mod, unison/glide, GRANULAR, KALEIDOSCOPE, full ANALOG + noise + drive + phase reset). Each is rendered at 44.1/48/96 kHz with a 173-sample block: exact hashes on `linux-x86_64-gcc`, 16-window loudness contours (0.1 %) everywhere |
| CPU | `analog-render --benchmark`: 16 voices 27.2 %, 8 voices 14.1 % of one core (48 kHz, 4x; measured while other builds ran, so an upper bound) |
| Missing inputs | The handoff folder's `JUPITER_REFERENCE_PRIVATE.txt`, `JUNO_REFERENCE_PRIVATE.txt`, `ANDOR_ANALOG_JUNO_ENGINE_BUILD_SPEC.md` and `ANDOR_ANALOG_V3_ORGANIC_MORPH_FULL_SPEC.md` were not in the working environment. The work follows the repository's two specs and the owner's master prompt |

## Dependency graph

```
P0 baseline ──► P1 state v4 (all IDs, one migration) ──┬─► P2 OSCILLATORS heading (shell opt-in)
                                                       ├─► P3 DCO ─► P4 WIDE + ring + SUPER SAW ─┐
                                                       ├─► P5 OTA + HPF ─────────────────────────┤
                                                       ├─► P6 chorus + LFO delay/rise + GATE/LINK ┤
                                                       │                                          ▼
                                                       │                               P7 GUI + factory sounds
                                                       └─► P8 ORGANIC source (shell hybrid layer) ─► P9 MorphProfile
                                                                                                    ─► P10 IMPRINT ─► P11 FUSE ─► P12 WEAVE
                                                                                                                                     ▼
                                                                                                                     P13 validation + delivery
```
OTA/HPF, the chorus and the MorphProfile analyser are pure DSP modules with no shared files. They
are built in parallel (separate worktrees) and integrated into the synth one at a time.

## State version 4: the one parameter set (append-only)

All 51 IDs are in `product/Source/AnalogParameters.h` (JUCE parameter version hint 4), pinned by
`product/golden/parameter-schema.txt`, documented in `docs/PARAMETER_REGISTRY.md` §E. Defaults are
neutral: CLASSIC + CLASSIC, LADDER, HPF off, chorus off, ring 0, ORGANIC off, MORPH off.
- The single `migrateFromVersion3` sets each one to its default when an older session or preset
  loads, so a session never inherits what played before.
- The V1→V2 and V2→V3 migrations still run first.
- A control is enabled on screen only once the engine plays it: `engineParameterIds()` keeps not-yet-played
  IDs out of `boundParameters`, and the bindings test proves those inert.

Decisions taken while freezing the IDs:
- `analog.osc*.width` stays the pulse width for every model; DCO and WIDE add no second width.
- `analog.osc*.wide.shape` sets the wave's edge (0 soft, 50 % calibrated, 100 % raw), as a
  pitch-tracking tilt on that slot's output, so it is audible for every WIDE wave.
- `analog.osc*.wide.spread` is SUPER SAW's detune; disabled for the other waves.
- `analog.filter.ota.slope` choices are ordered `24 dB, 12 dB`, so the default 24 dB is index 0.
  `analog.filter.ota.hpf` is `Off, 0, 1, 2, 3`.
  - The high-pass sits before whichever low-pass is active (its ID keeps "ota" from the spec).
  - Its curve follows OTA CHARACTER: JUPITER uses the Jupiter-8's, JUNO uses the chosen Juno variant's.
- `analog.noise.color` (White/Juno) was added for the Juno noise colouring. It shapes the one existing NOISE source; it is not a second noise.
- `analog.organic.playback` was not added: LOOP covers one-shot vs sustain.
- `analog.organic.root` is an integer −1..127, where −1 = AUTO.
- `analog.morph.formants` is in semitones (−12..12).
- MORPH OUTPUT `Morph Only` replaces the dry ORGANIC and the carrier with the morph. `Layered` adds the morph to the dry sources at −3 dB each.

## Conflict log (resolved)

| Conflict | Resolution |
|---|---|
| Old Organic spec mixes with BLEND | BLEND is gone (state v3). ORGANIC has its own LEVEL; MORPH has its own position; `analog.blend` is untouched |
| Card DETUNE label opened OSCILLATORS (v3) | Owner: the OSCILLATORS heading next to M/S opens it, and DETUNE is a plain control (P2) |
| Juno spec: "three hardware variants" as engines | Variants 6/60/106 are calibrations of DCO/OTA/HPF, not cloned engines |
| Arturia reports name OSC3, wavetables, asinh | Behaviour hints only. No third VCO, no copied tables. The OTA uses its own stable TPT solution |
| Spec: three source cards (osc1, osc2, organic) | The shell's generator card shows the oscillator **pair** (sync, Poly Mod and ring belong to the pair; layer A's LEVEL/TUNE/PAN/M/S address both). ORGANIC is a second card (shell layer B). So: 1 card (pair only or ORGANIC only) or 2 cards. Separate OSC 1/OSC 2 cards would need per-oscillator LEVEL/PAN/M/S in the shared shell; see open decisions |
| Spec: per-voice organic in stereo | Each voice's filter is mono. A stereo recording is summed to mono inside the voice and placed by ORGANIC PAN (and the voice's spread) |

## File map

| Phase | ANALOG files | OSP (opt-in, separate PR) |
|---|---|---|
| P1 | `dsp/Patch.h`, `product/Source/AnalogParameters.*`, `product/Source/Product.cpp`, golden, tests | — |
| P2 | `product/Source/Product.cpp`, product tests | `ShellProduct.h` (`generatorHeaderPopover`), `ShellEditor.*`, `EngineCard.*` (heading hit target) |
| P3–P4 | `dsp/Oscillator.h` (DCO, WIDE, SUPER SAW), `dsp/Synth.*` (slot dispatch, ring) | — |
| P5 | `dsp/OtaFilter.h`, `dsp/HighPassFilter.h`, `dsp/Synth.*` | — |
| P6 | `dsp/JunoChorus.*`, `dsp/Synth.*` (LFO delay/rise, GATE, LINK) | — |
| P7 | `product/Source/AnalogPopovers.*`, `dsp/FactoryPatches.cpp` | — |
| P8 | `dsp/OrganicVoice.*`, `product/Source/AnalogEngine.*` | shell hybrid source (a generator product taking a recording on layer B) |
| P9–P12 | `dsp/morph/*` | — |

## Risks and how each is tested

- **Legacy sound drift.** The legacy golden runs in every build; CLASSIC keeps the original code path.
- **CPU.** Measured per model and per morph method at 8 and 16 voices (`analog-render --benchmark`).
- **Real-time safety.** `[realtime]` allocation tests extended to every new path.
- **Shared UI.** Every OSP change is opt-in. OSP's design capture is byte-identical before and after, and its visual regression stays at 0 px.
