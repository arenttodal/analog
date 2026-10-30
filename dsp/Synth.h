#pragma once

#include "Envelope.h"
#include "HalfBand.h"
#include "HighPassFilter.h"
#include "JunoChorus.h"
#include "LadderFilter.h"
#include "Oscillator.h"
#include "OrganicVoice.h"
#include "OtaFilter.h"
#include "LiveGranulator.h"
#include "Patch.h"
#include "PolyBlep.h"

#include "core/Prng.h"
#include "engine/HostTiming.h"
#include "engine/KeyDna.h"
#include "engine/MirageEngine.h"
#include "engine/Modulation.h"
#include "engine/PostProcessor.h"
#include "engine/ReimaginedStage.h"
#include "engine/TapeFrameEngine.h"
#include "engine/ToyboxEngine.h"
#include "model/InstrumentModel.h"
#include "model/PlaybackSource.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace andor::analog
{

/**
    The instrument's own controls as modulation moves them, one voice at a time (the product
    knows its parameters and how each sets the patch): mod::productDest (k) is the k-th.
*/
class ControlModulator
{
public:
    virtual ~ControlModulator() = default;
    /** Sets in `patch` what product destination `k` controls, its knob moved by `offset`
        (a share of the knob's whole travel, clamped at its ends). Audio thread. */
    virtual void moveControl (Patch& patch, int k, double offset) const noexcept = 0;
};

/**
    ANDOR ANALOG's sound engine: a 16-voice, two-oscillator subtractive synthesizer after the
    classic five-voice polyphonic instruments of 1978-80, in pure C++ (no JUCE).

    Per voice: OSC 2 (saw, triangle, pulse; keyboard tracking or not; low-frequency range),
    OSC 1 (saw, pulse; hard-synced to OSC 2 on request), white noise, a mixer, a four-pole
    resonant ladder low-pass with saturation, a filter envelope and an amplitude envelope.
    Poly Mod sends the filter envelope and OSC 2 to OSC 1's pitch and pulse width and to the
    cutoff, at audio rate. One LFO for the instrument (ANDOR OSP's LFO 1); every voice slot
    has its own fixed component tolerances and a slow wander (ANALOG), all drawn from the
    variation seed.

    Between the mixer and the filter, each voice records its own sound (LiveRecording):
    GRANULAR plays grains taken live from it (ANDOR OSP's granulator and KEY DNA), and
    ANDOR OSP's REIMAGINED modes work on that live sound - KALEIDOSCOPE (saturation, a
    doubling head, remapped grains; its resonators and formants on the mix), TAPE FRAME,
    TOYBOX and MIRAGE (their own engines, unchanged, as on a granular layer). MOVEMENT's
    DRIFT wanders each voice's pitch, tone, level and place as in ANDOR OSP.

    Voices run at an oversampled rate (4x by default) and are decimated by two half-band
    stages after they are summed; KALEIDOSCOPE's stage and ANDOR OSP's DRIVE, MOVEMENT, ECHO
    and SPACE (osp::PostProcessor) follow at the host rate, then a soft output guard.

    Threads: prepare() allocates (message thread). Everything else is real-time safe and
    deterministic: the same seed, patch, events and block boundaries give the same output.
    Events are applied where they fall: the caller renders up to an event's sample, applies
    it, and renders on (see processMidi in the plugin, or render tests).
*/
class Synth
{
public:
    static constexpr int maxVoices = 16;
    /** Control-rate period in host samples (envelope times, LFO, glide, pitch). */
    static constexpr int controlPeriod = 8;

    void prepare (double sampleRate, int maximumBlockSize, int oversampling = 4);
    /** Silences everything and returns to the state after prepare() (same seed: same sound). */
    void reset() noexcept;

    /** The sound (at the start of a block). */
    void setPatch (const Patch& patch) noexcept;
    const Patch& patch() const noexcept { return current; }
    /** The host's transport (start of a block): the LFO's SYNC and ECHO's tempo. */
    void setTransport (const osp::HostTiming& timing) noexcept;
    /** LFO 1's CUSTOM shape (its drawn curve). Real-time safe. */
    void setLfoCurve (const osp::mod::Curve& curve) noexcept;
    /** GRANULAR's KEY DNA (each key's own granular identity); recomputed only when it
        changes. Real-time safe. */
    void setKeyDna (const osp::keydna::Settings& settings) noexcept;
    /** MOVEMENT SHAPER's playhead (0..1), for its picture. */
    float shaperPhase() const noexcept { return effects.shaperPhase(); }

    /** The modulation (ANDOR OSP's: LFO 1 and 2, ENV 1 and 2, VEL, NOTE, the wheel, CC 1-4
        and pressure through 16 routes) and the controllers CC 1-4 follow. LFO 1 is the
        synth's LFO (the patch's): its classic depths and the routes hear the same one.
        Real-time safe (no allocation). Without it: no routes, LFO 1 from the patch. */
    void setModulation (const osp::mod::Settings& settings, const std::array<int, osp::mod::ccSlots>& controllers) noexcept;
    /** Who moves the product's own controls per voice (null: they are not moved per voice). */
    void setControlModulator (const ControlModulator* m) noexcept { modulator = m; }
    /** The modulation's state (the shared sources, the routes compiled). */
    const osp::mod::Runtime& modulation() const noexcept { return modRuntime; }
    /** A destination's offset as the shared stages hear it now (global sources, the global
        envelopes, the latest key's VEL and NOTE), in its own units; false: no route. */
    bool sharedOffset (osp::mod::Dest d, double& offset) const noexcept;
    /** While an arpeggiator plays the synth: VEL and NOTE on the shared stages follow the
        keys the musician pressed (setPlayedKey), not the notes it is given. */
    void setPlayedKeysOnly (bool on) noexcept { playedKeysOnly = on; }
    void setPlayedKey (int note, int velocity) noexcept { modRuntime.setInputNote (note, velocity); }
    /** The newest sounding voice's modulation (the panel's meters), or null. */
    const osp::mod::VoiceState* newestModulation() const noexcept;

    // Events, applied at the current position (between render calls).
    void noteOn (int channel, int note, float velocity) noexcept;
    void noteOff (int channel, int note) noexcept;
    void setPitchBend (float value) noexcept;      ///< -1..1
    void setModWheel (float value) noexcept;       ///< 0..1
    void setSustainPedal (bool down) noexcept;
    void allNotesOff() noexcept;                    ///< CC 123: releases every note
    void allSoundOff() noexcept;                    ///< CC 120: silence now
    void resetControllers() noexcept;               ///< CC 121
    /** A raw MIDI message (1-3 bytes); anything the synth does not play is ignored. */
    void handleMidi (const std::uint8_t* data, int size) noexcept;

    /** ORGANIC's recording (ANDOR OSP's prepared model; null: none). Audio thread. The previous
        one is read no more once this returns: the notes playing it fade it out from a short
        copy. New notes play the new one. */
    void setOrganic (const osp::InstrumentModel* model) noexcept;
    const osp::InstrumentModel* organicModel() const noexcept { return organicSource; }

    /** Renders `numSamples` into left/right (overwritten); any length (in pieces of the prepared maximum). */
    void render (float* left, float* right, int numSamples) noexcept;

    int activeVoices() const noexcept { return sounding.load (std::memory_order_relaxed); }
    /** Output delay of the decimation filters, in host samples. */
    int latencySamples() const noexcept { return latency; }
    int oversamplingFactor() const noexcept { return oversampling; }
    double sampleRate() const noexcept { return hostRate; }

    /** A voice's gate and pitch, for tests and the display (the slot's own view). */
    struct VoiceView
    {
        bool active = false, held = false;
        int note = -1;
        float amp = 0.0f;
    };
    VoiceView voiceView (int slot) const noexcept;

    /** The variation each voice slot carries at ANALOG 100 % (cents of OSC 1 pitch). */
    double slotPitchOffsetCents (int slot) const noexcept;
    /** The grains playing now in every sounding voice, while GRANULAR is on (the card's
        picture); returns how many were written (at most `max`). Audio thread. */
    int collectGrains (LiveGranulator::GrainDot* out, int max) const noexcept;
    /** A voice's grains now (tests). */
    int grainCount (int slot) const noexcept { return voices[static_cast<std::size_t> (std::clamp (slot, 0, maxVoices - 1))].granulator.grainCount(); }
    /** How long each voice's recording is (seconds), and the longest a mode engine runs per call. */
    static constexpr double recordingSeconds = 1.5;
    static constexpr int modeInterval = 32;

private:
    struct Drift
    {
        // Fixed per slot (component tolerances), -1..1.
        float pitch1 = 0, pitch2 = 0, width1 = 0, width2 = 0, cutoff = 0, resonance = 0;
        float attack = 0, decay = 0, release = 0, level1 = 0, level2 = 0, noise = 0;
        // The slow wander (-1..1), a smoothed seeded random walk.
        float wander = 0, wanderFrom = 0, wanderTo = 0;
        int wanderStep = 0, wanderLength = 1;
        std::uint64_t wanderCount = 0;   ///< targets drawn so far (seeds the next)
    };

    static constexpr std::uint64_t stillRunning = ~std::uint64_t { 0 };

    struct Voice
    {
        int slot = 0;
        bool active = false;      ///< sounding (the amplitude envelope runs)
        bool held = false;        ///< its key is down
        bool sustained = false;   ///< released while the pedal holds it
        int channel = 0, note = -1;
        std::uint64_t order = 0;  ///< note-on sequence (oldest first)
        std::uint64_t seed = 1;   ///< the note's own seed (grains, REIMAGINED, DRIFT)
        float velocity = 1.0f;
        double pitch = 60.0, pitchTarget = 60.0;   ///< MIDI notes (glide moves pitch)
        bool hasPlayed = false;
        std::uint64_t stoppedAt = 0;               ///< sample clock when it fell silent (stillRunning: taken, never silent)
        double unisonOffset = 0.0;                 ///< semitones
        float pan = 0.0f;                          ///< its place, -1..1 (before DRIFT)
        float panL = 1.0f, panR = 1.0f;

        double phase1 = 0.0, phase2 = 0.0;
        BlepLine line1, line2;
        float osc2Out = 0.0f;
        LadderFilter filter;
        // State version 4: each slot's model state and plan (control rate), the pair's ring
        // mix, NOISE's colour, the OTA low-pass and the high-pass before the filter.
        osc::SlotExtra extra1, extra2;
        osc::SlotPlan plan1, plan2;
        bool classicPair = true;      ///< both slots CLASSIC, no ring, white noise: the original path
        float ringNow = 0.0f;         ///< RING MOD, glided (a change never clicks)
        float pwm1 = 0.0f, pwm2 = 0.0f;           ///< DCO PWM from LFO 1 (width offset, control rate)
        float pwmEnv1 = 0.0f, pwmEnv2 = 0.0f;     ///< DCO PWM depth from the filter envelope (per sample)
        float noiseColour = 0.0f;     ///< JUNO noise: its low-pass state
        OtaFilter ota;
        float otaMix = 0.0f, otaTarget = 0.0f;    ///< LADDER 0 .. OTA 1 (a model change crossfades)
        float resonance01 = 0.0f;
        float otaCutoffOffset = 0.0f; ///< semitones (the Juno-6's calibration)
        VoiceHighPass highPass;
        int highPassTail = 0;         ///< samples the high-pass keeps running after it was switched off
        double ageSeconds = 0.0;      ///< since the note (re)started: LFO DELAY / RISE
        // ORGANIC: the recording this note plays beside the oscillators (into the same mixer).
        OrganicVoice organic;
        double organicStep = 0.0;     ///< source frames per internal sample (control rate)
        float organicGain = 0.0f;
        bool organicOn = false;       ///< playing, or fading a replaced recording out
        bool releaseWhenPlayedOut = false;   ///< FOLLOW off: the key was released; the recording plays out first
        float lfoFade = 1.0f;
        Envelope amp, env;
        osp::Prng noise { 1 };

        // Control rate: what each sample starts from.
        double dt1 = 0.0, dt2 = 0.0;
        float width1 = 0.5f, width2 = 0.5f;
        float cutoffNote = 100.0f, envDepth = 0.0f;
        float k = 0.0f;
        float gain1 = 1.0f, gain2 = 1.0f, gainNoise = 0.0f, outGain = 0.0f, outGainTarget = 0.0f;
        float makeUp = 1.0f;   ///< resonance and DRIVE make-up gain after the filter
        float otaMakeUp = 1.0f;   ///< the same after the OTA (DRIVE and TRIM only)

        // The live sound: its recording, the grains read from it, KALEIDOSCOPE's parts.
        LiveRecording recording;
        LiveGranulator granulator;
        bool grainsPlay = false;      ///< GRANULAR: the grains are the voice's source
        float cloud = 0.0f;           ///< KALEIDOSCOPE's remapped grains under the oscillators (ONE SHOT)
        double grainStep = 1.0;       ///< the grains' read speed (REIMAGINED's wow)
        float grainMix = 0.0f;        ///< ONE SHOT -> GRANULAR, glided (a switch never clicks)
        float saturationDrive = 1.0f, doubling = 0.0f;
        double notePitch = 60.0;      ///< the pitch it plays now (MIDI note), for the mode engines
        double doublingPhase = 0.0, doublingDelay = 0.0, doublingTarget = 0.0;   ///< (delays in samples)
        // TAPE FRAME, TOYBOX, MIRAGE: the one this note started with (or none).
        osp::TapeFrameEngine tapeFrame;
        osp::ToyboxEngine toybox;
        osp::MirageEngine mirage;
        osp::ReimaginedVoiceEngine* modeEngine = nullptr;
        int modeCountdown = 0;
        double modeFactor = 1.0;      ///< the mode's read-speed factor (wow, self-modulation)
        osp::mod::LfoState lfo;       ///< LFO 1 with POLY voices: this note's own
        // Modulation: this note's sources, and its patch where routes move its controls.
        osp::mod::VoiceState mod;
        Patch patch;                  ///< `current` with this voice's modulated controls (when `modulated`)
        bool modulated = false;
        std::uint64_t patchGeneration = 0;
        float panMod = 0.0f;          ///< PAN's offset (-1..1)
        double ampAttackSeconds = -1.0, ampDecaySeconds = -1.0, ampReleaseSeconds = -1.0;   ///< taken at note start / release (< 0: the knob's)
        float driveGain = 1.0f;       ///< FILTER DRIVE's gain for this voice
        // MOVEMENT DRIFT (and KALEIDOSCOPE's own instability): slow random glides of pitch
        // (cents), tone (octaves), level (dB) and place, as ANDOR OSP's voices wander.
        std::array<double, 4> driftFrom {}, driftTo {}, driftNow {};
        int driftCountdown = 0, driftSegment = 1;
        osp::Prng driftRng { 1 };

        // A voice taken for a new note fades out first (3 ms), then starts the new one.
        int fade = 0;
        bool pending = false;
        int pendingChannel = 0, pendingNote = 0;
        float pendingVelocity = 0.0f;
        bool pendingRetrigger = true;
    };

    void startVoice (Voice& v, int channel, int note, float velocity, bool retrigger) noexcept;
    void releaseVoice (Voice& v) noexcept;
    Voice* allocate() noexcept;
    void controlUpdate() noexcept;
    void updateVoiceControl (Voice& v, float lfoValue, float depth) noexcept;
    /** This voice's patch for the control period: `current` moved by its routes. */
    void applyVoiceModulation (Voice& v) noexcept;
    const Patch& patchOf (const Voice& v) const noexcept { return v.modulated ? v.patch : current; }
    double offsetOf (const Voice& v, osp::mod::Dest d) const noexcept { return v.mod.offset (modRuntime, d); }
    void rebuildModulation() noexcept;
    void updateDrift (Voice& v) noexcept;
    void startModeEngine (Voice& v) noexcept;
    osp::ReimaginedControl modeControl (const Voice& v) const noexcept;
    /** Renders `count` oversampled samples of a voice, adding them (panned) to left/right. */
    void renderVoice (Voice& v, float* left, float* right, int count) noexcept;
    /** One sample of a voice's source: oscillators, noise, the recording and the grains;
        `polyMod` is Poly Mod's value at this sample (for the cutoff). */
    float sourceSample (Voice& v, float envValue, float& polyMod) noexcept;
    /** The mixer's output (at the filter's input level) through the voice's recording,
        GRANULAR, KALEIDOSCOPE's doubling and saturation. */
    float liveStages (Voice& v, float x) noexcept;
    bool anyKeyHeld (const Voice* except) const noexcept;
    void renderChunk (float* left, float* right, int numSamples) noexcept;
    void monoNoteOn (int channel, int note, float velocity) noexcept;
    void monoNoteOff (int channel, int note) noexcept;
    void monoGoTo (int note, bool retrigger, float velocity, int channel) noexcept;
    int groupSize() const noexcept;
    bool monophonic() const noexcept { return current.voiceMode == VoiceMode::mono || current.unison; }
    void rebuildDrift() noexcept;
    void updateWander (Drift& d) noexcept;
    float cutoffG (float note) const noexcept;
    void applyPolyphonyLimit() noexcept;
    /** A slot's plan for the control period (its model's sums and extras). */
    void planSlot (Voice& v, int slot, const Patch& p, double dt) noexcept;
    /** One sample of OSC 1 and OSC 2 and their mix for a voice playing any model pair. */
    float modelPairSample (Voice& v, const Patch& p, float envValue, float& polyMod) noexcept;

    Patch current;
    double hostRate = 48000.0, internalRate = 192000.0;
    int oversampling = 4, maxBlock = 512;
    int latency = 0;

    std::array<Voice, maxVoices> voices;
    std::array<Drift, maxVoices> drift;
    std::uint64_t noteCounter = 0;
    std::uint64_t clock = 0;   ///< host samples rendered since reset
    int nextSlot = 0;
    std::uint32_t driftSeed = 0;

    // Mono / unison: the held keys, last on top.
    struct Key
    {
        int channel = 0, note = 0;
        float velocity = 0.0f;
    };
    std::array<Key, 32> keys {};
    int keyCount = 0;
    bool monoGate = false;
    double lastMonoPitch = -1.0;

    // Performance.
    float bend = 0.0f, wheel = 0.0f;
    bool pedal = false;

    // Modulation: ANDOR OSP's runtime (its global LFO 1 is the synth's LFO), the settings
    // it runs, the controllers CC 1-4 follow, and the product destinations moved per voice.
    osp::mod::Settings modSettings;
    osp::mod::Runtime modRuntime;
    std::array<int, osp::mod::ccSlots> modControllers { 2, 11, 16, 17 };
    std::array<int, osp::mod::maxProductDests> voiceControls {};   ///< product destinations with routes, per voice
    int voiceControlCount = 0;
    bool voiceRoutes = false;   ///< a route reaches something a voice reads
    std::uint64_t patchGeneration = 1;   ///< a new one: the voices' patches are copied from `current` again
    bool playedKeysOnly = false;
    const ControlModulator* modulator = nullptr;

    // LFO (ANDOR OSP's LFO 1) and transport.
    osp::mod::CurveTable lfoCurve {};
    float lfoValue = 0.0f;
    osp::HostTiming timing;
    double ppqAtBlock = 0.0;
    int blockOffset = 0;   ///< host samples rendered since the transport's block start

    // Smoothed controls.
    float toneNote = 100.0f;      ///< base cutoff as a MIDI note
    float resonanceSmoothed = 0.0f, driveSmoothed = 0.0f, blendSmoothed = 0.5f;
    float driveGain = 1.0f, driveBias = 0.0f, driveBiasOffset = 0.0f;
    bool smoothingPrimed = false;
    int controlCountdown = 0;

    // The cutoff coefficient g = tan(pi f / fs) by MIDI note, an eighth of a semitone apart.
    static constexpr float tableLowNote = -24.0f;
    static constexpr int tableStepsPerNote = 8;
    std::vector<float> gTable;
    float tableHighNote = 140.0f;

    // Oversampled mix and decimation.
    std::vector<float> mixL, mixR, halfL, halfR;
    HalfBandDecimator stage1L, stage1R, stage2L, stage2R;

    // GRANULAR's KEY DNA, REIMAGINED's live parts.
    osp::keydna::Settings keyDna;
    osp::keydna::Table keyDnaTable;
    /** What the mode engines take for "the recording" on a live voice (they read only its
        length, rate and root there): TOYBOX the voice's own recording's length; TAPE FRAME a
        twelve-second tape (its FRAMEs, 3.6 to 11.4 s, are longer than what a voice keeps: its
        travel is scaled into the recording); MIRAGE the voice rate (its clock). Rooted at C4,
        with the matching model. */
    osp::PlaybackSource tapeSource, toyboxSource, mirageSource;
    osp::InstrumentModel liveModel;
    // KALEIDOSCOPE on the mix: ANDOR OSP's stage, its resonators tuned to the harmonics of
    // the first note of each phrase (two models, so a retune is a new model to it).
    osp::ReimaginedStage kaleidoscopeStage;
    std::array<osp::InstrumentModel, 2> resonanceModels;
    int resonanceModel = 0, stageCountdown = 0;
    // One interval of a voice: its source, the mode engine's output, the envelopes and Poly Mod.
    std::array<float, modeInterval> scratchSource {}, scratchL {}, scratchR {}, scratchEnv {}, scratchPolyMod {};
    std::array<float, modeInterval> scratchAmp {};
    double sharedDriftCents = 0.0;   ///< MOVEMENT DRIFT's shared part (every voice together)

    const osp::InstrumentModel* organicSource = nullptr;
    JunoChorus chorus;            ///< state version 4: the chorus after the voices (off: bit-exact bypass)
    osp::PostProcessor effects;
    float dcL = 0.0f, dcR = 0.0f, dcStateL = 0.0f, dcStateR = 0.0f, dcCoef = 0.0f;

    std::atomic<int> sounding { 0 };
};

} // namespace andor::analog
