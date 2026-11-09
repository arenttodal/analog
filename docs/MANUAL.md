# ANDOR Analog — User Guide

**Version 1.0 · macOS (Audio Unit, VST3, standalone) · Windows (VST3, standalone)**

ANDOR Analog is a 16-voice polyphonic synthesizer. Two analog-modelled oscillators play beside a
third sound source of your choice (a recording, a third oscillator, noise or an instrument
patch), and MORPH can fuse them into one sound while you play. On top of that sit the ANDOR
effects (drive, movement, echo and space), an arpeggiator and a modulation panel.

---

## 1. Installing

**macOS 11 or later (Apple silicon and Intel)**
1. Open `ANDOR-Analog-<version>-macOS.pkg` and follow the installer. You can choose the Audio
   Unit, the VST3 plug-in and the standalone app.
2. The installer puts the plug-ins in `/Library/Audio/Plug-Ins/Components` and
   `/Library/Audio/Plug-Ins/VST3`, and the app in `/Applications`.
3. Restart your DAW, or rescan its plug-ins. In Logic Pro, ANDOR Analog is under
   *AU Instruments › OSP › ANDOR Analog*.

**Windows 10 or 11 (64-bit)**
1. Run `ANDOR-Analog-<version>-Windows-Setup.exe`.
2. The VST3 goes to `C:\Program Files\Common Files\VST3`, and the app to
   `C:\Program Files\ANDOR Analog`.
3. Rescan plug-ins in your DAW.

**First launch.** ANDOR Analog writes its 44 factory presets to
`Documents/ANDOR Analog/Presets/Factory`. Save your own presets to `Documents/ANDOR Analog/Presets`
and **not** inside `Factory`: the Factory folder is refreshed when an update brings new factory
sounds. (Since 1.0, files you saved there yourself are kept, but your own folder is the safe
place.)

**Uninstalling.** On macOS, delete `ANDOR Analog.component`, `ANDOR Analog.vst3` and
`ANDOR Analog.app` from the folders above. On Windows, use *Settings › Apps*. Your presets and
recordings in `Documents/ANDOR Analog` are never removed.

## 2. A tour of the window

From top to bottom:

| Area | What it does |
|---|---|
| **Header** | The preset name, with ‹ › to step through presets. Then SAVE, the routing view (the icon beside SAVE) and the A/B **MIX**: the oscillators on one side, card C on the other. |
| **Cards A, B, C** | **A** is oscillator 1 and **B** is oscillator 2. **C** is the third source: a SAMPLE, OSC 3, NOISE or an INSTRUMENT PATCH, chosen from the dropdown in its dark window. |
| **Macros** | Six large knobs: **MORPH, DRIVE, FILTER, MOVEMENT, SPACE, ECHO**. Click a macro's name to open its settings (its rack). |
| **Modulation and arpeggiator** | LFOs, envelopes and 16 routes; the arpeggiator. |
| **Keyboard** | Plays the instrument with the mouse; the colours show the keys you hold. |

You can resize the window by dragging its corner (869 to 1810 pixels wide). Hover over any
control to see what it does.

## 3. The sound sources

**Oscillators (cards A and B).** Each oscillator has a model:
- **CLASSIC**: the warm, drifting oscillator of late-1970s polysynths.
- **DCO**: stable pitch, with a sub-oscillator and pulse-width modulation.
- **WIDE**: saw, pulse, triangle and square, a SHAPE control, and SUPER SAW (seven saws).

Each card has its own DETUNE, TUNE, OCT, LEVEL, M/S (mute/solo), EQ and DAMPEN. The PAIR settings
(in the FILTER rack) add SYNC, RING modulation, POLY MOD and the noise mixer.

**Card C.** Choose what it plays:
- **SAMPLE**: drop a WAV, AIFF or FLAC recording on the card. ANDOR Analog finds its pitch and
  plays it across the keyboard through the same filter as the oscillators. The card's icon keys
  are LINK, REVERSE, LOOP and FOLLOW. A copy of the recording is kept in ANDOR Analog's sample
  store, so your project still opens if you move or delete the original file.
- **OSC 3**: a third oscillator.
- **NOISE**: COLOR, TONE and MOVEMENT.
- **INSTRUMENT PATCH**: a sound imported with the sample importer (see section 8).

Card C can also play in **GRANULAR** mode (POS, SIZE, DENS, TUNE, SPREAD), with **KEY DNA** to
give every key its own grain behaviour.

**MIX** (in the header) sets the balance between the oscillators (A+B) and card C.

## 4. The macros

| Macro | Turn it up for… | In its rack |
|---|---|---|
| **MORPH** | A sound *between* the oscillators and card C. This is a fusion of the two, not a crossfade. | FUSE / WEAVE / IMPRINT, LEVEL MATCH. Turning the knob switches MORPH on. |
| **DRIVE** | Saturation of the whole sound. | TUBE, TAPE, CRUNCH; TONE and BODY. |
| **FILTER** | Brighter sound: the four-pole filter's cutoff. | TYPE (LADDER or OTA, plus HIGH-PASS), RESO, ENV, KEYS, DRIVE and the filter envelope. The VOICE page has the amplifier, voices, LFO 1 depths, chorus and character; the PAIR page has the oscillators' interplay. |
| **MOVEMENT** | Life and motion. | Two lanes (A and B), each one of DRIFT, TAPE, CHORUS, PULSE or SHAPER (a rhythmic pattern; draw your own), with SYNC and PHASE. |
| **SPACE** | A room around the sound. | Rooms, ABYSS (vast, with FREEZE), VINTAGE (an early digital hall) and BLOOM (diffusion delay); 38 presets. |
| **ECHO** | Repeats. | TAPE, BBD, or ASCEND (repeats that climb in pitch); FREE or SYNC; TIME, FEEDBACK, TONE, AGE. |

## 5. Effects routing

The routing view (the icon beside SAVE) shows the effects chain.
- **CLASSIC** is the instrument's fixed chain: drive, movement, echo, space. Presets start here.
- **CUSTOM** lets you reorder effects, run them in parallel, bypass them, and add more: STEREO
  SPREAD, COMPRESSORS, EQUALIZER, FX FILTER, PHASER, FLANGER, FREQUENCY SHIFTER, CONVOLUTION and
  RETRO (tape, vinyl and digital-era colour).

Every change can be undone. Some effects (RETRO's codec, CONVOLUTION) add latency, which ANDOR
Analog reports to your DAW so it can compensate.

## 6. Modulation and the arpeggiator

**Modulation.**
- **Sources:** LFO 1 and 2, ENV 1 and 2, VELOCITY, NOTE, the mod wheel, pressure, and CC 1–4 (with
  MIDI Learn).
- **Making a route:** drag a source onto any knob, or set it in one of the 16 route slots.
- **Mod wheel:** it moves nothing until you route it. Older presets that used the classic wheel
  vibrato keep it.

**Arpeggiator.**
- 18 patterns, 10 rates, gate, octaves and swing. It syncs to your DAW's tempo and holds with the
  sustain pedal.
- **PLAYS A B C** chooses which cards arpeggiate while the others hold the chord.
- With **LINK** off, each card has its own pattern, rate and octaves.

## 7. Presets

- **Browsing:** ‹ › in the header, or the preset menu. Factory sounds are grouped as Basic, Bass,
  Brass, Keys, Lead, Pad, Sequence and Texture.
- **Saving:** SAVE writes a `.andoranalog` file.
- **Recordings in presets:** a preset that uses a recording refers to it and to its copy in the
  sample store. To give a preset to someone else, send the recording too.
- **DAW projects:** your DAW saves the complete state of the instrument with your project,
  recordings included by reference.

## 8. Importing sample instruments

Card C's INSTRUMENT PATCH can play sounds imported with the sample importer. In version 1.0 the
importer reads unencrypted Kontakt `.nki` instruments of the kind it was tested with. Encrypted
or library-protected instruments are refused. Only import sounds you have the right to use.
*Kontakt is a trademark of Native Instruments; ANDOR Analog is not affiliated with Native
Instruments.*

## 9. Performance tips

- **CPU use:** a typical patch uses about a quarter of one CPU core at 16 voices. MORPH with a
  recording and dense GRANULAR clouds use the most. Lower the voice count (FILTER rack › VOICE)
  if your computer struggles.
- **Bouncing:** ANDOR Analog renders identically in real time and offline, so bounces match what
  you heard.

## 10. Troubleshooting

| Problem | What to do |
|---|---|
| The plug-in does not appear | Rescan plug-ins. In Logic Pro, open *Plug-in Manager* and *Reset & Rescan Selection*. Check that you installed the format your DAW uses (AU or VST3). |
| macOS says it cannot be opened | Download the installer again from your account page. Release installers are signed and notarised by the publisher. |
| A project opens without its recording | The recording's file and its stored copy are both missing. Card C names the file; drop it on the card again. |
| No sound | Check card M/S, MIX, the output level and the MIDI channel. Try the factory preset *Basic*. |
| Clicks under heavy load | Raise the DAW's buffer size, or lower the voice count. |

## 11. Support and legal

- **Support:** [SUPPORT EMAIL / URL]
- **Licence agreement:** EULA.txt, installed with the software.
- **Third-party software and fonts:** THIRD_PARTY_NOTICES.txt, installed with the software.
- **Trademarks:** VST is a trademark of Steinberg Media Technologies GmbH. Audio Units and Logic
  Pro are trademarks of Apple Inc. Other product names are the trademarks of their owners and are
  used only to describe compatibility.
