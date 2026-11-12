# USER MACROS V1.1: report (gates 1–4)

USER MACROS gives ANDOR ANALOG a second bank of six macros beside the six CORE ones.
- Each USER macro moves up to 8 controls (48 in all), each over a LOW..HIGH range around where the control already sits.
- A new assignment never makes the sound jump.
- With no assignments the instrument is bit-exact as before. CORE, OSP and every factory sound are unchanged.

Baseline, branches and the inventory: [USER_MACROS_V1_1_GATE0.md](USER_MACROS_V1_1_GATE0.md).

**Status of this push.**
- Gates 1–4 are complete.
- The owner's folder-tab refinement is in.
- Gate 5 (FX-library slot targets, orphan / reconnect) is **not built yet**. It comes next, on this branch.
- A draft PR follows gate 5. Nothing is merged into `main` or the 1.0.0 release candidate.

## Interface

**CORE and USER tabs.**
- Two small folder tabs rise from the macro panel's top-left edge.
  - They follow the owner's mockup of 11 November, which replaces §3.2's flat micro-tab.
  - Each is 40 units wide and rises 14 units, with 8-unit rounded tops.
- The chosen tab is cut from the panel itself.
  - It is the panel's own surface, raised by the tab's height.
  - It flares into the edge with soft fillets, with no line across the join.
  - A thin muted-sage line runs under its word.
- The other tab sits behind it: recessed ceramic, a quieter border and dimmer letters, with the panel's edge passing in front.
- Each tab's target is 46 × 25 units, larger than drawn. Both are keyboard-focusable (Space / Return / arrows) and have accessible names.
- Switching banks only changes what the six knobs show:
  - no knob, name, light or modulation-panel element moves;
  - the window keeps its height;
  - no parameter changes.
- The bank shown is a local interface preference (`Interface.xml`, `<MacroBank shown="core|user">`). It is never stored in a preset or session.

**USER knobs.**
- USER 1–6 read left to right in CORE's six places. CORE's data order differs from its visual order (ECHO and SPACE are swapped); USER follows the visual columns.
- Names default to `MACRO n`.
- A double-click renames in place: sanitised, at most 24 characters, uppercase.
- Right-click on a name: Edit, Rename, Clear (Clear asks first).
- The CORE MORPH lamp is hidden in USER.

**Assigning.**
- **Lens drag (USER only):** drag the light under a USER knob.
  - Every eligible control gets a faint ring in the macro's colour (faders get a rounded outline).
  - The control under the pointer gets a strong ring and its name.
  - Dropping on it assigns, and the panel opens on the new assignment.
  - Escape, or dropping anywhere else, makes nothing and leaves no undo step.
- **Right-click on a control:** *Assign to USER* lists the six macros by name.
- **Pick mode (+ ADD ASSIGNMENT):** the panel folds into the banner "Assign to MACRO n · Click a control · Esc". A click on a ringed control assigns it.
- **Eligible targets:** continuous controls the engine reads every block (an `AudioParameterFloat` in the engine's reader).
  - Not switches, choices, modes, routes or modulation sources.
  - Not another USER macro (so no feedback).
  - FX-library slot parameters arrive with gate 5.

**The panel (inspector).** A compact MiniPanel anchored above the macro's name.
- One row per assignment: target, then LOW → HIGH in the target's own units, tidied to a musician's precision.
- LOW / HIGH sliders: each drag is one undo step.
- SWAP, EASE (linear / smoothstep), BYPASS, REBASE, REMOVE.
  - REBASE writes the control's current heard value as its new base, then re-anchors.
- `+ ADD ASSIGNMENT`.
- While a macro's name is hovered or its panel is open, thin arcs in its colour mark the controls it moves.

## Engine

**Parameters (append-only, version hint 34).**
- `analog.userMacro.1` .. `analog.userMacro.6`, float, 0–100 %, default 0.
- They are automatable like any other parameter.
- The schema golden gained exactly these six lines (591 → 597).

**State.**
- State version 18 → 19.
- An optional `<UserMacros schema="1">` child, written only when something is not default. Example:
  ```xml
  <UserMacros schema="1">
    <Macro index="0" name="BLOOM">
      <Map target="analog.resonance" enabled="1" anchorMacro="0.62" anchorTarget="0.15"
           low="0.15" high="0.8" curve="linear"/>
    </Macro>
  </UserMacros>
  ```
- Reading it is bounded and forgiving. It enforces:
  - at most 8 maps per macro, and no duplicates;
  - the ID character set, and finite numbers only;
  - sanitised names.
  Anything else is dropped, never trusted.
- A missing child means no assignments. Every 1.0 / V6 / V7 / DNA project opens exactly as before.

**Mapping (§6.2, anchored, additive, zero-jump).**
- Each map stores the macro position `a` and the control's value `b0` (both normalised) at the moment it was made, plus LOW `L` and HIGH `H`.
- The offset is:
  - `(L - b0) · shape((a - m) / a)` below `a`;
  - `(H - b0) · shape((m - a) / (1 - a))` above it;
  - exactly 0 at `a`.
  `shape` is linear, or smoothstep for EASE.
- Assigning never changes the sound: the offset is exactly 0 at the anchor. A new map started neutral (`L = H = b0`); since the destination-first update ([MACRO_DESTINATION_FIRST.md](MACRO_DESTINATION_FIRST.md)) it gets a visible default range of a quarter of the travel each way, still anchored, so there is still no jump.
- Offsets from several macros on one control add, then clamp once, in the parameter reader.
- Moving the knob itself moves the base: the overlay rides on it.

**Audio thread.**
- On each edit the message thread compiles the state into a fixed table (at most 48 maps, parameter indices resolved) and hands it over through a lock-free triple buffer.
- Each block, `AnalogEngine::applyUserMacros` reads the newest table:
  - it smooths the six macro values and each touched target's offset (about 20 ms, one pole);
  - it adds offsets to the snapshot before global modulation;
  - removed targets glide out.
- No allocation, no lock. With an empty table and nothing gliding, it returns at once: bit-exact.

**Undo.** Every edit (assign, range, swap, ease, bypass, rebase, remove, rename, clear) is one host undo step, holding the whole state before and after.

## Tests (Linux, GCC 13, this container)

**analog_dsp_tests `[usermacro]` (5 cases).**
- M06: neutral at 0 / 50 / 73 / 100 % and every position.
- M07: range, invert, ease, bypass.
- M08: macros add, then clamp.
- M12: NaN, infinities and out-of-range values made safe.
- The triple buffer across two threads: never torn, never waiting.

**andor_analog_tests `[usermacro]` (5 cases).**
- G1:
  - six parameters at 0, nothing saved, and moving them with no map is bit-exact (M01, M25);
  - names and maps round-trip; bad data is made safe (M12, M17).
- G3: BLOOM moves FILTER resonance, OSC 2 level and SPACE decay (M06–M09).
  - No jump at 0 / 50 / 73 / 100 %.
  - Offsets sum and clamp; the knob moving under the overlay works.
  - An assignment is one undo step (undo / redo).
  - A removed map glides out.
- G2, at 869, 1448 and 1810 px:
  - the folder tabs' size, rise, targets and place;
  - 50 bank switches change no parameter and not the height;
  - USER 1..6 left to right, in CORE's places;
  - the USER knob is the parameter.
- G2 / G4:
  - lens drag (cancel, assign, nowhere);
  - the panel's LOW / HIGH and SWAP with undo;
  - right-click *Assign to USER*;
  - eligibility;
  - pick mode with its banner and Escape;
  - rename.

**Other tests touched.**
- The bindings test ("every enabled control moves the sound") gives each USER macro one assignment before checking that it is heard.
- The SHAPER DNA parameters got the same kind of setup (MOVEMENT heard, lane A in SHAPER, DNA on). This fixed DNA's one red case in its full-suite run.
- The shell's parity test runs ANALOG's product on a stand-in engine. The USER descriptor now answers neutrally there (default names, nothing assigned, no panel) instead of throwing.

**Full 13-program ctest on this tree:** see the push's commit message.

**Screenshots:** `docs/screenshots/user-macros/`.

| File | Shows |
|---|---|
| `um-00-core-1448.png` | CORE |
| `um-tabs-core-1448.png`, `um-tabs-user-1448.png` | the folder tabs |
| `um-03-user-assigned-1448.png` | USER with BLOOM |
| `um-05-drag-1448.png` | a lens drag over OSC 2 LEVEL |
| `um-06-pick-1448.png` | pick mode |

They are reproduced by `ANALOG_SCREENSHOT_DIR=... andor_analog_tests "[screenshots-user-macros]"`.

## Not done yet, or not run here

- **Gate 5:**
  - FX-library slot targets (an `fxparams::Reader` overlay; convolution and latency parameters excluded);
  - orphaning when a route removes the slot, inside the same undo step;
  - a sticky orphan with an explicit Reconnect;
  - M14–M16.
- **CPU benchmarks at 0 / 8 / 48 maps:** not measured yet (gate 5's report).
- **Hosts:** Ableton Live, Logic, REAPER and the Standalone are not run:
  - automation of the six macros;
  - host undo;
  - saved projects reopened.
- **Real input:** mouse and trackpad on the tabs, lenses and panel; Retina and Windows scaling.
- **Validators:** pluginval / auval run in CI on push.
- **A listening test.**

None of these is reported as passed.
