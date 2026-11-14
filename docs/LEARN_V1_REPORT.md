# ANDOR Learn V1: report

The feature is described in [LEARN_V1.md](LEARN_V1.md), the baseline and boundary map in [LEARN_V1_GATE0.md](LEARN_V1_GATE0.md), and the authoring guide in `external/osp/shared/andor-learn/README.md`.

## Branches

| Repository | Branch | Base |
|---|---|---|
| `arenttodal/analog` | `feature/andor-learn-v1` | `main` `4bc6c41` (1.0.0) |
| `arenttodal/osp` | `feature/andor-learn-v1` | `adf8ed6` (Analog 1.0's pin) |

Nothing was merged. `main`, `claude/release-candidate` and the V6 branch are untouched.

## What was built, by gate

| Gate | Result |
|---|---|
| 0 | Branches and pins verified; boundary map; baseline 7/7 green on `main`; 724 baseline screenshots captured |
| 1 | `shared/andor-learn` core: data, catalog checks, runner, progress store, action IDs. On the JUCE side: target registry, touch bridge, controller. `ProductInfo::learn` is opt-in; the ARP UI's `onLearnAction` and `MiniKnob::onUserChange` are null by default. |
| 2 | Overlay: a paint-only ring and the ceramic note. The full 8-step ARP lesson, with Back, Show me, Skip, Continue and Close. Automation, presets and programmatic changes are proven not to complete steps. |
| 3 | Learn mode: menu items, region-specific hit areas (only while on), hover outline, click-to-explain, Start lesson / Open controls / Close, Escape order. 18 Analog help regions. |
| 4 | Topic browser with statuses and resume; the one-time invitation (Quick Start / Browse lessons / Not now); per-user JSON progress; *Reset learning progress*; MORPH and FX Routing lessons; a 4-step Quick Start. |
| 5 | Tests, screenshots, docs; this report |

## Change map

**OSP** (`external/osp`):

| File | Change |
|---|---|
| `shared/andor-learn/**` (new) | Core: `LessonTypes.h`, `LessonCatalog.*`, `LessonRunner.*`, `ProgressStore.*`, `ActionEvents.h`. JUCE: `LearnTargetRegistry.*`, `LearnActionBridge.*`, `LearnOverlay.*`, `LearnBrowser.*`, `LearnController.*`. Also `AndorLearn.cmake` and `README.md`. |
| `shared/andor-shell/ShellProduct.h` | `LearnDescriptor`, `std::optional<LearnDescriptor> learn` |
| `shared/andor-shell/ShellProcessor.h` | `learnEnabled()` (never in the parity render) |
| `shared/andor-shell/ShellEditor.*` | `createLearn()` (the shell's targets and states); menu items; Escape first; `tick()` in the existing timer; user events from the ARP card, the source-view key and the rack-name actions; read-only accessors |
| `shared/andor-shell/AndorInstrument.cmake` | Learn sources and includes; `LearnTests.cpp` in every product's test program |
| `shared/andor-ui/ArpSection.*` | `onLearnAction` on `ArpControl` / `ArpInlinePanel`, called from the existing click handlers; `isLinked()`, `shownRateKnob()` |
| `shared/andor-ui/ShapingPopups.*` | `MiniKnob::onUserChange` (choice-ordered knobs) |
| `tests/shell/LearnTests.cpp` (new) | Core, runner, store and opt-in tests, with a test-only sample catalog |
| `docs/STATUS.md` | Learn entry; NEXT: Learn in ANDOR OSP's own plugin |

**Analog**:

| File | Change |
|---|---|
| `product/Source/AnalogLearn.*` (new) | Catalog (4 lessons, 18 help regions), product targets (MORPH name / knob, routing board / mixer / chain), states, capabilities |
| `product/Source/Product.cpp` | `p.learn = learnDescriptor();` |
| `product/CMakeLists.txt` | The new source and test file |
| `product/tests/AnalogLearnTests.cpp` (new) | Integration tests and the `[screenshots-learn]` set |
| `docs/LEARN_V1*.md` | Docs |

**Not changed:**

- plugin IDs (`Aanl`, `com.osp.andoranalog`);
- the APVTS schema (544 product parameters; the golden is unchanged);
- `stateVersion` (13);
- any DSP, MIDI or audio code;
- presets, `Interface.xml`, window, card and rack dimensions.

## Tests (Linux, GCC 13, this container)

See "Test results" below for the exact runs and counts.

**New:**

| Program | Cases | Assertions |
|---|---|---|
| `andor_analog_tests "[learn]"` | 14 (7 shared + 7 Analog) | 390 |
| `andor_example_tests "[learn]"` | 7 | 97 |

What they cover:

- **Catalog checks:** duplicates, unknown targets, states and parameters, empty actions, broken help, quick start.
- **Capability filtering.**
- **The runner:**
  - only user origins complete steps, and only with an accepted action of the current generation and a postcondition that holds;
  - automation, preset, tutorial and unknown origins are rejected;
  - a stale generation is rejected, and one event cannot complete two steps;
  - the baseline follows unattributed changes and holds while a gesture is under way;
  - skip, back, continue, pause, Show me, close; finished-with-skips is not complete.
- **The progress store:** per product; temp file + replace; unknown fields kept; corrupt file kept as `.corrupt` and replaced; reset; one invitation per product in a process.
- **Opt-in:** the example product has no controller and writes no file. In Analog, the catalog is valid against the editor's targets and Learn is idle and adds no component.
- **Analog's ARP lesson end to end** with the real controls' handlers. At each step, automation (`setValueNotifyingHost`), a preset recall (`setStateInformation`) and programmatic calls (`choosePattern`) complete nothing. Step 7 completes only on B's own parameter, and the global pattern stays unchanged. Progress is saved; the sound stays as set; no Learn component remains.
- **Navigation never changes the sound.** Every lesson driven through Show me, Back, Next, Skip and Close leaves every parameter unchanged and adds none.
- **MORPH:** the rack opened by a double-click; automation followed by an idle touch completes nothing; a press, drag and release completes. The "nothing to play" variant shows.
- **FX Routing:** quick settings opened by code complete nothing; a touch completes; Close saves in-progress at `tile`.
- **Learn mode:** every expected region; none larger than a third of the window; hover outline; help with and without a lesson; routing help; Escape order. Afterwards there is no Learn component and the snapshot is pixel-identical to before.
- **Browser and invitation:** four rows, resume at the step left, reset; the invitation once per process; not offered again once answered.
- **The overlay follows its target** at 869, 1448 and 1810 px: the ring equals the real control's bounds, the note never covers it and stays inside, and a click at the target reaches the real panel.

**Learn OFF is visually unchanged.** The 724 screenshots of five existing sets, rendered before Learn existed (`main`) and after, are pixel-identical (724 / 724).

**Learn OFF is sonically unchanged.**

- No DSP, MIDI or audio file changed.
- `analog_dsp_tests` and the golden renders in `andor_analog_tests` pass.
- Learn has no parameter and nothing on the audio thread.

## Test results (Linux, this container)

### Baseline on `main`, before any Learn change

```
ctest -R "analog_dsp_tests|andor_analog_tests|andor_example_tests|osp_visual_reference|andor_bootstrap_tool"
```

7 / 7 passed:

- `analog_dsp_tests`, `andor_analog_tests` and `andor_analog_tests_visual`;
- `andor_example_tests` and `andor_example_tests_visual`;
- `osp_visual_reference` and `andor_bootstrap_tool`.

### With Learn, the full suite (`ctest -j3`, 13 programs, every OSP suite included)

12 / 13 passed in the parallel run:

- `plugin`, `unit`, `integration`, `regression` and `performance-smoke`;
- `memo_lab`;
- `osp_visual_reference`;
- both visual regressions (0 px);
- `andor_example_tests` and `andor_bootstrap_tool`;
- `analog_dsp_tests`.

The thirteenth, `andor_analog_tests`, failed two preset-count checks in that parallel run:

- `ShellTests.cpp:482`, `AnalogProductTests.cpp:513`: 46 presets listed where 47 were expected.
- Both count files in the product's shared Documents preset folder. `andor_analog_tests_visual` (the same product) uses that folder while running at the same time.
- Learn does not touch presets or that folder.
- Rerun alone on the final code, `andor_analog_tests` passed (all cases, both checks included).
- The race is a test-isolation issue of the parallel run. It was not reproduced against the pre-Learn binary in parallel, so that comparison was not run.

### Learn's own tests (final code)

| Program | Cases | Assertions | Result |
|---|---|---|---|
| `andor_analog_tests "[learn]"` | 14 | 390 | all passed |
| `andor_example_tests "[learn]"` | 7 | 97 | all passed |

## Screenshots

`[screenshots-learn]` (run with `ANALOG_SCREENSHOT_DIR`) writes 24 states at 1448 and 869 px. They are copied to `docs/screenshots/learn/`:

| # | State |
|---|---|
| 00 / 23 | off, before and after use |
| 01 | invitation |
| 02 | browser |
| 03–12 | the ARP lesson's steps, including 04 and 11 (done) and 06 (pattern) / 10 (independent card) |
| 13 | complete |
| 14–17 | Learn mode, hover on a macro, MORPH help, routing help |
| 18–19 | FX lesson |
| 20–22 | MORPH lesson |

## Manual host tests (not run here: no DAW, display or Mac in this container)

These have not been run and are not reported as passed:

1. Standalone, Ableton Live (AU / VST3), Logic (AU), REAPER (VST3): the steps in [LEARN_V1.md](LEARN_V1.md), "Testing it in a DAW".
2. **Real input:**
   - mouse, trackpad and keyboard on every ARP step;
   - Tab, Return and Space on the note's keys;
   - focus does not steal the DAW's typing keyboard.
3. **Automation:** with automation reading and writing `arp.pattern`, `arp.rate`, `analog.arp.cardB.*` and MORPH during each step, no false completion.
4. **Playing through it:** open and close Learn, Learn mode and the browser mid-playback. MIDI, CCs and audio continue; CPU is unchanged (no timer of its own; idle `tick()` returns at once).
5. **Display:** Retina and Windows scaling; ARP panel and racks open; routing view; window at its smallest.
6. **The invitation in a real host:** it appears once, after the editor is visible; a second instance does not duplicate it.
7. **Screen reader:** the note's title and description, the keys' titles, and Learn mode regions.
8. pluginval / auval on the Learn build (CI runs them on push).

## Blockers and limits

- **Keyboard completion of the ARP lesson.** ARP's list, chips and tabs have no keyboard handling of their own (as in 1.0). RATE is reachable through accessibility. The lesson's own keys are keyboard operable. Adding keyboard handling to the ARP panel would change the ARP UI, so it was left out (product choice).
- **Event origin.** Events from click handlers are reported as `userPointer` (keyboard activations of the same handlers too); RATE's slider changes likewise. All are user origins, so completion is unaffected.
- **Touch events (MORPH knob, routing board)** are mouse-listener events. A host automating MORPH during the musician's own drag could add to the change measured over that drag; outside a gesture, automation never counts.
- **The invitation's timing** depends on `isShowing()`. Headless tests drive `showInvitation()` directly.
- **The notes' type scales with the window** (as every popover does): at 869 px it is small.

## Product choices to confirm

1. **Base branch:** Learn sits on 1.0 (`main`), not on V6. It merges into either (V6's arp companion has no lesson).
2. **Branch names:** `feature/andor-learn-v1` in both repositories, as the spec names it.
3. **Quick Start** is implemented as four short explanations (the invitation's first choice).
4. **Learn mode at startup:** the last choice is recorded but Learn mode is not restored when the editor opens.
5. **Learn sources are compiled into every shell product** (inert without a catalog) rather than conditionally: no `#if` in the shell.
6. **Lesson copy** was written for V1 and should be reviewed by a musician.
7. **Step 7 selects EDIT B for the musician** (a view setting) rather than asking them to find it.
