# ANDOR ANALOG — parameter registry

Identifiers are forever (HANDOFF rule 9): never rename, re-range or reorder a parameter once
released; add new ones at the end with a new version hint. The host sees, in this order:

1. **ANDOR OSP's 350-parameter schema** (unchanged, pinned by `external/osp/tests/plugin/golden/osp-parameter-schema.txt`).
   The engine reads the ones listed in section B (same meaning as in ANDOR OSP); every other one
   is shown disabled and changes nothing (tested for each).
2. **ANDOR ANALOG's own** (section A), version hints 1 and 2, pinned by `product/golden/parameter-schema.txt`
   (`andor_analog_tests`, "the parameter schema …"; `OSP_UPDATE_GOLDEN=1` rewrites it only for an
   intentional, documented change).

**State version 2** (`ProductInfo::stateVersion`): ANDOR OSP's DRIVE, MOVEMENT, REIMAGINED, GRANULAR
and LFO 1 joined. A session or preset saved by version 1 is migrated when it is opened
(`migrateFromVersion1`, product/Source/Product.cpp; tested): `drive` (the filter's input drive then)
moves to `analog.filterDrive` and `drive` becomes 0; `analog.lfo.wave/rate/sync/division` move to
`mod.lfo1.shape/rate/sync/division` (S&H becomes RANDOM, ANDOR OSP's smooth random; 1/2T becomes
1/4D); `motion`, `reimagined` and `layerA.sourceMode` (stored at ANDOR OSP's defaults, unheard)
become 0 / ONE SHOT. Only display names changed among the version-1 IDs.

## A. ANDOR ANALOG's own (`analog.*`)

Defaults are the INIT patch (`dsp/Patch.h`); values are in the units shown.

| ID | Name | Kind | Range | Default |
|---|---|---|---|---|
| `analog.blend` | Blend | float | 0 % … 100 % | 50 % |
| `analog.tone` | Tone | float | 20 Hz … 18.0 kHz | 62 % |
| `analog.motion` | LFO Amount (v1: "Motion", the macro) | float | 0 % … 100 % | 0 % |
| `analog.osc1.wave` | Osc 1 Wave | choice | Saw, Pulse, Saw+Pulse | 0 |
| `analog.osc1.octave` | Osc 1 Octave | int | -2 oct … +2 oct | 0 oct |
| `analog.osc1.coarse` | Osc 1 Coarse | int | -12 st … +12 st | 0 st |
| `analog.osc1.width` | Osc 1 Pulse Width | float | 2 % … 98 % | 50 % |
| `analog.osc1.level` | Osc 1 Level | float | 0 % … 100 % | 100 % |
| `analog.osc2.wave` | Osc 2 Wave | choice | Saw, Triangle, Pulse, Saw+Tri, Saw+Pulse, Tri+Pulse, All | 0 |
| `analog.osc2.octave` | Osc 2 Octave | int | -2 oct … +2 oct | 0 oct |
| `analog.osc2.coarse` | Osc 2 Coarse | int | -12 st … +12 st | 0 st |
| `analog.osc2.fine` | Osc 2 Fine | float | -100 ct … +100 ct | 7 ct |
| `analog.osc2.width` | Osc 2 Pulse Width | float | 2 % … 98 % | 50 % |
| `analog.osc2.level` | Osc 2 Level | float | 0 % … 100 % | 80 % |
| `analog.osc2.keyTrack` | Osc 2 Keyboard | bool | off / on | 1 |
| `analog.osc2.lowFreq` | Osc 2 Low Freq | bool | off / on | 0 |
| `analog.noise` | Noise | float | 0 % … 100 % | 0 % |
| `analog.sync` | Sync | bool | off / on | 0 |
| `analog.polyphony` | Voices | choice | 1, 2, 4, 8, 16 | 3 |
| `analog.unison` | Unison | bool | off / on | 0 |
| `analog.unison.detune` | Unison Detune | float | 0 % … 100 % | 30 % |
| `analog.spread` | Stereo Spread | float | 0 % … 100 % | 40 % |
| `analog.resonance` | Resonance | float | 0 % … 100 % | 15 % |
| `analog.keyTrack` | Filter Keyboard | float | 0 % … 100 % | 50 % |
| `analog.envDepth` | Filter Env Amount | float | -60.0 st … +60.0 st | 24 st |
| `analog.velocityToFilter` | Velocity to Filter | float | 0 % … 100 % | 0 % |
| `analog.drive.bias` | Drive Bias | float | 0 % … 100 % | 0 % |
| `analog.drive.trim` | Drive Trim | float | -12.0 dB … +6.0 dB | 0 dB |
| `analog.filterEnv.attack` | Filter Attack | float | 1 ms … 30.0 s | 0.008 s |
| `analog.filterEnv.decay` | Filter Decay | float | 5 ms … 40.0 s | 0.5 s |
| `analog.filterEnv.sustain` | Filter Sustain | float | 0 % … 100 % | 20 % |
| `analog.filterEnv.release` | Filter Release | float | 5 ms … 45.0 s | 0.35 s |
| `analog.lfo.wave` | LFO Wave (v1, unused) | choice | Sine, Triangle, Ramp, Square, S&H | 1 |
| `analog.lfo.rate` | LFO Rate (v1, unused) | float | 0.02 Hz … 25.0 Hz | 4 Hz |
| `analog.lfo.sync` | LFO Sync (v1, unused) | bool | off / on | 0 |
| `analog.lfo.division` | LFO Division (v1, unused) | choice | 8 bars, 4 bars, 2 bars, 1 bar, 1/2, 1/2T, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 6 |
| `analog.lfo.pitch1` | LFO to Osc 1 Pitch | float | -12.00 st … +12.00 st | 0.4 st |
| `analog.lfo.pitch2` | LFO to Osc 2 Pitch | float | -12.00 st … +12.00 st | 0.4 st |
| `analog.lfo.width1` | LFO to Osc 1 Width | float | -45 % … +45 % | 15 % |
| `analog.lfo.width2` | LFO to Osc 2 Width | float | -45 % … +45 % | 15 % |
| `analog.lfo.cutoff` | LFO to Cutoff | float | -48.0 st … +48.0 st | 0 st |
| `analog.polyMod.env` | Poly Mod Filter Env | float | 0 % … 100 % | 0 % |
| `analog.polyMod.osc2` | Poly Mod Osc 2 | float | 0 % … 100 % | 0 % |
| `analog.polyMod.toPitch1` | Poly Mod to Osc 1 Pitch | bool | off / on | 0 |
| `analog.polyMod.toWidth1` | Poly Mod to Osc 1 Width | bool | off / on | 0 |
| `analog.polyMod.toCutoff` | Poly Mod to Cutoff | bool | off / on | 0 |
| `analog.analog` | Analog | float | 0 % … 100 % | 35 % |
| `analog.phase` | Osc Phase | choice | Free, Reset | 0 |
| `analog.filterDrive` (v2) | Filter Drive | float | 0 % … 100 % | 0 % |
| `analog.keyDna` (v2) | Key DNA | bool | off / on | 0 |
| `analog.keyDna.amount` (v2) | Key DNA Amount | float | 0 % … 100 % | 100 % |
| `analog.keyDna.individuality` (v2) | Key DNA Individuality (Smooth … Individual) | float | 0 % … 100 % | 30 % |
| `analog.keyDna.position`, `.size`, `.density`, `.spread` (v2) | Key DNA Position, Size, Density, Spread | float | 0 % … 100 % | 35 % |
| `analog.keyDna.tune` (v2) | Key DNA Tune | float | 0 % … 100 % | 0 % |

The four `analog.lfo.*` of version 1 are read only to migrate an older state; the KEY DNA
parameters are ANDOR OSP's `keydna::Settings` (amount, individuality, each parameter's
variation; flat curves: the curve editor for them is still to come in ANDOR OSP too).

## B. ANDOR OSP's parameters that ANDOR ANALOG reads (same meaning)

| ID | What it does here | Where it shows |
|---|---|---|
| `attack`, `decay`, `sustainLevel`, `release` | the amplitude envelope (VCA ADSR; ms, ms, %, ms) | the AMP panel |
| `velocityRange` | how much softer the softest note is (0–48 dB, quadratic in velocity) | Advanced: DYNAMICS RANGE |
| `drive`, `drive.mode`, `drive.tone`, `drive.body` | DRIVE: ANDOR OSP's saturation of the whole sound (TUBE, TAPE, CRUNCH) | macro 2 and ANDOR OSP's popover |
| `motion`, `movement.mode`, `movement.drift.*`, `movement.tape.*`, `movement.chorus.*`, `movement.pulse.*`, `movement.shaper.*` (with the custom pattern) | MOVEMENT: DRIFT in each voice (pitch, tone, level, place), TAPE, CHORUS, PULSE, SHAPER on the mix | macro 4 and ANDOR OSP's popover |
| `reimagined`, `layerA.reimagined.mode`, `.kaleidoscope.*`, `.tapeFrame.*`, `.toybox.*`, `.mirage.*` | REIMAGINED on the live sound (MOSAIC's settings are not read: it needs a recording) | the card's REIMAGINED and ANDOR OSP's popover |
| `layerA.sourceMode`, `layerA.granular.position`, `.size`, `.density`, `.tune`, `.spread` | ONE SHOT (the oscillators) or GRANULAR (grains from each voice's own last second; POS: how far back) | the card |
| `mod.lfo1.shape`, `.rate`, `.sync`, `.division`, `.phase`, `.polarity`, `.mode`, `.scope` (with its CUSTOM curve) | the synth's LFO (its depths are `analog.lfo.*`, its amount `analog.motion`) | the modulation panel's LFO 1 |
| `space`, `space.type`, `space.decay`, `space.preDelay`, `space.size`, `space.damping`, `space.modulation`, `space.width`, `space.lowCut`, `space.highCut` | SPACE (ANDOR OSP's reverb, its own mapping) | macro 5 and its popover |
| `echo`, `echo.type`, `echo.sync`, `echo.division`, `echo.time`, `echo.feedback`, `echo.tone`, `echo.age`, `echo.stereo` | ECHO (ANDOR OSP's delay, host tempo) | macro 6 and its popover |
| `voiceMode` | Poly / Mono (legato, last note) | Advanced: VOICES |
| `glide` | portamento time (to 95 %), poly and mono | Advanced: GLIDE |
| `bendRange` | pitch-bend range (semitones) | Advanced: BEND |
| `fineTune` | master fine tune (cents) | Advanced: FINE |
| `seed` | the variation seed: ANALOG's tolerances, noise, grains, DRIFT, REIMAGINED, KEY DNA, RANDOM | Advanced: RESEED |
| `layerA.tune`, `layerA.pan`, `layerA.level`, `layerA.mute` | the generator's TUNE (st), PAN, LEVEL (−48 dB = silent), MUTE | the OSCILLATORS card |
| `gain` | the master volume (dB) | the header's volume |

The wheels (`midi.pitchWheel`, `midi.modWheel` in the engine's binding list) are not parameters:
they enable the on-screen wheels. The pitch wheel bends both oscillators; the mod wheel brings in
the LFO depths (from the LFO amount, `analog.motion`, to full).

## C. Mapping from the implementation specification's proposed IDs

| Proposed | As built |
|---|---|
| `analog.macro.blend`, `.tone`, `.movement` | `analog.blend`, `analog.tone` (it *is* the cutoff); since version 2 the MOVEMENT macro is ANDOR OSP's `motion` (v1: `analog.motion`, now the LFO amount) |
| `analog.macro.drive`, `.space`, `.echo` | ANDOR OSP's `drive`, `space`, `echo` (since version 2 DRIVE is ANDOR OSP's own saturation; the filter's is `analog.filterDrive`) |
| `analog.osc1.saw`, `.pulse` | `analog.osc1.wave` (Saw / Pulse / Saw+Pulse) |
| `analog.osc2.saw`, `.triangle`, `.pulse` | `analog.osc2.wave` (any combination) |
| `analog.osc*.pw` | `analog.osc*.width` |
| `analog.noise.level`, `analog.osc.sync` | `analog.noise`, `analog.sync` |
| `analog.unison.enabled`, `.detune`, `.spread` | `analog.unison`, `analog.unison.detune`, `analog.spread` (also places poly voices) |
| `analog.vcf.cutoff` | `analog.tone` |
| `analog.vcf.resonance`, `.keytrack`, `.envDepth`, `.velocityDepth` | `analog.resonance`, `analog.keyTrack`, `analog.envDepth`, `analog.velocityToFilter` |
| `analog.vcf.drive` | `analog.filterDrive` (v1: `drive`) with `analog.drive.bias`, `analog.drive.trim` |
| `analog.vcfEnv.*` | `analog.filterEnv.*` |
| `analog.ampEnv.*` | ANDOR OSP's `attack`, `decay`, `sustainLevel`, `release` |
| `analog.lfo.wave`, `.rate`, `.sync`, `.division`, `.pitch1`, `.pitch2`, `.pw1`, `.pw2`, `.cutoff` | ANDOR OSP's `mod.lfo1.*` (v1: `analog.lfo.wave/rate/sync/division`); the depths `analog.lfo.pitch1/2`, `.width1/2`, `.cutoff` |
| `analog.polyMod.envAmount`, `.osc2Amount`, `.toPitch1`, `.toPw1`, `.toCutoff` | `analog.polyMod.env`, `.osc2`, `.toPitch1`, `.toWidth1`, `.toCutoff` |
| `analog.analog.amount`, `.seed`, `.phaseMode` | `analog.analog`, ANDOR OSP's `seed`, `analog.phase` |
| `analog.polyphony`, `.glide`, `.velocityAmp`, `.mono`, `.masterGain` | `analog.polyphony`, `glide`, `velocityRange`, `voiceMode`, `gain` |
| `analog.fx.space.*`, `analog.fx.echo.*` | ANDOR OSP's `space.*`, `echo.*` (a send at 0 is off and sleeps) |

