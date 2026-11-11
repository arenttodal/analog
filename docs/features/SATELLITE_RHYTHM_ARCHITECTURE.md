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

## Open decisions (for the product owner's review)

1. **OSP's default branch (Shared FX, Pedal Loop).** It is not merged into ANALOG's OSP line. It moves ANALOG's FX into OSP and needs its own integration pass.
2. **Stagger defaults.** All PHASEs default to 0°. A / B / C at 0° / 120° / 240° would make a three-card motion musical out of the box; that is not done yet.
3. **ORBIT's rear cue strength** (−3 dB, darkening near 2.2 kHz) is a first proposal, set by measurement, not yet by a listening test.
