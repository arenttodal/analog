# Destination-first macro assignment and visual range editing

Branch: `feature/kontakt-external-morph` in both repositories (stacked on USER MACROS V1.1,
SATELLITE + RHYTHM and the Kontakt experiment). Nothing is merged into `main` or the 1.0.0
release candidate.

This builds on the USER MACROS V1.1 system ([USER_MACROS_V1_1.md](USER_MACROS_V1_1.md)). The
macro engine, its state and the macro-first workflows were not rebuilt.

## 1. Audit: what already existed and what was reused

| Area | Where | Reused as |
|---|---|---|
| Macros | Six USER macros, `analog.userMacro.1..6` (0–100 %, automatable). CORE's six are fixed macros and are not assignable. | The macros of "Assign to Macro" (six, not eight) |
| Mapping | `dsp/UserMacroCore.h`: anchored and additive. Each map stores the macro position `a` and the control's value `b0` when it was made, plus LOW `L` (where macro 0 % takes it) and HIGH `H` (100 %). The offset is exactly 0 at `a`. | Unchanged. The halo shows and edits exactly this |
| Engine | `AnalogEngine::applyUserMacros`: a compiled table, lock-free hand-over, offsets smoothed (about 20 ms) and added to the snapshot before global modulation | Unchanged |
| State and undo | `<UserMacros schema="1">` (state version 19). Every edit is one host undo step holding the state before and after (`usermacros::edit`) | Unchanged format, no migration. Range drags join the same undo path |
| Macro-first workflows | Lens drag, pick mode (+ ADD ASSIGNMENT), the panel's LOW / HIGH / SWAP / EASE / BYPASS / REBASE / REMOVE | Unchanged |
| A control's right-click | OSP's `ModulationOverlay::contextMenu`: routes, sources, MIDI controllers, then a product's `extendContextMenu` (which offered "Assign to USER") | Extended: Assign to Macro and the assignment's items |
| Modulation halos | OSP's `ModulationOverlay`: a ring outside each modulated knob (the selected route's sweep from the base), other routes as thin arcs just outside, the "now" marker, the endpoint handle, the graphite readout, popover covers, caption breaks | The macro ranges are drawn by this same overlay, with its geometry, strokes, handles, marker and readout |
| "Arcs while a macro is hovered" | `MacroAssignOverlay` (show mode), driven by `ShellEditor::updateUserMacroArcs` | Replaced by the overlay's macro focus. The drag and pick modes are unchanged |

OSP is unaffected. Every new overlay entry point is an opt-in `std::function` that ANDOR OSP
does not set. Its visual reference test and its plugin suite pass unchanged.

## 2. What changed

### Right-click on any eligible control

The control's own items come first and are unchanged. Then, after a separator:

- **Assign to Macro ▸** lists `Macro 1 … Macro 6`. A named macro reads `Macro 1 — BRIGHTNESS`. A checkmark marks the macros already assigned. Choosing one assigns it at once: no macro has to be selected, armed or opened first.
- When the control is assigned:
  - Edit Macro Range;
  - Reset Macro Range;
  - Show Macro Assignment Details (opens the macro's existing panel on that row);
  - Remove from Macro n (one item per assigned macro);
  - Remove All Macro Assignments (only when several macros are assigned).
  - With several macros assigned, the per-macro items become one submenu each.
- **Eligible controls** are unchanged: a continuous parameter the engine reads every block. Switches, choices and the macros themselves get no assignment item.
- **Controls that are not modulation destinations** but are macro-eligible now get the menu too. They no longer show an empty MODULATION heading.

Both workflows call the same `usermacros::assign`, so they make exactly the same assignment.

### A visible default range, with no jump

Before, a new map was neutral (`L = H = b0`). Its range was invisible until edited in the
panel.

- A new assignment now gets a quarter of the travel each way: `L = b0 − 0.25`, `H = b0 + 0.25`. Where there is no room on one side (less than 10 % of the travel), that end goes the other way instead.
- The anchor is unchanged, so the control sounds exactly as before at the macro's current position. This is verified bit for bit against a reference render.
- This applies to every workflow (lens, pick, right-click), as they share `assign`.

### Halos (one vocabulary for modulation and macros)

| State | Drawing |
|---|---|
| No assignment | The knob exactly as before |
| Assigned, its macro not focused | A thin arc in the macro's colour over its range (1.8 px). On a modulated knob it sits just outside the route's ring and arcs; several macros stack outward |
| Macro focused (its panel open, its name hovered, chosen from a control's menu, its knob touched, a handle dragged), or the knob hovered with no route of its own | The range is the ring, drawn as a selected route is (glow, 4.5 px stroke, "now" marker). The 0 % end is a hollow handle and the 100 % end a filled one, which shows the direction. Routes on the same knob become the thin outer arcs |
| Hovered or editing | The guide track, a heavier stroke and the graphite readout: `MACRO 1 · BRIGHTNESS`, `At 0 % → 20 %`, `At 100 % → 85 %`, `Now 47 % (42 %)` |
| Macro moving (playing, host automation) | The marker follows: `now` is recomputed at the editor's refresh (30 Hz) from the macro parameter |

**Choosing a macro.**
- Its rings stay shown until another macro is chosen, a click lands elsewhere, or the bank is switched to CORE.
- Choosing from a control's menu switches to the USER bank, so the macro's knob is in view.
- Only visible controls draw rings. A popover opened later draws its controls' state at once, because it is walked when it appears.
- Rings are hidden under a popover, as before.

### Editing the range on the knob

- Press on the ring of a focused range: the nearer handle follows the pointer along the ring. **Shift** gives fine control (a fifth of the travel). **Escape** cancels: the state, the knob and the undo history are exactly as before.
- One drag is one undo step, including the knob's own move when the dragged end is the anchored one.
- **Reverse ranges** are allowed: drag the 100 % end below the 0 % end.
- **Double-click a handle** to set that end back to the control's own value. **Reset Macro Range** (menu) restores the default range.
- **The anchored end.** An assignment made with the macro at 0 % (the usual case) adds nothing at 0 %, so its 0 % end *is* the control's own value. Dragging that end moves the knob itself (one gesture, host-visible), while the 100 % end stays where it was. Symmetrically for an anchor at 100 %. This keeps the display honest: the halo never shows a range the DSP does not produce.
- **Normal knob gestures are untouched.** The ring sits outside the knob, so dragging the knob still moves the knob.
- Positions are the parameter's **normalised value**, which is the knob's own travel. Skewed (log) controls therefore edit naturally. Readouts use the parameter's own text (Hz, dB, %, s).

## 3. Evaluation order (unchanged, now shown honestly)

```
knob value (the parameter, normalised)
  + Σ USER macro offsets (each anchored map's offset at its macro's smoothed value)
  → clamp to 0..1 (once, in the parameter reader)
  → global modulation routes (in the destination's units, from that value)
  → per-voice modulation, smoothing → DSP
```

- **A macro's halo** shows its own assignment alone, on the control's own value: `base + offset(m)`. Its ends are the values at 0 % and 100 %, clamped where the parameter ends.
- **A modulation route's halo** starts from the knob plus every macro's offset now. Modulation applies after the macros, so its "Base" readout includes them. Before this change it started from the knob alone, which no longer matched the engine once a macro was assigned.
- **Several macros on one control** add. Each is isolated while edited: its own ring, its own handles, its own readout. The others show as thin arcs.

## 4. Tests

`andor_analog_tests "[macrodest]"` has four cases, 124 assertions (Linux, GCC 13, this container).

- **Assignment:**
  - Right-click OSC 1 LEVEL and choose Assign to Macro → `Macro 1 — BRIGHTNESS`. The routing is made and the macro's own list contains it.
  - The default range is 25 % → 75 % around 50 %. No jump: 0 offset, and a render bit-identical to a reference.
  - The halo is focused, with handles and the macro's name. The menu now has the checkmark, the range items and removal. A switch has no assignment item.
  - A click elsewhere returns to the quiet arc.
- **Range editing:**
  - Drag the 100 % handle to 85 %, then the 0 % handle to 20 % (which moves the knob).
  - The engine plays 20 % / 52.5 % / 85 % at macro 0 / 50 / 100 %, and the halo's marker agrees. The oscillator is audibly louder at 100 %.
  - Inverted (100 % → 10 %); one undo step per drag (undo and redo); Escape leaves no change and no undo step; Shift-drag is a fifth of the travel.
- **Existing assignments, modulation and automation:**
  - A neutral map made the old way shows its halo and is never rewritten.
  - With an LFO route on the same knob, the macro is the ring when its macro is focused and the route is the ring otherwise; both stay editable.
  - The route's Base includes the macro's offset.
  - Setting the macro parameter (as host automation does) moves the halo's marker to the engine's value.
- **Compatibility:**
  - Removal from the menu, with undo and redo.
  - A rename propagates to the menu and the halo.
  - Save and reopen keeps names, ranges and direction exactly.
- **Updated:** the G2/G4 case expects the menu's new label "Assign to Macro" (it was "Assign to USER").

**Regressions.** The full ANALOG product suite, the DSP suite, OSP's visual reference and the
products' visual regressions are run before the push; see its commit message.

**Screenshots** are in `docs/screenshots/macro-destination/` and are reproduced by
`ANALOG_SCREENSHOT_DIR=... andor_analog_tests "[screenshots-macrodest]"`:

| File | Shows |
|---|---|
| `md-01-before.png` | OSC 1 LEVEL unassigned |
| `md-02-assigned-focused.png` | Just assigned: the default range, handles live |
| `md-03-range-editing.png` | Mid-drag: the grabbed 100 % handle and the readout |
| `md-04-focused-macro-at-60.png` | The macro at 60 %, the marker inside the range |
| `md-05-quiet-unfocused.png` | Not focused: the thin arc |
| `md-06-modulation-and-macro.png` | An LFO route's ring with the macro's arc outside |
| `md-07-modulation-and-macro-focused.png` | Macro 1 focused: its ring, the route outside |
| `md-08/09/10-*.png` | The whole window, Macro 1 highlighted and quiet, at 869, 1448 and 1810 px |

## 5. Remaining limitations

- **Value fields (the EQ's numbers):**
  - A field shows its macro range as a bar under it, with no handles.
  - Its range is edited in the macro's panel or with Reset Macro Range.
- **Combined effect of several macros:** the macro readout shows each assignment alone. Their sum is shown only where it matters for modulation (a route's Base), not as its own "total" line.
- **Gate 5 (FX-library slots):** these parameters are not eligible yet. That is gate 5 of USER MACROS, unchanged here.
- **Not run here:**
  - real mouse, trackpad and Retina / Windows scaling;
  - DAW automation and host undo in Live, Logic, REAPER;
  - pluginval / auval, which run in macOS CI on push.

  None is reported as passed.
