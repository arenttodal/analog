# FX Rack V3 - final polish (9 November 2026)

The V3 addendum (`ANDOR_FX_RACK_V3_FINAL_POLISH_ADDENDUM.md`) on top of the V2 racks. Interface
and read-only telemetry only: no DSP, parameter ID, preset or saved-state change.

## What changed

| Area | Implementation | Shared (ANDOR OSP) and opt-in |
| --- | --- | --- |
| One rack face | `rack::Host::setFullBleed` (no host panel, title or divider; the module gets the host's whole 214 px; one shadow from the housing via `SurfaceFace::surfaceShadowOnly`); `Faceplate::fullBleed` (`hardware::measures`: no inset, 22 px stripes, 16 px margins, the panels' 13 px radius, two foot screws) | `ProductInfo::fullBleedRacks`, `Faceplate::fullBleed`; ANDOR OSP sets neither (parity 0 px) |
| Palette | SPACE in aged stone / spore (rack, macro ring and light, routing tile, reverb picture's tint); PHASER slate; SHIFTER aged metal; FILTER moss / sage; MORPH terracotta / copper; no violet left in ANALOG | `Materials::macros` (a product's own macro identities); SPACE picture's tint follows it |
| ON | `rackui::PowerKey::Style::slide`: a 30 x 16 horizontal ceramic slide with its word (ON / OFF), the thumb right while on (not colour alone), checkable accessibility state; FX racks, MORPH, MOVEMENT's lane power (`powerSlide`) | style opt-in; same parameter, gesture, undo, keys |
| Close | the host's collapse key as a borderless X (30 px target, 10 px from the right, on the rail's centre line); closes the view only | in full-bleed mode only |
| Rail | power, overflow, preset, lane keys and X on one centre line (y = 16) | |
| FILTER graph | LIVE: the most recent sounding voice's real cutoff (`Synth::FilterTap`, a seqlock per voice written once a control period: base cutoff incl. KEYS / ANALOG / LFO / DRIFT / routes, envelope, stage, depth x velocity, Poly Mod); BASE (muted), ENVELOPE REACH (dashed ghost), NOW (the response at the cutoff used); PEAK; A D S R stage. PREVIEW: an edit of an envelope, ENV, TONE, VEL TO FILTER or ENV LINK with nothing playing runs a silent C4 / 100 through the synth's own `Envelope` and parameter mapping (`params::Reader`), labelled PREVIEW ... SILENT; a click on the graph replays it | ANALOG only |
| Bindings fix | `analog.fx.*` declared read by the engine: the routed effects' controls were greyed and disabled whenever the shell re-applied bindings (a V2 bug) | |

## Tests

- `[analog][rack][filterlive]`: LIVE equals the engine's tap (base, cutoff, depth); ENV +24 reach x4
  (two octaves); -12 an octave down; 8 ms attack reaches its peak; sustain 20 %; release; the
  graph rests when the voice ends; KEYS (each base the engine's); velocity scaling 64 / 127;
  the most recent voice followed, back to the held one when it ends; PREVIEW's A / D / S / R
  through the same envelope; ENV LINK follows the AMP times; ENV 0 holds still.
- `[analog][fx][rack][ui]`, `[analog][fx][rack][ui][retro]`, the rack layout tests (V3 metrics),
  the bindings audit (FX parameters declared, silent in CLASSIC), every product suite;
  `analog_dsp_tests` incl. the CLASSIC golden renders (bit for bit), the realtime suites;
  visual parity (ANDOR OSP 0 px), the example product.
- Screenshots: `[screenshots-v3]` (every family in its modes at 869 / 1448 / 1810, rack and window);
  `[screenshots-filter-sweep]` -> `docs/screenshots/fx-rack-v3/filter-live-sweep.gif`.
