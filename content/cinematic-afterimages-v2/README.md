# CINEMATIC AFTERIMAGES VOL. 2

One hundred presets for ANDOR ANALOG, in ten families of ten: basses, leads, pads, an analog
orchestra, arpeggios, drones and fragile keys for film, game and trailer scoring.

**Status: written, measured and technically checked; awaiting the owner's listening approval.**
This bank is not finished until the owner has played it. Every preset is marked
`awaiting owner listening` in `manifest.json` until then.

It is an import pack. It does not replace or change the factory sounds, Vol. 1's ten presets,
anyone's user presets or the 1.0.0 release candidate.

## Installing

1. Copy the folder `presets/Cinematic Afterimages Vol. 2` into ANDOR ANALOG's preset folder,
   `Documents/ANDOR Analog` (the folder the preset menu's **Save** writes to), keeping its ten
   family folders.
2. Re-open the preset menu. The presets appear under *Cinematic Afterimages Vol. 2*, one folder
   per family.

Each file is a complete ANDOR ANALOG state: the oscillators, filter, envelopes, the four
effects, the FX routing, the modulation routes and the arpeggiator. Nothing is sampled and
nothing needs a recording on card C.

## How it was made

- **Defined in code, saved by the instrument.** Each preset is a short C++ definition in
  `source/Family*.cpp` (the sound, its description, how to play it, its musical role and its
  range). `[ca2-write]` builds each one in a real `ShellProcessor` and saves it with
  `ShellProcessor::savePreset`, the same path as the **Save** button, so the files are exactly
  what the instrument writes.
- **Only existing DSP.** No new engine features were added for the pack.
- **Calibrated loudness.** `[ca2-calibrate]` renders each preset's preview phrase without a
  trim and sets one gain for all its sources so that its *sounding* level (the RMS of the
  100 ms windows within 20 dB of the loudest) meets its family's target, never peaking above
  -1 dBFS. Targets: basses -17 dB, low-end drones -19 dB, leads -19 dB, pads -20 dB, orchestra
  -19 dB, arpeggios -20 dB, drones -21 dB, keys -21 dB. The gain goes into LEVEL (up to +6 dB)
  and then into the voice's TRIM after the filter; SUB and card C get the same gain.
- **Checked.** `[ca2]` reloads every file and requires it to restore the definition exactly
  (every parameter and the FX route), to render finite audio louder than -40 dB and to peak
  below -0.3 dBFS. `[ca2-render]` renders every preset at three velocities, its phrase, chords,
  a long note and an expression take (bend, mod wheel, aftertouch), wet and dry, and measures
  the sample-rate spread, block-size invariance, determinism and mono compatibility.
  `[ca2-tail]` plays root and fifth for 2 s and then a minute of silence, and requires every
  preset to fall under -80 dBFS for good (the longest, Deep Stairwell, after 27 s). The data is
  in `qa/qa.json` and `qa/tails.json`; the tables are below.

## Limitations (stated honestly)

- **Arpeggios follow the host's tempo.** Family H's patterns play at the host or standalone
  tempo; the previews use 120 BPM.
- **Custom Patterns are not used.** Every arpeggio uses the built-in patterns, rates, gate,
  octaves and swing; Moss Clock's loping walk is THUMB UP in dotted sixteenths with 30 % swing.
  A preset can carry a Custom Pattern, but none was needed for these ten.
- **Modulation routes go only to the FILTER.** Aftertouch and the mod wheel are routed to the
  cutoff where a preset calls for them; nothing is routed to ANALOG's product destinations.
- **Mono leads sit high on LEVEL.** A single voice is quieter than a chord, so the calibration
  put most of Family D and E's leads at LEVEL +6 dB with some TRIM; turn VOLUME down rather
  than LEVEL up if you need more.
- **Percussive sounds measure under their family's target.** Iron Rain is held back by its
  peak ceiling (-1 dBFS), so its sounding level is about 4 dB below its family's target; every
  other preset measures within 1.4 dB of its target.
- **SPACE's decay stops at 8 s.** The longest rooms are 8 s; ABYSS ENDLESS stretches its
  decay six-fold, so the three ENDLESS presets (Burning Aurora, Frosted Air, Deep Stairwell)
  use a 3 s decay that still takes 17-27 s to fade.
- **No reference audio.** Nothing was matched against film scores or other instruments; the
  briefs and the listening test are the only reference.

## Owner approval

| Family | Approved by the owner |
|---|---|
| A Fundamental Weight (001-010) | pending |
| B Corrosion (011-020) | pending |
| C Monumental Low End (021-030) | pending |
| D Endless Distance (031-040) | pending |
| E Human Voltage (041-050) | pending |
| F Slow Light (051-060) | pending |
| G Synthetic Orchestra (061-070) | pending |
| H Musical Machines (071-080) | pending |
| I Dark Matter (081-090) | pending |
| J Fragile Machines (091-100) | pending |

Report weak or wrong-sounding presets by ID (for example *CA2-047 too bright*). They are
reworked in their definitions and re-saved, never edited by hand in the files.

## Rebuilding the pack

From an ANALOG plugin build (`build-plugin`):

```sh
CA2_CALIBRATE=1 ./osp/external-products/product/andor_analog_tests "[ca2-calibrate]"   # writes source/trims.inc
ninja andor_analog_tests
CA2_WRITE=1 CA2_REVISION=$(git rev-parse --short HEAD) ./osp/external-products/product/andor_analog_tests "[ca2-write]"
./osp/external-products/product/andor_analog_tests "[ca2]"
CA2_RENDER_DIR=/some/folder ./osp/external-products/product/andor_analog_tests "[ca2-render]"   # previews + qa.json
CA2_RENDER_DIR=/some/folder ./osp/external-products/product/andor_analog_tests "[ca2-tail]"     # tails.json
```

## Reference

### Diversity

| Feature | Presets |
|---|---|
| SPACE ROOMS | 53 |
| MONO | 41 |
| UNISON | 15 |
| SPACE ABYSS | 14 |
| ECHO TAPE | 13 |
| ARP | 10 |
| DRIVE CRUNCH | 10 |
| DRIVE TUBE | 9 |
| OTA | 8 |
| SPACE VINTAGE | 8 |
| JUNO CHORUS | 7 |
| MOVEMENT TAPE | 7 |
| DRIVE TAPE | 5 |
| ECHO BBD | 5 |
| SPACE BLOOM | 5 |
| DCO | 4 |
| MOVEMENT PULSE | 4 |
| MOVEMENT CHORUS | 3 |
| MOVEMENT DRIFT | 3 |
| AMP 2 | 2 |
| ECHO ASCEND | 1 |

### The presets


#### Fundamental Weight - Fat, musical analog basses

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-001 | Deep Current | Sustained cinematic bass notes | E1 - G3 | Pure low-frequency weight with a soft harmonic edge. | Play single notes below C3; hold them. |
| CA2-002 | Stone Current | Rhythmic foundation bass | E1 - E3 | A heavy, rounded pulse bass with a short resonant snap at the start of every note. | Play detached notes; every note restarts its snap. |
| CA2-003 | Velvet Gravity | Melodic low lead-bass | E1 - C4 | A creamy, expensive mono bass for singing low melodies. | Play legato lines; the glide connects them. The wheel opens it. |
| CA2-004 | Blackwater Monolith | Weight under a cue | C1 - D3 | A dense, dark mono bass with tremendous body. | Hold long low notes; play softly for darkness, harder for a little edge. |
| CA2-005 | Ironwood | Plucked walking bass | E1 - C4 | A woody, plucked bass, almost acoustic in its response. | Play short notes with varied velocity, like a picked upright. |
| CA2-006 | Low Orbit | Modern sub foundation | C1 - E3 | An extremely clean modern sub with a faint saw hovering above it. | Soft notes stay pure; dig in to bring out the articulation. |
| CA2-007 | Earthmover | Impact bass for action cues | C1 - E3 | A massive, punchy cinematic bass that hits like a wall. | Short, strong notes; let the punch land. |
| CA2-008 | Coal Engine | Moving ostinato bass | C1 - G3 | A slow, rolling bass with a pulse moving inside it. | Hold notes and let the engine roll; slow lines work best. |
| CA2-009 | Lunar Pressure | Wide modern bass for scores | E1 - G3 | A focused bass core surrounded by a wide halo of upper harmonics. | Hold notes; the centre stays put while the halo spreads. |
| CA2-010 | Deepest Thread | Ambient / orchestral underpinning | C1 - G3 | A soft, almost sine-like bass that slowly opens and lingers. | Hold long notes or low chords under strings and pads. |

#### Corrosion - Distorted, aggressive and industrial basses

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-011 | Furnace Teeth | Aggressive driving bass | E1 - G3 | A distorted sync bass with a ferocious harmonic bite. | Try pitch bends and fast mono lines; the wheel opens the throat. |
| CA2-012 | Rust Circuit | Industrial menace bass | E1 - G3 | A metallic, growling bass with tuned dissonance over a steady floor. | Hold notes and move the wheel; low fifths growl best. |
| CA2-013 | Broken Reactor | Heavy hybrid score bass | D1 - E3 | An extremely aggressive bass: crunchy mids over compressed weight. | Short riffs and held power notes; keep the lines simple. |
| CA2-014 | Corrosion Field | Dystopian texture bass | D1 - G3 | A grainy, toxic bass with a noise component that moves. | Hold notes and let the grain crawl; works for slow pulses. |
| CA2-015 | Blackcoil | Gritty retro bass | E1 - G3 | A narrow-pulse bass with resonant teeth and an unstable vintage edge. | Medium-length notes; the wheel bares the teeth. |
| CA2-016 | Voltage Wound | Climactic bass scream | E1 - C4 | A screaming sync bass whose every note tears slowly upward. | Hold notes to let the scream climb; bend a full octave for leaps. |
| CA2-017 | Charred Glass | Percussive distorted bass | E1 - C4 | Hard, brittle distorted harmonics shattering over a soft sub. | Short stabs and staccato lines. |
| CA2-018 | Slagline | Driving action ostinato | D1 - G3 | A fast, punchy industrial bass for rhythmic playing, bone dry. | Sixteenth-note riffs; mute between notes. |
| CA2-019 | Motor Ghost | Machine-room tension bass | D1 - G3 | A mechanical, pulsating bass with an unsteady motor inside. | Hold notes; the motor keeps turning. |
| CA2-020 | Gravel Choir | Ominous vocal bass | E1 - G3 | A dark, vocal-like distorted bass that seems to sing through gravel. | Slow legato lines; the vowel moves on its own. |

#### Monumental Low End - Cinematic bass drones and colossal hybrid foundations

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-021 | Cathedral Below | Architectural low foundation | C1 - G3 | A low organ of octaves and fifths, breathing slowly in a vast stone room. | Hold low chords and pedal notes; let the room answer. |
| CA2-022 | Titan Footfall | Trailer-scale impact swell | C1 - E3 | A slow, colossal swell that lands with force and keeps expanding. | Hold single low notes or fifths; give each one time. |
| CA2-023 | Gravity Well | Ominous descent | C1 - D3 | A sub-bass that seems to bend inward as it sinks, a distorted layer growing above. | Long single notes; feel it fall. |
| CA2-024 | Cold Continent | Glacial low pad | E1 - E4 | A frozen, choir-like low synthesizer for long held chords. | Hold minor chords low on the keyboard. |
| CA2-025 | The Hollow Sun | Slow-burn tension bass | C1 - E3 | A huge, warm bass that reveals a distorted edge the longer you hold it. | Hold notes for several seconds and listen to them change. |
| CA2-026 | Abyssal Beacon | Deep-sea suspense drone | C1 - G3 | A dark, pulsing bass drone with a faint resonant tone far above it. | Hold a low note and let the beacon pulse. |
| CA2-027 | Pressure Vault | Dense low ensemble | C1 - G3 | A three-oscillator low instrument, compressed and dark, in a tight space. | Low chords and pedal tones; it stays controlled. |
| CA2-028 | Black Ice Passage | Cold atmospheric bass | C1 - G3 | Glass-like resonance ringing above a stable, dark bass. | Hold low notes; the glass sings on its own. |
| CA2-029 | Tectonic Choir | Ominous low chords | E1 - C4 | Stacked low analog voices, deliberately detuned, for ominous chords. | Play slow, dark chords; avoid fast passages. |
| CA2-030 | Heavy Weather | Storm underscore | C1 - G3 | A wind-blown cinematic bass with storms moving through it. | Hold low notes; the weather keeps changing. |

#### Endless Distance - Huge reverb-drenched leads

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-031 | Long Afterlight | Emotional solo line | C3 - C6 | A warm, singing lead suspended in an enormous space. | Play slowly and use the mod wheel; strike harder to open it. |
| CA2-032 | Glass Horizon | Ethereal top line | G3 - G6 | A glassy, crystalline lead dissolving into long, diffuse reflections. | Play high, slow melodies; the wheel adds a shimmer of vibrato. |
| CA2-033 | Ghost Transmission | Solitary signal melody | E3 - E6 | A lonely narrow-pulse lead disappearing into a huge echoing space. | Leave space between phrases; let the echoes answer. |
| CA2-034 | The Last Beacon | Heroic theme lead | C3 - C6 | A heroic, emotional mono lead whose saws slowly blossom. | Play long, slurred phrases; let each note bloom before moving. |
| CA2-035 | Halo Drift | Tender vocal-like line | G3 - E6 | A soft, pure, almost vocal lead floating in an exceptionally spacious tail. | Play slowly and softly; the vibrato arrives by itself. |
| CA2-036 | Burning Aurora | Soaring climax lead | E3 - E6 | A bright, aggressive sync lead that turns ethereal in a vast space. | Play bold lines; the wheel opens the fire. |
| CA2-037 | Distant Signal | Far-away memory line | G3 - E6 | A small, intimate lead heard across an immense distance. | Simple melodies, softly; the long pre-delay sets the distance. |
| CA2-038 | Pale Cathedral | Sacred high line | C4 - G6 | A high, choir-like lead with slow vowel-like movement in a cathedral. | Long held notes high up; the vowel turns slowly. |
| CA2-039 | Blue Ember | Woodwind-like melody | C4 - C7 | A breathy, flute-like lead with a soft glow of ambience. | Play like a flute: gentle attacks, small bends. |
| CA2-040 | Endless Arrival | Grand arrival theme | C3 - C6 | A powerful analog brass lead that blooms dramatically into a massive hall. | Strike harder for a bigger bloom; the wheel adds vibrato. |

#### Human Voltage - Expressive analog leads

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-041 | Amber Meridian | Expressive melodic lead | C3 - C6 | A warm reed with a brassy bite when you dig in. | Vary your velocity; aftertouch brightens a held note. |
| CA2-042 | Wounded Voltage | Wounded hero line | C3 - C6 | A slightly unstable sync lead that cries when pushed with aftertouch. | Lean into held notes; the wheel adds a wide, aching vibrato. |
| CA2-043 | Night Swimmer | Nocturnal melody | C3 - C6 | A smooth, haunting saw lead with a slow underwater vibrato. | Play long, legato lines in the middle of the keyboard. |
| CA2-044 | Silk Current | Singing lead | E3 - E6 | A soft, vocal lead with a moving formant-like character. | Slow phrases with gentle attacks; let the vowel move by itself. |
| CA2-045 | Glass Needle | Cutting top line | G3 - G6 | A thin, piercing resonant lead that cuts through any arrangement. | Velocity sets the sting; aftertouch pushes it brighter. |
| CA2-046 | Horizon Runner | Chase-scene lead | C3 - C6 | A propulsive, fast-modulated sync lead built for chase themes. | Fast lines and repeated notes; the wheel opens it up. |
| CA2-047 | False Sunrise | Hopeful rising theme | C3 - C6 | A dynamic lead that opens wide with velocity and aftertouch. | Play soft for shadow, hard for light; aftertouch and the wheel both open it. |
| CA2-048 | Copper Thread | Intimate solo line | E3 - E6 | A focused, thin analog lead with a late, expressive vibrato. | Hold notes long; the vibrato blooms after half a second. |
| CA2-049 | Arc of Breath | Breathing melody | G3 - G6 | An airy lead mixing triangle tone and soft breath noise. | Soft playing is almost all breath; dig in for the tone. |
| CA2-050 | Soft Defiance | Restrained resolve | A2 - A5 | A restrained, dark lead that becomes intense only when needed. | Stay quiet and low; strike hard or press in for the defiance. |

#### Slow Light - Lush evolving pads

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-051 | Slow Northern Light | Lush harmonic bed | E2 - E6 | Wide string-machine light, drifting open over long chords. | Hold wide chords for several bars. |
| CA2-052 | Weather Memory | Nostalgic pad | E2 - C6 | Soft detuned triangles with a gentle tape wobble, like an old recording of weather. | Hold open chords; let the wobble do the moving. |
| CA2-053 | Submerged Chapel | Sacred underwater pad | E2 - G5 | A distant organ-like pad heard from under water. | Hold simple triads in the middle register. |
| CA2-054 | Dust Bloom | Lo-fi blooming pad | E2 - C6 | A worn, grainy unison pad that blooms like dust in light. | Hold chords and let them bloom over a couple of seconds. |
| CA2-055 | Sleepwalk Circuit | Slow-reveal pad | E2 - C6 | Two pulses breathing against each other, waking over six seconds. | Hold one chord a long time; it opens very slowly. |
| CA2-056 | The Blue Archive | Classic poly bed | C2 - E6 | A clean, classic polysynth pad with the Juno's chorus. | Play chords in any register; it sits easily under other parts. |
| CA2-057 | Pale Moss | Living textural pad | C3 - E6 | A soft, rustling pad that pulses gently across the stereo field. | Hold chords; the pulse moves by itself left and right. |
| CA2-058 | Silent Meridian | Open-interval pad | C2 - E5 | A hollow pad of stacked fifths and fourths, open and unresolved. | Play single notes or simple shapes; the intervals are built in. |
| CA2-059 | Glass Fog | Shimmering haze | C3 - E6 | A misty, shimmering pad with faint metallic overtones. | Hold chords high in the middle register. |
| CA2-060 | Dawn Without Sun | Evolving dawn pad | E2 - C6 | A wide unison pad whose held notes swell in more slowly than the first chord. | Play a chord, then add notes over it; the added notes rise in like a dawn. |

#### Synthetic Orchestra - Brass, strings and cinematic ensembles

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-061 | Copper Choir | Brass ensemble chords | E2 - G#5 | A massive synthetic brass section, warm and wide. | Play full chords; accent with velocity. |
| CA2-062 | Wooden Horizon | Woodwind section | C3 - C6 | A soft, woody woodwind ensemble impression with a gentle reedy edge. | Play close-voiced chords or slow lines; the vibrato arrives on its own. |
| CA2-063 | Winter Horns | Horn section | E2 - E5 | A warm, distant French-horn section for slow chords. | Play soft, slow chords low in the middle register. |
| CA2-064 | Signal Brass | Brass stab | A2 - A5 | A bright, punchy brass stab for hits and fanfares. | Short chords, played hard; let them ring into the room. |
| CA2-065 | Amber Strings | String ensemble | E2 - E6 | A warm string-machine ensemble with the Juno's double chorus. | Hold chords across the middle of the keyboard. |
| CA2-066 | Low Chamber | Low string section | E1 - E4 | Dark, close low strings for tension and weight. | Play low, slow lines and open fifths. |
| CA2-067 | Fire Procession | Heroic brass | E2 - G#5 | A heroic brass ensemble that flares with every accent. | Accent with velocity; lean into held chords with aftertouch. |
| CA2-068 | Hollow Reeds | Reed choir | C3 - C6 | A thin, hollow reed ensemble with a fluttering edge. | Play lines and simple chords; the flutter is built in. |
| CA2-069 | Ashen Fanfare | Grand fanfare | E2 - G#5 | A grand, darkened brass fanfare with low octave support. | Play bold chords; they are big enough on their own. |
| CA2-070 | Frozen Ensemble | Glacial high strings | G3 - C7 | A glassy high ensemble, wide and cold, blooming into a wash. | Hold high chords; the wash keeps growing. |

#### Musical Machines - Arpeggios and rhythmic instruments

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-071 | Lost Telemetry | Driving underscore pulse | C3 - E5 | A retro sixteenth-note pulse, narrow and filtered, with stereo echoes. | Hold a chord; change the arpeggiator's rate and octaves. |
| CA2-072 | Hourglass Motor | Rolling triplet motor | A2 - E5 | A triplet pulse that climbs and falls like sand turning in an hourglass. | Hold a chord; try two octaves and change the chord every bar. |
| CA2-073 | Spindle Light | Sparkling cascade | E3 - G5 | A fast, sparkling three-octave arpeggio dissolving into light. | Hold a simple triad; it spins up through three octaves. |
| CA2-074 | Counting Shadows | Suspense ostinato | C2 - E4 | A dark, descending eighth-note pattern for suspense. | Hold low minor chords; it walks downward through them. |
| CA2-075 | Circuit Rain | Random glitter | E3 - G5 | Random metallic droplets falling through a chord. | Hold a wide chord; the order changes every time. |
| CA2-076 | Moss Clock | Organic swung pulse | A2 - E5 | An organic, swung pattern that keeps returning to the lowest note. | Hold a chord of three or four notes; the swing gives it a loping walk. |
| CA2-077 | Broken Compass | Gritty motor | E2 - C5 | A gritty, driven up-and-down pattern that loses its bearings. | Hold a dissonant chord; the grit grows with velocity. |
| CA2-078 | Neon Telegraph | Staccato signal | C3 - E5 | A short, clipped sync pattern tapping out messages in sixteenths. | Hold a chord; shorten or lengthen the gate to change the code. |
| CA2-079 | Paper Satellites | Floating ostinato | C3 - C6 | A soft, floating pattern that folds inward, blurred into a wash. | Hold wide chords; the outer notes meet in the middle. |
| CA2-080 | Binary Aurora | Evolving climax arpeggio | C3 - E5 | A wide, shimmering three-octave pattern whose filter rises and falls over several seconds. | Hold one chord for several bars; the light comes and goes by itself. |

#### Dark Matter - Drones, tension and noir atmospheres

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-081 | Signal Grave | Tension underscore | C1 - C4 | A deep, ominous drone with faint radio static and slow, uneasy pitch. | Hold one low note or a fifth; let it evolve. |
| CA2-082 | Frosted Air | Icy air drone | C3 - C6 | Cold, high air moving through an endless frozen space. | Hold one or two notes; the air keeps shifting on its own. |
| CA2-083 | Void Bending | Warping tension drone | E1 - E4 | A resonant drone whose centre slowly bends and wavers in a reversed space. | Hold a low note; let the resonance roam. |
| CA2-084 | Inverted Chapel | Reverse-swell pad | C2 - E5 | A reversed-feeling swell: slow to arrive, quick to vanish. | Play chords and lift your hands before they fully bloom. |
| CA2-085 | Blue Static | Noir static bed | C2 - E5 | A cold triangle drone under a worn layer of static. | Hold a note or two; the static wears and wavers. |
| CA2-086 | Coal Dust Atmosphere | Industrial dread | E1 - C4 | A dark, slowly pulsing industrial atmosphere. | Hold low notes; the pulse breathes slowly underneath. |
| CA2-087 | Ring of Ash | Inharmonic tension | C2 - C5 | A clangorous, inharmonic drone, metallic and unsettled. | Hold single notes; each key gives a different metal. |
| CA2-088 | Deep Stairwell | Dissonant abyss | E1 - C4 | A dissonant cluster sinking into an endless space. | Hold one low note; the cluster is built in. |
| CA2-089 | Forgotten Machine | Decaying machinery | C2 - C5 | An old, failing machine left running in an empty room. | Hold notes; the pitch sags and recovers like worn motors. |
| CA2-090 | Rusted Sea | Oceanic drone | C1 - C4 | A slow, heaving ocean of noise and low tone. | Hold low notes for a long time; the waves roll on their own. |

#### Fragile Machines - Keys, bells, plucks and cinematic effects

| ID | Name | Role | Range | What it is | How to play |
|---|---|---|---|---|---|
| CA2-091 | Glass Raindrops | Glittering keys / pluck | C4 - C7 | Delicate metallic drops falling into a soft, cascading space. | Play sparse high notes and broken chords. |
| CA2-092 | Hollow Felt | Intimate keys | C2 - G6 | A soft, felted electric-piano impression, warm and slightly hollow. | Play chords and voicings as on an electric piano; velocity sets the bark. |
| CA2-093 | After Rain | Reflective mallet | C3 - G6 | A soft mallet whose notes drip into a long, dark space. | Play sparse notes and leave room for the tail. |
| CA2-094 | Antique Circuit | Vintage toy organ | C2 - E6 | A worn little square-wave organ from a forgotten room. | Play chords and simple melodies; it is meant to sound old. |
| CA2-095 | Snowglass | Crystal bell | G3 - C7 | A clear, ringing bell that decays over three seconds. | Play single notes and open intervals high on the keyboard. |
| CA2-096 | Iron Rain | Metallic percussion | C2 - C6 | A hard, metallic percussive hit like rain on iron. | Play rhythms; short, hard notes work best. |
| CA2-097 | Falling Embers | Glowing pluck | C3 - E6 | A pluck whose pitch falls a little as it strikes, echoing away. | Play broken chords; let the echo carry them. |
| CA2-098 | Black Door | Thriller stab | E1 - E4 | A heavy, dark stab for doors slamming in a thriller. | Short, low chords, hard. |
| CA2-099 | Reverse Signal | Reverse swell | C2 - E5 | A sound that seems to play backwards, rising out of nothing. | Hold notes for four seconds or more; release before the next. |
| CA2-100 | Final Exposure | Closing-credits pad | E2 - E6 | The last image: a wide, slow blooming chord that fades into a wash. | Play one chord and add notes to it; let it fade completely. |

### QA measurements (the preview phrase at 48 kHz; rate spread over 44.1 / 48 / 96 kHz)

| ID | Name | Sounding RMS dB | Peak dBFS | Rate spread dB | Block 64 delta dB | Deterministic | Mono side/mid dB | Silent after (s) |
|---|---|---|---|---|---|---|---|---|
| CA2-001 | Deep Current | -17.1 | -9.4 | 0.0 | 0.0 | True | -241.9 | 0.5 |
| CA2-002 | Stone Current | -17.0 | -5.9 | 0.0 | 0.0 | True | -241.8 | 0.2 |
| CA2-003 | Velvet Gravity | -18.2 | -11.1 | 0.0 | 0.0 | True | -240.6 | 0.5 |
| CA2-004 | Blackwater Monolith | -18.4 | -8.8 | 0.0 | 0.0 | True | -240.6 | 0.5 |
| CA2-005 | Ironwood | -17.1 | -3.9 | 0.0 | 0.0 | True | -239.0 | 0.0 |
| CA2-006 | Low Orbit | -17.0 | -9.1 | 0.0 | 0.0 | True | -242.0 | 0.8 |
| CA2-007 | Earthmover | -17.0 | -2.9 | 0.0 | 0.0 | True | -241.8 | 0.2 |
| CA2-008 | Coal Engine | -17.1 | -9.6 | 0.0 | 0.0 | True | -241.9 | 0.5 |
| CA2-009 | Lunar Pressure | -17.0 | -8.6 | 0.0 | 0.0 | True | -18.9 | 0.8 |
| CA2-010 | Deepest Thread | -17.0 | -8.4 | 0.0 | 0.0 | True | -17.2 | 3.8 |
| CA2-011 | Furnace Teeth | -17.0 | -9.1 | 0.0 | 0.0 | True | -241.9 | 0.2 |
| CA2-012 | Rust Circuit | -17.0 | -9.1 | 0.0 | 0.0 | True | -241.8 | 0.2 |
| CA2-013 | Broken Reactor | -16.8 | -8.2 | 0.0 | 0.0 | True | -22.2 | 0.2 |
| CA2-014 | Corrosion Field | -17.0 | -7.4 | 0.0 | 0.0 | True | -241.9 | 0.5 |
| CA2-015 | Blackcoil | -17.0 | -8.3 | 0.0 | 0.0 | True | -241.8 | 0.5 |
| CA2-016 | Voltage Wound | -16.9 | -8.9 | 0.0 | 0.0 | True | -242.0 | 0.5 |
| CA2-017 | Charred Glass | -17.0 | -10.6 | 0.0 | 0.0 | True | -241.8 | 0.2 |
| CA2-018 | Slagline | -17.3 | -5.1 | 0.1 | 0.0 | True | -241.5 | 0.2 |
| CA2-019 | Motor Ghost | -17.2 | -6.7 | 0.0 | 0.0 | True | -26.7 | 0.5 |
| CA2-020 | Gravel Choir | -17.0 | -7.2 | 0.0 | 0.0 | True | -241.8 | 0.5 |
| CA2-021 | Cathedral Below | -18.9 | -9.2 | 0.0 | 0.0 | True | -8.9 | 7.0 |
| CA2-022 | Titan Footfall | -19.0 | -10.0 | 0.0 | 0.0 | True | -21.7 | 2.8 |
| CA2-023 | Gravity Well | -19.0 | -12.5 | 0.0 | 0.0 | True | -243.1 | 2.0 |
| CA2-024 | Cold Continent | -19.0 | -5.4 | 0.0 | 0.0 | True | -11.8 | 9.2 |
| CA2-025 | The Hollow Sun | -18.9 | -10.9 | 0.0 | 0.0 | True | -37.6 | 2.5 |
| CA2-026 | Abyssal Beacon | -19.3 | -5.9 | 0.0 | 0.0 | True | -7.1 | 11.2 |
| CA2-027 | Pressure Vault | -17.9 | -6.4 | 0.0 | 0.0 | True | -14.5 | 1.5 |
| CA2-028 | Black Ice Passage | -19.0 | -10.6 | 0.0 | 0.0 | True | -26.4 | 3.2 |
| CA2-029 | Tectonic Choir | -19.0 | -5.4 | 0.0 | 0.0 | True | -5.3 | 4.8 |
| CA2-030 | Heavy Weather | -19.2 | -5.5 | 0.0 | 0.0 | True | -9.3 | 10.0 |
| CA2-031 | Long Afterlight | -19.0 | -6.6 | 0.3 | 0.0 | True | -9.7 | 6.0 |
| CA2-032 | Glass Horizon | -19.0 | -4.1 | 0.1 | 0.0 | True | -6.0 | 5.0 |
| CA2-033 | Ghost Transmission | -19.1 | -4.5 | 0.1 | 0.0 | True | -10.8 | 5.2 |
| CA2-034 | The Last Beacon | -19.0 | -5.9 | 0.1 | 0.0 | True | -8.6 | 6.2 |
| CA2-035 | Halo Drift | -19.0 | -7.3 | 0.1 | 0.0 | True | -10.5 | 7.8 |
| CA2-036 | Burning Aurora | -19.7 | -8.2 | 0.1 | 0.0 | True | -12.8 | 16.8 |
| CA2-037 | Distant Signal | -19.0 | -5.8 | 0.1 | 0.0 | True | -7.5 | 6.5 |
| CA2-038 | Pale Cathedral | -19.0 | -4.6 | 0.0 | 0.0 | True | -7.0 | 6.0 |
| CA2-039 | Blue Ember | -19.0 | -7.6 | 0.1 | 0.0 | True | -11.9 | 5.8 |
| CA2-040 | Endless Arrival | -19.6 | -6.4 | 0.0 | 0.0 | True | -8.2 | 8.8 |
| CA2-041 | Amber Meridian | -19.0 | -9.9 | 0.0 | 0.0 | True | -33.2 | 0.5 |
| CA2-042 | Wounded Voltage | -19.0 | -11.2 | 0.0 | 0.0 | True | -21.8 | 2.2 |
| CA2-043 | Night Swimmer | -19.0 | -9.6 | 0.0 | 0.0 | True | -17.6 | 2.8 |
| CA2-044 | Silk Current | -19.0 | -9.6 | 0.1 | 0.0 | True | -15.1 | 2.2 |
| CA2-045 | Glass Needle | -19.0 | -3.9 | 0.0 | 0.0 | True | -26.1 | 1.0 |
| CA2-046 | Horizon Runner | -19.2 | -11.0 | 0.0 | 0.0 | True | -30.3 | 0.8 |
| CA2-047 | False Sunrise | -19.0 | -9.8 | 0.1 | 0.0 | True | -18.4 | 2.0 |
| CA2-048 | Copper Thread | -19.0 | -6.7 | 0.0 | 0.0 | True | -16.6 | 1.8 |
| CA2-049 | Arc of Breath | -19.0 | -9.3 | 0.2 | 0.0 | True | -16.4 | 2.0 |
| CA2-050 | Soft Defiance | -19.0 | -10.6 | 0.0 | 0.0 | True | -27.4 | 1.2 |
| CA2-051 | Slow Northern Light | -20.0 | -5.2 | 0.0 | 0.0 | True | -4.8 | 4.2 |
| CA2-052 | Weather Memory | -19.9 | -7.9 | 0.0 | 0.0 | True | -10.8 | 4.2 |
| CA2-053 | Submerged Chapel | -19.8 | -7.7 | 0.0 | 0.0 | True | -7.3 | 7.5 |
| CA2-054 | Dust Bloom | -20.1 | -10.1 | 0.0 | 0.0 | True | -9.9 | 4.5 |
| CA2-055 | Sleepwalk Circuit | -20.0 | -8.3 | 0.0 | 0.0 | True | -11.2 | 5.5 |
| CA2-056 | The Blue Archive | -20.0 | -5.6 | 0.0 | 0.0 | True | -4.3 | 2.8 |
| CA2-057 | Pale Moss | -20.0 | -7.2 | 0.0 | 0.0 | True | -10.5 | 4.0 |
| CA2-058 | Silent Meridian | -20.3 | -5.0 | 0.1 | 0.0 | True | -3.8 | 9.5 |
| CA2-059 | Glass Fog | -20.0 | -6.6 | 0.0 | 0.0 | True | -9.7 | 6.5 |
| CA2-060 | Dawn Without Sun | -20.0 | -7.7 | 0.0 | 0.0 | True | -8.6 | 6.2 |
| CA2-061 | Copper Choir | -19.0 | -6.9 | 0.0 | 0.0 | True | -13.1 | 3.0 |
| CA2-062 | Wooden Horizon | -19.0 | -6.8 | 0.0 | 0.0 | True | -15.4 | 1.8 |
| CA2-063 | Winter Horns | -19.0 | -7.3 | 0.0 | 0.0 | True | -11.2 | 3.8 |
| CA2-064 | Signal Brass | -19.8 | -9.2 | 0.0 | 0.0 | True | -15.6 | 1.2 |
| CA2-065 | Amber Strings | -19.0 | -6.5 | 0.1 | 0.0 | True | -9.9 | 3.0 |
| CA2-066 | Low Chamber | -19.0 | -6.3 | 0.0 | 0.0 | True | -13.4 | 3.8 |
| CA2-067 | Fire Procession | -19.0 | -6.4 | 0.0 | 0.0 | True | -15.6 | 3.2 |
| CA2-068 | Hollow Reeds | -19.0 | -5.7 | 0.0 | 0.0 | True | -14.5 | 1.2 |
| CA2-069 | Ashen Fanfare | -19.1 | -6.1 | 0.0 | 0.0 | True | -9.5 | 4.8 |
| CA2-070 | Frozen Ensemble | -19.0 | -5.2 | 0.0 | 0.0 | True | -4.6 | 7.5 |
| CA2-071 | Lost Telemetry | -20.0 | -2.5 | 0.0 | 0.0 | True | -15.5 | 1.8 |
| CA2-072 | Hourglass Motor | -20.0 | -6.4 | 0.0 | 0.0 | True | -16.1 | 1.2 |
| CA2-073 | Spindle Light | -20.0 | -5.3 | 0.0 | 0.0 | True | -12.8 | 4.2 |
| CA2-074 | Counting Shadows | -20.0 | -5.9 | 0.0 | 0.0 | True | -16.0 | 2.0 |
| CA2-075 | Circuit Rain | -20.0 | -3.6 | 0.0 | 0.0 | True | -14.0 | 1.5 |
| CA2-076 | Moss Clock | -20.0 | -4.7 | 0.0 | 0.0 | True | -17.4 | 1.2 |
| CA2-077 | Broken Compass | -20.0 | -5.0 | 0.0 | 0.0 | True | -16.4 | 0.8 |
| CA2-078 | Neon Telegraph | -20.0 | -3.9 | 0.0 | 0.0 | True | -15.9 | 0.8 |
| CA2-079 | Paper Satellites | -20.0 | -7.5 | 0.0 | 0.0 | True | -11.4 | 6.0 |
| CA2-080 | Binary Aurora | -20.0 | -5.1 | 0.0 | 0.0 | True | -3.7 | 1.8 |
| CA2-081 | Signal Grave | -21.1 | -8.8 | 0.0 | 0.0 | True | -10.2 | 8.8 |
| CA2-082 | Frosted Air | -20.9 | -6.8 | 0.3 | 0.0 | True | -7.7 | 22.8 |
| CA2-083 | Void Bending | -21.0 | -5.6 | 0.0 | 0.0 | True | -12.0 | 8.2 |
| CA2-084 | Inverted Chapel | -21.0 | -7.9 | 0.1 | 0.0 | True | -3.6 | 8.2 |
| CA2-085 | Blue Static | -21.1 | -11.9 | 0.0 | 0.0 | True | -18.0 | 5.5 |
| CA2-086 | Coal Dust Atmosphere | -21.5 | -10.2 | 0.0 | 0.0 | True | -14.5 | 5.5 |
| CA2-087 | Ring of Ash | -21.0 | -6.7 | 0.0 | 0.0 | True | -11.7 | 9.5 |
| CA2-088 | Deep Stairwell | -21.3 | -5.6 | 0.1 | 0.0 | True | -6.6 | 26.8 |
| CA2-089 | Forgotten Machine | -21.0 | -9.1 | 0.0 | 0.0 | True | -16.3 | 4.5 |
| CA2-090 | Rusted Sea | -21.2 | -7.4 | 0.0 | 0.0 | True | -11.1 | 9.2 |
| CA2-091 | Glass Raindrops | -21.0 | -4.8 | 0.1 | 0.0 | True | -9.4 | 1.0 |
| CA2-092 | Hollow Felt | -21.0 | -7.7 | 0.0 | 0.0 | True | -2.8 | 0.5 |
| CA2-093 | After Rain | -21.5 | -8.1 | 0.1 | 0.0 | True | -6.0 | 6.5 |
| CA2-094 | Antique Circuit | -21.0 | -12.4 | 0.0 | 0.0 | True | -4.3 | 1.0 |
| CA2-095 | Snowglass | -21.0 | -6.8 | 0.1 | 0.0 | True | -13.0 | 2.0 |
| CA2-096 | Iron Rain | -24.8 | -1.3 | 0.0 | 0.0 | True | -15.6 | 0.0 |
| CA2-097 | Falling Embers | -21.0 | -5.5 | 0.0 | 0.0 | True | -13.8 | 1.0 |
| CA2-098 | Black Door | -21.0 | -13.6 | 0.0 | 0.0 | True | -28.6 | 1.8 |
| CA2-099 | Reverse Signal | -21.0 | -6.4 | 0.0 | 0.0 | True | -14.8 | 2.8 |
| CA2-100 | Final Exposure | -21.0 | -8.7 | 0.0 | 0.0 | True | -7.8 | 9.0 |
