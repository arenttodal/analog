#pragma once

#include <array>

namespace andor::analog::fx
{

/**
    The new effects' settings, one block per pool slot (the host parameters `analog.fx.<type>.<n>.*`
    write them; AnalogParameters.cpp). Plain values in physical units. A slot keeps its values
    while it is out of the graph (removing an effect never resets it).

    Defaults are each effect's safe starting point. Choice fields are indices in the order of
    their parameter's choices (append only).
*/

struct Common
{
    bool on = true;            ///< the effect's own bypass (host-automatable)
    float mix = 1.0f;          ///< 0..1, phase-aligned dry / wet
    float outputDb = 0.0f;
};

struct SpreadSettings : Common
{
    enum Mode { hyper, dimension, both };
    int mode = both;
    float width = 1.0f;            ///< 0..2: 0 mono, 1 as it is, 2 twice the side
    int voices = 1;                ///< index: 2 / 4 / 6 / 8 heads
    float detuneCents = 8.0f;      ///< 0..60
    float hyperRateHz = 0.3f;      ///< 0.03..5
    float dimSize = 0.42f;         ///< 0..1 (5 .. 15 ms)
    float dimRateHz = 0.35f;       ///< 0.03..3
    float dimAmount = 0.35f;       ///< 0..1
    bool lowMono = true;
    float lowMonoHz = 130.0f;      ///< 50..400
    SpreadSettings() { mix = 0.5f; }
};

struct CompSettings : Common
{
    enum Mode { clean, fet, multiband };
    int mode = clean;
    // CLEAN
    float thresholdDb = -18.0f, ratio = 3.0f, attackMs = 10.0f, releaseMs = 180.0f, kneeDb = 6.0f, makeupDb = 0.0f;
    bool peak = false;             ///< detector: RMS (off) or PEAK
    float link = 1.0f;             ///< stereo link 0 (independent) .. 1 (one detector)
    float sidechainHz = 20.0f;     ///< the detector's high-pass (20: off)
    bool autoMakeup = false;
    // FET 76
    float fetInputDb = 12.0f, fetOutputDb = -12.0f, fetAttackUs = 200.0f, fetReleaseMs = 300.0f;
    int fetRatio = 0;              ///< index: 4 / 8 / 12 / 20 / ALL
    float fetColor = 0.5f;
    bool fetLink = true, fetColorOnly = false;
    // MULTIBAND
    float depth = 0.5f, time = 0.5f, tone = 0.5f;
    float mbThresholdDb = -24.0f, mbDownRatio = 4.0f, mbUpRatio = 1.5f, mbAttackMs = 10.0f, mbReleaseMs = 120.0f;
    float lowHz = 120.0f, highHz = 2500.0f;
    std::array<float, 3> bandOffsetDb { 0.0f, 0.0f, 0.0f }, bandGainDb { 0.0f, 0.0f, 0.0f };
    float deadbandDb = 3.0f;
    bool loudnessMatch = true;
};

struct EqBand
{
    enum Type { peak, lowShelf, highShelf, highPass, lowPass };
    bool on = true;
    int type = peak;
    float hz = 1000.0f, gainDb = 0.0f, q = 0.8f;
};

struct EqSettings : Common
{
    std::array<EqBand, 4> bands {
        EqBand { true, EqBand::lowShelf, 100.0f, 0.0f, 0.7f }, EqBand { true, EqBand::peak, 350.0f, 0.0f, 0.8f },
        EqBand { true, EqBand::peak, 2500.0f, 0.0f, 0.8f }, EqBand { true, EqBand::highShelf, 8000.0f, 0.0f, 0.7f }
    };
    bool hpOn = false, lpOn = false;
    float hpHz = 30.0f, lpHz = 18000.0f;
    bool hpSteep = false, lpSteep = false;   ///< 24 dB / octave (else 12)
};

struct FilterSettings : Common
{
    enum Type { lp12, lp24, hp12, hp24, bp12, notch, allpass, combPlus, combMinus, formant1, formant2, formant3 };
    int type = lp12;
    float hz = 1000.0f;            ///< 20..20000 (COMB: its pitch, the delay 1 / f)
    float resonance = 0.0f;        ///< 0..0.95
    float driveDb = 0.0f;          ///< 0..24
    float pan = 0.0f;              ///< -1..1
    float combDamping = 0.3f;      ///< 0..1
    float formantShift = 0.0f;     ///< semitones -12..12
};

struct PhaserSettings : Common
{
    enum Mode { classic, deep };
    int mode = classic;
    float rateHz = 0.5f;           ///< 0.03..10
    bool sync = false;
    int division = 4;              ///< index into the shared rate divisions
    float depth = 0.6f, centreHz = 800.0f, feedback = 0.35f;
    int stages = 1;                ///< index: 2 / 4 / 6 / 8 / 12
    float stereoDegrees = 90.0f;
    PhaserSettings() { mix = 0.5f; }
};

struct FlangerSettings : Common
{
    enum Mode { classic, throughZero };
    int mode = classic;
    float rateHz = 0.25f;
    bool sync = false;
    int division = 6;
    float depth = 0.5f, delayMs = 2.0f, feedback = 0.4f, stereoDegrees = 90.0f, width = 1.0f;
    FlangerSettings() { mix = 0.5f; }
};

struct ShifterSettings : Common
{
    enum Mode { shift, blur };
    int mode = shift;
    float shiftHz = 100.0f;        ///< 0..2000
    bool down = false;
    float width = 1.0f;            ///< 0..2: the right channel shifted the other way by width - 1
    float feedback = 0.0f;         ///< 0..0.8
    float blurAmount = 0.5f, blurMs = 120.0f;
    ShifterSettings() { mix = 0.5f; }
};

struct ConvSettings : Common
{
    enum Mode { realistic, creative };
    int mode = realistic;
    int impulse = 0;               ///< factory impulse index (a user file replaces it: the engine's asset)
    float preDelayMs = 0.0f, decaySeconds = 4.0f, size = 1.0f, lowCutHz = 20.0f, highCutHz = 20000.0f, width = 1.0f;
    bool normalize = true, reverse = false;
    ConvSettings() { mix = 0.3f; }
};

struct UtilitySettings : Common
{
    float gainDb = 0.0f, pan = 0.0f, width = 1.0f;
    bool invertLeft = false, invertRight = false;
    bool monoBass = false;
    float monoBassHz = 130.0f;
    bool hpOn = false, lpOn = false;
    float hpHz = 30.0f, lpHz = 18000.0f;
};

/** RETRO (V2; docs/FX_EFFECTS_LIBRARY.md): one aging machine, three personalities. Every
    amount is 0..1 unless stated; neutral defaults are what a newly inserted RETRO starts at. */
struct RetroSettings : Common
{
    enum Mode { tape, vinyl, digital };
    enum Era { eighties, nineties, codec };
    enum Preserve { keepLow, neutral, keepHigh };
    enum NoiseType { autoNoise, hiss, vinylNoise, room, digitalNoise };
    enum CodecMode { standard, drop, repeat, jitter, combined };
    enum CodecStereo { stereo, joint, mono };
    int mode = tape;
    int tapeModel = 0;             ///< 0 cassette, 1 studio reel
    int rpm = 0;                   ///< 0 33 1/3, 1 45, 2 78
    int era = nineties;
    float character = 0.4f, wear = 0.25f;
    float inputDb = 0.0f, toneDb = 0.0f;     ///< tone: a tilt, -12..+12 dB
    float saturation = 0.2f;
    int preserve = neutral;
    float wowDepth = 0.12f, wowHz = 0.55f, flutterDepth = 0.07f, flutterHz = 6.0f, randomness = 0.2f, stereoPhaseDeg = 0.0f;
    int noiseType = autoNoise;
    float noiseDb = -96.0f;        ///< -96: off
    float noiseFollow = 0.0f;
    bool noiseAutoMute = true;
    float crackle = 0.0f, pops = 0.0f;
    float dropoutRate = 0.0f, dropoutDepth = 0.0f, dropoutMs = 25.0f;
    float lowHz = 20.0f, highHz = 18000.0f, headBump = 0.15f;
    float compAmount = 0.0f, compAttack = 1.0f, compRelease = 1.0f, compTilt = 0.0f;   ///< tilt -1..1
    bool compLink = true;
    float bits = 12.0f, rateKHz = 24.0f;
    bool aliasHard = false;
    float digitalMotion = 0.0f;
    float codecLoss = 0.15f, codecSpeed = 0.2f, codecArtifact = 0.0f, codecAmbience = 0.0f;
    int codecMode = standard, codecStereo = stereo;
    bool artifactOnly = false;
    int seed = 0;
    RetroSettings() { mix = 0.65f; }
};

struct Settings
{
    std::array<SpreadSettings, 2> spread;
    std::array<CompSettings, 2> comp;
    std::array<EqSettings, 2> eq;
    std::array<FilterSettings, 2> filter;
    std::array<PhaserSettings, 2> phaser;
    std::array<FlangerSettings, 2> flanger;
    std::array<ShifterSettings, 1> shifter;
    std::array<ConvSettings, 1> conv;
    std::array<UtilitySettings, 2> utility;
    std::array<RetroSettings, 2> retro;
    double bpm = 120.0;   ///< the host's tempo (synced rates)
};

} // namespace andor::analog::fx
