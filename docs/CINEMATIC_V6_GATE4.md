# Cinematic V6 — Gate 4: MAIN + SEND

MAIN + SEND is an opt-in route that sends chosen cards to the wet effects and keeps the rest
dry. You only get it by choosing it in FX ROUTING's gear menu (**Use MAIN + SEND…**). Every
CLASSIC and CUSTOM session and preset plays exactly as before; none is ever converted.

## Signal flow

```
A  B  C (C1 + C2)      each card's part, after its own stages
 |  |  |
 |  |  +-- SEND C --+
 |  +----- SEND B --+-> sends -> the voice filter's twin (FILTER, as the dry)
 +-------- SEND A --+   -> each note's AMP, level and pan
 |                                   |
voice filter -> AMP -> stereo sum    |
 -> chorus / KALEIDOSCOPE            |
 + SUB (MAIN only)                   |
 -> MAIN chain (inserts only)        |
      MOVEMENT's level trace ------> x (PULSE / SHAPER VOL)
 ---------------------------------(+)-- SEND chain (wet only) <--+
 -> output gain, DC blocker, guard
```

- **MAIN** is the dry insert chain. It refuses the spatial processors (ECHO, SPACE,
  CONVOLUTION): an edit that would put one there is rejected, and adding one from the library
  while MAIN is shown puts it on SEND.
- **SEND** is fed only by the cards' sends and returns strictly wet:
  - its ECHO and SPACE give their wet alone, through the legacy stages' wet-only entry points,
    so the ECHO and SPACE macros set the return level exactly once;
  - its CONVOLUTION plays at full mix;
  - a parallel lane that never meets a spatial processor adds nothing;
  - a SEND with no spatial processor returns silence;
  - inserts before a spatial processor shape what it hears, and inserts after it shape its wet.
- **Single owner:** each processor belongs to MAIN or SEND, never both.
- **Latency:** the two paths meet at the slower one's latency, and the route reports it to the
  host.
- **SUB** joins MAIN before its inserts, so DRIVE, an EQ and the like can colour it. It never
  reaches SEND: it has no send, and SEND's input is the cards' sends alone. With one path, SUB
  stays after every effect, as in Gate 2.
- **Sends are taps, never moves.** Each card's SEND (`analog.send.a/b/c`, 0–100 %) taps that
  card's part as it reaches the filter. The dry is untouched: a send adds, it never subtracts.
  Card C's send carries both its layers.
- **The sends follow FILTER and MOVEMENT** (V7 follow-up, like SUB): everything runs through
  the macro panel's FILTER and MOVEMENT, so a closed filter or a gating SHAPER is heard in the
  room too, not only in the dry.
  - FILTER: each voice runs its send through a twin of its own filter (the same cutoff,
    envelope, resonance, model and high-pass; its own state), only while a send is open.
  - MOVEMENT: on MAIN, MOVEMENT writes its level trace (PULSE, SHAPER VOL; both lanes), and
    SEND's input is multiplied by it before SEND hears it. SHAPER's FILTER target and DRIFT,
    TAPE and CHORUS are not carried (as for SUB). MOVEMENT placed on SEND processes the sends
    itself, as before.
  - There is no switch: always on, as asked. CLASSIC and CUSTOM never compute a send, so they
    are untouched.
- **Gains are kept apart.** The macros never change with the tabs. On SEND, ECHO and SPACE
  set the wet return; the cards' sends set what feeds it.

## The musician's conversion

**Use MAIN + SEND…** in the gear menu is one undoable step:

- CLASSIC's (or a CUSTOM route's) insert-safe effects stay on MAIN in their order;
- its spatial ones move to SEND (CLASSIC: ECHO ‖ SPACE, summed);
- card C's send is set to 25 % when nothing is sent yet.

**Back to one path** puts MAIN then SEND in series as CUSTOM; undo reverses either step.

## Interface (FX ROUTING)

- **Tabs:** with MAIN + SEND, a compact **MAIN | SEND | OVERVIEW** key sits in the header after
  ROUTING. With one path the view is unchanged.
- **MAIN and SEND** each show their own chain at full width, with the V5 tiles and every
  gesture (drag, stack, bypass, quick settings, the library, undo).
- **OVERVIEW** draws both paths in miniature: MAIN's chain, and SEND's chain with the cards'
  sends. It edits nothing.
- **The mixer column** shows the cards' LEVELs on MAIN and their SENDs on SEND, at the same
  size and place.

## State

- **State version 17.** `analog.send.a/b/c` are appended (version hint 32), at 0 by default.
- **The route's state child:**
  - MAIN + SEND is saved as `<FxRouting schema="2" mode="mainSend" main="…" send="…"/>`, each
    chain as a schema-1 CUSTOM text.
  - CLASSIC (no child) and CUSTOM (`schema="1"`) are saved and read exactly as before, never
    upgraded.

## Tests

| Spec | Test | Checks |
|---|---|---|
| LEGACY-01 | `[v6-baseline]`, `[legacy]`, `[fx]`, `[mainsend][v6]` | Every golden is unchanged. In CLASSIC and CUSTOM the cards' sends change nothing (bit-identical). |
| SEND-02 / SEND-03 | `[mainsend][v6]` | A nearly closed FILTER quiets the room after the release (< 0.5×); PULSE on MAIN gates what SEND hears (room < 0.85× without). Both fail on the previous code. |
| BUS-01 | `[mainsend][v6]` | Card C at 100 % and card A at 0 %: the room after the release is C's alone. A silent C sent at 100 % adds nothing. The dry under the send is untouched. |
| Strictly wet | `[mainsend][v6]` | A SEND with only an EQ returns nothing. A lane without a spatial processor adds nothing. The SPACE and ECHO macros at 0 return nothing. |
| SUB | `[mainsend][v6][sub]` | Every card at 100 %, full SPACE and ECHO, feedback 0.95: SUB's release has no room or echo after it. |
| Graph | `[mainsend][v6]` | MAIN refuses ECHO, SPACE and CONVOLUTION. One processor per owner. The conversion and back, and schema 2's text round-trip. |
| RT | `[realtime][mainsend]` | Sends, macros and notes moving with SUB on MAIN: zero allocations. |
| UI | `[v6][mainsend][ui]` | Tabs only with MAIN + SEND. The mixer becomes the sends on SEND. CONVOLUTION from MAIN goes to SEND; an EQ stays on MAIN. OVERVIEW edits nothing. Back to one path, and undo. |
| State | `[v6][mainsend][state]` | CUSTOM saved as schema 1 and recalled as one path. MAIN + SEND saved as schema 2 and recalled exactly, with the sends. |
| Plugin | `[v6][mainsend]` | Through the plugin, card C's send fills SEND's room after the note. |
| Bindings | `analog: every bound parameter` | Each card's send moves the sound in a MAIN + SEND route. |

Screenshots at 1448 and 869 px of CLASSIC, MAIN, SEND and OVERVIEW:
`docs/screenshots/v6-gate4` (`[screenshots-v6-g4]`).
