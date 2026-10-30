#pragma once

#include "engine/GranularSource.h"
#include "engine/Modulation.h"
#include "engine/ReimaginedModes.h"
#include "engine/Shaping.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace andor::analog
{

enum class Osc1Wave { saw, pulse, sawPulse };
/** OSC 2's waveforms, any combination of SAW, TRI and PULSE (index order is the parameter's). */
enum class Osc2Wave { saw, triangle, pulse, sawTriangle, sawPulse, trianglePulse, all };
enum class PhaseMode { free, reset };

// State version 4 (one shared setup for the oscillator models, the OTA filter family, the
// chorus, ORGANIC and MORPH). Index orders are the parameters' choice orders: frozen.

/** Each oscillator slot's sound model: CLASSIC (the original oscillator, unchanged), DCO (a
    pitch-stable compound oscillator after the Juno family), WIDE (a VCO after the Jupiter-8,
    with an optional modern SUPER SAW). */
enum class OscModel { classic, dco, wide };
/** What sweeps a DCO's pulse width: the knob alone, LFO 1, or the filter envelope. */
enum class PwmSource { manual, lfo, env };
enum class WideWave { saw, pulse, triangle, square, superSaw };
enum class FilterModel { ladder, ota };
enum class OtaVoicing { juno, jupiter };
enum class JunoVariant { juno6, juno60, juno106 };
enum class NoiseColor { white, juno };
enum class MorphMethod { imprint, fuse, weave };
enum class MorphCarrier { osc1, osc2, both };
enum class MorphOutput { morphOnly, layered };

/** A DCO slot's settings (its pulse width is the slot's WIDTH, its tuning and level the slot's). */
struct DcoSettings
{
    bool saw = true, pulse = false;
    double subLevel = 0.0;        ///< 0..1: the square one octave down (true divide-by-two)
    PwmSource pwmSource = PwmSource::manual;
    double pwmDepth = 0.0;        ///< 0..1: how far LFO 1 or the envelope sweeps the width
};

/** A WIDE slot's settings (pulse width: the slot's WIDTH). */
struct WideSettings
{
    WideWave wave = WideWave::saw;
    double shape = 0.5;           ///< 0..1: the wave's edge (0 soft .. 0.5 calibrated .. 1 raw)
    double spread = 0.0;          ///< 0..1: SUPER SAW's detune (0: one saw, centred)
};

/** The ORGANIC source: a recording played by each voice. The file is the shell's layer B and
    these are its card's settings (ANDOR OSP's layerB.*, same meaning), plus the root override. */
struct OrganicSettings
{
    bool heard = true;            ///< not muted (or silenced by the other card's solo)
    double levelDb = 0.0;         ///< the card's LEVEL (-48 dB and below: silent)
    double pan = 0.0;             ///< -1..1
    double tuneSemitones = 0.0;
    int rootOverride = -1;        ///< MIDI note; < 0: the analysis' root (AUTO)
    double start = 0.0;           ///< 0..1 of the recording
    bool loop = true;             ///< hold a sustaining recording through its loop region
    bool follow = true;           ///< its level follows the AMP envelope (off: it plays out its natural length)
    bool reverse = false;
    /** The A/B mix (ANDOR OSP's blend): 0 the oscillators alone .. 1 ORGANIC alone, both at
        full level at the centre (ANALOG's own BLEND law). Only heard with a recording loaded. */
    double blend = 0.5;
};

/** MORPH: the recording turned into the chosen analog carrier. */
struct MorphSettings
{
    bool enabled = false;
    double position = 0.0;        ///< 0 the recording .. 1 the carrier
    MorphMethod method = MorphMethod::imprint;
    MorphCarrier carrier = MorphCarrier::both;
    double texture = 0.5;         ///< 0..1: the recording's noise / bow residue
    double attack = 0.5;          ///< 0..1: the recording's attack kept
    double formants = 0.0;        ///< semitones -12..12: the acoustic envelope moved
    double detail = 0.5;          ///< 0..1: how many partials / bands take part
    bool levelMatch = true;
    MorphOutput output = MorphOutput::morphOnly;
};
enum class VoiceMode { poly, mono };

/** The first version's own LFO divisions (analog.lfo.division, slowest first), kept to read
    sessions saved by it; today the LFO is ANDOR OSP's LFO 1 (osp::mod::syncName). */
constexpr int lfoDivisionCount = 13;
inline const char* lfoDivisionName (int index) noexcept
{
    constexpr const char* n[lfoDivisionCount] = { "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return n[index < 0 ? 0 : (index >= lfoDivisionCount ? lfoDivisionCount - 1 : index)];
}
/** A first-version division as ANDOR OSP's (1/2T, which it lacks, becomes the nearest: 1/4D). */
inline int syncDivisionOfLegacy (int index) noexcept
{
    constexpr int m[lfoDivisionCount] = { 8, 7, 6, 5, 4, 14, 3, 11, 2, 10, 1, 9, 0 };
    return m[index < 0 ? 0 : (index >= lfoDivisionCount ? lfoDivisionCount - 1 : index)];
}

/**
    BLEND folded into the oscillators' levels (version 3: the oscillators are mixed by their own
    LEVELs and BLEND rests at its centre, where it leaves both as they are). The levels the
    synth's equal-power BLEND law gave them: the same sound with BLEND at 50 %.
*/
inline void foldBlend (double blend, double& level1, double& level2) noexcept
{
    const double theta = std::clamp (blend, 0.0, 1.0) * 1.5707963267948966;
    level1 *= std::min (1.0, 1.4142135623730951 * std::cos (theta));
    level2 *= std::min (1.0, 1.4142135623730951 * std::sin (theta));
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

    // State version 4: each slot's model and its settings, the pair's ring modulation.
    OscModel osc1Model = OscModel::classic, osc2Model = OscModel::classic;
    DcoSettings dco1, dco2;
    WideSettings wide1, wide2;
    double ringMix = 0.0;         ///< 0..1: OSC 1 x OSC 2 mixed in (OSC 2 LEVEL scales it)
    bool osc1Enabled = true, osc2Enabled = true;
    NoiseColor noiseColor = NoiseColor::white;
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
    double filterDrive = 0.0;    ///< FILTER DRIVE 0..1: the ladder's input saturation
    double driveBias = 0.0;      ///< 0..1: asymmetry (even harmonics)
    double driveTrimDb = 0.0;    ///< -12..+6 dB after the filter
    // State version 4: the filter model (LADDER: the original), the OTA family's voicing and
    // slope, the high-pass before it (< 0 off; 0..3 the variant's positions).
    FilterModel filterModel = FilterModel::ladder;
    OtaVoicing otaVoicing = OtaVoicing::juno;
    bool otaTwoPole = false;
    int highPass = -1;
    JunoVariant junoVariant = JunoVariant::juno60;

    // Envelopes (seconds; sustain 0..1).
    double filterAttack = 0.008, filterDecay = 0.5, filterSustain = 0.2, filterRelease = 0.35;
    double ampAttack = 0.002, ampDecay = 0.6, ampSustain = 1.0, ampRelease = 0.7;
    double velocityRangeDb = 30.0;   ///< 0..48: how much softer the softest note is
    bool vcaGate = false;            ///< state version 4: the amplitude is a gate (the envelope only shapes the filter)
    bool envLink = false;            ///< state version 4: the amplitude follows the filter envelope's ADSR

    // LFO (global, as on the original instrument): ANDOR OSP's LFO 1 (shape, rate or sync,
    // phase, polarity, free / retrigger / one shot; CUSTOM is its drawn curve, Synth::setLfoCurve).
    osp::mod::LfoSettings lfo;
    double lfoAmount = 0.0;         ///< 0..1: how much of the depths below is applied (the mod wheel adds the rest)
    double lfoPitch1 = 0.4;         ///< semitones, -12..12
    double lfoPitch2 = 0.4;
    double lfoWidth1 = 0.15;        ///< pulse width, -0.45..0.45
    double lfoWidth2 = 0.15;
    double lfoCutoff = 0.0;         ///< semitones, -48..48
    double lfoDelaySeconds = 0.0;   ///< state version 4: each note's LFO depths wait this long ...
    double lfoRiseSeconds = 0.0;    ///< ... then fade in over this long (the Juno's DELAY)

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

    // The source: the oscillators as they are (ONE SHOT) or grains taken live from each
    // voice's own recent sound (GRANULAR: POS how far back, SIZE, DENS, TUNE, SPREAD).
    bool granular = false;
    osp::GranularParams grains;

    // REIMAGINED: ANDOR OSP's modes on the live sound (KALEIDOSCOPE, TAPE FRAME, TOYBOX,
    // MIRAGE; MOSAIC needs a recording's harmonic analysis and leaves the sound unchanged).
    double reimagined = 0.0;      ///< 0..1
    osp::ReimaginedSettings reimaginedSettings;

    // ANDOR OSP's macros and effects on the mix, with its own settings (`effects`):
    // DRIVE (tube, tape, crunch), MOVEMENT (DRIFT in the voices; TAPE, CHORUS, PULSE, SHAPER
    // on the mix), SPACE and ECHO.
    double drive = 0.0;           ///< 0..1 (0: out of the signal path)
    double motion = 0.12;         ///< MOVEMENT 0..1 (ANDOR OSP's default: a little DRIFT)
    double space = 0.10;          ///< wet 0..1
    double echo = 0.0;            ///< wet 0..1
    osp::Shaping effects;

    // State version 4: the chorus after the voices (0 OFF, 1 I, 2 II, 3 I+II), ORGANIC, MORPH.
    int chorusMode = 0;
    bool chorusNoise = false;
    OrganicSettings organic;
    MorphSettings morph;
};

} // namespace andor::analog
