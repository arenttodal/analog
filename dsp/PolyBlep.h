#pragma once

namespace andor::analog
{

/**
    Band-limited corrections for an oscillator's discontinuities, applied with look-behind:
    the oscillator's output is delayed by two samples, so a discontinuity found while
    computing sample n corrects n - 2, n - 1, n and n + 1. Every edge, a hard-sync reset
    nobody could predict included, takes the same correction.

    The band-limited step is the integral of a cubic B-spline (four samples wide), its
    corner (a change of slope) the integral of that. Its spectrum falls as sinc^4, so the
    folded energy is about 40 dB below the two-point PolyBLEP's at the same oversampling.

    `d` is how long before sample n the discontinuity happened, in samples (0 <= d < 1).
    docs/SPEC.md ("Oscillators") derives the residuals.
*/
namespace blep
{
    /** The cubic B-spline step's cumulative value at x (in samples, -2..2) minus the ideal step. */
    inline float stepResidual (float x) noexcept
    {
        // Phi(x) for x <= 0; the residual is odd: r(-x) = -r(x).
        const bool negative = x < 0.0f;
        const float a = negative ? x : -x;   // a <= 0
        float phi;
        if (a <= -2.0f)
            phi = 0.0f;
        else if (a <= -1.0f)
        {
            const float t = a + 2.0f;
            phi = t * t * t * t * (1.0f / 24.0f);
        }
        else
        {
            const float a2 = a * a;
            phi = 0.5f + a * (2.0f / 3.0f) - a2 * a * (1.0f / 3.0f) - a2 * a2 * 0.125f;
        }
        return negative ? phi : -phi;
    }

    /** The integral of the residual above (the corner's correction), even in x. */
    inline float cornerResidual (float x) noexcept
    {
        const float a = x < 0.0f ? x : -x;
        if (a <= -2.0f)
            return 0.0f;
        if (a <= -1.0f)
        {
            const float t = a + 2.0f;
            return t * t * t * t * t * (1.0f / 120.0f);
        }
        const float a2 = a * a;
        return 7.0f / 30.0f + a * 0.5f + a2 * (1.0f / 3.0f) - a2 * a2 * (1.0f / 12.0f) - a2 * a2 * a * (1.0f / 40.0f);
    }
}

struct BlepLine
{
    float older = 0.0f;   ///< sample n - 2 (emitted next)
    float old = 0.0f;     ///< sample n - 1
    float correction = 0.0f, nextCorrection = 0.0f;   ///< collected for n and n + 1

    /** A step of height `h` (new value minus old) `d` samples before sample n. */
    void step (float h, float d) noexcept
    {
        older += h * blep::stepResidual (d - 2.0f);
        old += h * blep::stepResidual (d - 1.0f);
        correction += h * blep::stepResidual (d);
        nextCorrection += h * blep::stepResidual (d + 1.0f);
    }

    /** A change of slope by `delta` (value per sample) `d` samples before sample n. */
    void corner (float delta, float d) noexcept
    {
        older += delta * blep::cornerResidual (d - 2.0f);
        old += delta * blep::cornerResidual (d - 1.0f);
        correction += delta * blep::cornerResidual (d);
        nextCorrection += delta * blep::cornerResidual (d + 1.0f);
    }

    /** Emits sample n - 2 and takes sample n (its naive value plus its corrections). */
    float end (float naive) noexcept
    {
        const float out = older;
        older = old;
        old = naive + correction;
        correction = nextCorrection;
        nextCorrection = 0.0f;
        return out;
    }

    void reset() noexcept { older = old = correction = nextCorrection = 0.0f; }
};

} // namespace andor::analog
