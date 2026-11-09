# FX library - the effects, their racks, quick settings and presets

The effects the FX ROUTING view can place after the voices (docs/FX_ROUTING_IMPLEMENTATION.md for
the route itself). The original four - DRIVE, MOVEMENT, ECHO, SPACE - are the instrument's own:
their macros, popovers and racks as before, never a second copy. Every other effect is new, with
its own DSP (`dsp/fx`), host parameters, rack, quick settings and presets.

## The effects

| Effect | Pool | Modes | DSP (dsp/fx) | Measured (`analog_dsp_tests [fx][effects]`) |
| --- | --- | --- | --- | --- |
| STEREO SPREAD | 2 | HYPER / DIMENSION / BOTH | detuned voices (Hermite delay lines), dimension chorus, LOW MONO | WIDTH 0 is mono; HYPER's detune measured as a pitch; DIMENSION widens; finite |
| COMPRESSOR | 2 | CLEAN / FET 76 / MULTIBAND (OTT) | feed-forward with knee; FET-style program-dependent release and colour; 3-band LR4 up/down | CLEAN's static curve; FET differs (feedback); COLOR ONLY reduces nothing; MULTIBAND depth 0 flat, upward lifts, silence stays silent |
| EQUALIZER | 2 | PARAMETRIC | 4 RBJ bands (shelf / bell / notch), HP / LP 12 or 24 dB | flat is a null; each band's measured gain = the response it reports (the graph) |
| FX FILTER | 2 | LP / HP 12-24, BP, NOTCH, ALL-PASS, COMB +/-, FORMANT I-III | Cytomic SVF (Butterworth 24 dB), comb, formant banks | LP24 / HP12 slopes; graph = filter; resonance and comb finite; formants peak |
| PHASER | 2 | CLASSIC / DEEP | 4-12 all-pass stages, free or synced RATE | MIX 0 / DEPTH 0 null; notches where it reports them; extremes finite |
| FLANGER | 2 | CLASSIC / THROUGH ZERO | modulated Hermite delay, feedback; through-zero latency reported | MIX 0 / DEPTH 0 null; extremes finite; through-zero reports its latency |
| FREQ SHIFTER | 1 | SHIFT / BLUR | Hilbert pair (squared Niemitalo coefficients); STFT 2048/512 magnitude smoothing | +100 Hz moves 1 kHz to 1.1 kHz, the 900 Hz image suppressed; BLUR finite at its latency |
| CONVOLUTION | 1 | REALISTIC / CREATIVE | uniformly partitioned overlap-save (256), 6 designed impulses, 4 s cap | a unit impulse returns the input after 256 samples; across partitions; factory impulses decay |
| UTILITY | 2 | - | gain, balance, width, invert L/R, mono bass, HP / LP | neutral is bit for bit; WIDTH 0 mono; polarity; mono bass; balance never boosts |
| RETRO (V2) | 2 | TAPE (cassette / reel) / VINYL (33⅓, 45, 78) / DIGITAL (80s, 90s, CODEC) | see RETRO below | `analog_dsp_tests [retro]`: 13 cases (latency, null, noise idle, response, speed, events, bits / alias, codec, artifact only, mono, automation, determinism) |

Pools are slots, not copies: `analog.fx.<type>.<n>.<name>` (n = 1 or 2), state version 12,
parameter version hint 28, appended after every existing parameter (the 165 before untouched).
Each effect's ON (`.on`) is its own bypass; MIX and OUTPUT are inside it (MIX against a dry
signal delayed by the effect's latency).

CONVOLUTION's impulse is built on the message thread (`AnalogEngine::updateConvolutionImpulses`:
at prepare and from a 10 Hz timer, only when IMPULSE, DECAY, SIZE, REVERSE, NORMALIZE or the rate
changed) and handed to the audio thread through `osp::ModelExchange` (crossfaded over 50 ms).
Loading the user's own impulse files is not built yet (see STATUS: NEXT).

## RETRO (V2)

One aging machine, three media (FX Rack V2, gates 5-6). Its parameters are
`analog.fx.retro.<n>.*` (48 a slot, version hint 29, appended after every other parameter;
state version 13; nothing to migrate). Inserted, it is ON, TAPE, CHARACTER 40 %, WEAR 25 %,
MIX 65 %, noise off, no compressor, no dropouts; an Init patch has none in its route.

- **Macros.** CHARACTER and WEAR offset the detail values (`deriveRetro`, pure and tested);
  they never write into them and never switch on what is off (the compressor, the noise,
  dropouts below half WEAR). CHARACTER: saturation, tilt, bits, CODEC's loss; WEAR: wow,
  flutter, bandwidth, dropouts past half, the converter's rate, CODEC's packet speed.
- **TAPE / VINYL.** An asymmetric tanh with first-order ADAA (its DC removed), blended so 0 is
  the input; PRESERVE leaves the lows or the highs out of it. The medium's response (RBJ:
  head bump, high-pass, low-pass, tilt; a cassette narrower than a reel; VINYL a softer
  shelf). The transport: a Hermite-read modulated delay (wow, two flutter partials, a seeded
  drift); VINYL's wow turns at the platter's speed (0.556 / 0.75 / 1.3 Hz). STEREO PHASE offsets
  the right side's transport above ~400 Hz only (the lows stay one transport: mono-safe,
  tested). Dropouts (Poisson; a dip with its overtones muted first), crackle and pops
  (VINYL; Poisson, shaped, sizes random), all from the instance's seed.
- **DIGITAL 80s / 90s.** Sample and hold at the effective rate and quantisation at the
  effective bits. 80s truncates (no dither); 90s rounds with TPDF dither and first-order noise
  shaping. CLEAN: an 8th-order elliptic anti-alias and reconstruction pair (0.1 dB ripple,
  65 dB from 1.23 x the edge, the edge at 0.40 x the effective rate; designed offline, bilinear
  at run time); HARD: the raw staircase - aliasing on purpose. At or above the host's rate it
  holds and filters nothing. MOTION drifts rate and bits slowly (seeded).
- **CODEC.** An STFT (Hann, hop N/4, normalised overlap-add; N = 1024 at 48 kHz, 2048 at 96,
  4096 at 192): a band limit and a mask of weak bins (against their neighbours and the frame's
  peak, held by SPEED), log-domain quantisation of what stays; DROP / REPEAT whole frames,
  JITTER the phases (the spectrum kept Hermitian), COMBINED; STEREO / JOINT / MONO masks;
  ARTIFACT ONLY (processed minus the aligned dry), ARTIFACT (its emphasis), AMBIENCE (a small
  diffusion of the artifact only). LOSS 0 reconstructs the input (< -60 dB error, tested). It
  is not an MP3 / AAC encoder and does not claim to be.
- **Noise.** Off by default (-96 dB = off). Hiss, groove (surface + rumble), room or a
  converter's; FOLLOW couples it to the input; AUTO MUTE fades it within 150 ms after 250 ms of
  silence (per sample: the same at any block size).
- **Latency.** Constant in every personality: one codec frame (1024 samples at 48 kHz), so
  switching personality never changes it; MIX's dry is delayed to match (MIX 0 is the input,
  delayed, exactly). Personality switches crossfade over 20 ms; the new one starts clean.
  In a route it is planned and reported to the host (docs/FX_ROUTING_IMPLEMENTATION.md, Latency).
- **Real time.** No allocation, lock or I/O in process (tested with the other new effects,
  personalities switched mid-stream, blocks 1-512); the FFTs are prepared in prepare().
  One instance costs 1.1-2.2 % of a core here (`[benchmark][retro]`, Release, 48 kHz).
- **Rack.** Terracotta faceplate. MAIN shows only the personality's own controls (TAPE:
  CHARACTER, WEAR, WOW, FLUTTER, TONE, NOISE, MIX, OUTPUT; VINYL: ... WOW, CRACKLE, POPS;
  80s / 90s: ... BITS, RATE, ALIAS; CODEC: ... LOSS, PACKETS, ARTIFACT); MACHINE (input,
  saturation, preservation, head bump, band, noise level; CODEC's stereo, artifact only,
  ambience), MOTION (transport, dropouts; MOTION / SPEED and SEED), DYNAMICS (the compressor,
  the noise's type, follow and auto mute). The graph: the response from the same filter designs
  over the output's live spectrum; DIGITAL: the live effective Nyquist and quantisation floor;
  CODEC: the band limit and the bins kept; the last four seconds' events (dropouts, crackle,
  pops, packets) where they happened. Quick settings: MODE, CHARACTER, WEAR, MIX.
- **Presets.** Warm Tape I, Worn Tape, Cassette Air, Gentle Flutter, Dusty Vinyl, 45 RPM Haze,
  Early Digital, 90s Sampler, Codec Air, Broken Packet, Artifact Only, Acoustic Warmth, Bowed
  Halo - original settings, no machine or brand names, no recorded material.

## The rack (one module, 214 px)

`rack::Kind::fx` (OSP, appended; a product rack with no macro of its own). One effect at a time:
another one swaps the module's contents, the rack keeps its height (tested). Built on the shared
`ParameterRack`: the title and its ON switch, pages (MAIN / DETAIL only where one row cannot
hold every control: STEREO SPREAD, COMPRESSOR, CONVOLUTION, UTILITY; RETRO's four) and MODE /
PRESET in the selector row, one row of controls on the shared cell lines, and a graph drawn from
what plays. The rail carries the preset browser, a save key beside it, the "..." actions.

- EQUALIZER: one full-width graphic (`EqGraphic`; `ParameterRack::setFullWidthGraph`), the card
  EQ's design at the rack's size. It has no cells, and its controls are overlays:
  - **The top row:** band chips 1-4 / HP / LP; the selected band's ON, TYPE, FREQ, GAIN and Q
    (or the pass filter's FREQ and its 12 / 24 slope); MIX and OUT. These are `EqValueField`s
    bound to the same parameters.
  - **The nodes:** drag one for frequency, and gain where the type has one; an off node comes
    on when taken. The wheel sets Q. A double-click resets the gain, or turns a pass filter off.
    Each drag is one undo step.
  - **The spectrum:** what plays through it, Pro-Q / EQ Eight style. The output is filled in
    the device's ink and the input is a quiet line. The Equalizer's `SpectrumTap`s are 4096
    samples each; the audio thread only stores samples (relaxed atomics in a fixed array, no
    allocation, no lock), and the graphic takes a 2048-point Hann FFT on the message thread at
    30 Hz. The display rises at once and falls at 45 dB/s.
- FX FILTER: the filter's response; COMPRESSOR: the static curve and live gain reduction,
  MULTIBAND's three bands; SPREAD: voices and live correlation; PHASER: the sweep range and the
  live notches; FLANGER: the comb; FREQ SHIFTER: the mapping or BLUR's smoothing; CONVOLUTION:
  the impulse's envelope; UTILITY: live levels and correlation.

Meters read the engine's processor (`AnalogEngine::fxProcessor`); before any sound they are empty,
never synthetic.

## Quick settings

A click on a tile opens a small popover anchored to it (under it, above it, or beside it when
neither fits; never over the routing header): the effect's name, EXPAND and a close; MODE and
PRESET; two to four controls (`quickControls`). The same parameters as the rack - automation,
undo and presets are theirs. EXPAND (or Return, or a double-click on the tile) opens the rack;
Esc or a click elsewhere closes it. A tile scrolled out of sight pans into view first. For the
original four the popover shows their MODE and main knobs; EXPAND opens their own rack (DRIVE its
popover).

## Presets

- Effect presets (`FxPresets`): factory lists in code (3-7 per effect; CONVOLUTION one per
  impulse) and user files in `<user data>/ANDOR/ANDOR Analog/FX Presets/<TYPE>/<name>.xml`. A
  preset holds that effect's parameters by name (any slot of its type), never ON, the route or
  the instrument. `< name >` steps, the name opens the list (Save as, Delete, Show folder); "*"
  when the settings moved from it.
- FX chains (the routing gear: Save FX chain, Load FX chain, Show chains folder):
  `.../FX Chains/<name>.xml` holds the route and its new effects' settings. Loading one checks the
  route before any setting moves, then sets the route (one undoable step) and those effects only.
