#pragma once

// Original synthetic test signals for the MORPH analysis (no audio files, no third-party
// audio). Every stochastic part draws from osp::Prng with the given seed, so each fixture is
// bit-for-bit repeatable. All are mono float at the requested rate.

#include "core/Prng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace morphfixtures
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    inline std::vector<float> silence (double seconds, double rate)
    {
        return std::vector<float> (static_cast<std::size_t> (std::max (0.0, std::round (seconds * rate))), 0.0f);
    }

    /** Raised-cosine fade in/out (seconds) so the band-limited tones start without a click. */
    inline void fade (std::vector<float>& x, double rate, double inSeconds = 0.005, double outSeconds = 0.01)
    {
        const auto n = x.size();
        const auto fin = std::min (n, static_cast<std::size_t> (inSeconds * rate));
        const auto fout = std::min (n, static_cast<std::size_t> (outSeconds * rate));
        for (std::size_t i = 0; i < fin; ++i)
            x[i] *= static_cast<float> (0.5 - 0.5 * std::cos (std::numbers::pi * static_cast<double> (i) / static_cast<double> (fin)));
        for (std::size_t i = 0; i < fout; ++i)
            x[n - 1 - i] *= static_cast<float> (0.5 - 0.5 * std::cos (std::numbers::pi * static_cast<double> (i) / static_cast<double> (fout)));
    }

    inline std::vector<float> sine (double hz, double seconds, double rate, double amplitude = 0.5)
    {
        auto x = silence (seconds, rate);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (amplitude * std::sin (twoPi * hz * static_cast<double> (i) / rate));
        fade (x, rate);
        return x;
    }

    /** Band-limited triangle: odd harmonics at 1/h^2 (alternating sign) up to 0.45 x rate. */
    inline std::vector<float> triangle (double hz, double seconds, double rate, double amplitude = 0.5)
    {
        auto x = silence (seconds, rate);
        std::vector<double> acc (x.size(), 0.0);
        const double scale = amplitude * 8.0 / (std::numbers::pi * std::numbers::pi);
        for (int h = 1; h * hz < 0.45 * rate; h += 2)
        {
            const double sign = ((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
            const double gain = scale * sign / (static_cast<double> (h) * h);
            const double w = twoPi * h * hz / rate;
            for (std::size_t i = 0; i < x.size(); ++i)
                acc[i] += gain * std::sin (w * static_cast<double> (i));
        }
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (acc[i]);
        fade (x, rate);
        return x;
    }

    inline std::vector<float> whiteNoise (double seconds, double rate, std::uint64_t seed, double amplitude = 0.25)
    {
        osp::Prng rng (seed);
        auto x = silence (seconds, rate);
        for (auto& v : x)
            v = static_cast<float> (amplitude * rng.bipolar());
        return x;
    }

    /** A single full-scale impulse at `atSeconds` in otherwise digital silence. */
    inline std::vector<float> click (double seconds, double rate, double atSeconds = 0.25)
    {
        auto x = silence (seconds, rate);
        const auto i = static_cast<std::size_t> (atSeconds * rate);
        if (i < x.size())
            x[i] = 1.0f;
        return x;
    }

    inline std::vector<float> dc (double seconds, double rate, double value = 0.5)
    {
        return std::vector<float> (static_cast<std::size_t> (seconds * rate), static_cast<float> (value));
    }

    /** A 10 ms tone: shorter than any analysis window. */
    inline std::vector<float> tooShort (double rate, double hz = 440.0)
    {
        auto x = silence (0.010, rate);
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.5 * std::sin (twoPi * hz * static_cast<double> (i) / rate));
        return x;
    }

    // ---- Bowed string ------------------------------------------------------------------------

    /** Body resonances of the bowed-string stand-in: centre (Hz), gain (dB), width (octaves). */
    struct Resonance
    {
        double hz, gainDb, widthOctaves;
    };
    inline constexpr std::array<Resonance, 3> bowedResonances { { { 450.0, 18.0, 0.15 }, { 1250.0, 18.0, 0.15 }, { 2900.0, 15.0, 0.15 } } };
    inline constexpr double bowedInharmonicity = 1.0e-5;
    inline constexpr double bowedVibratoHz = 5.5;
    inline constexpr double bowedVibratoCents = 15.0;
    inline constexpr double bowedAttackSeconds = 0.15;

    /** The fixed body (formant) gain, linear, at `hz`: Gaussian bumps in log frequency. */
    inline double bowedBodyGain (double hz)
    {
        double db = 0.0;
        for (const auto& r : bowedResonances)
        {
            const double o = std::log2 (hz / r.hz) / r.widthOctaves;
            db += r.gainDb * std::exp (-0.5 * o * o);
        }
        return std::pow (10.0, db / 20.0);
    }

    /**
        A nyckelharpa/cello-like sustained tone (a stand-in only): a Helmholtz-like 1/h source
        whose partials pass through a fixed body-resonance envelope at their instantaneous
        frequency (so vibrato also moves their levels, as on a real instrument), a 150 ms
        bowed attack, 5.5 Hz / +/-15 cent vibrato fading in after the attack, slight stiffness
        (B = 1e-5) and band-limited bow noise that is stronger during the attack.
    */
    inline std::vector<float> bowedString (double rootHz, double seconds, double rate, std::uint64_t seed)
    {
        osp::Prng rng (seed);
        auto x = silence (seconds, rate);
        const auto n = x.size();
        std::vector<double> acc (n, 0.0);

        // Pitch track (shared by all partials): vibrato depth fades in from 0.2 s to 0.6 s.
        const double vibratoPhase = rng.uniform (0.0, twoPi);
        std::vector<double> hz (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double depth = std::clamp ((t - 0.2) / 0.4, 0.0, 1.0);
            hz[i] = rootHz * std::exp2 (depth * bowedVibratoCents / 1200.0 * std::sin (twoPi * bowedVibratoHz * t + vibratoPhase));
        }
        // Bow envelope: raised-cosine attack, a slight bow-pressure swell, 80 ms release.
        std::vector<double> env (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double attack = t < bowedAttackSeconds ? 0.5 - 0.5 * std::cos (std::numbers::pi * t / bowedAttackSeconds) : 1.0;
            const double release = std::clamp ((seconds - t) / 0.08, 0.0, 1.0);
            env[i] = attack * release * (1.0 + 0.03 * std::sin (twoPi * 0.7 * t));
        }

        const int harmonics = std::min (80, static_cast<int> (0.42 * rate / rootHz));
        for (int h = 1; h <= harmonics; ++h)
        {
            const double stretch = h * std::sqrt (1.0 + bowedInharmonicity * h * h);
            double phase = rng.uniform (0.0, twoPi);
            double gain = 0.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                const double f = stretch * hz[i];
                // The body gain moves only with the vibrato: every 32 samples is plenty (and fast).
                if (i % 32 == 0)
                    gain = f < 0.45 * rate ? bowedBodyGain (f) / h : 0.0;
                acc[i] += gain * std::sin (phase);
                phase += twoPi * f / rate;
            }
        }

        // Bow noise: Gaussian, band-limited (one-pole high-pass ~800 Hz, two one-pole low-passes
        // ~6 kHz), louder while the bow bites in.
        double hp = 0.0, prevIn = 0.0, lp1 = 0.0, lp2 = 0.0;
        const double hpCoef = std::exp (-twoPi * 800.0 / rate);
        const double lpCoef = std::exp (-twoPi * 6000.0 / rate);
        double harmonicPeak = 1.0e-9;
        for (std::size_t i = 0; i < n; ++i)
            harmonicPeak = std::max (harmonicPeak, std::abs (acc[i] * env[i]));
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double> (i) / rate;
            const double in = rng.gaussian();
            hp = hpCoef * (hp + in - prevIn);
            prevIn = in;
            lp1 += (1.0 - lpCoef) * (hp - lp1);
            lp2 += (1.0 - lpCoef) * (lp1 - lp2);
            const double scratch = 1.0 + 3.0 * std::exp (-t / 0.05);
            const double noise = 0.04 * harmonicPeak * scratch * lp2;
            acc[i] = acc[i] * env[i] + noise * env[i];
        }

        double peak = 1.0e-9;
        for (double v : acc)
            peak = std::max (peak, std::abs (v));
        for (std::size_t i = 0; i < n; ++i)
            x[i] = static_cast<float> (0.5 * acc[i] / peak);
        return x;
    }

    // ---- Pluck -------------------------------------------------------------------------------

    /** A plucked-string stand-in by modal synthesis: sin(pi h beta)/h^1.5 partials (picked
        at beta = 0.13 of the length: a little brighter than an ideal string's 1/h^2), each decaying faster than the last (T60 1.2 s for the
        fundamental), slight stiffness (B = 5e-5) and a 4 ms seeded noise burst at the pluck. */
    inline std::vector<float> pluck (double rootHz, double seconds, double rate, std::uint64_t seed)
    {
        osp::Prng rng (seed);
        auto x = silence (seconds, rate);
        const auto n = x.size();
        std::vector<double> acc (n, 0.0);
        const double beta = 0.13, t60 = 1.2, b = 5.0e-5;
        const int harmonics = std::min (40, static_cast<int> (0.42 * rate / rootHz));
        for (int h = 1; h <= harmonics; ++h)
        {
            const double f = h * rootHz * std::sqrt (1.0 + b * h * h);
            if (f >= 0.45 * rate)
                break;
            const double amp = std::abs (std::sin (std::numbers::pi * h * beta)) / std::pow (static_cast<double> (h), 1.5);
            const double tau = (t60 / (1.0 + 0.15 * (h - 1))) / std::log (1000.0);
            const double phase = rng.uniform (0.0, twoPi);
            const double w = twoPi * f / rate;
            const double decay = std::exp (-1.0 / (tau * rate));
            double g = amp;
            for (std::size_t i = 0; i < n; ++i)
            {
                acc[i] += g * std::sin (phase + w * static_cast<double> (i));
                g *= decay;
            }
        }
        const auto burst = std::min (n, static_cast<std::size_t> (0.004 * rate));
        double lp = 0.0;
        for (std::size_t i = 0; i < burst; ++i)
        {
            lp += 0.3 * (rng.bipolar() - lp);
            acc[i] += 0.2 * lp * (1.0 - static_cast<double> (i) / static_cast<double> (burst));
        }
        double peak = 1.0e-9;
        for (double v : acc)
            peak = std::max (peak, std::abs (v));
        for (std::size_t i = 0; i < n; ++i)
            x[i] = static_cast<float> (0.5 * acc[i] / peak);
        // A 1 ms fade-in: a pluck starts sharp, but not with a step from digital silence.
        fade (x, rate, 0.001, 0.01);
        return x;
    }

    /** `x` after `seconds` of digital silence. */
    inline std::vector<float> delayed (const std::vector<float>& x, double seconds, double rate)
    {
        auto y = silence (seconds, rate);
        y.insert (y.end(), x.begin(), x.end());
        return y;
    }
} // namespace morphfixtures
