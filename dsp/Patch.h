#pragma once

#include "engine/Shaping.h"

#include <cstdint>

namespace andor::analog
{

enum class Osc1Wave { saw, pulse, sawPulse };
/** OSC 2's waveforms, any combination of SAW, TRI and PULSE (index order is the parameter's). */
enum class Osc2Wave { saw, triangle, pulse, sawTriangle, sawPulse, trianglePulse, all };
enum class LfoWave { sine, triangle, ramp, square, sampleHold };
enum class PhaseMode { free, reset };
enum class VoiceMode { poly, mono };

/** LFO sync divisions, slowest first (lengths in quarter notes). */
constexpr int lfoDivisionCount = 13;
inline double lfoDivisionQuarters (int index) noexcept
{
    constexpr double q[lfoDivisionCount] = { 32.0, 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return q[index < 0 ? 0 : (index >= lfoDivisionCount ? lfoDivisionCount - 1 : index)];
}
inline const char* lfoDivisionName (int index) noexcept
{
    constexpr const char* n[lfoDivisionCount] = { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return n[index < 0 ? 0 : (index >= lfoDivisionCount ? lfoDivisionCount - 1 : index)];
}

/** The polyphony choices (analog.polyphony). */
constexpr int polyphonyChoiceCount = 5;
inline int polyphonyFromChoice (int index) noexcept
{
    constexpr int v[polyphonyChoiceCount] = { 1, 2, 4, 8, 16 };
    return v[index < 0 ? 0 : (index >= polyphonyChoiceCount ? polyphonyChoiceCount - 1 : index)];
}

/**
    One sound, in musical units: everything the synth reads. Plain data, copied into the
    synth once per block (real-time safe). Defaults are the INIT patch and match the
    plugin parameters' defaults (product/Source/AnalogParameters.cpp).
*/
struct Patch
{
    // Oscillators. Levels 0..1; pulse widths 0.02..0.98; tuning in octaves, semitones, cents.
    Osc1Wave osc1Wave = Osc1Wave::saw;
    int osc1Octave = 0;
    double osc1Coarse = 0.0;
    double osc1Width = 0.5;
    double osc1Level = 1.0;

    Osc2Wave osc2Wave = Osc2Wave::saw;
    int osc2Octave = 0;
    double osc2Coarse = 0.0;
    double osc2FineCents = 7.0;
    double osc2Width = 0.5;
    double osc2Level = 0.8;
    bool osc2KeyTrack = true;
    bool osc2LowFrequency = false;

    double noiseLevel = 0.0;
    bool sync = false;
    /** BLEND: 0 OSC 1 only .. 0.5 both at their levels .. 1 OSC 2 only (equal power). */
    double blend = 0.5;

    // Unison (stacks the voices on one note, mono).
    bool unison = false;
    double unisonDetune = 0.3;   ///< 0..1 (1: +-25 cents at the outer voices)
    double stereoSpread = 0.4;   ///< 0..1: voices across the stereo field

    // Filter. Cutoff from TONE (0..1 -> 20 Hz..18 kHz, exponential).
    double tone = 0.62;
    double resonance = 0.15;     ///< 0..1
    double keyTrack = 0.5;       ///< 0..1 (1: one octave per octave, pivot C4)
    double envDepthSemitones = 24.0;   ///< -60..60
    double velocityToFilter = 0.0;     ///< 0..1: how much velocity scales the envelope amount
    double drive = 0.0;          ///< DRIVE 0..1: pre-filter saturation
    double driveBias = 0.0;      ///< 0..1: asymmetry (even harmonics)
    double driveTrimDb = 0.0;    ///< -12..+6 dB after the filter

    // Envelopes (seconds; sustain 0..1).
    double filterAttack = 0.008, filterDecay = 0.5, filterSustain = 0.2, filterRelease = 0.35;
    double ampAttack = 0.002, ampDecay = 0.6, ampSustain = 1.0, ampRelease = 0.7;
    double velocityRangeDb = 30.0;   ///< 0..48: how much softer the softest note is

    // LFO (global, as on the original instrument).
    LfoWave lfoWave = LfoWave::triangle;
    double lfoRateHz = 4.0;
    bool lfoSync = false;
    int lfoDivision = 6;
    double movement = 0.0;          ///< MOTION 0..1: how much of the depths below is applied (the mod wheel adds the rest)
    double lfoPitch1 = 0.4;         ///< semitones, -12..12
    double lfoPitch2 = 0.4;
    double lfoWidth1 = 0.15;        ///< pulse width, -0.45..0.45
    double lfoWidth2 = 0.15;
    double lfoCutoff = 0.0;         ///< semitones, -48..48

    // Poly Mod: sources (0..1) and destinations.
    double polyModEnv = 0.0;
    double polyModOsc2 = 0.0;
    bool polyModToPitch1 = false;
    bool polyModToWidth1 = false;
    bool polyModToCutoff = false;

    // Analog variation.
    double analogAmount = 0.35;   ///< 0..1
    PhaseMode phaseMode = PhaseMode::free;

    // Performance.
    int polyphony = 8;
    VoiceMode voiceMode = VoiceMode::poly;
    double glideSeconds = 0.0;
    double bendRangeSemitones = 2.0;
    double tuneSemitones = 0.0;   ///< the generator's TUNE
    double fineTuneCents = 0.0;
    double panPercent = 0.0;      ///< -100..100
    double levelDb = 0.0;         ///< the generator's LEVEL (-48 dB and below: silent)
    bool mute = false;
    double outputDb = 0.0;        ///< the master volume
    std::uint32_t seed = 1;

    // Effects (OSP's SPACE and ECHO, with OSP's own settings).
    double space = 0.10;          ///< wet 0..1
    double echo = 0.0;            ///< wet 0..1
    osp::Shaping effects;
};

} // namespace andor::analog
