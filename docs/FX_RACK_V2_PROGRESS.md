# FX Rack V2 — progress

One line per gate; evidence is the named tests and screenshots. "Done" means built, bound to real
parameters and DSP, tested and inspected - never a mock-up.

| Gate | State | Evidence |
| --- | --- | --- |
| G0 audit and freeze | done | `docs/FX_RACK_V2_AUDIT.md`; before-screenshots captured; all suites green at `7740c06` |
| G1 faceplate system (COMPRESSOR, SPREAD) | done | OSP `ParameterRack::Faceplate` (opt-in; OSP parity 0 px), `rackui::PowerKey`, `GraphFrame::setGlass`; every new effect's rack on it (styles in `FxRack.cpp faceplateFor`), preset browser and overflow in the rail; COMPRESSOR: live gain-reduction history + transfer curve with the live operating point (`Compressor::inputDb`); SPREAD: goniometer from the output (`InsertBase::ScopeTap`, lock-free, decimated); `[analog][fx][rack][ui]` (faceplate parts, cells clear of stripes and graph, power key = ON); `[screenshots-fx-racks]` with audio played |
| G2 the other v12 effects | done | EQ / FX FILTER take a 0.52 share, SHIFT / CONVOLUTION 0.55 (the graph is the instrument); EQ HP/LP slopes as 12/24 selectors over their frequencies, OUTPUT on the HP/LP page; curves with a glow; UTILITY: levels with ticks, polarity marked when inverted, correlation, goniometer of its output; MULTIBAND: bands at their real crossovers on a frequency axis with live down/up bars; `[screenshots-fx-racks]` |
| G3 DRIVE / MOVEMENT / ECHO / SPACE | done | OSP `rack::Faceplate` / `hardware::paint` shared (RackFaceplate.h); MOVEMENT / ECHO / SPACE wear it through `ProductInfo::rackFaceplates` (preset and actions in the rail, LED = the macro playing; OSP unchanged, parity 0 px, `osp_plugin_tests` 160 green); DRIVE: the FX rack's DRIVE faceplate on the instrument's own `drive*` parameters (TUBE / TAPE / CRUNCH, transfer curve and harmonics from `DriveProcessor::transfer`); `[screenshots-fx-racks]` |
| G4 MORPH / voice FILTER | done | synthesis racks (never in the FX chain) on faceplates: FILTER "SYNTH / VOICE FILTER", MORPH "SYNTH / SPECTRAL MORPH" with its power key; `[analog][rack]` |
| G5 RETRO foundation (TAPE, VINYL, DIGITAL) | done | `fx::Type::retro` appended (pool 2), 48 parameters a slot (hint 29), state 13; DSP `dsp/fx/FxRetro.cpp`; `[retro]` 13 DSP cases + `[analog][fx][rack][ui][retro]`; realtime: no allocation (`[realtime][fx][retro]`); 1.1-2.2 % of a core; 13 factory presets; docs/FX_EFFECTS_LIBRARY.md RETRO |
| G6 RETRO CODEC | done | STFT 1024/256 at 48 kHz: mask, quantisation, DROP / REPEAT / JITTER / COMBINED, STEREO / JOINT / MONO, ARTIFACT ONLY, AMBIENCE; LOSS 0 < -60 dB error; host latency not yet reported (G8) |
| G7 DRIVE creative modes, SPREAD extras, compressor per-band | not started | |
| G8 latency planner, splitters, stems / buses, Advanced, Lock FX, user IRs | not started | |
| G9 verification matrix | not started | |
| G10 DAW acceptance | not started | |
