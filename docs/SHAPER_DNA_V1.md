# SHAPER DNA V1: report

SHAPER DNA gives MOVEMENT lane A's SHAPER a rhythm per note.

- It has up to three profiles (A / B / C), each with its own pattern and rate.
- Each new note is assigned one profile, by CYCLE (1, 2 or 3 notes per profile) or by KEY ZONES (two splits).
- A note keeps its profile until it ends.
- DNA is OFF by default. With DNA OFF, every patch and session sounds exactly as before.

Baseline, branches and the code map: [SHAPER_DNA_V1_GATE0.md](SHAPER_DNA_V1_GATE0.md).

## Commits (`feature/shaper-dna-v1`)

| Gate | analog | OSP (`feature/shaper-dna-v1`) |
|---|---|---|
| 0: baseline | `c1a3848` | |
| 1: pure model | `4158dde` | |
| 2: per-note audio and hand-over | `57c43c1` | `d0c7075` (the opt-in SHAPER hand-over) |
| 3: parameters and state | `b12870e` | |
| 4: rack key and panel | `df23bca` | `b8c24c0` (the rack key, the anchored popover) |
| C1/C2 tab relocation (GUI only, separate) | `e52d186` | |
| 5: validation, CPU, demo, report | this commit | |

Nothing is merged. `main` and `claude/release-candidate` (1.0.0) are untouched.

## How it works

```
note-on -> profile (CYCLE / KEY ZONES), latched for the note's life
voice: source -> voice filter -> AMP -> SHAPER DNA (its profile; VOL / LP12) -> pan -> sum
mix -> ... -> MOVEMENT lane A: its SHAPER handed over (share 0), lane B and every other mode as ever
```

**One clock.**
- While the transport runs, the clock is the host's PPQ plus the sample offset in the block.
- When it is stopped, a local clock starts with the first note and runs while anything sounds. After 1.5 s of silence it waits again.
- New notes never reset it. All profiles use the same clock and PHASE, so they stay locked to one grid.

**Cost.**
- Each profile's curve is computed once per control chunk, at the host rate, and every note reads it.
- Per voice, the cost is a 2 ms gain ramp, plus a mono LP12 for FILTER / BOTH (each voice has its own state).
- Nothing allocates.

**The hand-over** (40 ms, both ways).
- Lane A's SHAPER and the notes share one gate geometrically: the SHAPER's gain is gain^(1-h) and each note's is gain^h, while the filter's octaves split h : 1-h.
- So there is never a doubled gate, never a full opening, and never silence.
- FILTER is a little brighter (two half-closed LP12s) for those 40 ms.

**When DNA plays.** DNA must be ON, lane A must be in SHAPER mode and switched on, and MOVEMENT must be heard on MAIN (CLASSIC, or not bypassed on a route's MAIN). Otherwise DNA is suspended, its settings are kept, and the rack key's tooltip says why.

## Parameters (append-only, version hint 33) and state

| ID | Type | Default |
|---|---|---|
| `analog.movement.shaperDna.enabled` | bool | OFF |
| `analog.movement.shaperDna.assignment` | CYCLE, KEY ZONES | CYCLE |
| `analog.movement.shaperDna.profiles` | A / B, A / B / C | A / B / C |
| `analog.movement.shaperDna.notesPerProfile` | 1, 2, 3 | 1 |
| `analog.movement.shaperDna.splitAB` | int 0..126 | 59 (B3) |
| `analog.movement.shaperDna.splitBC` | int 1..127 | 71 (B4) |
| `analog.movement.shaperDna.{a,b,c}.pattern` | FOLLOW + the 12 patterns | FOLLOW |
| `analog.movement.shaperDna.{a,b,c}.rate` | FOLLOW, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | A FOLLOW, B 1/8, C 1/8T |

- **State version:** 17 to 18. Nothing to migrate: a missing parameter rests at its default (OFF).
- **Schema golden:** the 12 lines are appended (579 to 591). ANDOR OSP's schema is unchanged.
- **Untouched:** `movement.*`, `motion`, the lane IDs, presets, the FX-routing schema and the plugin IDs.

## Interface

**Rack.** A `DNA` key sits beside the SHAPER display's magnifier, at the same height (48 × 20).
- It shows only for lane A in SHAPER mode. It is hidden in other modes and on lane B, and its setting is kept.
- Its light switches DNA (undoable).
- Its name opens the panel; the key again closes it.
- The rack's size and existing controls are unchanged.

**Panel.** A MiniPanel in the SUB / SPACE language, 372 px wide.
- The header has an ON light.
- The display shows the three profiles' curves, drawn from the real settings.
- Under the curves:
  - in KEY ZONES, a keyboard with two split handles; each drag is one gesture, never crossing;
  - in CYCLE, the profile sequence.
- Controls: CYCLE | KEY ZONES; A / B | A / B / C; NOTES 1 | 2 | 3 (CYCLE only); then PATTERN and RATE for A, B and C.
- TARGET, SMOOTH and DEPTH stay on the rack.
- It opens anchored under the key, inside the window. A transparent backdrop takes the click that closes it, so that click reaches no control. Escape also closes it.

**C1/C2.** Card C's layer keys now sit under its TYPE dropdown, right-aligned to it. The sample's tools keep the left. This is a separate GUI-only commit; the card's size and the keys' behaviour are unchanged.

## Tests (Linux, GCC 13, this container)

### analog_dsp_tests `[shaperdna]`: 14 cases (1547 assertions)

| Spec | Test |
|---|---|
| DNA-01 | CYCLE 1 / 2 / 3; A/B and A/B/C; chord order; reset |
| DNA-02 | A held C5 keeps C while the chord changes, through sustain, release and settings moves; a replayed note is a new assignment |
| DNA-03 | KEY ZONES: edges 0 / 59 / 60 / 71 / 72 / 127; two zones; ordered splits |
| DNA-04 | Tempo lock (triplets included); every rate meets the 16-quarter grid; PHASE; same-profile notes started apart gate together |
| DNA-05 | Block sizes 64 / 128 / 512 / 2048 with the host transport at 117 BPM from PPQ 3.37 and odd MIDI offsets: < 1e-6 apart |
| DNA-06 | Depth 0 exactly transparent; VOL and BOTH follow the SHAPER |
| DNA-07 / OLD-01 | Bit-exact OFF with PULSE, DRIFT, lane A off, MOVEMENT bypassed, MOVEMENT on SEND only, depth 0 |
| DNA-08 | SUB keeps its level, equal to SUB without SHAPER (< 0.5 %) |
| DNA-09 | Card-channel notes and arpeggio steps each count, in order; unison's voices of one note share its profile |
| OLD-04 | Hand-over on and off mid-note (VOL, BOTH): the level stays between the two pure renders |
| — | FOLLOW = the SHAPER; per-profile rates (1/16 vs 1/8T, and really different) |
| RT-01 | Zero allocations: 16 voices, hand-overs, profile changes, transport start / stop, FILTER / BOTH |

The full `analog_dsp_tests` run passes 228 cases, with every golden render unchanged.

### andor_analog_tests: `[shaperdna]` (5 cases) and GUI-04

- OLD-02: every real 1.0 project opens with DNA OFF, even over a stale ON.
- GUI-03: saved, recalled, read by the engine, undoable.
- Through the plugin: ON hands over and changes the groove; OFF hands back.
- The rack key: visibility, size, light (undoable), the panel anchored inside, the key / Escape closing it.
- The panel at 1448 and 869 px: CYCLE sequences, A / B, KEY ZONES drags (undoable, never crossing), bindings.
- GUI-04: C1/C2 under the dropdown at 869, 1448 and 1810 px, four types, one and two layers.

The full-suite results are recorded in the final commit's message.

## CPU (CPU-01: 16 held voices, three profiles at FOLLOW / 1/8T / 1/32, this VM, three runs)

| | 1x | 4x |
|---|---|---|
| DNA off (the global SHAPER) | 9–13 % | 28–31 % |
| DNA VOL | 9–13 % | 28–35 % |
| DNA FILTER | 12–13 % | 32–37 % |
| DNA BOTH | 13–15 % | 29–38 % |

The figures are "% of one core". The VM's run-to-run noise is about ±3 points.

- VOL costs about the same as the global SHAPER.
- FILTER / BOTH add a per-voice LP12, a few points at 16 voices.

The first version evaluated the curves at the internal rate, which cost +10–13 points at 4x even on VOL. Moving the curves to the host rate (as the global SHAPER reads them) fixed it.

## Demo

`docs/shaper-dna/` holds "Held Above", the spec's example 2.

| File | What |
|---|---|
| `SHAPER DNA - Held Above.andoranalog` | The preset |
| `SHAPER DNA - Held Above.mid` | 120 BPM, 4 bars: C5 held over C major, then F major |
| `SHAPER DNA - Held Above.wav` | The render with DNA on |
| `SHAPER DNA - Held Above (DNA off).wav` | The same with DNA off |

The patch is KEY ZONES:
- A, the chords below B3: THREE at 1/16;
- B, C4–B4: FIVE at 1/8;
- C, the held C5: BREATH at 1/8T, triplets against the sixteenths.

The files are reproduced by `ANALOG_DEMO_DIR=... andor_analog_tests "[shaperdna-demo]"`.

## Route locations and trade-offs

**Supported routes.** DNA plays in CLASSIC and on any route with MOVEMENT (lane A's bus) on MAIN, not bypassed. This includes one-path CUSTOM routes, MAIN + SEND's MAIN, and MOVEMENT inside a parallel group or splitter.

**Suspended routes.** DNA is suspended, with its setting kept, when:
- MOVEMENT is bypassed;
- MOVEMENT is absent from MAIN;
- MOVEMENT is placed on SEND;
- lane A is off.

**Before the sum, by design.** In a CUSTOM route, DNA acts before the voice sum wherever the MOVEMENT tile sits. Inserts placed before the MOVEMENT tile therefore hear the DNA-shaped voices, not the dry mix.

**Sends (MAIN + SEND).** Each voice's send takes that voice's DNA, so the room follows each note's rhythm. This follows the owner's 11 November rule that everything goes through FILTER and MOVEMENT; the spec's "pre-filter taps" text predates it.

**SUB.** SUB is never DNA'd. While DNA plays, the handed-over SHAPER no longer gates SUB; PULSE and lane B still move it.

**Not bit-identical to the global SHAPER.** DNA ON shapes each voice before the mix, so it is close to the global SHAPER but not the same. FOLLOW stays within 8 % of the SHAPER's level envelope. During the 40 ms FILTER hand-over the sound is a little brighter.

**Latched profiles.** Profiles are latched per note. Changing assignment settings affects new notes only; there is no live reassignment in V1.

**Not in V1.** STACK / chord-position mapping, more than three profiles, per-profile CUSTOM grids, and the concept image's FX multiplier column. The rack's optional three-curve preview is not drawn either: the rack is unchanged with DNA off or on, and the curves live in the panel.

## Not run here (no DAW, display or Mac in this container)

These are not reported as passed:

- **Hosts:** Ableton Live, Logic, REAPER and the Standalone:
  - automation of every DNA parameter while notes play;
  - undo in the host;
  - transport loops and tempo changes against host PPQ;
  - saved projects reopened.
- **Real input:**
  - mouse and trackpad on the key and the panel;
  - Retina and Windows scaling;
  - the smallest window with long localised names.
- **Validators:** pluginval / auval on this branch (CI runs them on push).
- **A formal listening test.**
