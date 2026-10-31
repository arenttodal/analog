# ANDOR ANALOG — Oscillator Workbench

_30 October 2026. Interface only: no DSP, parameter, preset or saved-state change._

The OSCILLATORS settings moved out of a large floating popover into the card's own dark
well. Clicking **OSCILLATORS** (beside the A badge and M/S) swaps the waveform for the
**workbench**, and clicking it again brings the waveform back. DETUNE, the card's first knob,
is an ordinary tuning knob.

## Behaviour

| Action | Result |
|---|---|
| Click OSCILLATORS (or Return/Space on it, when focused) | Waveform → workbench; workbench → waveform (this also unpins it) |
| Escape (no popover open) | Back to the waveform, like clicking OSCILLATORS |
| QUICK / DETAILED | Changes the view only; no value moves |
| PIN | Keeps the workbench: it comes back after the EQ, and when the window or the plugin opens again |
| EQ opens | The EQ takes the well. When it closes: the workbench if pinned, otherwise the waveform |
| Preset / factory sound / automation | The workbench stays open and shows the new values |
| M, S, mode, menu, card knobs, macros | Unchanged; they do not close the workbench |

- The well shows one view at a time: waveform, workbench, EQ or NOTE MAP. A hidden workbench
  never takes a click.
- While the workbench is shown, the waveform is hidden and does not repaint underneath.
- The card never moves or resizes.

### Navigation (final, 31 October 2026)

There are two dimensions only:

- **Page:** WAVE | OSC 1 | OSC 2 | PAIR, on the left of the header.
- **Depth:** QUICK | DETAILED, on the right with the pin. It is smaller and quieter than the
  page tabs. All three sit on one centre line.

Changing the page or the depth changes the view only, never a value. WAVE is the first page;
the page and depth chosen are remembered as interface preferences.

| Page | QUICK | DETAILED adds |
|---|---|---|
| WAVE | The oscillators' mix as one large picture (the engine's own); OSC 1, OSC 2 and PAIR chips (light, model, waves; SYNC / RING / POLY MOD / LO FREQ lit when active). A click on a chip opens that page | SOURCES: OSC 1 and OSC 2 level, NOISE, noise colour (WHITE / JUNO) |
| OSC 1 / OSC 2 (one grid for both) | Heading: ON, model, its waves. Then CYCLE (the oscillator's own picture) → PITCH (OCT, SEMI, DETUNE on OSC 2) → SHAPE (CLASSIC: WIDTH; DCO: WIDTH, SUB; WIDE: SHAPE, SPREAD) → OUTPUT (LEVEL) | A second row under the same families: LFO PITCH; CLASSIC: LFO PW; DCO: PWM, LFO PW, and PWM BY (source); WIDE: WIDTH, LFO PW. Nothing in the first row moves across |
| PAIR | INTERACTION (SYNC, KEYBOARD, LO FREQ, RING) and POLY MOD (FILT ENV, OSC 2, to PITCH 1, WIDTH 1, FILTER), centred about one hairline | VOICES under INTERACTION: polyphony, UNISON, UNI DETUNE, SPREAD |

**Where the former MORE and KEY DNA went:**

- NOISE and its colour → WAVE, DETAILED, SOURCES.
- Polyphony, UNISON, UNI DETUNE, SPREAD → PAIR, DETAILED, VOICES.
- KEY DNA → ANDOR OSP's own KEY DNA editor, opened from the card's KEY DNA key in GRANULAR.
  It is no longer in the workbench.

Controls that do nothing in the current state are faded, not hidden, and stay editable:

- WIDTH and LFO PW without a pulse wave;
- PWM when its source is MAN;
- SPREAD unless the wave is SUPER SAW.

Another model's controls are hidden but keep their values. An oscillator switched off keeps
its controls editable and quieter.

**View preference.** Pinned, Quick/Detailed and the Detailed section are an interface
preference of ANDOR Analog:

- They are kept in `Documents/ANDOR Analog/Interface.xml`, so they survive closing the window
  and restarting the plugin.
- They are never a parameter, never in a preset, never in the session.
- They are written only on the message thread, when the musician changes them.

## Architecture (all opt-in; ANDOR OSP unchanged)

**OSP, shared code (`external/osp`):**

- `CardWellEditor` (andor-ui): a product's editor in a source card's well, plus its view state.
- `EngineCard::setWellEditor` / `setWellEditorShown`:
  - the editor sits exactly on the display's bounds;
  - the EQ and NOTE MAP hide it;
  - the card's `refresh()` updates it while it is shown;
  - the GRANULAR controls and the muted veil stay off it.
- `ProductInfo::generatorWellEditor` (andor-shell): a generator product's factory for the
  editor.
- `ShellEditor`:
  - `toggleWellEditor`, `showWellEditor`, `isWellEditorShown`;
  - the heading's door, EQ hand-back, Escape, and pinned-at-open;
  - the editor is made the first time it is asked for, then kept (hidden).
- `ShellProcessor::wellViewState` / `setWellViewState`: the preference file.
- `SegmentedControl::setOnDark`, `ValueSelector::setOnDark`: graphite variants for the well.

**ANALOG:**

- `product/Source/AnalogOscillatorWorkbench.{h,cpp}`: the workbench.
  - Every control is a shared control (`MiniKnob` in its on-dark style, the dark
    `SegmentedControl` / `ValueSelector`) or a well key, bound to an existing parameter.
  - Only the page being shown exists, so no parameter is attached twice.
  - Each picture is the engine's own `AnalogEngine::pictureOf` for that oscillator alone. It
    is recomputed only when a setting it shows changes.
- The old `createOscillatorPopover` / `blendPopover` are removed (no caller remained).
- `Product.cpp` registers `generatorWellEditor`; DETUNE keeps `generatorStartControl` with no
  popover.

## Milestones

- [x] A — Baseline: code audit, screenshots of the old popover.
- [x] B — Heading toggles the in-well editor; DETUNE is a plain knob.
- [x] C — Shared opt-in well-editor slot, EQ arbitration, ownership/teardown; OSP suite and
  design capture.
- [x] D — QUICK: two oscillator halves with live attachments, model-specific controls,
  pictures.
- [x] E — DETAILED: OSC 1 / OSC 2 / PAIR / MORE / KEY DNA.
- [x] F — PIN and persistence; EQ/preset/window-recreation transitions.
- [x] G — Captures at one and two cards, 869 / 1448 / 1810 px windows and @2x; tests;
  documentation.

## Tests

`product/tests/AnalogProductTests.cpp`, tag `[workbench]`:

- OSCILLATORS opens and closes the workbench in the well:
  - the card's bounds are unchanged and no popover opens;
  - WAVE, Escape, Return/Space and M/S behave as described;
  - DETUNE is a plain knob, and no popover route remains;
  - no parameter changes.
- Each model's controls appear in Quick and Detailed:
  - automation and other external changes are followed;
  - another model's values are kept;
  - the controls move their parameters;
  - inactive controls are faded;
  - Quick ↔ Detailed changes no value;
  - every section's controls are present;
  - the layout fits (inside the well, ≥ 24 px targets, no overlaps) in Quick and every
    section, at one card and at two cards.
- The EQ takes the well and gives it back to a pinned workbench:
  - presets leave it open;
  - the pin and the section survive the window closing and a new plugin instance;
  - clicking OSCILLATORS unpins;
  - the session holds none of it;
  - closing the window with the workbench open is safe.

Screenshots: `ANALOG_SCREENSHOT_DIR=… andor_analog_tests "[screenshots]"`, files
`analog-workbench-*.png`.

## Caveats

- Checked in screenshots and headless tests on Linux (Xvfb), not yet in a DAW or by ear.
- Interaction with NOTE MAP is handled by the shared card (it would hide the workbench), but
  ANDOR Analog has no NOTE MAP, so that path is untested here.
- Modulation drag-and-drop onto workbench knobs uses the shared `MiniKnob` drop target
  (`paramId`). It was not exercised by a dedicated test.
