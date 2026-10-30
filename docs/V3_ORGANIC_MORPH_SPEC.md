# ANDOR Analog V3 — Organic Morph Expansion

_Queued as the next improvement (owner's specification, 29 October 2026). Not started: the
work begins when the owner says so. Recorded verbatim below._

Complete product, DSP, UX and implementation specification
Date: 29 October 2026
Repositories: arenttodal/analog (default branch at audit: claude/upbeat-allen-3i0jey), with arenttodal/OSP as submodule
Status: Proposed expansion; no code changes made
Formats: VST3, AU (macOS), Standalone

## 1. Product vision and deliverables

Turn the working ANDOR Analog into an expressive three-source instrument consisting of two analog-style oscillators and a third sample-derived instrument, with genuine timbral morphing between the organic recording and an analog carrier. A single recorded nyckelharpa or cello note should be playable across the keyboard, either dry, layered, spectrally imprinted onto an analog oscillator, or fused into a coherent new instrument. This is not merely crossfading two audio outputs.

Required features:
1. OSC 1: independently switchable CLASSIC VCO, DCO / ENSEMBLE, WIDE VCO models.
2. OSC 2: the same independent choices, with its own wave/tuning/PWM/level/sub settings.
3. ORGANIC: one imported tonal recording playable using ANDOR OSP one-shot/sustain technology.
4. DYNAMIC CARDS: the existing ANDOR hero/dual/triple card arrangement, according to how many sources are enabled.
5. MORPH: separate from oscillator balance; 0% original sample, 100% selected original analog carrier; genuinely hybrid intermediate timbres.
6. MORPH METHODS: IMPRINT first, FUSE as signature engine, WEAVE later.
7. PERFORMANCE: existing polyphony, glide, note stealing, pedal, per-note filter, modulation, reimagined/granular and shared effects where compatible.
8. COMPATIBILITY: existing Analog V1/V2 presets and host automation must be preserved; the shared OSP GUI and plugin remain unchanged for existing products.

Not V3 blockers: sample multisets, a live sidechain-audio morph effect, complex user-drawn partial curves, exact emulations of commercial synths, dynamic per-note real-time STFT processing, new GUI visual vocabulary.

Key musical distinction
- BLEND continues to balance OSC 1 versus OSC 2.
- MIX/LEVEL balances the resulting audio sources in normal layered playback.
- MORPH changes harmonic shape, formants, moving texture and attack characteristics to generate a new sound between the organic recording and the selected oscillator carrier.

Never implement MORPH by simply renaming or reusing analog.blend.

## 2. Verified baseline: current working implementation

The connected arenttodal/analog source includes:
- dsp/Synth.cpp/.h: 16-voice architecture, sample-accurate MIDI, poly/mono/unison/glide, 4× oversampling, decimation, shared OSP effects.
- dsp/Oscillator.h, PolyBlep.h: independently phased, band-limited saw/pulse/triangle synthesis and hard sync.
- dsp/LadderFilter.h, Envelope.h: nonlinear four-pole ladder and ADSR.
- dsp/LiveGranulator.*: live recording of the synthesizer's own output for OSP granular/reimagined processing.
- dsp/Patch.h: patch definitions and existing oscillator balance.
- product/Source/AnalogParameters.*: Analog's product-only parameters, appended after OSP's frozen schema.
- product/Source/AnalogEngine.*, Product.cpp, AnalogPopovers.*: JUCE shell integration, GUI, preset migrations.
- product/golden/parameter-schema.txt: host parameter regression reference.
- docs/STATUS.md: records tested V2 and 34 factory presets; original macOS AU/auval and Linux CI passed. Formal DAW/pluginval, signing and final listening remained outstanding at the audit.

The V2 STATUS document records a 22-sample output latency with existing 4× oversampling. That is the baseline to measure, not an assumption for the new path.

ANDOR OSP now supports ProductInfo::addParameters, ProductInfo::stateVersion and migrateState, Engine::latencySamples(), generator card sourcePicture(), and a shared shell. These are useful extension points. However, the current generator mode presents one oscillator card, rejects file loading, and does not natively model generators and recordings together. Do not try to make hybrid source slots appear by inventing fake LoadedInstrument sample objects.

The original plugin identity is fixed:

```
PLUGIN_CODE: Aanl
MANUFACTURER_CODE: Ospx
BUNDLE_ID: com.osp.andoranalog
STATE_TYPE: ANDOR_ANALOG
PRESET_EXTENSION: .andoranalog
```

Preserve all existing IDs, ranges, choice ordering, defaults and state migration, including the V1→V2 migration in Product.cpp.

## 3. High-level signal architecture

```
MIDI NOTE / VELOCITY / BEND / MODULATION
                │
         ParentHybridVoice [up to 16]
                │
  ┌─────────────┼─────────────────────────┐
  │             │                         │
OSC1         OSC2                    OrganicVoice
(model A)    (model B)          (OSP one-shot/continuation)
  │             │                         │
  └──── analog OSC blend ─┐               │
                         │               ├──> dry organic
                selected carrier          │
                         └──── MORPH ENGINE
                                      │
                          hybrid pre-filter sound
                                      │
                           existing V2 4-pole filter
                                      │
                             existing amp envelope
                                      │
                       decimation + post-processing
                                      │
                               stereo output
```

Keep one parent voice per MIDI note. The two oscillator renderers, organic sample reader, morph renderer and filter all belong to that same parent voice, so they share sample-accurate note events and one voice-stealing rule. Do not instantiate a whole second copy of OSP or launch a separate 16-voice synth in each oscillator card.

### 3.1 Explicit source identities

Source IDs never change with screen position:

| Source | Stable identity | Parameter namespace |
|---|---|---|
| Oscillator 1 | osc1 | analog.osc1.* |
| Oscillator 2 | osc2 | analog.osc2.* |
| Organic recording | organic | analog.organic.* |

Visual order is the enabled subset in the order OSC 1, OSC 2, ORGANIC. OSC 2 plus ORGANIC is therefore a two-card presentation. Do not map presentation index 0 to the OSC 1 parameter namespace when OSC 1 is disabled.

### 3.2 Routing and mix laws

Define the carrier selector analog.morph.carrier: OSC 1, OSC 2, BOTH. The BOTH carrier uses current analog.blend to create the oscillator combination. Let S be organic dry audio, B be selected pre-filter oscillator carrier, m morph position:
- H(S,B,0) = S (the original playable sample)
- H(S,B,1) = B (the original analog carrier)
- 0 < m < 1: method-specific timbral synthesis H, not merely (1-m)S+mB.

If OSC 1 is selected as carrier, OSC 2 may remain as an independently mixable non-morphed oscillator. If BOTH is selected, do not also add both selected oscillators into a second dry sum unless an intentional parallel mix is engaged. Level compensation must consider correlated signals and avoid double loudness at midpoint.

Filter the resulting hybrid through the existing Analog per-voice filter and amp stages where feasible. Existing OSC2-to-OSC1 sync/FM/Poly Mod runs before the morph and must remain musically available.

### 3.3 Source behaviors

- OSC generators can sound with no sample, without triggering file loading.
- Organic dry playback works with zero OSCs enabled.
- MORPH is disabled when no organic sample/carrier exists; its stored value is retained for later reload.
- Removing a sample during a sustained note fades/retires source generations safely, never dereferences freed memory.
- Toggling a source or oscillator model must not reset held-note envelopes or host parameter mappings.
- Live GRANULAR and REIMAGINED applied to oscillator audio in V2 continue to work for legacy sessions. For new hybrid patches, define explicitly whether these process the pre-filter hybrid bus or only the analog branch, and label unsupported combinations honestly.

## 4. Vintage oscillator personalities

Both OSC 1 and OSC 2 can independently select any of the three models. These are original synthesis personalities, not proprietary DSP clones or three whole synthesizers nested per oscillator.

### 4.1 CLASSIC VCO — existing default

Prophet-inspired analog behavior: saw/pulse and existing OSC2 triangle combinations, sync, pulse width modulation, audio-rate Poly Mod and deterministic voice drift. Route mode CLASSIC through V2 rendering unchanged so existing presets recall the same sound. Avoid changing the V2 oscillator's band-limiting, gain structure, free-running phases or default wave values.

### 4.2 DCO / ENSEMBLE

Juno-inspired compound DCO mode:
- tightly controlled oscillator frequency and reduced analog pitch wander;
- saw and pulse, PWM, plus optional suboscillator at −1 octave, with optional −2 octave choice;
- adjustable sub level and oscillator wave level;
- retain MIDI pitch, glide, bend and phase-consistent synthesis;
- use existing MOVEMENT/CHORUS for ensemble stereo treatment; chorus does not live independently inside every DCO voice.

New model-specific settings stay in the patch even when CLASSIC or WIDE is selected.

### 4.3 WIDE VCO

Jupiter-inspired broad VCO mode: saw, triangle, pulse and combinations where present, lively PWM, brighter harmonic options, optional wave color/tilt, compatibility with existing OSC sync/Poly Mod. A Jupiter-like 12/24 dB filter option would be a future filter feature, not a hidden side effect of changing oscillator model.

### 4.4 Mode switching

Use two new host-visible choice parameters analog.osc1.model, analog.osc2.model with stable ordering [Classic, DCO, Wide]. Do not overwrite analog.osc*.wave choice values. Transition models on held notes by a bounded, preallocated 15–40 ms crossfade. Avoid dual-rendering all voices permanently when no mode change is occurring.

All nine pairs (3×3) must be tested for pitch, loudness, aliasing, sync, Poly Mod and no-allocation processing.

## 5. The organic sample source and OSP reuse

### 5.1 Reusable engine boundary

Use reusable classes from OSP such as InstrumentVoice, prepared InstrumentModel/LoadedInstrument, and ContinuationAnalyzer. Reuse OSP's sample loading, pitch, onset, continuation, pitch shifting, sustain, and release behavior; do not instantiate an OSP plugin or copy its entire UI and engine.

Proposed product adapter:

```cpp
class OrganicVoice {
public:
    void prepare(double sampleRate, int maxBlock); // allocations here only
    void start(int midiNote, float velocity, int channel,
               const PreparedOrganicSource* source, uint64_t seed) noexcept;
    void setPitchBend(float semitones) noexcept;
    void release() noexcept;
    void render(float* left, float* right, int samples) noexcept;
    void stop() noexcept;
    bool active() const noexcept;
};
```

The actual method signatures must follow OSP's current InstrumentVoice contract. If an OSP class assumes ownership by its full InstrumentEngine, extract the smallest general-purpose playback adapter within shared pure C++ code and prove OSP tests stay green. Do not add JUCE dependencies to the analog_dsp pure DSP library to achieve sample loading.

### 5.2 Loading, analysis and lifetime

1. User drops WAV/AIFF/FLAC supported by the shared loader.
2. Decode/validate asynchronously; preserve original file untouched.
3. Create/play LoadedInstrument with the existing OSP root/continuation pipeline.
4. Publish immutable ready-to-play sample model to the audio thread through a generation-safe pointer exchange.
5. Start optional spectral MorphAnalyzer on a background worker.
6. Publish immutable MorphProfile when complete, without stopping dry notes.
7. If new sample supersedes old analysis, drop old pending results by generation ID.
8. Keep old model/profile alive until old voices have stopped reading it; reclaim off audio thread.
9. Save sample content hash, managed-store identity, original path fallback, user root override and analysis version in preset state.
10. If missing at reload: retain settings, show MISSING SAMPLE · RELINK, keep analog sources playable, disable hybrid processing.

Manual root is important for a sustained nyckelharpa/cello note whose initial transient or vibrato makes automatic pitch uncertain. Never assert high confidence when the analyzer could not determine it.

### 5.3 Realtime / 4× integration

Current Analog uses 4× oscillator/filter rendering and reports 22 host-rate samples of latency. The organic path must align to the same filter and output timeline. Two candidate approaches:
- Candidate A: render OSP sample at host rate, upsample with a prepared halfband interpolator for entry into the existing 4× per-voice filter. Account for its interpolation delay.
- Candidate B: render the OSP source directly at internal sample rate, if performance tests make it viable.

Choose through impulse, phase, CPU and timbre tests; do not guess delay corrections. Keep reported plugin latency stable during source/morph mode changes. Sample and oscillator endpoints must be aligned before any parallel mixing. A high-latency STFT live mode, if added later, requires separate explicit latency/PDC behavior rather than surprise delay changes during automation.

## 6. Offline acoustic analysis (MorphProfile)

### 6.1 Goal

Instead of running a full FFT spectral morph for every live note, analyze the acoustic one-shot once. Produce a compact, pitch-relative, time-varying profile that can shape 16 independently sounding parent voices with deterministic low latency. The profile is shared, immutable and revisioned. No analysis occurs in processBlock().

### 6.2 Suggested prepared data model

```cpp
struct MorphFrame {
    double timeSeconds = 0.0;
    double f0Hz = 0.0;
    float rms = 0.0f;
    float voicingConfidence = 0.0f;
    std::array<float, 96> harmonicAmplitude{}; // partial n=1..96
    std::array<float, 32> spectralEnvelope{};  // perceptual log bands
    std::array<float, 32> residualSpectrum{};  // aperiodic texture
    float noiseRatio = 0.0f;
    float attackStrength = 0.0f;
};

struct MorphProfile {
    uint32_t schemaVersion = 1;
    std::string sourceHash;
    std::string analyzerVersion;
    double referencePitchHz = 0.0;
    float confidence = 0.0f;
    double attackEndSeconds = 0.0;
    double sustainStartSeconds = 0.0;
    double sustainEndSeconds = 0.0;
    std::vector<MorphFrame> frames; // immutable after construction
};
```

The values above are initial design caps, not a claim that 96 partials are always necessary or that 32 bands are optimal. Profile format can evolve only with versioning and cache invalidation. Cache keys should include (sample content hash, analyzer version, analysis settings).

### 6.3 Analysis pipeline, in exact sequence

1. Decode and validate. Reject non-finite samples, unsupported formats, silence, unreasonable duration/memory. Downmix analysis to mono mid, preserve original stereo dry playback and optionally stereo texture metadata.
2. Root/pitch tracking. Reuse OSP's pitch detection; track time-varying F0 and confidence. Allow user root correction. Do not invent harmonic tracks for unvoiced material.
3. Segment. Identify attack/transient, stable body, continuation/sustain region and release, using the existing continuation model where applicable.
4. Multiresolution STFT, offline. Starting candidates: 4096-point FFT with 75% overlap for sustain at 48 kHz, shorter 1024/2048 analyses for attacks. Tune by actual signal quality, not by assumptions about Zynaptiq's internals.
5. Track partials. Match near-integer multiples of F0 over adjacent frames; estimate harmonic amplitudes and frequency deviation. Treat bowed/vibrato motion as a time trajectory, not as a fresh random phase per frame.
6. Extract spectral envelope. Derive broad body resonances/formants with cepstral smoothing, stable spectral liftering, or another measured approximation. Represent in 24–32 perceptual bands with an explicit Hz coordinate.
7. Extract residual. Estimate aperiodic noise spectra after harmonic energy is separated. Retain bow friction, breath, air/noise and broadband movement in independently controllable form.
8. Extract transient. Keep attack strength and envelope, and optionally a tiny original attack segment for clean sample-derived note onset. Do not duplicate it during playback.
9. Reduce temporal data. Retain informative 8–24 keyframes or a bounded profile; increase where sustained spectral movement genuinely merits it. Include loop-phase mapping to avoid discontinuities.
10. Compute confidence/fallback. A short pluck, noisy sample and pure sine cannot be treated like the same sustained bowed instrument.
11. Cache/publish. Serialize with a version; atomically publish completed immutable profile off audio thread.

### 6.4 Unusable/partial profiles

| Source issue | Required fallback |
|---|---|
| Silent/invalid file | Reject with explicit error, no audio-thread state corruption |
| Short transient/pluck | Dry OSP playback + basic IMPRINT; FUSE sustain unavailable or limited |
| Uncertain pitch | Manual root control; spectral-envelope IMPRINT may work; disable harmonic FUSE until usable |
| Inharmonic metal/noise | Texture/envelope-only processing; honest availability status |
| Missing file on recall | Preserve parameters and name; offer relink, disable morph until loaded |
| Source replaced during processing | Ignore stale generation; never free model while active note references it |

## 7. Morph algorithms — implement in quality order

### 7.1 Explicit endpoint invariant

The public MORPH knob uses one consistent orientation in all modes:
- 0%: the actual original ORGANIC sample path (not a wavetable approximation of it).
- 100%: the actual selected analog carrier rendered with its original current oscillator model/settings (not a spectrum approximation).
- Intermediate positions: genuinely transformed signal. Morph amount can be automated and smoothed without loud pops.

If a method uses a special hybrid resynthesizer internally, smoothly join it to its true endpoint source(s) near the boundaries. Do not advertise dry endpoints unless an audio comparison verifies them. After parameter smoothing settles, the endpoint must be correct.

### 7.2 IMPRINT — first playable morph

Purpose: transfer the organic instrument's moving resonance/formant envelope onto the analog carrier, without abandoning the oscillator's phase coherence or pitch responsiveness.

DSP approach:
1. Take the precomputed acoustic spectral envelope Ea(f,t) at the current voice's mapped time/loop phase.
2. Derive the oscillator/carrier spectral envelope Eb(f,t) for its current wave, PWM and tuning (analytic/pitch-zone banked table preferred).
3. Use a bounded ratio with a floor to avoid infinite gain where the oscillator has spectral nulls: G(f,t)=clamp((Ea+ε)/(Eb+ε), Gmin, Gmax).
4. Morph gains in the log-amplitude domain with smoothing: log G_m=(1-m) log G. A filter-bank/spectral-tilt implementation or band-limited harmonic amplitude correction may realize it; benchmark both.
5. Add a small, separately shaped portion of the original acoustic texture and transient according to TEXTURE and ATTACK.
6. Use correlation-aware output gain compensation; avoid loudness jumps on switching waves and models.
7. Maintain the explicit 0/100 endpoint invariant with a bounded and measured source/hybrid transition when needed.

A preliminary 16–32 envelope-band model is sufficient for first listening tests. IMPRINT is not equivalent to full spectral morphing: it primarily transfers timbral color and movement, and its sound should be judged against a simple wet/dry crossfade in direct A/B.

Acceptance: at 25/50/75% a sustained bowed nyckelharpa/cello should influence the oscillator's resonant shape audibly, beyond mere dry layering. Pitch, attacks, release and latency remain stable.

### 7.3 FUSE — flagship harmonic + residual morph

Purpose: synthesize a phase-coherent hybrid retaining acoustic partial development, harmonic motion, formants, residual bow/breath noise and controlled sample attacks.

DSP process, per parent voice:
1. Set the fundamental from MIDI, bend, glide and current oscillator tuning; derive a stable relative time trajectory from OrganicVoice's attack/body/loop state.
2. Read two adjacent acoustic profile frames, interpolate their partial amplitudes, envelope bands and residual/noise levels smoothly over time.
3. Obtain carrier harmonic amplitudes and waveform/phase state from OSC 1, OSC 2 or their existing BLEND mix.
4. Match harmonics by partial index relative to the fundamental; use a bounded amplitude interpolation that preserves energy. A starting conceptual expression is:
   H_n(t,m)=exp((1-m) ln(A_n(t)+eps) + m ln(B_n(t)+eps)) - eps
   Add floors and explicit treatment of missing partials. Where a partial is absent in one source, use a controlled fade rather than allowing the log floor to erase it abruptly.
5. Do not interpolate wrapped STFT phase angles. Instead, preserve a continuous phase generator for synthesized partials, phase-locked to the oscillator and current note. Do not reset phase at every keyframe.
6. Resynthesize efficiently: first evaluate a bank of band-limited harmonic snapshot wavetables across pitch zones/time segments; benchmark against a sparse additive approach with ~24–64 strongest partials. Decide by listening quality and 16-voice CPU, not implementation fashion.
7. Render residual with a deterministic osp::Prng per note/voice, shaped by the residual spectrum and scaled by TEXTURE. This is not the same as Analog's existing white-noise oscillator level.
8. Retain the acoustic attack/transient according to ATTACK, avoiding double transients and respecting mono-legato behavior.
9. Preserve broad acoustic formants independently of keyboard pitch when desired; provide controlled formant shift/pitch-follow. Do not move the fundamental frequency when moving the formant envelope.
10. Normalize overall level with a bounded limiter/guard. Apply existing per-voice analog filter/VCA and shared FX without reprocessing the direct sample twice.

Quality target: 50% should sound like a new instrument—somewhere between cello/nyckelharpa and synth—not like two sources playing simultaneously. No pronounced phase collapse, zipper noise, transient doubling, or sustain-loop tick. Aim for approximately ±1.5 dB level consistency across morph position on representative tonal test fixtures with level matching enabled, subject to honest musical exceptions.

### 7.4 WEAVE — experimental method, later

Assign coherent bands or harmonic groups to organic versus analog spectral structures using smoothly changing masks. Avoid random per-bin assignment that causes rough combing and frame-to-frame instability. Use seeded deterministic choices, smooth transitions, and a complexity/detail control. Do not enable the WEAVE option in release builds until it makes meaningful sound and passes the same performance gates. It is not required to deliver IMPRINT/FUSE.

### 7.5 Independent parameters

- MORPH POSITION: 0–100, sample→analog hybrid position, suggested 10–30 ms smoothing.
- CARRIER: OSC 1 / OSC 2 / BOTH, preallocated switch crossfade to avoid clicks.
- METHOD: IMPRINT / FUSE / WEAVE, disabled if method isn't implemented; click gives a clear explanation.
- TEXTURE: 0–100, retains aperiodic acoustic residual bow/air noise independently from the analog oscillator noise.
- ATTACK: 0–100, original acoustic onset prominence, never a repeated layer that doubles attacks.
- FORMANTS: normalized bipolar control; defines how strongly resonant envelope is held vs follows pitch/shifted; must not change note f0.
- DETAIL: Low / Normal / High, governs harmonics/keyframes/residual quality budget and CPU; never silently changes global latency.
- LEVEL MATCH: on by default, bounded loudness normalization with safe headroom.
- PARALLEL WET: optional dry/hybrid balance, independent of MORPH POSITION (advanced control).

### 7.6 Audio thread contract

Every audio-thread function is noexcept, lock-free, allocation-free and file-I/O-free. prepare() allocates scratch, sets FFT/free spectral lookup tables if needed, and caches atomic parameter pointers. The analyzer performs FFT and builds MorphProfile only on worker threads. Do not call JUCE AudioProcessorValueTreeState::copyState() or replaceState() inside audio rendering: the official API notes that these use locks.

The current Synth renders in fixed internal chunks and already provides deterministic note timing. Integrate into that model, preserve seeded osp::Prng, and use a generation-safe immutable publication mechanism for model/profile swaps.

## 8. User interface and dynamic layouts

### 8.1 The ANDOR visual contract

Read external/osp/andor-design-system/HANDOFF.md before editing. Preserve its 1448×1086 reference canvas, ceramic/teak rendered assets, Inter/Outfit fonts, original knob/lens art, spacing, typography, dark waveform wells, and six macro positions. Never create a parallel UI theme or re-render knobs with primitives/AI. Extend shared/andor-ui opt-in, keeping OSP's own appearance pixel-identical.

### 8.2 Four source states

| Enabled sources | Display | Example |
|---|---|---|
| OSC 1 only | One full hero generator card | Classic VCO synth |
| OSC 1 + OSC 2 | Two equal cards | Wide + DCO |
| OSC 1 + ORGANIC | Two equal cards | Classic + cello |
| OSC 1 + OSC 2 + ORGANIC | Three equal compact cards | Dual synth + nyckelharpa |

Also support OSC 2 only, ORGANIC only, OSC 2 + ORGANIC, and no-source fallback. Source visibility must be based on active stable identities, not the highest loaded sample layer index. Arrangement may change dynamically, but the engine's stable source identity and host parameter ID never change. Source enabling/disabling should fade without cutting held notes abruptly.

### 8.3 Card details

OSC card: fixed source label, model selection, waveform picture, pitch/octave, PWM, source level and popover for model-specific settings. The current oscillators can keep their existing meaningful parameters; unused controls are visibly disabled.

ORGANIC card: filename, detected/manual root, waveform, sample/start/loop controls, source level, RELINK, replace/remove, analysis status and a morph-control door. The sample's original OSP REIMAGINED feature must remain available with its old meaning; do not rename that parameter to MORPH.

Morph UI: use an existing MiniPanel popover with MiniKnob and SegmentedControl for Method / Carrier / Texture / Attack / Formants / Detail / Level Match. Ideally a primary MORPH rotary control appears inside the organic card variant, using an already-established visual control and without changing housing or macro geometry. If that conflicts with the strict unmodified card-content contract, provide a prominent popover entry within the existing source-card layout and flag the tradeoff; never break OSP regression just to make space.

### 8.4 Retain the existing six macros

| Position / binding | Keep |
|---|---|
| BLEND — analog.blend | Existing balance of OSC 1 and OSC 2 |
| DRIVE — shared OSP drive | Existing OSP saturation |
| TONE — analog.tone | Existing Analog filter cutoff |
| MOVEMENT — shared OSP motion | Existing drift/tape/chorus/pulse/shaper |
| SPACE — shared OSP space | Existing reverb |
| ECHO — shared OSP echo | Existing delay |

Screen order of SPACE/ECHO follows OSP (ECHO before SPACE visually). Morph is a seventh function, not a seventh macro position. Do not repurpose BLEND or choose a new mapping based only on appearance.

### 8.5 Loading and disabled UI

- Drop a sample → dry OSP source becomes playable first; status reads MORPH ANALYZING until profile ready.
- Unsupported analysis → keep dry sample, disable only unsupported FUSE/WEAVE controls.
- No loaded sample → MORPH disabled, existing synth fully playable.
- No active oscillator → MORPH carrier unavailable; dry sample playable.
- Changing modes/controls during sustained notes must not create huge repaints, flicker, audio glitches or inconsistent state.

## 9. Shared shell extension — explicit design work

Current Engine::generatorName() creates one generator card and refuses files; normal shell sample mode creates recording cards. Extend via a small opt-in hybrid source description, not duplicate shell/editor classes. Proposed conceptual interface (adapt signatures to actual repository):

```cpp
enum class SourceKind { Generator, Sample };
struct SourceSlotView {
    StableSourceId id;             // osc1 / osc2 / organic, not visual index
    SourceKind kind;
    bool enabled;
    juce::String displayName;
};

// Message-thread product/shell adapter:
std::array<SourceSlotView, 3> visibleSourceSlots();
// Existing sourcePicture() can be generalized for OSC1 and OSC2.
// Organic file loading targets a stable source ID / reserved data slot.
```

For audio, the existing shell setSource(int layer, LoadedInstrument*) may route the organic source through reserved slot 2, but never assume visual card #2 means layer index 2. The presentation and audio layer mappings are separate. Ensure ModelExchange keeps references valid during held voices and replacements. Add an opt-in shared source-card variant factory without changing the original OSP card's render.

Visual-test rule: OSP's current 18-state parity tests and three masks must remain unchanged. A hybrid Analog card necessarily changes content; add distinct product golden screenshots for 0/1/2/3 sources, each personality, loading state and morph popover. Preserve strict parity of unaffected frame/header/macro/keybed. Do not widen OSP's old masks just to make an Analog product test pass. If necessary, separate OSP shared-component parity mode from Analog production-variant screenshots.

## 10. V3 parameter registry and migration

### 10.1 Golden rule

Append, never rename or re-range. Preserve all existing OSP 350 parameters, current Analog V1/V2 product parameter order (57 product-specific params at audit), and existing analog.blend, analog.osc1.wave, analog.osc2.wave, analog.tone, filter/env/LFO/Poly Mod and shared OSP effects. The present ProductInfo::addParameters is already suitable for Analog-only parameters. Every choice's numeric ordering is part of session recall. Register all added controls in docs/PARAMETER_REGISTRY.md and product schema golden.

### 10.2 Proposed new IDs (subject to actual no-collision audit)

```
analog.osc1.enabled             bool, true
analog.osc2.enabled             bool, true
analog.osc1.model               choice Classic/DCO/Wide, Classic
analog.osc2.model               choice Classic/DCO/Wide, Classic
analog.osc1.dco.subLevel        0..100%, 0
analog.osc2.dco.subLevel        0..100%, 0
analog.osc1.dco.subOctave       choice -1/-2, -1
analog.osc2.dco.subOctave       choice -1/-2, -1
analog.osc1.wide.colour         -100..+100%, 0
analog.osc2.wide.colour         -100..+100%, 0
analog.organic.enabled          bool, false
analog.organic.level            -48..+6 dB, 0
analog.organic.pan              -100..100%, 0
analog.organic.rootOverride     MIDI -1..127 (-1 auto), -1
analog.organic.start            0..100%, 0
analog.organic.loop             bool, true
analog.organic.follow           bool, true
analog.organic.reverse          bool, false
analog.morph.enabled            bool, false
analog.morph.position           0..100%, 0
analog.morph.method             choice Imprint/Fuse/Weave, Imprint
analog.morph.carrier            choice Osc1/Osc2/Both, Both
analog.morph.texture            0..100%, 50
analog.morph.attack             0..100%, 75
analog.morph.formants           -100..100%, 0
analog.morph.detail             choice Low/Normal/High, Normal
analog.morph.levelMatch         bool, true
analog.morph.wet                0..100%, 100
```

Implementing all IDs is not authorization to present incomplete controls as usable. A method not yet implemented must be unavailable. Avoid duplicating ANDOR sample parameters if they already have exact same semantics and can safely be bound via the new hybrid slot; this requires a per-ID audit first. Use fixed display units, sane taper, and stable defaults.

### 10.3 Upgrade logic

Set ProductInfo::stateVersion=3 and preserve the V1→V2 migration chain:

```cpp
if (fromVersion < 2) migrateFromVersion1(host);
if (fromVersion < 3) migrateFromVersion2(host);
```

V2 state migration must not change the sound: default both oscillator models CLASSIC and enabled, organic disabled, morph disabled; all existing waves/tunes/osc balance/drive/filter FX retained. Preserve old analog.blend automation and factory preset names. The saved sample model should include content hash, managed sample-store reference and path fallback, and morph profile schema/version without requiring huge spectral FFT arrays inside every DAW session. Missing source shows relink state.

## 11. File-by-file implementation map

### 11.1 Analog — add (suggested)

```
dsp/OscillatorModel.h           new oscillator personalities / mode renderer
dsp/HybridRouting.h            stable source identity, carrier/parallel mix laws
dsp/OrganicVoice.h/.cpp        OSP note playback adapter, no plugin embedding
dsp/MorphProfile.h             immutable audio-derived profile, schema/version
dsp/MorphAnalyzer.h/.cpp       offline analysis, never audio thread
dsp/MorphImprint.h/.cpp        low-latency envelope imprint
dsp/MorphFuse.h/.cpp           harmonic/residual/attack fusion
dsp/MorphVoice.h/.cpp          endpoints, smoothing, per-note morph state
dsp/MorphWeave.h/.cpp          optional later mode
product/Source/HybridSourceBridge.*  loader/cache/model publishing
product/Source/HybridCards.*        composed ANDOR shared card components
tests/OrganicVoiceTests.cpp
tests/MorphAnalysisTests.cpp
tests/MorphImprintTests.cpp
tests/MorphFuseTests.cpp
tests/HybridRoutingTests.cpp
tests/HybridRealtimeTests.cpp
docs/MORPH_LISTENING_QA.md
```

The pure analog_dsp library must remain independent of JUCE. Put JUCE-driven loader/UI adapters in product/integration libraries. If InstrumentVoice cannot be used without JUCE-independent OSP model types, extract a suitable minimal shared adapter rather than mixing UI dependencies into DSP.

### 11.2 Analog — modify

| File | Specific responsibility |
|---|---|
| dsp/Patch.h | Append source/morph/model state with safe V2-compatible defaults |
| dsp/Oscillator.h and existing BLEP | Preserve CLASSIC; add compound DCO/Wide wave models |
| dsp/Synth.h/.cpp | Extend voice with organic/morph and stable pre-filter routing; use existing filter/oversampling/FX |
| product/Source/AnalogParameters.* | Append parameters, reader/writer, binding list and tests |
| product/Source/AnalogEngine.* | Forward sample model/profile, prepare memory, source availability and GUI picture/readiness |
| product/Source/AnalogPopovers.* | Model settings and MORPH using shared MiniPanel/SegmentedControl |
| product/Source/Product.cpp | State version 3, V2 migration, factory version once presets deliberately change |
| product/golden/parameter-schema.txt | Append-only host parameter golden |
| product/tests/AnalogProductTests.cpp | V1/V2 migration, source UI and preset recall |
| docs/STATUS.md, SPEC.md, PARAMETER_REGISTRY.md, AUDIBLE_QA.md | Document changes, benchmark and listening evidence |
| CMake targets | Add sources/tests with existing build system only |

### 11.3 Shared OSP — change only if essential

Implement neutral opt-in hybrid source model, card composition and sample loader routing. Keep ANDOR OSP's original behavior and renders. Pin the tested OSP submodule commit in Analog after shared CI passes. Don't copy shared rendered artwork into Analog's public repository without the owner's authorization.

## 12. Sequential implementation plan with gates

Every numbered stage is a separately testable and revertible PR. Do not start the next stage with broken tests.

### Stage 0 — Baseline and code audit (no sound changes)

1. Inspect the actual current Analog branch, commit, git status and pinned OSP submodule SHA.
2. Read docs/STATUS.md, docs/SPEC.md, docs/PARAMETER_REGISTRY.md, product/Product.cpp, AnalogEngine, dsp/Synth, and OSP HANDOFF.
3. Run V2 analog_dsp_tests, analog-render, plugin build, andor_analog_tests, OSP visual tests.
4. Save/reference all existing factory and INIT preset renders, spectrum/peak/latency info, source UI screenshots, parameter golden and V1/V2 state fixtures.
5. Profile on real release hardware; keep VM-only CPU metrics distinguished.
6. Write a proposed migration/compatibility summary and source-graph diagram before changes.

Gate 0: existing builds and tests green; reference artifacts retained; original sound and parameter baselines known.

### Stage 1 — V3 schema and safe defaults

1. Add new V3 struct fields/model enums to Patch.
2. Append analog.osc*.model, source enabling, organic and morph parameters; do not reuse old IDs.
3. Add migrateFromVersion2, set stateVersion 3, update registry/golden intentionally.
4. Keep all new features inactive under V2 preset migration.
5. Verify V1 and V2 saved presets and DAW state preserve audio and automation behavior.

Gate 1: no audible change from old V2 patches; new host parameters append only; no fake active controls.

### Stage 2 — Switchable analog oscillator personalities

1. Split oscillator rendering into a common OscillatorModel interface with CLASSIC using the existing renderer unchanged.
2. Add DCO saw/pulse/sub and WIDE waveform/colour personality.
3. Connect independent model parameters for OSC 1/OSC 2; keep existing osc wave options and Poly Mod routes.
4. Preallocate all state; smooth model switching while notes are held.
5. Implement 3×3 cross-pair tests plus pitching, aliasing, sync/FM, level/headroom, sample rates and no allocation.
6. Add compact model selectors to current Analog popover/card built with OSP components.

Gate 2: all nine combinations playable; CLASSIC/CLASSIC presets still sound the same; no CPU spike or click on model change.

### Stage 3 — Opt-in hybrid cards in shared shell

1. Implement stable osc1, osc2, organic presentation identities distinct from visual index.
2. Build shared card composition adapter capable of generator and recorded source concurrently; retain existing OSP source rendering for other products.
3. Reuse frame::sourceColumn and current hero/dual/triple responsive component density.
4. Add source enable/disable actions without host parameter identity changes.
5. Preserve ANDOR's macro row, housing, keybed, shadows, assets, colors, typography and exact canvas.
6. Add separate Analog golden screenshots for all 0–3 source states; do not relax the existing OSP 18-state reference masks.

Gate 3: every source subset lays out correctly; original OSP visual regression stays pixel-identical; oscillator-only view intact.

### Stage 4 — Dry OrganicVoice using ANDOR OSP

1. Implement a realtime-safe OrganicVoice based on OSP's playable sample model/voice.
2. Add organic-only source loading, sample hash/store, root estimate and override, sustain continuation, note release, pedal and multiple voice behavior.
3. Publish prepared immutable samples safely and retain old generations until active voices finish.
4. Integrate with the parent voice; ensure separate oscillators and organic can all sound simultaneously.
5. Measure/align sample timing through the 4× filter and 22-sample-reported V2 latency path.
6. Add ORGANIC card waveform/name/root/replace/relink and clear loading/error states.
7. Test no sample/invalid sample/replacing sample while MIDI sustains and restarting a host project.

Gate 4: user can load a bowed nyckelharpa/cello one-shot, play a chord, hear it with OSC 1 and OSC 2, save/reopen it, and turn individual sources on/off. No morph feature is required yet.

### Stage 5 — Offline MorphProfile

1. Add deterministic offline pitch/attack/sustain/residual spectral analysis with schema and cache.
2. Use OSP analysis results when suitable; extract compact time-varying partials and envelope bands.
3. Build background analysis publication/cancellation and confident/uncertain availability flags.
4. Unit-test sine, bowed notes, short pluck, voice, noise, stereo and malformed files.
5. Verify analysis never occurs on audio thread and stale generations cannot overwrite the latest source.

Gate 5: dry sample plays during analysis; finished profile is valid, deterministic, cached and thread-safe.

### Stage 6 — IMPRINT (first true morph build)

1. Add MorphVoice router, selected carrier mapping and exact 0/100 endpoint states.
2. Implement spectral-envelope imprint and time-frame smoothing, with bounded resonant gain.
3. Add morph amount, carrier, texture, attack, formants and level match controls in OSP popover primitives.
4. Handle complete bypass, missing sample, one or both analog oscillators and latency alignment.
5. Render controlled A/B comparing actual IMPRINT at 25/50/75 to plain equal-power sample/OSC crossfade.
6. Verify fast MIDI playing and morph automation with 16 parent voices.

Gate 6: the 50% patch sounds like a different timbre, not two layered dry sounds; endpoints correct; no clicks, weird attack doubling or pitch shifts.

### Stage 7 — FUSE (signature sound)

1. Track/interpolate acoustic harmonic trajectories and formants across note life.
2. Implement phase-coherent harmonic synthesis using a benchmark-selected approach (precomputed wavetable/keyzone vs sparse additive).
3. Add deterministic shaped residual and retained acoustic attack in controllable amounts.
4. Ensure sustained/looping sample spectral frames transition without audible seams.
5. Optimize CPU at 8/16 notes; expose quality/detail limits only when necessary and with honest behavior.
6. Add stereo/pitchbend/unison/mono/portamento and rapid morph modulation tests.

Gate 7: recognizable bowed acoustic identity survives mid-morph and across multiple octaves, with stable realtime behavior and convincing sustained notes.

### Stage 8 — Optional WEAVE, expression and content

1. Implement band/group spectral WEAVE if FUSE already passes its listening test.
2. Consider modulation routes for morph amount (LFO/mod wheel/aftertouch) only where actually implemented.
3. Make organic-to-analog presets using sounds legally cleared for distribution, including nyckelharpa/cello examples.
4. Add concise, meaningful status text and a simplified popover hierarchy.
5. Perform focused musician listening and adjust normalization, residual, attack, body and formant defaults.

Gate 8: every enabled control is audible, factory assets are licensed, and UX has no misleading panels or crowded defaults.

### Stage 9 — Final plugin/release qualification

1. Run all original Analog and OSP CTests and visual captures; require OSP reference pixel parity.
2. Run pluginval VST3/AU and auval -v aumu Aanl Ospx on macOS.
3. Test Ableton Live and Logic Pro DAW session save/reopen; test offline bounce versus realtime.
4. Verify mono/stereo, host transport, low/high sample rates, small/large blocks, multiple simultaneous instances, note flood, MIDI reset and sample replacement.
5. Test V1/V2 migration and all 34 previous factory presets with sound references and legal source state.
6. Record worst-case CPU on target Apple Silicon and Intel hardware; verify no allocations/locks in process().
7. Complete signing/notarization and license/artwork checks if distributing publicly.
8. Write release notes listing morph quality, source constraints, unsupported sample types and any remaining caveats.

Gate 9: no P0/P1 failures, no hidden sample loss, tested target latency and CPU, all original products preserved, owner-approved sound/UX.

## 13. Test matrix and audio quality gates

Audio fixtures
- Own/rights-cleared sustained nyckelharpa note with bow/rosin noise and sympathetic resonances.
- Sustained cello note with mild vibrato.
- Violin with bright bow friction and unstable attack.
- Monophonic vocal vowel (formant stress), bowed metal (inharmonic fallback).
- Clean sine 440 Hz, saw, pulse, square, amplitude ramps, noise, silence.
- Very short pluck, 1 s note, 10 s sustain, 44.1/48/96 kHz, mono/stereo, corrupt/NaN data.

Don't commit private sound recordings to a public repository without rights clearance. Synthetic deterministic fixtures may be committed.

Automated requirements

| Category | Acceptance test |
|---|---|
| V2 backward compatibility | V1/V2 parameters and old factory renders unchanged or precisely documented intentional difference |
| Mode combinations | 3×3 OSC combinations finite, tuned and free of unacceptable aliasing |
| Stable identity | Any active source subset uses same engine ID and host automation |
| Dry sample | Original one-shot note/release/continuation works across keyboard |
| Root detection | Report uncertainty; manual override corrects note |
| Morph endpoints | 0% dry sample and 100% selected oscillator, time-aligned and with correct release |
| 50% difference | Hybrid spectral/aural result differs meaningfully from straight audio crossfade |
| Harmonic coherence | Stable partial frequency trajectory under MIDI pitch bends and vibrato |
| Formants | Body resonances controlled independently of note F0 |
| Transient behavior | No duplicated attacks, especially mono legato and note retrigger |
| Residual texture | Deterministic/noise-bounded, disabled exactly when texture 0 |
| No asset / missing asset | Safe bypass, preserve state, relink path |
| Source replacement | No use-after-free, stale analysis never published |
| Automation | Fast sweeps no clicks/NaN; stable reported latency |
| MIDI and polyphony | 16 voices, unison, glide, pedal, all-notes-off, voice steal, note flood |
| CPU and realtime | No heap allocation, I/O or locks on audio thread; bounded memory and CPU |
| DAW recall | Reopen host sessions with exactly correct enabled sources and model/morph state |
| Visual | OSP original 18-state regression remains unchanged; Analog hybrid has own goldens |
| Rates/blocks | 44.1/48/88.2/96/192 kHz, 16/32/64/128/512/1024 frames; zero/irregular blocks safely handled |

CPU benchmark targets (not promises)

The Analog V2 status reported approximately 21% of one Linux VM core for plain 16-voice sound at 48 kHz/4×, and heavier granular/reimagined patches up to ~50%. These are not portable to macOS; always compare on the same machine and benchmark script.

Suggested engineering targets after establishing measured baseline:
- Dry third source with both oscillators: prefer no more than ~1.5× the comparable V2 cost at 8 voices, where feasible.
- IMPRINT: prefer no more than ~1.5× equivalent dry three-source patch at 8 voices.
- FUSE: prefer no more than ~2× equivalent dry three-source patch at 8 voices, with dropout-free playback at 16 voices on target hardware.
- Record peak per-block processing time, not just average CPU. No process-time allocation. When FUSE exceeds budget, first precompute harmonic banks, prune inaudible high partials, reduce frame update rate with smoothing, or offer explicit ECO/HIGH quality choices. Never silently change PDC during a morph sweep.

Listening pass

Create fixed samples for each method at 0%, 25%, 50%, 75%, 100%. Compare against (a) original sample, (b) dry oscillator and (c) simple equal-power crossfade. Listen for bow texture, formants, harmonic evolution, note sustain, loop seams, transient sharpness, stereo phase, loudness dips, robotic/metallic artifacts, natural pitches over C2–C6 and expressiveness under velocity and modulation.

## 14. Build and validation commands

Run in the existing Analog repository, not a new generator/bootstrap project:

```sh
git clone --recurse-submodules https://github.com/arenttodal/analog.git
cd analog
git status --short
git submodule status
cmake -B build -G Ninja
cmake --build build --target analog_dsp_tests analog-render
./build/analog_dsp_tests
./build/analog-render --all renders/baseline-v2
cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON
cmake --build build-plugin --target ANDOR_Analog_All andor_analog_tests
ctest --test-dir build-plugin -R 'analog|osp_visual_reference' --output-on-failure
```

If there are newer tests/targets in the current source, inspect CMakeLists.txt first. Existing OSP and Analog CI must run after each shared modification. Use https://github.com/Tracktion/pluginval and macOS auval -v aumu Aanl Ospx for final host validation. Review JUCE commercial licensing and the OSP asset manifest before a commercial release.

## 15. Implementation agent operating instructions

You are working on ANDOR Analog V3 Organic Morph. First read this specification, README.md, docs/STATUS.md, docs/SPEC.md, docs/PARAMETER_REGISTRY.md, the exact product source and the pinned external/osp/andor-design-system/HANDOFF.md. Preserve all existing working audio and UI behavior. Create a short implementation ledger with the nine stages above; keep it updated in docs/STATUS.md. Build and snapshot baseline before editing. Execute stages in order with atomic commits and documented tests; each PR should be revertible. Use original DSP algorithms, no proprietary source copying. Optimize for a musician playing expressive sustained samples on a keyboard, not for a generic real-time sound morph effect. Keep new analysis off the realtime audio thread, maintain deterministic PRNG and immutable profiles, and use OSP components rather than forking visual code. Do not declare controls functional before DSP is connected. Do not widen OSP pixel masks or update goldens to conceal failures. After each stage present test evidence, before/after audio examples, CPU and latency numbers, and any design decisions requiring owner review.

Suggested PR sequence: PR-00 V2 baseline; PR-01 V3 schema; PR-02 independent OSC models; PR-03 OSP-neutral hybrid cards; PR-04 organic dry playback; PR-05 offline morph profile; PR-06 IMPRINT; PR-07 FUSE; PR-08 optional WEAVE/presets; PR-09 release validation.

## 16. Final definition of done

The expansion is complete when two independently switchable analog oscillator modes, one acoustic one-shot and dynamic 1–3 source cards all work together; 0/100 MORPH positions are the correct original sample/oscillator signal; IMPRINT and FUSE create an audible third timbre rather than ordinary crossfade; nyckelharpa/cello sustains remain recognizable across the keyboard; sample persistence/relink is reliable; automation and 16-voice playback are glitch-free; old V1/V2 sessions and factory sounds preserve their identity; the OSP design system and regression remain intact; and standalone/VST3/AU release gates pass.

Core product principle: An acoustic recording should become an editable harmonic identity inside an analog synthesizer—not just another playback layer.

## References

Repository sources:
- https://github.com/arenttodal/analog
- https://github.com/arenttodal/OSP/blob/claude/charming-lovelace-n25fte/andor-design-system/HANDOFF.md

Public external context (not source-code implementation):
- https://www.zynaptiq.com/morph/
- https://www.zynaptiq.com/morph/what-s-new-in-morph-version-3/
- https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html
- https://github.com/Tracktion/pluginval
