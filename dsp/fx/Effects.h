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
/** The last few hundred output samples (L, R), decimated, for a goniometer (lock-free:
    the audio thread writes, the interface reads; a torn pair only moves one dot). */
struct ScopeTap
{
    static constexpr int size = 256;
    std::array<std::atomic<float>, size> l {}, r {};
    std::atomic<int> head { 0 };
    int every = 6, count = 0;
    void push (float a, float b) noexcept
    {
        if (++count < every)
            return;
        count = 0;
        const int h = head.load (std::memory_order_relaxed);
        l[static_cast<std::size_t> (h)].store (a, std::memory_order_relaxed);
        r[static_cast<std::size_t> (h)].store (b, std::memory_order_relaxed);
        head.store ((h + 1) % size, std::memory_order_release);
    }
};

class InsertBase : public Processor
{
public:
    /** Opt-in (SPREAD, UTILITY): the effect's output, for the rack's goniometer. */
    ScopeTap scope;
    bool scopeOn = false;

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
    /** The input's peak level, held and falling 12 dB/s (dB; the graph's operating point). */
    std::atomic<float> inputDb { -120.0f };
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


//==============================================================================
/** RETRO's macro mapping (pure: tested apart). CHARACTER pushes saturation and a darker tilt
    (and, when the user has switched the compressor on, its amount); WEAR deepens wow and
    flutter, narrows the bandwidth and, past half, brings in dropouts. Base values are the
    user's; the macros offset them and never overwrite them. */
struct RetroDerived
{
    float saturation, tiltDb, compAmount, wowDepth, flutterDepth, dropoutRate, dropoutDepth, lowHz, highHz, bits, rateKHz, codecLoss, codecSpeed;
};
RetroDerived deriveRetro (const RetroSettings& s) noexcept;

/**
    RETRO: an aging machine in three personalities (docs/FX_EFFECTS_LIBRARY.md). TAPE: an
    anti-aliased (ADAA) asymmetric saturation with frequency preservation, head bump and
    bandwidth, wow / flutter / drift on a modulated delay, dropouts, hiss. VINYL: the platter's
    wow at its speed (33 1/3, 45, 78), crackle and pops, groove rumble, a softer response.
    DIGITAL: 80s / 90s converters (sample and hold with or without anti-alias filters, bit
    depth with dither or raw) and CODEC (an STFT: masking and quantisation of weak bins, band
    limit, dropped / repeated frames, phase jitter; stereo, joint or mono; the artifact alone
    or emphasised; a small ambience on the artifact). One optional compressor before it all.
    Its latency is constant (one codec frame) in every personality; switching crossfades.
    Every random thing is seeded (osp::Prng): the same settings render the same samples.
*/
class Retro final : public InsertBase
{
public:
    Retro();
    ~Retro() override;
    void resetEffect() noexcept override;
    void set (const Settings& s, int slot) noexcept override;
    bool hasTail() const noexcept override { return true; }
    RetroSettings settings() const noexcept { return p; }

    /** The machine's linear response (head bump, bandwidth, tilt) at a frequency (dB): the
        graph's curve, from the same designs the audio uses. */
    static double responseDb (const RetroSettings& s, double rate, double hz) noexcept;
    /** The codec frame (and the latency) at a rate. */
    static int frameFor (double rate) noexcept;

    // Telemetry (any thread reads; the audio thread writes).
    static constexpr int bands = 48;
    std::array<std::atomic<float>, bands> spectrumDb {};   ///< the output, 30 Hz - 20 kHz (log)
    std::atomic<float> wowCents { 0.0f }, effectiveBits { 16.0f }, effectiveRateKHz { 48.0f }, keptFraction { 1.0f };
    std::array<std::atomic<std::uint32_t>, 16> eventTicks {};   ///< recent events (dropouts, clicks, packets), in ticks
    std::array<std::atomic<int>, 16> eventKinds {};             ///< 0 dropout, 1 crackle, 2 pop, 3 packet
    std::atomic<int> eventHead { 0 };
    std::atomic<std::uint32_t> ticks { 0 };                     ///< 1 tick = 64 samples
    std::atomic<float> ticksPerSecond { 750.0f };               ///< (at the prepared rate)
    static constexpr int tickSamples = 64;

private:
    struct State;
    void prepareEffect() override;
    void processWet (float* left, float* right, int n) noexcept override;
    void runMode (int mode, const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void runAnalog (int mode, const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void runDigital (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void runCodec (const float* inL, const float* inR, float* outL, float* outR, int n) noexcept;
    void codecFrame() noexcept;
    void analyse (const float* l, const float* r, int n) noexcept;
    void event (int kind) noexcept;
    void design() noexcept;
    int personality (const RetroSettings& s) const noexcept { return s.mode == RetroSettings::digital && s.era == RetroSettings::codec ? 3 : s.mode; }

    RetroSettings p;
    int slot = 0;
    std::unique_ptr<State> st;
};

} // namespace andor::analog::fx
