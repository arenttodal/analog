#pragma once

#include "FxDsp.h"
#include "FxProcessor.h"

#include "core/Prng.h"
#include "model/ModelExchange.h"

#include <atomic>
#include <memory>
#include <string>

namespace andor::analog::fx
{

/**
    The FX library's processors (docs/FX_EFFECTS_LIBRARY.md). Each is one pool slot's whole
    insert: its effect, then its MIX (the dry signal delayed by its latency, so dry and wet line
    up) and OUTPUT, both glided. Meters for the graphs are atomics (read on the message thread).
*/
class InsertBase : public Processor
{
public:
    void prepare (double sampleRate, int maximumBlockSize) override;
    void process (float* left, float* right, int numSamples) noexcept final;
    /** The effect's state cleared; MIX and OUTPUT at their settings (no glide from nothing). */
    void reset() noexcept final;
    int latency() const noexcept override { return latencySamples; }

protected:
    /** The effect alone, in place (wet), over a run of at most maxBlock samples. */
    virtual void processWet (float* left, float* right, int numSamples) noexcept = 0;
    virtual void prepareEffect() = 0;
    virtual void resetEffect() noexcept = 0;
    void setCommon (const Common& c) noexcept;
    void setLatency (int samples);

    double rate = 48000.0;
    int maxBlock = 512, latencySamples = 0;

private:
    std::vector<float> dryL, dryR;
    dsp::DelayLine dryDelayL, dryDelayR;
    dsp::Glide mixGlide, outGlide;
};

//==============================================================================
class Utility final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    std::atomic<float> meterL { 0.0f }, meterR { 0.0f }, correlation { 1.0f };

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    UtilitySettings p;
    dsp::Glide gain, panL, panR, width;
    std::array<dsp::Crossover, 2> bass;
    std::array<dsp::Biquad, 2> hp, lp;
    float lastBassHz = -1.0f, lastHp = -1.0f, lastLp = -1.0f;
    double sumLL = 0, sumRR = 0, sumLR = 0;
};

//==============================================================================
class Equalizer final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    /** The response the coefficients now give, in dB at hz (the graph). Message thread. */
    double responseDb (double hz) const noexcept;
    EqSettings settings() const noexcept { return p; }

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    void design() noexcept;
    EqSettings p, designed;
    bool dirty = true;
    std::array<std::array<dsp::Biquad, 2>, 4> bands;   // [band][channel]
    std::array<bool, 4> bandActive {};
    std::array<std::array<dsp::Biquad, 2>, 2> hpf, lpf;   // [stage][channel]
    int hpStages = 0, lpStages = 0;
    int countdown = 0;
};

//==============================================================================
class FxFilter final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    /** The filter's magnitude in dB at hz (the graph; the formants' peaks included). */
    double responseDb (double hz) const noexcept;
    FilterSettings settings() const noexcept { return p; }
    /** A vowel table's three formants (Hz) at a morph position 0..1, shifted. */
    static std::array<double, 3> formants (int table, double morph, double shiftSemitones) noexcept;

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    void design() noexcept;
    FilterSettings p;
    std::array<std::array<dsp::Svf, 2>, 3> svf;   // [stage or formant][channel]
    std::array<dsp::DelayLine, 2> comb;
    std::array<float, 2> combLow {};
    dsp::Glide hz, drive;
    float combDelay = 48.0f, combGain = 0.0f;
    int countdown = 0;
};

//==============================================================================
class Compressor final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    /** Gain reduction now (dB, >= 0) - CLEAN / FET: one value; MULTIBAND: per band, and the
        upward gain per band (dB, >= 0). */
    std::atomic<float> reductionDb { 0.0f };
    std::array<std::atomic<float>, 3> bandReductionDb {}, bandBoostDb {};
    /** CLEAN's static curve: output level for an input level (dB) - the graph. */
    static float cleanCurveDb (const CompSettings& s, float inputDb) noexcept;
    /** MULTIBAND's per-band static gain (dB) for a detector level (dB). */
    static float bandGainDb (const CompSettings& s, int band, float levelDb) noexcept;
    CompSettings settings() const noexcept { return p; }

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    void processClean (float* left, float* right, int n) noexcept;
    void processFet (float* left, float* right, int n) noexcept;
    void processMultiband (float* left, float* right, int n) noexcept;
    CompSettings p;
    int mode = 0;
    // CLEAN
    float envL = 0, envR = 0, grSmooth = 0;
    std::array<dsp::Biquad, 2> sidechain;
    float lastSidechain = -1.0f;
    // FET
    float fetGain = 1.0f, fetEnv = 0.0f;
    std::array<float, 2> fetGainLR { 1.0f, 1.0f }, fetEnvLR {};
    // MULTIBAND
    std::array<dsp::Crossover, 2> lowSplit, highSplit;
    std::array<dsp::Crossover, 2> lowPhase;   // (the low band takes the high crossover's phase)
    float lastLow = -1.0f, lastHigh = -1.0f;
    std::array<float, 3> bandEnv {}, bandGain {};
    double inEnergy = 0, outEnergy = 0;
    float match = 1.0f;
};

//==============================================================================
class StereoSpread final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    std::atomic<float> correlation { 1.0f }, sideLevel { 0.0f }, midLevel { 0.0f };
    SpreadSettings settings() const noexcept { return p; }

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    SpreadSettings p;
    std::array<dsp::DelayLine, 2> hyperLine, dimLine;
    std::array<double, 8> rampPhase {}, lfoPhase {};
    std::array<double, 4> dimPhase {};
    std::array<dsp::Crossover, 2> lowCross;
    float lastLowHz = -1.0f;
    double sumLL = 0, sumRR = 0, sumLR = 0, sumM = 0, sumS = 0;
    dsp::Glide widthGlide;
};

//==============================================================================
class Phaser final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    PhaserSettings settings() const noexcept { return p; }
    /** The notches' frequencies now (Hz; 0 when fewer), left channel (the graph). */
    std::array<std::atomic<float>, 6> notchHz {};
    static constexpr std::array<int, 5> stageCounts { 2, 4, 6, 8, 12 };

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    PhaserSettings p;
    double phase = 0.0, bpm = 120.0;
    std::array<std::array<float, 12>, 2> state {};
    std::array<float, 2> feedback {};
};

//==============================================================================
class Flanger final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    FlangerSettings settings() const noexcept { return p; }
    std::atomic<float> delayNowMs { 0.0f };

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    FlangerSettings p;
    double phase = 0.0, bpm = 120.0;
    std::array<dsp::DelayLine, 2> line;
    std::array<float, 2> fb {}, dcIn {}, dcOut {};
    bool throughZero = false;
};

//==============================================================================
class FrequencyShifter final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    ShifterSettings settings() const noexcept { return p; }
    /** The BLUR window's length (its latency, both modes, so switching never moves the sound). */
    static constexpr int window = 2048, hop = 512;

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    float hilbert (int channel, float x, float& quadrature) noexcept;
    ShifterSettings p;
    std::array<std::array<std::array<float, 16>, 2>, 2> apState {};   // [channel][branch][4 stages x (x1 x2 y1 y2)]
    std::array<float, 2> delayed {};
    double carrier = 0.0;
    std::array<float, 2> fb {};
    std::array<dsp::DelayLine, 2> align;   // the SHIFT path delayed to the BLUR window's latency
    // BLUR
    dsp::Fft fft;
    std::array<std::vector<float>, 2> inRing, outAccum;
    std::array<std::vector<float>, 2> smoothMag;
    std::vector<std::complex<float>> spectrum;
    std::vector<float> hann;
    int ringPos = 0, hopCount = 0;
    osp::Prng prng { 0x5ebd };
};

//==============================================================================
/** CONVOLUTION's impulse, prepared off the audio thread (partitioned spectra). Immutable. */
struct Impulse
{
    std::uint64_t generation = 0;
    int partitionSize = 256;
    int channels = 2;
    int lengthSamples = 0;
    double sampleRate = 48000.0;
    std::vector<std::vector<std::complex<float>>> partitions[2];   // [channel][partition][bin]
    std::vector<float> envelope;   ///< the impulse's level over time (the graph), 256 points
    std::string name;
    /** Builds it from samples (one or two channels) - message thread. Applies DECAY, SIZE,
        REVERSE and NORMALIZE as the settings say. */
    static std::shared_ptr<const Impulse> make (const std::vector<std::vector<float>>& samples, double sampleRate, const ConvSettings& settings,
                                                double playRate, std::uint64_t generation, std::string name);
    /** The factory impulses (designed for ANDOR: seeded, deterministic). */
    static std::vector<std::vector<float>> factory (int index, double sampleRate);
    static constexpr int factoryCount = 6;
    static const char* factoryName (int index) noexcept;
};

class Convolution final : public InsertBase
{
public:
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    bool hasTail() const noexcept override { return true; }
    /** Message thread: the impulse to play next (crossfaded in over 50 ms). */
    void setImpulse (std::shared_ptr<const Impulse> impulse);
    void collectGarbage();
    std::shared_ptr<const Impulse> impulse() const;
    static constexpr int partition = 256;

private:
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    void convolve (const Impulse& ir, int channel, float* out) noexcept;
    ConvSettings p;
    osp::ModelExchange<Impulse>* exchange = nullptr;
    std::unique_ptr<osp::ModelExchange<Impulse>> exchangeStore;
    const Impulse* current = nullptr;
    const Impulse* previous = nullptr;
    int fade = 0;
    dsp::Fft fft;
    std::array<std::vector<float>, 2> inputBlock, overlap, outputBlock, prevOverlap, prevOutput;
    std::array<std::vector<std::vector<std::complex<float>>>, 2> history;   // [channel][partition][bin]
    std::vector<std::complex<float>> work, acc;
    int historyPos = 0, blockPos = 0;
    dsp::DelayLine preDelayL, preDelayR;
    std::array<dsp::Biquad, 2> lowCut, highCut;
    float lastLow = -1.0f, lastHigh = -1.0f;
    dsp::Glide widthGlide;
};

} // namespace andor::analog::fx
