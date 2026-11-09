# ANDOR Analog 1.0.0 — release candidate runbook

This is how a release candidate is built, signed and checked, and what only a person can do.
The test results for the current candidate are in `READINESS_SCORECARD.md`.

## 1. What a release candidate is

- Analog `claude/release-candidate` (to be merged to `main`), with `product/product.json` version
  `1.0.0`. The version is written only there; CMake reads it and fails on anything that is not
  `MAJOR.MINOR.PATCH`.
- OSP pinned by submodule at `claude/analog-release`: OSP 3d949cd (what `main` shipped) plus
  two release fixes, the arpeggiator's audio-thread allocation and the automation filter.
- CI (`.github/workflows/ci.yml`) builds every artefact from that commit:

| Artefact | Job | Signed? |
|---|---|---|
| `ANDOR-Analog-macOS-installer` → `ANDOR-Analog-1.0.0-macOS[-UNSIGNED].pkg` + `.sha256` | plugin-macos | Yes, when the Apple secrets exist; otherwise ad-hoc and named `-UNSIGNED` |
| `ANDOR-Analog-macOS-test.zip` (per-user install script) | plugin-macos | No |
| `ANDOR-Analog-Windows-installer` → `ANDOR-Analog-1.0.0-Windows-Setup.exe` | plugin-windows | Not yet (section 3) |
| `ANDOR-Analog-Windows-test.zip` | plugin-windows | No |
| `pluginval-logs`, `visual-regression` | plugin-macos | — |

## 2. macOS signing and notarisation: the owner's one-time setup

1. Enrol in the Apple Developer Program as the selling entity.
2. Create a **Developer ID Application** and a **Developer ID Installer** certificate. Export both,
   with their private keys, into one `.p12` with a password.
3. In App Store Connect, create an **API key** with the Developer role. Download the `.p8`, and
   note the Key ID and the Issuer ID.
4. Add these GitHub repository secrets to arenttodal/analog (*Settings › Secrets and variables ›
   Actions*):

| Secret | Value |
|---|---|
| `APPLE_CERTIFICATES_P12` | `base64 -i certs.p12` |
| `APPLE_CERTIFICATES_PASSWORD` | the `.p12` password |
| `ANALOG_APP_IDENTITY` | `Developer ID Application: <Name> (<TEAMID>)` |
| `ANALOG_INSTALLER_IDENTITY` | `Developer ID Installer: <Name> (<TEAMID>)` |
| `ANALOG_NOTARY_KEY` | the contents of the `.p8` |
| `ANALOG_NOTARY_KEY_ID` | Key ID |
| `ANALOG_NOTARY_ISSUER` | Issuer ID |

5. Re-run CI on the release commit. The macOS installer step then signs with the hardened runtime
   and a timestamp, builds the `.pkg`, signs it, notarises it and staples it.
6. **Verify on a Mac that has never had ANDOR installed** (evidence goes in the scorecard):
   ```
   pkgutil --check-signature ANDOR-Analog-1.0.0-macOS.pkg     # Developer ID Installer, trusted
   spctl -a -vv -t install ANDOR-Analog-1.0.0-macOS.pkg       # accepted, source=Notarized Developer ID
   xcrun stapler validate ANDOR-Analog-1.0.0-macOS.pkg        # The validate action worked
   # install it, then:
   codesign --verify --deep --strict "/Library/Audio/Plug-Ins/Components/ANDOR Analog.component"
   auval -v aumu Aanl Ospx
   ```

The same script runs locally: `packaging/macos/build-installer.sh <artefacts> 1.0.0 out
THIRD_PARTY_NOTICES.txt`, with the same environment variables, or with `ANALOG_NOTARY_PROFILE`
for a keychain profile.

## 3. Windows signing: the owner's one-time setup

1. Buy an OV or EV code-signing certificate. EV certificates come on hardware tokens or as
   cloud signing (Azure Trusted Signing, SSL.com eSigner, DigiCert KeyLocker).
2. Sign `ANDOR Analog.vst3\Contents\x86_64-win\ANDOR Analog.vst3` and `ANDOR Analog.exe`
   *before* the installer is compiled, then sign the produced `Setup.exe`:
   ```
   signtool sign /fd sha256 /tr http://timestamp.digicert.com /td sha256 /a <file>
   ```
   With cloud signing, use the provider's GitHub Action in the `plugin-windows` job, between
   *Build* and *Build the Windows installer*. That step is to be added once the provider is
   chosen.
3. Verify: `signtool verify /pa /v <file>`. On a fresh Windows 11 VM, check that SmartScreen
   names the publisher.

## 4. Before the first sale (owner actions, no code)

- [ ] JUCE 8 commercial licence held by the selling entity (the right revenue tier).
- [ ] Steinberg VST3 licence agreement signed (required by the VST3 SDK's proprietary licence).
- [ ] EULA approved (start from `packaging/common/EULA-DRAFT.txt`). Replace the draft file in the
  installers (the scripts read `EULA-DRAFT.txt`: rename it, or point them at the approved file).
- [ ] Company name, copyright line and bundle domain are final. `COMPANY_NAME "OSP"` and
  `com.osp.andoranalog` are recalled by hosts forever after the first public build.
- [ ] Trademark decisions: the "Juno" / "Jupiter" / "106" names in presets and on controls;
  "Kontakt" in the importer (a disclaimer is in the manual); clearance of "ANDOR".
- [ ] Written authorisation for the rendered artwork (ceramic skin, ivory knobs, walnut) in
  `andor-design-system/assets/manifest.json`.
- [ ] Whether the repositories stay public: anyone can build the product from them today.

## 5. Human validation (cannot be automated here)

Record each result with a screenshot or log in the scorecard. Never tick a box without one.

| # | Host / check | Format | Steps | Pass when |
|---|---|---|---|---|
| H1 | Logic Pro 11 (Apple silicon) | AU | Scan; play factory presets; automate FILTER and MORPH; save, quit, reopen; bounce offline and in real time | Validation passes in the Plug-in Manager; identical reopen; bounces match; automation list holds only live controls |
| H2 | Logic Pro (Intel or Rosetta) | AU | As H1 | As H1 |
| H3 | Ableton Live 12 | AU and VST3 | As H1, plus switching sample rate 44.1/48/96 and buffer 64–2048 during playback | No dropouts or garbage; state survives |
| H4 | REAPER 7 | VST3 (macOS and Windows) | As H1, plus a CUSTOM route with RETRO codec and CONVOLUTION (latency changes) | REAPER's PDC follows (no offset in a bounce against a reference track) |
| H5 | Cubase or Studio One | VST3 (Windows) | As H1 | As H1 |
| H6 | FL Studio | VST3 (Windows) | Load, play, save | Works |
| H7 | Host transport | any | LFO SYNC, SHAPER SYNC and the arpeggiator against a running transport at 90 and 140 BPM | They stay in time |
| H8 | Window sizes | any | 869 px to 1810 px, Retina and 100 % Windows scaling | Nothing clipped |
| H9 | Clean install (both OSes) | installer | Section 2.6 / section 3.3 | No warnings; no missing DLL |
| H10 | Listening | — | `docs/AUDIBLE_QA.md`'s checklist; all 44 factory presets | Signed off by the owner |
| H11 | Upgrade | installer | Install 1.0.0, save a project, install the next candidate over it | Same sound and automation; user presets kept |
