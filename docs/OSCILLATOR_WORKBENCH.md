# ANDOR ANALOG — Oscillator Workbench

_30–31 October 2026. Interface only: no DSP, parameter, preset or saved-state change._

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

### Navigation (31 October 2026: BOTH)

The header: **BOTH | OSC 1 | OSC 2 | PAIR** on the left; on the right a small waveform key
(back to the large waveform, as clicking OSCILLATORS) and PIN. The focused pages (OSC 1,
OSC 2, PAIR) also have their depth, QUICK | DETAILED, quieter, beside them; BOTH has one view
and shows no depth. Changing the page or the depth changes the view only, never a value.
BOTH is the first page; the page and depth chosen are remembered as interface preferences.

**BOTH** shows the two oscillators at once, each in its own equal section (one component for
both, so they cannot drift apart):

| Zone | Contents |
|---|---|
| Identity | OSC 1 / OSC 2 (its light; a click opens its own page), CLASSIC / DCO / WIDE, ON at the right |
| Wave and character | The wave choice on the picture's top edge (CLASSIC: OSC 1 SAW / PULSE / BOTH, OSC 2 its seven; DCO: SAW and PULSE as two switches; WIDE: its waves, SUPER SAW among them) over the oscillator's own picture (`AnalogEngine::pictureOf`); CHARACTER beside it: CLASSIC WIDTH, LFO PW; DCO SUB, PWM and its source (MAN / LFO / ENV) under PWM; WIDE SHAPE, SPREAD |
| Pitch and output | OCT, SEMI under the picture; LEVEL under the character. OSC 2's fine DETUNE is the card's own knob below the well (and on OSC 2's page) |

Every model uses the same knob size and rows (the row under CHARACTER is kept for the DCO's
PWM source in every model), so changing a model changes only the controls that differ.

Widths (the well's own, the instrument's scale aside):

- **Wide** (≥ 900 px: the OSCILLATORS card alone, or beside the ORGANIC dock): the two
  sections side by side about a hairline, at most 620 px each.
- **Compact** (a loaded ORGANIC card beside it, about 650 px): two strips, one above the
  other: name, model, wave choice and ON on a line; under it a small picture, the character
  and LEVEL. OCT, SEMI and the DCO's PWM source are on the oscillator's own page (its name
  opens it).

| Page | QUICK | DETAILED adds |
|---|---|---|
| OSC 1 / OSC 2 (one grid for both) | Heading: ON, model, its waves. Then CYCLE (the oscillator's own picture) → PITCH (OCT, SEMI, DETUNE on OSC 2) → SHAPE (CLASSIC: WIDTH; DCO: WIDTH, SUB; WIDE: SHAPE, SPREAD) → OUTPUT (LEVEL) | A second row under the same families: LFO PITCH; CLASSIC: LFO PW; DCO: PWM, LFO PW, and PWM BY (source); WIDE: WIDTH, LFO PW |
| PAIR | INTERACTION (SYNC, KEYBOARD, LO FREQ, RING) and POLY MOD (FILT ENV, OSC 2, to PITCH 1, WIDTH 1, FILTER), about one hairline | VOICES under INTERACTION (polyphony, UNISON, UNI DETUNE, SPREAD); NOISE under POLY MOD (its level, WHITE / JUNO) |

**Where things went:** WAVE's oscillator levels are in BOTH; NOISE and its colour are in
PAIR, DETAILED; KEY DNA is ANDOR OSP's own KEY DNA editor, opened from the card's KEY DNA
key in GRANULAR.

### The ORGANIC dock

Empty, ORGANIC is a compact **ADD SAMPLE** dock beside the OSCILLATORS card (about 80 / 20 of
the source area; `ProductInfo::hybridSampleDock`, opt-in, other products unchanged): a click
browses; a WAV, AIFF or FLAC dropped anywhere on the instrument loads it (the dock lit while
one is dragged). Loading opens ORGANIC's own card (the two cards then equal; the workbench
keeps its page and pin, compact). Removing the recording brings the dock back; a file that
cannot be read leaves the dock saying so (TRY ANOTHER FILE), the sound untouched. Empty
ORGANIC plays nothing and changes nothing (no parameter moves with the layout).

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
- [x] H — BOTH (two equal sections, wide and compact), the waveform key, NOISE in PAIR; the ORGANIC dock (80 / 20, 50 / 50 when loaded).

## Tests

`product/tests/AnalogProductTests.cpp`, tag `[workbench]`:

- OSCILLATORS opens and closes the workbench in the well:
  - the card's bounds are unchanged and no popover opens;
  - the waveform key, Escape, Return/Space and M/S behave as described;
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
