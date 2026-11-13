# D / AUX Addendum 04: fixed control layout, and the owner's fixes of this round

Branch: `feature/aux-d` in both repositories. Nothing is merged into `main` or the 1.0.0 release candidate.

This round holds three things:
1. the owner's report that D RHYTHM played with no key and that RATE did nothing;
2. the window cut off at the side after opening the arpeggiator;
3. Addendum 04, the strip's fixed layout.

## 1. D RHYTHM: played by the keys, RATE the right way

**What was wrong**

- D RHYTHM played its loop whenever D was powered (on the host's grid, or its own clock in a standalone), whether a key was down or not. That was how it was built; it is now a GATE choice.
- RATE ran backwards on loops with a detected grid: the loop player stretches a loop over `factor x beats`, so "2x" played at half speed.
- On a recording where the analysis found no reliable pulse, D played the file straight through at its own speed, and RATE and SYNC were ignored. The owner's break showed this: its readout had no BPM.

**What it does now**

- **GATE** (`analog.aux.rhythm.gate`, appended, **KEYS by default**; FREE is the old behaviour). In the AUX rack beside CLOCK; the strip's readout ends with KEYS or FREE.
  - **KEYS (the owner's choice: "start at note"):**
    - A key after silence starts the loop from its first beat at once. The first slice's 2 ms pre-roll plays while the gate opens, so the first hit lands 2 ms after the key, whole.
    - Tempo: the host's (CLOCK SYNC) or the loop's own (CLOCK FREE). The transport neither starts nor stops it.
    - It plays while any key is held. Legato: a second key never restarts it.
    - Released, it fades with the AMP's release (-60 dB at the release time); the next key starts it from the top.
    - The keys are the musician's, before the arpeggiator (the same stream that gates D NOISE).
  - **FREE:** always playing, on the host's grid while the transport runs.
- **RATE:** 2x is twice as fast on the grid, 1/2x half as fast; the pitch is untouched. D passes `1 / RATE` to the player; card C's HALF / DOUBLE keep their meaning.
- **A recording loaded into D RHYTHM is always a loop.** With no reliable pulse found, `prepareLoop`'s opt-in `assumeLoop`:
  - takes the whole recording from its first frame;
  - as whole bars (a power of two, or a multiple of four beats);
  - at the tempo the analysis measured nearest.

  RATE, SYNC and the keys then play it like any loop. **Card C is unchanged** (no opt-in).

**Tests:**
- DSP `[aux][rhythm]`:
  - keys: silent until a key, the first beat at it, the release, the restart from the top, legato;
  - RATE at 1x / 2x / 1/2x;
  - the assumed grid.
- Product `[keys]`: through the processor; the GATE in the rack.
- The earlier D tests now run as GATE FREE (what they describe).

## 2. The window cut off at the side

**Cause.** Opening the arpeggiator (or a rack) makes the instrument taller. The shared `frame::applyWindowShape` asked for the matching window height at the current width. On a laptop screen, with the AUX strip, its rack and the arpeggiator open, that height is larger than the screen. The host clamps such a window, and its constraints then left the editor wider than the window, so the instrument was cut off at the side.

**Fix (shared, so ANDOR OSP gets it too).**
- When the instrument grows taller, the window never asks for more height than the screen holds. It narrows to the widest window that fits; the instrument stays whole, scaled down.
- The width you chose comes back once it fits again (unless you resized the window by hand meanwhile).
- The resize limits follow, so the host's constraints never force a larger window.
- An editor first shown with a tall layout (a session saved with the arpeggiator open) is fitted to its screen once, as it appears (`frame::ScreenFit`).

**Off screen (headless tests, the pixel captures): nothing changes.** The rule is a pure function (`frame::fittedWindowWidth`), tested by shell test `[window]`.

## 3. Addendum 04: one fixed strip layout

| Zone | Contents (left to right) | Moves with the mode? |
|---|---|---|
| Permanent left (A's zone) | D badge, M / S, AUX + ON / OFF, POWER, **SOURCE MODE** dropdown, **LEVEL** | Never (tested: identical bounds in RHYTHM, NOISE and KONTAKT) |
| Source display | From just after LEVEL to B's dark well's right edge (aligned to the cards) | Never |
| Mode-specific (C's zone) | RHYTHM: RATE, PITCH, OCT, MOD AMOUNT. NOISE: COLOR, TONE. KONTAKT: nothing | Only these change |
| Permanent right | RACK, then the menu dots, anchored at the right | Never |

- **SOURCE MODE:**
  - a compact ceramic dropdown (the AUX rack's own list style), directly after POWER;
  - fixed width (KONTAKT fits), so it never resizes;
  - never inside the black display.
- **LEVEL:** permanent, directly after the dropdown, the same `analog.aux.level` binding, automation and modulation as before.
- **The display:**
  - **Status:**
    - It no longer draws OFF: AUX ON / OFF says it, and a veil still dims the picture when D is not heard.
    - MUTED / NOT SOLOED sit discreetly at its top left, never over the waveform.
  - **Per mode:**
    - **RHYTHM empty:** "DROP A LOOP OR CLICK TO LOAD" once, NO LOOP top right.
    - **NOISE:** "WHITE NOISE · KEYS" top right.
    - **KONTAKT:** "KONTAKT NOT LOADED" exactly once, with no level bar while nothing is loaded. Loaded, its name once, with no duplicate.
  - Click to load and drop ownership are unchanged.
- **One deliberate departure from Addendum 03:** the display used to sit at B's well's exact x and width. Addendum 04 asks for it immediately after LEVEL with no large gap on the left, so its left edge now follows LEVEL; its right edge stays on B's well's right edge.

**Evidence:** `docs/aux/addendum04/`:
- `compare-before-after-1448.png` (all three modes);
- each mode at 869 / 1448 / 1810 px;
- the rack;
- off (with and without a loop), muted, not soloed.

## Verification (this container: Linux, headless)

| Check | Result |
|---|---|
| D RHYTHM keys, RATE, assumed grid (DSP and product) | PASS |
| Permanent parts identical in all three modes; SOURCE MODE after POWER, LEVEL after it, the display after LEVEL; knobs before RACK | PASS (`AuxProductTests` `[ui]`) |
| Window rule (`[window]`); off-screen editors unchanged | PASS |
| ANALOG DSP 300 cases; ANALOG product 184 (2 skipped: Bukkehorn recordings not in the repository); parameter golden +1 line (GATE) | PASS |
| Visual regressions (OSP, example, ANALOG with D hidden); OSP plugin suite | see the commit |

### NOT VERIFIED (owner tests)

- **Window fit:** on a real Mac screen in Logic / Ableton / Reaper (open the arpeggiator with the AUX rack open on a laptop screen). xvfb cannot show a window here.
- **Listening:** D RHYTHM played by the keys (start, legato, release) and RATE on your own loops, including the break that showed no BPM.
- **Drag and drop** from Finder onto the new display.
