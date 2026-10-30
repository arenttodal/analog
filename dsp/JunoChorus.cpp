#include "JunoChorus.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /** tanh by its [3/2] Padé form, exact at the clamp (+/-3 -> +/-1) so it is continuous
        there. A polynomial, so every platform computes the same samples. */
    double softSaturate (double x) noexcept
    {
        const double z = std::clamp (x * JunoChorus::drive, -3.0, 3.0);
        return z * (27.0 + z * z) / (27.0 + 9.0 * z * z) / JunoChorus::drive;
    }

    /** The LFO's triangle, -1..1, rising through zero at phase 0. */
    double triangle (double phase) noexcept
    {
        if (phase < 0.25)
            return 4.0 * phase;
        if (phase < 0.75)
            return 2.0 - 4.0 * phase;
        return 4.0 * phase - 4.0;
    }

    /** sin (pi/2 t) for t in -1..1 (folding the triangle gives the sine at the same phase).
        A Taylor polynomial to x^9 (error < 4e-6) rather than std::sin, whose last bits
        differ between maths libraries: DAW bounces must match across platforms. */
    double foldedSine (double t) noexcept
    {
        const double x = 0.5 * pi * t;
        const double x2 = x * x;
        return x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0 * (1.0 - x2 / 72.0))));
    }
}

JunoChorus::ModeSpec JunoChorus::spec (int mode) noexcept
{
    switch (mode)
    {
        case one:  return { 0.513, 3.5, 1.85, false };
        case two:  return { 0.863, 3.5, 1.85, false };
        case both: return { 8.3, 3.5, 0.2, true };
        default:   return {};
    }
}

void JunoChorus::Biquad::setLowPass (double cutoffHz, double sampleRate) noexcept
{
    // Butterworth (Q = 1/sqrt 2) by the bilinear transform (RBJ's cookbook form).
    const double w = 2.0 * pi * cutoffHz / sampleRate;
    const double alpha = std::sin (w) / (2.0 * 0.70710678118654752);
    const double c = std::cos (w);
    const double a0 = 1.0 + alpha;
    b0 = 0.5 * (1.0 - c) / a0;
    b1 = (1.0 - c) / a0;
    b2 = b0;
    a1 = -2.0 * c / a0;
    a2 = (1.0 - alpha) / a0;
}

void JunoChorus::prepare (double sampleRate, int /*maximumBlockSize*/)
{
    // Every sample is processed on its own (no scratch buffers), so any block size works.
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;

    double longestMs = 0.0;
    for (int m = one; m <= both; ++m)
        longestMs = std::max (longestMs, spec (m).centreMs + spec (m).depthMs);
    // + 4: the cubic interpolator reads one sample past the integer delay on each side.
    const auto needed = static_cast<std::uint32_t> (std::ceil (longestMs * 0.001 * rate)) + 4;
    std::uint32_t size = 1;
    while (size < needed)
        size <<= 1;
    line.assign (size, 0.0f);
    mask = size - 1;

    const double cutoff = std::min (filterHz, 0.45 * rate);
    pre.setLowPass (cutoff, rate);
    for (auto& s : slots)
    {
        s.postLeft.setLowPass (cutoff, rate);
        s.postRight.setLowPass (cutoff, rate);
    }

    fadeLength = std::max (1, static_cast<int> (std::lround (fadeSeconds * rate)));

    // Uniform noise in [-1, 1) has a variance of 1/3. A 2nd-order Butterworth low-pass
    // passes an equivalent noise bandwidth of pi / (2 sqrt 2) x the cutoff, so the share
    // of white noise power that survives is that over Nyquist. Scaling by it keeps the
    // hiss at the same level at every sample rate.
    const double enbw = pi / (2.0 * std::sqrt (2.0)) * cutoff;
    const double filteredRms = std::sqrt ((1.0 / 3.0) * enbw / (0.5 * rate));
    noiseAmplitude = std::pow (10.0, hissDbfs / 20.0) / (wetGain * filteredRms);

    reset();
}

void JunoChorus::clearLine() noexcept
{
    std::fill (line.begin(), line.end(), 0.0f);
    write = 0;
    pre.clear();
}

void JunoChorus::reset() noexcept
{
    clearLine();
    for (auto& s : slots)
    {
        s.mode = off;
        s.phase = 0.0;
        s.postLeft.clear();
        s.postRight.clear();
    }
    current = 0;
    slots[0].mode = target;
    fading = false;
    fadePos = 0;
    prng.reseed (osp::Prng::deriveSeed (noiseSeed, 0x4242440aull));
}

void JunoChorus::setMode (int mode) noexcept
{
    // Only recorded here: process() starts the fade at the next sample, or once a fade
    // already under way has finished (cutting a fade short would click).
    target = std::clamp (mode, static_cast<int> (off), static_cast<int> (both));
}

void JunoChorus::setNoise (bool on, std::uint32_t seed) noexcept
{
    noiseOn = on;
    if (seed != noiseSeed)
    {
        noiseSeed = seed;
        prng.reseed (osp::Prng::deriveSeed (noiseSeed, 0x4242440aull));
    }
}

bool JunoChorus::isActive() const noexcept
{
    return fading || slots[current].mode != off || target != off;
}

double JunoChorus::readTap (double delaySamples) const noexcept
{
    // Catmull-Rom (4-point cubic Hermite) between the samples at delays i and i + 1.
    const auto i = static_cast<std::uint32_t> (delaySamples);
    const double f = delaySamples - static_cast<double> (i);
    const std::uint32_t at = write - i;
    const double xm1 = line[(at + 1) & mask];
    const double x0 = line[at & mask];
    const double x1 = line[(at - 1) & mask];
    const double x2 = line[(at - 2) & mask];
    const double c1 = 0.5 * (x1 - xm1);
    const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

double JunoChorus::hiss() noexcept
{
    return noiseAmplitude * prng.bipolar();
}

void JunoChorus::process (float* left, float* right, int numSamples) noexcept
{
    if (line.empty())
        return;

    const double msToSamples = 0.001 * rate;

    for (int n = 0; n < numSamples; ++n)
    {
        if (! fading && target != slots[current].mode)
        {
            const int old = current;
            current = 1 - current;
            auto& s = slots[current];
            s.mode = target;
            // The hardware's LFO keeps running through a mode change: carry its phase over.
            s.phase = slots[old].phase;
            s.postLeft.clear();
            s.postRight.clear();
            // Engaging from silence: whatever the line held when the chorus was switched
            // off is stale, start it empty.
            if (slots[old].mode == off)
                clearLine();
            fading = true;
            fadePos = 0;
        }

        if (! fading && slots[current].mode == off)
            return;   // the bypass: this and the remaining samples stay untouched

        const double dryLeft = left[n], dryRight = right[n];
        line[write & mask] = static_cast<float> (pre.process (softSaturate (0.5 * (dryLeft + dryRight))));

        // Linear crossfade; it ends on exactly 1 and 0.
        const double gainNew = fading ? static_cast<double> (fadePos + 1) / fadeLength : 1.0;
        double wetLeft = 0.0, wetRight = 0.0, presence = 0.0;
        for (int k = 0; k < 2; ++k)
        {
            const bool isNew = k == current;
            if (! isNew && ! fading)
                continue;
            auto& s = slots[k];
            if (s.mode == off)
                continue;
            const double gain = isNew ? gainNew : 1.0 - gainNew;
            const auto sp = spec (s.mode);
            const double tri = triangle (s.phase);
            const double lfo = sp.sine ? foldedSine (tri) : tri;
            s.phase += sp.rateHz / rate;
            if (s.phase >= 1.0)
                s.phase -= 1.0;
            double tapLeft = readTap ((sp.centreMs + sp.depthMs * lfo) * msToSamples);
            double tapRight = readTap ((sp.centreMs - sp.depthMs * lfo) * msToSamples);
            if (noiseOn)
            {
                tapLeft += hiss();
                tapRight += hiss();
            }
            wetLeft += gain * s.postLeft.process (tapLeft);
            wetRight += gain * s.postRight.process (tapRight);
            presence += gain;
        }

        const double dry = 1.0 - (1.0 - dryGain) * presence;
        left[n] = static_cast<float> (dryLeft * dry + wetGain * wetLeft);
        right[n] = static_cast<float> (dryRight * dry + wetGain * wetRight);

        write = (write + 1) & mask;
        if (fading && ++fadePos >= fadeLength)
            fading = false;
    }
}

} // namespace andor::analog
