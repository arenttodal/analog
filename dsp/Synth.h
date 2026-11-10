#pragma once

#include "Envelope.h"
#include "HalfBand.h"
#include "HighPassFilter.h"
#include "JunoChorus.h"
#include "LadderFilter.h"
#include "Oscillator.h"
#include "OrganicVoice.h"
#include "SampleSource.h"
#include "ZonePlayer.h"
#include "VoiceEq.h"
#include "OtaFilter.h"
#include "morph/LiveMorph.h"
#include "LiveGranulator.h"
#include "Patch.h"
#include "PolyBlep.h"

#include "core/Prng.h"
#include "engine/HostTiming.h"
#include "engine/KeyDna.h"
#include "engine/MirageEngine.h"
#include "engine/Modulation.h"
#include "engine/PostProcessor.h"
#include "fx/FxEngine.h"
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
    /** A card's DAMPEN curve over the keys (layer 0 the oscillators, 1 ORGANIC: the shell's
        DAMPEN curves). Real-time safe. */
    void setDampenCurve (int layer, const osp::mod::Curve& curve) noexcept;
    /** Tests only: card A's EQ / DAMPEN on the oscillators' sum, as before state version 8 (to
        prove a migrated pair, the same curve on both cards, sounds as the old one). */
    void setLegacyCardsForTest (bool legacy) noexcept { legacyCards = legacy; }
    /** GRANULAR's KEY DNA (each key's own granular identity); recomputed only when it
        changes. Real-time safe. */
    void setKeyDna (const osp::keydna::Settings& settings) noexcept;
    /** GRANULAR's KEY DNA from outside (the shell's: ANDOR OSP's editor computes the profile
        off the audio thread): its table, valid until the next call (null: none), and AMOUNT
        (0..1, live). Replaces what setKeyDna set. Real-time safe. */
    void setKeyDnaTable (const osp::keydna::Table* table) noexcept { keyDnaInUse = table; }
    void setKeyDnaAmount (double amount) noexcept { keyDna.amount = std::clamp (amount, 0.0, 1.0); }
    /** MOVEMENT SHAPER's playhead (0..1), for its picture. */
    float shaperPhase() const noexcept { return effects.shaperPhase(); }

    /** The FX route (message thread; null or CLASSIC: the original chain, bit for bit). */
    void setFxGraph (std::shared_ptr<const fx::CompiledGraph> graph) { router.setGraph (std::move (graph)); }
    void collectFxGarbage() { router.collectGarbage(); }
    bool fxCustomPlaying() const noexcept { return router.playingCustom(); }
    const fx::FxEngine& fxEngine() const noexcept { return router; }
    /** Message thread: CONVOLUTION's impulses (FxEngine::setImpulse). */
    fx::FxEngine& fxEngine() noexcept { return router; }
    /** A MOVEMENT lane's SHAPER playhead (0 A, 1 B), -1 when idle. */
    float shaperPhase (int lane) const noexcept { return effects.shaperPhase (lane); }

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

    /** The arpeggiator's card lanes (the shell's per-card arpeggiator): while on, a note's MIDI
        channel names the cards it sounds on, as 1 + a mask (bit 0 card A: OSC 1 and the
        noise; bit 1 card B: OSC 2; bit 2 card C: the hybrid), so held notes can sustain on
        some cards while an arpeggio plays others. Off (the default) every note sounds on
        every card, whatever its channel. A card a voice does not sound is not computed where
        it can be skipped (the oscillator pair, or the hybrid). */
    void setCardRouting (bool on) noexcept { cardRouting = on; }
    bool cardRoutingOn() const noexcept { return cardRouting; }
    static constexpr std::uint8_t allCards = 7;
    /** The cards a note on this channel sounds on (allCards unless routing is on). */
    std::uint8_t cardsFor (int channel) const noexcept
    {
        if (! cardRouting)
            return allCards;
        const int mask = (channel - 1) & 7;
        return static_cast<std::uint8_t> (mask == 0 ? allCards : mask);
    }

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
    /** Cinematic V6 gate 2: one message of the musician's own input (before the arpeggiator),
        for SUB. It joins the output after the effects (never their input). */
    void handleSubMidi (const std::uint8_t* data, int size) noexcept { subOsc.handleMidi (data, size); }
    /** Tests: SUB. */
    const SubOsc& sub() const noexcept { return subOsc; }

    /** ORGANIC's recording (ANDOR OSP's prepared model; null: none). Audio thread. The previous
        one is read no more once this returns: the notes playing it fade it out from a short
        copy. New notes play the new one. */
    void setOrganic (const osp::InstrumentModel* model) noexcept;
    /** An imported sample source (the Sample Source importer) in ORGANIC's place: each note
        sounds the zones its key and velocity select (overlapping ones together), through
        ORGANIC's level, the A/B mix and card B. Null: ORGANIC's recording (or nothing). The
        same lifetime contract as setOrganic (the old one is not read after this returns).
        Real-time safe. */
    void setSampleSource (const SampleSource* source) noexcept;
    /** Cinematic V6 gate 3: card C's second layer's own loaded source (a file or an instrument
        patch; null: none), with the same lifetime contract as setSampleSource. */
    void setSampleSource2 (const SampleSource* source) noexcept;
    const SampleSource* sampleSource2Now() const noexcept { return sampleSource2; }
    const SampleSource* sampleSourceInUse() const noexcept { return sampleSource; }
    /** The imported source's zones sounding now, over every voice (tests, the status line). */
    int sampleZonesSounding() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            for (int i = 0; i < v.zoneCount; ++i)
                n += v.zones[static_cast<std::size_t> (i)].playing() ? 1 : 0;
        return n;
    }
    const osp::InstrumentModel* organicModel() const noexcept { return organicSource; }

    /** Renders `numSamples` into left/right (overwritten); any length (in pieces of the prepared maximum). */
    void render (float* left, float* right, int numSamples) noexcept;

    int activeVoices() const noexcept { return sounding.load (std::memory_order_relaxed); }
    /** Output delay of the decimation filters, in host samples. */
    int latencySamples() const noexcept { return latency; }
    int oversamplingFactor() const noexcept { return oversampling; }
    double sampleRate() const noexcept { return hostRate; }
    /** Tests: the amount KALEIDOSCOPE's stage on the mix is set to (the cards', level-weighted). */
    double kaleidoscopeAmount() const noexcept { return kaleidoscopeStage.amount(); }

    /** A voice's gate and pitch, for tests and the display (the slot's own view). */
    struct VoiceView
    {
        bool active = false, held = false;
        int note = -1;
        float amp = 0.0f;
        std::uint8_t cards = 7;   ///< the cards it sounds on (setCardRouting)
        double driftCents = 0.0;  ///< MOVEMENT DRIFT's wander of its pitch now (both lanes)
    };
    VoiceView voiceView (int slot) const noexcept;

    /**
        The voice filter's telemetry (read only - the FILTER rack's live graph; FX Rack V3). One
        tap per voice slot, written by the audio thread once a control period from what the
        voice's filter used (its control-rate base cutoff, the envelope, Poly Mod), a sequence
        count around each write; any thread reads a consistent snapshot. Writing it changes
        nothing the voice plays; no allocation, no lock.
    */
    struct FilterTap
    {
        struct Snapshot
        {
            bool active = false, released = false;
            int note = -1, stage = 0;   ///< MIDI note; Envelope::Stage of the filter's envelope
            std::uint64_t order = 0;    ///< note-on sequence (0: never played)
            float velocity = 0.0f;      ///< 0..1
            float baseNote = 0.0f;      ///< the cutoff before the envelope (TONE, KEYS, ANALOG, LFO, DRIFT, routes), MIDI note
            float envDepth = 0.0f;      ///< semitones at envelope 1 (ENV scaled by velocity)
            float env = 0.0f, envPeak = 0.0f;   ///< the filter envelope now; its highest since the note began
            float modNote = 0.0f;       ///< Poly Mod's semitones now
            float cutoffNote = 0.0f;    ///< what the filter used last: base + env x depth + Poly Mod (before the table's clamp)
        };
        /** False if no consistent snapshot could be read (a write in progress each try). */
        bool read (Snapshot& out) const noexcept;
        void write (const Snapshot& s) noexcept;
        bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }

    private:
        std::atomic<std::uint32_t> sequence { 0 };
        std::atomic<bool> active { false }, released { false };
        std::atomic<int> note { -1 }, stage { 0 };
        std::atomic<std::uint64_t> order { 0 };
        std::atomic<float> velocity { 0.0f }, baseNote { 0.0f }, envDepth { 0.0f }, env { 0.0f }, envPeak { 0.0f }, modNote { 0.0f }, cutoffNote { 0.0f };
    };
    const FilterTap& filterTap (int slot) const noexcept { return filterTaps[static_cast<std::size_t> (std::clamp (slot, 0, maxVoices - 1))]; }
    /** The cutoff's range as the filters reach it (MIDI notes: the coefficient table's ends). */
    float cutoffNoteLow() const noexcept { return tableLowNote; }
    float cutoffNoteHigh() const noexcept { return tableHighNote; }

    /** The variation each voice slot carries at ANALOG 100 % (cents of OSC 1 pitch). */
    double slotPitchOffsetCents (int slot) const noexcept;
    /** The grains playing now in every sounding voice, while that card's GRANULAR is on (the
        card's picture; `card` 0: A, OSC 1; 1: B, OSC 2); returns how many were written (at
        most `max`). Audio thread. */
    int collectGrains (int card, LiveGranulator::GrainDot* out, int max) const noexcept;
    /** ORGANIC's read heads (card B's playheads, as ANDOR OSP's One Shot voices): one per voice
        playing the recording, where it reads (0..1 of the recording shown) and how loud the
        note is (its amplitude envelope, 0..1). Returns how many. Any thread that renders. */
    int collectPlayheads (float* where, float* level, int max) const noexcept;
    /** A voice's grains now (tests; `card` 0: A, 1: B). */
    int grainCount (int slot, int card = 0) const noexcept
    {
        const auto& v = voices[static_cast<std::size_t> (std::clamp (slot, 0, maxVoices - 1))];
        return (card == 1 ? v.granulator2 : v.granulator).grainCount();
    }
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
        std::uint8_t cards = 7;   ///< the cards this note sounds on (setCardRouting)
        std::uint64_t order = 0;  ///< note-on sequence (oldest first)
        float tapEnv = 0.0f, tapPeak = 0.0f, tapMod = 0.0f;   ///< (telemetry only: what the filter used last)
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
        // Each card's EQ and DAMPEN on its own part, before the filter (Patch::eqA ...).
        VoiceEq eqA, eqB;
        VoiceEq eqC;                  ///< state version 8: card B's (OSC 2), the shell's layer C
        bool splitCards = false;      ///< card A's or B's EQ / DAMPEN on: each oscillator through its own
        VoiceDampen dampenA, dampenB, dampenC;
        bool dampenFresh = true;      ///< a new note: its damping lands at once
        int highPassTail = 0;         ///< samples the high-pass keeps running after it was switched off
        double ageSeconds = 0.0;      ///< since the note (re)started: LFO DELAY / RISE
        // ORGANIC: the recording this note plays beside the oscillators (into the same mixer).
        OrganicVoice organic;
        std::array<ZonePlayer, SampleSource::maxLayers> zones;   ///< an imported source's zones for this note
        std::array<double, SampleSource::maxLayers> zoneSteps {};
        std::array<float, SampleSource::maxLayers> zoneGains {};
        int zoneCount = 0;
        double organicStep = 0.0;     ///< source frames per internal sample (control rate)
        float organicGain = 0.0f;
        bool organicOn = false;       ///< playing, or fading a replaced recording out
        // With a recording loaded, layer A's LEVEL / MUTE and the A/B mix scale the oscillators
        // and the mix ORGANIC alone (glided at control rate; set at once on a note's first).
        float oscLayerGain = 1.0f, organicLayerGain = 1.0f;
        bool layerGainsFresh = true;
        bool releaseWhenPlayedOut = false;   ///< FOLLOW off: the key was released; the recording plays out first
        // MORPH: the recording turned into the carrier (when its profile is ready).
        morph::LiveMorph morph;       ///< MORPH: the oscillators and card C heard live, made one
        double morphSourceHz = 220.0; ///< the fundamental card C plays for this note
        bool morphOn = false;
        int morphCountdown = 0;       ///< MORPH's control runs every fourth control period
        float lfoFade = 1.0f;
        Envelope amp, env;
        osp::Prng noise { 1 };
        // State version 8: the hybrid as an oscillator (OSC 3: the slots' code, no sync or Poly
        // Mod) or a noise source (its own seeded noise, COLOR, TONE, MOVEMENT).
        HybridType hybridGen = HybridType::sample;   ///< oscillator / noise: this voice generates the hybrid
        double phase3 = 0.0, dt3 = 0.0;
        BlepLine line3;
        osc::SlotExtra extra3;
        osc::SlotPlan plan3;
        float width3 = 0.5f, pwm3 = 0.0f, pwmEnv3 = 0.0f;
        osp::Prng hybridNoise { 1 };
        float hybridColour = 0.0f, hybridTone = 0.0f, hybridToneCoef = 1.0f, hybridMove = 1.0f, hybridMoveTarget = 1.0f;
        int hybridMoveCountdown = 0;
        // Cinematic V6 gate 3: card C's second layer (C2), its own generator and zones, summed
        // into card C's part (Patch::hybrid2).
        struct Layer2
        {
            bool on = false;          ///< sounding (or its zones fading)
            HybridType gen = HybridType::oscillator;   ///< oscillator / noise: generated; else its zones
            double phase = 0.0, dt = 0.0;
            BlepLine line;
            osc::SlotExtra extra;
            osc::SlotPlan plan;
            float width = 0.5f, pwm = 0.0f, pwmEnv = 0.0f;
            osp::Prng noise { 1 };
            float colour = 0.0f, tone = 0.0f, toneCoef = 1.0f, move = 1.0f, moveTarget = 1.0f;
            int moveCountdown = 0;
            std::array<ZonePlayer, SampleSource::maxLayers> zones;
            std::array<double, SampleSource::maxLayers> zoneSteps {};
            std::array<float, SampleSource::maxLayers> zoneGains {};
            int zoneCount = 0;
            float gain = 0.0f;        ///< LEVEL x VELO x card C's M / S and its share of the mix
        } c2;

        // Control rate: what each sample starts from.
        double dt1 = 0.0, dt2 = 0.0;
        float width1 = 0.5f, width2 = 0.5f;
        float cutoffNote = 100.0f, envDepth = 0.0f;
        float k = 0.0f;
        float gain1 = 1.0f, gain2 = 1.0f, gainNoise = 0.0f, outGain = 0.0f, outGainTarget = 0.0f;
        float makeUp = 1.0f;   ///< resonance and DRIVE make-up gain after the filter
        float otaMakeUp = 1.0f;   ///< the same after the OTA (DRIVE and TRIM only)

        // The live sound: its recording, the grains read from it, KALEIDOSCOPE's parts. Card A
        // (OSC 1, the noise) and card B (OSC 2, the ring) each record and granulate their own
        // part (state version 8: GRANULAR per card); card C's part never passes through grains.
        LiveRecording recording;      ///< card A's part
        LiveGranulator granulator;
        LiveRecording recording2;     ///< card B's part
        LiveGranulator granulator2;
        float partB = 0.0f, partC = 0.0f;   ///< this sample's card B and card C parts (at the filter's input level)
        float cloud2 = 0.0f, grainMix2 = 0.0f;   ///< card B's (as cloud and grainMix)
        bool grainsPlay = false;      ///< GRANULAR: the grains are the voice's source
        float cloud = 0.0f;           ///< KALEIDOSCOPE's remapped grains under the oscillators (ONE SHOT)
        double grainStep = 1.0;       ///< card A's grains' read speed (its REIMAGINED's wow)
        double grainStep2 = 1.0;      ///< card B's (state version 14: its own REIMAGINED)
        float grainMix = 0.0f;        ///< ONE SHOT -> GRANULAR, glided (a switch never clicks)
        float saturationDrive = 1.0f, doubling = 0.0f;     ///< card A's KALEIDOSCOPE
        float saturationDrive2 = 1.0f, doubling2 = 0.0f;   ///< card B's (state version 14)
        float lastA = 0.0f, lastB = 0.0f, lastC = 0.0f;    ///< this sample's three parts after the cards' own stages
        double notePitch = 60.0;      ///< the pitch it plays now (MIDI note), for the mode engines
        double doublingPhase = 0.0, doublingDelay = 0.0, doublingTarget = 0.0;   ///< (delays in samples)
        // TAPE FRAME, TOYBOX, MIRAGE: the one each card's note started with (or none). State
        // version 14: card A's and card B's each on its own part (card C is never transformed).
        osp::TapeFrameEngine tapeFrame, tapeFrame2;
        osp::ToyboxEngine toybox, toybox2;
        osp::MirageEngine mirage, mirage2;
        osp::ReimaginedVoiceEngine* modeEngine = nullptr;    ///< card A's
        osp::ReimaginedVoiceEngine* modeEngine2 = nullptr;   ///< card B's
        int modeCountdown = 0;
        double modeFactor = 1.0;      ///< card A's mode's read-speed factor (wow, self-modulation)
        double modeFactor2 = 1.0;     ///< card B's
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
        // MOVEMENT lane B's own DRIFT wander, and the two combined (what the voice plays).
        struct LaneDrift
        {
            std::array<double, 4> from {}, to {}, now {};
            int countdown = 0, segment = 1;
            osp::Prng rng { 1 };
        } driftB;
        std::array<double, 4> driftDepthA {}, driftDepthB {}, driftOut {};

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
    void combineDrift (Voice& v) noexcept;
    void glideDrift (std::array<double, 4>& from, std::array<double, 4>& to, std::array<double, 4>& now, int& countdown, int& segment, osp::Prng& rng,
                     const std::array<double, 4>& depth, double rateHz) noexcept;
    void startModeEngine (Voice& v) noexcept;
    osp::ReimaginedControl modeControl (const Voice& v, int card = 0) const noexcept;
    /** The level-weighted REIMAGINED for what is shared by the voice and the mix (the drift,
        KALEIDOSCOPE's stage): each card's KALEIDOSCOPE amount by how loud the card is heard. */
    double sharedKaleidoscope (const Patch& p, double* focus = nullptr, double* spread = nullptr) const noexcept;
    /** Renders `count` oversampled samples of a voice, adding them (panned) to left/right. */
    void renderVoice (Voice& v, float* left, float* right, int count) noexcept;
    void publishFilterTaps() noexcept;
    /** One sample of a voice's source: oscillators, noise, the recording and the grains;
        `polyMod` is Poly Mod's value at this sample (for the cutoff). */
    float sourceSample (Voice& v, float envValue, float& polyMod) noexcept;
    /** The mixer's output (at the filter's input level) through the voice's recording,
        GRANULAR, KALEIDOSCOPE's doubling and saturation. */
    float liveStages (Voice& v, float x) noexcept;
    /** A card's DAMPEN and EQ on its own part of the voice (A: the oscillators, B: ORGANIC). */
    float cardA (Voice& v, float x) noexcept;
    float cardB (Voice& v, float x) noexcept;
    float cardC (Voice& v, float x) noexcept;
    /** The hybrid's own sound this sample (OSC 3 or NOISE), at full level (its card's LEVEL and
        the A/B mix apply as to a recording). */
    float hybridSample (Voice& v, float envValue) noexcept;
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
    static void planModel (osc::SlotPlan& plan, osc::SlotExtra& x, OscModel model, osc::Waves classicWaves, const DcoSettings& c, const WideSettings& w,
                           double analogAmount, double tolerance, double dt) noexcept;
    void controlLayer2 (Voice& v, const Patch& p, double common, float lfoNow) noexcept;
    float layer2Sample (Voice& v, const Patch& p, float envValue) noexcept;
    /** One sample of OSC 1 and OSC 2 and their mix for a voice playing any model pair. */
    float modelPairSample (Voice& v, const Patch& p, float envValue, float& polyMod) noexcept;

    Patch current;
    double hostRate = 48000.0, internalRate = 192000.0;
    int oversampling = 4, maxBlock = 512;
    int latency = 0;

    std::array<Voice, maxVoices> voices;

    std::array<FilterTap, maxVoices> filterTaps;   ///< (telemetry: FilterTap)
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
    bool cardRouting = false;
    const ControlModulator* modulator = nullptr;

    // LFO (ANDOR OSP's LFO 1) and transport.
    osp::mod::CurveTable lfoCurve {};
    bool legacyCards = false;
    std::array<osp::mod::CurveTable, 3> dampenCurves { osp::mod::compileCurve (osp::dampen::defaultCurve()), osp::mod::compileCurve (osp::dampen::defaultCurve()),
                                                       osp::mod::compileCurve (osp::dampen::defaultCurve()) };
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
    // Cinematic V6 gate 4: the cards' sends (MAIN + SEND only), at the internal rate, then the host's.
    std::vector<float> sendMixL, sendMixR, sendHalfL, sendHalfR, sendOutL, sendOutR;
    HalfBandDecimator sendStage1L, sendStage1R, sendStage2L, sendStage2R;
    bool sendTaps = false;            ///< this chunk: the voices add their sends
    bool sendWas = false;             ///< (the last block's: its decimators cleared when the sends start again)
    std::array<float, 3> sendFrom {}, sendTo {};   ///< the cards' sends across this block (a ramp: never a step)
    int tapBase = 0, tapTotal = 1;    ///< (renderVoice's: where this chunk sits in the block, internal samples)
    // SUB with one path: held back by the route's latency, so it meets the voices the host aligns.
    fx::AlignLine subDelay;
    std::vector<float> subL, subR;
    int subTail = 0;
    float* tapL = nullptr;            ///< (renderVoice's: where this chunk's sends go)
    float* tapR = nullptr;
    HalfBandDecimator stage1L, stage1R, stage2L, stage2R;

    // GRANULAR's KEY DNA, REIMAGINED's live parts.
    osp::keydna::Settings keyDna;
    osp::keydna::Table keyDnaTable;
    const osp::keydna::Table* keyDnaInUse = &keyDnaTable;   ///< setKeyDna's, or the shell's
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
    SubOsc subOsc;   ///< Cinematic V6: after the effects, before the output gain and guard
    std::array<osp::InstrumentModel, 2> resonanceModels;
    int resonanceModel = 0, stageCountdown = 0;
    // One interval of a voice: its source, the mode engine's output, the envelopes and Poly Mod.
    std::array<float, modeInterval> scratchSource {}, scratchL {}, scratchR {}, scratchEnv {}, scratchPolyMod {};
    std::array<float, modeInterval> scratchA {}, scratchB {}, scratchC {};   ///< the cards' parts while a mode engine runs
    std::array<float, modeInterval> scratchAmp {};
    double sharedDriftCents = 0.0;   ///< MOVEMENT DRIFT's shared part (every voice together)

    const osp::InstrumentModel* organicSource = nullptr;
    const SampleSource* sampleSource2 = nullptr;   ///< card C's second layer's (Patch::hybrid2)
    const SampleSource* sampleSource = nullptr;
    /** ORGANIC's part is sounding in a voice (its recording or an imported source's zones). */
    static bool organicPlaying (const Voice& v) noexcept;
    /** FOLLOW off: a layer of card C that is heard is still playing out (each layer by its own gain). */
    static bool heardPlayingOut (const Voice& v) noexcept;
    float lastNoise = 0.0f;   ///< this sample's noise in the voice's mix (MORPH takes the pair without it)
    std::unique_ptr<morph::LiveMorph::Shared> morphShared;   ///< MORPH's transforms and windows (every voice's)
    JunoChorus chorus;            ///< state version 4: the chorus after the voices (off: bit-exact bypass)
    osp::PostProcessor effects;
    fx::FxEngine router;   ///< plays `effects` as they always were (CLASSIC), or the musician's route
    float dcL = 0.0f, dcR = 0.0f, dcStateL = 0.0f, dcStateR = 0.0f, dcCoef = 0.0f;

    std::atomic<int> sounding { 0 };
};

} // namespace andor::analog
