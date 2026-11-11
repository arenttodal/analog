# SHAPER DNA V1: gate 0, baseline

The spec is ANDOR SHAPER DNA V1 (the implementation assignment of 11 November 2026). The concept image is visual inspiration only; the real ANDOR interface takes precedence.

## Branches

| Repository | Branch | Base |
|---|---|---|
| `arenttodal/analog` | `feature/shaper-dna-v1` | `feature/sub-popup-v7` `53aa55d` (V7 + MORPH fixes + sends through FILTER / MOVEMENT) |
| `arenttodal/OSP` | `feature/shaper-dna-v1` | `claude/space-presets` `a91d7f2` (the pin of `53aa55d`) |

Not touched: `main`, `claude/release-candidate` (1.0.0) and the V6 / V7 branches. Nothing is merged.

## Baseline (Linux, GCC 13, this container, at `53aa55d`)

`ctest -R "analog_dsp_tests|andor_analog_tests|andor_example_tests|osp_visual_reference|andor_bootstrap_tool"` passed 7 / 7:

- `analog_dsp_tests`, with every factory and legacy golden render;
- `andor_analog_tests`, with the APVTS schema golden (579 entries, state version 17);
- `andor_example_tests`, and both visual regressions at 0 px;
- `osp_visual_reference` and `andor_bootstrap_tool`.

The frozen audio baselines are the existing golden renders (`[v6-baseline]`, `[legacy]`, factory renders). SHAPER DNA OFF must leave all of them unchanged.

## Code map (what DNA touches)

| Where | What |
|---|---|
| OSP `src/engine/RhythmicShaper.*` | The pattern library and the pure `evaluate` / `span` / `stepsOf` / `patternSteps` / `stepQuarterNotes`, reused, not copied. |
| OSP `src/engine/MovementBus.*` | Lane A's SHAPER on the summed bus: it gets the opt-in handoff (dry while DNA plays). |
| OSP `src/engine/PostProcessor.*` | `process()` (CLASSIC) and `customMovement()` (CUSTOM) both run lane A's `MovementBus`, so one handoff covers both. |
| `dsp/Synth.*` | Note-ons (assignment, latched per voice) and `renderVoice` (per voice, before pan and sum, at the internal rate). |
| `dsp/fx/FxEngine.*` | Whether MOVEMENT is audible on MAIN (CLASSIC: always; CUSTOM: present and not bypassed). |
| `product/Source/AnalogParameters.cpp`, `Product.cpp`, `dsp/Patch.h` | Append-only parameters. |

## Recorded design decisions

- **Sends.** The spec says MAIN + SEND taps keep their pre-filter behaviour. On 11 November the owner asked that everything go through FILTER and MOVEMENT, so the sends are now filtered and follow MOVEMENT (`53aa55d`). With DNA, each voice's send takes that voice's own DNA shaping, so the room follows each note's rhythm. With the global SHAPER handed off, its trace no longer gates the sends: no double shaping.
- **SUB.** SUB stays out of DNA (V1). While DNA plays, the handed-off global SHAPER's trace is 1, so SUB is no longer gated by it. SUB keeps PULSE and lane B.
