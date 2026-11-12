# D / AUX: implementation report and handoff

Branch: `feature/aux-d` in both repositories (stacked on `feature/kontakt-external-morph`). Nothing is merged into `main` or the 1.0.0 release candidate.

| Document | What it holds |
|---|---|
| `00_AUDIT.md` | Gate 0: the experiments found, the DSP entry points, the hazards |
| `MORPH_DECISION.md` | **OPEN, owner decides.** This version behaves as Option A |
| this file | What was built, how it was verified, what is not verified, what is deferred |

## What D is

D is a fourth source, beside cards A, B and C. It is not a fourth card: it is a compact strip under the cards and a full-width rack.

```
A OSC 1 ─┐
B OSC 2 ─┼─ voices (MIX / MORPH / FILTER / AMP) ─ decimate ─ C RHYTHM loop ─ C EXTERNAL bus ─┐
C HYBRID ┘                                                                                  │
D AUX: RHYTHM | NOISE | KONTAKT ─ mode fade ─ AUX FILTER (follows FILTER) ─ LEVEL / MUTE ────┤
                                                       chorus ─ KALEIDOSCOPE ─ DRIVE / MOVEMENT / ECHO / SPACE / CUSTOM route ─ out
```

- **Independent of C.** Its own parameters (`analog.aux.*`, version hint 37), its own recording, its own Kontakt instance. Card C HYBRID is untouched.
- **Joins before the shared effects**, after the voices and C's buses, so DRIVE, MOVEMENT, ECHO, SPACE, the chorus, KALEIDOSCOPE and any CUSTOM route process it as they process A/B/C. It never joins SUB's dry path.
- **Follows the FILTER by default.** D's stereo AUX FILTER is a third `ExternalFilter`. `configureBusFilter` maps `analog.tone` (the FILTER macro) and the FILTER rack (resonance, drive, LADDER / OTA, the high-pass, key tracking, the envelope) onto it. ROUTE → "Around the FILTER" (`analog.aux.filterFollow` off) crossfades it out.
- **Ramped.** Power, mute, level and a mode change are ramped (10 ms). Off and faded out, `mixAux` returns before it touches a sample.
- **Solo.** D's SOLO joins the cards' solo only while D is powered: a powered-off D can never silence A/B/C.

## Modes

| Mode | Source | Keys | Strip knobs | Rack |
|---|---|---|---|---|
| RHYTHM | A WAV / AIFF / FLAC loop: LOAD, or dropped on the strip. Copied into the sample store (named by its content), decoded and analysed on the loader thread (`rhythm::prepareLoop`), handed over through `ModelExchange` | None (it plays on the grid) | RATE, MOD AMOUNT, LEVEL | MODE, CLOCK (SYNC / FREE), LOAD LOOP, RATE, LEVEL, MOD: TARGET, AMOUNT, SENS, ATTACK, RELEASE, FILTER FOLLOW |
| NOISE | White / pink (Kellet) / brown (leaky), its own TONE low-pass, MOVEMENT, WIDTH. Seeded from the patch's seed (deterministic) | KEYS: a legato gate from the musician's keys; FREE: always on | COLOR, TONE, LEVEL | MODE, COLOR, GATE, TONE, LEVEL, ATTACK, RELEASE, MOVEMENT, WIDTH, FILTER FOLLOW |
| KONTAKT | A second `ExternalInstrument`, one per ANALOG instance | The musician's keys **before the arpeggiator**, every channel message normalised to channel 1 | LEVEL | MODE, LOAD KONTAKT / OPEN / UNLOAD, LEVEL, FILTER FOLLOW |

RHYTHM's loop is placed on the host's grid while the transport runs. With no transport it runs on its own clock. Its slices keep their pitch: RATE changes speed against the tempo, never pitch. Its envelope (the loop's level above SENSITIVITY, through ATTACK / RELEASE) modulates a target only through MOD AMOUNT, which defaults to 0.

## Interface

- **Main menu:** "Show AUX" / "Hide AUX" (shell opt-in, `ProductInfo::auxStrip`). Hidden by default.
- **Strip** (76 px, 18 px under the cards; everything under the cards moves down 94 px and the window grows by as much):
  - D badge (dusty plum) and AUX, then power and M / S;
  - a graphite well with the mode dropdown (card C's TYPE dropdown's look), a live display (the loop with its grid, playhead and envelope; the noise colour and its gate light; Kontakt's state) and LOAD / RACK / OPEN;
  - the mode's knobs, then ROUTE and `...`.
- **`...`:** Hide AUX, Open / Close AUX Rack, Reset (D's parameters to their defaults; the recording and Kontakt stay loaded).
- **Double-click** the strip: opens or closes the AUX rack. It is a product rack (`rack::Kind::aux`, added to the shared enum) with a plum enamel faceplate, like FILTER and MORPH.
- **Escape** closes one level at a time: a popup, then the well editor, then the AUX rack. It never hides the strip.
- **View, never sound:**
  - Showing or hiding never mutes D or unloads anything (tested bit-identical).
  - Visibility is saved in the session's `<View auxShown>`. Loading a preset (and its undo) keeps the current value.

## State

- **Parameters:** `analog.aux.*` (21), version hint 37, appended. The state version is 23 (22 before). There is nothing to migrate: absent means off.
- **Engine children** (only once there is one):
  - `<AuxRecording asset= name=>`: the stored file's name in the sample store;
  - `<AuxKontakt><ExternalInstrument …/></AuxKontakt>`: Kontakt's identity and native state, written back exactly as read if Kontakt is missing.
- **Failure handling:**
  - A missing stored file → MISSING, written back as it was.
  - An unreadable file → ERROR, with D silent and nothing else affected.

## Latency

- D KONTAKT's reported latency delays everything before it in series: the voices and C EXTERNAL's bus.
- The engine reports C's + D's latency, and D's only while D is on in KONTAKT with Kontakt loaded.
- D off, or no Kontakt: no latency added (tested).

## Verification (this container, Linux, headless)

| Check | How | Result |
|---|---|---|
| D off is bit-exact | DSP `[aux][g1]` legacy hash; product's 1.0 fixtures (`v6-baseline-renders`) | PASS |
| POWER / MUTE / LEVEL ramps, no clicks | `[aux][g1]` | PASS |
| FILTER on D in every mode (TONE, RESONANCE, LADDER / OTA, FOLLOW) | `[aux][g2]` | PASS |
| SPACE / ECHO / DRIVE carry D | `[aux][g2]` | PASS |
| RHYTHM grid, envelope, mute, power-off parity | `[aux][g3]` | PASS |
| NOISE gate, colours, determinism | `[aux][g4]` | PASS |
| KONTAKT bus and latency (surrogate buffers) | `[aux][g5]` | PASS |
| No allocation on the audio thread | `realtime: D / AUX's bus allocates nothing` | PASS |
| Every D parameter moves the sound | the bindings test | PASS |
| Strip: show / hide, places, window, no sound change, menu, rack, Escape, Reset, session vs preset | `AuxProductTests.cpp` `[aux][ui]` | PASS |
| Recording: async load, store, session round trip, missing, unreadable | `AuxProductTests.cpp` `[aux]` | PASS |
| ANALOG unchanged with D hidden | `andor_analog_tests_visual` (pixels outside the approved regions) | PASS |
| OSP unchanged | shell `[aux]` opt-in test (`andor_example_tests`); `osp_visual_reference`, `andor_example_tests_visual`; OSP's plugin suite (162 cases) | PASS |
| Suites | ANALOG DSP 289 cases; ANALOG product 176 cases (2 skipped: the Bukkehorn recordings are not in the repository); ANDOR Example 10 cases | PASS |

### NOT VERIFIED (owner tests)

- **UNVERIFIED: real Kontakt as D.** D KONTAKT is the surrogate-validated `ExternalInstrument`, a second instance. Loading Kontakt, its editor, its state recall, and two Kontakt instances (C and D) side by side are **OWNER TEST** on a Mac with Kontakt.
- **UNVERIFIED: DAWs.**
  - Host sync of D RHYTHM against a real transport (loop and locate);
  - latency compensation with D KONTAKT;
  - automation of `analog.aux.*`.
  
  These are **OWNER TEST** in Logic / Ableton / Reaper.
- **UNVERIFIED: drag and drop** of a file onto the strip from Finder. It is implemented (`FileDragAndDropTarget`), but there is no desktop here.

## Evidence

- **Screenshots:** `docs/aux/screenshots/`, from the hidden test "AUX: screenshots"; ceramic skin.
  - The window at 869 / 1448 / 1810 px wide: hidden; RHYTHM, NOISE and KONTAKT;
  - the rack in each mode;
  - D off.
- **Renders:** `docs/demos/aux/`, with `renders.txt`:
  - the FILTER sweep on D NOISE: +20 dB from closed to open;
  - D RHYTHM through SPACE and ECHO;
  - A/B/C + D together;
  - the D-off null: max difference 0.

## Deferred (documented, not built)

- **MAIN + SEND send for D** (gate 7, optional): D joins the main path only. A D SEND amount needs the MAIN + SEND graph to take a fourth source bus. Plan: a `sendD` parameter and a tap of `mixAux`'s output into the send bus, as the cards' sends.
- **DIRECT** (bypassing the shared effects): not built. ROUTE's menu says so.
- **D as a modulation destination** in the modulation matrix: not added, to keep `modulation-destinations.txt` unchanged. D's float parameters can be assigned to USER macros and automated like any other.
- **MORPH with D:** see `MORPH_DECISION.md`. Unchanged until the owner chooses.
