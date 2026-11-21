# ANDOR ANALOG — Cinematic Afterimages Vol. 1

Ten original presets made only with ANDOR ANALOG's own synthesis (no samples, no recordings,
no score melodies). Each one is after the timbral character of a classic film or television
synthesizer sound. That reference is written down for the team only; it never appears in a
preset file.

**Status:** sound-design branch `claude/cinematic-afterimages`. The presets are **not**
installed with the factory set and **not** published. They wait for the owner's listening
approval.

- **Files:** `presets/Cinematic Afterimages Vol. 1/*.andoranalog` (the factory installer's
  format; open them with *Open preset…*).
- **Definitions:** `product/tests/CinematicPresets.cpp`. The test
  `cinematic afterimages: the collection's files are its definitions` checks that the files match.
- **Needs:** a build with state version 24 (per-card UNISON, LEGATO, AMP 2). Older builds would
  load the files but ignore what they do not know.

## How they were made and checked

- **Phase 1 audit.** The audit covered:
  - the preset file (the shell's state: `PARAM` children plus KEY DNA's tree);
  - the parameter registry (`AnalogParameters.cpp`; `valuesOf` maps a `Patch` to it);
  - the loader (`ShellProcessor::loadPreset`).

  Expression as the engine handles it:
  - velocity: amp range (dB) and the filter envelope's amount;
  - pitch bend: range per patch;
  - mod wheel: VIBRATO (`osp::vibrato`) and any route;
  - aftertouch: channel and poly pressure feed the PRESSURE source.

  Two parameters a `Patch` does not write (card C's LEVEL `layerB.level`, the
  routes, the arpeggiator) are set on the shell directly. No engine code was changed.
- **Harness** (`product/tests/CinematicPresetsTests.cpp`). It loads each file through ANALOG's
  own loader, with a host transport playing at 120 BPM (SHAPER, the arpeggiator and the synced
  echo follow it).
  - **Renders**, finished and dry (dry = SPACE and ECHO off), 48 kHz, 24-bit:
    - single notes at velocities 35 / 80 / 120;
    - an original eight-note motif;
    - chords low, middle and high;
    - a 6.4 s held note;
    - a whole-tone bend, the wheel rising, then aftertouch rising.
  - **Automated tests** (`[cinematic]`, in the suite):
    - the files equal the definitions;
    - every preset loads, sounds (> -40 dB RMS) and stays unclipped (< -0.3 dBFS at velocity 120);
    - a soft chord is at least 2 dB quieter than a loud one (not the organ);
    - reloading gives identical settings;
    - 44.1 / 48 / 96 kHz are finite, unclipped and within 2 dB of each other.
  - **Run** `ANALOG_CINEMATIC_RENDER_DIR=<dir> andor_analog_tests "[cinematic-render]"` for the
    previews and `levels.txt`. Add `ANALOG_CINEMATIC_WRITE=1` to rewrite the files from the
    definitions.
- **Gain staging.** Single lines sit around -22 to -27 dB RMS. Chords peak between -3 and -6 dBFS.
  Bass and arpeggio lines peak around -8 to -12 dBFS. These are measured, in the table at the end.
  - The SUB and card C have their own levels; the generator's LEVEL does not scale them.
  - Three presets were rebalanced for that reason (see *what changed in refinement*).

## The presets

Ranges are MIDI notes (C4 = 60).

### 1. Neon Reverie
*After:* expressive CS-80-style poly brass (Blade Runner, 1982).

- **Character:** a warm brass swell that blooms in the filter after the note starts, slow vibrato
  on the wheel, brightness growing under the fingers, a long hall.
- **Oscillators:** two saws 8 cents apart, card A with 3-voice UNISON (detune 22 %, width 60 %).
- **Filter:** ladder at 780 Hz, low resonance, 32 st of envelope (attack 140 ms, decay 1.1 s,
  sustain 50 %), velocity 60 %, a light high-pass.
- **Amp:** 80 ms / 0.8 s / 88 % / 1.5 s.
- **Expression:**
  - aftertouch to cutoff +45 %;
  - wheel to vibrato (22 ct, 5.6 Hz, delay 0.3 s, rise 0.6 s);
  - MOVEMENT DRIFT 15 %.
- **FX:** hall 5.5 s at 32 % (pre-delay 32 ms), dotted-eighth tape echo at 10 %.
- **Range:** 36–84. **Voices:** 8. **Use:** slow chords and expressive top lines; lean on
  aftertouch for the CS-80 swell.

### 2. Signal From Below
*After:* a pulsing analog arpeggio (Stranger Things).

- **Character:** a resonant, plucky saw line with an octave-down pulse, running UP in sixteenths
  over two octaves from whatever chord is held.
- **Oscillators:** saw, plus a pulse (42 %) an octave down at 55 %.
- **Filter:** ladder 520 Hz, resonance 38 %, 30 st envelope (decay 240 ms, sustain 8 %),
  velocity 45 %.
- **Amp:** 1 ms / 0.4 s / 55 % / 140 ms.
- **ARP:** on, UP, 1/16, 2 octaves, gate 62 %.
- **Expression:** wheel to cutoff +35 % (open the sequence up).
- **FX:** dotted-eighth tape echo 16 %, plate 1.6 s at 12 %.
- **Range:** 36–79 (the arpeggio adds an octave). **Use:** hold two- to four-note chords; follows
  the host's tempo.

### 3. Obsidian Horizon
*After:* monumental, distorted synth brass (Blade Runner 2049).

- **Character:** a wall of detuned saws an octave apart, driven, opening slowly, with a
  sub underneath and a dark endless space.
  - **AMP 2:** a note played over held ones swells in over 1.4 s instead of 0.3 s, so long
    drones stay calm under new notes.
- **Oscillators:**
  - A: saw, 6-voice UNISON (detune 32 %, width 90 %);
  - B: saw an octave down, 4-voice UNISON;
  - SUB: sine an octave down at -18.5 dB (keys 24–67).
- **Filter:** ladder 360 Hz, drive 45 %, 36 st envelope (attack 350 ms, decay 2.4 s,
  sustain 50 %).
- **Amp:** 300 ms / 1.6 s / 95 % / 3.0 s. **AMP 2:** attack 1.4 s.
- **Expression:** aftertouch to cutoff +40 %.
- **FX:** DRIVE 30 %, ABYSS DARK 8 s at 36 % (pre-delay 40 ms).
- **Range:** 24–67 (low register). **Voices:** 6. **Use:** big low chords and swells; in the
  low octave, keep it to single notes or fifths.

### 4. Gravity Cathedral
*After:* an immense organ (Interstellar).

- **Character:** pipe-organ registration built from oscillators, a church-sized hall, every key
  alike.
- **Oscillators (registration):**
  - square (8' flute);
  - saw an octave up (4' principal);
  - card C's OSC 3 square two octaves up at -14 dB (a mixture);
  - SUB triangle an octave down at -27 dB (16').
- **Filter:** ladder open at 5.2 kHz, no envelope.
- **Amp:** 60 ms / sustain 100 % / release 1.8 s.
- **Expression:**
  - no velocity, as an organ;
  - wheel to a gentle tremulant (9 ct, 6.2 Hz).
- **FX:** hall 7.5 s at 38 % (pre-delay 45 ms).
- **Range:** 24–96. **Voices:** 12. **Use:** sustained chords and pedal points; leave room for
  the hall.

### 5. Sandborne
*After:* a distorted, organic desert horn (Dune, 2021).

- **Character:** a guttural, throaty horn: a low saw against a narrow pulse a fifth up, the
  ring modulator's grit, a resonant, driven band, breath noise, glide between notes.
- **Oscillators:**
  - A: saw an octave down, 3-voice UNISON;
  - B: pulse (28 %) a fifth above it, at 45 %;
  - ring 22 %, noise 12 %.
- **Filter:** ladder 620 Hz, resonance 52 %, drive 70 %, bias 35 %, high-pass 2, 16 st
  envelope (attack 350 ms).
- **Amp:** 220 ms / 1.0 s / 90 % / 1.3 s.
- **Voicing:** MONO, LEGATO, glide 120 ms, bend ±7 st.
- **Expression:** wheel to a throat waver (38 ct, 4.6 Hz); aftertouch to cutoff +45 %.
- **FX:** DRIVE 50 %, hall 4.5 s at 28 %.
- **Range:** 28–60. **Voices:** 1. **Use:** long single lines with bends and slides.

### 6. Polar Heartbeat
*After:* a minimalist, ominous bass (The Thing, 1982).

- **Character:** a dark, round, slightly hollow pulse with a soft thump, retriggered on every
  note, nearly dry: for slow repeated notes.
- **Oscillators:** triangle, plus a square at 55 %.
- **Filter:** ladder 240 Hz, 16 st envelope (decay 320 ms, sustain 0), velocity 60 %.
- **Amp:** 3 ms / 0.7 s / 35 % / 0.4 s.
- **Voicing:** MONO, LEGATO off (every note restarts).
- **Expression:** wheel to cutoff +40 %.
- **FX:** a small room at 8 %.
- **Range:** 24–52. **Voices:** 1. **Use:** two-note heartbeats around C1–C2. Its energy is
  mostly below 100 Hz, so judge it on full-range monitors.

### 7. Circuit Cathedral
*After:* a futuristic synth/orchestral pulse (TRON: Legacy).

- **Character:** a bright SUPER SAW chord, gated in sixteenths by MOVEMENT's SHAPER, so a held
  chord becomes a driving pulse.
  - Measured: 8 Hz at 120 BPM, about 16 dB deep.
- **Oscillators:** A WIDE SUPER SAW (spread 42 %); B pulse an octave down at 50 %.
- **Filter:** OTA Jupiter 1.7 kHz, resonance 20 %, 22 st envelope (decay 280 ms).
- **Amp:** 3 ms / 0.4 s / 85 % / 0.3 s.
- **Motion:** MOVEMENT SHAPER, pattern PULSE, 1/16, volume + filter, 65 %.
- **FX:** plate 2.2 s at 18 %, BBD echo 1/8 at 10 %.
- **Range:** 36–84. **Voices:** 8. **Use:** held chords under a running transport. The MOVEMENT
  macro sets how hard it pulses.

### 8. Machine Heart
*After:* metallic, mechanical synthesis (The Terminator, 1984).

- **Character:** a clangy, struck metal tone: a synced saw an octave up, swept by Poly Mod's
  envelope, ring-modulated against a pulse; short and cold.
- **Oscillators:**
  - A: saw +12 st, synced to B;
  - B: pulse (35 %);
  - ring 45 %, Poly Mod envelope to pitch 1 at 55 %.
- **Filter:** ladder 2.2 kHz, resonance 30 %, 26 st envelope (decay 0.5 s).
- **Amp:** 2 ms / 0.9 s / 30 % / 0.45 s.
- **Expression:** wheel to cutoff +30 %.
- **FX:** DRIVE 15 %, BBD echo 1/8 at 14 %, plate 1.8 s at 16 %.
- **Range:** 36–84. **Voices:** 8. **Use:** ostinatos and stabs.

### 9. Velvet Pines
*After:* a dreamy electric piano / pad (Twin Peaks).

- **Character:** a soft, round tine struck brighter by harder velocity, decaying like an
  electric piano, ensemble chorus and a slow suitcase tremolo.
- **Oscillators:** triangle, plus a narrow pulse (18 %) at 28 % for the tine.
- **Filter:** ladder 1.5 kHz, 20 st envelope (decay 0.6 s, sustain 15 %), velocity 75 %.
- **Amp:** 3 ms / 3.2 s / 30 % / 1.1 s; velocity range 22 dB.
- **Motion:** chorus I + II; MOVEMENT PULSE 22 % (slow stereo tremolo).
- **FX:** hall 3.6 s at 28 %.
- **Range:** 36–88. **Voices:** 12. **Use:** slow, soft chords; play with dynamics.

### 10. Midnight Chrome
*After:* a nostalgic analog polysynth (Drive, 2011).

- **Character:** a warm DCO saw + pulse with a touch of sub, the Juno OTA, the classic chorus,
  a dotted echo.
- **Oscillators:** A DCO saw + pulse, sub 30 %; B off.
- **Filter:** OTA Juno 1.4 kHz, resonance 16 %, 18 st envelope (decay 1.0 s, sustain 50 %).
- **Amp:** 10 ms / 1.0 s / 80 % / 0.8 s.
- **Motion:** chorus II.
- **FX:** dotted-eighth tape echo 14 %, hall 2.6 s at 20 %.
- **Range:** 36–88. **Voices:** 8. **Use:** chord pads and stabs.

## Measured (finished, 48 kHz, velocity 90–120)

| Preset | Motif RMS | Motif peak | Chords RMS | Chords peak | Centroid (dry chords) |
|---|---|---|---|---|---|
| Neon Reverie | -26.6 dB | -10.9 dBFS | -19.7 dB | -4.3 dBFS | 597 Hz |
| Signal From Below | -22.5 | -8.6 | -22.1 | -8.3 | 599 Hz |
| Obsidian Horizon | -23.6 | -11.5 | -16.8 | -3.8 | 234 Hz |
| Gravity Cathedral | -24.0 | -10.5 | -18.6 | -5.9 | 457 Hz |
| Sandborne | -23.3 | -8.5 | -24.8 | -8.6 | 346 Hz |
| Polar Heartbeat | -23.8 | -9.9 | -26.7 | -11.8 | 59 Hz |
| Circuit Cathedral | -25.9 | -12.4 | -19.8 | -5.5 | 707 Hz |
| Machine Heart | -26.8 | -8.5 | -24.2 | -2.9 | 741 Hz |
| Velvet Pines | -23.1 | -8.2 | -21.9 | -4.7 | 339 Hz |
| Midnight Chrome | -23.8 | -10.0 | -18.1 | -4.2 | 569 Hz |

RMS figures are over the whole take, silence included. The mono presets (Sandborne, Polar
Heartbeat) play their chords take as single notes.

### What changed in refinement

- **Levels.** First renders spread from -16 to -42 dB RMS. Each preset's level was set from its
  motif and chord measurements.
- **Layers the generator LEVEL does not scale.** Gravity Cathedral's card C sat at 0 dB (its
  level is the shell's `layerB.level`, which a `Patch` does not write), and its chords reached
  0 dBFS. It is now set directly at -14 dB.
- **Polar Heartbeat.** Its SUB played an octave under bass keys (around 20 Hz: felt, not
  heard). It is off, and the filter opens a little more.
- **Circuit Cathedral.** It did not pulse in renders without a transport. The harness now gives
  a 120 BPM host transport, as a DAW does.

## Known limitations

- **SHAPER and the arpeggiator follow the host.**
  - Circuit Cathedral pulses only while a transport runs.
  - Signal From Below arpeggiates at the host's tempo.
  - In the standalone app, check that its transport is playing.
- **Aftertouch** (Neon Reverie, Obsidian Horizon, Sandborne) needs a keyboard that sends it. The
  wheel routes work everywhere.
- **Polar Heartbeat** is mostly below 100 Hz. It sounds thin on laptop speakers by nature.
- **Not acoustic copies.** These are interpretations with ANALOG's engine. No measurement here
  is evidence of matching a reference recording, and no reference audio was used.
- **Reference comparison.** Timbral comparison against authorised reference audio has not been
  done. None was supplied.

## Human listening approval needed

Every preset needs the owner's ears before it ships. These are the ones where the numbers say
least:

| Preset | What to judge |
|---|---|
| Neon Reverie | The brass bloom and how much aftertouch opens it; vibrato depth on the wheel |
| Signal From Below | Arp rate / gate / resonance at your tempo; the echo's place |
| Obsidian Horizon | Weight vs mud in the low octave; AMP 2's slower swell over held notes |
| Gravity Cathedral | Registration balance (8' / 4' / mixture / 16'); hall length |
| Sandborne | The throat band (resonance 52 %, drive 70 %): organic or harsh? |
| Polar Heartbeat | Thump vs boom, on full-range monitors |
| Circuit Cathedral | Pulse depth (MOVEMENT macro) under a running transport |
| Machine Heart | Clang vs harshness of sync + ring |
| Velvet Pines | Tine brightness across velocity; tremolo speed |
| Midnight Chrome | Chorus depth and echo amount |
