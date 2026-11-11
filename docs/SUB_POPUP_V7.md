# SUB popup V7

Branch `feature/sub-popup-v7`, from `claude/upbeat-allen-3i0jey` at `1c0cb6d`. That commit is V6 with SUB following
FILTER and MOVEMENT; OSP is at `e7a3834`. The release candidate and `main` are untouched.
The spec is `ANDOR_SUB_MODAL_V7_IMPLEMENTATION_SPEC.md`, and the approved composition is `01_APPROVED_SUB_MOCK.png`.

## What changed

The change is interface only, in two files:

- `product/Source/SubPanel.{h,cpp}` is new.
- `createSubPopover()` (in `AnalogPopovers.cpp`) now returns a `SubPanel`. The old `RangeKeybed` was removed; the display took over its behaviour.

**`SubPanel : MiniPanel`** (compact, 440 × 340 units, the same in FLAT and TAPER):

- **Top row.** WAVE `SINE | TRI | SQUARE | SAW` and a compact OCT `-1 | -2` share one baseline. Both are the shared
  `SegmentedControl`, bound to `subWave` and `subOctave`.
- **`SubResponseDisplay`** is one dark well (the oscillator / SPACE slate), painted in layers:
  1. A faint grid.
  2. A still picture of the selected wave, following `SubOsc::sample` (SAW rises, SQUARE starts high).
  3. A tint over the playable range.
  4. The relative response: bright inside the range, dashed outside it.
  5. The FROM and slope handles (TAPER only).
  6. The keyboard, with the range lit and its two end caps. Labels use `getMidiNoteName (k, true, true, 4)`
     (MIDI 60 = C4), the parameters' own text.
- **Bottom row.**
  - `SubGritValve`, a small drawn valve beside GRIT. Its glow is `pow (grit, 0.8)`, dimmed with SUB off.
    It is static: it changes with the parameters, never by itself.
  - GRIT and LEVEL: shared `MiniKnob`s.
  - RESPONSE `FLAT | TAPER` over the smaller FROM and dB / OCT knobs. In FLAT those two knobs are dimmed, but kept, readable
    and editable.

**The response is `SubOsc::keyGain` without LEVEL:**

- the loss is `slope × (key − FROM) / 12` above FROM with TAPER, and 0 otherwise;
- the graph's height spans the shown loss in 6 dB steps (3 dB at least). The scale shows, quietly, while a curve handle is hovered or dragged;
- the mock's values (C3–D4, FROM C4, 5.5 dB / oct) draw as they sound: under 1 dB inside the range. The longer fall is
  dashed, as a forecast;
- slope 0 with TAPER draws flat, and the mode stays TAPER.

**Edits.** Each edit is one host gesture with `ParameterAttachment`, undoable, and ended even if the popup closes mid-drag:

- **Range ends.** Drag either end; the two never cross. Both on one key: the drag's direction picks which one moves.
- **The lit span.** Drag it to move the whole range, keeping its width, within 0..127.
- **Keyboard keys.** Left / Right move the low end; with Shift, the high end. The old keybed's Shift path could never fire,
  because `KeyPress == keyCode` is false whenever a modifier is held; it now compares key codes.
- **FROM.** Drag horizontally, in whole semitones.
- **Slope.** Drag vertically: down is steeper; 0 and 12 at the ends. The handle sits an octave above FROM, or at the display's edge when that octave is off screen.
- **Resetting.** A curve handle's double-click resets it.

**Keyboard window:**

- C1–C8 when the range fits there;
- otherwise the same width shifted by octaves (C-1…C6, C0…C7, C2…C9, or G2…G9 up to MIDI 127);
- otherwise all of 0..127.

Saved keys are never clamped to what is shown. The window holds still during a drag.

## Not changed

- No parameter, ID, range, default or state version changed. `parameter-schema.txt` keeps 579 entries.
- No DSP, preset, routing or plugin ID changed.
- Nothing runs on the audio thread. Repaints come from parameter attachments (message thread), and the panel has no timer.

## Tests

| Test | What |
|---|---|
| `SUB V7: the panel's composition…` | One baseline for WAVE / OCT; the order of the rows; the same size in FLAT and TAPER; FLAT dims (does not hide) FROM and dB / OCT; every knob is its existing parameter |
| `…the drawn response is SubOsc::keyGain's…` | FLAT and TAPER × FROM 40 / 60 / 90 × slope 0 / 5.5 / 12, against a real `SubOsc`. The mock's example near-flat; LEVEL never moves the curve; slope 0 flat |
| `…the range on the keyboard…` | One gesture per drag; never crossing; one key (direction); span move (width kept, one undo step); 0..127 view; shifted views; Left / Right / Shift; bounds |
| `…FROM and the slope on the curve…` | Graph → knob sync; knob or automation → graph with no gesture; note names (C4); slope down / up to 12 / 0; double-click reset; FLAT (no handles, values kept); popup destroyed mid-drag ends its gesture |
| `…the wave picture and the valve…` | Automation reaches WAVE / OCT; the valve brightens monotonically with GRIT, dims with SUB off, and never changes GRIT |
| `…the popover inside the instrument at every size` | At 869, 1086, 1448, 1810 and 2172 px the card is inside the window; opening it does not switch SUB |

The screenshots (`[screenshots-sub-v7]`, `docs/screenshots/sub-v7`, at 1448 and 869 px, 2× crops):

| File | State |
|---|---|
| `v7-00-*` | Before (V6) |
| `v7-01` | Defaults, SUB off |
| `v7-02` | Defaults, plus the full instrument |
| `v7-03` | The mock's values, plus the full instrument |
| `v7-04` | FLAT |
| `v7-05-*` | TRI / SQUARE / SAW |
| `v7-06-grit-*` | GRIT 0 / 50 / 100 |
| `v7-07` | 0..127 |
| `v7-08` | One key |
| `v7-09` | A steep taper |
| `v7-10` | Mock, V7, current SUB and SPACE side by side |

## Deliberate differences from the mock

- **The response is real.** The mock's long sweep is illustrative; V7 draws the actual loss and dashes everything outside the range.
- **Header and values.** The title keeps the shared compact `MiniPanel` header (the mock's larger title is not used). The values shown are the patch's, not the mock's.
- **No keyboard focus on the segments.** WAVE / OCT / RESPONSE use the shared `SegmentedControl`, which takes no keyboard focus (as in every
  ANDOR popover). The host and accessibility reach them as parameters. Adding focus would change OSP's shared control.
