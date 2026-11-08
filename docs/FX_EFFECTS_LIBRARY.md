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

Pools are slots, not copies: `analog.fx.<type>.<n>.<name>` (n = 1 or 2), state version 12,
parameter version hint 28, appended after every existing parameter (the 165 before untouched).
Each effect's ON (`.on`) is its own bypass; MIX and OUTPUT are inside it (MIX against a dry
signal delayed by the effect's latency).

CONVOLUTION's impulse is built on the message thread (`AnalogEngine::updateConvolutionImpulses`:
at prepare and from a 10 Hz timer, only when IMPULSE, DECAY, SIZE, REVERSE, NORMALIZE or the rate
changed) and handed to the audio thread through `osp::ModelExchange` (crossfaded over 50 ms).
Loading the user's own impulse files is not built yet (see STATUS: NEXT).

## The rack (one module, 214 px)

`rack::Kind::fx` (OSP, appended; a product rack with no macro of its own). One effect at a time:
another one swaps the module's contents, the rack keeps its height (tested). Built on the shared
`ParameterRack`: the title and its ON switch, pages (MAIN / DETAIL; EQ's BAND 1-4 and HP / LP) and
MODE / PRESET in the selector row, one row of controls on the shared cell lines, and a graph drawn
from what plays:

- EQ: the response from the coefficients, draggable band handles (frequency, gain).
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
