# ANDOR Learn 2.0 in ANDOR Analog

Learn as an interactive instrument manual: every step says what to do, outlines exactly where to
do it, says what will happen, and checks the real state before it moves on. Built in the shared
module (`external/osp/shared/andor-learn`, its README is the authoring guide), integrated here
in `product/Source/AnalogLearn.cpp`. Branch: `feature/aux-live-input` in both repositories.

## Gate 0: what there was (the audit)

| | Before (Learn V1 as merged into `feature/aux-live-input`) |
|---|---|
| Menu | Three Learn items: **Learn ANDOR...**, **Learn mode**, **Learn > Reset learning progress** |
| Browser | One card, an uppercase heading per catalog group (*Sound sources* twice), "Not started" on every row |
| Note | Back, **Show me**, **Skip**, Next; "Already open · Continue" inside a key |
| Highlight | A copper ring; several steps ringed a whole region (`rack.morph` for MORPH TO, `sources`, `macros`, `aux.strip` for the source mode, `source.card.A` for OSC 1 LEVEL) |
| Placement | The first side with room (below, above, right, left), else the window's farthest corner |
| Sound | Never checked: MORPH could be taught with card C or D silent; the AUX lesson passed without audio |
| Lessons | Quick Start (5), Make a moving pattern (8), Blend with MORPH (6), Make your own macro (5), Play your DAW through D (5), See the effect chain (4) |
| Help | 21 Learn-mode topics |
| Progress | `complete` / `in-progress` per lesson and revision; Skip let a lesson be finished with tasks undone |

## What the musician sees now

- **One menu entry: Learn.**
  - **Tutorials...** opens the browser.
  - **Toggle Info** replaces "Learn mode"; a tick while on.
  - **Restore Previous Sound** appears only while a sound from before an example is kept.
  - **Reset Tutorials...** asks first ("Reset tutorial progress?" with Cancel / Reset Tutorials). It clears lesson records only; the sound, presets, Info and the answered invitation stay.
- **The browser.**
  - One 420 px ceramic panel: LEARN ANDOR, "Learn the instrument by using it."
  - **START HERE** holds Quick Start; **LESSONS** holds the other five.
  - Each row: a title and a one-line description.
  - On the right: the time (new), "Resume · Step 3 of 9" (under way), or a check (done; a finished lesson can be replayed).
  - Arrow keys, Return and Escape work; the selection ring is visible; the wheel scrolls when the editor is short.
  - It never resizes the plugin.
- **The note.**
  - The lesson's small label and STEP n OF N, a title, two or three short sentences.
  - A status line: a check and what changed once done ("OSC 1 now plays sustained notes."), or "ARP is already on." with Continue.
  - At most one primary key (Next / Continue / Finish / Load Example Sound / Open Rack) and a quiet Back.
  - The X always exits. **There is no Skip.**
- **The green outline.**
  - A 2 px green line with a soft glow, about 5 px outside the exact control, corners matched to it.
  - It reads on the ceramic and the dark displays alike, and the text always names the control too.
  - When the note cannot sit beside its target, a thin connector joins them.
- **Placement.**
  - Scored: right, left, above, below (in that order of preference), each slid along its side.
  - Never over the outlined control; covering the step's other controls costs a lot; distance costs a little.
  - Kept inside the visible part of a scrolled editor; a target below the fold is scrolled into view once.
- **Info.** Point to an area to outline it; click it to read about it ("Esc to exit"). The rewritten texts are listed under Info texts below.

## The lessons

Revisions rose for every lesson (the flow changed). Progress stays readable:
- a lesson once completed shows its check;
- one under way resumes at the same step, by its stable ID;
- any other recorded state, such as an earlier build's record of a skipped lesson, reads as new, never as done.

### Quick Start (revision 3, 7 steps, 2 min)

| Step | Outlined | Done when |
|---|---|---|
| Select a sound | preset bar | Next |
| Oscillators A and B | cards A and B together | Next |
| The hybrid oscillator | card C's source dropdown | Next |
| CORE and USER controls | the CORE / USER tab | Next |
| Arpeggiator | the ARP row (not SUB) | Next |
| Sub-oscillator | the SUB row | Next |
| AUX D | the AUX strip; hidden: **Show AUX** | Next |

### Make an arpeggio (revision 2, 9 steps, 3 min; example `analog.example.arp`)

| Step | Outlined | Done when |
|---|---|---|
| Open the arpeggiator | ARP opener | its click opens the panel (already open: Continue) |
| Turn on ARP | the ARP light | its click and `arp.enabled` = 1; needs A or B audible |
| Select a pattern | PATTERN | a pattern chosen in the list |
| Change the rate | RATE | RATE changed by the musician |
| Keep OSC 1 sustained | PLAYS A | `cardA` = 0 |
| Arpeggiate OSC 2 | PLAYS B | `cardB` = 1 |
| Give each oscillator its own pattern | LINK | `link` = 0 |
| Edit OSC 2 | EDIT B | B selected (never selected for the musician); needs LINK off |
| Change OSC 2's pattern | B's PATTERN (B's RATE kept clear) | B's own pattern or rate changed; A's change does not count |

### Understand MORPH (revision 3, 10 steps, 4 min; example `analog.example.morph`)

| Step | Outlined | Done when / needs |
|---|---|---|
| Choose a playable sound (preparation) | - | Use Current Sound or Load Example Sound; checks A/B, C, D |
| Turn off the arpeggiator | the ARP light | `arp.enabled` = 0 by its click (already off: Continue) |
| Open the MORPH rack | MORPH's name | its double-click |
| Enable MORPH | the lamp beside MORPH | `morph.enabled` = 1 |
| Select OSC C | MORPH TO's **C · HYBRID** segment | `morph.target` = 0; needs the rack open and C playable |
| Hear the two sources | the MORPH knob | moved by the musician; needs A/B, C, MORPH on, toward C |
| Adjust the texture | TEXTURE | moved; needs MORPH TO C |
| Understand the methods | METHOD | changed; needs MORPH TO C |
| Select AUX D | MORPH TO's **D** segment | `morph.target` = 1; needs D playable |
| Hear the AUX source | the MORPH knob | moved; needs D playable, toward D |

Order changed from the spec's: TEXTURE and METHOD come **before** MORPH TO D, because the D stage
(GLOBAL IMPRINT) has neither control - "only offer implemented methods for the selected
destination". The segment names are the product's: **C · HYBRID** and **D · <D's mode>**.

### Create a macro (revision 2, 6 steps, 3 min; example `analog.example.simple`)

| Step | Outlined | Done when |
|---|---|---|
| Open USER macros | the CORE / USER tab | USER chosen |
| Choose a parameter | OSC 1 LEVEL | *its own* right-click menu opened (another control's does not count); moves on by itself, since Next would close the menu |
| Assign Macro 1 | (none: the note sits by the knob) | Macro 1 moves `analog.osc1.level` (Macro 2 does not count) |
| Set the range | OSC 1 LEVEL's halo | an end dragged and committed |
| Turn Macro 1 | USER Macro 1 | turned by the musician |
| Review the assignment | Macro 1's name | Next |

### Use external audio (revision 2, 7 steps, 3 min; no example: a sidechain cannot be faked)

| Step | Outlined | Done when / needs |
|---|---|---|
| Show AUX D | the strip (hidden: **Show AUX**) | the strip shown |
| Select LIVE INPUT | D's source dropdown | LIVE INPUT chosen in its menu |
| Turn on AUX D | D's power key | `aux.enabled` = 1 |
| Route audio from your DAW | D's display | Next (the plugin never touches the host's routing) |
| Verify the incoming signal | D's display (LEVEL kept clear) | **audio actually arriving**: the host must route a bus (else "No incoming audio"), and its level must move ("connected, but no audio is arriving") |
| Shape the audio | FILTER | turned while audio arrives |
| Use the audio in MORPH | MORPH TO's D segment | `morph.target` = 1; the rack first (Open Rack) |

### Explore FX Routing (revision 2, 6 steps, 2 min)

| Step | Outlined | Done when |
|---|---|---|
| Open FX Routing | the routing icon | its click |
| Understand the mixer | the mixer | Next |
| Select an effect | one block (the selected one) | its quick settings opened by a click |
| Open the full rack | that block | a rack opened by its double-click |
| Change the chain | that block | the chain's order changed by a drag (needs MAIN or SEND, not OVERVIEW) |
| Return to the instrument | the routing icon | FX Routing closed by its click |

## A playable state before a lesson listens

Readiness is read from the real state, never assumed:

| Check | Reads |
|---|---|
| A/B audible | OSC on, LEVEL above zero, card not muted or left out by a solo |
| C playable | OSCILLATOR / NOISE play; SAMPLE / INSTRUMENT PATCH need the layer loaded; the hosted instrument must be running ("a file chosen" is not enough) |
| C heard, C level | M / S, LEVEL |
| D on, heard, level | power, M / S, LEVEL |
| D playable | RHYTHM: a loop ready; NOISE: yes; KONTAKT: the instrument running; LIVE INPUT: audio arriving now |
| Sidechain | the host kept and enabled the bus; its level |
| Macro 1 ↔ OSC 1 LEVEL | the USER macro's own target list |

A missing requirement blocks the step:
- its title and text say exactly what is missing;
- the control that fixes it is outlined;
- a fix key opens it where that helps (Open Rack, Show AUX, Choose Source);
- Load Example Sound is offered where the lesson has an example.

No listening step can be marked done with a silent destination.

## Example sounds

- `analog.example.morph`: A + B, C as a third OSCILLATOR an octave up, D as keyed pink NOISE. MORPH is on, toward C, at 0 %; ARP is off.
- `analog.example.arp`: A and B an octave apart. ARP is off (the lesson turns it on), UP at 1/8, A and B following, LINK on.
- `analog.example.simple`: both oscillators, MORPH and ARP off.

All three are built from the product's own oscillators and noise:
- every other parameter is at its default;
- no recording, Kontakt instrument or sidechain is involved;
- nothing else is carried over (modulation links and macro assignments stay in the kept sound).

There is no licensed acoustic or rhythmic recording in the product to bundle. If one is
approved, card C SAMPLE and D RHYTHM examples are a data change here.

How loading and restoring work:
- An example loads only on **Load Example Sound**.
- Before it loads, the complete sound (`ShellProcessor::stateXml`: every parameter, the cards' recordings by their stored files, macro assignments, D) is kept in the plugin instance. A second example keeps the sound from before the first.
- The X closes at once and restores nothing.
- The completion note offers **Restore Previous Sound** / **Keep Example Sound**.
- The menu keeps **Restore Previous Sound** while the kept sound exists: until restored or the plugin instance closes. It is not saved with the session.
- A failed load changes nothing and says so.
- No preset file, DAW track, routing or transport is ever touched.

## Info texts

The 21 topics carry the owner's text verbatim:
- Presets; OSC 1, OSC 2, OSC C;
- MORPH, DRIVE, FILTER, MOVEMENT, ECHO, SPACE;
- CORE and USER macros, Modulation, Keyboard;
- Arpeggiator, Sub-oscillator, Arpeggiator controls, AUX D, Advanced settings;
- FX Routing, Effect chain, Effect rack.

## Verification (15 Nov 2026)

Run headless with the real editor and its real controls under xvfb (`andor_analog_tests
"[learn]"`, 23 cases, 1760 assertions; OSP `tests/shell/LearnTests.cpp`, 9 cases, in every shell
product). Only the musician's gestures complete steps; every case also checks that automation,
presets and programmatic changes complete nothing.

| Area | Test | Result |
|---|---|---|
| One Learn entry: Tutorials..., Toggle Info (tick), Reset Tutorials...; no "Learn mode" / "Learn ANDOR..." | `[learn][analog]` catalog / menu | PASS |
| No Skip on any step of any lesson; every key a short label; Back on every step but the first; X always closes; the sound untouched | "no lesson step ever shows Skip" (every step of all six lessons) | PASS |
| Make an arpeggio, 9 steps, exact outlines (ARP light, RATE, PLAYS A, PLAYS B, LINK, EDIT B), B's own pattern only | ARP lesson | PASS |
| Understand MORPH: preparation, example on request, ARP off, MORPH TO's C and D segments exactly, TEXTURE, METHOD, both blends, Restore exact | MORPH lesson | PASS |
| MORPH preparation matrix: empty C + D off, valid C + D off, C muted, C level 0, D RHYTHM without loop, D LIVE INPUT without sidechain, with audio, NOISE / NOISE, A + B silent, MORPH off, ARP on | matrix (9 states + 2) | PASS: each names the missing part, outlines its fix and offers a fix or the example; none passes silently |
| Create a macro: OSC 1 LEVEL's own menu (another's does not count), Macro 1 verified (Macro 2 does not count), range commit, turn | macro lesson | PASS |
| Use external audio: Show AUX fix, LIVE INPUT via its dropdown (the choice after the click), power, no host input → "No incoming audio" with no example and no Next, silent bus → "no audio is arriving", audio → on; FILTER; MORPH TO D after Open Rack | AUX lesson | PASS |
| Explore FX Routing: the one block (not the canvas), its rack by double-click, a real reorder (CLASSIC → CUSTOM alone is not one), back | FX lesson | PASS |
| Quick Start: 7 steps on their own parts; D hidden → Show AUX | Quick Start | PASS |
| Info: regions, the rewritten texts (INFO label), Escape order, no pixel left | Info | PASS |
| Browser: START HERE / LESSONS, descriptions, time only for new, "Resume · Step 3 of 9", keys (featured first), Escape; Reset asks, Cancel keeps, Reset clears lessons only | browser | PASS |
| Invitation: Learn ANDOR, Not Now / Tutorials / Quick Start, no lesson unasked | invitation | PASS |
| Examples: only on request, unknown example loads nothing, X keeps it (Restore in the menu), Restore exact, parameters only (no recording / Kontakt / sidechain) | examples | PASS |
| Outline and note at 869 / 1448 / 1810 px: the exact target, the note inside, never on it, pointer passes through | sizes | PASS |
| Legacy progress: a skipped record reads as new, in-progress resumes by step ID, complete stays; Reset keeps the invitation | OSP `[learn][unit]` | PASS |
| Runner: requirements block (no event, Next or Continue passes), preparation never blocks, example load is no gesture, a dropdown's late choice completes only `viaMenu()` steps | OSP `[learn][unit]` | PASS |

Full suites after the change:
- OSP plugin tests: 4 / 4 shards (42 cases each), including ANDOR OSP's pixel-identity captures; Learn is not in ANDOR OSP's own editor.
- Example product: 24 cases.
- ANDOR Analog: 234 passed, 2 skipped (the Bukkehorn samples are not on this machine).
- analog_dsp_tests: 304 cases.

Screenshots (`docs/screenshots/learn/`, every lesson step at 1448 and 869 px, plus the
invitation, the browser with and without progress, a blocked step, the completion with an
example, the reset confirmation and Info): `ANALOG_SCREENSHOT_DIR=... andor_analog_tests
"[screenshots-learn]"`.

### Not verified here (OWNER TEST)

- **A running plugin in a DAW.** Every lesson was exercised through the real editor's controls
  headlessly; nobody has clicked through them by hand in Logic / Ableton / REAPER. Please run
  each lesson once in a host.
- **The right-click menu.** Native popup menus cannot be drawn over. The step about Assign to
  Macro > Macro 1 places its note beside OSC 1 LEVEL with no outline, as the spec allows, and
  moves on by itself when that knob's menu opens.
- **A real sidechain** (a DAW track routed to AUX D Live Input). The tests feed the engine's
  input the way the shell does each block.
- **Host keyboard focus.** The browser takes the keyboard while open (arrows, Return, Escape);
  whether every DAW passes those keys to a plugin window is host-specific.
