# D ARP: the AUX arpeggiator follower

Status: implemented on `feature/aux-live-input` in both repositories. Real Kontakt and DAWs are
**UNVERIFIED (OWNER TEST)**: Kontakt is not installed here. D's instrument in the tests and demos
is "ANDOR Test Instrument" (`product/tests/surrogate`), hosted through the same VST3 path.

## What the musician gets

When D is in **KONTAKT** or **NOISE**, the strip shows two more controls just before RACK:

- **ARP**, which turns D ARP on or off;
- **FOLLOW**, with three keys: **A**, **B** and **C**.

The AUX rack shows the same two settings in its ARP group.

| Situation | What D plays |
|---|---|
| D ARP off | The keys as played, exactly as before. |
| The arpeggiator (ARP) off | The keys as played, even with D ARP on. |
| D ARP on, LINK on | The shared arpeggio. FOLLOW is kept but has no effect, and its caption reads `FOLLOW: LINK`. |
| D ARP on, LINK off | The arpeggio of the card FOLLOW names: its pattern, rate and octaves. This works even while that card's own arpeggio is off. |
| D in RHYTHM or LIVE INPUT | Unchanged. A loop or live audio is never arpeggiated. |

What reaches each instrument:

- **KONTAKT**: D's instrument gets the notes on channel 1 through its existing interface. No
  extra instance is created.
- **NOISE**: in KEYS mode, D's gate opens on each step of the arpeggio.

D gets the lane's own notes:

- the same note, velocity, octave, swing and gate;
- each note at the same sample as on the lane.

Bends, the wheels, pressure and CCs still pass through. The arpeggio keeps its pedal to itself,
as the card lanes do.

## How it works

The arpeggiator's existing delivery to the cards is left as it is. The new part is an opt-in
listener in the shared code (ANDOR OSP's shell), which sits after that delivery.

**Shared code (OSP):**

- `ProductInfo::arpFollower` names four parameters (on, lane, power, mode) and the modes that
  follow.
- `ShellProcessor::runArpFollower` runs after `runCardArpeggiators`.
- `Engine::setAuxNotes` hands D's notes to the engine for that block. Null means the keys.

**How the follower stays in step with its lane:**

- While the lane it follows runs, the follower takes a copy of that lane's arpeggiator in the
  block the lane starts. The copy holds only fixed-size arrays, so it needs no allocation.
- From then on it is fed the same keys, so both play the same notes at the same samples.
- While that lane is idle (its card's own arpeggio is off), the follower runs its own copy with
  the lane's settings and seed.

**What starts it over:** any change of the lane followed, ARP, the arpeggiator, D's mode or
power, LINK or a loaded state. Each of these sends, in order:

1. a note-off for every note the follower sounded, or for every key D was playing;
2. pedal off;
3. All Notes Off.

After a restart, D plays the new lane, or the keys still held.

**Real-time safety:**

- All storage is set aside in `prepare`.
- There are no locks or allocations on the audio thread.
- A state load is passed to the audio thread through a single atomic flag.

**ANDOR Analog:**

- `AnalogEngine::process` gives D's KONTAKT MIDI and D's keys (the NOISE gate, D's filter
  envelope) the follower's notes. These are merged in time order with SUB's played keys.
- With no follower notes, the path is exactly the old one.

ANDOR OSP has no D, so its product sets no follower and nothing runs there. This is an ARP
feature that ANDOR OSP can only gain together with an AUX source. That gap is listed under NEXT
in OSP's STATUS.

## State

- Two new parameters: `analog.aux.arp` (bool, OFF) and `analog.aux.arp.follow` (choice A/B/C,
  A). Both are appended with version hint 37.
- A session or preset saved before this feature opens with D ARP off and FOLLOW set to A.
- FOLLOW keeps its value while ARP is off and while D's mode changes.
- The IDs of the arpeggiator and of A, B and C are unchanged.
- Reset (D's menu) returns both to their defaults.

## Regression report

All tests are in `product/tests/AuxArpTests.cpp` (tag `[auxarp]`).

| Spec acceptance | Test | Result |
|---|---|---|
| Follow A, UP | LINK off: UP sixteenths, 16 notes in 2 s, 0.125 s apart | PASS |
| Follow B | B's DOWN eighths, at the played velocities, 0.25 s apart. The same events, samples included, with B's own arpeggio on or off | PASS |
| Follow C, RANDOM | D's events equal the events card C's own hosted instrument gets from its lane | PASS |
| LINK on / off | LINK on (A + C): D equals C's lane, which is A's UP pattern, and FOLLOW B is ignored. LINK on with every card: FOLLOW C gives A's pattern | PASS |
| D ARP off | Three note-ons at sample 0: the chord | PASS |
| Arpeggiator off | The chord, with D ARP on | PASS |
| A / B / C unchanged | Card C's instrument events and the whole output (D muted) are identical with D ARP on and off | PASS |
| Kontakt spiccato | Stand-in instrument via VST3: PASS. Real Kontakt: OWNER TEST | UNVERIFIED |
| Keyed NOISE | Gate open for every step, under 20 % between steps, shut after release | PASS |
| Change with no stuck notes | 13 changes mid-chord (FOLLOW, ARP, arpeggiator, LINK, mode, power, B's card), then 100 blocks of rapid FOLLOW / ARP switching. No note left on; the tail decays to silence | PASS |
| State load mid-chord | All Notes Off at the load, then nothing left hanging after release | PASS |
| Preset save / reload | Both values kept. An older session (without them) opens OFF / A | PASS |
| Existing arp tests | Full Analog suite and ANDOR OSP's plugin suite | see STATUS |

### Demos and screenshots

The renders are in `docs/demos/aux-d/`. Run `ANALOG_DEMO_DIR=... andor_analog_tests
"[auxarp-demo]"` to make them again. All five use the stand-in instrument, at 120 BPM.

The screenshots are in `docs/aux-d/screenshots/arp/`:

- the strip following B, in KONTAKT and in NOISE;
- the default strip (arpeggiator off, LINK on: dimmed);
- the AUX rack in KONTAKT and in NOISE.

| File | What it plays |
|---|---|
| `auxarp-01-d-solo-keys` | D alone, playing the keys |
| `auxarp-02-d-solo-follow-b-down` | D alone, following B (DOWN) |
| `auxarp-03-d-solo-follow-c-random` | D alone, following C (RANDOM) |
| `auxarp-04-mix-a-up-with-d-follow-b` | The full mix: A's UP sixteenths on the cards, D on B's pattern |
| `auxarp-05-mix-d-off` | The full mix with D ARP off |

### Owner test (definition of done)

1. Load a Kontakt spiccato patch into D.
2. Turn the arpeggiator on and LINK off.
3. Turn D's ARP on and choose FOLLOW B.
4. Hold a chord.

Expected: Kontakt plays B's pattern, while A, B and C sound exactly as before.
