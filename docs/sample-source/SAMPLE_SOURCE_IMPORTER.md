# ANDOR Analog — Sample Source importer (MVP A1)

_31 October 2026. Kontakt-assisted native patch generator. The first target is one real
instrument: Evenant Engine — Bukkehorn 4.5. It is not a sampler, a Kontakt replacement or a
universal NKI player._

The evidence behind every mapping is in
[BUKKEHORN_A1_0_EVIDENCE.md](BUKKEHORN_A1_0_EVIDENCE.md).

## Using it

> **Three engines (1 November 2026).** The imported sounds now play as card C's **INSTRUMENT PATCH**
> (card C's TYPE dropdown; SOUNDS in its well picks one). Choosing a sound there changes only
> card C, never OSC 1, OSC 2, the filter or the effects. A recording dropped on C makes it a
> SAMPLE; the imported sound stays, dormant, until C is an INSTRUMENT PATCH again. The steps
> below still apply; where they say ORGANIC, read card C.

1. **Start the import.** On the empty ORGANIC dock, click **IMPORT KONTAKT PATCH**. (It is also
   in ORGANIC's card menu.)
2. **Choose the NKI.** Its sample folder is found beside it, named as the NKI's file list says
   (`… Bukkehorn 4.5 Samples`). If it is not there, choose the folder.
3. **Review.** The window lists every sound the NKI defines (lane and slot, name), its zones, and
   how many of its recordings are in the folder. **Saved Lanes** is the instrument as it was
   saved: four lanes layered. Choose the sound to start with.
4. **Import.** In the background, with a progress window:
   - the recordings are copied into ANDOR Analog's own store (named by their content);
   - a preset is written per sound into `Presets/Bukkehorn 4.5/` (they appear in the preset
     list);
   - the chosen sound starts playing in ORGANIC's place.
5. **Pick another sound.** ORGANIC's heading (`Bukkehorn 4.5 · A8 Voxx`) opens the import's
   sounds. A click plays another sound; the patch's other settings stay.
6. **Blend.** The header's A/B mix blends the sound with the oscillators. The oscillators may
   also be off: the sound plays alone.

## What is imported, and what is not

| Kontakt | ANDOR Analog |
|---|---|
| Each group's zones: recording, key and velocity range, root, fine tune, level, start | Played exactly (one zone's output correlates 0.9998 with its recording). Overlapping zones sound together, up to 8 per note. |
| Zone loop 1 (with its crossfade; "until end") | Played, forward with an equal-power crossfade, or back and forth. Loops 2–8 are never reached in Kontakt (loop 1 repeats forever) and are not used. |
| Group and instrument volume | The level, raised by 17 dB so a sound plays at its Kontakt level (checked within 0.1 dB) |
| The four lanes' saved selection | **Saved Lanes**: the four groups layered at their saved levels |
| Two volume AHDSRs (shared) | ANALOG's amp envelope: A 0.51 s, D 0.75 s to −12 dB, R 1.1 s (an approximation of their product) |
| SV LP2 15.3 kHz | The filter at 15.3 kHz, no resonance |
| Reverb send (SPACE, send slot 5) | ANDOR OSP's SPACE at 0.3 (an approximation, not that reverb) |
| Replika (Tape, return −∞), Plate, Convolve, SuperGT, Stereo Modeller, EQ3, group EQ1, the cutoff LFO | Not imitated; their values are kept in the manifest. ANDOR Diffusion Wash is a separate project. |
| Group pan, stereo recordings | Summed to mono: a voice in ANDOR Analog is one signal, as ORGANIC's |
| The 16 user presets | Empty in this NKI: none are invented |

## Saving and portability

- **Presets and sessions keep the source.** A preset (and a DAW session) keeps the imported
  source's *definition*: its name, its link to the NKI and its groups, and per zone its mapping
  and its recording's content name. It is a `SampleSource` element beside the parameters.
- **The recordings live in the store.** They are kept in ANDOR Analog's store (Application
  Support → ANDOR Analog → Samples → Imported), not in the Kontakt folder. A preset therefore
  does not depend on where the Kontakt instrument lives.
- **A missing recording is reported, never hidden.** Say a session is moved to another machine
  without the store. The status line then names how many recordings are missing ("relink to
  play them"). The rest of the sound still plays. `import::relink` finds them again by name in a
  folder and copies them into the store.
- **Other ANDOR Analog presets are unchanged.** Factory presets and older sessions have no
  `SampleSource`: they play as they did, and a reset clears the source.

## Architecture

**ANALOG (pure C++, `dsp/`):**
- `SampleSource` / `SampleZone`: an immutable zone map, on ANDOR OSP's `PlaybackSource`.
- `ZonePlayer`: one zone in a voice (ORGANIC's Hermite read, loops, a swap's tail).
- `Synth::setSampleSource`: the zones play in ORGANIC's place, through ORGANIC's level, the A/B
  mix, card B, the filter and effects. Real-time safe (tested: no allocation).

**ANALOG (product, JUCE):**
- `KontaktImport`: a bounded NKI reader. It reads the NI container, FastLZ, the preset chunks,
  groups, zones, loops, the file list, and the script with its saved values. It executes
  nothing, refuses encrypted or damaged files with a reason, and agrees with
  `tools/kontakt/nki_inspect.py` on all 333 zones.
- `SampleImport`: manifest + folder → definition (recordings copied into the store) →
  `SampleSource`; relink.
- `SamplePatches`: the presets and the import's record (its sounds, for ORGANIC's popover).
- `AnalogEngine`: the definition is saved and restored with the state, loaded on its own thread,
  and handed to the audio thread through ANDOR OSP's `ModelExchange`. Card B gets its name and
  outline; a recording dropped onto ORGANIC replaces the import.
- `SampleSourceUI`: the import flow, the review window, ORGANIC's sound popover.

**ANDOR OSP shell (opt-in; ANDOR OSP unchanged):**
- `Engine::saveState` / `restoreState`;
- `Engine::engineSourceName` / `removeEngineSource`;
- `ProductInfo::hybridHeaderPopover` / `hybridAction`;
- `GeneratedSource::periodic`;
- `ShellProcessor::productEngine()`.

**Analysis tools:** `tools/kontakt/nki_inspect.py` and `bukkehorn_manifest.py` (A1.0).

## Tests

- `SampleSourceTests` (DSP):
  - selection and overlap;
  - pitch from root and tune at 44.1 / 48 / 96 kHz;
  - a click-free loop joint; alternating loops; LOOP off;
  - levels; polyphony and release;
  - swaps; determinism;
  - plus a realtime no-allocation case.
- Product, `[samplesource]` (runs in CI, synthetic recordings):
  - presets generated, opened, played, saved and reopened;
  - layered lanes;
  - reset;
  - missing recording and relink;
  - ORGANIC's card, its sound popover, the A/B mix's ends;
  - a recording replacing the import;
  - the review's counts.
- Product, `[nki]`:
  - garbage, empty, absent and truncated files are refused with a reason;
  - with `ANALOG_BUKKEHORN_NKI`, the C++ reader matches the tools' manifest on every zone.
- Product, `[bukkehorn]` (with `ANALOG_BUKKEHORN_SAMPLES`):
  - Voxx loads complete;
  - a single-zone key correlates 0.9998 with its recording, at Kontakt's level within 0.1 dB;
  - overlaps layer as the NKI says; held notes sustain on the loops;
  - the generated Voxx preset plays complete through the instrument, its definition linked to
    NKI group 7.

## Milestones

- [x] **A1.0 Evidence and extraction:** the report and the manifest.
- [x] **A1.1 Sample playback:** Voxx, from its 13 recordings.
- [x] **A1.2 Patch generator:** a preset per sound, Saved Lanes, a hybrid starting point;
  persistence; relink.
- [x] **A1.3 Hybrid playback:** the A/B mix with the oscillators; sounds alone with the
  oscillators off.
- [x] **A1.4 Import interface:** NKI + folder, the review, ORGANIC's sound popover.
- [ ] **A1.5 Musical validation:** a test build on a Mac, every group from the full sample folder
  (only 18 of 201 recordings were available here), comparison with Kontakt by ear.

## Known limits

- **Mono only.** Each voice is one signal: stereo recordings and group pans are summed.
- **Shared settings only.** Per-group envelopes, EQ1 and filter values are known only as shared
  (the screenshots were taken with Edit All Groups), and are imported as shared.
- **Velocity.** Velocity → volume follows the ANALOG patch's own range: Kontakt's depth is not
  visible in the evidence.
- **No playheads.** The read heads are not drawn on the imported source's outline.
- **Kontakt 5+ NKIs only.** The NKI reader handles this kind of NKI (a Kontakt 5+ program, not
  encrypted). A library NKI, a monolith or a multi is refused with a reason.
