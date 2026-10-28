#pragma once

#include "Patch.h"
#include "AnalogMath.h"

#include "core/Prng.h"

#include <cmath>

namespace andor::analog
{

/**
    The instrument's LFO (one for every voice, as on the original): SINE, TRIANGLE, RAMP,
    SQUARE and SAMPLE & HOLD, -1..1. Free-running at its rate, or locked to the host's beat
    position while the transport plays (SYNC). Sample & hold draws from a seeded generator,
    so a render repeats exactly. Advanced at control rate. Real-time safe.
*/
class Lfo
{
public:
    void reset (std::uint64_t seed) noexcept
    {
        phase = 0.0;
        rng.reseed (osp::Prng::deriveSeed (seed, 0x4c464fu));
        held = static_cast<float> (rng.bipolar());
    }

    /** Moves on by `seconds` at `rateHz` (free) and returns the value. */
    float advance (LfoWave wave, double rateHz, double seconds) noexcept
    {
        setPhase (wave, phase + rateHz * seconds);
        return value (wave);
    }

    /** Locks to the host's position (quarter notes since the song start) for a division. */
    float lockTo (LfoWave wave, double ppqPosition, double divisionQuarters) noexcept
    {
        const double cycles = ppqPosition / divisionQuarters;
        const double next = cycles - std::floor (cycles);
        // Wrapped (or the host jumped back): a new cycle.
        setPhase (wave, next < phase ? next + 1.0 : next);
        return value (wave);
    }

    float value (LfoWave wave) const noexcept
    {
        const double p = phase;
        switch (wave)
        {
            case LfoWave::sine: return static_cast<float> (std::sin (2.0 * math::pi * p));
            case LfoWave::triangle: return static_cast<float> (p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0));
            case LfoWave::ramp: return static_cast<float> (2.0 * p - 1.0);
            case LfoWave::square: return p < 0.5 ? 1.0f : -1.0f;
            case LfoWave::sampleHold: return held;
        }
        return 0.0f;
    }

    double currentPhase() const noexcept { return phase; }

private:
    void setPhase (LfoWave, double next) noexcept
    {
        // Every new cycle draws the next held value, whatever the wave, so the sequence
        // does not depend on which waves were chosen along the way.
        const double whole = std::floor (next);
        if (whole >= 1.0)
            held = static_cast<float> (rng.bipolar());
        phase = next - whole;
    }

    double phase = 0.0;
    float held = 0.0f;
    osp::Prng rng { 1 };
};

} // namespace andor::analog
