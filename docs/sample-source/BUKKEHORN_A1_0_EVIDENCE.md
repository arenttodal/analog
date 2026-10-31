# Bukkehorn 4.5 import — A1.0 evidence and extraction

_31 October 2026. Milestone A1.0 of the Sample Source importer (MVP A1). Analysis only: nothing
plays yet, no product code changed._

**Inputs:** the NKI (`Evenant Engine v2 - Finding Rhythm - Bukkehorn 4.5.nki`, 35,607 bytes),
the full KSP (`Evenant_Engine_Bukkehorn_Full_KSP.txt`), the eight Kontakt screenshots, the
research guide (hypotheses), and five samples from the instrument's folder.

**Tools (reproducible):**
- `tools/kontakt/nki_inspect.py` — a bounded reader for this NKI. It walks the NI container,
  decompresses the FastLZ sub-tree, and reads the Kontakt preset chunks: the program, its 48
  groups, 333 zones (with loops), the file list, and the script with its saved persistent values.
  It was written from the documented format (ConvertWithMoss's readers used as reading material,
  no code copied) and executes nothing.
- `tools/kontakt/bukkehorn_manifest.py` — turns that dump into
  [`bukkehorn-4.5-manifest.json`](bukkehorn-4.5-manifest.json): every group in lane order, every
  zone, the saved lane state, and which samples are present.

Note names follow Kontakt's convention (MIDI 60 = C3).

## 1. Summary

- **Every group, zone and sample reference is recoverable from the NKI itself,** with no guessing:
  - 48 groups, in the script's lane order;
  - 333 zones (33 groups have zones, 15 are empty placeholders);
  - 201 distinct sample files, all relative to `Evenant Engine v2 - Finding Rhythm - Bukkehorn 4.5 Samples/`.
- **Cross-checks agree.** The lane offsets (A 0, C 12, B 24, D 36), the group levels and pans,
  and the effect slots are confirmed three ways: KSP, NKI and screenshots (§3).
- **The saved instrument state is fully recoverable.** Each lane's selected group and every lane
  control are saved in the NKI with the script. This gives one faithful combined patch (§5).
- **The 16 user presets are not recoverable.** `%PRESET_DATA` is saved as `0` (empty). No
  combined presets will be invented from them.
- **Blocked:** 196 of the 201 sample files are not here yet. A1.1+ needs the original
  `… Bukkehorn 4.5 Samples` folder. The five supplied files match their NKI entries exactly
  (name and length in frames, §6).
- **The research guide is contradicted on its main claim.** The screenshots show Replika in
  **Tape** mode (return −∞), not Diffusion (§7).

## 2. Evidence, by source

### Verified in KSP
- Four lanes; 12 groups each; offsets `$A_START 0`, `$C_START 12`, `$B_START 24`, `$D_START 36`.
- Lane names are read with `group_name()`.
- `on note` disallows every group, then allows one per lane: `START + ENGINE_x_SELECT`. That is
  one group per lane, gated by MUTE / SOLO (solo exclusive, mute blocks).
- Per-lane controls:
  - level (`ENGINE_PAR_VOLUME`) and pan on the lane's 12 groups;
  - tune (`ENGINE_PAR_TUNE`, 500000 = 0 st; label = (v − 500000) / 13888, rounded);
  - COLOUR (`ENGINE_PAR_GAIN1` on group insert slot 1).
- Shared controls: ATTACK / RELEASE (`ENV1` on every group), LP cutoff (instrument insert
  slot 0), HP (slot 3), SPACE (`SEND_EFFECT_OUTPUT_GAIN` of send slot 5), CHARGER (SuperGT mix,
  slot 4), width (`ENGINE_PAR_STEREO`, slot 5).
- Presets: 16 × 26 values (`%PRESET_DATA`), in the order `P_ENG_A` … `P_COLOUR_D` (KSP lines 19–44).
- The script embedded in the NKI is **identical** to the supplied KSP (after normalising line
  endings).

### Verified in NKI
- Program name; 48 groups (name, linear volume, pan −1..1, tune ratio, key tracking on, no
  release trigger, interpolation standard).
- 333 zones, each with:
  - its sample;
  - its key range, root and velocity range;
  - per-zone tune (a ratio: −27 … +22 cents);
  - per-zone volume (≠ 1 in 14 groups);
  - sample start (non-zero in 3 zones), length in frames, rate and channels;
  - loops (§4).
- The 15 empty placeholder groups are A11, B 9, C5–C12 and D8–D12.
- Key ranges overlap in 15 groups (overlapping zones sound together in Kontakt). Velocity
  splits exist only in **Bright Moog** and **D6**.
- Samples are 48 kHz stereo except some 44.1 kHz (mono and stereo) and 96 kHz files. 74 files
  are shared by more than one group (lane copies, e.g. Tagelharpa / Nyckelharpa / Flageolett in
  both A and C).
- Saved persistent values:
  - `ENGINE_A/B/C/D_SELECT` = 7 / 1 / 5 / 5;
  - `VOLUME1–4`, `PAN1–4`, `TUNE_A–D` (all 500000 = 0 st), `COLOUR_A–D`;
  - `cutoff_slider` 960031, `HP1` 0, `ATTACK_master` 621428, `RELEASE_master` 671429;
  - `SPACE` 336471, `CHARGER` 607143, `CHOWIDTH` 592857;
  - mute / solo all 0, `CURRENT_PRESET` 0, `%PRESET_DATA` 0.
- The resource container is `../../Evenant Engine 1.1.nkr` (not supplied; holds the
  performance view graphics, not needed).

### Verified in Kontakt screenshots
Note: the screenshots are taken with **Edit All Groups**; the group shown is Violin 1a.

- **Group FX:** SV HP1, then EQ1 (1-band: 2.6 kHz, BW 2.8 oct, −7.9 dB).
- **Amplifier:** 11.7 dB, 7 R.
- **Modulation:**
  - two AHDSR envelopes to volume: (1) curve −33 %, A 509.8 ms, H 0, D 1.1 k ms, S 0 dB,
    R 1.1 k ms, retrigger; (2) curve −33 %, A 1.0 ms, H 0, D 750 ms, S −12 dB, R 1.1 k ms,
    retrigger;
  - a sine LFO (rate 4, tempo-synced, retrigger) to cutoff;
  - velocity to volume (partial); pitch bend to pitch.
- **Instrument inserts:**
  - SV LP2 (15.3 kHz, reso 0); Tape (**bypassed**);
  - EQ3 (109.5 Hz −6.2 dB / 1.4 kHz −2.4 dB / 7.5 kHz −1.0 dB);
  - SV HP1;
  - SuperGT (input −15 dB, saturate 4.0 moderate, compress 5.0, attack 4.3, release 5.0, Fat,
    mix 60.7 %, output 0 dB);
  - Stereo Modeller (pseudo stereo, spread 18.6 %); Choral; Sends (all four at 0.0 dB).
- **Send FX:**
  - Replika in **Tape** mode: 376.5 ms, feedback 44.9 %, low cut 10 Hz, high cut 19 kHz, ping
    pong, tape age 25 %, flutter 30 %, saturate 10 %, **return −∞**;
  - then Plate, Convolve and Reverb (their settings are not shown).

### Inferred (consistent across sources, not stated directly)
- **Kontakt's normalised controls map to the values shown, at the saved points:**
  - `VOLUME1` 988789 ↔ group 0 at 3.867 (+11.75 dB, shown 11.7 dB);
  - `PAN1` 533333 ↔ +0.067 ((v − 500000) / 500000), shown 7 R; likewise `PAN2` → B groups
    +0.156, `PAN3` → C groups +0.156, `PAN4` → D groups −0.022;
  - `cutoff_slider` 960031 ↔ 15.3 kHz; `ATTACK_master` 621428 ↔ 509.8 ms;
    `RELEASE_master` 671429 ↔ 1.1 k ms; `CHARGER` 607143 ↔ 60.7 %.
  
  Only these single points are known. The curves between them are Kontakt's.
- **The "washy" tail comes from send slot 5, not Replika.** SPACE drives send slot 5, which is
  the Reverb in the screenshots; Replika's return is −∞. So the tail comes from the Reverb (and
  perhaps Plate / Convolve at their own returns), not from Replika.
- **Zone tune is a frequency ratio:** 12 · log2(t) semitones.
- **Lane naming was renamed historically:** groups 12–23 are named "Violin 1b", "B 5"… and 24–35
  "Plucked Tex", "C5"…. The names follow an older lane naming, but the script plays 12–23 as
  lane **C** and 24–35 as lane **B**. The script's offsets are authoritative; this report
  follows them.

### Unknown or needs your input
- The **sample folder** (196 files missing).
- **Per-group differences.** Envelope, EQ1 and filter values are known only for Violin 1a. The
  screenshots were taken with Edit All Groups, and the script sets attack / release / COLOUR for
  every group, so they are probably shared. They are not proven per group.
- The **Plate, Convolve and Reverb** send settings, and their return levels.
- How Kontakt uses the **eight loop slots** in the texture groups (§4).
- The **velocity → volume depth** (the slider is visible, its value isn't), and the LFO depth.

## 3. Cross-checks between sources

| Claim | KSP | NKI | Screenshots |
|---|---|---|---|
| Lane offsets A 0 / C 12 / B 24 / D 36 | declared | the saved pans land on exactly those group blocks (A +0.067, C +0.156, B +0.156, D −0.022) | — |
| Group level / pan of Violin 1a | set from `VOLUME1` / `PAN1` | 3.867 linear (+11.75 dB) / +0.067 | 11.7 dB / 7 R |
| LP cutoff 15.3 kHz | slot 0, `cutoff_slider` | 960031 saved | SV LP2 15.3 kHz |
| Attack 509.8 ms, release 1.1 k ms | ENV1 on every group | 621428 / 671429 saved | envelope 1 |
| SuperGT mix 60.7 % | `CHARGER` → slot 4 | 607143 saved | 60.7 % |
| FX slots | FILTER 0, TAPE 1, HP 3, SUPERGT 4, WIDTH 5 | — | SV LP2, Tape, EQ3, SV HP1, SuperGT, Stereo |
| The embedded script | — | identical to the supplied KSP | — |

## 4. Loops

- 258 zones have no loop: they play to the sample's end, or until release.
- 9 zones have one loop (mode "until end").
- 66 zones carry eight loop slots: slot 1 is the whole sample, alternating; slots 2–8 are
  consecutive one-second spans. These are in **Granulated, Voxx, Plucked Tex, String Tex (2),
  Soft Moog, Bright Moog, D6, D7**.

Which slots are active in Kontakt is not certain from the data. The first import will play the
sample with its first loop where one exists (mode "until end"), and will flag these groups in
the review.

## 5. The saved instrument (the one combined patch that can be reconstructed)

| Lane | Selected group | Level (saved) | Pan | Tune | Colour |
|---|---|---|---|---|---|
| A | 7 **Voxx** | 988789 (+11.75 dB) | +0.067 | 0 st | 281250 |
| B | 25 **Pedal Tex** | 830718 (+7.21 dB) | +0.156 | 0 st | 361607 |
| C | 17 **Swells 8va** | 973094 (+11.33 dB) | +0.156 | 0 st | 388393 |
| D | 41 **D6** | 845292 (+7.66 dB) | −0.022 | 0 st | 361608 |

Shared: LP 15.3 kHz, HP 0, attack 509.8 ms, release 1.1 k ms, SuperGT mix 60.7 %, width
592857, SPACE 336471.

The dB values are from the groups' stored volumes, which match these saved controls.

## 6. The supplied samples

All five match their NKI entries by name and length in frames:

| File | NKI zone(s) | Frames (NKI / file) |
|---|---|---|
| `Audio (Bounce) (Bounce) (Bounce) [2025-10-15 205756].wav` | String Tex (27): E2–E4, root E2 | 108000 / 108000 |
| `… [2025-10-15 205755]-1.wav` | String Tex: D2–D#2, root D2 | 436686 / 436686 |
| `… [2025-10-15 205752]-1.wav` | String Tex: A#0–C1, root C1 | 500652 / 500652 |
| `Audio (Bounce) (Bounce) [2025-10-15 205732].wav` | Swells8va (5) and Swells 8va (17): C#2–D2, root D2 | 411998 / 411998 |
| `Audio [2025-10-18 162558].aif` | Granulated (4): D3–E3, root E3 | 393001 / 393001 |

So String Tex has 3 of its 9 zones here. No group is complete.

## 7. Where the research guide disagrees with the evidence

| Guide | Evidence |
|---|---|
| The wash is Replika in **Diffusion** mode | Replika is in **Tape** mode with return −∞; SPACE drives send slot 5 (Reverb) |
| 202 samples | 201 distinct files referenced by the zones (333 entries in the file list) |
| `A_START` 36 (C2) as a first MIDI note | `$A_START` is 0, a group index (lane A's first group) |
| `PRE` 16 = predelay or preset count | `$NUM_PRESETS` is 16; there is no `PRE` constant |
| "One sample per key, chromatic" | zones span several keys from one root (e.g. Violin 1a: 6 roots over D#0–D#6); some overlap |

## 8. All 48 groups

| # | Lane / slot | Group name (NKI) | Zones | Keys | Velocity | Loops | Level / pan | Supplied |
|---:|---|---|---:|---|---|---|---|---:|
| 0 | A 1 | Violin 1a | 8 | D#0–D#6 (6 roots) | full | none | +11.8 dB / +0.067 | 0 |
| 1 | A 2 | Tagelharpa | 2 | G#-1–B5 (2 roots) | full | none | +11.8 dB / +0.067 | 0 |
| 2 | A 3 | Nyckelharpa | 12 | C1–F#4 (12 roots) | full | none | +11.8 dB / +0.067 | 0 |
| 3 | A 4 | Flageolett | 1 | G#-1–C#6 (1 roots) | full | none | +11.8 dB / +0.067 | 0 |
| 4 | A 5 | Granulated | 20 | C-2–A3 (18 roots) | full | 17 of 20 | +11.8 dB / +0.067 | 1 |
| 5 | A 6 | Swells8va | 21 | C-1–F3 (12 roots) | full | none | +11.8 dB / +0.067 | 1 |
| 6 | A 7 | Nyckelharpa Hi | 12 | C0–F#3 (12 roots) | full | none | +11.8 dB / +0.067 | 0 |
| 7 | A 8 | Voxx **(saved lane A)** | 13 | C1–F3 (8 roots) | full | 13 of 13 | +11.8 dB / +0.067 | 0 |
| 8 | A 9 | A9 | 6 | C0–G8 (6 roots) | full | none | +11.8 dB / +0.067 | 0 |
| 9 | A 10 | A10 | 2 | G#-1–B5 (2 roots) | full | none | +11.8 dB / +0.067 | 0 |
| 10 | A 11 | A11 | 0 | — | — | — | +11.8 dB / +0.067 | — |
| 11 | A 12 | A12 | 1 | G#-1–C#6 (1 roots) | full | none | +11.8 dB / +0.067 | 0 |
| 12 | C 1 | Violin 1b | 8 | D#0–D#6 (6 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 13 | C 2 | Tagelharpa | 2 | G#-1–B5 (2 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 14 | C 3 | Nyckelharpa | 12 | C1–F#4 (12 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 15 | C 4 | Flageolett | 1 | G#-1–C#6 (1 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 16 | C 5 | B 5 | 30 | D-1–C#5 (25 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 17 | C 6 | Swells 8va **(saved lane C)** | 21 | C-1–F3 (12 roots) | full | none | +11.3 dB / +0.156 | 1 |
| 18 | C 7 | B 7 | 12 | C1–F#4 (12 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 19 | C 8 | B 8 | 1 | G#-1–C#6 (1 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 20 | C 9 | B 9 | 0 | — | — | — | +11.3 dB / +0.156 | — |
| 21 | C 10 | B 10 | 6 | F1–G8 (6 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 22 | C 11 | B 11 | 1 | C-2–D#5 (1 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 23 | C 12 | B12 | 1 | A#0–E5 (1 roots) | full | none | +11.3 dB / +0.156 | 0 |
| 24 | B 1 | Plucked Tex | 20 | C-2–A3 (20 roots) | full | 19 of 20 | +7.2 dB / +0.156 | 0 |
| 25 | B 2 | Pedal Tex **(saved lane B)** | 8 | G0–F4 (8 roots) | full | none | +7.2 dB / +0.156 | 0 |
| 26 | B 3 | Rain Tex | 11 | D0–G4 (10 roots) | full | none | +7.2 dB / +0.156 | 0 |
| 27 | B 4 | String Tex | 9 | F#-1–E4 (9 roots) | full | 2 of 9 | +7.2 dB / +0.156 | 3 |
| 28 | B 5 | C5 | 0 | — | — | — | +7.2 dB / +0.156 | — |
| 29 | B 6 | C6 | 0 | — | — | — | +7.2 dB / +0.156 | — |
| 30 | B 7 | C7 | 0 | — | — | — | +7.2 dB / +0.156 | — |
| 31 | B 8 | C8 | 0 | — | — | — | +7.2 dB / +0.156 | — |
| 32 | B 9 | C9 | 0 | — | — | — | +7.2 dB / +0.156 | — |
| 33 | B 10 | C10 | 0 | — | — | — | +7.2 dB / +0.156 | — |
| 34 | B 11 | C11 | 0 | — | — | — | +7.2 dB / +0.156 | — |
| 35 | B 12 | C12 | 0 | — | — | — | +7.2 dB / +0.156 | — |
| 36 | D 1 | Moog Wave | 30 | D-1–C#5 (25 roots) | full | none | +7.7 dB / -0.022 | 0 |
| 37 | D 2 | Moog Hi | 30 | D-2–C#4 (25 roots) | full | none | +7.7 dB / -0.022 | 0 |
| 38 | D 3 | Gunnotron | 8 | E1–A#3 (8 roots) | full | none | +7.7 dB / -0.022 | 0 |
| 39 | D 4 | Soft Moog | 1 | G#-1–C5 (1 roots) | full | 1 of 1 | +7.7 dB / -0.022 | 0 |
| 40 | D 5 | Bright Moog | 5 | A-2–C5 (4 roots) | split | 5 of 5 | +7.7 dB / -0.022 | 0 |
| 41 | D 6 | D6 **(saved lane D)** | 10 | F-1–A5 (8 roots) | split | 10 of 10 | +7.7 dB / -0.022 | 0 |
| 42 | D 7 | D7 | 8 | C0–G#5 (8 roots) | full | 8 of 8 | +7.7 dB / -0.022 | 0 |
| 43 | D 8 | D8 | 0 | — | — | — | +7.7 dB / -0.022 | — |
| 44 | D 9 | D9 | 0 | — | — | — | +7.7 dB / -0.022 | — |
| 45 | D 10 | D10 | 0 | — | — | — | +7.7 dB / -0.022 | — |
| 46 | D 11 | D11 | 0 | — | — | — | +7.7 dB / -0.022 | — |
| 47 | D 12 | D12 | 0 | — | — | — | +7.7 dB / -0.022 | — |

## 9. What the importer will reuse (OSP), and the plan for A1.1

- **Loading and analysis:** OSP's loader and sample store, `InstrumentModel` and
  `PlaybackSource`, run off the audio thread with content hashes (portable assets, relink).
- **Playing many files:** OSP's `InstrumentSet` plays one recording per pitch group, choosing
  the nearest root. A Kontakt zone has an explicit key range and velocity range, and its root
  is not always centred; zones may overlap. The importer needs an **explicit zone map** (key
  range, velocity range, root, tune, level, loop) on top of the same members. That is a small,
  opt-in addition: "this member plays exactly these keys / velocities". It is not a second
  sampler.
- **In ANDOR ANALOG:** ORGANIC (layer B) plays one recording per voice (`OrganicVoice`). A1.1
  would let a voice read the zone a key and velocity select from the imported set, with the
  same resampling, loop and envelope path. Overlapping zones would play as extra voices.
- **Effects:** only what ANALOG already has (its filter, DRIVE, SPACE / ECHO, width). Every
  Kontakt FX value above is kept as import metadata (manifest `savedState`), not imitated.

**Next (needs the sample folder):** A1.1 plays one complete group, e.g. String Tex (27), or any
group you choose, from the manifest, checked against the zones in §8.
