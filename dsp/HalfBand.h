#pragma once

#include <vector>

namespace andor::analog
{

/**
    A linear-phase half-band FIR decimator by two (Kaiser-windowed sinc). Every other tap
    of a half-band filter is zero, so only the odd taps and the centre tap are multiplied.
    design() allocates; process() is real-time safe.

    `transition` is the transition band's width as a fraction of the input rate (the pass
    band ends at 0.25 - transition / 2), `attenuationDb` the stop-band rejection.
*/
class HalfBandDecimator
{
public:
    void design (double transition, double attenuationDb);
    void reset() noexcept;

    /** Two input samples in, one out. */
    float process (float a, float b) noexcept;

    /** Group delay in input samples. */
    int latency() const noexcept { return (taps - 1) / 2; }
    int length() const noexcept { return taps; }
    /** The impulse response (all taps, for tests). */
    std::vector<double> impulseResponse() const;

private:
    int taps = 3;
    std::vector<float> odd;       ///< the non-zero, non-centre taps (symmetric, half stored)
    float centre = 0.5f;
    std::vector<float> history;   ///< 2 x taps, written twice so reads never wrap
    int write = 0;
};

} // namespace andor::analog
