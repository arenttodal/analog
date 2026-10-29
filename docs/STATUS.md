# ANDOR ANALOG — status

_29 October 2026, version 2 with the arpeggiator, the whole modulation panel and the grains on
the card. Built and tested on Linux x86-64 (GCC 13, Xvfb); the macOS build, its tests and auval
pass in CI (`plugin-macos`), the Windows build and its tests too (`plugin-windows`, 64-bit MSVC).
No DAW session, pluginval, signing or formal listening test yet; the owner has played version 1
and version 2 (the macOS test builds). Queued, not started: V3_ORGANIC_MORPH_SPEC.md, then
JUNO_ENGINE_SPEC.md (they share the oscillator-model parameters)._

| Area | State | Evidence |
|---|---|---|
| Engine: oscillators (saw, pulse, triangle; B-spline BLEP/BLAMP; free-running phase) | ✅ done and tested | `analog_dsp_tests` [osc]: pitch < 0.1 cent at 44.1/48/96 kHz; aliasing table in AUDIBLE_QA.md |
| Hard sync with sub-sample reset correction | ✅ done and tested | [osc] sync on the master's grid, −76.8 dB at 4x |
| Poly Mod (filter env + OSC 2 → OSC 1 pitch, OSC 1 width, cutoff), audio rate | ✅ done and tested | [modulation]: every route moves the sound; amounts without destinations do nothing |
| Four-pole ZDF ladder with saturation, resonance to self-oscillation, FILTER DRIVE / BIAS / TRIM | ✅ done and tested | [filter] |
| Two ADSRs (amplitude: ANDOR OSP's AMP parameters; filter: its own) | ✅ done and tested | [envelope] |
| LFO: ANDOR OSP's LFO 1 (7 shapes incl. RANDOM and a drawn CUSTOM, rate or sync, phase, polarity, free / retrigger / one shot, global or POLY), its amount and the mod wheel | ✅ done and tested | [modulation], [live][lfo]; the modulation panel's LFO 1 page; sync not yet tested against a running host transport |
| Modulation: ANDOR OSP's panel (LFO 1 and 2, ENV 1 and 2, VEL, NOTE, wheel, CC 1-4 with Learn, pressure; 16 routes, depth routes, curves, APPLY TO AMP) to every continuous control: 72 of ANDOR ANALOG's own (per note or the instrument's) and the 16 of ANDOR OSP's it plays; drag a source onto any knob | ✅ done and tested | `analog: every modulation destination it offers moves the sound`; [modulation] in analog_dsp_tests (per note VEL, controllers, reset, APPLY TO AMP); no allocation ([realtime][modulation]); CPU unchanged within noise (24.7-25.8 % of a VM core, 16 voices) |
| AMP envelope picture follows its knobs and automation (it redrew only when its dots were dragged) | ✅ fixed | the shell's editor repaints it when attack, decay, sustain or release move, as ANDOR OSP's does |
| Arpeggiator: ANDOR OSP's (18 patterns, 10 rates, gate, octaves, swing, velocity, host sync, hold with the pedal, its display), in front of the synth; the ARP destinations | ✅ done and tested | `shell: an engine that binds arp.* is played by ANDOR OSP's arpeggiator`; the bindings test (every ARP control heard) |
| GRANULAR on the card: POS, the SPREAD pane and every playing grain (where it reads, its window, its row), as ANDOR OSP shows them over a recording | ✅ done and tested / ⚠️ seen only in screenshots | `analog: GRANULAR's grains are shown on the card ...`; `shell: a generator engine's grains reach the card` |
| GRANULAR on the oscillators: each voice records its own sound (1.5 s) and plays ANDOR OSP's grains from it (POS how far back, SIZE, DENS, TUNE, SPREAD), with KEY DNA | ✅ done and tested | [live][granular]: speaks at once, sustains, every setting heard, TUNE +12 is an octave, extremes finite, KEY DNA neutral when off |
| REIMAGINED on the live sound: KALEIDOSCOPE (saturation, doubling, remapped grains; ANDOR OSP's resonator and formant stage on the mix, tuned to each phrase's first note), TAPE FRAME, TOYBOX, MIRAGE (ANDOR OSP's own engines, unchanged); MOSAIC needs a recording's analysis and leaves the sound as it is (its popover says so) | ✅ done and tested | [live][reimagined] in ONE SHOT and GRANULAR; each mode's settings heard; 0 % is the plain sound |
| MOVEMENT: DRIFT in each voice (ANDOR OSP's wander of pitch, tone, level, place) and TAPE, CHORUS, PULSE, SHAPER (with its custom pattern) on the mix | ✅ done and tested | [live][movement]; `analog: SHAPER's custom pattern is played, saved …` |
| DRIVE: ANDOR OSP's (TUBE, TAPE, CRUNCH; TONE, BODY) | ✅ done and tested | [live][drive] |
| ANALOG per-voice tolerances and wander from the seed | ✅ done and tested | [analog] ranges at 50 % |
| 16 voices: allocation, stealing with fade, repeated notes, pedal, CC 120/121/123, mono legato, glide, unison | ✅ done and tested | [voices]; a taken voice keeps its timing with grains and modes on |
| Sample-accurate MIDI; bit-identical across block sizes; no allocation while rendering | ✅ done and tested | [render], [realtime], [live][determinism], `analog: a note starts on its own sample` |
| 4x oversampling with half-band decimation; latency reported | ✅ done and tested | 22 samples |
| CPU (16 voices held, one Linux VM core, 4x, 48 kHz) | ✅ measured | plain sounds ~21 %; REIMAGINED modes 28–42 %; GRANULAR clouds up to ~50 % (Grain Cloud, Frozen Glass) |
| SPACE and ECHO (ANDOR OSP's own effects) | ✅ done and tested | [modulation] space/echo routes; [bindings] every space/echo setting audible |
| Output headroom and guard | ✅ done and tested | every factory chord below the guard's knee |
| Plugin: product identity, registry, parameters (ANDOR schema unchanged + 57 own, golden) | ✅ done and tested | `andor_analog_tests` (shell suite + product tests) |
| Sessions and presets of version 1 migrated (state version 2) | ✅ done and tested | `analog: a version-1 session or preset is migrated and sounds as it did` |
| Every enabled control audible, every disabled one inert (all ~400 host parameters) | ✅ done and tested | `analog: every control the interface enables ...` |
| Generator card (OSCILLATORS + waveform picture, ONE SHOT / GRANULAR and the grain controls), no drop zone, files refused | ✅ done and tested | shell test `a generator engine has one card ...`; screenshots |
| Macro popovers: DRIVE, MOVEMENT, SPACE, ECHO and REIMAGINED are ANDOR OSP's own (shared from its repository; OSP stays pixel-identical); BLEND and TONE are the instrument's | ✅ done and tested / ⚠️ BLEND and TONE reviewed only as screenshots | `analog: DRIVE, MOVEMENT, SPACE and ECHO open ANDOR OSP's popovers …`; `[screenshots]` |
| 34 factory presets (10 new for the new engines), installed to Documents/…/Factory | ✅ done and tested | `analog: the factory presets are installed, listed and load exactly`; headroom test |
| Session recall | ✅ done and tested | `analog: a session recalls the sound exactly` |
| Visual regression (parity with ANDOR OSP, 18 states, 0 px outside masks) | ✅ done and tested | `andor_analog_tests_visual` |
| ANDOR OSP unchanged (design capture, schema golden, suite) | ✅ done and tested | design capture byte-identical except the MOVEMENT popover's moving strip, which also differs run to run on the unchanged build; OSP's ctest green |
| Linux VST3 and Standalone build | ✅ built | `ANDOR_Analog_All` |
| macOS build, AU, auval | ✅ done (CI) | `plugin-macos`: build, ctest, `auval -v aumu Aanl Ospx`, the test-build zip |
| Windows build (VST3, standalone; 64-bit) | ✅ done (CI) | `plugin-windows`: Visual Studio 2022, the engine's and the product's tests, the test-build zip with Install.bat |
| pluginval, DAWs | ❌ not done | docs/RELEASE.md |
| Listening tests and preset voicing by ear | ❌ not done | docs/AUDIBLE_QA.md checklist |
| Host parameter list trimmed to the used ones | ❌ not implemented (shell-wide limitation) | release blocker |
| Signing, notarization, installer, JUCE licence | ❌ not done | docs/RELEASE.md |

## Version 2 notes

- The first version's sessions and presets open as they sounded, with one exception: an LFO set
  to S&H becomes ANDOR OSP's RANDOM (a smooth random; the stepped one is not among LFO 1's shapes).
  The factory "Random Steps" uses RANDOM now.
- MOSAIC rebuilds a recording from the harmonic frames ANDOR OSP analyses when it is loaded; a
  live sound has none, so in ANDOR ANALOG the mode plays the sound unchanged and its popover says
  so ("NEEDS A RECORDING: PLAYS UNCHANGED"). Its settings are shown disabled.
- TAPE FRAME's FRAME lengths (3.6–11.4 s of tape) are longer than a voice's recording (1.5 s):
  the tape runs for the whole FRAME, its travel scaled into the recording.
- KEY DNA has no curves here yet (ANDOR OSP has no editor for them either): AMOUNT,
  INDIVIDUALITY and each parameter's variation (BLEND popover).

## Open decisions for the owner

1. Final product name and tagline ("ANDOR Analog", "/ANALOG", "A living analog instrument").
2. The generator card as built (one hero card with the oscillators' waveform) — or a designed
   ANDOR extension with oscillator controls on the card.
3. The arpeggiator ships as ANDOR OSP's (played by the shell): by ear.
4. Voicing: filter and drift constants, factory sounds (the ten new ones included) — by ear.
5. JUCE commercial licence, signing credentials, the artwork's use in this product.
