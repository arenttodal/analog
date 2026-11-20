# Updating ANDOR ANALOG's OSP dependency

ANDOR ANALOG builds on ANDOR OSP's shared code through the git submodule `external/osp`: the
instrument shell, the ANDOR interface, the shared engine (arpeggiator, STRUM, Custom Patterns,
SPACE / ECHO / MOVEMENT, wheels, Learn). **OSP is the source of truth for shared features;**
ANALOG holds only what is ANALOG's: its sound engine (`dsp/`), its product code
(`product/Source`), its factory sounds and its own visual identity.

The rule from both repositories' `CLAUDE.md`: a shared SPACE, ECHO or ARP feature is built in
OSP's shared code and ships in both instruments, tested in both. ANALOG never copies a shared
implementation into its own tree. A gap that cannot close at once is listed under NEXT in OSP's
`docs/STATUS.md`.

## The pin

- The submodule pin is **one exact commit**, recorded in ANALOG's tree (`git ls-tree HEAD
  external/osp`). It must be reachable from a branch or tag of the OSP remote; CI checks this on
  every push (`engine` job, "OSP pin").
- `.gitmodules` names `claude/analog-release` as the tracking branch. It is only a label: **never
  run `git submodule update --remote`**, which would follow that branch instead of the approved
  commit.
- Every build's artefacts carry `BUILD-INFO.txt` with ANALOG's commit and the OSP pin (CI writes
  it beside the test build and the installer).

## A shared change, from OSP to ANALOG

1. **In OSP:** implement and test the change in the shared code (`src/engine`,
   `shared/andor-ui`, `shared/andor-shell`). Keep it opt-in through `ProductInfo` /
   `InstrumentHost` wherever a product could be affected. OSP's own suites, design captures and
   every product's visual regression must stay at 0 px unless the change is meant to be seen.
2. **OSP review:** the change lands on an OSP branch with a PR. Its CI is green.
3. **ANALOG dependency update:** on a new ANALOG branch, move the pin with
   `tools/update-osp-pin.sh <osp-ref>`. The script resolves the ref to a commit, checks that the
   commit is reachable, commits the new pin with the list of OSP commits, and builds and runs
   `ctest` when a build directory exists. It never pushes.
4. **ANALOG checks** (the PR's checklist):
   - [ ] `andor_analog_tests` and `analog_dsp_tests` pass, and ANDOR OSP's suites inside the
     ANALOG build.
   - [ ] **State:** the parameter-schema golden (`product/golden/parameter-schema.txt`) changed
     only by *appended* lines. Their version hints are above ANALOG's own (today 40 and up),
     and every existing ID, order, type, range and default is unchanged.
   - [ ] **Sound:** `[v6-baseline]` passes. Every factory sound and FX route renders as
     recorded. Audio goldens are never regenerated to hide a change.
   - [ ] **Recall:** older sessions and presets load (the `[legacy]` tests); a new feature is off
     in them.
   - [ ] **UI:** `andor_analog_tests_visual` at 0 px, or each difference explained and approved.
     The relevant screenshot tests (`ANALOG_SCREENSHOT_DIR=… "[screenshots-…]"`,
     `ANALOG_MENU_SHOTS=… "[menu-parity-shots]"`) are inspected.
   - [ ] **Shared-name headers:** a new shared header with the name of one of ANALOG's own files
     (see `docs/OSP_PARITY_AUDIT.md`, 2.1) is included explicitly (`../Source/...`) in ANALOG's
     tests.
   - [ ] CI on macOS (AU validation, pluginval) green; Windows when releasing.
   - [ ] `docs/STATUS.md` (both repositories) updated; the pin named in the commit message.
5. **Approve and merge:** a person approves the dependency PR. No bot or workflow merges a pin
   change that alters plugin behaviour.

## Automation (what may and may not happen automatically)

- **May:** CI checks the pin's reachability and builds and tests every push, including a pin
  update branch made by the script. A scheduled job may *propose* an update (a branch plus the
  script's report).
- **May not:** auto-merge any pin change, regenerate goldens, or follow an OSP branch tip.

## Product-specific vs shared (where code belongs)

| Belongs in OSP (shared) | Belongs in ANALOG |
|---|---|
| The arpeggiator, STRUM, Custom Patterns, SATELLITE motion, SPACE / ECHO / MOVEMENT, the wheels and their popovers, the menu look and icons, the shell's preset handling, Learn's runtime | The oscillators, filters, C / C2 hybrid, MORPH, SUB's voicing, Kontakt / sample import, ANALOG's FX routing (until it converges, below), factory sounds, the walnut skin, ANALOG's Learn catalog |

ANALOG still carries its own copies of FX routing, SUB and USER MACRO UI that OSP has since
taken into shared code. The plan to converge them, with what must first move into OSP, is in
`docs/OSP_PARITY_AUDIT.md`, section 5.
