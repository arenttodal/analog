# ANDOR paid-release readiness scorecard (Black Friday 2026)

**Date:** 9 November 2026. **Primary:** ANDOR Analog 1.0.0. **Secondary:** ANDOR OSP (bundle
companion).

**Status legend:**
- **PASS**: ran, with the evidence given.
- **FAIL**: ran and failed.
- **NOT RUN**: not executed; the reason is given.
- **OWNER**: needs a decision, credential or purchase that only the owner can make.

Nothing is marked PASS without a run.

## Recommendation

| Product | Private beta | Paid release on Black Friday |
|---|---|---|
| **ANDOR Analog 1.0.0** | **GO**: both beta installers are built and validated in CI (run 150, macOS: tests, auval, pluginval; run 155, Windows: tests, DLL check, installer) | **CONDITIONAL GO.** Every automated gate passes. The remaining blockers are owner and human items: the JUCE and VST3 licences, Developer ID signing and notarisation, Windows signing, the EULA, and DAW and listening sign-off (H1–H11). If they are done by about 20 November, Black Friday is reachable. If not, launch the private beta and sell after. |
| **ANDOR OSP** | GO as a free public beta for Analog buyers (after its own CI run on the entitlement fix) | **NOT YET for Black Friday.** It has a macOS installer script and the owner has thousands of sounds, but those sounds are not yet packaged as factory presets, the installer has never been run with a Developer ID and has no notices or EULA, its musical review is open, and its release line has diverged from what Analog ships. Do not let it delay Analog (section 3). |

## 1. ANDOR Analog: automated evidence

| Gate | Result | Evidence |
|---|---|---|
| Baseline build (Linux x86-64, GCC 13, Release, `main` a25c270 + OSP 3d949cd) | **PASS** | 605/605 targets |
| Baseline full suite (13 ctest suites: engine, product, OSP's plugin/unit/integration/regression, visual regressions, performance smoke) | **PASS** 13/13 | local, 1958 s |
| Baseline macOS (universal) build + ctest + `auval -v aumu Aanl Ospx`; Windows build + engine and product tests | **PASS** | GitHub Actions analog run 142 (main a25c270): engine, plugin-macos and plugin-windows jobs all green |
| **Release candidate** full suite (13 suites, with every fix below) | **PASS** 13/13 | local, 1891 s, on `claude/release-candidate` deb382b |
| CLASSIC audio unchanged (`[legacy]` 34 factory + edge patches at three rates; `[fx baseline]` 114 frozen renders: exact hashes on Linux, contours elsewhere) | **PASS** | inside `analog_dsp_tests` and `andor_analog_tests`, RC run |
| Parameter IDs and order unchanged (OSP schema golden, Analog schema golden of 544 lines) | **PASS** | `shell: the parameter schema is ANDOR OSP's, unchanged…`, RC run |
| Host automation list: only engine-read parameters offered | **PASS** (new) | `analog: a DAW may automate exactly the parameters the engine reads`: 831 automatable, 128 not offered (the bindings test proves those 128 inert) |
| Modulation destinations frozen (87 destinations) | **PASS** (new) | `analog: the modulation destinations keep their places…` against `product/golden/modulation-destinations.txt` |
| Session and preset recall from real 1.0 state (11 sessions: default, 8 factory categories, modulated, CUSTOM FX) | **PASS** (new) | `analog: projects saved by 1.0 open with the same settings and the same sound` (21,309 assertions) |
| Older sessions migrate (v1→v13) | **PASS** | the migration tests per version, RC run |
| Factory update keeps the musician's presets | **PASS** (new) | `analog: a factory update replaces only the factory files it wrote; the musician's stay` |
| Realtime: no allocation while rendering; arpeggiator allocation fixed | **PASS** | `[realtime]` suites; OSP c2748b6, RC run |
| CPU | **PASS** (smoke) | `performance-smoke` RC run. Measured earlier (STATUS): 16 voices, 48 kHz, one VM core: CLASSIC 24.8 %, ORGANIC + MORPH 52–55 %. Not measured on a real Mac. |
| Visual regression (parity with OSP, 0 px outside masks) | **PASS** | `andor_analog_tests_visual`, RC run (Linux) |
| **Release candidate, macOS (universal arm64 + x86_64)**: build, full ctest (engine, product, OSP's suites, visual regressions), `auval -v aumu Aanl Ospx` | **PASS** | CI run 150 (https://github.com/arenttodal/analog/actions/runs/42668904963), job plugin-macos, every step green (58f3c63) |
| pluginval 1.0.4, strictness 10, VST3 and AU (macOS) | **PASS** | run 150: both runs end in `SUCCESS`, including parameter fuzzing and bus layouts; logs in the `pluginval-logs` artefact |
| **Release candidate, Windows (x64, MSVC)**: build, engine and product tests | **PASS** | CI run 155 (https://github.com/arenttodal/analog/actions/runs/42678945240), job plugin-windows, every step green (3c5e534) |
| Windows: no non-system DLL dependency | **PASS** | run 155: the `dumpbin /dependents` check passes for the VST3 and the app. SQLite is now static, so there is no `sqlite3.dll` (run 150 showed it gone); `dbghelp.dll` is System32. |
| macOS installer built | **PASS (unsigned)** | run 150: `ANDOR-Analog-1.0.0-macOS-UNSIGNED.pkg` (181 MB: AU, VST3, app, notices, EULA page) plus `.sha256`, artefact `ANDOR-Analog-macOS-installer`. Ad-hoc signed; no Apple credentials yet. |
| Windows installer built | **PASS (unsigned)** | run 155: `ANDOR-Analog-1.0.0-Windows-Setup.exe` (57 MB, Inno Setup: VST3, app, notices, EULA), artefact `ANDOR-Analog-Windows-installer`. Not Authenticode-signed yet. |

## 2. ANDOR Analog: blockers by class

**RELEASE BLOCKERS: fixed and verified here**
1. Dead parameters offered for automation (Analog's own RELEASE.md blocker): fixed (1069d7e,
   OSP d6cda7b).
2. Factory-preset update could delete user files (data loss): fixed (2faed13).
3. Arpeggiator allocated on the audio thread (realtime rule): fixed (OSP c2748b6).
4. No frozen contract for modulation destinations, and no real-state recall fixtures: added
   (7e3457b).
5. No third-party notices or EULA in any package, no installer, no signing path: added
   (a4817c7: notices generator, EULA draft, signing-ready `.pkg` and Inno Setup, CI steps;
   ran locally only as far as the notices).
6. Version written in three unchecked places: one source, 1.0.0 (deb382b).

**RELEASE BLOCKERS: open, owner or human only**
- **OWNER: JUCE 8 commercial licence.** JUCE is AGPLv3 or commercial; a closed-source sale needs
  the commercial licence.
- **OWNER: Steinberg VST3 licence agreement.** The VST3 SDK's proprietary licence requires a
  signed agreement before publishing.
- **OWNER: Apple Developer ID and notarisation.** Credentials go into CI secrets (runbook §2).
  **Windows Authenticode** certificate (runbook §3).
- **OWNER: EULA approval.** The draft is in `packaging/common/EULA-DRAFT.txt`.
- **OWNER: final company name, copyright line and bundle domain.** `COMPANY_NAME "OSP"`,
  `com.osp.andoranalog` and the plug-in codes are permanent after the first public build.
- **HUMAN: DAW validation H1–H11.** Logic, Live, REAPER, Cubase/Studio One, FL Studio; automation,
  recall, bounce, sample-rate and buffer changes, latency changes, transport sync, window
  sizes, clean installs, upgrade (runbook §5).
- **HUMAN: listening sign-off.** `docs/AUDIBLE_QA.md`, 44 factory presets. The docs call this the
  release gate.

**IMPORTANT NON-BLOCKERS**
- Trademark exposure in user-facing names: "Juno", "Jupiter" and "106" in presets and controls,
  "Kontakt" in the importer (the manual has a disclaimer), clearance of "ANDOR". These are owner
  decisions. Parameter *names* can change without breaking sessions; IDs cannot.
- Kontakt importer scope: a single-library MVP. Ship it labelled as such, or hide it.
- CLASSIC exact render hashes exist only for Linux; macOS and Windows are checked by contour.
  Record per-platform hashes in CI.
- The shell does not save the sample playback model (root, start, gain). A later
  analyser-version change could re-pitch saved ORGANIC layers, so do not change OSP's
  `analyzerVersion` in a 1.x update without a migration.
- The shell writes `schemaVersion` but never reads it: a newer project opened in an older build
  is not refused.
- Docs are stale: PARAMETER_REGISTRY stops at state 6 and the STATUS header is dated 30 October.
- Owner design approval of the FILTER and MORPH racks; owner voicing decisions (HPF, DCO).

**POST-LAUNCH**
- AAX (Pro Tools) and copy protection.
- FX routing gates 6–13 (stems, splitters, Advanced view, Lock FX, user impulses).
- `MidiKeyboardState` lock on the audio thread for the on-screen keyboard.
- The intermittent macOS pixel difference in ANDOR Example's visual regression.
- The `andor-core` extraction (`ANDOR_SHARED_PLATFORM_PLAN.md`, OSP repository).

## 3. ANDOR OSP (bundle companion)

| Gate | Result | Evidence |
|---|---|---|
| Full suite on OSP default branch | **PASS** 10/10 | local this session at 8da975f (plugin, visual reference, regression, integration, unit…); later commits on that branch are research and docs only |
| macOS build + ctest in CI | **PASS** | OSP run 276 (191f617) green; later runs were still in progress at the time of writing |
| Standalone microphone entitlement for notarisation | **Fixed, NOT RUN** | 1a844ed (`packaging/macos/OSP-Standalone.entitlements`; `scripts/package-macos.sh` checks it); needs a Developer ID |
| Installer | **EXISTS, NOT RUN** | `scripts/package-macos.sh` builds a signed, notarised `OSP-<version>.pkg` (AU, VST3, standalone) but needs a Developer ID, so it has never been run; CI ships only the unsigned test zip. No Windows installer. |
| Notices, EULA in the installer | **NOT DONE** | Analog's `packaging/common/` (notices generator, EULA page) is the template. |
| Factory sounds | **OWNER: choose the Core set** | Decided: one installer with a Core factory pack of 50–100 presets; instruments and styles as expansion packs sold as `.osppack` downloads from the Evenant website (OSP `docs/release/SOUND_PACKS_SPEC.md`). Built and tested on OSP's default branch: **Publish as pack…** (the publisher's id, version, licence, factory mark) and the build step that puts the Core pack into every bundle (`[unit][library][packs]`, `[plugin][library-packs]`). With no pack supplied, builds and installers are unchanged. Left: the owner publishes the Core pack from a Library collection. |
| DAW validation, listening (SYNTH Imprint, REIMAGINED, live capture in a DAW) | **NOT RUN** | human |
| Release line | **IMPORTANT** | Analog ships OSP `claude/space-presets` (+ `claude/analog-release`). OSP's default branch is 19 commits apart from it and lacks SPACE presets, racks and the faceplates. Reunite the lines first (platform plan, step 0). |

**Recommendation:** do not bundle at Black Friday. Offer OSP to Analog buyers as a free public
beta, and sell the bundle when the owner's sounds ship as factory presets, the installer has run signed
and notarised, and OSP has had its own DAW pass.

## 4. What happens next

1. **Done:** pushed, and CI is green on both platforms. **Private-beta package:**
   - `ANDOR-Analog-macOS-installer`: https://github.com/arenttodal/analog/actions/runs/42668904963/artifacts/13699989819
   - `ANDOR-Analog-Windows-installer`: https://github.com/arenttodal/analog/actions/runs/42678945240/artifacts/13701574056
   - Test zips: macOS https://github.com/arenttodal/analog/actions/runs/42668904963/artifacts/13700552748;
     Windows: the `ANDOR-Analog-Windows-test` artefact of run 155.
   - With `docs/MANUAL.md` and `docs/release/BETA_TESTER_GUIDE.md`.
2. **Owner:** the licences, certificates and secrets (runbook §2–4). Re-run CI: signed and
   notarised installers.
3. **Humans:** H1–H11, listening sign-off. Record the evidence here.
4. **Merge** `claude/release-candidate` into `main`; tag `v1.0.0`; publish (purchase flow spec).
