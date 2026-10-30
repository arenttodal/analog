# ANDOR ANALOG — release checklist

Nothing below has been signed, notarized, purchased or published. Credentials, licences and the
decision to sell are the owner's.

## Build

- macOS: `cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"`,
  target `ANDOR_Analog_All` (AU, VST3, Standalone). Deployment target 11.0 (ANDOR OSP's).
- Windows: Visual Studio 2022, 64-bit, `cmake -B build-plugin -G "Visual Studio 17 2022" -A x64
  -DOSP_BUILD_PLUGIN=ON` with SQLite from vcpkg (`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`);
  VST3 and Standalone only (no AU on Windows).
- Release builds with symbols archived; identifiers (`Aanl`, `com.osp.andoranalog`, parameter IDs,
  `ANDOR_ANALOG`, `.andoranalog`) never change.

## Validate

- [ ] `ctest --test-dir build-plugin --output-on-failure` (ANDOR OSP's suite, the visual regressions, `andor_analog_tests`, `analog_dsp_tests`).
- [ ] `pluginval --strictness-level 10` on the VST3 and the AU.
- [ ] `auval -v aumu Aanl Ospx`.
- [ ] Logic Pro (AU), Ableton Live (AU and VST3), REAPER or Cubase (VST3): scan, play, automate,
      save/reopen, offline bounce, sample-rate and buffer changes, MIDI panic, window sizes 869–1810 px.
- [ ] The listening checklist (docs/AUDIBLE_QA.md).

## Package (macOS)

- Install locations: `/Library/Audio/Plug-Ins/Components/ANDOR Analog.component`,
  `/Library/Audio/Plug-Ins/VST3/ANDOR Analog.vst3`, `/Applications/ANDOR Analog.app`.
- Factory presets are written by the plugin itself on first run
  (`~/Documents/ANDOR Analog/Presets/Factory`); the installer need not copy files.
- Sign app and plug-ins with a Developer ID Application certificate (hardened runtime, secure
  timestamp); the PKG with a Developer ID Installer certificate; submit the DMG/PKG to Apple's notary
  service; staple; verify with `codesign --verify --deep --strict`, `spctl -a -vv`, `stapler validate`,
  then install on a clean Mac. Keys live in CI secrets, never in a repository.
- ANDOR OSP's `plugin-pipeline/` is the build-and-download kit to reuse.
- Until then, the unsigned **test build**: CI (`plugin-macos`) uploads `ANDOR-Analog-macOS-test.zip`
  with the AU, VST3, standalone app, `packaging/macos-test/install.command` (per-user install, no
  admin rights, removes the download quarantine, makes AU hosts rescan) and its README.

## Package (Windows)

- Install locations: `C:\Program Files\Common Files\VST3\ANDOR Analog.vst3`,
  `C:\Program Files\ANDOR Analog\ANDOR Analog.exe` (and a Start menu entry).
- Sign the plug-in, the app and the installer (Authenticode, an EV or OV code-signing
  certificate) so SmartScreen does not warn.
- Until then, the unsigned **test build**: CI (`plugin-windows`) uploads `ANDOR-Analog-Windows-test.zip`
  with the VST3, the standalone app, `Install.bat` (asks for administrator rights) and its README.

## Licensing (decide before selling)

- [ ] **JUCE**: ANDOR OSP pins JUCE 8.0.9. Check the licence that applies to that version, and buy
      the commercial entitlement (or comply with the AGPL) before distributing closed-source binaries.
- [ ] **Artwork**: only `shared/andor-ui/Resources` from ANDOR OSP's repository (see its
      `assets/manifest.json`); this repository holds none. The owner authorizes its use in this product.
- [ ] **Fonts**: Inter and Outfit travel with their OFL licence files.
- [ ] **Branding**: "Prophet-inspired" is internal sound direction only; no product text, preset name
      or marketing may suggest Sequential or Arturia endorsement or copied sound, code or design.
- [ ] Factory presets are synthesized only (no samples, no outside files) — true today.

## Known release blockers

- The host lists all 350 ANDOR parameters plus ANALOG's 57 (the shell's grouped layout is still to come;
  four of ANALOG's own, the first version's LFO, are unused since version 2 and say so in their names).
- No host or listening validation yet (Linux tests and the macOS CI build with auval, see docs/STATUS.md).


## State version 4

- Sessions and presets from versions 1–3 migrate in one chain, ending with V3→V4. The test is `analog: a version-3 session ...`.
- ORGANIC recordings are saved by content hash and path, with a copy in ANDOR OSP's sample store, so a moved file still loads. A portable preset that embeds the audio is **not** implemented: presets store references.
- MORPH's profiles are rebuilt on load (about 0.5 s for 10 s of audio, on a worker thread) and are not stored.
- Still to do before release: pluginval, DAW sessions (Logic, Ableton, REAPER), listening QA (AUDIBLE_QA.md).
