# EXPERIMENT: Kontakt as card C's external SoundSource + GLOBAL MORPH

Proof of concept, not a production feature: spec "ANDOR ANALOG - Kontakt external SoundSource +
global MORPH", MVP, 11 November 2026. Branch `feature/kontakt-external-morph` in both repositories.

## Gate 0: baseline and feasibility audit

### Baseline

| Repository | Base | SHA | Evidence |
|---|---|---|---|
| analog | `feature/satellite-rhythm` (newest validated line: SATELLITE + RHYTHMIC MORPH on USER MACROS, SHAPER DNA, V7) | `5ad966d` | full `ctest` 13 / 13 on Linux x86-64 (GCC 13) |
| OSP | `feature/satellite-rhythm` (ANALOG's pin) | `41edc0be` | the same run (`osp_visual_reference`, `unit`, `integration`, `regression`, ...) |

The experiment adds no SATELLITE or RHYTHM code. Its default branches (`claude/upbeat-allen-3i0jey`,
OSP `claude/charming-lovelace-n25fte`) are older lines and are not touched.

### What exists

- **No SoundSource abstraction.**
  - Card C's types are `HybridType` (SAMPLE, OSCILLATOR, NOISE, INSTRUMENT PATCH).
  - SAMPLE and INSTRUMENT PATCH play prepared zones (`dsp/SampleSource.h`).
  - A dedicated bridge is therefore needed: `product/Source/ExternalInstrument`.
- **Card C's MIDI.**
  - The shell's per-card arpeggiator puts each lane on MIDI channel 1 + its card mask; `Synth::cardsFor` decodes it.
  - With no lanes, every note sounds every card.
  - Card C's stream is every event whose cards include C: its arpeggiated lane when C arpeggiates, the held keys when C is passed through.
  - The musician's raw keys (SUB's stream, `setPlayedKeys`) are a separate buffer and are not used.
- **A bus point exists** in `Synth::render`, after the decimators and before the Juno chorus, KALEIDOSCOPE and the FX router: RHYTHMIC MORPH's loop already joins there, at the host rate.
- **Latency and state:**
  - `InstrumentEngine::latencySamples()` is reported to the host by the shell, re-read on its timer.
  - `AnalogEngine::saveState` / `restoreState` carry the engine's own XML children.
- **Plugin hosting is off.** JUCE 8.0 bundles the VST3 SDK, but `JUCE_PLUGINHOST_VST3` was never set. It is now set for ANDOR Analog only; ANDOR OSP and the shell host nothing.
- **This machine:**
  - Linux container, no DAW, no Kontakt, and no VST3 plugins installed (`~/.vst3`, `/usr/lib/vst3`, `/usr/local/lib/vst3` absent).
  - Xvfb is available but has no window manager.
  - **Kontakt validation is BLOCKED here** (it needs an installed Kontakt, on macOS or Windows).

### The surrogate

`product/tests/surrogate/SurrogateInstrument.cpp` builds **ANDOR Test Instrument**, a small VST3 instrument for the tests only (never shipped). It is hosted through the same VST3 path Kontakt would take: file, scan, instantiate, bus layout, prepare, MIDI, stereo audio, editor, native state and reported latency.
- Two "instruments" are chosen in its own editor, as a Kontakt instrument is chosen in Kontakt's.
- **LEGATO STRINGS:** monophonic legato. An overlapping note glides (80 ms) without a new attack, and a release falls back to the held note.
- **EVOLVING PAD:** polyphonic, with a slow filter drift.

What it proves and what it cannot: it validates ANDOR's side of the protocol. It says nothing about Kontakt's own behaviour (its threading, load time, state size, licensing dialogs, legato scripts).

## Gate 1: host smoke test (surrogate-validated; Kontakt BLOCKED)

`product/Source/ExternalInstrument.{h,cpp}`:
- **Locating:** only Kontakt's VST3 is looked for (`Kontakt*.vst3` in the platform's standard VST3 folders and one level down). For tests and the surrogate demo, the file `ANDOR_EXTERNAL_INSTRUMENT_VST3` names is used instead.
- **Creation:** on the message thread, prepared there (buses: no audio in, main out stereo, every other output off), then handed over through `osp::ModelExchange`.
- **The audio thread:**
  - takes it at a block boundary and never creates, frees, scans, reads files, allocates or locks;
  - splits larger blocks into the prepared size;
  - gives the plugin a transport snapshot (its own `AudioPlayHead`, never the DAW's pointer).
- **Retirement (UNLOAD, a new instance, card C another type):** sustain off and all-notes-off are sent, a 10 ms fade runs, and then it is no longer processed. Instances are freed on the message thread once the audio thread has moved past them.
- **The editor:** the plugin's own editor in a floating window. Closing it (its close button, Escape) deletes only the editor; the instrument keeps playing.

| Test (`andor_analog_tests "[external]"`) | Result |
|---|---|
| finds the instrument (override), instantiates the VST3, reports 0 latency | PASS (surrogate) |
| Kontakt not installed: an actionable message, nothing loaded, silence, no crash | PASS (this machine) |
| a held note in stereo (L and R differ); release tail after the note-off with no note held; gone 0.8 s later | PASS (surrogate) |
| editor created by the plugin, closed (sound continues, still loaded), reopened, closed by UNLOAD | PASS (surrogate; window kept off the desktop: Xvfb has no window manager) |
| UNLOAD mid-note: faded and silent within 20 ms, freed; card C inactive: the same fade, then not processed | PASS (surrogate) |
| real Kontakt 7 / 8: instantiate, open its UI, load a licensed instrument, close the UI with audio continuing | BLOCKED: requires installed Kontakt |

## Architecture (as built)

```
DAW MIDI ─> shell (per-card arpeggiator lanes: channel 1 + card mask)
   ├─ every event ─────────────> Synth voices (A, B; C-only notes skipped while card C is EXTERNAL)
   │                              └─ voices' sum ─> decimators ─┐
   └─ card C's lane (cards ∋ C) ─> channel 1 ─> ExternalInstrument (hosted VST3, Kontakt)        │
                                  stereo bus ─> × card C LEVEL · MIX share · MUTE/SOLO ─────────┤
                                                                                               v
                              Synth::mixExternal:  MORPH off: A+B + C   |   MORPH on: GlobalMorph (A+B, C, position)
                                                   (A+B delayed by the instrument's reported latency)
                                                                                               v
                                         Juno chorus ─> KALEIDOSCOPE ─> FX router (DRIVE, MOVEMENT, ECHO, SPACE) ─> out
```

- **The integration boundary** is where RHYTHMIC MORPH's loop already joins: after the voices' sum and decimation, at the host rate, before the shared effects.
  - Kontakt goes through ANDOR's chorus and effects like the cards.
  - It is not filtered per voice, does not join the SEND bus, and is never multiplied by polyphony.
- **Off is off.** With card C on any other type and nothing ever loaded, `Synth::mixExternal` returns at once: the DSP goldens, the legacy hashes and the product's "EXTERNAL not chosen" comparison are bit-identical.
- **Files:**
  - DSP: `dsp/morph/GlobalMorph.{h,cpp}`, `dsp/Synth.{h,cpp}` (`setExternal`, `mixExternal`), `dsp/Patch.h` (`HybridType::external`).
  - Product: `product/Source/ExternalInstrument.{h,cpp}`, `AnalogEngine.{h,cpp}` (lane, bus, state, latency), `AnalogParameters.cpp` (EXTERNAL appended), `Product.cpp` (state 22, card C's LEVEL only), `AnalogWellOverlays.cpp` (the EXTERNAL panel).
  - Tests: `product/tests/ExternalInstrumentTests.cpp`, `product/tests/surrogate/SurrogateInstrument.cpp`, `tests/GlobalMorphTests.cpp`, `tests/RealtimeTests.cpp`.

## Gate 2: card C's MIDI and legato

- **Card C's lane:** every event whose cards include C. That is its arpeggiator's lane when it arpeggiates, and the held keys when it is passed through. It is sent on channel 1, at its own samples; SUB's raw keys are never sent.
- **The fixed overlap** (C3 0 ms, D3 450 ms, C3 off 600 ms, D3 off 1100 ms) reaches the instrument as four events, exact to the sample, in order, with velocities.
- **Audibly legato:** the stand-in's LEGATO STRINGS keeps one bow over the overlap, staying at 0.64 of the first note's level at the second note. With a 50 ms gap it starts a new bow from silence (0.09).
- **Arpeggiators:**
  - C arpeggiating (eighths) while A and B hold: only C's arpeggio reaches the instrument, 0.25 s apart.
  - A arpeggiating while C holds: the instrument gets the held chord once (3 note-ons).
- **Limitation:** on the shell's first block after its lanes change, it can still route one event the old way (seen at start-up). Tests warm up first.

## Gate 3: the bus

- **Level:** card C's LEVEL is exact (−6 dB → 0.501 of the amplitude, twin renders). MUTE silences it. Its release tail rings after the last note-off.
- **Not per voice:** four keys reach the instrument four times, never per voice.
- **C2 is neutral:** with C2 on, the render is identical.
- **Switching away mid-note:** C fades out (10 ms). Sustain off, all notes off and all sound off are sent, so switching back finds nothing left sounding.

## Gate 4: GLOBAL MORPH

- **The method** (`GlobalMorph`, zero latency): a 24-band third-octave spectral imprint.
  - The carrier (A+B) is shaped by a graphic EQ whose gains take each band's energy toward C's.
  - Analysis is linked stereo; gains are clamped to −36 … +18 dB; an activity gate leaves A+B alone while C is silent.
  - Energy make-up steers the level to the geometric interpolation of the two buses.
- **The law:** 0 % is A+B exactly; 0–50 % the imprint deepens; 50 % is the full hybrid; 50–100 % is an equal-power fade to C; 100 % is C exactly.
- **What was tried first:** adding band-passed differences back onto A+B. Measured as almost a crossfade (14 dB from C's envelope at 50 %), so it was replaced.
- **The renders** (`docs/demos/kontakt-external-morph`; `andor_analog_tests "[external-renders]"` with `ANALOG_DEMO_DIR`):
  - **The series:** `strings_dry_AB`, `strings_dry_C`, `strings_morph_OFF`, and `strings_morph_0` … `_100`. A+B is the init patch; C is the stand-in's LEGATO STRINGS on the same legato chord changes (each chord 60 ms over the next).
  - **Example 1:** that series (sustained analog with a legato line).
  - **Example 2** (`example2_chord_evolving_pad_morph50`): a full chord with the stand-in's evolving pad, at 50 %.
  - **Example 3** (`example3_C_arp_AB_hold`): C's own arpeggiator while A and B hold.

Measured (spectral-shape distance between third-octave profiles with levels removed, in dB; from `renders.txt`):

| MORPH | to A+B | to C | to the dry 50/50 crossfade | level vs A+B |
|---|---|---|---|---|
| 0 % | 0.00 | 9.67 | 5.95 | +0.0 dB |
| 25 % | 5.34 | 4.82 | 3.08 | −2.3 dB |
| 50 % | 11.33 | 3.16 | 6.42 | −4.1 dB |
| 75 % | 11.07 | 2.45 | 6.15 | −4.4 dB |
| 100 % | 9.67 | 0.00 | 4.55 | −4.3 dB (C's own level) |

- **50 % is not a crossfade:** its envelope is 3.2 dB from C's and 6.4 dB from the crossfade (which sits 4.6 dB from C). With the evolving pad on C (the G4 test): 2.6 dB from C, 13.3 dB from A+B.
- **The level** moves smoothly from A+B's to C's.
- **The DSP tests:** 0 % and 100 % are bit-exact, the sweep stays within +3 dB of the louder bus, the stereo image is kept, and a silent C leaves A+B unshaped.

**Listening notes.** These are not listening notes: nobody has heard these renders on monitors. What the measurements predict:
- 0 % is the analog chord.
- 25 % is the analog chord with its spectrum tilted toward the strings' (darker above 2 kHz, the formant region lifted).
- 50 % keeps the analog chord's harmonics, pitches and attack, with the strings' tone colour and their bow envelope moving it.
- 75–100 % fades to the strings.

Whether that is musically convincing is the open question a listening test must answer, with real Kontakt instruments.

## Gate 5: the session

- **Saved:** `<ExternalInstrument version="1" name=... state=base64>` with JUCE's `PLUGIN` description: the identity, plus the native state as the host saves it.
- **Restored:** opened, saved and opened again, it is the same instrument with the same instrument chosen in it (the stand-in's EVOLVING PAD, read back from its hosted parameter).
- **Missing** (saved with "Kontakt 8", opened without it):
  - card C says MISSING and why, A and B play, nothing crashes;
  - saving again writes the external instrument exactly as it was read.
- **Older sessions:** with no element, nothing is hosted, and card C's other types are unchanged.
- **Known limitation:** state is applied right after creation, on the message thread. Kontakt loads its library asynchronously after `setState`, so audio may arrive a moment later. Whether the same library and instrument come back depends on Kontakt and its licence: BLOCKED here.

## Gate 6: what this machine could run

- **Latency:** an instrument reporting 256 samples delays A+B by exactly 256, and the engine reports it to the host (re-read on prepare and on the engine's timer).
- **Rates and blocks:** 44.1 / 48 / 96 kHz × 64 / 128 / 512-sample blocks, all with MORPH at 50 %. C sounds, nothing is non-finite, and the lane's events land on their exact samples.
- **Allocation:** the synth's external bus and GLOBAL MORPH never allocate (the `realtime` case: switching, morphing, moving, a latency).
  - `ExternalInstrument::process` uses only buffers prepared beforehand (a MIDI buffer reserved to 8 kB, audio buffers resized without reallocating). That was checked by reading the code, not by an allocation counter: the hosted plugin's own allocations would mix in.

| Cost, % of one core (this VM, 512-sample blocks, 48 kHz, two runs) | 4 notes | 8 notes | 16 notes |
|---|---|---|---|
| native (card C OSC 3) | 9.3–9.4 | 16.2–16.4 | 19.3–20.4 |
| hosted stand-in on card C | 8.0–9.3 | 14.7–16.6 | 15.4–18.0 |
| hosted stand-in + GLOBAL MORPH 50 % | 17.9–18.4 | 15.8–17.4 | 19.0–19.2 |
| GLOBAL MORPH alone (DSP, one stereo pair) | 1.6–2.2 at any position | | |

- **Memory:** loading the stand-in adds 5–10 MB resident the first time and ~20 kB after. Kontakt's own memory is its libraries' and is not measurable here.
- **Noise:** the figures vary by ±3 points from run to run; the stand-in is cheap, so they say nothing about Kontakt's CPU.

## Test matrix

| Item | Host / OS | Rate / block | Result |
|---|---|---|---|
| VST3 hosting path: scan, instantiate, buses, prepare, MIDI, stereo, editor, state, latency | headless tests, Linux x86-64 (surrogate VST3) | 48 kHz / 256; 44.1–96 kHz × 64–512 | PASS (surrogate) |
| Card C's lane, the fixed legato overlap, arpeggiator lanes | headless, Linux | 48 kHz / 256 | PASS (surrogate) |
| Bus, level, MUTE, C2 neutral, no stuck notes, release tails | headless, Linux | 48 kHz / 256 | PASS (surrogate) |
| Off path bit-identical (goldens, legacy hashes, product comparison) | headless, Linux | 44.1 / 48 / 96 kHz | PASS |
| GLOBAL MORPH: endpoints exact, 50 % not a crossfade, loudness, image, silent C | DSP tests + plugin, Linux | 48 kHz | PASS |
| State round trip ×2; missing plugin; older sessions | headless, Linux | — | PASS (surrogate) |
| Latency alignment and report | headless, Linux | 48 kHz | PASS (surrogate, 256 samples) |
| No allocation on the audio thread (synth bus, morph) | DSP realtime tests | 48 kHz / 512 | PASS |
| Card C's UI (LOAD / OPEN / UNLOAD, states, GLOBAL note) | headless editor; screenshots at 869 / 1448 / 1810 | — | PASS (`docs/screenshots/external`) |
| Editor window on a real desktop (open, close, Escape, audio continuing) | Xvfb has no window manager | — | logic PASS (off-desktop); desktop window BLOCKED here |
| Real Kontakt 7 / 8: instantiate, its UI, load a licensed instrument, legato library, state restoring the library | — | — | **BLOCKED: requires installed Kontakt** |
| Plugin-inside-plugin in Ableton Live (macOS), a second DAW, the standalone; save / reopen, bypass, stop / play, tempo change, offline bounce | — | — | **BLOCKED: no DAW here** |
| AU builds hosting Kontakt (Logic) | — | — | NOT BUILT: VST3 hosting only (`JUCE_PLUGINHOST_AU` off) |
| Offline bounce mode forwarded to the hosted plugin | — | — | NOT DONE (non-realtime is not forwarded) |

## How to try it with Kontakt (untested here: the expected steps)

1. Build ANDOR Analog from `feature/kontakt-external-morph` (VST3 / standalone; macOS: the AU too, which hosts Kontakt's VST3).
2. Kontakt 7 or 8 must be installed as VST3 in the standard folder: `/Library/Audio/Plug-Ins/VST3` on macOS, `C:\Program Files\Common Files\VST3` on Windows (its Native Instruments subfolder is searched too).
3. In ANDOR Analog, set card C's type to EXTERNAL and press LOAD KONTAKT. Card C shows the plugin's name; an absent Kontakt is reported in words.
4. Press OPEN KONTAKT and load a licensed instrument in Kontakt's own window. For legato, choose a legato patch and set its legato mode there. Close the window: Kontakt keeps playing.
5. Play: card C's notes reach Kontakt. Switch MORPH on and sweep it (0 % A+B, 50 % the hybrid, 100 % Kontakt). Card C's LEVEL sets Kontakt's level.
6. Save the DAW session, close it, and reopen it twice: check that Kontakt's instrument comes back (not an empty Kontakt).

For a demo without Kontakt, build `andor_test_instrument` and set `ANDOR_EXTERNAL_INSTRUMENT_VST3` to its bundle: LOAD KONTAKT then loads the stand-in.

## Known issues and limitations

- **Not verified:**
  - Kontakt itself, its licensing dialogs, its load time and memory;
  - nested hosting in real DAWs (some hosts or sandboxes may refuse a plugin hosting plugins, or its windows);
  - AU hosting.
- **Notes from the start:** a host that sends notes before LOAD finishes reaches nothing. Loading happens on the message thread, synchronously (the UI waits for Kontakt's own startup; how long that is was not measured here).
- **One instrument:** no multi-outputs, no keyswitch or CC handling beyond pass-through, no MPE.
- **GLOBAL MORPH:**
  - It is one mode. The MORPH rack's method, carrier, texture and other settings do not apply to it; the card says so.
  - FUSE / WEAVE (harmonic fusion, partial interleaving) are not attempted at bus level: a chord has no single pitch to fuse to.
- **Sends:** Kontakt does not join the SEND bus in MAIN + SEND.
- **Automation shift:** the card C type parameter gained a fifth choice, so its *normalised* automation value moved. Sessions recall by index; the 1.0 fixtures pass.

## Recommendation: CONDITIONAL

- **ANDOR's side is viable and cheap:**
  - the bus-level design keeps the off path bit-identical;
  - it never multiplies the instrument by polyphony and keeps legato overlaps intact;
  - GLOBAL MORPH is a real, measurable spectral hybrid at about 2 % of a core with no latency;
  - state survives a missing plugin.
- **Before production:**
  1. a real Kontakt session on macOS in Ableton Live, then Logic (AU) and a second DAW: instantiation, UI windows, state restoring the library, legato libraries, offline bounce;
  2. asynchronous loading (no UI stall) and forwarding the offline mode;
  3. a listening test of the 50 % hybrid against real orchestral libraries;
  4. a decision on Kontakt joining SEND and on its licensing terms for embedding in another product.
- **FUSE / WEAVE:** worth researching as an STFT cross-synthesis (magnitudes from C, the carrier's phases, with a latency-compensated dry path) only if listening shows the imprint's tone-colour transfer is not enough. A chord-wide pitch fusion has no single pitch to aim at; per-note fusion would need one Kontakt per note, which this design rejects.
