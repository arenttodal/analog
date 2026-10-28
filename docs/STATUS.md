# ANDOR ANALOG — status

_28 October 2026. Everything below was built and tested on Linux x86-64 (GCC 13, Xvfb) only.
No Mac, host, pluginval, auval, signing or listening session has happened yet._

| Area | State | Evidence |
|---|---|---|
| Engine: oscillators (saw, pulse, triangle; B-spline BLEP/BLAMP; free-running phase) | ✅ done and tested | `analog_dsp_tests` [osc]: pitch < 0.1 cent at 44.1/48/96 kHz; aliasing table in AUDIBLE_QA.md |
| Hard sync with sub-sample reset correction | ✅ done and tested | [osc] sync on the master's grid, −76.8 dB at 4x |
| Poly Mod (filter env + OSC 2 → OSC 1 pitch, OSC 1 width, cutoff), audio rate | ✅ done and tested | [modulation]: every route moves the sound; amounts without destinations do nothing |
| Four-pole ZDF ladder with saturation, resonance to self-oscillation, DRIVE / BIAS / TRIM | ✅ done and tested | [filter] |
| Two ADSRs (amplitude: ANDOR OSP's AMP parameters; filter: its own) | ✅ done and tested | [envelope] |
| LFO (5 waves, free or host-synced), MOTION and the mod wheel | ✅ done and tested | [modulation]; sync is not yet tested against a running host transport |
| ANALOG per-voice tolerances and wander from the seed | ✅ done and tested | [analog] ranges at 50 % |
| 16 voices: allocation, stealing with fade, repeated notes, pedal, CC 120/121/123, mono legato, glide, unison | ✅ done and tested | [voices] |
| Sample-accurate MIDI; bit-identical across block sizes; no allocation while rendering | ✅ done and tested | [render], [realtime], `analog: a note starts on its own sample` |
| 4x oversampling with half-band decimation; latency reported | ✅ done and tested | 22 samples; CPU 16 % of a VM core for 16 voices |
| SPACE and ECHO (ANDOR OSP's own effects) | ✅ done and tested | [modulation] space/echo routes; [bindings] every space/echo setting audible |
| Output headroom and guard | ✅ done and tested | every factory chord below the guard's knee |
| Plugin: product identity, registry, parameters (ANDOR schema unchanged + 48 own, golden) | ✅ done and tested | `andor_analog_tests` (shell suite + product tests) |
| Every enabled control audible, every disabled one inert (all ~400 host parameters) | ✅ done and tested | `analog: every control the interface enables ...` |
| Generator card (OSCILLATORS + waveform picture), no drop zone, files refused | ✅ done and tested | shell test `a generator engine has one card ...`; screenshots |
| Six macro popovers from shared parts | ⚠️ built, reviewed only as screenshots | `[screenshots]` test; needs a designer's review against ANDOR's guide |
| 24 factory presets, installed to Documents/…/Factory | ✅ done and tested | `analog: the factory presets are installed, listed and load exactly` |
| Session recall | ✅ done and tested | `analog: a session recalls the sound exactly` |
| Visual regression (parity with ANDOR OSP, 18 states, 0 px outside masks) | ✅ done and tested | `andor_analog_tests_visual`: 0 px in all 18 states, tolerance 0 |
| ANDOR OSP unchanged (design capture, schema golden, suite) | ✅ done and tested | design capture byte-identical except the MOVEMENT popover, which also differs run to run on the unchanged build; ctest 12/12 green (OSP's unit, integration, regression, plugin) |
| Linux VST3 and Standalone build | ✅ built | `ANDOR_Analog_All` |
| macOS build, AU, auval, pluginval, DAWs | ❌ not done | CI workflow `plugin-macos` written, not yet run |
| Listening tests and preset voicing by ear | ❌ not done | docs/AUDIBLE_QA.md checklist |
| Arpeggiator | ❌ not implemented (shown disabled) | |
| Host parameter list trimmed to the used ones | ❌ not implemented (shell-wide limitation) | release blocker |
| Signing, notarization, installer, JUCE licence | ❌ not done | docs/RELEASE.md |

## Open decisions for the owner

1. Final product name and tagline ("ANDOR Analog", "/ANALOG", "A living analog instrument").
2. The generator card as built (one hero card with the oscillators' waveform) — or a designed
   ANDOR extension with oscillator controls on the card.
3. Whether the arpeggiator ships (it needs note generation in the engine).
4. Voicing: filter and drift constants, factory sounds — by ear.
5. JUCE commercial licence, signing credentials, the artwork's use in this product.
