#pragma once

#include "AnalogMath.h"

namespace andor::analog
{

/** Which OTA filter the calibration follows (see OtaFilter): the same circuit, two sets of
    constants. */
enum class OtaCharacter { juno, jupiter };

/**
    The four-stage OTA-cascade low-pass of the IR3109 / 80017 family (Juno-6/60/106,
    Jupiter-8), written from the circuit's idealised equations; not a ladder with a different
    curve.

    The circuit. Each stage is an OTA driving a grounded capacitor, buffered, with the buffer's
    output fed back to the OTA's inverting input, so the capacitor charges with a current
    proportional to tanh of the stage's *own* input difference:

        dy_i/dt = wc * sat (x_i - y_i),      x_1 = u,  x_i = y_(i-1)
        u = c * x - fb (k * y_4)             (input and resonance summed into OTA 1)
        sat (d) = tanh (h d) / h,  fb (v) = tanh (hf v) / hf   (unit slope at 0)

    Every stage saturates (what makes an OTA cascade round and slew-limited at large levels),
    unlike the transistor ladder, where one saturator at the feedback junction does it all. The
    resonance current comes from its own OTA (the resonance VCA), which saturates as well: fb.
    That second curve is what holds the self-oscillation level without detuning it. A
    memoryless curve in the loop changes the loop's gain but not its phase, so the ring stays
    at fc; the stage curves alone would hold the level only by slowing the stages (lowering
    their effective cutoff), which measured 280 to 450 cents flat at full resonance.

    The discretisation. Each stage is a trapezoidal (TPT) integrator, prewarped through
    g = tan (pi fc / fs): y = s + g f,  s' = y + g f  (f the integrator's input, s its state),
    which keeps each stage's -3 dB / -45 degree point exactly at fc, so the resonance is tuned.

    The implicit (zero-delay) loop with nonlinear stages is solved with the secant ("effective
    transconductance") method: each curve is replaced by a linear gain through the origin,
        sat (d) = T_i d,   T_i = sat (d_i*) / d_i*,   fb (v) = T_f v,   T_f = fb (v*) / v*
    (all in (0, 1]), evaluated at an estimate of that curve's input. With the T fixed, the whole
    filter is linear and its zero-delay loop has a closed form:
        G_i = g T_i / (1 + g T_i),   y_i = G_i x_i + s_i / (1 + g T_i)
        y_4 = A u + B  (A = G_1 G_2 G_3 G_4, B from the states),  u = c x - k T_f y_4
        =>  y_4 = (A c x + B) / (1 + k T_f A)
    and then the stages are evaluated in order. When the estimates equal the solved inputs, the
    linear solution *is* the exact implicit trapezoidal solution (T d = sat (d) exactly); a
    fixed-point iteration on the T approaches it (measured below). Three solves per sample
    (no data-dependent loop):
        1. with the previous sample's T (a warm start: the signal moves little per sample at
           the oversampled rate);
        2. and 3. with the T re-evaluated at the inputs of the previous solve.
    Measured against a 13-solve reference (a 110 Hz saw at amplitude 1 and 8, resonance 0.5,
    0.85 and 1, both calibrations): two solves leave errors of -41 dB at worst (JUPITER, fc
    12 kHz, resonance 1), -49..-72 dB at 2 kHz; three leave -70 dB at worst and -90 dB or less
    below 2 kHz. Hence three: 10 curve evaluations and 13 divisions per sample, measured at
    about 3.3 times LadderFilter's cost (two solves: about 2.1 times). If the CPU budget
    demands it, `refinements = 1` is the documented fallback.
    Why this rather than Newton: it cannot diverge. Every T is in (0, 1], so every G_i is in
    (0, g / (1 + g)] and 1 + k T_f A >= 1: the solve never divides by a small number, for any
    g and any input, and each state's step (2 g T d) is bounded by the curves.

    Resonance and self-oscillation. With all four stages at -45 degrees at fc, the loop's phase
    is -180 degrees there and its gain k / 4: for k > 4 the filter rings by itself at fc, and
    the ring grows until fb's falling gain brings the loop gain back to 1. The more k exceeds
    4, the louder it sings and the harder the stages work (slightly flatter, see the tests).

    12 dB/oct (Jupiter-8's 2-pole mode): the output is taken after stage 2 while the feedback
    still comes from stage 4 (the IR3109 is one 4-stage chip; the output tap moves, the loop
    stays). The response y_2 / x = c G^2 / (1 + k G^4) keeps the same loop, so the same
    resonance law, tuning and self-oscillation, and rolls off at 12 dB/oct above fc. Moving the
    feedback to stage 2 instead would give a 2-pole loop that can never reach -180 degrees: it
    could not self-oscillate, and its peak would need very large k. At fc the 2-pole tap is
    6 dB above the 4-pole output (two stages fewer at -3 dB each), so it rings louder; the
    caller trims levels. The 2-pole tap is honoured for both characters (the Juno had none;
    whether to offer it there is the caller's choice).

    Bass-loss compensation. A loop around a low-pass cuts the pass band to 1 / (1 + k) (the
    "thin" sound of a resonant Juno). The input gain c = 1 + comp * k restores part of it, so
    the pass band at DC is (1 + comp * k) / (1 + k): comp = 0 none, comp = 1 flat at any
    resonance. It is applied at OTA 1's input (as a resistor change would), so high resonance
    also drives the stages harder. It scales with the resonance current that actually flows,
    c = 1 + comp * k * T_f: identical for small signals, and it recedes when a loud signal
    saturates the resonance OTA (which removes the bass loss it was making up for), so loud
    input is not boosted by up to 1 + comp * k.

    Cutoff convention (as LadderFilter): fc is each stage's corner, where the 4-pole output is
    -12 dB (-6 dB for the 2-pole tap) at resonance 0 and where the resonance rings; the overall
    -3 dB point at resonance 0 is 0.435 fc (4-pole), 0.644 fc (2-pole).

    Real-time safe: no allocation, no data-dependent loop; the coefficient g comes from the
    caller's table (at the 4x rate, g < 0.4 up to 20 kHz; the solve is stable for any g > 0).
*/
class OtaFilter
{
public:
    void reset() noexcept
    {
        for (int i = 0; i < 4; ++i)
        {
            s[i] = 0.0f;
            t[i] = 1.0f;
        }
        tf = 1.0f;
    }

    /** x: input (already driven); g = tan (pi fc / fs); resonance 0..1 (around 0.9..1.0 it
        self-oscillates, its level held by saturation);
        twoPole: 12 dB/oct response (Jupiter's 2-pole mode). */
    float process (float x, float g, float resonance, OtaCharacter character, bool twoPole) noexcept
    {
        const Calibration& cal = calibrations[character == OtaCharacter::jupiter ? 1 : 0];
        const float r = std::clamp (resonance, 0.0f, 1.0f);
        const float k = cal.maxFeedback * r * (cal.taperLinear + (1.0f - cal.taperLinear) * r);

        float y[4];
        float u = solve (x, g, k, cal.compensation, y);   // 1: the previous sample's gains
        for (int pass = 0; pass < refinements; ++pass)
        {
            updateGains (u, k, y, cal);
            u = solve (x, g, k, cal.compensation, y);
        }

        for (int i = 0; i < 4; ++i)
        {
            // Trapezoidal state update: s' = 2 y - s. Flushed when tiny, so a decaying tail
            // never reaches denormals (which cost tens of cycles each on x86).
            const float next = 2.0f * y[i] - s[i];
            s[i] = std::abs (next) < 1.0e-20f ? 0.0f : next;
        }
        return twoPole ? y[1] : y[3];
    }

    bool isQuiet() const noexcept
    {
        return std::abs (s[0]) + std::abs (s[1]) + std::abs (s[2]) + std::abs (s[3]) < 1.0e-7f;
    }

private:
    struct Calibration
    {
        float maxFeedback;       // k at resonance 1 (k = 4 is the self-oscillation threshold)
        float taperLinear;       // k = max * r * (a + (1 - a) r): how early the resonance comes in
        float compensation;      // pass-band restore: input gain 1 + compensation * k
        float hardness;          // h: the stages' curve tanh (h d) / h; larger bends sooner
        float feedbackHardness;  // hf: the resonance OTA's curve; sets the self-oscillation level
    };

    /*  The two calibrations: chosen by measuring this model (see tests/OtaFilterTests.cpp) to
        follow the published character of the instruments. None is a measured hardware
        constant; all are to be confirmed by ear against the instruments.

        JUNO (IR3109 / 80017A as in the Juno-6/60/106):
          maxFeedback 4.2: with the taper below, k reaches 4 at resonance ~0.97, so it rings by
            itself only at the very top of the slider, as a Juno does, with a modest excess
            (5 %) that keeps the ring round, quiet and close to fc.
          taperLinear 0.6: part-squared taper: the lower half of the slider adds gentle
            emphasis and the peak sharpens near the top (a Juno's resonance comes in late).
          compensation 0.25: gentle. At full resonance the pass band is (1 + 1.05) / 5.2,
            -8.1 dB (uncompensated: -14.3 dB): the Juno's audible bass loss, not a collapse.
          hardness 0.7: the stages bend later (|d| ~ 1.4), so normal levels stay clean and the
            saturation is smooth.
          feedbackHardness 1.0: the resonance current limits at the same scale as the ladder's
            junction (|u| < 1), so the ring settles at about the ladder's level (measured at
            resonance 1: 0.108 peak, 9 cents flat; LadderFilter at the synth's k = 4.15: 0.094).

        JUPITER (IR3109 as in the Jupiter-8):
          maxFeedback 4.6: rings by itself from resonance ~0.89 and with a stronger excess
            (15 %): the brighter, more aggressive Jupiter resonance.
          taperLinear 0.85: closer to linear, so the resonance comes in earlier on the slider and
            the peak is above the Juno's at every setting (k_jupiter > k_juno for every r > 0,
            and its compensation is larger too).
          compensation 0.4: at full resonance the pass band is (1 + 1.84) / 5.6, -5.9 dB:
            brighter and punchier with the resonance up.
          hardness 0.9: the stages bend sooner: slightly harder, more harmonics when driven.
          feedbackHardness 1.25: limits the stronger loop a little earlier, which keeps its ring
            closer to fc (measured at resonance 1: 26 cents flat, 0.139 peak; at 1.0 it was 40
            cents flat), a little louder than the Juno's. */
    static constexpr Calibration calibrations[2] = {
        { 4.2f, 0.6f, 0.25f, 0.7f, 1.0f },      // juno
        { 4.6f, 0.85f, 0.4f, 0.9f, 1.25f },     // jupiter
    };

    static constexpr int refinements = 1;   // solves after the warm start (see the class note): two solves per sample, chosen for the 16-voice budget

    /** The linear zero-delay solve with the curves' current effective gains t[], tf: fills
        the stages' outputs and returns u, OTA 1's input. One division: with d_i = 1 + g T_i,
        D = d_1 d_2 d_3 d_4 and N = (g T_1)(g T_2)(g T_3)(g T_4), the closed form above becomes
            y_4 = (N c x + B D) / E,   E = D + k T_f N   (B D a polynomial in the states)
        and 1 / (D E) yields both 1 / E and 1 / D, from which each 1 / d_i follows. */
    float solve (float x, float g, float k, float compensation, float* y) const noexcept
    {
        const float gt0 = g * t[0], gt1 = g * t[1], gt2 = g * t[2], gt3 = g * t[3];
        const float d0 = 1.0f + gt0, d1 = 1.0f + gt1, d2 = 1.0f + gt2, d3 = 1.0f + gt3;
        const float d01 = d0 * d1, d012 = d01 * d2, d23 = d2 * d3;
        const float D = d012 * d3;
        const float N = gt0 * gt1 * gt2 * gt3;
        const float BD = gt3 * (gt2 * (gt1 * s[0] + d0 * s[1]) + d01 * s[2]) + d012 * s[3];

        const float kt = k * tf;
        // The compensation follows the resonance current that flows (k T_f): when a loud
        // signal saturates the resonance OTA, the bass loss recedes and so does its make-up,
        // instead of turning into a boost of up to 1 + comp k.
        const float xc = x * (1.0f + compensation * kt);
        const float E = D + kt * N;
        const float inv = 1.0f / (D * E);
        const float y4 = (N * xc + BD) * D * inv;
        const float invD = E * inv;

        const float u = xc - kt * y4;
        y[0] = (gt0 * u + s[0]) * (d1 * d23 * invD);
        y[1] = (gt1 * y[0] + s[1]) * (d0 * d23 * invD);
        y[2] = (gt2 * y[1] + s[2]) * (d01 * d3 * invD);
        y[3] = (gt3 * y[2] + s[3]) * (d012 * invD);
        return u;
    }

    /** Re-evaluates the effective gains (curve (v) / v) at the inputs of the last solve. */
    void updateGains (float u, float k, const float* y, const Calibration& cal) noexcept
    {
        tf = secant (cal.feedbackHardness * k * y[3]);
        t[0] = secant (cal.hardness * (u - y[0]));
        t[1] = secant (cal.hardness * (y[0] - y[1]));
        t[2] = secant (cal.hardness * (y[1] - y[2]));
        t[3] = secant (cal.hardness * (y[2] - y[3]));
    }

    /** tanh (v) / v for math::tanhFast's [7/6] Pade form, in (0, 1], with one division:
        tanh's numerator is v times an even polynomial, so the v cancels (no 0 / 0 at v = 0).
        Beyond |v| = 4.97, where tanhFast holds at 1, the result is 1 / |v|: m and the 4.97
        on top make that one expression. */
    static float secant (float v) noexcept
    {
        constexpr float edge = 4.97178686f;
        const float a = std::abs (v);
        const float c = std::min (a, edge);
        const float c2 = c * c;
        const float num = 135135.0f + c2 * (17325.0f + c2 * (378.0f + c2));
        const float den = 135135.0f + c2 * (62370.0f + c2 * (3150.0f + c2 * 28.0f));
        const float m = std::max (a, edge);
        return (num * edge) / (den * m);
    }

    float s[4] { 0.0f, 0.0f, 0.0f, 0.0f };   // trapezoidal integrator states
    float t[4] { 1.0f, 1.0f, 1.0f, 1.0f };   // the stages' effective gains (the warm start)
    float tf = 1.0f;                         // the resonance OTA's effective gain
};

} // namespace andor::analog
