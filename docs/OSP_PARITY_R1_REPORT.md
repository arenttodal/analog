# OSP synchronization and feature parity R1: final integration report

_20 Nov 2026. The audit, with every area classified, the decisions and the gate details, is
`docs/OSP_PARITY_AUDIT.md`. The update process is `docs/OSP_DEPENDENCY_UPDATES.md`._

## Exact revisions

| | Branch | Commit | OSP pin |
|---|---|---|---|
| ANALOG baseline | `feature/aux-live-input` | `e3a8090` | `b522a06` |
| **ANALOG integration** | `claude/osp-parity-r1` | `ec261bd` | **`127f430`** (OSP `claude/analog-parity-r1`) |
| ANALOG + Custom ARP (stacked) | `claude/osp-parity-r1-custom-arp` | `348e68c` | `d1bf2d2` (OSP `claude/analog-parity-r1-custom-arp`) |
| OSP integration | `claude/analog-parity-r1` | `127f430` | – (`aef5e0d` + 3 commits: shell presets / menus, test fix, STATUS) |
| OSP + Custom ARP | `claude/analog-parity-r1-custom-arp` | `d1bf2d2` | – (the above + `claude/custom-arp` `e39b0bb` + CUSTOM hint check) |

Not touched: ANALOG `main` and `claude/release-candidate` (1.0.0 RC), OSP's default branch,
`claude/macos-release` (0.9.0-beta.1) and `claude/custom-arp`. No release was published.

## Integration changelog

**OSP (shared, every shell product):**
- SAVE updates only the musician's own preset, never a factory one in place. "Save preset" and
  "Save preset as..." are separate items. The preset list shows "Factory" first, then "Your
  presets". The dead "Save starting state..." item is gone.
- The main menu right-aligns to its key at every scale. Undo / Redo show their shortcuts.
  Removing a layer or the recording asks once more.
- A USER macro's ranges stay shown while its own target is turned.
- Tests: the shell schema check takes the product's STRUM / CUSTOM hint. A new SAVE / presets
  shell test. Window-manager atoms are prepared once for real-window tests under Xvfb.

**ANALOG:**
- OSP pin moved from `b522a06` to `127f430`: STRUM, the ceramic menus, USER MACRO refinements,
  OSP's compact wheel / ADVANCED popovers, and every other shared change since 16 Nov.
- STRUM version hint 40. The shared menu icons added to ANALOG's own menus.
- The FX pedal-light fix ported into ANALOG's own routing view.
- ANALOG's original macro lenses kept (its walnut pack).
- New tests: `ArpParityTests`, `MenuParityTests`; `CustomArpParityTests` on the stacked branch.
- CI: an OSP pin reachability check, and BUILD-INFO.txt in the test build and the installer.
- Process: `tools/update-osp-pin.sh`, `docs/OSP_DEPENDENCY_UPDATES.md`.

## Parameter schema comparison (vs the baseline, 672 lines)

| Change | Lines | Hint |
|---|---|---|
| `analog.arp.cardB.strum.{on,direction,sync,ms,division}`, `analog.arp.cardC.strum.*` | 10 (the 19 Nov branch had them at v35) | v40 |
| `arp.strum.*` (card A, the shell's namespace; checked against OSP's schema) | 5 | v40 |
| Stacked branch only: `analog.arp.cardB.custom`, `analog.arp.cardC.custom`; `arp.custom` (card A) | 2 + 1 | v41 |

No existing parameter changed ID, order, type, range, default or hint. All appended parameters
are off by default. An older session opens with them off; this is tested fresh and over an
instance with STRUM on.

## Audio regression comparison

- `[v6-baseline]`: every factory sound and FX route renders as recorded at 1.0. Unchanged, with
  no golden regenerated.
- STRUM off and CUSTOM off leave the arpeggiator's output unchanged. OSP's 18-style ARP golden
  is unchanged in the ANALOG build (OSP unit / regression suites).
- No ANALOG DSP file changed. `analog_dsp_tests` pass.

## Compatibility and migration

- No state-version bump. `<FxRouting schema=2>`, `<UserMacros schema=1>`, C2, sample and Kontakt
  state are untouched. `<CustomPatterns schema=1>` (stacked branch) is written only when a card
  has one, and a state without it clears them, as in OSP.
- Factory presets: the installer and its manifest are unchanged. SAVE no longer overwrites a
  factory preset in place, but a deliberate Save As into the Factory folder still works, and
  the installer keeps such files (its existing test).
- Bundle ID, manufacturer and plugin codes are unchanged.

## Results (Linux, this container)

See "Integration results", "Gate 4" and "Visual comparison" in the audit. Summary:

| Suite | Main line | Custom ARP line |
|---|---|---|
| `andor_analog_tests` | 255 cases pass (1 skip, as before) | 261 pass (1 skip) |
| `andor_analog_tests_visual` | 0 px | 0 px |
| `analog_dsp_tests` | pass | pass |
| OSP unit, integration, regression, plugin; example product; memo lab; perf smoke; bootstrap | pass | pass |
| Menu geometry / screenshots (Xvfb) | pass | pass |

## CI and host validation

- Linux engine job (with the OSP pin check): green on `f9e4e9b`.
- macOS (universal build, ctest, auval, pluginval at strictness 10, installer): see "CI status"
  below.
- **Not verified here (OWNER TEST):**
  - DAW hosts (Logic, Live, Reaper): STRUM and Custom ARP automation and recall, menus in a host
    window, SAVE / Save As with the host's file dialogs.
  - Windows (not built, as instructed).
  - Listening to STRUM and custom patterns on real material.

## Remaining issues and priorities

1. **Custom ARP R2** (blocked on OSP): when it lands, rebuild the stacked line on it and re-pin.
   Only then can Custom Patterns reach the integration branch.
2. **FX convergence** (P1, OSP work): upstream V6 MAIN + SEND, RETRO 100 % and the AUX identity
   into OSP's shared FX, then delete ANALOG's copies (audit, section 5).
3. **SUB vibrato upstream** (P2): OSP's shared SUB lacks the MOD-wheel vibrato ANALOG has.
   Upstream it (OSP's SUB gains it too), then switch ANALOG.
4. **One USER MACRO glue** (P2): move the shell onto OSP's `UserMacroController`.
5. **OSP's own `[menu-visual]` test** needs `xvfb-run -s -noreset` when other real-window tests run
   first (OSP's plugin test main has no atom preparation). The shell's has it now.
6. **Macro lens palette** (decision): ANALOG keeps its original lenses. If ANALOG should adopt
   OSP's new CORE palette instead, its arc colours and lenses should move together.
