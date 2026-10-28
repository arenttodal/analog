#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace andor::analog::math
{

constexpr double pi = 3.14159265358979323846;

/** 2^x for audio-rate modulation (relative error below 2e-6, 0.003 cent, for |x| < 60): the
    integer part goes straight into the exponent, the fraction through a 7th-order Taylor
    polynomial of e^(f ln 2). */
inline float exp2Fast (float x) noexcept
{
    x = std::clamp (x, -60.0f, 60.0f);
    const float fl = std::floor (x);
    const float f = x - fl;
    const float p = 1.0f + f * (0.693147182f + f * (0.240226507f + f * (0.0555041087f + f * (0.00961812911f + f * (0.00133335581f + f * (0.000154035304f + f * 0.0000152527338f))))));
    const auto e = static_cast<std::int32_t> (fl) + 127;
    std::int32_t bits = e << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof scale);
    return p * scale;
}

/** tanh as its [7/6] Pade approximant: exact to 1e-12 near zero (no noise floor on quiet
    signals), within 1e-4 up to |x| = 4.97, where it reaches 1 and is held there. Odd,
    smooth and bounded by +-1. */
inline float tanhFast (float x) noexcept
{
    x = std::clamp (x, -4.97178686f, 4.97178686f);
    const float x2 = x * x;
    const float num = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return num / den;
}

inline double semitonesToRatio (double st) noexcept { return std::exp2 (st / 12.0); }
inline double noteToHz (double note) noexcept { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }
inline double hzToNote (double hz) noexcept { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
inline float dbToGain (double db) noexcept { return static_cast<float> (std::pow (10.0, db / 20.0)); }

/** TONE (0..1) -> cutoff in Hz: 20 Hz .. 18 kHz, one octave per 1/9.8 of the knob. */
inline double toneToHz (double tone) noexcept { return 20.0 * std::pow (900.0, std::clamp (tone, 0.0, 1.0)); }
inline double hzToTone (double hz) noexcept { return std::log (std::clamp (hz, 20.0, 18000.0) / 20.0) / std::log (900.0); }

/** Coefficient of a one-pole smoother reaching 63 % in `seconds` at `rate`. */
inline float smoothingCoefficient (double seconds, double rate) noexcept
{
    return seconds <= 0.0 ? 1.0f : static_cast<float> (1.0 - std::exp (-1.0 / (seconds * rate)));
}

} // namespace andor::analog::math
