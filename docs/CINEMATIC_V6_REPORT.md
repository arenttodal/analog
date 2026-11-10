# Cinematic V6 — final report

Branch `claude/upbeat-allen-3i0jey`, from `main` at 4bc6c41 (post-1.0); the release candidate
(`claude/release-candidate`) is untouched. OSP is pinned at `a0339b0` (`claude/space-presets`).
Each gate is documented on its own page: [Gate 0](CINEMATIC_V6_GATE0.md), [1](CINEMATIC_V6_GATE1.md),
[2](CINEMATIC_V6_GATE2.md), [3](CINEMATIC_V6_GATE3.md), [4](CINEMATIC_V6_GATE4.md).

## What was built, and the owner's decisions

| Gate | What | Owner's decision that shaped it |
|---|---|---|
| 0 | The 1.0 baseline frozen: 92 renders through the whole instrument (`[v6-baseline]`) | — |
| 1 | Each oscillator card's own REIMAGINED (card B on OSP's `layerC.reimagined*`); B's VELO in its well | No pair-wide compatibility path. Five REIMAGINED factory sounds change by design. |
| 2 | SUB: an 8-voice sub oscillator on the played keys (before the arp), dry after the effects; `arp` / `sub` housing; SUB popover with a RANGE keybed | — |
| 3 | C2: a second layer inside card C (OSC, NOISE, or its own recording / instrument patch) | C2 is "just another layer in C": card C's M/S, modifiers, EQ/DAMPEN, arp lane, MORPH and routing are shared. No fourth lane, separate send or MORPH target. Its SAMPLE is a simple zone player. |
| 4 | MAIN + SEND: an opt-in route of its own. MAIN is the dry inserts; SEND is fed by each card's send (taken before the filter) and is strictly wet. SUB goes on MAIN, never SEND. MAIN \| SEND \| OVERVIEW tabs. | Card C's send carries both its layers (follows gate 3). |

## New parameters (all appended; IDs, order and ranges frozen)

| State version | IDs | Hint |
|---|---|---|
| 14 | none: card B uses OSP's existing `layerC.reimagined*` | — |
| 15 | `analog.sub.*` (10) | v30 |
| 16 | `analog.hybrid2.*` (22) | v31 |
| 17 | `analog.send.a`, `.b`, `.c` | v32 |

All of them are listed in [PARAMETER_REGISTRY.md](PARAMETER_REGISTRY.md).
`product/golden/parameter-schema.txt` grew from 544 to 579 entries, by appending only.

## Migration semantics

- **Older states (≤ 13).** Card A's REIMAGINED amount and mode settings are copied to card B
  (`migrateFromVersion13`), and the factory sounds get the same copy.
- **Versions 15–17 migrate nothing.** SUB, C2 and the sends default to off or 0, so every
  older session plays as before.
- **C2's own source** is the engine's `Hybrid2Source` state child: a `SampleSource` definition
  whose recordings are stored by content in the instrument's sample store. C1's `layerB.*`
  and its sources are never reinterpreted.
- **FX routes:**
  - CLASSIC (no child) and CUSTOM (`schema="1"`) load and save exactly as in 1.0 and are
    never upgraded.
  - MAIN + SEND exists only by the musician's explicit choice (gear: Use MAIN + SEND…,
    undoable) and is saved as `schema="2" mode="mainSend" main="…" send="…"`.
  - Back to one path is the reverse step.

## Golden audio comparison

| Golden | Result |
|---|---|
| `[v6-baseline]` (44 factory sounds + 2 routes, 44.1 and 48 kHz) | Bit-identical on Linux for every sound except the five REIMAGINED ones re-recorded in gate 1 (Grain Cloud's hash only; Kaleidoscope Pad, Tape Frame Keys, Toybox Chime, Mirage Bass). Frozen Glass is unchanged. |
| `tests/golden/legacy-renders.txt`, `fx-baseline-renders.txt` | The same five sounds and the KALEIDOSCOPE edge patch (gate 1). Unchanged by gates 2–4. |
| Gates 2–4 with their features off | Bit-identical (SUB-05, C2-03, LEGACY: the sends change nothing in CLASSIC or CUSTOM). |

## UI screenshots (1448 and 869 px)

| Spec # | What | File |
|---|---|---|
| 1 | Old Init | `screenshots/v6-gate4/g4-1-classic-*`; the card in `screenshots/v6-gate3/g3-1-one-layer-*` |
| 2 | Independent A/B REIMAGINED | `screenshots/v6-gate1/g1-1-cards-*`, `g1-2-b-popover-*` |
| 3 | C1 only | `screenshots/v6-gate3/g3-1-one-layer-*`, `g3-2-one-layer-hover-*` |
| 4 | C1 / C2 | `screenshots/v6-gate3/g3-3…g3-6-*` |
| 5 | arp / sub off | `screenshots/v6-gate2/g2-1-housing-off-*` |
| 6 | arp on + sub on | `screenshots/v6-gate2/g2-2-housing-on-*` |
| 7 | SUB popup and its range | `screenshots/v6-gate2/g2-3-sub-popover-*`, `g2-4-sub-popover-flat-*` (range C2–C5) |
| 8–10 | MAIN, SEND, OVERVIEW | `screenshots/v6-gate4/g4-2-main-*`, `g4-3-send-*`, `g4-4-overview-*` |
| 11 | Full FX rack | unchanged since V5 (`screenshots/` V5 sets); the routing board in `g4-2-main-*` |

## Tests

- **Local runs (Linux):** every suite passes.
  - `analog_dsp_tests`: 205 cases.
  - `andor_analog_tests`: 108 cases.
  - `andor_analog_tests_visual`, `andor_example_tests` and `osp_visual_reference` (OSP's
    housing and parity at 0 px), and `andor_bootstrap_tool`.
- **New tags:**
  - engine: `[reimagined][v6]`, `[sub][v6]`, `[hybrid2][v6]`, `[mainsend][v6]`, plus the
    realtime cases (no allocation in each new path);
  - plugin: `[v6][reimagined]`, `[v6][sub]`, `[v6][hybrid2]`, `[v6][mainsend]`;
  - the bindings test covers every new parameter (each moves the sound in its context).
- **CI (Linux, macOS with pluginval at strictness 10, Windows):** it had been red since gate 0
  on two test-side issues, fixed in `ee0e5ab`:
  - the baseline demanded exact hashes recorded on Linux only;
  - test Synths on the stack overflowed Windows' 1 MB stack.

  From `ee0e5ab`, all three jobs are green, including pluginval (VST3 and AU), the AU
  validation, and the macOS and Windows installers.
- **OSP's own suites** (unit, integration, regression, plugin, visual reference, example
  product) pass with the opt-in arp housing: 10 / 10.
- **An independent review** of the engine and routing found nine issues, all fixed with tests
  in `338a74b`:
  1. On SEND, an insert group placed before SPACE silenced it.
  2. With one path, SUB arrived early by a latent CUSTOM route's latency.
  3. A note right after ALL SOUND OFF was dropped.
  4. An FX chain that did not fit its tab moved settings before being refused.
  5. Loading the same recording into C2 again left it "loading".
  6. The sends switched in a single step and kept stale decimator history.
  7. With FOLLOW off, a silent layer held a note.
  8. A SEND with no spatial processor was counted in the reported latency.
  9. SUB reached CUSTOM's effects during the duck into MAIN + SEND.

  It found no real-time safety issue. SUB also follows the on-screen pitch wheel now
  (`872aa6b`).

## DAW work that needs a person

- Archive Logic, Ableton and Reaper projects saved with the 1.0 installers, then reopen them
  with this build. The default sound must be unchanged.
- Save and reopen each format (AU, VST3, standalone) with:
  - SUB on, with its range and wave set;
  - C2 holding a recording or an instrument patch;
  - a MAIN + SEND route with sends;
  - each card's REIMAGINED.
- Automate the new parameters in a host; check undo after Use MAIN + SEND… and Back to one
  path.
- Listen:
  - cinematic chords, with SUB holding the chord under an arpeggio;
  - a heavily wet C2 against a dry A/B (MAIN + SEND, C's send up);
  - SUB staying dry with full SPACE / ECHO.
- Windows host check, plugin scanners, and accessibility with a screen reader. The new keys
  have titles and tooltips.
- Signed and notarised installers. CI builds unsigned ones.

## Remaining limits

- C2 has no OSP continuation or granular (C1 keeps them); its SAMPLE cannot play REVERSE.
- A file dropped on card C loads C1. C2's recording comes from its ADD SAMPLE / Replace.
- A Kontakt import lands in C1. Its sounds are then offered in C2's SOUNDS.
- MAIN + SEND has no per-path latency report beyond the route's maximum, which the host
  receives. FX CHAIN presets save the chain shown (MAIN's or SEND's).
- The spec's "beta pair-wide REIMAGINED states" path was dropped by the owner's decision
  (gate 1).

## Per-gate release readiness

| Gate | Ready to release? |
|---|---|
| 0 | Done: baseline only. |
| 1 | Ready after DAW reopen checks. Five factory sounds change by design. |
| 2 | Ready after a listening and DAW pass. Musically valuable on its own (the spec's "minor update"). |
| 3 | Ready after a listening pass with real recordings and a DAW save/reopen of C2's sources. |
| 4 | Feature-complete and tested headless. Needs the most listening (send levels, mix laws) and DAW checks before release. |
| 5 | Release notes (`RELEASE_NOTES_V6.md`) and this report are written. Signing, notarising and the DAW matrix are human steps. |
