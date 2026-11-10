# Cinematic V6 — Gate 1: each oscillator card's own REIMAGINED

Cards A (OSC 1) and B (OSC 2) each have their own REIMAGINED: an amount, a mode (KALEIDOSCOPE,
TAPE FRAME, TOYBOX, MIRAGE) and that mode's settings. Each one transforms only its own card.
Card C is never transformed.

## Decisions

- **No pair-wide compatibility path (owner's decision).** There is one model. Older patches open
  with the old amount and mode on both cards, each processed on its own card. That is close to
  the old sound but not identical. Five factory sounds change: Grain Cloud (bit-level only),
  Kaleidoscope Pad, Tape Frame Keys, Toybox Chime and Mirage Bass. Frozen Glass renders exactly
  as before, because its OSC 2 is silent.
- **Card B uses ANDOR OSP's existing per-layer parameters.** It uses `layerC.reimagined` and
  `layerC.reimagined.*` (card B is the shell's storage layer C) rather than new
  `analog.osc2.*` IDs. They were already in the parameter registry, inert, so nothing is
  appended. It also gets OSP's per-layer modulation destination (`reimaginedC`) and OSP's own
  per-layer mode popover.
- **KALEIDOSCOPE's shared parts are level-weighted.** Two parts of KALEIDOSCOPE come after the
  cards are mixed: the voice-wide drift and the resonator/formant stage on the mix. They take
  one amount: each card's KALEIDOSCOPE amount weighted by how loud that card is heard (its
  level; muted or soloed away counts as 0). One amount on both cards, as every older patch
  opens, gives exactly that amount. A soloed card hears only its own.

## Signal path (per voice)

| Stage | Before (1.0) | Now |
|---|---|---|
| KALEIDOSCOPE grain cloud and remap | each card, one amount | each card, its own amount |
| KALEIDOSCOPE doubling | read from A + B, applied to the whole source (A + B + C) | each card reads and doubles its own part |
| KALEIDOSCOPE saturation | the whole source, card C included | each card's own part |
| TAPE FRAME / TOYBOX / MIRAGE | one engine per voice on the whole source (A + B + C) | one engine per card per voice, each on its own part; C passes by |
| The mode's read speed on pitch and grains | both oscillators, both cards' grains | each card's own oscillator and grains |
| KALEIDOSCOPE drift and mix stage | the amount | level-weighted amount (above) |

Ring and sync material: the ring is part of card B's signal and sync is on OSC 1 (card A), as
in 1.0. Each card's REIMAGINED transforms whatever its own part contains.

## State

- **State version 14.** States from version 13 and older get card A's amount and mode settings
  copied to card B on load (`migrateFromVersion13`). The factory sounds do the same
  (`factoryPatches()`), so a session and its factory sound open alike.
- **Parameters.** No new IDs. `layerC.reimagined*` are now read by the engine, so they're
  offered for automation.

## Interface

- **Card B's fifth knob** is its REIMAGINED: OSP's per-layer knob, whose name opens that layer's
  mode popover. It's the same component as card A's.
- **Card B's VELO** moves into its well overlay, at the same scale as the timbre controls:
  - CLASSIC: in a free cell.
  - DCO and WIDE: a sixth cell.

  Its ID, value and automation are unchanged.
- **OSP** is not changed.

## Tests

- **`[reimagined][v6]` (DSP), RI-01 and RI-02, all four live modes:** a card heard alone sounds
  the same (max difference < 1e-6) whatever the other card's REIMAGINED is set to. Also checked:
  each card's REIMAGINED changes its own card; both at 0 % are bit-identical to the plain voice;
  the mix stage's level weighting.
- **`[v6][reimagined]` (plugin):** card B's parameters are read by the engine; version 13
  states migrate; version 14 states keep the cards apart; card B's fifth knob is REIMAGINED; VELO
  is in B's well in CLASSIC, DCO and WIDE, and not in the knob row.
- **Goldens, re-recorded for the intended changes only:**
  - `product/golden/v6-baseline-renders.txt`: the five sounds;
  - `tests/golden/legacy-renders.txt` and `fx-baseline-renders.txt`: those sounds and the
    KALEIDOSCOPE edge patch, which now sets both cards as an older patch opens.

  Every other entry is bit-identical.
- **Screenshots:** `docs/screenshots/v6-gate1`.
