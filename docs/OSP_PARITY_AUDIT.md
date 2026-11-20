# ANDOR ANALOG ↔ ANDOR OSP — parity audit (R1)

_20 Nov 2026. Integration branch `claude/osp-parity-r1` (ANALOG) on OSP `claude/analog-parity-r1`.
This is the Gate 0 audit; the results of the later gates are in "Integration results" at the end._

## 1. Baseline (Gate 0)

### Revisions found (both remotes re-read on 20 Nov 2026)

The spec's reference points were stale. The current lines:

| Repository / line | Commit | OSP pin | Note |
|---|---|---|---|
| ANALOG `feature/aux-live-input` | `e3a8090` | `b522a06` | **newest ANALOG line** (PR #12 merged into it); the baseline |
| ANALOG `claude/analog-shared-fx-tests` | `49e858a` | `aef5e0d` | e3a8090 + tests on OSP's current line (another session, 19 Nov) |
| ANALOG `claude/analog-d2-macros` | `f5f9cb1` | `7fb5dfa` | e3a8090 + the D2 macro-knob experiment (not approved in OSP) |
| ANALOG `claude/upbeat-allen-3i0jey` (GitHub default) | `1c0cb6d` | `e7a3834` | V6 line, older than all of the above |
| ANALOG `main` / `claude/release-candidate` | `4bc6c41` / `2fc9856` | `adf8ed6` / `d6cda7b` | 1.0.0 release candidate: **not touched** |
| OSP `claude/charming-lovelace-n25fte` (default) | `6d07440` | – | approved line: STRUM (#32), menu visual (#31), USER MACROS, wheels, SUB, SATELLITE, Shared FX |
| OSP `claude/shell-window-once` | `aef5e0d` | – | default + one shell fix (one size request per layout change) |
| OSP `claude/custom-arp` | `e39b0bb` | – | default + Custom ARP CP2–CP4 + the same shell fix (not merged into the default) |
| OSP `claude/andor-arp-spec-xy4umx` | `bb7722c` | – | custom-arp + `andor_arp_core` (ARP spinoff, in progress in another session) |
| OSP `claude/macos-release` | `c74b6af` | – | custom-arp + 0.9.0-beta.1 release infrastructure: **OSP's beta, not touched** |
| OSP `claude/analog-release` (`.gitmodules` branch) | `b522a06` | – | = ANALOG's current pin; a cherry-pick of `7652f42`, which the default has |

- **Custom ARP R2 does not exist yet.** The newest Custom Pattern editor is CP3/CP4 (`claude/custom-arp`,
  `docs/research/custom-arp/04-cp3-cp4.md`). It is not on OSP's default line.
- The OSP default contains everything ANALOG's pin has (b522a06 is the same change as 7652f42).
- `.gitmodules` keeps `branch = claude/analog-release`; the pin is set by commit, never by
  `git submodule update --remote`.

### Baseline results (ANALOG e3a8090, OSP b522a06, Linux, Release)

- Build: clean (7 min 45 s incremental on 4 cores).
- `andor_analog_tests`: **242 cases, all pass** (4 shards: 61 / 61 / 60 / 60). This includes
  the parameter-schema golden (`product/golden/parameter-schema.txt`, 672 lines), the
  modulation-destination golden and the V6 gate-0 render regression (`[v6-baseline]`: every
  factory sound and FX route against `product/golden/v6-baseline-renders.txt`).
- The rest of `ctest` (OSP unit / integration / regression, plugin, example product, visual
  references, performance smoke, bootstrap tool): see "Integration results".
- Pre-existing failures: none in ANALOG's suites.

## 2. Where shared code and ANALOG stand

Between ANALOG's pin and OSP's default there are 120 commits and 28 600 added lines in shared code
(`shared/`, `src/engine`). Much of it is **ANALOG's own work moved into OSP** ("Shared FX",
"SUB in ANDOR OSP", "SATELLITE ... now shared", "USER MACROS: ANDOR ANALOG's core ... shared").
Those ports were taken around 10–15 Nov. ANALOG kept evolving afterwards, so for several areas
ANALOG's copy is **ahead** of the shared copy.

### 2.1 Duplicated areas (ANALOG copy vs shared copy)

| Area | ANALOG | Shared (OSP default) | Who is ahead | Classification |
|---|---|---|---|---|
| FX DSP | `dsp/fx/*` | `src/engine/fx/*` | **ANALOG.** 10 of 14 files are byte-identical. FxGraph/FxEngine carry V6 MAIN + SEND (schema 2: `Graph::mainSend`, `runMainSend`, wet-only SEND root), which the shared copy lacks | Potentially incompatible (switching would drop MAIN + SEND) |
| FX routing view | `product/Source/FxRouting.*` | `shared/andor-ui/Source/FxRouting.*` | **ANALOG** (MAIN / SEND / OVERVIEW tabs, send knobs). **Shared** has the pedal light that follows the real bypass state (d0c3d269), host-generic hooks and menu icons | Requires ANALOG adaptation (port the two fixes now); convergence later |
| FX rack / presets / parameters / identity | `product/Source/FxRack.*`, `FxPresets.*`, `FxParameters.*`, `FxIdentity.*` | `shared/andor-ui/Source/Fx*` | **ANALOG** for content: RETRO MIX 100 % default and factory presets at 100 % (14 Nov), the AUX D identity. Shared adds `RackOpener`, `fxUserFolder`, USER MACRO live offsets | Potentially incompatible (the shared defaults would change `analog.fx.retro.1.mix` and the factory presets) |
| EQ spectrum | `product/Source/EqSpectrum.*` | `shared/andor-ui/Source/EqSpectrum.*` | Identical code | Already equivalent; switch with the FX |
| SUB | `dsp/SubOsc.*`, `product/Source/SubPanel.*` | `src/engine/sub/*`, `AndorSubPanel.*` | **ANALOG** DSP (MOD-wheel VIBRATO on the SUB, 15 Nov). Panels equivalent (shared takes IDs and key state from the host) | Requires upstreaming VIBRATO before switching |
| SATELLITE | `product/Source/SatellitePanel.*` (DSP already shared: `engine/SatelliteMotion.h`) | `SatellitePanel.*`, `SatelliteLive.h` | Equal. `addSatelliteParameters` hard-codes OSP's IDs and names, so ANALOG keeps its own parameters | Optional future improvement (panel can switch) |
| USER MACROS | `UserMacroState.*`, `UserMacroUI.*`, `dsp/UserMacroCore.h` | `UserMacroAssignments.*`, `UserMacroEditor.*`, `UserMacroController.*`, `usermacro/UserMacroCore.h` | Core and state identical. Shared UI is ahead: the USER palette and "turning a target knob keeps the chosen macro's ranges" (`UserMacroController` only; the shell's own glue still clears it) | Requires adaptation in the shell (shared) |
| FILTER rack | `AnalogRacks.cpp` `FilterRack` | `CharacterRack.*` | Not a duplicate: OSP's CHARACTER filter vs ANALOG's ladder / OTA filter. Only the look is shared | OSP-only / not applicable |
| Per-card ARP lanes | (none: the shell's `CardLanes`) | `CardArpLanes.*` (OSP's processor) | Duplication is inside OSP (shell vs OSP), not ANALOG | Already inherited (through the shell) |

**Include shadowing.** OSP's `shared/andor-ui/Source` now has `FxRouting.h`, `FxRack.h`,
`FxPresets.h`, `FxIdentity.h`, `FxParameters.h`, `EqSpectrum.h` and `SatellitePanel.h`, the
same names as ANALOG's. ANALOG's product sources still get their own (quoted includes search
the including file's directory first), and the namespaces differ (`andor::analog` vs
`osp::plugin`), so the product links. ANALOG's **tests** see OSP's first and did not compile.
`claude/analog-shared-fx-tests` fixed this by including `../Source/...` explicitly, so the tests
test the FX ANALOG ships. That fix is carried here.

**Decision for R1:** ANALOG keeps its own FX, SUB, SATELLITE panel and USER MACRO UI. Switching
any of them today would lose released ANALOG behaviour: MAIN + SEND, RETRO's defaults, SUB
vibrato. Two shared fixes ANALOG's copies lack are ported (the FX pedal light; the macro
keep-ranges fix goes into the shell, see 2.3). The convergence plan is in section 5.

### 2.2 Features by area

Classification key: **I** = already inherited and working, **V** = present in shared OSP, not yet
verified in ANALOG, **C** = missing product-specific configuration, **A** = requires ANALOG
adaptation, **O** = OSP-only / not applicable, **X** = potentially incompatible, **F** = optional
future improvement.

| # | Area | Shared source (OSP default) | Commits | State for ANALOG | Required action | Tests |
|---|---|---|---|---|---|---|
| 1 | STRUM, per card | `src/engine/Arpeggiator.*`, `shared/andor-ui/Source/StrumPanel.*`, `ArpSection.*`, `AndorParameters.cpp` (`addStrumParameters`), `ShellProcessor.cpp` | 0fdf73b, d8b8f199, d70b909c, 457645bc, cb6e874e, bb4e5b7b | **V + C.** The shell adds 15 parameters automatically: card A `arp.strum.{on,direction,sync,ms,division}`, cards B / C `analog.arp.card{B,C}.strum.*`. The key sits in the ARP title row beside SATELLITE | Set `ProductInfo::strumParameterHint` to 40: ANALOG's own hints already reach 39, and the shell's default of 35 is for products below that. Verify the key fits ANALOG's housing | Shell `[strum]`, `[shell]` tests per product; new ANALOG tests (modes, A/B/C, link, CHORD, held notes, sustain, transport, recall, no stuck notes); the 18-style golden |
| 2 | Custom ARP Patterns (CP3/CP4; R2 pending) | `src/model/ArpPatternModel.*`, `src/io/ArpPatternJson.*`, `src/engine/PatternCapture.*`, `shared/andor-ui/Source/CustomPatternEditor.*`, `ArpSection` | `claude/custom-arp` (9c3bd495 … e39b0bbc), not on the default | **V + C** (only on the custom-arp line). Parameters `arp.custom`, `analog.arp.card{B,C}.custom` (hint 36 → 41 for ANALOG); state `<CustomPatterns schema="1">` | Stacked branch pinned to custom-arp; set `customPatternParameterHint`; per-card tests; re-pin to R2 when it exists | `[arp-pattern]`, `[arp-custom-ui]`, `[arp-custom-state]`, shell `[arp-custom]`, new ANALOG per-card tests |
| 3 | Ceramic menu look | `OspLookAndFeel.*` (`SharedMenuLook`), `MenuIcons.*`, `docs/research/menu-visual/README.md` | 0719b910 | **I** for the look: `ShellEditor` holds a `SharedMenuLook`, so every menu in the binary takes it. **A** for icons: none of ANALOG's 9 own menus has icons | Add `menuicons` to ANALOG's menus (sound key, AUX, FX preset actions, FX routing gear / group / tile, sound import); rust ink for Delete | Shared menu-look tests; an ANALOG menu geometry test at 100 / 150 / 200 %; screenshots |
| 4 | Menu structure / content | `ShellEditor::showMenu`, `showLayerMenu`, `MainSections.cpp` `PresetBar` | 79e56e82, 0719b910 | **I** (icons on the global menu, source identity headers, preset dropdown). The five-group menu, save-in-place, Save As and confirmations are **only in OSP's editor** (`apps/plugin`) | Bring the applicable parts into the shell (shared, see 8) | Shell menu tests, new preset tests |
| 5 | Preset navigation / saving | `PresetBar`, `InstrumentHost::groupsPresets`, OSP `saveInstrument` / `canSaveInPlace` | d0e61dd0, 79e56e82 | Shell: SAVE always opens a chooser; no factory protection; "Save starting state…" shown with no handler (pre-existing bug) | Shell: SAVE updates a writable user preset in place, Save As…, factory presets protected, the dead item hidden. Grouping stays off for ANALOG (flat list fits its factory set) | New shell + ANALOG preset tests |
| 6 | USER MACROS, right-click assignment | `UserMacroController.*`, `UserMacroEditor.*`, ShellEditor glue | f639d382, 9415f886, 897d1130, 7ccb0e14, c010a8f4 | **I** mostly (halos, range drags, label menu with icons, popovers staying open through right-clicks, USER knob palette). **A** (shared): the shell still clears a macro's ranges when its target knob turns | Port the keep-ranges fix into the shell | ANALOG macro tests; new keep-ranges test |
| 7 | Wheels (vibrato, pitch) | `WheelPopups.cpp`, `ShapingPopups.h` `popover::` | d64f9193, 58684787 | **I.** ANALOG already had vibrato and the compact popovers (16 Nov); the shared versions are what it already runs | None | Existing `[wheels]` tests |
| 8 | ADVANCED / popover proportions | `ShapingPopups.h`, `frame::advancedPopoverOrigin (clearOf)` | d64f9193 | **I.** ADVANCED now opens clear of the ARP key. ANALOG's own popovers (TONE, MORPH, SUB, SATELLITE, SHAPER DNA, RHYTHM, SOUND) predate the `popover::` measures | Check visually; adopt the measures only where they do not change ANALOG's approved layouts | Screenshots |
| 9 | ARP SATELLITE / related UI | `SatellitePanel.*`, `ArpSection` | b6c6af87, bd208526, 49cff531 | **I.** ANALOG keeps its own panel (equal code) | None in R1 (see 2.1) | Existing `[satellite]` tests |
| 10 | PEDAL LOOP × REIMAGINED | `PedalFold.h`, `TapeFrame/Toybox/Mosaic/MirageEngine` | c34195ca line (PL-R1…R4) | **O** for the feature: PEDAL LOOP is OSP's sampler mode (One Shot + sustain pedal) and ANALOG has no such mode. ANALOG's A/B REIMAGINED use the same engines through `ReimaginedVoiceEngine`; "Normal, an unpressed pedal and 0 % are bit-identical" | Compile and prove ANALOG's REIMAGINED output unchanged | `[v6-baseline]`, REIMAGINED tests |
| 11 | Shared FX routing / devices | `src/engine/fx`, `Fx*` | 418562fc … 240ce874, d0c3d269 | **X** (see 2.1) | Keep ANALOG's; port the pedal-light fix | Existing FX tests, `[v6-baseline]` |
| 12 | MOVEMENT / ECHO / SPACE popovers and racks | `MacroPopups.*`, `EffectRack.*` | various | **I** (the shell hosts them; ANALOG's racks are opt-in modules) | None | Existing rack tests and visual regression |
| 13 | Keyboard utility keys / AUX | `KeyboardUtilityActions.*` | `Action::library` appended | **I** (ANALOG's D / SETTINGS / INFO unchanged) | None | `[utility-keys]` |
| 14 | ANDOR Learn | `shared/andor-learn` | be726f56 (done steps advance after 3 s), 7652f42 | **I** + **V**: ANALOG has its own complete catalog (`AnalogLearn.cpp`: Quick Start + lessons). Auto-advance is new to ANALOG | Run ANALOG's Learn tests | `AnalogLearnTests` |
| 15 | Source cards / engine controls | `EngineCard.*`, `EqEditor.*`, `NoteMapEditor.*` | various | **I** where opt-in flags are off. OSP's sampler-only card features (playback keys in the display, root / note-map editors) are **O** | Visual regression must stay at 0 px for ANALOG's cards | `andor_analog_tests_visual` |
| 16 | Visual / typography / hover | `Design.h`, `InstrumentFrame.*`, CORE lens sprites (c010a8f4) | c010a8f4, 7ccb0e14 | **V**: the CORE macro lens sprites were re-rendered in OSP's new palette; ANALOG keeps its mineral arcs | Check ANALOG's macro row in the visual regression; keep ANALOG's identity | Visual regression, screenshots |
| 17 | Other shared changes | `CharacterTap.h`, `SpectralImprint.*`, `LiveParameters.h`, `InstrumentLoader`, `RootNoteSelector`, `TutorialSounds` | – | **O** (OSP's sampler) or **I** (inert for ANALOG) | None | Full suite |

### 2.3 Shared API compatibility (the explicit pass)

| Interface | Change since b522a06 | ANALOG impact |
|---|---|---|
| `InstrumentHost` | New virtuals with defaults: `hasFxLibrary`, `fx*`, `groupsPresets`, user-macro state, capture, `createStrumPanel`, `setCustomArpPattern` (custom-arp) | None required. FX hooks stay off: ANALOG runs its own FX |
| `ShellProcessor` | `hasStrum()`, `isKeyHeld`, `characterName()`, `liveCapture()` (false); STRUM parameters and readers added | STRUM is on for ANALOG automatically. Set its hint |
| `ShellEditor` | `#include "FxRouting.h"` (shared `RoutingKey`), `SharedMenuLook`, menu icons, user-macro glue | Shadowing handled (2.1). The menu look applies |
| `ShellProduct` / `ProductInfo` | `UserMacros` = `osp::plugin::UserMacroHooks` (same members); new `strumParameterHint`; custom-arp adds `customPatternParameterHint` | Every field ANALOG sets still exists. Set the two hints |
| `ArpSection` | STRUM key; custom-arp adds the CUSTOM rows | Visual check in ANALOG's ARP editor |
| `CardArpLanes` | OSP processor only | None |
| `MacroPopups` | Minor | None |
| `OspLookAndFeel` | `SharedMenuLook`, menu drawing, text fields | Look applies to all of ANALOG's menus |
| Preset / menu components | `PresetBar` icons and grouping (off by default) | None required |
| Shared FX abstractions | New, off by default | ANALOG keeps its own (2.1) |
| `KeyboardUtilityActions` | `Action::library` appended | ANALOG does not switch on it |

The tests named `andor::analog::FxRoutingView`, `fxRackShown` and `andor::analog::fxpresets`
(the CP3/CP4 report) are ANALOG's own, unchanged APIs. They failed because the include picked
OSP's same-named header. Nothing was deleted or stubbed: the includes point at ANALOG's files.

## 3. Parameter and state compatibility

- Appended parameters only: STRUM (15: 5 for card A as `arp.strum.*`, 10 as
  `analog.arp.card{B,C}.strum.*`), off by default. On the custom-arp line also CUSTOM
  (3: `arp.custom`, `analog.arp.card{B,C}.custom`), off by default.
- Version hints: STRUM 40 and CUSTOM 41, both above ANALOG's highest (39). No existing ID, index,
  range, type or default changes.
- State: no `stateVersion` bump is needed. A session without STRUM parameters loads them at
  their defaults (off). A session without `<CustomPatterns>` clears the patterns, as before
  Custom ARP existed. `<FxRouting schema=2>`, `<UserMacros schema=1>`, C2, sample and Kontakt
  state are untouched.
- Bundle ID, manufacturer and plugin codes are untouched (`product/CMakeLists.txt`).

## 4. Not applicable to ANALOG (kept out on purpose)

Open Instrument / portable instruments, Starting States authoring, Load / Record audio, Map
notes, Root, Detect notes, sound packs and pack authoring, OSP's Library window, live capture,
PEDAL LOOP, the CHARACTER filter, OSP's five-group menu sections that only hold sampler actions,
grouped preset navigation (ANALOG's factory set is flat). Nothing new is added to ANALOG's
architecture: no new oscillator, AUX strip or audio route. C / C2 stay as they are.

## 5. Convergence plan (after R1)

1. **SUB** (low risk). Upstream the MOD-wheel VIBRATO from `dsp/SubOsc` into
   `osp::sub::SubOsc`; OSP's own SUB gains vibrato with it. Then ANALOG uses `addSubParameters`
   and `SubPanel` from `AndorSubPanel.h`, and its copies go.
2. **SATELLITE panel** (low). Switch to the shared panel with `params::satelliteId` as `IdOf`.
   Keep ANALOG's parameter function, or give the shared one an ID / name hook.
3. **USER MACRO UI** (medium). Move the shell onto `UserMacroController`, so OSP and every shell
   product run one copy of the glue. Fix the shared `descriptor()` (host IDs). Delete
   `UserMacroUI.*` and `UserMacroState.*`.
4. **FX** (high). Upstream V6 MAIN + SEND (graph, engine, routing tabs, send mixer), RETRO at
   100 %, and the AUX identity into OSP. Each is opt-in, so OSP's goldens stay. Add shell
   forwarders for the `fx*` host hooks, then delete ANALOG's six FX files and `dsp/fx`.
   - Gate: ANALOG's `[v6-baseline]` and schema goldens byte-identical, and OSP's renders unchanged.

Each step is its own OSP PR plus ANALOG pin PR (see `docs/OSP_DEPENDENCY_UPDATES.md`).

## Integration results (Gates 1–3, 5–7; Linux)

**Pins.** ANALOG `claude/osp-parity-r1` pins OSP `claude/analog-parity-r1` at `867ce06`, which is
`aef5e0d` (OSP's default plus the shell-window fix) plus two shell commits from this work
(`d8cecaf`, `867ce06`).

| Gate | Result |
|---|---|
| 1 Dependency | ANALOG builds against the new line. The only compile errors were in the new tests (two includes). |
| 2 Shared APIs | No ANALOG product source needed an API change. Shadowed headers: the tests include ANALOG's own (`../Source/...`, carried from `49e858a`). |
| 3 STRUM | `ArpParityTests.cpp`, 7 cases, all pass: defaults and hint 40; FREE UP / DOWN / RANDOM (seeded) / ALTERNATE; SYNC 1/32 = 62.5 ms at 120 BPM; unlinked A / B / C each with their own settings; linked as A; C alone; changes of STRUM, CHORD, LINK, ARP and SYNC under held keys, with sustain and an all-notes-off; recall of all 15 parameters; an older session (no STRUM in it) opening with STRUM off, fresh or over STRUM on; the key beside SATELLITE in ANALOG's panel, its light per edited card, its panel's card B page binding `analog.arp.cardB.strum.*`, the panel inside the window. |
| 5 Menus | `MenuParityTests.cpp`, 5 cases plus a screenshot case, all pass (Xvfb). The shared look is held while an editor is open; every action in the global, preset and FX-routing menus has an icon; Save / Save As are shown, with Undo / Redo shortcuts; no dead "Save starting state". Geometry at 100 / 150 / 200 %: the card on its anchor, the submenu edge to edge, and the global menu right-aligned to its key (fixed in the shell: it opened rightwards, off the plugin). |
| 6 Extras | Preset saving (shared shell): SAVE updates only the musician's own preset; factory presets listed first; exact reload of every parameter; SAVE leaves the factory files byte-identical. USER MACROS keep-ranges (two existing ANALOG tests updated: pressing the macro's own target now keeps its range; pressing elsewhere clears it). FX pedal light ported. |
| 7 Regression | `andor_analog_tests`: 255 cases (242 before, plus 13 new); all pass except one skip (a Kontakt fixture, skipped before too). Included: `[v6-baseline]` renders unchanged; the schema golden changed only in the 10 STRUM lines (hint 35 → 40). `analog_dsp_tests`, `andor_analog_tests_visual` (0 px), ANDOR OSP's unit / integration / regression / plugin suites, the example product, memo lab, performance smoke and bootstrap all pass. |

**Behaviour changes a musician will see** (all from OSP's approved shared work):
- STRUM (off by default).
- Menus in the ceramic look, with icons; the main menu right-aligned to its key.
- SAVE no longer opens a chooser for one's own preset (it saves in place, with a notice).
- The preset list shows "Factory" first, then "Your presets".
- Removing the recording asks once more.
- A macro's range stays shown while its target is turned.
- FX tiles' bypass lights follow automation and undo; a bypassed tile dims its display instead
  of a cream veil.

**Environment notes.** Real-window tests need a display: Xvfb. The shell test main now creates
the window-manager atoms JUCE looks up, on a connection kept open, so the server never resets
them. Before this, any real-window test after the first crashed with `BadAtom`, OSP's own
`[menu-visual]` test included. Screenshots: `docs/screenshots/parity/menus/` (100 and 150 %,
plus the preset dropdown at 200 %).

## Gate 4: Custom ARP Patterns (stacked, not approved in OSP yet)

**Status: prepared and tested, waiting on OSP.** OSP's Custom Pattern work is on
`claude/custom-arp` (CP2–CP4). It is not on OSP's default line, and **R2 does not exist yet**.
Per the spec, it is kept off the main integration branch:

- **ANALOG `claude/osp-parity-r1-custom-arp`** (stacked on `claude/osp-parity-r1`) pins **OSP
  `claude/analog-parity-r1-custom-arp`**: the parity line with `claude/custom-arp` (e39b0bb)
  merged in. Only `docs/STATUS.md` conflicted.
- `ProductInfo::customPatternParameterHint` is 41 (after STRUM's 40).
- Schema golden: two lines appended (`analog.arp.cardB.custom`, `analog.arp.cardC.custom`, v41).
  Card A's `arp.custom` is the shell's, checked against OSP's at the product's hint.
- The shell's `[arp-custom]` test now runs in ANALOG and passes; CP3/CP4 noted that it never had.
- New `CustomArpParityTests.cpp`, 5 cases, all pass:
  - three parameters, off by default;
  - each card's pattern on that card alone (A OSC 1, B OSC 2, C the hybrid), the others
    playing UP;
  - LINK: card A's pattern on every card, B's and C's own documents ignored;
  - unlinked, three patterns at once (with a rest);
  - while playing: a pattern changed, C2 added, CUSTOM off, C's arpeggiator off and on again,
    with no note left held;
  - C2 is part of card C: no voice ever sounds on a fourth card;
  - a factory sound with patterns on B and C, saved as a user preset and in a session, recalled
    onto the same cards and playing the same notes; a preset without patterns clears them;
  - the library is ANDOR's (`~/Documents/ANDOR/Patterns`, shared by every product). A
    `.andorpattern` file written by the shared codec reads back as the same document and
    plays on card B.
- Full ANALOG suite on this line: 261 cases, all pass (the usual Kontakt-fixture skip).

**When R2 lands:** merge R2 into `claude/analog-parity-r1-custom-arp` (or rebuild it on R2), move
ANALOG's pin with `tools/update-osp-pin.sh`, re-run `[arp-custom]`, `CustomArpParityTests` and the
editor screenshots. The editor is the only part R2 is expected to change; the ANALOG-side
contract (card mapping, state, hints) does not depend on it.

## Visual comparison (baseline vs after)

I compared 141 screenshots from ANALOG's existing screenshot tests: keys, SATELLITE, wheels, USER
macros, routing, the arp / SUB housing, SUB, macro destinations and the FX racks. Each ran on
the baseline (e3a8090) and after (the Custom ARP line, a superset of the parity line), at
869 / 1448 / 1810 px wide. Every difference is accounted for:

| Difference | Where | Verdict |
|---|---|---|
| The six CORE macro lenses changed colour (OSP re-rendered them in its new palette, c010a8f4), so they no longer matched ANALOG's arcs | every full-window shot | **Regression, fixed in ANALOG:** its walnut pack now carries the original lenses (`tools/add_macro_lenses.py`, pieces copied unchanged from OSP's skin at b522a06). Those shots are again identical to the baseline |
| STRUM key beside SATELLITE in the ARP title row | SATELLITE and rack shots with ARP open | Intended (STRUM) |
| MOD WHEEL / PITCH BEND popovers: OSP's compact layout (CONTROL row, smaller segmented keys, accent arcs) | wheels, keys | Intended (shared, d64f9193) |
| ADVANCED opens clear of the ARP key | keys (SETTINGS open) | Intended (shared `advancedPopoverOrigin (clearOf)`) |
| The window with AUX D shown is laid out once at its final height | keys (D active) | Intended (shared shell-window fix aef5e0d) |
| A bypassed FX tile dims its display; the cream veil is gone | routing | Intended (OSP's pedal-light fix, ported to ANALOG's view) |
| A USER macro's range stays shown while its own target is pressed | macro destinations | Intended (keep-ranges). The screenshot steps that deselect now press elsewhere, so their images match the baseline |

Nothing else moved: cards, knobs, racks, SUB, the keyboard and the housing are pixel-identical
to the baseline. `andor_analog_tests_visual` (the product's visual regression) is at 0 px.
