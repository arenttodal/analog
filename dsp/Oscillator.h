#pragma once

#include "Patch.h"
#include "PolyBlep.h"

#include <algorithm>
#include <array>
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

//==============================================================================
// State version 4: the slot models. CLASSIC is the code above, untouched; DCO and WIDE reuse
// the same B-spline BLEP/BLAMP corrections (one BlepLine per slot: corrections are linear, so
// a DCO's sub and a SUPER SAW's seven saws share it).

/** WIDE's waves as the walker's sums (SQUARE is a pulse held at 50 %; SUPER SAW: saw). */
inline Waves wavesOf (WideWave w) noexcept
{
    Waves s;
    s.saw = w == WideWave::saw || w == WideWave::superSaw;
    s.pulse = w == WideWave::pulse || w == WideWave::square;
    s.triangle = w == WideWave::triangle;
    return s;
}

/** A DCO's saw and pulse (either, both or neither: the sub may sound alone). */
inline Waves wavesOf (const DcoSettings& d) noexcept
{
    Waves s;
    s.saw = d.saw;
    s.pulse = d.pulse;
    s.gain = d.saw && d.pulse ? 0.70710678f : 1.0f;
    return s;
}

/** SUPER SAW: seven saws, symmetric about the centre one (offsets in units of the spread's
    detune; the centre saw is the slot's own phase, so sync and Poly Mod still act on it). */
constexpr int superSawSides = 6;
constexpr std::array<double, superSawSides> superSawOffsets { -1.0, -0.62, -0.29, 0.29, 0.62, 1.0 };

/** The outermost saws' detune in cents at SPREAD s (0..1): fine near 0, wide at the top; 0
    exactly at 0 (one centred saw). An original law (not a copy of any instrument's curve). */
inline double superSawCents (double s) noexcept
{
    s = std::clamp (s, 0.0, 1.0);
    return 5.0 * s + 55.0 * s * s;
}

/** The side saws' level against the centre's (0 at SPREAD 0: exactly one saw), and the gain
    that keeps the sum's loudness steady as they come in (incoherent sum: 1 / sqrt(1 + 6 a^2)). */
inline float superSawSideLevel (double s) noexcept { return static_cast<float> (0.75 * std::sqrt (std::clamp (s, 0.0, 1.0))); }
inline float superSawNormal (float side) noexcept { return 1.0f / std::sqrt (1.0f + static_cast<float> (superSawSides) * side * side); }

/** Per-voice state a slot's models need beyond its phase and BLEP line (preallocated). */
struct SlotExtra
{
    float subSign = 1.0f;                              ///< DCO: the divide-by-two flip-flop
    std::array<double, superSawSides> sides {};        ///< SUPER SAW: the side saws' phases
    std::array<double, superSawSides> sideRatio {};    ///< their frequency ratios to the centre
    float sideLevel = 0.0f, sawNormal = 1.0f;
    float tilt = 0.0f, tiltCoef = 1.0f;                ///< WIDE SHAPE: the pitch-tracking edge filter
};

/** What a slot plays this sample: its model's sums and extras (from the control rate). */
struct SlotPlan
{
    OscModel model = OscModel::classic;
    Waves waves;
    float subLevel = 0.0f;      ///< DCO
    bool superSaw = false;      ///< WIDE
    bool tilt = false;          ///< WIDE SHAPE below raw
    bool squareWidth = false;   ///< WIDE SQUARE: the width held at 50 %
};

/**
    One sample of a DCO or WIDE slot (CLASSIC never comes here). `syncAt` >= 0: the master
    wrapped that long before the sample (hard sync). Returns the slot's output (after its BLEP
    line, two samples late like CLASSIC); `wrapAt` gets its own wrap's time (or stays).
*/
inline float renderModel (double& phase, BlepLine& line, SlotExtra& x, const SlotPlan& plan, double dt, float width, double syncAt,
                          double& wrapAt) noexcept
{
    if (plan.squareWidth)
        width = 0.5f;
    const auto& w = plan.waves;
    const float sawGain = (plan.superSaw ? x.sawNormal : 1.0f) * w.gain;
    // The centre (or only) oscillator, with sync as CLASSIC does it.
    double ownWrap = -1.0;
    Waves centre = w;
    centre.gain = plan.superSaw ? sawGain : w.gain;
    if (syncAt >= 0.0)
    {
        const double before = phase;
        const double atReset = walk (before, dt, width, centre, 1.0 - syncAt, syncAt, line, ownWrap);
        syncReset (atReset, dt, width, centre, static_cast<float> (syncAt), line);
        phase = walk (0.0, dt, width, centre, syncAt, 0.0, line, ownWrap);
    }
    else
        phase = walk (phase, dt, width, centre, 1.0, 0.0, line, ownWrap);
    float naiveValue = naive (centre, phase, width);

    // DCO: the sub flips at every cycle start of its oscillator (a wrap or a sync reset): a
    // true divide-by-two, its edges corrected like any other.
    if (plan.subLevel > 0.0f)
    {
        const double flipAt = syncAt >= 0.0 ? syncAt : ownWrap;
        if (flipAt >= 0.0)
        {
            line.step (-2.0f * x.subSign * plan.subLevel, static_cast<float> (flipAt));
            x.subSign = -x.subSign;
        }
        naiveValue += x.subSign * plan.subLevel;
    }
    else if (ownWrap >= 0.0 || syncAt >= 0.0)
        x.subSign = -x.subSign;   // (kept turning while silent: switching the sub on never clicks out of phase)

    // SUPER SAW: six more saws, each its own phase, sharing the line; a sync restarts them all.
    if (plan.superSaw && x.sideLevel > 0.0f)
    {
        const float g = x.sideLevel * sawGain;
        for (int i = 0; i < superSawSides; ++i)
        {
            auto& p = x.sides[static_cast<std::size_t> (i)];
            const double sdt = std::min (0.45, dt * x.sideRatio[static_cast<std::size_t> (i)]);
            if (syncAt >= 0.0)
            {
                double atReset = p + sdt * (1.0 - syncAt);
                if (atReset >= 1.0)
                {
                    const double past = (atReset - 1.0) / sdt;   // wrapped before the reset
                    line.step (-2.0f * g, static_cast<float> (syncAt + past));
                    atReset -= 1.0;
                }
                line.step (g * static_cast<float> (-2.0 * atReset), static_cast<float> (syncAt));   // back to -1
                p = sdt * syncAt;
            }
            else
            {
                p += sdt;
                if (p >= 1.0)
                {
                    p -= 1.0;
                    line.step (-2.0f * g, static_cast<float> (p / sdt));
                }
            }
            naiveValue += g * static_cast<float> (2.0 * p - 1.0);
        }
    }
    if (ownWrap >= 0.0)
        wrapAt = ownWrap;
    float out = line.end (naiveValue);
    if (plan.tilt)
    {
        x.tilt += x.tiltCoef * (out - x.tilt);
        out = x.tilt;
    }
    return out;
}

} // namespace andor::analog::osc
