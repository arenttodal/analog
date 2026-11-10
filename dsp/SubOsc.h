#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace andor::analog
{

/**
    Cinematic V6 gate 2: SUB - an independent polyphonic sub-bass instrument. It plays the keys
    the musician holds (the MIDI before the arpeggiator), so it can sustain a chord while the
    cards arpeggiate, and it joins the output after the effects: never ECHO's, SPACE's or any
    other effect's input (docs/CINEMATIC_V6_GATE2.md).

    Its own voices (maxVoices), envelope, key range, high-note response, GRIT and level; a
    band-limited wave (SINE, TRIANGLE, SQUARE, SAW) one or two octaves below the key, centred.
    Real-time safe: prepare() sizes nothing (fixed arrays); every other call is allocation-,
    lock- and I/O-free.
*/
struct SubSettings
{
    enum class Wave { sine, triangle, square, saw };
    bool enabled = false;
    Wave wave = Wave::sine;
    int octave = -2;             ///< -1 or -2
    double grit = 0.0;           ///< 0..1: soft saturation, its tone tamed (0: the plain wave)
    double levelDb = -18.0;
    int lowKey = 48;             ///< the played keys it answers (MIDI notes, inclusive): C3 ..
    int highKey = 84;            ///< .. C6 (sounding C1 .. C4 two octaves down)
    bool taper = true;           ///< TAPER: quieter above taperStart; FLAT: every key the same
    int taperStart = 60;         ///< C4
    double taperDbPerOctave = 6.0;
    bool operator== (const SubSettings&) const = default;
};

class SubOsc
{
public:
    static constexpr int maxVoices = 8;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setSettings (const SubSettings& s) noexcept;
    void setBendRange (double semitones) noexcept { bendRange = semitones; }
    /** The pitch wheel without MIDI (the instrument's on-screen wheel), -1..1 of the bend range. */
    void setBend (float wheel) noexcept { bendSemitones = bendRange * static_cast<double> (std::clamp (wheel, -1.0f, 1.0f)); }
    const SubSettings& settingsNow() const noexcept { return settings; }

    /** One MIDI message of the musician's own input (note on / off, sustain, bend, 120, 121, 123). */
    void handleMidi (const std::uint8_t* data, int size) noexcept;

    /** Adds `n` samples to left / right (mono, centred). Nothing sounding: nothing added. */
    void render (float* left, float* right, int n) noexcept;

    bool sounding() const noexcept;
    /** Tests: how many voices play, and a voice's key (-1: free). */
    int activeVoices() const noexcept;
    int voiceKey (int index) const noexcept { return voices[static_cast<std::size_t> (index)].key; }
    /** The level a held key at `key` would have (linear, before the envelope): range and response. */
    float keyGain (int key) const noexcept;

private:
    struct Voice
    {
        int key = -1;              ///< the played key (-1: free)
        double phase = 0.0;
        float env = 0.0f;          ///< 0..1
        bool releasing = false;
        bool killed = false;       ///< ALL SOUND OFF: this voice's quick fade (a later note is its own)
        float gain = 0.0f, gainTarget = 0.0f;   ///< range x response x velocity, glided
        float velocity = 1.0f;
        std::uint64_t age = 0;
    };

    void startKey (int key, float velocity) noexcept;
    void releaseKey (int key) noexcept;
    void releaseAll() noexcept;
    void reconcile() noexcept;   ///< voices for the held keys in range; the others released
    bool inRange (int key) const noexcept { return key >= settings.lowKey && key <= settings.highKey; }
    float velocityGain (float velocity) const noexcept { return 0.6f + 0.4f * velocity; }
    float sample (Voice& v, double increment) noexcept;

    SubSettings settings;
    double rate = 48000.0, bendRange = 2.0, bendSemitones = 0.0;
    std::array<Voice, maxVoices> voices {};
    std::array<std::uint8_t, 128> held {};       ///< how many times each key is down (repeated notes)
    std::array<bool, 128> sustained {};          ///< released while the pedal was down
    std::array<float, 128> heldVelocity {};
    bool pedal = false;
    std::uint64_t clock = 0;
    float attackStep = 0.0f, releaseCoef = 0.0f, rangeCoef = 0.0f, quickCoef = 0.0f;
    // GRIT's tone and the subsonic guard (two one-pole high-passes at ~25 Hz).
    float toneCoef = 1.0f, toneState = 0.0f;
    float hpCoef = 0.0f, hp1x = 0.0f, hp1y = 0.0f, hp2x = 0.0f, hp2y = 0.0f;
};

} // namespace andor::analog
