# FX ROUTING - Gate 0 audit and frozen baseline

Recorded 2026-11-08, before any FX-routing change. The code is the authority; where the
handoff (`ANDOR_FX_ROUTING_MASTER_IMPLEMENTATION_SPEC.md`) differs, this table says what is
actually there.

## Baseline

| Item | Value (checked in the checkout) |
| --- | --- |
| ANDOR ANALOG | `51910c6` (branch `claude/upbeat-allen-3i0jey`) |
| ANDOR OSP (submodule `external/osp`) | `b5655b2` (branch `claude/space-presets`) |
| JUCE | 8.0.9 (`OSP_JUCE_TAG`) |
| State version | `stateVersion = 11` (`product/Source/Product.cpp`), chained migrations 1 -> 11 |
| Plugin identity | product `ANDOR Analog`, `PLUGIN_CODE Aanl`, manufacturer `Ospx` (OSP's `AndorInstrument.cmake`), `BUNDLE_ID com.osp.andoranalog`, AU `kAudioUnitType_MusicDevice` (`aumu Aanl Ospx`), VST3 `Instrument Synth`, version 0.1.0 |
| Parameter registry | ANALOG's own: 165 `analog.*` parameters, frozen in `product/golden/parameter-schema.txt` (version hints v1 .. v27); the shared ones (`drive`, `motion`, `space`, `echo`, `drive.mode/tone/body`, `movement.*`, `echo.*`, `space.*`, `analog.movement.b.*`, ...) are the shell's (`InstrumentHost::createParameterLayout`) |
| Macros | MORPH, DRIVE, FILTER, MOVEMENT, ECHO, SPACE (the shell's six) |
| Voices | 16 (`Synth::maxVoices`), polyphony choice 1 / 2 / 4 / 8 / 16 |
| Oversampling / latency | 4x by default (1x / 2x supported); two half-band stages; reported latency `stage2.latency()/2 + stage1.latency()/4` rounded (22 samples at 4x) |

## The post-voice chain as the code runs it (normative for CLASSIC)

`Synth::renderChunk` (`dsp/Synth.cpp`):

1. every active voice renders at the oversampled rate (per voice: sources A/B/C, card EQ /
   DAMPEN, MORPH, the per-voice high-pass, LADDER or OTA filter with its own DRIVE / BIAS /
   TRIM, VCA, pan, per-voice DRIFT);
2. the sum is decimated (two half-band stages) to the host rate;
3. the Juno chorus (`JunoChorus`, off: untouched);
4. KALEIDOSCOPE (`osp::ReimaginedStage`, ANALOG's own instance);
5. `osp::PostProcessor::process` (OSP `src/engine/PostProcessor.cpp`), per sample:
   `reimaginedStage` (0 in ANALOG) -> `DriveProcessor` (untouched while DRIVE is 0) ->
   MOVEMENT (lanes A then B, each crossfaded by its switch; lane B's TAPE / CHORUS level guard)
   -> ECHO as a **send** (`EchoDelay`, level smoothed, sleeps when silent) and SPACE as a
   **send** (two crossfading slots of ROOMS / ABYSS / VINTAGE / BLOOM, asleep below -120 dBFS)
   from the same post-MOVEMENT signal: `out = (1 - 0.2 * min (1, space)) * x + space * spaceWet + echo * echoWet`.
   ECHO's wet never enters SPACE.
6. DC blocker, output gain, `softGuard`.

`Synth::setCardRouting` is MIDI card selection (which cards a note sounds on), not an FX bus.
The three cards are not separate post-filter stems: the per-voice filter and VCA act on their
sum (nonlinear: `Filter (a + b) != Filter (a) + Filter (b)`).

## Frozen baselines

- `tests/golden/fx-baseline-renders.txt` (`tests/FxBaselineTests.cpp`, `[fx][legacy]`): 114
  renders - every factory sound (44) and a post-voice matrix (70): DRIVE's three modes at two amounts,
  MOVEMENT's five modes alone and all 25 lane-A x lane-B pairs, ECHO's three machines x three
  stereo modes plus free time (2 s with tails), SPACE's four rooms, ABYSS, VINTAGE, BLOOM and
  frozen ABYSS (2.5 s), the full chain (also 16 voices, repeated notes, silence), the chorus
  modes and KALEIDOSCOPE upstream, 1 / 8 / 16 voices, 44.1 / 96 kHz, 1x / 2x oversampling.
  FNV-1a hashes of every sample's bits on this platform (`linux-x86_64-gcc`) and a 16-window RMS
  contour (0.1 %) everywhere. The CLASSIC route must reproduce every one.
- `tests/golden/legacy-renders.txt` (state version 3's sounds) and
  `product/golden/parameter-schema.txt` (the registry) stay as they are.
- OSP's own visual and schema goldens (`[design-capture]`, `osp_visual_reference`) stay green.

## CPU (this container: Intel Xeon 2.1 GHz, one core, 48 kHz, 4x, blocks of 256)

| Voices | Chain off | Full chain (DRIVE, MOVEMENT A + B, ECHO, SPACE) |
| --- | --- | --- |
| 1 | 3.4 % | 6.4 % |
| 8 | 17.8 % | 24.0 % |
| 16 | 32.2 % | 40.7 % |

(`fx baseline: CPU ...`, hidden tag `[fx][cpu]`; reported, not asserted. Not a Mac figure.)

## Decisions taken from the audit

- CLASSIC keeps calling `PostProcessor::process` unchanged. CUSTOM uses new, separate stage
  entry points on the same `PostProcessor` objects (DRIVE, MOVEMENT lanes, ECHO wet, SPACE wet
  with its own dry law), so the legacy processors, their smoothing, sleep and crossfades are
  reused rather than re-implemented, and CLASSIC's code is not touched.
- A CUSTOM parallel group made from CLASSIC's ECHO || SPACE uses a *send-sum* law
  (`y = x + sum (lane (x) - x)`), which reproduces CLASSIC's mix; a user-made stack defaults to
  the spec's linear-unity law (`0.5 / 0.5`).
- New effects run at the host rate after KALEIDOSCOPE, in the slot `PostProcessor` occupies.
