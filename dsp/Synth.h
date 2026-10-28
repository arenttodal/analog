#pragma once

#include "Envelope.h"
#include "HalfBand.h"
#include "LadderFilter.h"
#include "Lfo.h"
#include "Patch.h"
#include "PolyBlep.h"

#include "core/Prng.h"
#include "engine/HostTiming.h"
#include "engine/PostProcessor.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace andor::analog
{

/**
    ANDOR ANALOG's sound engine: a 16-voice, two-oscillator subtractive synthesizer after the
    classic five-voice polyphonic instruments of 1978-80, in pure C++ (no JUCE).

    Per voice: OSC 2 (saw, triangle, pulse; keyboard tracking or not; low-frequency range),
    OSC 1 (saw, pulse; hard-synced to OSC 2 on request), white noise, a mixer, a four-pole
    resonant ladder low-pass with saturation, a filter envelope and an amplitude envelope.
    Poly Mod sends the filter envelope and OSC 2 to OSC 1's pitch and pulse width and to the
    cutoff, at audio rate. One LFO for the instrument; every voice slot has its own fixed
    component tolerances and a slow wander (ANALOG), all drawn from the variation seed.

    Voices run at an oversampled rate (4x by default) and are decimated by two half-band
    stages after they are summed; OSP's SPACE and ECHO (osp::PostProcessor) follow at the
    host rate, then a soft output guard.

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

    struct Voice
    {
        int slot = 0;
        bool active = false;      ///< sounding (the amplitude envelope runs)
        bool held = false;        ///< its key is down
        bool sustained = false;   ///< released while the pedal holds it
        int channel = 0, note = -1;
        std::uint64_t order = 0;  ///< note-on sequence (oldest first)
        float velocity = 1.0f;
        double pitch = 60.0, pitchTarget = 60.0;   ///< MIDI notes (glide moves pitch)
        bool hasPlayed = false;
        std::uint64_t stoppedAt = 0;               ///< sample clock when it fell silent
        double unisonOffset = 0.0;                 ///< semitones
        float panL = 1.0f, panR = 1.0f;

        double phase1 = 0.0, phase2 = 0.0;
        BlepLine line1, line2;
        float osc2Out = 0.0f;
        LadderFilter filter;
        Envelope amp, env;
        osp::Prng noise { 1 };

        // Control rate: what each sample starts from.
        double dt1 = 0.0, dt2 = 0.0;
        float width1 = 0.5f, width2 = 0.5f;
        float cutoffNote = 100.0f, envDepth = 0.0f;
        float k = 0.0f;
        float gain1 = 1.0f, gain2 = 1.0f, gainNoise = 0.0f, outGain = 0.0f, outGainTarget = 0.0f;
        float makeUp = 1.0f;   ///< resonance and DRIVE make-up gain after the filter

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
    float renderVoiceSample (Voice& v) noexcept;
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

    // LFO and transport.
    Lfo lfo;
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

    osp::PostProcessor effects;
    float dcL = 0.0f, dcR = 0.0f, dcStateL = 0.0f, dcStateR = 0.0f, dcCoef = 0.0f;

    std::atomic<int> sounding { 0 };
};

} // namespace andor::analog
