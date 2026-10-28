#pragma once

#include "AnalogMath.h"

namespace andor::analog
{

/**
    The four-pole (24 dB/oct) resonant low-pass: four zero-delay-feedback one-pole stages
    (topology-preserving transform, trapezoidal integrators) in a ladder with global
    negative feedback, and a tanh saturator where the feedback meets the input.

    Each stage, with g = tan(pi fc / fs) and G = g / (1 + g):
        v = G (x - s);  y = v + s;  s = y + v
    The feedback loop has no unit delay: the ladder's output is solved for in closed form
    from the stages' states (the linear solution), and that estimate drives the saturator,
    so the resonance stays tuned up to the top of the range, and an abrupt cutoff or
    resonance change cannot blow the loop up (tanh bounds what enters it).

    k (0..~4.2) is the loop gain: around 4 the filter rings by itself, its level held by the
    saturator. Pass-band gain falls as 1 / (1 + k) as resonance rises (the "bass loss" of a
    real ladder); `compensation` restores part of it.

    Real-time safe; the coefficient g comes from the caller (a table lookup, see Voice).
*/
class LadderFilter
{
public:
    void reset() noexcept { s1 = s2 = s3 = s4 = 0.0f; }

    /** x: input; g = tan(pi fc / fs); k: feedback; bias: DC added before the saturator
        (asymmetric clipping: even harmonics), removed again after it. */
    float process (float x, float g, float k, float bias, float biasOffset) noexcept
    {
        const float G = g / (1.0f + g);
        const float beta = 1.0f / (1.0f + g);
        const float G2 = G * G;
        const float G4 = G2 * G2;
        // y4 = G^4 u + S, with S what the states contribute.
        const float S = beta * (G * G2 * s1 + G2 * s2 + G * s3 + s4);
        const float estimate = (G4 * x + S) / (1.0f + k * G4);
        const float u = math::tanhFast (x - k * estimate + bias) - biasOffset;

        float v = G * (u - s1);
        const float y1 = v + s1;
        s1 = y1 + v;
        v = G * (y1 - s2);
        const float y2 = v + s2;
        s2 = y2 + v;
        v = G * (y2 - s3);
        const float y3 = v + s3;
        s3 = y3 + v;
        v = G * (y3 - s4);
        const float y4 = v + s4;
        s4 = y4 + v;
        return y4;
    }

    bool isQuiet() const noexcept
    {
        return std::abs (s1) + std::abs (s2) + std::abs (s3) + std::abs (s4) < 1.0e-7f;
    }

private:
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
};

} // namespace andor::analog
