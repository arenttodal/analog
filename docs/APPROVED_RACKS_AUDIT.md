# Approved effects racks — G0 audit (2026-11-08)

Baseline for `ANDOR_APPROVED_EFFECTS_CLAUDE_IMPLEMENTATION_SPEC.md` and
`ANDOR_FILTER_MORPH_RACK_CLAUDE_IMPLEMENTATION_SPEC.md`. No code changed at this gate.

## Baseline

| Item | Value |
|---|---|
| ANALOG branch / commit | `claude/upbeat-allen-3i0jey` @ `948ac44` |
| OSP pin (`external/osp`) | `claude/space-presets` @ `45c7a11` |
| `stateVersion` (Product.cpp) | **11** |
| Tests at baseline | ANALOG `analog_dsp_tests`, `andor_analog_tests`, `andor_analog_tests_visual`, `andor_example_tests(_visual)`: pass. OSP `plugin`, `osp_visual_reference`, `andor_example_tests(_visual)`: pass. CI run #95 (macOS, Windows, Linux): pass |
| Baseline screenshots | `[screenshots-adaptive]`, `[screenshots-header]`, `[screenshots-polish]`, `[screenshots-modtabs]` (869 / 1448 / 1810 px) |
| Rack host | `osp::plugin::rack::Host` (`shared/andor-ui/Source/EffectRack.*`), modules in `MacroPopups.cpp` (`UnifiedRack`, `MovementRack`, `EchoRack`, `SpaceRack`); product opt-in `ShellProduct::rackModules = { movement, echo, space }` |
| FILTER / MORPH today | popovers only: `product/Source/AnalogPopovers.cpp` (`tonePopover`, `morphPopover`), macros 2 and 0 |

## Parameter bindings per rack (as the engine defines them)

### MOVEMENT (per lane: lane A uses the original IDs, lane B `analog.movement.b.*`)

| Control | Parameter (lane A) | Type / choices |
|---|---|---|
| MODE | `movement.mode` | choice Drift, Tape, Chorus, Pulse, Shaper |
| INTENSITY / DEPTH | `analog.movement.a.level` | float % |
| lane ON | `analog.movement.a.on` / `.b.on` | bool |
| DRIFT SPEED, PITCH, TONE | `movement.drift.speed`, `.pitch`, `.tone` | float (Hz / cents / % via formatters) |
| TAPE WOW, FLUTTER, WEAR | `movement.tape.wow`, `.flutter`, `.wear` | float |
| CHORUS RATE, WIDTH, STEREO | `movement.chorus.rate`, `.width`, `.stereo` | float (no sync exists) |
| PULSE TIMING | `analog.movement.a.sync` | bool FREE / SYNC |
| PULSE RATE (FREE) | `movement.pulse.rate` | float Hz |
| PULSE RATE (SYNC) | `analog.movement.a.pulse.division` | choice (cycles) — **a second parameter**: one knob, two bindings |
| PULSE SHAPE, STEREO | `movement.pulse.shape`, `.stereo` | float |
| PHASE (synced PULSE, SHAPER) | `analog.movement.a.phase` | float % |
| SHAPER PATTERN | `movement.shaper.pattern` (+ `movement.shaper.custom`) | choice: 12 library patterns; CUSTOM, saved |
| SHAPER RATE | `movement.shaper.rate` | choice 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 |
| SHAPER TARGET | `movement.shaper.target` | choice Volume, Filter, Both |
| SHAPER SMOOTH | `movement.shaper.smooth` | float |

### ECHO

| Control | Parameter | Type / choices |
|---|---|---|
| MODE | `echo.type` | choice Tape, BBD, Ascend |
| TIMING | `echo.sync` | bool FREE / SYNC |
| TIME (FREE) | `echo.time` | float 20..1500 ms |
| TIME (SYNC) | `echo.division` | choice (shaping::echoDivisionName) — one knob, two bindings |
| FEEDBACK, TONE | `echo.feedback`, `echo.tone` | float |
| AGE (TAPE, BBD) / CLOCK (ASCEND) | `echo.age` | float; ASCEND shows it as CLOCK (13 → 7 kHz, `EchoDelay::clockHz`). **BBD has no exposed CLOCK**: its clock follows TIME; `echo.age` is BBD's wear (drive, aliasing). BBD keeps AGE. |
| STEREO | `echo.stereo` | choice Mono, Ping-Pong, Wide (index 0/1/2 = MONO/PING/WIDE top→bottom) |
| INTERVAL (ASCEND) | `echo.ascend.interval` | choice +12, +7, +5, -12 |
| MOTION (ASCEND) | `echo.ascend.motion` | choice Rise, Steady, Late |

### SPACE (seven modes = `space.engine` × `space.type`)

| Mode | Parameters |
|---|---|
| ROOM / HALL / PLATE / SPRING | `space.engine` = Rooms, `space.type` = Room, Hall, Plate, Spring; `space.preDelay`, `.size`, `.decay`, `.damping`, `.modulation`, `.width`, `.lowCut`, `.highCut` (all real for every room type) |
| ABYSS | `space.engine` = Abyss, `space.abyss.variant`; + `space.abyss.gravity`, `.resonance`, `.freeze` (**bool**) |
| VINTAGE | `space.engine` = Vintage, `space.vintage.variant`; + `space.vintage.era` (Early, Later), `space.vintage.diffusion` |
| BLOOM | `space.engine` = Bloom, `space.bloom.variant`; + `space.bloom.sync` (bool), `space.bloom.time` / `space.bloom.division` (one knob, two bindings), `space.bloom.diffusion` |

### FILTER (macro 2: cutoff, unchanged)

| Page | Control | Parameter | Type / choices |
|---|---|---|---|
| FILTER | TYPE | `analog.filter.model` | choice Ladder, OTA (2-way segmented) |
| | HIGH-PASS | `analog.filter.ota.hpf` | choice Off, 0, 1, 2, 3 (5 options: dropdown) |
| | OTA voicing | `analog.filter.ota.character` (Juno, Jupiter), `analog.filter.ota.slope` (24, 12 dB), `analog.filter.juno.variant` (6, 60, 106) | OTA only |
| | RESO, ENV, KEYS, DRIVE | resonance, envDepth, keyTrack, filterDrive (`params::`) | float |
| | ATTACK DECAY SUSTAIN RELEASE VELO | filter envelope + velocityToFilter | float |
| VOICE | ENV / GATE | `analog.juno.vcaMode` | 2-way segmented |
| | ENV LINK | `analog.juno.envLink` | bool |
| | VOICES | `analog.polyphony` | choice 1, 2, 4, 8, 16 |
| | UNISON | `analog.unison` | bool |
| | DETUNE, SPREAD | `analog.unison.detune`, spread | float |
| | LFO 1 AMOUNT, CUTOFF, PITCH 1, PITCH 2, DELAY, RISE | motion, lfoCutoff, lfoPitch1/2, lfoDelay, lfoRise | float |
| | WHEEL | `analog.lfo.wheel` | bool |
| | CHORUS | `analog.juno.chorusMode` | choice Off, I, II, I+II (4-way) |
| | HISS | `analog.juno.chorusNoise` | bool |
| | ANALOG, BIAS, TRIM | analogAmount, driveBias, driveTrim | float |
| | FREE / RESET | `analog.phase` | choice Free, Reset |
| PAIR | SYNC | `analog.sync` | bool |
| | RING, BLEND | ringMix, blend | float |
| | POLY MOD FILT ENV, OSC 2 | polyModEnv, polyModOsc2 | float |
| | PITCH 1, WIDTH 1, FILTER | `analog.polyMod.toPitch1`, `.toWidth1`, `.toCutoff` | **three independent bools** |
| | NOISE LEVEL | noise | float |
| | WHITE / JUNO | `analog.noise.color` | 2-way |

### MORPH (macro 0)

| Control | Parameter | Type |
|---|---|---|
| ON | `analog.morph.enabled` | bool |
| MODE | `analog.morph.method` | choice Imprint, Fuse, Weave |
| CARRIER | `analog.morph.carrier` | choice OSC 1, OSC 2, Both |
| MORPH, TEXTURE, ATTACK, FORMANTS, DETAIL | `analog.morph.position`, `.texture`, `.attack`, `.formants`, `.detail` | float |
| OUTPUT | `analog.morph.output` | choice Morph Only, Layered |
| LEVEL MATCH | `analog.morph.levelMatch` | bool |
| ROOT | `analog.organic.root` | int -1 (AUTO) .. 127 |

## Gaps and conflicts found (need a decision or the references)

1. **The design references are not in the handoff**: `ANDOR_MOVEMENT_RACK_APPROVAL.html`, `ANDOR_ECHO_RACK_APPROVAL.html`, `ANDOR_SPACE_RACK_APPROVAL.html`, `ANDOR_RATE_AND_VERTICAL_SWITCH_APPROVAL.html` and the seven `*_REVIEW.png` (and, for FILTER / MORPH, `cream_ceramic_filter_module.png`, `cream_morph_rack_with_spectral_visualiser.png`) were not uploaded; only the three specification documents were. The spec's table gives some dimensions and colours, but not the actuator, segment, stepped-knob or A/B key drawings, or the per-family graph widths.
2. **PULSE PHASE**: the spec's PULSE row lists five controls (no PHASE). `analog.movement.*.phase` is real (approved with the lanes) and only the rack exposes it for lane B.
3. **ROOM / SPRING**: the spec lists six controls (no LOW CUT / HIGH CUT), but both cuts are real for every room type.
4. **Layout families**: the spec asks for two (Standard, Dense) and a fixed 294 px PRESET; the build has three (compact / medium / dense) with the picture widening leftward, from the previous request.
5. **BBD CLOCK**: not exposed by the DSP (see ECHO); BBD keeps AGE, as the spec allows.
