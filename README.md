# ANDOR ANALOG

**ANDOR /ANALOG — a living analog instrument.** A 16-voice, two-oscillator virtual-analog
polyphonic synthesizer in the ANDOR instrument family, after the classic five-voice polysynths
of 1978–80 (the sound direction is "Prophet-inspired"; nothing here is, or claims to be,
Sequential's or Arturia's).

It is built from ANDOR OSP's own interface and plugin shell (`external/osp`, a submodule of
[arenttodal/OSP](https://github.com/arenttodal/OSP)), so it is the same ceramic instrument with
the same controls; its sound engine is original C++20 written for it.

| | |
|---|---|
| Plugin | ANDOR Analog: AU (macOS), VST3, Standalone |
| Identifiers | plugin code `Aanl`, manufacturer `Ospx`, bundle `com.osp.andoranalog` |
| Saved state / presets | `<ANDOR_ANALOG product="analog">`, `*.andoranalog` |
| Engine | `dsp/` (pure C++20, no JUCE): `analog_dsp` |
| Plugin | `product/` (built by ANDOR OSP's `andor_add_instrument`) |

## Build

```bash
git submodule update --init
```

```bash
cmake -B build -G Ninja && cmake --build build --target analog_dsp_tests analog-render
```

```bash
./build/analog_dsp_tests
```

The plugin (needs JUCE's GUI dependencies; on Linux the packages listed in OSP's CI):

```bash
cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON && cmake --build build-plugin --target ANDOR_Analog_All andor_analog_tests
```

```bash
ctest --test-dir build-plugin -R "analog|osp_visual_reference" --output-on-failure
```

Listen to the engine without a host:

```bash
./build/analog-render --list
```

```bash
./build/analog-render --all renders/
```

## What is where

- `dsp/` — the synth: `Synth` (voices, allocation, mono/unison, glide, MIDI, oversampling,
  decimation, effects, output guard), `Oscillator.h` + `PolyBlep.h` (band-limited saw, pulse,
  triangle, hard sync), `LadderFilter.h` (4-pole ZDF ladder with saturation), `Envelope.h`,
  `Lfo.h`, `HalfBand.*`, `Patch.h` (a sound in musical units), `FactoryPatches.*` (24 sounds).
- `product/` — the plugin: `Product.cpp` (identity, macros, popovers, parameters, presets),
  `AnalogEngine.*` (the shell boundary), `AnalogParameters.*`, `AnalogPopovers.*`, its tests and
  its parameter golden.
- `tests/` — the engine's tests (pitch, aliasing against a naive oscillator, filter, envelopes,
  voices, determinism, headroom, every modulation route, realtime allocation).
- `tools/analog-render` — offline renders (WAV) and a CPU benchmark.
- `docs/` — [SPEC](docs/SPEC.md) (design and derivations), [PARAMETER_REGISTRY](docs/PARAMETER_REGISTRY.md),
  [AUDIBLE_QA](docs/AUDIBLE_QA.md) (measurements and the listening checklist),
  [RELEASE](docs/RELEASE.md) (packaging, signing, licensing), [STATUS](docs/STATUS.md).

## Rules

ANDOR OSP's `CLAUDE.md` and `andor-design-system/HANDOFF.md` apply: realtime safety, determinism
from a stored seed, no new visual vocabulary, never present an unconnected control as working,
identifiers are forever. The rendered artwork stays in ANDOR OSP's repository (this one only
references it through the submodule).
