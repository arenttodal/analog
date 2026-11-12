# D / AUX Refinement Addendum 03: report

Branch: `feature/aux-d` in both repositories. Baseline: analog `19a6a45`, OSP `4b6772a5` (the D / AUX implementation, gates 0–8). Nothing is merged into `main` or the 1.0.0 release candidate.

**MORPH:** untouched, as this addendum requires. The D MORPH stage (Option D, approved just before this addendum) is paused, unpushed, in `git stash` ("D MORPH (Option D) - paused for Addendum 03").

## Gate 0: audit

| Area | Finding |
|---|---|
| Where AUX lives | `product/Source/AuxUI.{h,cpp}` (strip, rack), `AnalogEngine` (recording, D Kontakt, keys), `dsp/Synth.cpp` (`renderAux`, `mixAux`), `AnalogParameters` (`analog.aux.*`); the shell's opt-in `ProductInfo::auxStrip` |
| **The C / D drop bug** | `ShellEditor` is the only `FileDragAndDropTarget` JUCE can reach: the modulation overlay covers every part, so a drop on the strip or the rack never got there. The editor sent any drop off the cards to `addLayers`, which loads card C in ANALOG (a hybrid product). D never received a file |
| FILTER | D already went through a stereo `ExternalFilter` that follows `analog.tone` and the FILTER rack; ROUTE (`analog.aux.filterFollow`) could take it around |
| M / S | The strip drew its own keys. On ANALOG's ceramic skin, the cards' M / S are drawn by the card's surface (`skin::drawKeys`), so they never matched |
| **NOISE alignment** | Not a centred container: the plain `ValueSelector` draws its text **right-aligned** in a fixed 124 px box, so a shorter name (NOISE) moved right |
| RACK / ROUTE / LOAD | RACK and LOAD sat inside the well; ROUTE was beside the menu |
| Pitch | `rhythm::LoopPlayer` already re-pitches slices on the grid (`Settings::pitchRatio`, card C RHYTHM's TUNE / OCT): each slice starts at its place on the host grid and plays faster or slower. Tempo and loop length are unchanged |

## What changed, by gate

### Gate 1: drop ownership (shared, opt-in)

- `osp::plugin::DropOwner` (new, `andor-ui/DropOwner.h`): a part that owns the files dropped on it.
- `ShellEditor` routes each drag and drop to the topmost owner under the pointer, skipping the overlays, then up its parents.
  - The owner gets the hover (`dropHover`) and the drop (`dropFiles`). The drop is consumed: it never also reaches a card.
  - Hover highlights only where the drop will land.
- The AUX strip and the AUX rack are owners. A drop anywhere on them is D's:
  - one WAV / AIFF / FLAC: D RHYTHM's loop;
  - anything else: refused, with the reason in the well, and nothing changed.
- With an AUX strip, the editor is interested in every drag, so an unsupported file is refused by D rather than passed on. Card C's drops are unchanged.
- **OSP and products without owners: unchanged** (shell test `[aux]`; visual regressions).

| Drop | Lands in |
|---|---|
| Card C's well or card | C (the existing rules) |
| D's strip (well or elsewhere on it) | D |
| D's rack (graph or elsewhere on it) | D |
| A / B cards | the existing rules |
| Empty plugin space | **the existing rule** (ANALOG: the hybrid recording, card C), never D. See "For the owner" |

### Gate 2: always through the FILTER

- ROUTE, "Through / Around the FILTER" and the rack's FILTER FOLLOW / AROUND switch are gone.
- The reader always sets D's filter on (`filterFollow = true`). The FILTER macro, its automation and the FILTER rack move D in every mode, whether the strip is shown or hidden and whether the rack is open or closed.
- `analog.aux.filterFollow` stays as a hidden compatibility parameter: in the schema, never read, not offered for automation.
- **An older experimental session saved with AROUND now goes through the FILTER, intentionally.**
- D's place in the chain is unchanged: after the voices and C's buses, before the chorus, KALEIDOSCOPE and the FX route, summed once. SUB is unchanged.

### Gate 3: M / S

- `LayerStateButton` (the cards' own M / S) gains an opt-in constructor for a product source's parameter. D's M / S are that component: same sizes (27 × 21, stacked beside the badge), faces and states. SOLO is lit in D's plum.
- The strip draws ceramic keys as the cards do.
- **Audio, as before:**
  - D's solo joins the cards' (only while D is powered);
  - mute, solo and power are ramped (10 ms);
  - hiding the strip never mutes D;
  - MUTE silences D's audio, not its envelope (RHYTHM's established contract).
- The well's veil says why D is not heard: OFF, MUTED or NOT SOLOED (the cards' wording).

### Gate 4: the strip's layout

- **Three zones, from the cards' own bounds** (`ShellEditor::cardDisplayBounds`, nothing hard-coded):
  - **A's zone:** D badge (at the cards' badge place), M / S, AUX and its state, power.
  - **B's zone:** the dark well, **at B's well's x and width exactly** (tested).
  - **C's zone:** the mode's knobs, RACK, and the menu dots at the cards' menu place.
- RHYTHM: RATE, PITCH, OCT, MOD AMOUNT (a double cell, so its caption fits), LEVEL. NOISE: COLOR, TONE, LEVEL. KONTAKT: LEVEL.
- **The mode selector:** top left of the well (12 px in, 6 px down), sized to its name, so its left edge never moves between modes (tested in all three). The readout (file name, BPM, beats, clock) sits at the right of the well's top row.
- **RACK:** one key, the cards' EQ / NOTES face, immediately left of the menu dots. It is pressed in while the rack is open. The rack's X and Escape close it, as before.

### Gate 5: click and drop loading

- No LOAD or LOAD LOOP buttons remain.
- The strip's well and the rack's graph are the loading surfaces:
  - RHYTHM: a click browses for a loop; a drop loads it.
  - KONTAKT: a click loads D's Kontakt, or opens its window once loaded.
  - NOISE: nothing to load.
- **Failure handling:**
  - A replacement shows LOADING while the loop in use keeps playing.
  - A failed one (not audio, unreadable, not copied) **leaves the loop in use playing**, with the reason shown in the well.
  - A cancelled chooser changes nothing.
  - Loading stays on the loader thread.

### Gate 6: PITCH and OCT

- **New parameters:** `analog.aux.rhythm.pitch` (−12…+12 semitones, default 0) and `analog.aux.rhythm.octave` (−2…+2, stepped, default 0). Both are appended at version hint 37 and automatable. Total transposition = PITCH + 12 × OCT. An older preset has neither, so both stay at 0.
- **Method: slice re-pitch on the grid.** The loop player places every slice on the host grid as before; PITCH and OCT only change how fast each slice is read.
  - Higher: a shorter slice, a gap before the next one.
  - Lower: a longer slice, cut at the next one.
  - **The tempo, the beat positions and the loop's length never change** (tested: ±12 st, +7, +24, OCT ±1; the ticks on the same grid; the 2 kHz tick at 4 kHz at +12 st).
  - There is no pitch-shifter latency: the method has none, so nothing is added to the reported latency.
- **FREE** now runs the slices on D's own clock at the loop's detected tempo, so PITCH does not change the tempo there either.
- **The envelope keeps the loop's own timing.** It is read at the un-pitched position (`LoopPlayer::Settings::envelopeOnGrid`, opt-in for D only; card C RHYTHM is unchanged), so the modulation pulses never move with the pitch (tested).
- **Automation is glided** (20 ms): no spike, no transport reset, the grid kept (tested with a sweep and an octave jump). A host tempo change moves the ticks to the new grid at once, with the pitch kept (tested 100 → 140 BPM).
- **Usable range: ±24 semitones total.** The player's ratio is 0.25…4. PITCH ±12 with OCT ±2 can ask for ±36; anything past ±24 is held at ±24. Past about ±12 st, slices become very short (up) or heavily truncated (down). This is natural for drum and percussive loops, but audible on sustained material (pads, held notes).
- **Proposal, if sustained material matters:** a true time-preserving pitch shifter (a granular or phase-vocoder stage after the loop player, with latency compensation). Not built here.
- **A recording with no beat grid** (no rhythm found) plays whole at its own speed in FREE. There, PITCH changes its speed too, like a sampler.

## Verification (this container: Linux, headless)

| Check | Result |
|---|---|
| Drops: D's strip well, elsewhere on the strip, D's rack → D only; C's well → C only; after hide / show; after rack open / close; save / reload (each its own) | PASS (`[aux][drop]`, through the editor's real drop path at real points) |
| Unsupported file on D: nothing changes; failed `.wav`: the loop in use plays on, its reason kept | PASS |
| FILTER moves D (FILTER 10 % → 95 %: D NOISE more than 3× louder, RMS), the old AROUND unread | PASS |
| D's well at B's well's x and width; M / S are `LayerStateButton`, 27 × 21; RACK left of the menu, not over the well; knobs between them; the selector's left edge the same in all modes | PASS |
| PITCH / OCT: grid, pitch, envelope timing, automation, host tempo change; OCT +1 renders identically to PITCH +12 | PASS |
| Bindings test (every D parameter, PITCH / OCT included, moves the sound); parameter golden (+2 lines: PITCH, OCT) | PASS |
| Suites: ANALOG DSP 293 test cases; ANALOG product 181 (2 skipped: the Bukkehorn recordings are not in the repository); OSP plugin 162; example product 10; visual regressions (OSP, example, ANALOG with D hidden) | PASS |

**Evidence:**
- `docs/aux/addendum03/`: the strip in every mode, the rack, drop hover, MUTED, NOT SOLOED, soloed, off; 869 / 1448 / 1810 px; `compare-strip-before-after.png`; `compare-noise-alignment.png`.
- `docs/demos/aux/aux03-*.wav`: PITCH −12 / 0 / +12, OCT −1 / +1, RATE 2× with PITCH +5, PITCH automated.

### NOT VERIFIED (owner tests)

- **Drag and drop from Finder or a DAW's browser** onto the strip, the rack and card C. The tests drive the editor's own drop handlers at real points, but no real desktop drag ran here.
- **Listening:** PITCH / OCT on real loops (drums vs sustained material); the FILTER on D in a real session.
- **Real Kontakt as D** (load, window, state); **DAW** automation of PITCH / OCT, sync and offline bounce.

## For the owner

1. **A drop on empty plugin space** keeps the existing rule: in ANALOG it loads the hybrid recording (card C), as the editor always did. The addendum's table says "existing behavior", but its test list says "neither C nor D". Should a drop outside the cards and D do nothing?
2. **PITCH beyond ±12 st on sustained loops:** keep slice re-pitch, or add a time-preserving pitch shifter (latency, CPU)?
3. **D MORPH (Option D):** paused for this addendum. Land it now?
