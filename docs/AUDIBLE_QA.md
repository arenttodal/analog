# ANDOR ANALOG — measurements and listening checklist

## Measured (Linux x86-64, GCC 13, Release; `tests/`, `tools/analog-render`)

### Aliasing: inharmonic energy within 0–18 kHz at 48 kHz, relative to the total

One SAW on OSC 1, filter open, no variation or effects (`measurementPatch`). "Naive" is an
unfiltered `2p − 1` sawtooth at 48 kHz, the baseline every band-limited oscillator must beat.

| Note | f0 (Hz) | naive | 1x | 2x | **4x (ships)** | test limit (4x) |
|---|---|---|---|---|---|---|
| C4 (60) | 262 | −23.4 dB | −61.5 | −77.0 | **−83.6** | |
| C5 (72) | 523 | −20.4 | −58.5 | −74.1 | **−81.5** | < −70, and naive − 30 |
| C6 (84) | 1047 | −17.3 | −55.4 | −71.0 | **−78.4** | < −70, and naive − 30 |
| C7 (96) | 2093 | −14.3 | −52.6 | −67.8 | **−75.3** | < −70, and naive − 30 |
| G7 (103) | 3136 | −12.3 | −50.4 | −65.6 | **−73.1** | < −70, and naive − 30 |
| C8 (108) | 4186 | −11.4 | −50.1 | −64.0 | **−71.3** | |

| Other waves (4x) | Inharmonic | Test limit |
|---|---|---|
| Pulse 50 % (C6) | −79.7 dB | < −65 |
| Pulse 10 % (C6) | −75.3 dB | < −65 |
| Pulse 2 % (C6) | −69.8 dB | < −65 |
| Triangle (A4 / C7) | −88.0 / −92.9 dB | < −70 |
| Hard sync, slave +7.3 st (E7): 1x / 2x / 4x | −53.4 / −69.3 / −76.8 dB | < −55 (4x) |

The two-point PolyBLEP first tried left −60 dB at G7 (4x); the four-point B-spline correction now
used gains about 13 dB there (docs/SPEC.md §3).

### Pitch

A4 = 440 Hz within 0.1 cent at 44.1, 48 and 96 kHz (zero-crossing count over 1.7 s); octave,
semitone, fine (+25 cents), keyboard tracking off (C4 for any key), bend (+7 st range, sample-
accurate) and glide end points within 0.1–0.5 cent.

### Filter

−12 dB at the cutoff, 24 dB per octave above it, resonance peaking at the cutoff (500 Hz, 4 kHz,
15 kHz), self-oscillation within 150 cents of the cutoff at a bounded level, no NaN/Inf under
random cutoff/resonance jumps every 37 samples with ±8 input.

### Level and headroom (`render: ... five-note chord without clipping`)

INIT, one note at velocity 100: −14.8 dBFS peak, −23.8 dBFS RMS. Every factory sound's five-note
chord at velocity 127 stays below the output guard's knee (−1.9 dBFS); the loudest are String Pad
and Hollow Organ (trimmed by their LEVEL).

### CPU (`analog-render --benchmark`, one core of the build VM, 48 kHz, 512-sample blocks)

| Voices | 1x | 2x | 4x (ships) |
|---|---|---|---|
| 16 | 5.2 % | 11.6 % | 16.1 % |
| 8 | | | 8.7 % |

(About 1 % of one core per voice at 4x on this VM; Apple Silicon is typically 2–3x faster.)

Version 2 (16 held voices, 4x, best of several runs; the VM's timings vary by about ±5 %):
plain sounds (Warm Poly, Chorus Ensemble) 21–23 %; Toybox Chime 28 %, Mirage Bass 33 %,
Kaleidoscope Pad 38 %, Tape Frame Keys 42 %; GRANULAR clouds 40–50 % (Grain Cloud: 22 grains a
second of 280 ms each, about six overlapping per voice). The grains run at the voice rate (4x),
so they read the oscillators' oversampled sound without aliasing.

### Realtime and determinism

No heap allocation while rendering, through notes, controllers, pedal, voice stealing and patch
changes every block (`realtime: ...`, a counting `operator new`). Bit-identical output for the same
seed, patch and events, and for any block size (1, 7, 64, 2048 and host-varied).

## Listening checklist (not yet done: needs ears and a host)

None of the following has been carried out; they are the release gate (spec §8, "Musical listening").

- [ ] Band-limited vs naive oscillators on high pads and hard-sync sweeps (render both: `--oversampling 1` vs default).
- [ ] Filter at high resonance under fast cutoff modulation (Wah Clav, Sweep Pad, Noise Swell).
- [ ] The same chord at ANALOG 0 %, 50 %, 100 % (`--analog`), and two seeds (`--seed`).
- [ ] Every factory sound, dry (SPACE and ECHO at 0) and as shipped: useful without effects, attractive with them.
- [ ] Voice stealing on fast playing at VOICES 4 (no clicks), sustain pedal over chords, mono legato and glide.
- [ ] FILTER DRIVE 0–100 % with BIAS: warm, never harsh at ordinary levels; ANDOR OSP's DRIVE (TUBE, TAPE, CRUNCH) on chords and leads.
- [ ] GRANULAR: switch ONE SHOT ↔ GRANULAR while holding a chord (no click); POS 0 → 100 % on a moving sound
      (LFO on pitch): the grains reach back in time; TUNE ±12; KEY DNA on/off across the keyboard.
- [ ] REIMAGINED, each mode in ONE SHOT and GRANULAR at 30 / 60 / 90 %: KALEIDOSCOPE's resonance follows the
      phrase's first note; TAPE FRAME's wow and wear; TOYBOX's turning memory; MIRAGE's clock and filter.
- [ ] MOVEMENT: DRIFT at 100 % on a chord (voices wander apart, not out of tune); TAPE, CHORUS, PULSE; SHAPER with
      the host playing and stopped, and with a custom pattern.
- [ ] The ten new factory sounds by ear (levels set by measurement only).
- [ ] Hosts: Logic (AU), Ableton Live (AU/VST3), REAPER (VST3): automation, preset recall, project reopen, offline bounce = realtime.

`./build/analog-render --all renders/` writes every factory sound's demo phrase.


## Version 4 listening checklist

Listening package: 20 tracks rendered from `tests/fixtures` (the bowed stand-in, synthesized)
and the new factory sounds. None of these has been judged by ear yet.

- [ ] CLASSIC vs DCO vs WIDE on the same patch: DCO steadier, WIDE brighter, SUPER SAW wide but not chorused-sounding at small SPREAD.
- [ ] DCO sub at the bottom and the top of the keyboard (clicks? the octave clean?).
- [ ] OTA JUNO vs JUPITER vs LADDER at the same cutoff and resonance; self-oscillation pitch.
- [ ] High-pass positions (the 106's bass boost); Juno-6 vs Juno-60 voicing.
- [ ] Chorus I / II / I+II on a pad: depth and hiss level.
- [ ] ORGANIC: a real nyckelharpa/cello recording over two octaves; loop seams on held notes; FOLLOW off; REVERSE.
- [ ] MORPH IMPRINT / FUSE / WEAVE sweeps 0 → 100 % with CLASSIC, DCO and WIDE carriers; the middle should sound like one instrument, not two.
- [ ] MORPH with 8-note chords; with LAYERED; at extreme FORMANTS.
