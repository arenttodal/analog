# SATELLITE + RHYTHMIC MORPH: architecture and gate log

This is spec revision 1.0 (11 November 2026): "ANDOR ANALOG: Satellite + Rhythmic Morph".
This document is the gate 0 map of the actual code, plus the record of each gate's decisions.

## Gate 0: baseline and integration branch

### Branch inventory (11 November 2026)

| Repository | Branch | SHA | State |
|---|---|---|---|
| analog | `main` | `4bc6c41` | 1.0.0 release merged |
| analog | `claude/release-candidate` | `2fc9856` | 1.0.0 RC (untouched) |
| analog | `claude/upbeat-allen-3i0jey` (default) | `1c0cb6d` | V6, SUB |
| analog | `feature/sub-popup-v7` | `53aa55d` | V7, MORPH lamp, sends through FILTER / MOVEMENT |
| analog | `feature/andor-learn-v1` | `ee777d2` | Learn: 2 commits on its own line |
| analog | `feature/user-macros-v1` | `2dc7873` | **newest**: V7 + SHAPER DNA + USER MACROS gates 1–4 (13 / 13 ctest) |
| OSP | `claude/charming-lovelace-n25fte` (default) | `0a418b9e` | Pedal Loop 1.0 and "Shared FX" (38 commits not on ANALOG's OSP line) |
| OSP | `feature/user-macros-v1` | `56fca28e` | ANALOG's pin: `claude/analog-release` + card arpeggiator + DNA + USER MACROS hooks |
| OSP | `claude/osp-card-arpeggiator` | `ef47245d` | ANDOR OSP's own per-card arpeggiator (1 commit, unmerged) |

**Decision: integrate on `feature/user-macros-v1`.**
- New work is on `feature/satellite-rhythm` in both repositories, stacked on that branch.
- The build compiles against the OSP tree pinned by the submodule.
- The OSP default branch's Shared FX line moves ANALOG's FX graph into OSP. It is a separate integration, not merged here (see "Open decisions").

### Baseline evidence

- **Full suite.** On `feature/user-macros-v1` (ANALOG `2dc7873`, OSP `56fca28e`) all 13 ctest programs pass:
  - `analog_dsp_tests`, 228 cases plus the 5 USER MACROS cases;
  - `andor_analog_tests`, OSP's visual reference, `unit`, `integration`, `regression` and the rest.
- **Audio goldens.** `analog_dsp_tests` holds every factory render as a golden (`tests/golden`, LegacyTests, FxBaselineTests). Every gate below re-runs them, and SATELLITE off must leave them bit-identical.

## The signal path as it is (`dsp/Synth.cpp`)

```
note -> voice (card mask: v.cards; all 7 unless the per-card arpeggiator's routing is on)
  sourceSample / modelPairSample        A: OSC 1 + noise   B: OSC 2 (+ ring)   C: hybrid (C1 + C2)
     cards' DAMPEN / EQ on each part     MORPH (when the note sounds A/B and C): C's part = morph
                                         contribution (MORPH ONLY: y - carrier; LAYERED: C + core)
  liveStages                             each card's GRANULAR / KALEIDOSCOPE on its part
  mode engines (TAPE FRAME ...)          A and B parts (C passes by)
  per-voice FILTER (ladder / OTA,        one nonlinear filter on the SUM of the parts
     high-pass, drive)                   (MAIN + SEND: a twin on the send-weighted parts)
  AMP, outGain, steal fade, SHAPER DNA
  voice pan (equal power, unity at centre: v.panL / panR)
  -> mixL / mixR (internal rate)           -> sends' tap (tapL / tapR)
decimators -> JUNO chorus -> KALEIDOSCOPE stage -> FX router (CLASSIC / CUSTOM / MAIN + SEND)
SUB: its own oscillator after the effects (or on MAIN before inserts in MAIN + SEND); never a voice
DC blocker, output gain, guard
```

**Attribution facts that shape SATELLITE:**
- A voice's cards share one nonlinear filter. There is no per-card post-filter stream, so a stereo sum cannot simply be split back into cards.
- With the per-card arpeggiator's routing on, a lane's note sounds only its card (`v.cards`). Such a voice is all of one card.
- MORPH replaces or fuses C into the carrier inside the voice. Its result is part carrier, part C.

## SATELLITE (gates 1–2)

### Where it acts

SATELLITE acts at the voice's output, at the voice pan. That point is after the voice's filter, AMP and SHAPER DNA, and before the mix, chorus, effects and sends' bus.

- **A voice of one card** (a lane's note, or a patch sounding one card) moves whole. The card's place replaces the voice's centre, and the voice's spread is kept, scaled by WIDTH. This is exact: no filter is added and nothing else is affected.
- **A voice of several cards, some moving** runs each moving card's part through a twin of the voice's filter. The twin has the same cutoff, resonance, model, high-pass and drive, and its own state, as the existing send twin does.
  - The twin's output, after AMP and SHAPER DNA (its low-pass and level, with their own state), is that card's stem. The stem goes to the card's place.
  - The voice less its moving stems stays at the voice's place. That remainder is the still cards plus the filter's nonlinear interplay.
  - When every card a voice sounds is moving, the last needs no twin. It is the voice less the others, so the interplay travels with it.
  - Twins run only while their card moves in that voice. They start from rest under a 20 ms glide.
- **SUB** is not a voice and is never moved.

**Measured leakage on one note sounding all three cards** (the "a note on all three cards" test). B is moved hard right; what of B stays on the left:
- −29.7 dB at hot levels (0.6 / 0.6 / −4 dB);
- −53 dB at quiet levels.

The ladder saturates the sum more than B alone. On the card lanes' own voices nothing stays: below −60 dB, the measurement floor.

### MORPH rule (deterministic, no new control)

- In a note MORPH fuses (it sounds A/B and C), C's part belongs to the carrier: OSC 1 means card A, OSC 2 means card B, and the pair means half to each.
  - C's own SATELLITE does not tear the fused note.
  - With every SATELLITE on C only, a fused note is bit-identical to SATELLITE off (tested).
- C's own notes (its lane, or C alone) move with C's SATELLITE.

### Sends (MAIN + SEND)

- A card's send moves with it. In a whole voice the send tap is panned as the dry; in a split voice each moving card's send stem is split as its dry stem.
- SEND stays wet, stereo and aligned: nothing is delayed and no latency is added.
- The route, the sends' filter twins and SUB's place are unchanged.

### Motion (`external/osp/src/engine/SatelliteMotion.h`, shared and pure)

| Mode | What it does |
|---|---|
| PAN | sine about OFFSET |
| ORBIT | PAN's travel, plus a rear cue on the far half: up to −3 dB and a one-pole darkening near 2.2 kHz. Perceptual only; no binaural or HRTF claim. Mono is never cancelled (tested within 3.5 dB of PAN). |
| BOUNCE | a soft square, tanh of a sine |
| BOUNCE + NOTE | each new note of the card takes the other side, held for the note |

- **Timing:**
  - SYNC: the host's quarter notes, sample-accurate from the block start. With the transport stopped, a local clock starts at the first note, as SHAPER DNA's does.
  - FREE: its own accumulator, 0.02–10 Hz.
- **Positions:** computed per control chunk (8 host samples) and interpolated per sample, with 6 ms of smoothing so a mode or setting change never jumps. ON / OFF glides over 20 ms.
- **Law:** the voices' own equal-power law, unity at the centre.
- **DEPTH** is travel within the room OFFSET leaves, never clipped at an edge. **WIDTH** is the card's own voice spread.

### Off means off

- With every card off and the glides done, the voices take the original code path. The old renders and the 244 DSP cases, goldens included, are unchanged.
- After a card is switched off, the voices are bit for bit the original. Only the output's DC blocker and decimators still remember the moved past, decaying (below 1e-4 at 50 ms).

### Cost (16 held voices, this VM, % of one core, ±3 noise)

| | 1x | 4x |
|---|---|---|
| off | 7–9 | 19–24 |
| moving whole voices | 5–7 | 17–20 |
| every voice on three cards, all moving (two twins each) | 9–12 | 26–32 |

There are no allocations (the `realtime` case: cards on and off, modes, notes on single cards and on all three).

## SATELLITE's controls (gate 3)

- A **SATELLITE** key on the per-card arpeggiator's title row (shared `ArpSection`, opt-in through `ShellProduct::satellite`; ANDOR OSP shows nothing). Its light switches the cards (off: all, remembered; on: the remembered ones, else the edited card) as one undo step; its name opens the panel. Space switches, Return opens.
- The panel (`product/Source/SatellitePanel`): A / B / C chips (showing, never switching), the card's light, PAN / ORBIT / BOUNCE, SYNC / FREE, CYCLE / NOTE (BOUNCE only), RATE as a division (SYNC) or Hz (FREE), DEPTH, WIDTH, PHASE, OFFSET.
- The path display draws the card's real path, and its marker is where the audio has the card now (`Synth::satelliteView`, the engine's own telemetry). With nothing moving the marker rests, hollow, where the path starts. It never animates on its own.
- Parameters (version hint 35, state version 20): `analog.satellite.{a,b,c}.{enabled, mode, sync, division, rate, depth, width, phase, offset, retrigger}`.

## RHYTHMIC MORPH (gates 4–7)

### Analysis (`dsp/rhythm/RhythmAnalysis`, pure C++, a loader thread)

- **Onsets:** log spectral flux on a 5 ms hop, an adaptive threshold, each onset refined back to its attack. Each carries its strength, its compressed level and its **rise** (the level's jump over the 60 ms before it).
- **Tempo:** the onset curve's autocorrelation, every octave of the period scored, with a gentle prior around 120 BPM. HALF / DOUBLE are manual corrections and are never applied automatically.
- **Loop:** when the length is a whole number of bars at a candidate (power-of-two beats within 4 %, the ×3 family, any count within 0.3 %), the tempo is made exact, so a synced loop never drifts. Meter (3 or 4) and the downbeat come from the attacks' levels and a low band below 120 Hz.
- **Kind:**
  - RHYTHMIC needs a clear pulse, at least four strong onsets that are real attacks (their level at least doubles), and most of them on a sixteenth or triplet grid.
  - A sung vowel's vibrato moves the flux curve periodically with no attack; it read as 125 BPM before the attack rule. Now it is UNSURE (tested).
  - Rain, ambience and single hits are UNSURE and play FREE.
- **Envelopes for C RHYTHM:** the level (RMS) and the transients (a pulse at each attack, as loud as it is).
- **Cost:** a 60 s recording analyses in 0.39 s. Nothing is stored: the analysis is recomputed when a recording loads (`Analysis::version` marks a change of result).

### The loop (`dsp/rhythm/RhythmLoop`)

- **Chosen method: slices on the grid, not a time-stretch.**
  - The loop is cut at its onsets. Each slice starts exactly where the host's grid puts its attack (2 ms of pre-roll before) and plays at its original speed, so its pitch and transient are the recording's own.
  - A slower host leaves a short gap: the slice fades by the next slice's pre-roll, judged from where the head really is. A faster host cuts the slice with a 3 ms crossfade.
  - Zero latency, no allocation, block-size independent. Seeks and host loops are found from jumps in the ppq clock.
  - Signalsmith Stretch (in OSP for offline work) was not used. Real time, it adds latency, and it smears the transients this feature is about. Recorded as a decision below.
- **Measured** (`rhythm_break_C_audio_and_AB_filter_90_120_140.wav`): a 100 BPM break on hosts at 90, 120 and 140 BPM. The attacks land on the host's eighth grid with a median of 1.8–2.1 ms and a 90th percentile of 2.3–2.6 ms (the detector's own edge offset included).
- **FREE:** UNSURE sources, or SYNC off, play round the whole recording at its own speed (5 ms crossfade at the turn).
- **Clock:** the host's ppq while the transport runs; stopped, the local clock starts at the first note, as SATELLITE's and SHAPER DNA's do.

### In the instrument

- **Card C's recording is the loop** while C's type is SAMPLE (OSC 3, NOISE and INSTRUMENT PATCH leave it dormant). It is analysed when published, handed over through `ModelExchange`, and a newer recording makes an older answer stale.
- **AUDIO / MOD / BOTH:**
  - AUDIO: the loop is heard and C RHYTHM rests at 0.
  - MOD: the loop runs silent on the grid and is only felt.
  - BOTH: heard and felt.
  - While the loop is heard (AUDIO, BOTH), the keys hand it the recording. In MOD card C's keys keep playing it, so MORPH keeps its C. That is RHYTHMIC MORPH proper: the recording's own rhythm moves the morph toward the recording (`rhythm_rain_unsynced_modulates_morph.wav`).
  - **MORPH with the loop heard** (fixed after the first build: MORPH did nothing in AUDIO / BOTH, since the voices had no C left). With MORPH on, the loop and A + B now meet in the GLOBAL MORPH (`dsp/morph/GlobalMorph`, from the Kontakt experiment), at MORPH's position:
    - 0 %: A + B alone, exactly;
    - 50 %: A + B in the loop's spectral envelope (its colour and rhythm on the oscillators);
    - 100 %: the loop alone, exactly.
    - MORPH off: the loop beside the voices, as before, bit for bit. Switching it crossfades in 10 ms. The MORPH rack's method and carrier do not apply to it (it is the bus-level morph). In MOD, MORPH is the voices' own, unchanged.
    - Tests: `RHYTHMIC MORPH: MORPH acts on the heard loop*` and `*MORPH in MOD*`.
- **Hearing the loop:**
  - Gated by card C's notes: the highest AMP among C's voices, so it starts with the attack and ends with the release.
  - At card C's LEVEL, MUTE and SOLO, and at SATELLITE C's place.
  - It goes to MAIN only, not to C's send. The rear cue of ORBIT is not applied to it (its place only).
- **C RHYTHM:**
  - A 13th modulation source (`osp::mod::Source::cRhythm`, appended).
  - It is offered only when a product asks (`ProductInfo::rhythmSource`). ANDOR OSP's lists keep their twelve and its golden is unchanged.
  - The value is the loop's level or transients at the playhead, above SENSITIVITY's threshold (0.6 × (1 − sens)), through ATTACK / RELEASE, times DEPTH. It is one value for the instrument, set before each control update.
- **Off:** with RHYTHM off, or with nothing loaded, the instrument is the one before, bit for bit:
  - the synth tests;
  - the legacy golden hash of `factory/Init` with every new setting away from its default but off, and a loop loaded (`baseline_identical_off_render.wav`);
  - MOD with no route sounds exactly as off.
- **Parameters** (version hint 36, state version 21, nothing to migrate): `analog.rhythm.{enabled, audio, sync, tempo, shape, attack, release, sensitivity, depth}`. `sync` is SYNC | FREE.

### Card C's controls (gate 7)

- A **RHYTHM** key in card C's well, under the TYPE dropdown, shown while card C plays its recording. Its light switches RHYTHM (one undo step); its name opens the panel beside it. It is not a fourth card and not a page.
- **The panel** (`product/Source/RhythmPanel`):
  - The display: the loop as analysed (level, beat and bar grid, attacks, SENSITIVITY's threshold), with the audio thread's playhead and C RHYTHM's value (telemetry, not animation).
  - SYNC | FREE and HALF | DETECTED | DOUBLE: enabled only for a loop with a pulse, and the tempo only in SYNC.
  - AUDIO | MOD | BOTH.
  - ENVELOPE | TRANSIENT and SENS / ATTACK / RELEASE / DEPTH: enabled only while something feeds on C RHYTHM.
- **Quick routes:** C RHYTHM to LEVEL A, LEVEL B, FILTER and MORPH, each one step (again: removed).
  - A new route takes the first free slot: levels at −60 % (a pump), FILTER +40 %, MORPH +50 %.
  - It switches RHYTHM on and makes it feed: AUDIO becomes BOTH. MORPH chooses MOD and switches MORPH on, because a heard loop would take the recording MORPH needs.
- **Screenshots:** `docs/screenshots/rhythm` at 869, 1448 and 1810.

### Cost (16 held voices, this VM, % of one core)

| | 1x | 4x |
|---|---|---|
| RHYTHM off | 4.6–6.7 | 13.2–14.5 |
| AUDIO | 5.1–5.6 | 13.7–14.8 |
| BOTH + a route | 5.2–7.2 | 13.8–17.6 |

- The difference is inside the run-to-run noise.
- There are no allocations (the `realtime` loop-player case).

## Gate 8: demo renders (`docs/demos/satellite-rhythm`)

Produced through the whole instrument (ShellProcessor, a host playhead, the real per-card arpeggiators), from seeded synthetic sources (a 120 BPM clock, a syncopated 4-bar break at 100 BPM, rain):
- `andor_analog_tests "[demo-renders]"` with `ANALOG_DEMO_DIR`;
- `analog_dsp_tests "SATELLITE + RHYTHMIC MORPH off*"` for the baseline.

`renders.txt` beside them holds the peaks and the measurements.

| File | What it shows | Measured |
|---|---|---|
| `satellite_a_chord_b_center_c_orbit_120bpm.wav` | A's chord placed left, B centred, C's sixteenths orbiting once a bar | strongest stereo motion 0.54 Hz (a bar at 120 is 0.5 Hz) |
| `satellite_bounce_three_unlinked_arps_140bpm.wav` | three unlinked arpeggiators, each card BOUNCING (C per note) | balance −0.76 … +0.89, strongest motion 3.45 Hz (C's 1/8T notes alternating: 3.5 Hz) |
| `satellite_orbit_mono_compat_comparison.wav` | ORBIT stereo, ORBIT folded to mono, off folded to mono | mono fold against off: mean −1.4 dB, range −2.1 … −0.7 dB (no cancellation) |
| `rhythm_clock_C_modulates_AB_amp_120bpm.wav` | a clock on muted card C pumping LEVEL A and B (MOD) | 9 dB level swing; deepest dips on 1 and 3 (the clock's thumps) |
| `rhythm_break_C_audio_and_AB_filter_90_120_140.wav` | a 100 BPM break heard on hosts at 90, 120 and 140, opening the filter (TRANSIENT) | attacks on the host grid: 90th percentile ≤ 2.6 ms at every tempo |
| `rhythm_rain_unsynced_modulates_morph.wav` | rain (UNSURE: FREE, transport stopped) on C's keys, moving MORPH | the analysis: not rhythmic (FREE) |
| `combined_cinematic_satellite_plus_rhythmic_morph.wav` | the break felt through MORPH and LEVEL A, SATELLITE on all three cards, SPACE | — |
| `baseline_identical_off_render.wav` + `.hash.txt` | `factory/Init`, every new setting off, a loop loaded | FNV-1a 9913012343000941499 = the hash stored before these features: IDENTICAL |

The measurements are analysis, not listening. Nobody has listened to these renders on monitors in a DAW. That is still to do (see the decisions below).

## Open decisions (for the product owner's review)

1. **OSP's default branch (Shared FX, Pedal Loop).** It is not merged into ANALOG's OSP line. It moves ANALOG's FX into OSP and needs its own integration pass.
2. **Stagger defaults.** All PHASEs default to 0°. A / B / C at 0° / 120° / 240° would make a three-card motion musical out of the box; that is not done yet.
3. **ORBIT's rear cue strength** (−3 dB, darkening near 2.2 kHz) is a first proposal, set by measurement, not yet by a listening test.
4. **The route-source list grew (ANDOR ANALOG only).** C RHYTHM is the 14th choice of "Mod n Source", so that parameter's *normalised* value for an existing source changed. Sessions recall by index: the same source and the same sound (the 1.0 fixtures pass). A DAW automation lane drawn on "Mod n Source" would now point a little differently. Automating a route's source is rare; the alternative (a separate parameter for C RHYTHM routes) was judged worse.
5. **Time-stretch.** The loop follows the host by slices on the grid, not by Signalsmith or a phase vocoder: zero latency, transients untouched. Pads or sustained loops that must stretch smoothly would need a real-time stretch with latency. That is a later option, not in V1.
6. **The loop's routing.** It plays on MAIN only (not C's send), takes SATELLITE C's place but not ORBIT's rear cue, and is gated by card C's notes (it does not run free under the transport alone).
7. **The analysis is not saved** with the session. It is recomputed when the recording loads (0.39 s for 60 s of audio). HALF / DOUBLE are saved; a manual BPM or downbeat override is not offered in V1.
8. **MOD keeps the recording on card C's keys** (so the voices' MORPH has its C); AUDIO and BOTH hand it to the loop, and there MORPH is the GLOBAL MORPH with the loop. A product owner may prefer otherwise.
9. **Quick-route depths** (levels −60 %, FILTER +40 %, MORPH +50 %) are first proposals.
10. **Not validated in a DAW.** Automated tests, headless renders and the measurements above only. No DAW session, host-automation pass or listening test on monitors has been done, and none is claimed.
