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
