# VISUAL IDENTITY INDEX: V5 Patterned Hardware

Each device has one identity: a muted enamel, an original motif, inks and a graph glass. The
routing tile, the FX library entry, the quick-settings title band and the full 214 px rack all
draw from that one identity, so the miniature and the rack read as the same instrument at two
scales.

- **Source of truth:** `product/Source/FxIdentity.cpp`, through `visualIdentityFor (fx::Type)`
  and `synthIdentity (SynthRack)`.
- **Derived values:** everything is computed from the device's type. There is no parameter,
  nothing is saved, and there is no theme state.
- **Motifs:** vector paths drawn by `paintFxMotif`, at three scales (library thumbnail, tile,
  rack face), with fixed seeds, so every session and every screenshot shows the same picture.
- **Inks:**
  - Names are bone `#F5EDDF` or carbon `#241D17`, whichever has the higher contrast on that
    enamel. Every device scores at least 3.9 : 1, which meets WCAG AA for large text; tile
    names are 13.5 px bold.
  - The rail is the enamel `darker (0.22)`, the edge is `darker (0.45)`, and the graph glass is
    a near-black tinted 10–16 % toward the enamel.
- **No violet:** no enamel has a hue between 245° and 330°; `[v5]` checks this.

| Device | Enamel | Motif ink | Name ink (contrast) | Motif | Tile knobs (real parameter IDs) | Pair |
|---|---|---|---|---|---|---|
| MOVEMENT | `#A85E42` faded rust | `#E8C9AD` | bone (4.16) | **flow**: modulated contours from the lower left, gentle interference | AMOUNT `motion`, plus the mode's own: DRIFT speed/pitch, TAPE wow/flutter, CHORUS rate/width, PULSE rate/shape, SHAPER smooth | `screenshots/fx-v5/pairs-reference-six.png` |
| DRIVE | `#68744C` olive | `#CCD0AB` | bone (4.31) | **branch**: tanh transfer curves from one origin, parting and converging at the knee | `drive`, `drive.tone`, `drive.body` | same |
| ECHO | `#597789` dusty slate blue | `#C1D4D9` | bone (4.08) | **rings**: repeats about a source off the left edge, spaced and fading | TIME `echo.time` or `echo.division` (synced), FEED `echo.feedback`, MIX `echo` | same |
| FX FILTER | `#B18336` ochre | `#EAD6A7` | carbon (4.88) | **sweep**: low-pass responses at descending cutoffs, each with a resonant knee | the quick controls: FREQ, RESO, MIX | same |
| SPACE | `#4C7771` sea green | `#B5CEC5` | bone (4.32) | **particles**: a seeded field, dense at its source and thinning along the decay | `space.size`, `space.decay`, MIX `space` | same |
| STEREO SPREAD | `#766D62` warm taupe | `#D9D3C7` | bone (4.37) | **loops**: two linked circles and their lens (wider phase arcs on the rack) | WIDTH, DETUNE, MIX | same |
| COMPRESSOR | `#656D55` tobacco olive | `#D7CBAC` | bone (4.66) | **gainScale**: a calibrated tick arc and a knee | by mode: CLEAN thresh/ratio/mix, FET in/out/mix, MULTIBAND depth/time/mix | `pairs-other-eight.png` |
| EQUALIZER | `#9D8051` warm brass | `#E7D6AD` | carbon (4.47) | **bands**: three low band arcs on one baseline | LOW, LO MID, HI MID gains | same |
| PHASER | `#617887` blue-grey steel | `#C5D6D9` | bone (3.97) | **notches**: nested arcs broken by their notches | RATE, DEPTH, FEED | same |
| FLANGER | `#547D7B` sage slate | `#C1DAD5` | bone (3.93) | **comb**: close teeth swept by an interference | RATE, DEPTH, FEED | same |
| FREQ SHIFTER | `#516C79` deep ocean | `#BBD0D2` | bone (4.79) | **hatch**: staggered, offset wave strokes | SHIFT: hz/feed/mix; BLUR: amount/time/mix | same |
| CONVOLUTION | `#74735B` antique pine-bronze | `#D4C99F` | bone (4.16) | **impulse**: an impulse and its decaying reflections | DECAY, SIZE, MIX | same |
| UTILITY | `#676A63` warm graphite | `#DBDBD2` | bone (4.73) | **graticule**: meter bars and a reference scale | GAIN, PAN (balance), WIDTH | same |
| RETRO | `#96543E` aged copper | `#D5B17B` | bone (4.97) | **grooves**: offset concentric grooves (tape pack / record) | CHAR, WEAR, MIX | same |
| MORPH (synthesis) | `#A66F58` copper earth | `#E4C7B4` | carbon (3.99) | **weave**: two intertwined spectral paths | no tile (upstream) | `before-after-racks-2.png` |
| voice FILTER (synthesis) | `#5F7462` sage | `#C9D6C2` | bone (4.34) | **resonance**: a resonant response family | no tile (upstream) | same |

## Where each identity is used

| View | What it takes |
|---|---|
| Routing tile (`FxRouting.cpp` `paintTile`) | Enamel face, a 1 px highlight and edge, the motif in the left quarter fading into the face, a 13.5 px bold name, a 10.5 px mode line, a status light showing the real ON, and 2–3 `TileKnob`s. A parallel lane's 60 px tile shows only the name and mode. |
| FX library (`FxRouting.cpp` `Library::paint`) | A 26 px enamel swatch with the motif at thumbnail scale |
| Quick settings (`FxRack.cpp` `QuickPopover`) | An enamel title band with the motif; knob arcs in the enamel colour on the cream popover |
| Full rack (`faceplateFromIdentity`, shared painter `rack::hardware::paint`, `enamelBody`) | The whole face in enamel with a deeper rail, the motif behind the left of the controls, four bolts, a 17 px title, cells in the face's ink, knob arcs in the motif ink, and the graph glass in a deeper enamel bezel |

## Notes on the mapping

- **Tile knobs bind only to parameters that already exist** (V5 §3.3). Wherever V5 suggested
  a control the DSP doesn't have, the nearest real parameter was used, or nothing:
  - ECHO's and SPACE's MIX is their macro amount (`echo`, `space`); that macro is their wet
    level.
  - DRIVE has no MIX parameter, so its third knob is BODY.
  - STEREO SPREAD has no separate AMOUNT on its quick set, so it shows DETUNE.
  - FX FILTER has no ENV, so it shows MIX.
- **Upstream racks never become route tiles.** The voice FILTER and MORPH keep their own
  synthesis racks; the routing library lists only the post-FX FX FILTER.
- **Motifs are etchings, not graphs.** On a rack face they sit at 28 % ink alpha behind the
  first controls and fade out within about a sixth of the body; on a tile, 62 %. Every
  functional display keeps its dark glass and its live data: FILTER's live envelope graph,
  the TAPE deck, the GR meter, the IR, the vectorscope.
