# ANDOR ANALOG — engine and integration specification (as built)

This describes what is implemented, how, and where it departs from the implementation
specification of 28 October 2026 (and why). The sound reference ("Prophet-5 V oscillator and
filter notes") was used for architecture and musical direction only; no code, table or asset
from any other product is used, and none of its formulas were adopted without checking them.

## 1. Where it lives

- **arenttodal/analog** (this repository): the engine (`dsp/`, pure C++20, no JUCE), the plugin
  product (`product/`), tests, tools, docs.
- **arenttodal/OSP** (submodule `external/osp`, branch `claude/andor-analog-shell`): the shared
  interface and shell, extended with opt-in, OSP-neutral extension points (OSP HANDOFF.md, "What
  a product may add"), and the product's registry entry. The plugin is built inside ANDOR OSP's
  CMake project (`ANDOR_EXTERNAL_PRODUCTS`), exactly like `products/*`.

The repository split was the owner's decision (it keeps the licensed artwork in ANDOR OSP's
repository, HANDOFF rule 12).

## 2. Signal flow (per voice, at 4x the host rate)

```
OSC 2 (saw / tri / pulse, KEYBOARD, LO FREQ) ──┬────► mixer ─► the voice's recording ─► ONE SHOT / GRANULAR ─► REIMAGINED ─► FILTER DRIVE ─► 4-pole ladder ─► VCA ─► pan ─┐
          │ wrap (hard sync)                   └─► Poly Mod ─┐  ▲          (1.5 s)        (grains: POS, SIZE,    (KALEIDOSCOPE,                     ▲  cutoff       ▲          │
          ▼                                   filter env ──► │ noise                       DENS, TUNE, SPREAD,   TAPE FRAME, TOYBOX,              filter env, keys,  amp env      │
OSC 1 (saw / pulse) ◄── pitch, width ◄───────────────────────┘                             KEY DNA)              MIRAGE)                          LFO, Poly Mod, ANALOG, DRIFT  │
                                                                                                                                                                         ▼
  sum of voices (4x) ─► half-band 4x→2x ─► half-band 2x→1x ─► KALEIDOSCOPE's stage ─► DRIVE ─► MOVEMENT (TAPE, CHORUS, PULSE, SHAPER) ─► ECHO / SPACE ─► DC block ─► output guard
```

One LFO for the instrument (as on the original; ANDOR OSP's LFO 1, optionally one per note),
modulating both oscillators' pitch and pulse width and the cutoff; its amount (and the mod wheel,
which adds the rest of the way to full) sets how much of the per-destination depths is applied.

Since version 2 the voice records its own sound between the mixer and the filter, so ANDOR OSP's
granulator and REIMAGINED modes work on it live (the filter, the envelopes, Poly Mod and the LFO
still act on the result). GRANULAR's POS is how far back in the recording the grains are taken;
a grain never reads ahead of what has been played (at a note's start it reads just behind the
sound). TAPE FRAME, TOYBOX and MIRAGE are ANDOR OSP's own engines, unchanged, in their granular
form (they transform the stream and steer the grains); KALEIDOSCOPE is its per-voice parts plus
ANDOR OSP's resonator and formant stage on the mix, tuned to the harmonics of each phrase's first
note. MOVEMENT's DRIFT is ANDOR OSP's per-voice wander; its other modes, DRIVE, ECHO and SPACE are
ANDOR OSP's PostProcessor.

## 3. Oscillators (`dsp/Oscillator.h`, `dsp/PolyBlep.h`)

- **Phase**: double-precision accumulators, one per oscillator (they do not share a phase).
  Pitch: `f = 440 · 2^((note − 69 + 12·oct + semi + cents/100 + bend + LFO + ANALOG)/12)`, computed at
  control rate (every 8 host samples) with `std::exp2`, clamped to `dt ≤ 0.45` per internal sample.
  Audio-rate FM (Poly Mod) multiplies `dt` by `exp2Fast` (7th-order, ≤ 0.003 cent error).
- **Waves**: saw `2p − 1`; pulse `±1` with its DC (`2w − 1`) removed, width 2..98 %; triangle
  `−1 → +1` at `p = 0.5` → `−1`. Sums are scaled by 1/√n to keep a steady level.
- **Band-limiting**: every discontinuity is found with its exact sub-sample time `d` by walking
  the phase through the sample (wrap, triangle peak, pulse edge, sync reset), and corrected with a
  **4-point B-spline BLEP** (steps) or **BLAMP** (slope changes), with two samples of look-behind:
  the output is delayed by two internal samples (≈ 0.01 ms), which lets an unpredictable sync
  reset take the same exact correction as a regular edge.
  - Step residual: `r(x) = Φ(x) − H(x)`, Φ the integral of the cubic B-spline (support −2..2):
    for x ≤ −1, `Φ = (x+2)⁴/24`; for −1 ≤ x ≤ 0, `Φ = 1/2 + 2x/3 − x³/3 − x⁴/8`; odd: `r(−x) = −r(x)`.
  - Corner residual: `R(x) = ∫ r`: for x ≤ −1, `(x+2)⁵/120`; for −1 ≤ x ≤ 0,
    `7/30 + x/2 + x²/3 − x⁴/12 − x⁵/40`; even.
  - A step of height h at distance d before sample n adds `h·r(d−2), h·r(d−1), h·r(d), h·r(d+1)`
    to samples n−2 … n+1 (`BlepLine`). Unit-tested against a numerical integral of the spline.
  - Why not the two-point PolyBLEP of the reference notes: measured, it left −60 dB of aliasing at
    the top of the keyboard even at 4x; the B-spline's sinc⁴ spectrum gains ~13 dB more there at the
    same oversampling (docs/AUDIBLE_QA.md). The reference's sign conventions were not adopted: every
    residual's polarity is fixed by tests.
- **Triangle**: the naive triangle plus BLAMP corrections at its corners (slope changes of
  ±8·dt) — an analytic band-limited triangle; no leaky integrator, so no DC drift and no level
  change with pitch (tested: level within 15 % from A2 to C7).
- **Hard sync**: OSC 2 runs first; its wrap time `d` within the sample splits OSC 1's sample: OSC 1
  walks to the reset (its own edges corrected on the way), takes a step (and corner) correction of
  `naive(0) − naive(p_at_reset)`, then walks from 0 for the rest. Tested on the master's harmonic grid.
- **OSC 2 KEYBOARD off**: pitch as for C4 (plus its tuning, the bend and TUNE). **LO FREQ**: seven
  octaves down (C4 → ~2 Hz), for Poly Mod.
- **Free-running phases**: a voice slot's oscillators keep turning while silent (advanced by the
  elapsed time when the slot plays again); `analog.phase` RESET restarts them at every note.
- **Noise**: per-voice `osp::Prng` (seeded from the variation seed and the slot), uniform white.

## 4. Mixer and DRIVE

`mix = OSC1 · L1 · g1(BLEND) + OSC2 · L2 · g2(BLEND) + noise · N`, where BLEND crossfades with equal
power and leaves both oscillators at their own levels at 50 % (`g = min(1, √2·cos/sin θ)`). Since
state version 3 the oscillators are mixed by their LEVELs: BLEND is not on screen and rests at 50 %;
an older session's or preset's BLEND is folded into the two levels (`foldBlend`: the same sound).
The mix enters the filter at −7 dBFS per oscillator (×0.45); DRIVE adds up to +24 dB before the
saturator and takes about half back after it; BIAS offsets the saturator (asymmetric: even
harmonics; its DC is removed); TRIM sets the level after the filter (−12..+6 dB).

## 5. Filter (`dsp/LadderFilter.h`)

Four TPT one-pole stages (`G = g/(1+g)`, `g = tan(π fc/fs)`; `v = G(x − s)`, `y = v + s`, `s = y + v`)
in a ladder with global negative feedback k. The loop is solved without a unit delay: the ladder's
output is computed in closed form from the stages' states (the linear solution,
`y4 = (G⁴x + S)/(1 + kG⁴)`), and that estimate drives a tanh saturator at the input
(`u = tanh(x − k·y4 + bias) − tanh(bias)`). The saturator is a [7/6] Padé tanh (exact to 1e-12 near
zero — no noise floor on quiet signals — and bounded). k = 4.15 × RESONANCE (≤ 4.2): it rings by
itself near full, its level held by the saturator. Pass-band loss with resonance is partly made up
(`1 + 0.35k`).

- Cutoff: TONE (0..100 % → 20 Hz..18 kHz, exponential), plus key tracking (0..100 %, pivot C4), the
  filter envelope (±60 semitones, scaled by velocity if asked), LFO, Poly Mod and ANALOG, all summed
  as MIDI notes, clamped once, and turned into `g` by one table lookup (1/8-semitone steps,
  interpolated) — `exp2` and `tan` in one read. The table stops at min(0.45·fs_internal, 40 kHz).
- Tested: −12 dB at fc, 24 dB/octave, resonance peaks at the cutoff from 500 Hz to 15 kHz, no
  NaN/Inf or runaway under random cutoff/resonance jumps every 37 samples with ±8 input.
- The reference's 6.9–6214 Hz cutoff range and "k = 4 self-oscillates" were not adopted (see the
  implementation specification §3.3); the reference's claim that a symmetric tanh adds even
  harmonics is wrong — BIAS provides them (tested).

## 6. Envelopes (`dsp/Envelope.h`)

Exponential ADSR per voice, per sample: ATTACK charges towards 1.3 and turns at 1 (convex, exactly
the attack time to the top); DECAY and RELEASE reach 99 % of the way in their times; release ends
below −100 dB. A new note restarts the attack from the current level. SUSTAIN follows a knob moved
while a key holds. The filter envelope is independent of the amplitude envelope (tested both ways).

- Amplitude envelope: ANDOR OSP's own parameters (`attack` 0–2 s, `decay` 1 ms–20 s,
  `sustainLevel`, `release` 20 ms–15 s), so the shared AMP panel and its display work as in OSP.
  **Departure**: the implementation specification suggested 1 ms–30 s attack and 5 ms–45 s release;
  reusing OSP's AMP envelope (a working, shared editor) was preferred to a second, parallel one.
- Filter envelope: `analog.filterEnv.*` (1 ms–30 s, 5 ms–40 s, 0–100 %, 5 ms–45 s).

## 7. Voices (`dsp/Synth.cpp`)

16 preallocated slots; VOICES 1/2/4/8/16. Allocation: a free slot, taking turns (so each slot's
character is heard); else the quietest released voice; else the oldest held one. A stolen voice
fades out over 3 ms, then starts the new note. A key pressed again while its last note rings out
reuses that voice; a note-off releases the oldest held voice of that key. Sustain pedal (CC 64),
all notes off (CC 123–127), all sound off (CC 120, also clears the decimators), reset controllers
(CC 121). Mono (`voiceMode`): last-note priority, legato (no new attack while a key holds), return
to the held key, glide in the pitch domain (time to 95 %). UNISON stacks VOICES voices on one note
(mono), detuned ±25 cents × DETUNE at the outer voices and spread across the stereo field. In poly,
SPREAD places the slots across the field. GLIDE also works in poly (each voice from its last note).

All events are applied at their own samples: the plugin renders up to each MIDI event, applies it,
and renders on. Bitwise identical output for any block size (tested from 1 to 2048 samples).
**The on-screen wheels** arrive with the block (the shell's design): block-quantized.

## 8. ANALOG (per-voice variation)

Each slot has twelve fixed tolerances (−1..1, from `Prng(seed, slot)`): both oscillators' pitch and
width, cutoff, resonance, attack/decay/release times, both levels and the noise level; plus a slow
wander (a smoothed random walk, a new target every 0.6–1.8 s). At ANALOG 100 %: ±10 cents fixed and
±3 cents moving on pitch; ±1 semitone (+0.2 moving) on the cutoff; ±4 % width; ±6 % resonance;
±20 % envelope times; ±1.5 dB levels. At 50 %: ±5 (+1.5) cents and ±50 cents — the ranges asked for.
The seed is the shared `seed` parameter (Advanced, RESEED); nothing varies between runs unless it
changes. No tables from any other product.

## 9. Oversampling, effects, output

- Voices run at 4x and are summed before decimation: two linear-phase half-band FIRs (Kaiser,
  96 dB; 23 and 79 taps), flat to 0.42·fs. Latency 22 host samples at any rate (0.46 ms at 48 kHz), reported to the host.
  2x and 1x exist for tests and the benchmark; 4x ships.
- SPACE and ECHO are ANDOR OSP's own `osp::PostProcessor` (SpaceReverb, EchoDelay: sends,
  crossfaded type/size changes, sleeping when silent), with OSP's own parameters and mapping; its
  other stages are held at zero (bypassed). ECHO follows the host tempo.
- DC blocker (5 Hz), master volume (`gain`), then a guard: linear to −1.9 dBFS, a tanh knee that
  never passes 0 dBFS. Every factory sound plays a five-note fortissimo chord below the knee (tested).

## 10. Plugin integration (`product/`)

- **Parameters**: 57 of its own (four unused since version 2) (`analog.*`, appended after the ANDOR schema, pinned by
  `product/golden/parameter-schema.txt`), and ANDOR OSP's own where the meaning is identical (see
  PARAMETER_REGISTRY.md). **Departures** from the implementation specification's inventory: the
  waveform switches are choices (SAW / PULSE / BOTH; any combination of OSC 2's three) rather than
  separate booleans; TONE is the cutoff itself (no separate `vcf.cutoff`); DRIVE, SPACE, ECHO,
  GLIDE, mono, bend range, fine tune, seed, velocity range and the master volume are ANDOR OSP's
  parameters (same meaning, shared controls); unison implies mono.
- **Every control the interface enables moves the sound, every one it disables does not**: tested
  for every host parameter (all ~400): changing a bound one changes the output, changing any other
  leaves it bit-identical (`analog: every control the interface enables ...`).
- **The card**: layer A's hero card shows "OSCILLATORS" and two periods of the oscillators' mix
  (`Engine::sourcePicture`), never a drop zone; files are refused (and said so). TUNE, PAN, LEVEL and
  MUTE act on the generator, and since version 2 ONE SHOT / GRANULAR with the grain controls
  (the card draws POS, SPREAD and every playing grain, `Engine::grainSnapshot`: a grain's place
  is how far back it reads, on POS's scale), REIMAGINED (its name opens ANDOR OSP's popover) and
  the modulation panel; START, LINK, REVERSE, LOOP, FOLLOW, the EQ, SOLO, MPE, pitch character
  and DYNAMICS are shown disabled (ANDOR OSP's disabled look), not relabelled.
- **Modulation** (version 3 of the engine work, same state version): ANDOR OSP's whole
  modulation panel - LFO 1 (the synth's LFO: its classic depths and the routes hear the same
  one) and LFO 2, ENV 1 and 2, VEL, NOTE, the mod wheel, CC 1-4 and pressure - through its 16
  routes to every continuous control the synth reads (PARAMETER_REGISTRY.md C). The synth runs
  ANDOR OSP's `mod::Runtime` and a `mod::VoiceState` per voice at the control rate (8 samples);
  a voice whose controls are routed plays from its own copy of the patch, the routed controls
  moved (`ControlModulator`: the product converts a knob's moved value exactly as its parameter
  does); the instrument's controls are moved once per block from the shared sources. Smoothed
  controls (BLEND, TONE, RESONANCE, FILTER DRIVE) keep their smoothing and add the note's move.
  Drag a source onto a knob (the macros and the popovers' knobs too) to assign it.
- **Arpeggiator**: ANDOR OSP's, played by the shell in front of the synth (every pattern, rate,
  gate, octaves, swing, velocity; host sync; its display); the ARP destinations come from the
  synth's shared sources.
- **Macros** (data order; ECHO's knob before SPACE's on screen): the first is not assigned yet (shown
  disabled, "—"), DRIVE (ANDOR OSP's), FILTER (the cutoff; its popover: resonance, envelope amount,
  tracking, velocity; filter envelope; FILTER DRIVE with bias and trim; the LFO's amount and cutoff
  depth; ANALOG and phase), MOVEMENT, SPACE, ECHO (ANDOR OSP's). FILTER is a compact card built only
  from the shared parts; the others are ANDOR OSP's own popovers.
- **The card's first knob** (START's place, `ProductInfo::generatorStartControl`): DETUNE, OSC 2's
  fine tuning against OSC 1. Its name opens OSCILLATORS (both oscillators' waves, tuning, levels -
  the mix - and LFO depths, sync, Poly Mod, noise, voices, unison, KEY DNA).
- **Presets**: 34 factory sounds (`dsp/FactoryPatches.cpp`, synthesized only) are written to
  `Documents/ANDOR Analog/Presets/Factory/<Category>/` by the plugin (never over the musician's own).

## 11. Not in this version (by design)

Rev 2/Rev 3 circuit models, the function generator, double cross-mod, LIFE and CHARACTER, MOSAIC
(it needs a recording), microtuning, MPE, multitimbrality. (APPLY TO AMP shapes the velocity the
loudness follows by VEL's curve, in front of VELOCITY RANGE.) The next version's plan:
V3_ORGANIC_MORPH_SPEC.md.
The shell still lists all 350 ANDOR parameters to hosts (HANDOFF "Known limitations": a release blocker).
