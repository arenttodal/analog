#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

/**
    Small building blocks for the FX library's processors (pure C++, no JUCE). Coefficients are
    computed in double at control rate (never per sample); the per-sample paths are plain floats
    with no allocation.
*/
namespace andor::analog::fx::dsp
{

constexpr double pi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, 0.05f * db); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (1.0e-9f, g)); }
inline float flushDenormal (float x) noexcept { return std::abs (x) < 1.0e-20f ? 0.0f : x; }

/** A linear glide to a target over a fixed time (parameters that must not zipper). */
struct Glide
{
    float value = 0.0f, target = 0.0f, step = 0.0f;
    int steps = 1, left = 0;
    void prepare (double rate, double seconds) noexcept { steps = std::max (1, static_cast<int> (rate * seconds)); }
    void jump (float v) noexcept
    {
        value = target = v;
        left = 0;
    }
    void set (float v) noexcept
    {
        if (v == target)
            return;
        target = v;
        step = (target - value) / static_cast<float> (steps);
        left = steps;
    }
    float next() noexcept
    {
        if (left > 0)
        {
            value += step;
            if (--left == 0)
                value = target;
        }
        return value;
    }
    bool moving() const noexcept { return left > 0; }
};

/** A biquad (transposed direct form II), its coefficients from RBJ's cookbook in double. */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    enum class Shape { peak, lowShelf, highShelf, highPass, lowPass, bandPass, notch, allPass };

    void reset() noexcept { z1 = z2 = 0.0f; }
    void identity() noexcept
    {
        b0 = 1;
        b1 = b2 = a1 = a2 = 0;
    }
    void design (Shape shape, double rate, double hz, double q, double gainDb) noexcept
    {
        hz = std::clamp (hz, 5.0, 0.49 * rate);
        q = std::max (0.05, q);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * hz / rate, c = std::cos (w), s = std::sin (w);
        const double alpha = s / (2.0 * q);
        double nb0 = 1, nb1 = 0, nb2 = 0, na0 = 1, na1 = 0, na2 = 0;
        switch (shape)
        {
            case Shape::peak:
                nb0 = 1 + alpha * A; nb1 = -2 * c; nb2 = 1 - alpha * A; na0 = 1 + alpha / A; na1 = -2 * c; na2 = 1 - alpha / A;
                break;
            case Shape::lowShelf:
            {
                const double sq = 2.0 * std::sqrt (A) * alpha;
                nb0 = A * ((A + 1) - (A - 1) * c + sq); nb1 = 2 * A * ((A - 1) - (A + 1) * c); nb2 = A * ((A + 1) - (A - 1) * c - sq);
                na0 = (A + 1) + (A - 1) * c + sq; na1 = -2 * ((A - 1) + (A + 1) * c); na2 = (A + 1) + (A - 1) * c - sq;
                break;
            }
            case Shape::highShelf:
            {
                const double sq = 2.0 * std::sqrt (A) * alpha;
                nb0 = A * ((A + 1) + (A - 1) * c + sq); nb1 = -2 * A * ((A - 1) + (A + 1) * c); nb2 = A * ((A + 1) + (A - 1) * c - sq);
                na0 = (A + 1) - (A - 1) * c + sq; na1 = 2 * ((A - 1) - (A + 1) * c); na2 = (A + 1) - (A - 1) * c - sq;
                break;
            }
            case Shape::highPass:
                nb0 = (1 + c) / 2; nb1 = -(1 + c); nb2 = (1 + c) / 2; na0 = 1 + alpha; na1 = -2 * c; na2 = 1 - alpha;
                break;
            case Shape::lowPass:
                nb0 = (1 - c) / 2; nb1 = 1 - c; nb2 = (1 - c) / 2; na0 = 1 + alpha; na1 = -2 * c; na2 = 1 - alpha;
                break;
            case Shape::bandPass:
                nb0 = alpha; nb1 = 0; nb2 = -alpha; na0 = 1 + alpha; na1 = -2 * c; na2 = 1 - alpha;
                break;
            case Shape::notch:
                nb0 = 1; nb1 = -2 * c; nb2 = 1; na0 = 1 + alpha; na1 = -2 * c; na2 = 1 - alpha;
                break;
            case Shape::allPass:
                nb0 = 1 - alpha; nb1 = -2 * c; nb2 = 1 + alpha; na0 = 1 + alpha; na1 = -2 * c; na2 = 1 - alpha;
                break;
        }
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = static_cast<float> (b1 * x - a1 * y + z2);
        z2 = static_cast<float> (b2 * x - a2 * y);
        z1 = flushDenormal (z1);
        z2 = flushDenormal (z2);
        return static_cast<float> (y);
    }
    /** |H| at `hz` (the graphs: the coefficients that play). */
    double magnitude (double rate, double hz) const noexcept
    {
        const std::complex<double> z = std::polar (1.0, -2.0 * pi * hz / rate);
        const auto num = b0 + b1 * z + b2 * z * z, den = 1.0 + a1 * z + a2 * z * z;
        return std::abs (num / den);
    }
};

/** Linkwitz-Riley 24 dB crossover (two Butterworth pairs): low + high is an all-pass (flat
    magnitude); allPass() is the same phase for a band that does not split here. */
struct Crossover
{
    std::array<Biquad, 2> lp, hp, ap;
    void design (double rate, double hz) noexcept
    {
        for (auto& f : lp)
            f.design (Biquad::Shape::lowPass, rate, hz, 0.70710678, 0.0);
        for (auto& f : hp)
            f.design (Biquad::Shape::highPass, rate, hz, 0.70710678, 0.0);
        // LR4 low + high = the 2nd-order all-pass at hz (Q 0.5 equivalent): two first-order
        // sections' worth, built as one Butterworth all-pass pair's phase.
        ap[0].design (Biquad::Shape::allPass, rate, hz, 0.70710678, 0.0);
        ap[1].identity();
    }
    void reset() noexcept
    {
        for (auto* set : { &lp, &hp, &ap })
            for (auto& f : *set)
                f.reset();
    }
    void split (float x, float& low, float& high) noexcept
    {
        low = lp[1].process (lp[0].process (x));
        high = hp[1].process (hp[0].process (x));
    }
    float allPass (float x) noexcept { return ap[0].process (x); }
};

/** Cytomic's trapezoidal SVF (low, band, high from one state pair). */
struct Svf
{
    double g = 0.1, k = 1.4, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0, ic2 = 0;
    void reset() noexcept { ic1 = ic2 = 0.0f; }
    void design (double rate, double hz, double damping) noexcept
    {
        g = std::tan (pi * std::clamp (hz, 5.0, 0.48 * rate) / rate);
        k = std::max (0.02, damping);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void process (float x, float& low, float& band, float& high) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flushDenormal (static_cast<float> (2.0 * v1 - ic1));
        ic2 = flushDenormal (static_cast<float> (2.0 * v2 - ic2));
        low = static_cast<float> (v2);
        band = static_cast<float> (v1);
        high = static_cast<float> (x - k * v1 - v2);
    }
    /** The low / band / high magnitudes at hz (for graphs). */
    std::complex<double> response (double rate, double hz, int output) const noexcept
    {
        // Analog prototype at the prewarped frequency (the trapezoidal SVF matches it exactly
        // under the bilinear map).
        const double w = std::tan (pi * std::min (hz, 0.499 * rate) / rate) / g;
        const std::complex<double> s (0.0, w);
        const auto den = s * s + k * s + 1.0;
        if (output == 0)
            return 1.0 / den;
        if (output == 1)
            return s / den;
        return s * s / den;
    }
};

/** A ring buffer with a 4-point Hermite read (fractional, modulated delays). */
struct DelayLine
{
    std::vector<float> data;
    int mask = 0, write = 0;
    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4)
            size <<= 1;
        data.assign (static_cast<std::size_t> (size), 0.0f);
        mask = size - 1;
        write = 0;
    }
    void reset() noexcept
    {
        std::fill (data.begin(), data.end(), 0.0f);
        write = 0;
    }
    void push (float x) noexcept
    {
        data[static_cast<std::size_t> (write)] = x;
        write = (write + 1) & mask;
    }
    /** `delay` samples behind the last sample pushed (>= 1). */
    float read (float delay) const noexcept
    {
        const float d = std::max (1.0f, delay);
        const int i = static_cast<int> (d);
        const float f = d - static_cast<float> (i);
        auto at = [this] (int back) { return data[static_cast<std::size_t> ((write - back) & mask)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    float readInteger (int delay) const noexcept { return data[static_cast<std::size_t> ((write - delay) & mask)]; }
};

/** A radix-2 complex FFT (in place), its twiddles made in prepare(). */
struct Fft
{
    int size = 0, bits = 0;
    std::vector<std::complex<float>> twiddles;
    std::vector<int> reversed;
    void prepare (int n)
    {
        size = n;
        bits = 0;
        while ((1 << bits) < n)
            ++bits;
        twiddles.resize (static_cast<std::size_t> (n / 2));
        for (int i = 0; i < n / 2; ++i)
            twiddles[static_cast<std::size_t> (i)] = std::polar (1.0f, static_cast<float> (-2.0 * pi * i / n));
        reversed.resize (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            reversed[static_cast<std::size_t> (i)] = r;
        }
    }
    void transform (std::complex<float>* x, bool inverse) const noexcept
    {
        for (int i = 0; i < size; ++i)
            if (const int r = reversed[static_cast<std::size_t> (i)]; r > i)
                std::swap (x[i], x[r]);
        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len / 2, step = size / len;
            for (int i = 0; i < size; i += len)
                for (int j = 0; j < half; ++j)
                {
                    auto w = twiddles[static_cast<std::size_t> (j * step)];
                    if (inverse)
                        w = std::conj (w);
                    const auto t = w * x[i + j + half];
                    x[i + j + half] = x[i + j] - t;
                    x[i + j] += t;
                }
        }
        if (inverse)
        {
            const float scale = 1.0f / static_cast<float> (size);
            for (int i = 0; i < size; ++i)
                x[i] *= scale;
        }
    }
};

/** Balance (not pan law): the far side falls, the centre stays at unity, nothing is boosted. */
inline void balance (float position, float& left, float& right) noexcept
{
    left = std::min (1.0f, 1.0f - position);
    right = std::min (1.0f, 1.0f + position);
}

/** The shared rate divisions (synced LFOs): beats per cycle, by index. */
constexpr std::array<double, 12> divisionBeats { 16.0, 8.0, 4.0, 2.0, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125 };
constexpr std::array<const char*, 12> divisionNames { "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/8D", "1/8", "1/4T", "1/16D", "1/16", "1/8T", "1/32" };
inline double syncedHz (double bpm, int division) noexcept
{
    const auto beats = divisionBeats[static_cast<std::size_t> (std::clamp (division, 0, static_cast<int> (divisionBeats.size()) - 1))];
    return std::max (1.0, bpm) / 60.0 / beats;
}

} // namespace andor::analog::fx::dsp
