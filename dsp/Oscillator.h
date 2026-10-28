#pragma once

#include "Patch.h"
#include "PolyBlep.h"

#include <cmath>

namespace andor::analog::osc
{

/** Which waveforms an oscillator sums, and the gain that keeps the sum's level steady. */
struct Waves
{
    bool saw = false, triangle = false, pulse = false;
    float gain = 1.0f;
};

inline Waves wavesOf (Osc1Wave w) noexcept
{
    Waves s;
    s.saw = w != Osc1Wave::pulse;
    s.pulse = w != Osc1Wave::saw;
    s.gain = w == Osc1Wave::sawPulse ? 0.70710678f : 1.0f;
    return s;
}

inline Waves wavesOf (Osc2Wave w) noexcept
{
    Waves s;
    s.saw = w == Osc2Wave::saw || w == Osc2Wave::sawTriangle || w == Osc2Wave::sawPulse || w == Osc2Wave::all;
    s.triangle = w == Osc2Wave::triangle || w == Osc2Wave::sawTriangle || w == Osc2Wave::trianglePulse || w == Osc2Wave::all;
    s.pulse = w == Osc2Wave::pulse || w == Osc2Wave::sawPulse || w == Osc2Wave::trianglePulse || w == Osc2Wave::all;
    const int count = (s.saw ? 1 : 0) + (s.triangle ? 1 : 0) + (s.pulse ? 1 : 0);
    s.gain = count == 3 ? 0.57735027f : (count == 2 ? 0.70710678f : 1.0f);
    return s;
}

/** The waveforms' sum at phase p (0..1) without band-limiting. SAW rises from -1 to 1;
    TRIANGLE starts at -1 and peaks at 0.5; PULSE is +1 below the width, its DC removed. */
inline float naive (const Waves& w, double p, float width) noexcept
{
    float v = 0.0f;
    if (w.saw)
        v += static_cast<float> (2.0 * p - 1.0);
    if (w.triangle)
        v += static_cast<float> (p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
    if (w.pulse)
        v += (p < width ? 1.0f : -1.0f) - (2.0f * width - 1.0f);
    return v * w.gain;
}

/** The sum's slope at phase p, per sample. */
inline float slope (const Waves& w, double p, double dt) noexcept
{
    double s = 0.0;
    if (w.saw)
        s += 2.0 * dt;
    if (w.triangle)
        s += p < 0.5 ? 4.0 * dt : -4.0 * dt;
    return static_cast<float> (s) * w.gain;
}

/**
    Moves `phase` on by `duration` samples at `dt` per sample, the segment ending `endBefore`
    samples before the sample being computed, and corrects every discontinuity it passes on
    `line`: the wrap (SAW falls by 2, PULSE rises by 2, TRIANGLE turns up), the triangle's
    peak (turns down) and the pulse's falling edge. Returns the new phase; `wrapAt` gets the
    wrap's time before the sample (or stays as it was if there is none).
*/
inline double walk (double phase, double dt, float width, const Waves& w, double duration, double endBefore, BlepLine& line,
                    double& wrapAt) noexcept
{
    double remaining = duration;
    for (int guard = 0; guard < 4 && remaining > 0.0; ++guard)
    {
        double next = 1.0;
        int kind = 0;   // 0 wrap, 1 triangle peak, 2 pulse edge
        if (w.triangle && phase < 0.5)
        {
            next = 0.5;
            kind = 1;
        }
        if (w.pulse && phase < width && width < next)
        {
            next = width;
            kind = 2;
        }
        const double t = (next - phase) / dt;
        if (t > remaining)
        {
            phase += dt * remaining;
            return phase;
        }
        remaining -= t;
        const auto d = static_cast<float> (endBefore + remaining);
        switch (kind)
        {
            case 0:
                if (w.saw)
                    line.step (-2.0f * w.gain, d);
                if (w.pulse)
                    line.step (2.0f * w.gain, d);
                if (w.triangle)
                    line.corner (static_cast<float> (8.0 * dt) * w.gain, d);
                phase = 0.0;
                wrapAt = d;
                break;
            case 1:
                line.corner (static_cast<float> (-8.0 * dt) * w.gain, d);
                phase = 0.5;
                break;
            default:
                line.step (-2.0f * w.gain, d);
                phase = width;
                break;
        }
    }
    // (Unreachable for dt < 0.5: at most a wrap, a peak and an edge fit in one sample.)
    phase += dt * remaining;
    return phase - std::floor (phase);
}

/**
    A hard-sync reset `d` samples before the sample being computed: the slave starts again
    from phase 0, and the jump (and change of slope) it makes is corrected like any edge.
*/
inline void syncReset (double phaseAtReset, double dt, float width, const Waves& w, float d, BlepLine& line) noexcept
{
    line.step (naive (w, 0.0, width) - naive (w, phaseAtReset, width), d);
    if (w.triangle)
        line.corner (slope (w, 0.0, dt) - slope (w, phaseAtReset, dt), d);
}

} // namespace andor::analog::osc
