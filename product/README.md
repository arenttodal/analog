# ANDOR Analog

An ANDOR instrument built from the ANDOR Instrument Shell (`shared/andor-shell`), written by
`tools/create_andor_instrument.py` on 2026-10-28.

| | |
|---|---|
| Product name | ANDOR Analog |
| Slug | `analog` |
| Plugin code | `Aanl` (manufacturer `Ospx`) |
| Bundle ID | `com.osp.andoranalog` |
| Version | 0.1.0 |
| CMake target | `ANDOR_Analog` (tests: `andor_analog_tests`) |
| Presets | `Documents/ANDOR Analog/Presets/*.andoranalog` |
| Saved state | `<ANDOR_ANALOG product="analog">` |

This product lives in arenttodal/analog and is built by ANDOR OSP (the `external/osp`
submodule) through `ANDOR_EXTERNAL_PRODUCTS` (see the repository's top-level `CMakeLists.txt`).

Build (from the repository root): `cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON && cmake --build build-plugin --target ANDOR_Analog_All`
Test: `ctest --test-dir build-plugin -R andor_analog_tests --output-on-failure`
Its engine is `../dsp` (pure C++); its own parameters are pinned by `golden/parameter-schema.txt`.

What to change, and what never to change: `andor-design-system/HANDOFF.md`.
- `Source/Product.cpp`: the identity, the macros (and their popovers, `AnalogPopovers.cpp`), the
  product's own parameters (`AnalogParameters.cpp`), the factory presets, the engine (`AnalogEngine.cpp`).
- The engine: implement `andor::shell::Engine` (`shared/andor-shell/Source/ShellEngine.h`).
  It names the parameters it reads; every other control is shown disabled.
- The look is ANDOR OSP's, from `shared/andor-ui`: never copy or restyle it here.
