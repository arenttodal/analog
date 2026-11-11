# V7 follow-ups: MORPH octave-aware pitch, MORPH's lamp, sends through FILTER and MOVEMENT

Three changes on `feature/sub-popup-v7`, after the SUB popup.

## 1. MORPH: octave-aware pitch

**The problem:** card C an octave up (OCT +1) with the oscillators playing sounded a tritone up; OCT +2 sounded an octave up. Once C's sample ended, the pitch came back.

**The cause:** MORPH (FUSE / WEAVE) fuses the two sources' pitch geometrically, `f = fx^(1-p) · fy^p`. At 50 % an octave apart lands half an octave (a tritone) away.

**The fix:**

- C's pitch is first moved by whole octaves to the octave nearest the oscillators', then fused: `f = fx^(1-p) · fy'^p`.
- Sources a whole number of octaves apart no longer glide in pitch as MORPH moves; they only exchange timbre.
- Sources within an octave glide as before.
- IMPRINT is unchanged (it keeps the carrier's pitch).

`tests/LiveMorphTests.cpp`, "live morph: the pitch morphs octave-aware", checks:

| Case | Expected |
|---|---|
| 220 Hz against 110, 440 and 880 Hz, at 25 / 50 / 75 % | 220 Hz |
| An octave plus a semitone, at 50 % | Half a semitone up |
| A whole tone, at 50 % | A semitone up |
| IMPRINT | 220 Hz |

Every factory golden render is unchanged.

## 2. MORPH's on / off lamp (off by default)

A tiny lamp sits immediately left of the MORPH name, on its centre line. It takes no extra room: the name and the knob stay where they were.

**Look:**

- about 14 px drawn, with a 26 px invisible click target;
- off: a muted ring and dot;
- on: the dot in MORPH's copper, with a soft halo;
- tooltip "Enable / bypass Morph.".

**Behaviour:**

- A click toggles it, as does Return or Space when it has keyboard focus. Each toggle is one host gesture and can be undone.
- The name still opens MORPH's editor; the lamp toggles independently.
- **Off** dims only MORPH's knob arc. The knob keeps its exact value, and MORPH processing is bypassed; the sources keep sounding.
- **On** restores everything as it was.
- Every source moves the lamp and the arc: automation, presets, undo, and the rack's and popover's existing switches.
- The knob no longer switches MORPH on when taken in hand (`MorphTouch` is removed). Only the switch does.

**Defaults:**

| Where | MORPH |
|---|---|
| A new instance | Off. `analog.morph.enabled` defaults to 0, an intentional schema golden change. |
| INIT | Off. Factory set version `analog-factory-9`, so installed factory presets are rewritten once. INIT has no card C, so its sound is unchanged. |
| Every other factory preset | On, as before |
| Saved sessions and presets | Keep their stored value. A pre-version-4 state stays off, as before. |

**Code:**

- **OSP (shared):** `MacroSwitchLamp` in `MainSections.h/.cpp`. It is opt-in through `ProductInfo::macroSwitches` and never appears in the parity render, so ANDOR OSP and the other products are unchanged.
- **ANDOR ANALOG:** `Product.cpp`, `AnalogParameters.cpp`, `FactoryPatches.cpp`, `AnalogEngine.*`.

**Tests:** `[analog][morph][ui]`. Screenshots are in `docs/screenshots/morph-lamp` (`[screenshots-morph-lamp]`).

## 3. MAIN + SEND: the sends run through FILTER and MOVEMENT

**The problem:** in MAIN + SEND, card C's send was tapped before the voice filter and fed straight to SEND. Its echo and room ignored FILTER and MOVEMENT, so with the filter closed or the SHAPER gating, C's wet stayed open and steady. The +6 dB in the mixer's MAIN tab is C's LEVEL (dB, as C has always shown it), not its send.

**The fix** (always on, like SUB): see `CINEMATIC_V6_GATE4.md`.

- **FILTER:** each voice runs its sends through a twin of its own filter, only while a send is open.
- **MOVEMENT:** MOVEMENT on MAIN gates SEND's input with its level trace (PULSE, SHAPER VOL).
- **Not carried:** SHAPER's FILTER target, DRIFT, TAPE and CHORUS (as for SUB).
- CLASSIC and CUSTOM are untouched.

**Tests:** SEND-02 and SEND-03 (`[mainsend][v6]`). Both fail on the previous code and pass now.
