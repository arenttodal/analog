# PITCH and MOD wheels: settings popovers and VIBRATO

Branch `feature/aux-live-input` in both repositories (shared code in ANDOR OSP, Analog's DSP
here). Owner decision (15 Nov 2026): CONTROL = ASSIGNED uses an **assignable AMOUNT** (0–100 %,
default 0). Vibrato stays silent until AMOUNT is assigned or raised; DEPTH stays the most it can
swing.

## What changed

| Area | Change | Where |
|---|---|---|
| Wheel captions | Each wheel component is 7 px wider on either side (`Wheel::sidePad`). The wheel is drawn exactly where it was. The caption (MOD with its socket) has its own bounded region (`captionArea()`) and is no longer clipped. Only the wheel and the caption take the mouse (`hitTest`). | `shared/andor-ui/Source/MainSections.*`, `InstrumentFrame.cpp` |
| Click vs drag | A press that stays under 3 px opens the settings. On MOD, a drag of 3 px or more carries a modulation cable, as before. Dragging the wheel body turns the wheel, as before. | `Wheel::mouseDown/Drag/Up` |
| Door look | The caption works like a macro's name: hover darkens and underlines it, the cursor is a pointing hand, and the underline stays in the accent colour while the popover is open. Over MOD's socket the cursor is the dragging hand. | `Wheel::paint`, `mouseMove` |
| PITCH BEND popover | RANGE is the existing `bendRange` parameter (ID, range and automation unchanged), shown as ±N st, with a picture of the wheel's reach and the specification's help text. BEND is removed from Advanced. | `shared/andor-ui/Source/WheelPopups.*`, `ShapingPopups.cpp` |
| MOD WHEEL popover (VIBRATO) | Controls: VIBRATO OFF/ON, DEPTH (cents), RATE (Hz), DELAY, RISE, AMOUNT, CONTROL (MOD WHEEL / ALWAYS ON / ASSIGNED) and SOURCES. A picture shows one note's pitch over 2.5 s: the outline is full DEPTH, the line is what plays now. The help text follows CONTROL. AMOUNT is dimmed unless CONTROL is ASSIGNED. | `WheelPopups.*` |
| Popover behaviour | A ceramic MiniPanel above the caption, kept inside the instrument by `placePopover` (the window never resizes). It closes on a click elsewhere, Escape, the same caption again, or another popover opening. | `ShellEditor.cpp`, OSP `PluginEditor.cpp` |
| Parameters | The shared `vibrato.enabled/depth/rate/delay/rise/control/amount/sourceA/sourceB/sourceC` (version hint 28; off by default) and Analog's `analog.vibrato.sourceSub` (v39, off). All are automatable and bound. | `AndorParameters.*`, `AnalogParameters.*` |
| Modulation | New core destinations `VIBRATO DEPTH` (cents, span 100) and `VIBRATO AMOUNT` (0..1), both per voice. They are appended to `mod.routeN.destExt` (choices 9 and 10); the frozen `dest` list is unchanged. Both can be assigned to USER macros by right-click. | `src/engine/Modulation.*` |
| DSP | `osp::vibrato` (pure C++, `src/engine/Vibrato.h`): a per-note sine whose phase and age restart at note on. It waits DELAY, then rises over RISE with a raised cosine. OSP adds it to each voice's control-rate pitch (32 samples) for the layers chosen. Analog adds it to OSC 1, OSC 2 and card C pitched (OSC 3, sample, zones), plus SUB on its own voices (32-sample control rate). It is never post-FX. | `InstrumentVoice.cpp`, `dsp/Synth.cpp`, `dsp/SubOsc.*` |

Never pitched by vibrato: noise, D (AUX), live input.

Left as they were:
- CC 1 routes;
- Analog's classic WHEEL-to-LFO link;
- the old LFO-to-pitch depths.

## Compatibility

Vibrato is off by default. Every older session, preset and factory sound is unchanged sample
for sample, because off adds exactly 0.0 cents in the same places. These cases are also
bit-identical:
- on with MOD WHEEL at 0;
- ASSIGNED with AMOUNT 0;
- no chosen source sounding.

Tests in both repos assert this. The route `dest` choice list is frozen, and the new
destinations sit only in the append-only `destExt` list.

## Gates

| Gate | Result |
|---|---|
| G0 audit | The MOD caption (12.5 px medium, tracked, plus a 13 px socket ≈ 44 px) was centred in a 36 px component and clipped at both edges. PITCH (≈ 40 px) was clipped too. |
| G1 caption geometry | Fixed with the bounded caption region. Geometry tests are in OSP and Analog. |
| G2 click vs drag | Tests cover a click opening, a drag of 3 px or more making a cable (no popover), the body turning the wheel, and rapid clicks. |
| G3 PITCH BEND + Advanced | Done. Advanced no longer has BEND (tests). |
| G4–G6 vibrato DSP | OSP: `tests/unit/VibratoTests.cpp`. Analog: `tests/VibratoTests.cpp` (±50 cents measured, centred, deterministic, block-size independent, no allocation). |
| G7 MOD WHEEL popover | Done (screenshots below). |
| G8 parameters / modulation | Covered by schema goldens, route tests (VEL → AMOUNT, VEL → DEPTH), recall, and macro eligibility. |
| G9 presets | Factory presets are unchanged, so vibrato is off in all of them (no preset was given vibrato). |
| G10 regression, renders, screenshots, docs | See below. |

## Evidence

- Renders (`docs/demos/wheels/`):
  - OSP: a sustained take without vibrato, with ALWAYS ON, and with the wheel; GRANULAR; a chord where each note gets its own vibrato.
  - Analog: OSC 1 without and with vibrato; OSC 2 alone of the pair; card C as OSC 3; a CC 1 wheel ramp; the default patch unchanged.
- Screenshots: `docs/screenshots/wheels/` (869, 1448 and 1810 px; MOD WHEEL off, ALWAYS ON and ASSIGNED).

## OWNER TESTS (UNVERIFIED here)

- Listening: whether DEPTH 25 cents at 5.5 Hz with DELAY 250 ms and RISE 400 ms sounds
  natural on a bowed recording, on the oscillators and on card C.
- DAW: automation of every `vibrato.*` parameter; CC 1 from a hardware wheel; the popovers in
  a host window at fractional scale.
- Kontakt / external sources are not pitched by vibrato (by design). Confirm it is not missed.
