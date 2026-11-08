# FX Rack V2 — progress

One line per gate; evidence is the named tests and screenshots. "Done" means built, bound to real
parameters and DSP, tested and inspected - never a mock-up.

| Gate | State | Evidence |
| --- | --- | --- |
| G0 audit and freeze | done | `docs/FX_RACK_V2_AUDIT.md`; before-screenshots captured; all suites green at `7740c06` |
| G1 faceplate system (COMPRESSOR, SPREAD) | done | OSP `ParameterRack::Faceplate` (opt-in; OSP parity 0 px), `rackui::PowerKey`, `GraphFrame::setGlass`; every new effect's rack on it (styles in `FxRack.cpp faceplateFor`), preset browser and overflow in the rail; COMPRESSOR: live gain-reduction history + transfer curve with the live operating point (`Compressor::inputDb`); SPREAD: goniometer from the output (`InsertBase::ScopeTap`, lock-free, decimated); `[analog][fx][rack][ui]` (faceplate parts, cells clear of stripes and graph, power key = ON); `[screenshots-fx-racks]` with audio played |
| G2 the other v12 effects | done | EQ / FX FILTER take a 0.52 share, SHIFT / CONVOLUTION 0.55 (the graph is the instrument); EQ HP/LP slopes as 12/24 selectors over their frequencies, OUTPUT on the HP/LP page; curves with a glow; UTILITY: levels with ticks, polarity marked when inverted, correlation, goniometer of its output; MULTIBAND: bands at their real crossovers on a frequency axis with live down/up bars; `[screenshots-fx-racks]` |
| G3 DRIVE / MOVEMENT / ECHO / SPACE | not started | |
| G4 MORPH / voice FILTER | not started | |
| G5 RETRO foundation (TAPE, VINYL, DIGITAL) | not started | |
| G6 RETRO CODEC | not started | |
| G7 DRIVE creative modes, SPREAD extras, compressor per-band | not started | |
| G8 latency planner, splitters, stems / buses, Advanced, Lock FX, user IRs | not started | |
| G9 verification matrix | not started | |
| G10 DAW acceptance | not started | |
