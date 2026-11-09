# ROUTING and TAPE, V4

A visual, interaction and telemetry pass on top of FX ROUTING (gates 0-5), FX RACK V2 and V3
(docs/FX_ROUTING_IMPLEMENTATION.md, docs/FX_RACK_V2_PROGRESS.md, docs/FX_RACK_V3_POLISH.md).
Nothing was rebuilt. It changes no saved parameter, parameter ID, routing graph, graph grammar,
preset schema or state version, and no DSP. The one addition on the audio thread is a
read-only output-peak telemetry value (below). CLASSIC still plays bit for bit.

## ROUTING (`product/Source/FxRouting.*`)

> **Later cleanup (supersedes parts of this page):** the routing view now carries no words it
> does not need. The ENGINE MIXER / FX CHAIN titles, TO FX, "A + B + C -> one shared FX path",
> the header's SERIES / PARALLEL status, the PARALLEL pill (SPLIT keeps its name), the CONT. 2 /
> FROM 1 words (the wire turns at a small node instead) and the whole footer (path summary,
> selection, the arrow / Parallel / Series keys; the tile menu and the keys do the same) are
> gone. The mixer's rows share the column's full height, one per card. The collapse key is an X.
> The TAPE deck's display fills its column, as tall as SPACE's graph, with no REPEATS / SPEED
> caption or PREDICTED word.


**Engine mixer.** It takes 21 % of the panel (it was 26.8 %), with a 170 px floor. Each card's
row has its colour block (28 px), its name (13.5 px semibold), its model (10.5 px), and a small
ceramic knob (42 px; 38 px when narrow) with the value under it. The knob is the instrument's own
knob (`OspLookAndFeel` "mini" knob, rotary drag, double-click to default). It is a
`SliderAttachment` to the card's own LEVEL parameter: `osc1Level`, `osc2Level`, `layerB.level`.
That means there is no second gain stage and no new ID, and the knob shows the parameter's own
text, so HYBRID stays "0.0 dB".

**Typography.** ROUTING is 15.5 px. ENGINE MIXER and FX CHAIN are 13.5 px semibold, on one
line. Effect names are 13.5 px semibold (13 px in a parallel lane, never below 12.5 px at the
canonical width). Secondary lines are 10.5 px. + ADD EFFECT is 12 px with a plus icon. CLASSIC /
CUSTOM sits by the gear. The footer is one restrained line (effect count, parallel stages,
bypassed effects, what comes before the chain). Restore original and the chain presets stay in
the gear menu only.

**The board: a projection of `fx::Graph`.** Layout never edits the route. `layoutChain` chooses,
in this order:

1. The board scale `b`. It is 1 at the canonical width and larger in design units in a small
   window, so names stay legible. `bMin = 0.7 / windowScale`, `bMax = 0.78 / windowScale`, both
   clamped to 1–1.3. At 869 px this gives b ≈ 1.17–1.3, which means four tiles a row at most.
2. One row, centred between the mixer's wire and OUTPUT, if the whole chain fits.
3. Otherwise two rows: the serial chain wraps left to right (never reversed) at the most even
   split that fits. Row 1 ends at a **CONT. 2** marker, row 2 starts at **FROM 1**, a quiet
   dashed return joins them, and OUTPUT sits on row 2's line after its last block.
4. Only if nothing fits at the legible minimum: parallel stages (biggest first), then the
   chain's tail, become **group summary tiles** ("3 EFFECTS"). Each shows its members' colours,
   what it is (parallel lanes, or the effects' names) and how many are on. A click opens a menu
   that reaches every member (open rack, bypass, remove). No effect is hidden or reordered, and
   the route is never shown wrongly.

Tiles are 150 × 72 (66 in a parallel lane), with 18 px wire gaps and an icon row (24 px icon,
bypass light with a 24 × 24 hit area). Parallel stages keep their split and join (dots) with the
lanes stacked and the label above the stage. The wires are 1.3 px hairlines with arrowheads into
each block. Hit testing, gaps (drop targets, including both ends of a row wrap), the drag ghost
and the quick-settings anchor all use the bounds that are drawn. Horizontal scrolling is gone:
the wheel goes to the page. The window's scale is checked before every paint and on the timer,
so a host resize, which moves no bounds inside the scaled content, re-projects the board before
it is drawn.

## TAPE (`external/osp/shared/andor-ui/Source/MacroPopups.cpp`, shared, opt-in)

`rack::Faceplate::tapeDeck` (ANALOG sets it on the MOVEMENT and ECHO faceplates) shows the TAPE
modes as a light reel deck inside the same 214 px rack, with V3's full-bleed face, horizontal ON,
borderless X, preset bar and menu unchanged. ANDOR OSP sets no faceplate, so its racks stay 0 px
from before. Other modes (BBD, ASCEND, DRIFT, CHORUS, PULSE, SHAPER) keep the approved layout.

- **Left (29 %).** The mode selector, then four ceramic rack knobs on the dense slots. ECHO:
  TIME (ms, or a stepped division when synced), FEEDBACK, TONE, AGE. MOVEMENT: INTENSITY, WOW,
  FLUTTER, WEAR. These are the same cells and parameters as before.
- **Centre (37 %), `TapeDeck`.** Two ivory reels (≤ 106 px, three earthy windows showing the
  olive tape pack, a warm metal hub), the tape around two guides and a champagne head whose gap
  lights amber while the tape runs. The readout sits between the reels:
  - ECHO: the division and its ms at the host tempo ("1/8D", "375 ms · 120 BPM", SYNC) and the
    divisions as ticks, or the free time.
  - MOVEMENT: the wow rate, the flutter rate, and a tick row that follows the speed's live
    deviation.
  - ECHO's TIMING (FREE / SYNC) and STEREO (MONO / PING / WIDE) are small ceramic segmented keys
    in the deck's top row, on the same parameters.
- **Right.** The existing graphite display at about 336 × 122, captioned REPEATS (ECHO: repeat
  pulses from the actual TIME and FEEDBACK, L/R for ping-pong) or SPEED · WOW / FLUTTER
  (MOVEMENT: the wow trace above, flutter below, WEAR). Both are marked **PREDICTED**: they are
  computed from the settings, not metered.

**Reel motion.**

- ECHO: the take-up reel turns at `1.6 / delay seconds` rad/s (a longer delay means slower
  tape), clamped to 0.8–9. The supply reel turns slower by the ratio of the two pack radii.
- MOVEMENT: a base speed that wobbles at the actual wow and flutter rates, with depth from WOW,
  FLUTTER and INTENSITY.
- The reels turn only while the module plays (its macro, ECHO on, MOVEMENT's lane on) **and**
  the instrument sounds. They ease to rest (0.35 s) in silence, and tails count, because the
  measure is the output itself. Nothing implies a transport position or elapsed time.
- `InstrumentHost::outputPeak()` is the last block's output peak, published by `ShellProcessor`
  (`AudioBuffer::getMagnitude` plus one relaxed atomic store, no allocation, no lock). It
  defaults to -1 ("not measured"; then the reels turn whenever the module plays).
- The deck animates at 30 fps only while showing, and drops to a 4 Hz idle check when hidden.

## Tests

- `[v4]`: the mixer knobs are the cards' parameters (both directions, units, the route
  untouched).
- `[v4]`: the board's projection: five in one row, ten in two rows in order with CONT. 2 /
  FROM 1, a drop into row 2's first gap lands at index 5 (undone), a parallel stage, 869 with
  ≤ 4 a row and every effect on the board as a tile or in a summary, no overlaps, the route text
  unchanged.
- `[v4]`: the TAPE decks: regions and display size, TIMING / STEREO keys on their parameters,
  reels still in silence, turning while the echo sounds, at rest after it stops; BBD and DRIFT
  as approved; the rack height unchanged.
- `[screenshots-v4]`: the eight approval shots.

## Not done / limitations

- **There is no ADVANCED graph yet** (FX ROUTING gate 9, still in the backlog). A route too long
  for two legible rows shows group summaries whose menu reaches every member. Editing *inside* a
  collapsed group (dragging into it) waits for that view.
- The reels do not follow a system "reduce motion" setting. JUCE exposes none on every platform,
  so the deck always shows its numbers and display, and only the reels move.
- Reviewed as screenshots rendered on Linux, not in a DAW on macOS / Windows.
