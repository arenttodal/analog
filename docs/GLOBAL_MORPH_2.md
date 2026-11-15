# GLOBAL MORPH 2.0 — engineering record

Spec: "ANDOR ANALOG — GLOBAL MORPH 2.0" (14 November 2026). The user's ruling (15 November):
start with a mandatory gate that verifies and repairs D's audio routing **before** any new
MORPH functionality. Ambiguous CARRIER, OUTPUT and LEVEL MATCH behaviour is put to the owner
before those controls are implemented.

Branch: `feature/aux-live-input` (analog, OSP submodule at `938344d8`).

## Gate 0 — audit

1. **Branch / commit.** analog `19ab481` on `feature/aux-live-input` (D, LIVE INPUT, MORPH TO,
   D ARP, Learn 2.0). OSP `938344d8`. Nothing in OSP's shared code is involved so far.
2. **C (per voice).** `dsp/morph/LiveMorph.{h,cpp}` (566 lines): per-voice harmonic analysis at
   the note's known fundamental, residual, IMPRINT / FUSE / WEAVE, attack retention, formant
   shift, detail, level match, MORPH ONLY / LAYERED. State: `analog.morph.enabled / position /
   method / carrier / texture / attack / formants / detail / levelMatch / output` and
   `analog.organic.root` (ROOT). Patch: `Patch::morph`, `perVoiceOn()` false while MORPH TO D.
3. **D (global).** `dsp/morph/GlobalMorph.{h,cpp}` (306 lines): a zero-latency 24-band
   (third-octave, 70 Hz .. 14 kHz) spectral IMPRINT — the synth bus through a graphic EQ set
   to D's band energies, a level make-up, then an equal-power fade to D above 50 %. One instance
   per use: `globalMorph` (card C's external instrument), `rhythmMorph` (C RHYTHM), `auxMorph`
   (MORPH TO D). State: `analog.morph.target` (0 C, 1 D) only; the position is MORPH's own
   (`analog.morph.position`, shared). `analog.aux.morph` (old D MORPH knob) is kept, unread.
4. **Routing.** `Synth::render`: voices → (oversampling down) → `mixRhythm` (C RHYTHM) →
   `mixExternal` (C Kontakt + its GLOBAL MORPH) → `mixAux` (D: source → Kontakt latency
   alignment of everything before D → D's stereo FILTER (`auxFilter`) → LEVEL / MUTE / SOLO /
   power ramp → MORPH TO D's stage `mixAuxMorph` (exclusive: D only through the stage) or added
   to the bus) → chorus → KALEIDOSCOPE → FX router (MAIN / SEND, CUSTOM graphs) → SUB → output.
   So MORPH sits before the global FX, as the spec requires. S = the bus before `mixAux`
   (A/B/C after the FILTER, C's buses), D = D after its FILTER and LEVEL.
5. **Reusable spectral parts.** GlobalMorph's band analysis / graphic EQ; LiveMorph's
   partial/residual model (note-aware, per voice: not reusable on a polyphonic bus as is);
   `dsp/fx` has an FFT-free effects library; JUCE `juce::dsp::FFT` is available to `product/`
   but `dsp/` is pure C++ (no JUCE) — a global FFT stage needs either a small FFT in `dsp/` or
   to live behind the engine. To be decided in gate 4 (recorded there).
6. **Latency.** GlobalMorph: 0 samples. D KONTAKT: the instrument's reported latency, the bus
   before D delayed to meet it (reported as C's + D's). Nothing else on D's path adds latency.
7. **Tests.** `AuxProductTests` (MORPH TO: C default, D drives the stage, D solo = D itself, the
   layered sound comes back at C), `[dmorph]` / `[live05]` renders, `GlobalMorph` DSP tests in
   `dsp/tests`. None measured D along its path or checked the endpoints against D itself.
8. **Reusable UI.** `MorphRack` (`ParameterRack`, copper/rose identity, MORPH TO selector,
   METHOD / CARRIER segments, five knobs, OUTPUT / LEVEL MATCH switch cells, ROOT) and
   `MorphGraph` (C: a concept view from the settings; D: the stage's live band gains).
9. **Missing.** D: METHOD (FUSE, WEAVE), TEXTURE, ATTACK, FORMANTS, DETAIL, CARRIER, OUTPUT,
   LEVEL MATCH; separate D parameters; carrier taps; a measured graph for S, D and the result.
10. **Risks.** C's sound (LiveMorph untouched); the shared GlobalMorph class (also C Kontakt
    and C RHYTHM: changes go behind options, off for them); D's layered behaviour with MORPH
    off; latency changes when switching C ↔ D; CPU of an FFT stage on top of the voices.

## Gate 1 — D input reliability

### Telemetry added

`Synth::AuxTelemetry` (`dsp/Synth.h`): D measured stage by stage over each host block —
**source**, after D's **FILTER**, at its **LEVEL** (power, MUTE / SOLO, LEVEL), the stage's
**D input**, the stage's **output**, the **FX input** — peak and RMS, plus D's state and its
availability for the stage. Accumulated in the audio thread with no allocation and published
through relaxed atomics once per host block (`publishAuxTelemetry`, called by the engine after
the block's renders). `AnalogEngine::auxStatus()` turns it into words for the rack.

States (measured, or deterministically so — never guessed): OFF, MUTED, NO LOOP / NO
INSTRUMENT, WAITING FOR MIDI (RHYTHM GATE KEYS, keyed NOISE, KONTAKT with no key and no
signal), WAITING FOR TRANSPORT (RHYTHM FREE on a stopped host), NO INPUT (LIVE INPUT), SIGNAL
ACTIVE (signal now, or within 2 s: a loop between its hits), SILENT (should sound, measures
silent).

### What the probe found (`[gm2][dpath]`, `product/tests/GlobalMorphTests.cpp`)

Every source mode reaches the stage (peaks, dBFS, keys held, MORPH TO D 50 %): RHYTHM source
−2.1 → filter −7.3 → level −13.0 → stage in −13.0; NOISE −6.6 / −11.1 / −14.1 / −14.1;
KONTAKT (the test instrument) −11.9 / −14.8 / −17.8 / −17.8; LIVE INPUT −10.3 / −11.2 /
−14.2 / −14.2. Signal was never lost between the source and the stage. The defects were:

1. **The message was wrong.** The rack's "D is silent" came from the imprint's own level
   follower, which only runs while the stage runs. It said "D is silent" at MORPH 0 % while
   the loop played (282 of 282 blocks), and said nothing better when D was in fact waiting for
   a key (RHYTHM's default GATE KEYS with no note held — the most likely case behind the
   screenshot), waiting for a stopped host transport, muted or off.
2. **100 % was not D.** The stage's position was multiplied by D's instantaneous activity, on
   top of the activity already inside the imprint. A sparse loop's activity falls between its
   hits, so at MORPH 100 % the synth came back between the ticks: the 100 % render differed
   from D itself by −2.4 dB (RHYTHM, ticking loop), −25 dB (NOISE), −32 dB (KONTAKT),
   −27 dB (LIVE INPUT) — the synth audibly present at the endpoint where only D belongs.

### The repair

- The stage's **availability** of D is now decided by what D is set to play, not by its
  level from moment to moment: on, heard (not muted / soloed away), and its source able to
  sound — RHYTHM with a loop played by the keys or on a running clock, NOISE, KONTAKT with its
  instrument ready. A quiet moment is still D (100 % = D, silences included); only an
  unavailable D gives the stage back to the synth (300 ms). Keyed sources are available before
  their first key, so no synth attack slips through at 100 %. LIVE INPUT is the one source no
  setting can vouch for: it is available while the sidechain has had signal within the last
  2 s (a singer's pause gives the synth back — the behaviour the existing
  `MORPH TO D LIVE: a silent input leaves the synth as it is` test pins — and the morph
  returns within a few ms of the voice; an input arriving together with the notes after a
  silence lets the synth's first ~15 ms through, by design).
- The position is no longer scaled a second time by D's level.
- The rack's MORPH TO D graph says D's real state (`D · RHYTHM · WAITING FOR MIDI: D is played
  by the keys: play a note to hear it`, `… WAITING FOR TRANSPORT`, `… NO INPUT`, `AUX D is
  off`, `D · RHYTHM · SIGNAL ACTIVE · each bar …`).

### Evidence (all executed here)

`[gm2][dpath]` — 2 test cases, all passing:

| Mode | D itself (RMS) | 0 % vs the synth | 100 % vs D itself (before → after) | 50 % vs synth / vs D |
|---|---|---|---|---|
| RHYTHM (ticks, GATE KEYS) | −38 dB | −235 dB (bit exact) | −2.4 dB → **−215 dB** | +0.1 / +5.5 dB |
| NOISE (keyed) | −29 dB | −235 dB | −25 dB → **−225 dB** | −0.2 / +3.0 dB |
| KONTAKT (test instrument) | −28 dB | −235 dB | −32 dB → **−226 dB** | +0.1 / +3.3 dB |
| LIVE INPUT (sawtooth, from the start) | −21 dB | −235 dB | −27 dB → **−50 dB** ¹ | −2.4 / +3.1 dB |

(Difference energy relative to the reference; ≈ −220 dB is float rounding, i.e. identical.
¹ the stage's 15 ms glide-in from power-up, left in the SPACE tail; LIVE INPUT's availability is measured.)
Also checked: at 100 % between a loop's ticks the output is D's silence (and its own SPACE
tail), not the synth; a stopped transport (RHYTHM FREE) leaves the synth exactly as it is; no
key → WAITING FOR MIDI in every block; MUTE / power → MUTED / OFF.

Renders (`docs/demos/gm2/`, 16-bit): `after-gm2-g1-1-synth`, `-2-d-itself`, `-3/4/5-morph-to-d-
0/50/100` (a pad × the ticking loop, GATE KEYS) with per-block telemetry CSVs (each tap's peak,
the state, the availability); `before-` / `after-dmorph-0x` — the older `[dmorph]` renders
(dense noise loop, GATE FREE) from the code before and after the repair: identical at 0 %, at
50 % / 100 % differing by −49 / −43 dB (that loop is dense; the sparse case above is where the
old stage failed).

### Known, not changed in gate 1 (decided in later gates)

- GLOBAL IMPRINT with D alone (no synth playing) at 50 % is silent: an imprint of nothing on
  the synth. FUSE / WEAVE and OUTPUT (LAYERED) will define D's own share at intermediate
  positions — one of the questions put to the owner.
- At 50 % IMPRINT is level-matched to D: a quiet D makes the 50 % point much quieter
  (−36 dB RMS against the synth's −19 dB in the gate-1 render). LEVEL MATCH's definition is
  the second question.

## Decisions (owner, 15 November 2026)

- **CARRIER on D: morph the carrier.** As on C: the selected oscillator(s) (OSC 1, OSC 2, or
  BOTH) are what morphs toward D; the other oscillator and card C stay dry between the ends and
  fade out toward 100 % (100 % is D alone). Implemented with per-oscillator taps from the
  voices (the carrier bus), never three labels on one summed bus.
- **OUTPUT = LAYERED on D: exact ends + morph.** 0 % is the synth and 100 % is D exactly, as
  in MORPH ONLY; between them the dry sources crossfade under the morph, gain-compensated (no
  build-up). (C's LAYERED — both dry sources at full level everywhere — stays as it is on C.)
- **LEVEL MATCH = ON on D: smooth between exact ends.** 0 % and 100 % are the sources exactly;
  between them the loudness glides between the synth's and D's measured levels (in dB, bounded
  ±12 dB, ~300 ms windows, nothing below −60 dBFS ever lifted). OFF: the morph's natural level.
