# Cinematic V6 — Gate 0: baseline and reconciliation

Post-1.0 work for the Cinematic Expansion V6 spec (independent REIMAGINED, SUB, C1/C2,
MAIN / SEND). This page records the baseline every later gate is held to, and where the spec's
assumptions differ from the code.

## Baseline

| What | Value |
|---|---|
| Branch | `claude/upbeat-allen-3i0jey`, restarted from `main` at `4bc6c41` (post-1.0). The release candidate branch `claude/release-candidate` (`2fc9856`) is untouched; there is no `v1.0.0` tag yet. |
| `main` vs the release candidate | `main` adds PR #9's interface work (EQ graphic, routing cleanup, header, GRANULAR key) and docs. Its only DSP change is the EQ's spectrum telemetry taps (read-only). The 1.0 session fixtures (`product/tests/fixtures/state-1.0`, recorded on the release candidate) pass on `main`. |
| OSP submodule | `adf8ed6` on `claude/space-presets`. It contains the release candidate's pin `d6cda7be` (`claude/analog-release`) plus interface-only work. |
| State version | 13 (`product/Source/Product.cpp`). |
| Parameters | 544 in `product/golden/parameter-schema.txt` (frozen, append-only); 87 modulation destinations in `product/golden/modulation-destinations.txt` (frozen). |
| Factory sounds | 44 (`dsp/FactoryPatches.cpp`). |

## Frozen goldens

| Golden | Covers | Kind |
|---|---|---|
| `product/golden/v6-baseline-renders.txt` (new) | All 44 factory sounds, a macro-heavy CLASSIC state, and a CUSTOM route through every effect. Rendered through the whole instrument (voice, CLASSIC/CUSTOM FX, output) at 44.1 and 48 kHz: 92 renders. | Exact hash per platform (linux-x86_64-gcc recorded) plus a 16-window RMS contour for other platforms. Test `[v6-baseline]`. |
| `tests/golden/legacy-renders.txt` | The first 34 factory sounds and edge patches through the DSP engine, at 44.1, 48 and 96 kHz. | Hash per platform and contour. `[legacy]`. |
| `tests/golden/fx-baseline-renders.txt` | The post-voice CLASSIC chain. | `[fx][legacy]`. |
| `product/tests/fixtures/state-1.0` | Real 1.0 saved states (default, one factory sound per category, a CUSTOM route): values and contours. | `[state][fixtures]`. |
| `product/golden/parameter-schema.txt`, `modulation-destinations.txt` | IDs, order, ranges; destinations' indices. | `[golden]`. |

The test `[v6-baseline]` was recorded on the unchanged 1.0 code and reproduced bit for bit on a
second run. Every V6 gate must pass it with its own features off.

Still to be done by a person: archive DAW projects (Logic, Ableton, Reaper) saved with the 1.0
installers, to reopen after each gate.

## Where the spec and the code differ

1. **Factory sounds do use REIMAGINED.** The spec says they reportedly don't. Six of the 44 do,
   at 30–70 %:

   | Sound | Mode |
   |---|---|
   | Grain Cloud | KALEIDOSCOPE |
   | Kaleidoscope Pad | KALEIDOSCOPE |
   | Tape Frame Keys | TAPE FRAME |
   | Toybox Chime | TOYBOX |
   | Mirage Bass | MIRAGE |
   | Frozen Glass | KALEIDOSCOPE |

   So the pair-wide path is not a narrow compatibility case for beta sessions. It carries six
   shipped factory sounds, and any user preset made from them. It has to stay as a full engine
   path, selected by an internal flag stored with the state.

2. **REIMAGINED is not an A/B effect today. It reaches card C and the whole mix.**
   - **KALEIDOSCOPE:**
     - per-card granular clouds (A and B);
     - doubling read from both cards' recordings;
     - saturation on the voice's whole source, card C included;
     - extra pitch, tone, level and place drift on the whole voice;
     - a resonator and formant stage on the stereo mix after the voices (`kaleidoscopeStage`).
   - **TAPE FRAME, TOYBOX, MIRAGE:** one engine per voice. It transforms the voice's whole source
     (A + B + C), moves both cards' grain read speed and position, and bends the oscillators'
     pitch.

   "Independent A and B" must therefore define what happens to card C and to the mix stage. In
   the independent model, card C is untouched, each card has its own per-voice engine, and
   KALEIDOSCOPE's mix-level stage becomes per-card or drops out. That model sounds different from
   the pair-wide one, which is why the six sounds above keep the legacy path.

3. **OSP pin.** The spec names `claude/analog-release`. `main` now pins `claude/space-presets`,
   which contains it. No OSP audio code differs between the two.

4. **The routing view has changed since the spec's screenshots.** PR #9 removed its titles and
   footer. The cards' mixer column is still there at 21 %; the spec's per-source send controls
   would go in it.

5. **State version.** The spec's "v1–v13" matches the code (13). V6 will append parameters and
   use 14 and up.
