# FX Rack V2 — G0 audit (8 November 2026)

The V2 handoff (`ANDOR_FX_RACKS_V2_HANDOFF`: boutique hardware racks, RETRO, the remaining V1 routing
gates) checked against the code as it is on the working branch, before any V2 change.

## Baseline

| | |
| --- | --- |
| ANDOR ANALOG | `claude/upbeat-allen-3i0jey` @ `7740c06` (FX routing gates 4-5) |
| ANDOR OSP (submodule `external/osp`) | `claude/space-presets` @ `25b940b` |
| JUCE | 8.0.9 |
| Plugin identity | `aumu Aanl Ospx`, bundle `com.osp.andoranalog`, version 0.1.0 (unchanged by V2) |
| Product state version | **12** (`<FxRouting schema="1" graph=...>` engine child, written only when CUSTOM) |
| Product parameters | 448 in `product/golden/parameter-schema.txt`: 165 from before FX routing + **283** `analog.fx.*` (version hint 28) |
| `fx::Type` | drive, movement, echo, space, spread, compressor, eq, filter, phaser, flanger, shifter, convolution, utility (13; append-only) |
| Pools | {1,1,1,1,2,2,2,2,2,2,1,1,2}; at most 16 effects, 4 levels of nesting |
| CLASSIC | `osp::PostProcessor::process`, untouched: 114 frozen renders (`tests/golden/fx-baseline-renders.txt`) bit-identical |
| Tests at baseline | `analog_dsp_tests` 160 cases green; `andor_analog_tests` 85 cases green (2 skipped: recordings not in the repository); visual parity 0 px; OSP `osp_plugin_tests` 160 cases green |

## What V2 assumed vs what is there

| V2 statement | Code | Consequence |
| --- | --- | --- |
| State 12, gates 0-5 done | Yes (plus the quick popover, FX presets, chain presets, CONVOLUTION impulse building) | Upgrade in place; next state version for RETRO is **13**; next parameter version hint **29** |
| Single 214 px FX rack exists | `product/Source/FxRack.cpp` on OSP `ParameterRack` (title 32 + body 172) | The faceplate is a layer on `ParameterRack` (opt-in), not a new host |
| Splitter grammar exists, DSP unfinished | `compile()` refuses splitter graphs (they were played as a plain parallel group before `7740c06`) | Gate 8 work |
| Latency alignment unfinished | `CompiledGraph::latency` is 0; CONVOLUTION (256), FLANGER through-zero, BLUR report latency but lanes are not aligned and the host is not told | Gate 8 work, before RETRO CODEC adds more |
| User IRs, Lock FX, stems/buses, Advanced | Not built | Gate 8 work |
| FET 76 has no threshold | Correct: `fet.input`, `fet.output`, `fet.attack`, `fet.release`, `fet.ratio` (4/8/12/20/ALL), `fet.color`, `fet.colorOnly`, `fet.link` | The concept image's THRESHOLD is not shown in FET mode |
| SPREAD Focus/Rotation/Microshift absent | Correct: mode, width, voices, detune, hyperRate, dimSize, dimRate, dimAmount, lowMono, lowMonoHz, mix, output | Shown only if built as new DSP (Gate 7); otherwise the real controls |
| Global DRIVE BIAS must not be `analog.drive.bias` | `analog.drive.bias` / `analog.drive.trim` are the voice filter's input drive | Not touched; any global BIAS is a new ID |
| MOVEMENT five modes, A/B lanes; ECHO TAPE/BBD/ASCEND; SPACE ROOMS/ABYSS/VINTAGE/BLOOM | As stated (OSP's shared `UnifiedRack`s) | Gate 3 skins them only through an opt-in in OSP; OSP stays pixel-identical |

## Rack host facts the faceplate must respect

- The rack host (`osp::plugin::rack::Host`) draws the ceramic panel, each module's title (LED + name in
  the first 160 px of the 32 px title row) and the collapse key (26 px, right, above the module).
- A module covers its title row and body (`titleOffset()` = 32, body 172); its own title-row parts
  go in `layoutTitleRow`. A module that paints its title row opaque replaces the host's title.
- `ParameterRack`: selector row 39 px, cells on the shared lines (caption 18, control centre 59,
  value 92, cell 112), graph 160 px high in the right column (`leftShare`), content inset 20 px.

## Captured before any change

Screenshots of the working UI (scratchpad `v2-before/`): every FX rack and quick popover at 1448,
EQ at 869 / 1810, COMPRESSOR FET / MULTIBAND, SHIFT BLUR, ECHO / SPACE racks in every engine at
869 / 1448 / 1810, the routing view (CLASSIC, CUSTOM, parallel, library) at 869 / 1448 / 1810.

## Plan (gates as in the handoff, against this baseline)

1. G1 — `ParameterRack::Faceplate` (opt-in): ceramic chassis, enamel title rail with title and
   subtitle, LED following the real ON, ceramic power key, preset in the rail, overflow menu,
   left stripe motif, screws, graphite glass with a bezel. COMPRESSOR and STEREO SPREAD first.
2. G2 — the other seven new effects on the faceplate, page layouts per mode.
3. G3/G4 — the original four and MORPH / voice FILTER: opt-in faceplates; OSP pixel-identical.
4. G5/G6 — RETRO: `fx::Type::retro` appended (pool 2), `analog.fx.retro.<n>.*` (hint 29), state 13,
   TAPE / VINYL / DIGITAL 80s-90s / CODEC DSP, presets, rack, the spec's 7.8 tests.
5. G7/G8 — DRIVE creative modes (appended, CUSTOM only), latency planner and lane alignment, host
   latency, splitters, stems and buses, Advanced view, Lock FX, user IRs.
6. G9/G10 — the screenshot matrix, realtime / CPU / latency measurements, what can run here
   (pluginval on Linux VST3 if available); DAW and auval need a Mac and are reported as not run.
