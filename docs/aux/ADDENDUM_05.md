# D / AUX Addendum 05: LIVE INPUT (the DAW's sidechain) and MORPH TO D

Branch: `feature/aux-live-input` in both repositories, stacked on `feature/aux-d`. Nothing is merged into `main` or the 1.0.0 release candidate.

**Owner decisions taken before work:**
- Build past Gate 1 with an in-process VST3 host as the routing proof, and send an early Mac build for a real DAW test.
- **MORPH TO replaces the D MORPH knob:** one global stage, chosen in the MORPH rack and driven by the MORPH macro.

## Gate 0: audit

| Area | Finding | What changed |
|---|---|---|
| Shell buses | `ShellProcessor` declared stereo out only; `processBlock` calls `buffer.clear()` before the engine, and JUCE shares input and output channels in that buffer | A product opt-in (`ProductInfo::externalInputName`); the input is copied out before the clear (Gate 1) |
| JUCE 8.0.9, VST3 | `VST3ClientExtensions::getPluginHasMainInput()` decides whether input bus 0 is `kMain` or `kAux` | The shell returns an extension saying `false` for a product with an input: an auxiliary (sidechain) bus, so the plugin stays an instrument |
| AU | `IS_SYNTH` (aumu) with an input bus: Logic offers a sidechain menu for instruments that have one | Unchanged identity; **unverified until tested in Logic / auval on the Mac CI** |
| D's audio path | D's sources share one path in `Synth::mixAux`: ramps, LEVEL, MUTE / SOLO, the AUX FILTER (the FILTER macro and rack), then into the shared effects before the FX route; D KONTAKT's bus is read forward across the MIDI sub-renders | LIVE INPUT reuses that path and that sub-block reading |
| Global morph | `GlobalMorph` (24 third-octave bands, zero latency, filter-based spectral imprint) already drives D MORPH (Option D) and C EXTERNAL's morph; `LiveMorph` is per voice | Reused; no new morph DSP |
| UI | AUX strip (Addendum 04 layout), AUX rack (`AuxUI.cpp`), MORPH rack (`AnalogRacks.cpp`, `MorphRack`) | LIVE INPUT in the strip and rack; MORPH TO in the MORPH rack |

## Gates 1–2: the sidechain bus and safe transport

- **Shell (OSP, opt-in):**
  - `ProductInfo::externalInputName` adds one input bus: stereo, **off by default**, mono / stereo / off accepted.
  - VST3 marks it an auxiliary bus.
  - `processBlock` copies it into preallocated buffers before writing the output, and hands it to the new optional `Engine::setExternalInput`.
  - A bus the host keeps off gives nothing, never the buffer's stale contents.
- **ANDOR OSP and every product without the opt-in: unchanged** (shell test `[buses]`; OSP's suites and captures).
- **ANDOR Analog** opts in with the bus name **"AUX D Live Input"**.

**Proof here (not a DAW):** ANDOR Analog's own VST3 is built on Linux and hosted through JUCE's VST3 path in test `[vst3]`. It checks:
- it scans as an instrument with MIDI in and the AUX D Live Input bus;
- sidechain audio routed in comes out through D with no MIDI;
- with the bus off, junk left in the host's buffer is never heard (identical to zeros).

## Gates 3–4: AUX LIVE INPUT

- **AUX Mode** gains **LIVE INPUT** (appended; D's parameters are unreleased).
- **Audio:**
  - The engine hands the block's input to the synth.
  - D plays it **with no MIDI held**, through a 5 Hz DC blocker and a non-finite guard (no gain of its own, no hidden normalisation).
  - Then the same single path as every D source: LEVEL, MUTE / SOLO / POWER ramps (10 ms), the AUX FILTER (it follows the FILTER macro and rack) and the shared effects (DRIVE, MOVEMENT, ECHO, SPACE, the FX route).
  - Never twice, never after the effects.
- **Strip:**
  - LIVE INPUT in the source dropdown; no contextual knobs (LEVEL is the permanent one).
  - **The display:** the input's level history (one bar a block), or SIDECHAIN INPUT DISABLED / NO INPUT SIGNAL / LIVE INPUT UNAVAILABLE IN THIS HOST.
  - **The readout:** LIVE INPUT, MONO / STEREO and its peak.
  - **Interaction:** a drop is refused ("nothing to load"); RHYTHM's loop is never touched. A click opens the rack.
- **Rack:** MODE gains LIVE; an INPUT meter (left / right peaks, −60…0 dB, state) and LEVEL. The tooltip explains the DAW routing.
- **State:** a session remembers LIVE INPUT mode only: no audio, no source-track name (the DAW owns the routing). The other modes keep their state (tested).

## Gates 5–7: MORPH TO D (GLOBAL IMPRINT)

- **MORPH TO** (`analog.morph.target`: C HYBRID, the default, or D AUX; appended) in the MORPH rack. Only one processor runs at a time:
  - **C:** the per-voice MORPH exactly as before.
  - **D:** the MORPH macro (its position, automation and USER macros; while MORPH's lamp is on) drives the global stage. Every per-voice MORPH path stands aside: the voices, C RHYTHM's loop morph and C EXTERNAL's. Their settings are untouched and come back with C.
- **Exclusive routing:** with MORPH TO D and MORPH on, D is heard **only** through the stage, never also layered.
- **Endpoints (tested):**
  - **0 %:** the synth exactly, bit for bit;
  - **50 %:** the synth in D's spectral envelope, moved towards D's balance and over 30 % away (RMS) from a plain crossfade;
  - **100 %:** D alone, at D's level and brightness.
  - The stage is GlobalMorph's imprint (each band's gain glided, clamped −36…+18 dB, level steered to the interpolation of the two energies), labelled **GLOBAL IMPRINT**. No per-voice method's name is shown for it.
- **Silence and stability:**
  - The stage is scaled by D's activity (0 while D is silent, muted or off), so with nothing arriving the synth plays as it is: no normalisation of nothing.
  - The input starting and stopping never clicks.
  - D's own layer glides (10 ms) when switching between layered and exclusive.
- **Solo:** D soloed is D itself (the stage at 0), never a morph of a silenced synth.
- **Latency:** none added (the stage is filter-based; nothing new is reported to the host).
- **CPU:** about 2 % of one core for the stage here (10 s of a 3-note chord with the sidechain: 0.57 s layered, 0.77 s with MORPH TO D at 50 %; hidden test `[cpu][live]`).
- **MORPH rack under D:** MORPH TO (its D side named for D's mode: "D · LIVE INPUT"), METHOD "GLOBAL IMPRINT", and MORPH. The graph shows the stage's **measured** 24 band gains (engine telemetry), D's mode, and why nothing happens when nothing can (MORPH off, D off, NO INPUT SIGNAL).
- **D MORPH retired:** the AUX rack's knob is gone; `analog.aux.morph` stays hidden and unread (older experimental sessions load). Its modulation destination is removed, so the destination list is again exactly as in 1.0 (golden −1 line).
- **Not covered:** per-voice modulation of MORPH (an LFO routed to MORPH, which is per voice) does not move the global stage; the macro, automation and USER macros do.

## Verification (this container: Linux, headless)

| Check | Result |
|---|---|
| DSP: LIVE through LEVEL / FILTER, mono, DC, NaN, MUTE / power ramps; MORPH TO D endpoints, silence, start / stop; realtime (no allocation, four modes, input on / off, exclusive toggling) | PASS |
| Product: the bus (off by default, auxiliary VST3, layouts), sidechain → D with no MIDI, the bus off with junk (nothing heard), other modes ignore it, drop / click / rack, state round trip, other modes' state kept; MORPH TO (default C, D drives the stage, one processor, solo, heard); bindings (MORPH TO moves the sound) | PASS |
| **ANDOR Analog's VST3 hosted in-process** (instrument, MIDI in, the bus, audio in → D → out) | PASS |
| Goldens: parameter schema (+AUX Mode's fourth choice, +Morph To), modulation destinations (−D MORPH) | intentional |
| Right-click → Assign to Macro through the knob's own press (the owner's report, fixed on `feature/aux-d` first) | PASS |

**Evidence:**
- **Screenshots** (the hidden screenshot test): `aux-13…16` LIVE INPUT disabled / no signal / signal / rack; `aux-17…19` MORPH TO C, D (NOISE), D (LIVE INPUT).
- **Demos** in `docs/demos/aux/live/` (with `live05-renders.txt`). The external tracks are **synthesized stand-ins**, since no real recordings are in the repositories; each goes through the plugin's sidechain bus:
  1. pad + plucked string, layered;
  2. the plucked string alone under a FILTER sweep;
  3. saw chord → formant voice, MORPH TO D swept 0 → 100 %;
  4. pad + bowed string, MORPH TO D 0 / 50 / 100 %;
  5. pad + drums, layered;
  6. D switched LIVE → RHYTHM → NOISE → KONTAKT → LIVE.

### NOT VERIFIED (owner tests; no DAW here)

- **Ableton Live (VST3):**
  1. ANDOR Analog on a MIDI track, audio on another.
  2. Choose the source in Analog's sidechain routing.
  3. AUX D → LIVE INPUT, power on: the input meter moves.
  4. Play notes: the two layered.
  5. MORPH rack → MORPH TO D, MORPH on: sweep MORPH.
- **Logic Pro (AU):** the plugin header's Side Chain menu → the source track; then as above. **auval** with the input bus runs on the Mac CI ("Validate ANDOR Analog AU").
- **REAPER (VST3):** send the source track to the instrument's track (a send to channels 3/4, the track at 4 channels), then map the plugin's input pins 1/2 - its only input, AUX D Live Input - to track channels 3/4 (Plug-in pin connector).
- **Standalone:** the bus takes the audio device's input when enabled; JUCE's standalone mutes input by default (no feedback).
- **Listening** with real material (vocals, cello, guitar, drums), offline bounce, latency compensation in a host.
