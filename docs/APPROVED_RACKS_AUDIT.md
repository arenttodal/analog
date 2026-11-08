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

## Decisions (product owner, 2026-11-08)

- Design references: the HTML prototypes and review PNGs will be uploaded; G1 (the ceramic
  switch, segmented and stepped-rate components) starts from them, not from the text alone.
- PULSE: INTENSITY · TIMING · RATE · SHAPE · STEREO · **PHASE** (PHASE kept as the sixth,
  active when synced).
- ROOM and SPRING keep all eight controls (LOW CUT and HIGH CUT are real for every room type).
- Layout: the approved spec's two families (Standard, Dense) with fixed-width MODE (165) and
  PRESET (294) replace the three-density layout.

## Implementation (G1 – G4, FILTER and MORPH)

### Shared components (OSP `shared/andor-ui`, opt-in: only the racks build them)

| Component | File | Approved source | Notes |
|---|---|---|---|
| `rackui::VerticalSwitch` (slot) | RackControls.* | rack HTML `.vertical-track` 20 × 61, radius 11, 2 px `#c7bfb2` rim, `#18191a/#393535/#18191a` channel; `.actuator` 17 × 19, radius 9, 145° `#fffdf4 → #dad2c6 (73 %) → #b6aea4`; travel 39 px; labels 9 px | two positions: labels above / below; three: labels to the right over 75 px; click a label or the slot, drag snaps, ↑ ↓ Home End, Space / Return; named undo transaction; accessibility value by name ("Ping-Pong") |
| `rackui::VerticalSwitch` (mini) | RackControls.* | `.miniSwitch` 16 × 26, radius 9, `#322f2d`, rim `#b3aca2`, thumb 10 × 12 `#faf6ed → #d6cfc4`, 9 px travel | header ON (MOVEMENT's lane, MORPH); "ON" word 9 px `#777069` beside it |
| `rackui::SegmentedSwitch` | RackControls.* | `.segChoice` 27 px, radius 4, `#eee9e0`, edge `#c8bdb0`, dividers `#cfc6ba`, 10 px text; active `#ccdee4` / `#476a7d`, inset shadow | other modules: the same saturation and brightness in their hue |
| `rackui::LaneKey` | RackControls.* | `.lane` 40 × 23, radius 4, `#eeeae3`/`#cec6bb`; active `#d9e5ea`/`#8cabbc`/`#3b5868`; LED 5 px `#6798b2` / `#b8b2a9`; 2 px apart | right-click / Shift+F10: copy, swap |
| Stepped rate knob | `MiniKnob::setDetents` | rate study `.tickring` 2 × 5 px ticks `#aaa49a`, the chosen 3 × 7 in the module's colour | same knob, same place in FREE and SYNC; detents from the parameter's own choices |
| Rack cell metrics | `MiniKnob::setRackMetrics`, `rackui::Cell` | `.lab` 11 px `#74716c` in 18 + 4; 59 px knob; `.readout` 13 px `#4b4945`; list 25 px at 40; segments 27 px at 42; switch band 79 px at 29 | ANDOR's own fonts (Inter) in place of the prototype's Arial |
| MODE / option lists | `ValueSelector::setRackStyle` | `.modeChoice` 165 × 27, radius 5, `#f6f2eb`, `#d3cbbf`, 11 px; `.optionSelect` 25 px, `#f8f4ec`, 10 px | |
| PRESET field + `···` | `RackPresetBar`, `UnifiedRack::ActionsKey` | `.preset` 294 × 27, `#eae4db`, `#d4ccbf`, 32 px arrow wells; name 11 px semibold `#607b8b`; `.secondary` `···` | name: the list; `···`: save, rename, delete, folder, MOVEMENT's lane actions |
| Graph frame | `Visual` (detailed), `rack::GraphFrame` | `.graph` `#1c2528 → #192124`, edge `#343f43`, radius 5, 160 px | |

### Geometry (module coordinates; the rack is the plugin's 1378 px rack, the approved 1392 px stage scaled 1 : 1 in reference pixels)

| Item | Value |
|---|---|
| Content inset | 20 px each side (19 padding + 1 border) |
| Controls' column | MOVEMENT 57 %, ECHO 61 %, SPACE 68 % of the content (per module, never per mode) |
| Divider | column + 17 px, 1 px `#cbc4b9`; graph 18 px after it to the right inset |
| Selector row | 39 px under the 32 px title; MODE 165 × 27 at x 20, PRESET 294 × 27 12 px after, `···` 30 × 27 12 px after |
| Controls | 8 px under; cells 112 px high (the two-position switch's labels fit); standard slots 104 / 95 / 119, 7 px; dense (8+ controls: ECHO ASCEND, SPACE) 89 / 85 / 100, 5 px |
| Rack height | 214 px (unchanged), SHAPER's precision editor 268 |

### Deviations from the prototype (and why)

- The rack keeps the plugin's 214 px height (the prototype stage is 235): the title row is 32 px (34) and the controls start 8 px under the selector row as approved.
- SHAPER RATE's detents are the engine's six divisions (1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32), not the prototype's nine; PULSE's synced RATE uses the lane's cycle choices.
- PULSE keeps PHASE (product owner's decision), live only while synced (dimmed in FREE, so nothing moves).
- ROOM / SPRING keep LOW CUT and HIGH CUT (product owner's decision).
- BBD keeps AGE (its clock follows TIME: no CLOCK parameter exists).
- SPACE's MODE shows the engine's name (ABYSS); its variant is chosen in the MODE list's engine submenus and named by the preset (as the approved image: "ABYSS" / "Abyss Deep").
- VINTAGE ERA (Early / Later) is a two-way segmented switch; BLOOM's SYNC is the TIMING switch (FREE / SYNC) with its TIME knob stepped when synced.
- The PRESET field's arrows are drawn chevrons in the approved colour, not the prototype's text glyphs.

### FILTER and MORPH (inline racks, for approval: not yet approved designs)

- Built by the product (`ShellProduct::productRacks`, ANALOG `AnalogRacks.*`) on the shared `rack::ParameterRack` (the same cells, switches, frame), opened from the FILTER and MORPH macros' names like the others; their popovers stay the quick view and share the parameters.
- Both are two rows tall (a documented exception: their control count), the same height on every page.
- FILTER: pages FILTER / VOICE / PAIR (interface only; the last one is remembered). Its graph keeps one rectangle on every page: FILTER the filter model's small-signal response (LadderFilter's and OtaFilter's own equations and the high-pass positions), VOICE the AMP envelope (or GATE), PAIR a schematic with the pair's settings. POLY MOD's PITCH 1, WIDTH 1 and FILTER are three independent switches. With ENV LINK on, the filter envelope's knobs rest (dimmed) and the group says so. OTA's SLOPE is live only with OTA; CHARACTER and JUNO stay live (they also set the high-pass's curve).
- MORPH: ON in its title row, a short status ("Load an eligible sample into C to morph") when card C is silent; MODE (IMPRINT / FUSE / WEAVE) and CARRIER (OSC 1 / OSC 2 / BOTH) as segmented switches in the selector row; MORPH TEXTURE ATTACK FORMANTS DETAIL; OUTPUT (MORPH ONLY / LAYERED), LEVEL MATCH, ROOT under them. Its graph is a concept view drawn from the settings and labelled as such: the engine publishes no analysis to the interface yet (MORPH 2.0's telemetry stage replaces it with measured spectra).


## Correction (2026-11-08): one rack height, one set of lines

- Every rack - MOVEMENT, ECHO, SPACE, FILTER (every page and sub-page) and MORPH - is the
  standard 214 px rack at 1448 px (title 32, body 172, the 10 px foot), with the same title,
  selector row, horizontal inset and graph (160 px). No page enlarges the rack; the keyboard
  never moves when pages change (tested).
- One set of cell lines for every control (`rackui::captionHeight` 18, `controlCentre` 59,
  `valueTop` 92, from the cell's top): a knob's centre, a vertical switch's TRACK centre (its
  two labels above and below it; its three labels beside it on their detents), a segmented or
  list key's centre all sit on the centreline. Fixed in the shared `rackui::Cell` and
  `MiniKnob::setRackMetrics`, not per instance. (The knob is 6.5 px lower than the prototype's
  so the two-position switch's upper label clears the caption.)
- FILTER: its type, high-pass and OTA voicing in the selector row; RESO ENV KEYS DRIVE and the
  envelope in one row. VOICE has sub-pages AMP (ENV / GATE, ENV LINK, VOICES, UNISON, DETUNE,
  SPREAD), MOD (LFO 1's AMOUNT CUTOFF PITCH 1 PITCH 2 DELAY RISE, WHEEL), CHARACTER (ANALOG,
  PHASE, BIAS, TRIM, CHORUS, HISS). PAIR: SYNC RING BLEND, POLY MOD's FILT ENV, OSC 2 and its
  three destination switches, NOISE level; the noise colour in the selector row.
- MORPH: METHOD and CARRIER in the selector row; MORPH TEXTURE ATTACK FORMANTS DETAIL in one row
  with OUTPUT, LEVEL MATCH and ROOT after a quiet divider. MORPH 2.0's controls will take pages
  (MORPH / IDENTITY / MOTION), never a taller rack.
- Tests: `analog: every rack and page on the same lines ...` (every control of every module,
  mode and page on one centreline; one rack top and bottom; one instrument height),
  `[screenshots-guides]` (the ten views with their guide lines; 869 and 1810 px windows).

## Refinement (2026-11-08): the racks' hierarchy

What each rank of FILTER and MORPH is, told apart at a glance, on the same 214 px rack and lines:

- **Navigation** (shared `rack::ParameterRack`): the pages (FILTER / VOICE / PAIR) are the
  primary words - 11 px medium, the shown one dark with a 2 px underline in the module's
  colour. VOICE's sub-pages (AMP / MOD / CHARACTER) are a rank below - 10 px regular, lighter in
  both states, a 1 px underline in the same place. A short quiet rule separates the pages from
  the sub-pages, and the navigation from the page's contextual settings (fields, not words).
  The cells below are the sound's controls.
- **Groups**: a captioned group (ENVELOPE, LFO 1, POLY MOD, SETUP) has a hairline from its
  caption to its end, beside the quiet divider between groups, so what belongs together reads
  without moving a control off the grid. A *quiet* group (`group (caption, true)`) is
  configuration, not shaping: its captions and values a step lighter (`rackui::palette::
  quietCaption / quietValue`, opt-in on `MiniKnob` and `rackui::Cell`).
- **FILTER page**: only the settings the chosen model uses, in a fixed order - TYPE; SLOPE
  (OTA only); VOICING and JUNO whenever they act (the OTA's voicing, and the high-pass's curve:
  so with OTA, or with the ladder while HPF is on; JUNO not with JUPITER); HPF. A rebuild
  happens only when the model, HPF on/off, the voicing or ENV LINK changes (not on values).
  RESO ENV KEYS DRIVE | the ENVELOPE group (ATTACK … VELO), its caption and hairline.
- **VOICE**: AMP's graph is the AMP envelope (or the gate). MOD's graph is LFO 1 as the engine
  applies it, from the parameters: each target's swing now (AMOUNT × its depth, bipolar bars
  in st), with the wheel linked an outline to the full depth the wheel brings in, and the
  fade-in (DELAY, then RISE) to time scale. CHARACTER has nothing measured to draw, so no
  graph: a restrained status of where each setting acts in the voice (VOICE: ANALOG, PHASE ->
  FILTER DRIVE: BIAS, TRIM -> CHORUS: mode, HISS; the chorus quiet while off).
- **PAIR**: the signal-flow schematic stays. The noise's COLOR (WHITE / JUNO) is no longer in
  the navigation strip: it sits over the NOISE knob it belongs to (`selectorSegment (…, over)`).
- **MORPH**: METHOD and CARRIER, the two selector groups at the top; MORPH TEXTURE ATTACK
  FORMANTS DETAIL the primary row; then the SETUP group, quiet: OUTPUT (a two-position switch,
  MORPH / LAYERED - no longer a stretched 215 px segment), LEVEL MATCH, ROOT. The header holds
  only the title and ON: card C's eligibility is the graph's empty state ("Load an eligible
  sample into C to morph", with the engine's own reason under it) instead of the concept view.
  ATTACK's tooltip says it is card C's onset kept, not an envelope time (MORPH 2.0's contour
  ATTACK / RELEASE will be separate). IDENTITY / MOTION pages are not shown: they do not exist
  yet.
- No DSP, parameter ID, automation or state change. OSP's own racks and popovers do not use
  these paths (`ParameterRack` is built only from a product's `productRacks`; the quiet styling
  is opt-in), so OSP stays pixel-identical.
- Screens: `[screenshots-guides]` now also renders FILTER with OTA and with the ladder's
  high-pass on, MOD with LFO 1 set, CHARACTER with the chorus on, and MORPH with a source.

### Remaining hierarchy notes (for review)

- PAIR's COLOR sits in the selector row's band over the NOISE knob (the row's only free space
  at 214 px); it reads as NOISE's, but it is still a field in the top band, not inside the cell.
- MOD's bars use each parameter's full span (CUTOFF ±48 st, PITCH ±12 st), so small depths
  draw short; the values beside them carry the precision.
- MORPH's spectral view remains a labelled concept view until MORPH 2.0 publishes measured
  spectra.
