# MORPH with D / AUX: the owner's decision

**Status: OPEN, for the owner to decide. Not implemented.**

This version behaves as **Option A**:
- MORPH is card C's existing per-voice A / B ↔ C morph (with its carrier choice and the method rack).
- When C is EXTERNAL or plays its RHYTHM loop, MORPH is the GLOBAL MORPH between A + B and C's bus.
- D is never a MORPH origin or destination. It is layered beside everything else, through its AUX FILTER and the shared effects.
- No control suggests otherwise: there is no MORPH destination selector.

## The question

When D / AUX is active, which sound(s) should MORPH use as its origin (the carrier) and its destination (the character)? And:
- if D is a destination, should it fuse with the unfiltered carrier, the filtered A/B/C mix, or the fully processed main bus?
- should the existing MORPH macro switch target, or should D's morph have its own assignable control?

## Where things are today

```
A OSC 1 ─┐
B OSC 2 ─┼─ per voice: MIX ─ MORPH (A/B carrier × C character, per note) ─ FILTER ─ AMP ─┐
C HYBRID ┘                                                                                │
                                         voices' bus (host rate) ── C's GLOBAL MORPH* ────┤
D AUX (RHYTHM | NOISE | KONTAKT) ─ level / mute ─ AUX FILTER (follows FILTER) ────────────┤
                                                       chorus, KALEIDOSCOPE, FX route ─ out
* only when C is EXTERNAL, or plays its RHYTHM loop
```

## The options

### Option A: existing only (this version)

A + B → C stays the only MORPH. D is layered and processed, never morphed.

| | |
|---|---|
| Per voice / global | Unchanged |
| Stereo / Kontakt / legato | D's Kontakt plays its own notes and legato untouched; nothing analyses it |
| CPU / latency | None added |
| Migration | None. Every preset is unchanged |
| Risk | None. D cannot be the character of a morph |

### Option B: destination selector (C or D)

The origin stays A, B or A + B (the existing carrier choice), and a selector chooses the destination: C or D.

| | |
|---|---|
| Per voice / global | C keeps the per-voice path. D is one stereo bus (Kontakt's notes mixed, a loop), so it needs the **global** path: a spectral imprint on the A + B bus, as `GlobalMorph` does for C EXTERNAL |
| Stereo / Kontakt / legato | Kontakt's own notes and legato are unaffected; its bus is analysed in 24 bands. Per-note pitch fusion (FUSE / WEAVE) is impossible on a chord bus |
| CPU / latency | About 2 % of one core (measured for C EXTERNAL's GLOBAL MORPH at 48 kHz), and no latency (the imprint uses past band energies) |
| Migration | A new parameter (default C: existing presets unchanged) |
| Risk | Two different algorithms behind one knob: the method rack's settings would not apply to D |

### Option C: dual destination (C + D)

The origin is A + B, the destination a blend of C and D, perhaps with separate C and D amounts.

| | |
|---|---|
| Per voice / global | Both paths at once: C per voice, D global. Their results must be summed so A + B are not counted twice |
| Stereo / Kontakt / legato | As B |
| CPU / latency | Per-voice MORPH plus the global imprint |
| Migration | New amounts (default D 0: unchanged) |
| Risk | The most complex. Two analyses must not run per voice for D |

### Option D: two MORPH stages

The original A + B → C per-voice MORPH stays untouched. An optional second, global morph stage takes the whole A/B/C output as its input A and D as its input B.

| | |
|---|---|
| Per voice / global | Stage 1 unchanged, stage 2 global (`GlobalMorph`, already built and tested) |
| Stereo / Kontakt / legato | The cleanest for Kontakt and tempo-synced loops: D is one stereo bus, morphed as a whole |
| CPU / latency | About 2 % of one core when on, no latency, nothing when off |
| Migration | New parameters (off by default: unchanged) |
| Control | Either the MORPH macro gains a second meaning (not recommended), or D MORPH has its own position, assignable like any control (a USER macro, a route) |

```
A/B/C (MORPH stage 1, per voice) ─ FILTER ─ voices' bus ─┐
D ─ AUX FILTER ──────────────────────────────────────────┴─ GLOBAL MORPH (stage 2: A/B/C × D) ─ FX ─ out
```

## Where D would fuse (if it becomes a destination)

| Point | Meaning |
|---|---|
| Unfiltered carrier | Only possible per voice (C's path). Not available for a bus |
| The filtered A/B/C bus (recommended for B / C / D) | D takes the same FILTER as A/B/C before the imprint, so FILTER shapes both |
| The fully processed main bus (after the FX) | The effects would be applied to the imprint, not to D. Tails would carry the morph. Not recommended |

## Recommendation

- **Ship Option A now:** it is already in place.
- **If D should be morphable, choose Option D next.** It reuses the tested `GlobalMorph`, keeps the per-voice MORPH and every preset bit-exact, and suits Kontakt and loops.
- **It should have its own position control** (default 0: off) rather than a second meaning for the MORPH macro.

D's bus is built so that Option D can be added at its mixing point without changing D's sources.

**Owner: please choose A, B, C or D, the fusion point, and the control.**
