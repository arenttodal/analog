#pragma once

#include "core/Prng.h"

#include <cstdint>
#include <vector>

namespace andor::analog
{

/**
    A shared, post-voice, Juno-style bucket-brigade (BBD) chorus, at the host rate.

    The mono sum of the input runs through a gentle compander-like saturation and an
    anti-alias low-pass into one delay line (the "BBD"). Two taps read it, modulated by one
    LFO in opposite phase; each tap has its own reconstruction low-pass (and, optionally,
    its own hiss, as the hardware's two BBD chips do). Left = dry L + wet(+LFO), right =
    dry R + wet(-LFO). The dry path has no latency.

    Figures (from first principles plus public write-ups; none is copied from a product):
      - LFO rates: I 0.513 Hz and II 0.863 Hz, triangles. These are the figures given by
        the open-source KR-106 emulation's DSP notes (github.com/kayrockscreenprinting/
        ultramaster_kr106, docs/DSP_ARCHITECTURE.md). A GroupDIY forum member's scope of a
        Juno-60 (groupdiy.com, "Juno-60 chorus LFO frequency too fast") measured periods of
        about 2.3 s and 1.4 s (0.43 / 0.71 Hz), which he reports as under 20 % off from the
        nominal figures: both are approximations, the units differ.
      - I+II: the same forum report describes a sine "a little over 8 Hz"; 8.3 Hz is chosen
        (an approximation, low confidence), with a shallow sweep, so it reads as a vibrato.
      - Delay: the Juno's MN3009 BBDs can span 0.64-12.8 ms (datasheet, as quoted by
        Eventide's forum and SoundForce's module notes) and the Juno uses only a narrow
        part of that. I and II sweep 1.65-5.35 ms (3.5 ms +/- 1.85 ms), I+II 3.3-3.7 ms
        (3.5 ms +/- 0.2 ms). These match the commonly quoted "about 1.5-5.5 ms" sweep, but
        are approximations, not measurements of a unit.
      - BBD band limit: 2nd-order Butterworth low-pass at 9.5 kHz before the line and
        another after each tap (4th order overall), clamped to 0.45 x the rate. The Juno's
        BBD clock falls to roughly 25 kHz at the longest delay (256 stages:
        delay = 128 / clock), so its filters sit around 8-10 kHz; 9.5 kHz is an
        approximation in that range.
      - Saturation: tanh (0.5 x) / 0.5 on the BBD input (rational approximation of tanh):
        under 1 % compression at -10 dBFS, ~8 % at full scale. A stand-in for the
        compander/BBD headroom, not a model of a specific circuit.
      - Hiss: uniform white noise per tap ahead of each reconstruction filter, scaled per
        sample rate so the output carries about -66 dBFS RMS per channel (the requested
        -70..-60 dBFS window). Seeded with osp::Prng; off by default.
      - Gains: dry x 0.74, wet x 0.74 (about -2.6 dB each), so engaging the chorus keeps
        broadband material within +/-1.5 dB of the bypassed level (measured at 48 kHz,
        tests/ChorusTests.cpp: saw chord -0.6 dB in I and II, -1.3 dB in I+II; white noise
        -1.4 dB, the wet path's 9.5 kHz band limit; pink noise +1.0 dB, low frequencies add
        in phase). A single note can sit on a comb notch: a lone 440 Hz saw in I+II
        measured -2.4 dB.

    Mode changes crossfade over 30 ms between the old and the new tap set (both read the
    same line); a change asked during a fade is applied when that fade ends. With mode OFF
    and its fade finished, process() returns without touching the buffers (bit-exact).

    Threading: prepare() allocates (message thread). Everything else is real-time safe and
    belongs to the audio thread (no allocation, no locks). Deterministic for the same seed,
    input, and the sample positions of setMode/setNoise calls; the block size does not
    change the output.
*/
class JunoChorus
{
public:
    enum Mode { off = 0, one = 1, two = 2, both = 3 };

    struct ModeSpec
    {
        double rateHz = 0.0;
        double centreMs = 0.0;
        double depthMs = 0.0;   ///< the sweep is centreMs +/- depthMs
        bool sine = false;      ///< sine LFO (I+II) or triangle (I, II)
    };
    static ModeSpec spec (int mode) noexcept;

    static constexpr double dryGain = 0.74;
    static constexpr double wetGain = 0.74;
    static constexpr double fadeSeconds = 0.03;
    static constexpr double filterHz = 9500.0;
    static constexpr double hissDbfs = -66.0;   ///< RMS per channel at the output
    static constexpr double drive = 0.5;

    void prepare (double sampleRate, int maximumBlockSize);
    /** Silences the line and filters, restarts the LFO at its zero crossing, re-seeds the
        hiss from the stored seed, and settles on the requested mode without a fade. */
    void reset() noexcept;
    /** 0 OFF, 1 I, 2 II, 3 I+II. A change crossfades (about 30 ms): never a click. */
    void setMode (int mode) noexcept;
    int getMode() const noexcept { return target; }
    /** The BBD's hiss (seeded, deterministic). Off by default. A new seed restarts the
        noise sequence; calling again with the same seed does not (safe to call per block). */
    void setNoise (bool on, std::uint32_t seed) noexcept;
    /** In place, stereo in and out. With mode OFF (and its crossfade finished) the output
        is the input, bit for bit. */
    void process (float* left, float* right, int numSamples) noexcept;
    /** True while it changes the sound (on, or fading out). */
    bool isActive() const noexcept;

    double sampleRate() const noexcept { return rate; }

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        void setLowPass (double cutoffHz, double sampleRate) noexcept;
        void clear() noexcept { z1 = z2 = 0.0; }
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Slot
    {
        int mode = off;
        double phase = 0.0;   ///< 0..1; 0 is the LFO's rising zero crossing
        Biquad postLeft, postRight;
    };

    void clearLine() noexcept;
    double readTap (double delaySamples) const noexcept;
    double hiss() noexcept;

    double rate = 48000.0;
    std::vector<float> line;
    std::uint32_t mask = 0;
    std::uint32_t write = 0;
    Biquad pre;
    Slot slots[2];
    int current = 0;          ///< the slot fading in, or the only one sounding
    int target = off;
    bool fading = false;
    int fadePos = 0, fadeLength = 1;

    bool noiseOn = false;
    std::uint32_t noiseSeed = 0;
    double noiseAmplitude = 0.0;
    osp::Prng prng;
};

} // namespace andor::analog
