// STEREO SPREAD (HYPER, DIMENSION, BOTH), PHASER, FLANGER.
#include "Effects.h"

namespace andor::analog::fx
{

using namespace dsp;

namespace
{
    constexpr std::array<int, 4> hyperVoices { 2, 4, 6, 8 };
    constexpr double hyperWindowSeconds = 0.035, hyperMinDelay = 0.002;
    constexpr double twoPi = 2.0 * pi;

    double wrap (double x) noexcept { return x - std::floor (x); }
}

//==============================================================================
// STEREO SPREAD.
// HYPER: each voice is a real pitch shift (two read heads sweeping a 35 ms window, crossfaded
// by sin^2 / cos^2 so their gains always sum to one) - a sustained detune, not a fixed delay's
// phase. The voices spread from -DETUNE to +DETUNE cents, wander a little at HYPER RATE, and
// alternate sides. DIMENSION: four short modulated taps (5 - 15 ms, staggered phases), each
// side's ensemble from the other side's line. Then WIDTH (M / S) and LOW MONO (LR4 below the
// corner summed to mono).

void StereoSpread::prepareEffect()
{
    scopeOn = true;   // (its rack draws a goniometer)
    for (auto& l : hyperLine)
        l.prepare (static_cast<int> ((hyperWindowSeconds + hyperMinDelay + 0.01) * rate));
    for (auto& l : dimLine)
        l.prepare (static_cast<int> (0.03 * rate));
    widthGlide.prepare (rate, 0.03);
}

void StereoSpread::resetEffect() noexcept
{
    for (auto* set : { &hyperLine, &dimLine })
        for (auto& l : *set)
            l.reset();
    for (std::size_t v = 0; v < rampPhase.size(); ++v)
    {
        rampPhase[v] = 0.37 * static_cast<double> (v);   // (voices start apart: no common comb)
        lfoPhase[v] = 0.13 * static_cast<double> (v);
    }
    dimPhase = { 0.0, 0.25, 0.5, 0.75 };
    for (auto& c : lowCross)
        c.reset();
    widthGlide.jump (p.width);
    sumLL = sumRR = sumLR = sumM = sumS = 0.0;
}

void StereoSpread::set (const Settings& s, int slot) noexcept
{
    p = s.spread[static_cast<std::size_t> (slot)];
    setCommon (p);
    widthGlide.set (std::clamp (p.width, 0.0f, 2.0f));
    if (p.lowMonoHz != lastLowHz)
    {
        lastLowHz = p.lowMonoHz;
        for (auto& c : lowCross)
            c.design (rate, std::clamp (static_cast<double> (p.lowMonoHz), 50.0, 400.0));
    }
}

void StereoSpread::processWet (float* left, float* right, int n) noexcept
{
    const bool hyper = p.mode != SpreadSettings::dimension, dimension = p.mode != SpreadSettings::hyper;
    const int voices = hyperVoices[static_cast<std::size_t> (std::clamp (p.voices, 0, 3))];
    const double window = hyperWindowSeconds * rate, minDelay = hyperMinDelay * rate;
    const double detune = std::clamp (static_cast<double> (p.detuneCents), 0.0, 60.0);
    const double wanderStep = std::clamp (static_cast<double> (p.hyperRateHz), 0.03, 5.0) / rate;
    const double dimBase = (0.005 + 0.010 * std::clamp (static_cast<double> (p.dimSize), 0.0, 1.0)) * rate;
    const double dimDepth = (0.0004 + 0.0012 * std::clamp (static_cast<double> (p.dimSize), 0.0, 1.0)) * rate;
    const double dimStep = std::clamp (static_cast<double> (p.dimRateHz), 0.03, 3.0) / rate;
    const float dimAmount = std::clamp (p.dimAmount, 0.0f, 1.0f);
    const float voiceNorm = 1.0f / std::sqrt (0.5f * static_cast<float> (voices));
    for (int i = 0; i < n; ++i)
    {
        float l = left[i], r = right[i];
        if (hyper)
        {
            hyperLine[0].push (l);
            hyperLine[1].push (r);
            float hl = 0.0f, hr = 0.0f;
            for (int v = 0; v < voices; ++v)
            {
                const auto k = static_cast<std::size_t> (v);
                const double spread = voices > 1 ? 2.0 * v / (voices - 1) - 1.0 : 0.0;
                lfoPhase[k] = wrap (lfoPhase[k] + wanderStep * (1.0 + 0.11 * v));
                const double cents = detune * (spread + 0.1 * std::sin (twoPi * lfoPhase[k]));
                const double ratio = std::exp2 (cents / 1200.0);
                rampPhase[k] = wrap (rampPhase[k] + (1.0 - ratio) / window);
                const double a = rampPhase[k], b = wrap (a + 0.5);
                const float ga = static_cast<float> (std::sin (pi * a)), gb = static_cast<float> (std::sin (pi * b));
                const auto& line = hyperLine[k % 2];
                const float y = ga * ga * line.read (static_cast<float> (minDelay + a * window)) + gb * gb * line.read (static_cast<float> (minDelay + b * window));
                // Alternate sides, the outer voices wider.
                const float side = (v % 2 == 0 ? -1.0f : 1.0f) * (0.35f + 0.65f * static_cast<float> (std::abs (spread)));
                float gl, gr;
                balance (side, gl, gr);
                hl += gl * y;
                hr += gr * y;
            }
            l = voiceNorm * hl;
            r = voiceNorm * hr;
        }
        if (dimension)
        {
            dimLine[0].push (l);
            dimLine[1].push (r);
            float taps[4];
            for (std::size_t t = 0; t < 4; ++t)
            {
                dimPhase[t] = wrap (dimPhase[t] + dimStep * (1.0 + 0.07 * static_cast<double> (t)));
                const double d = dimBase + dimDepth * std::sin (twoPi * dimPhase[t]);
                taps[t] = dimLine[t % 2].read (static_cast<float> (d));
            }
            // Each side's ensemble from the other side's line (the spread), the dry kept.
            const float el = 0.5f * (taps[1] + taps[3]), er = 0.5f * (taps[0] + taps[2]);
            l = l * (1.0f - 0.35f * dimAmount) + dimAmount * el;
            r = r * (1.0f - 0.35f * dimAmount) + dimAmount * er;
        }
        // WIDTH (M / S), then LOW MONO.
        const float w = widthGlide.next();
        if (w != 1.0f)
        {
            const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
            l = m + s;
            r = m - s;
        }
        if (p.lowMono)
        {
            float lowL, highL, lowR, highR;
            lowCross[0].split (l, lowL, highL);
            lowCross[1].split (r, lowR, highR);
            const float lowMono = 0.5f * (lowL + lowR);
            l = highL + lowMono;
            r = highR + lowMono;
        }
        left[i] = l;
        right[i] = r;
        sumLL += static_cast<double> (l) * l;
        sumRR += static_cast<double> (r) * r;
        sumLR += static_cast<double> (l) * r;
        sumM += 0.25 * static_cast<double> (l + r) * (l + r);
        sumS += 0.25 * static_cast<double> (l - r) * (l - r);
    }
    const double d = std::sqrt (sumLL * sumRR);
    if (d > 1.0e-12)
        correlation.store (static_cast<float> (sumLR / d), std::memory_order_relaxed);
    midLevel.store (static_cast<float> (std::sqrt (sumM / std::max (1, n))), std::memory_order_relaxed);
    sideLevel.store (static_cast<float> (std::sqrt (sumS / std::max (1, n))), std::memory_order_relaxed);
    for (auto* v : { &sumLL, &sumRR, &sumLR, &sumM, &sumS })
        *v *= 0.9;
}

//==============================================================================
// PHASER: first-order all-pass stages swept together (the notch positions follow the
// coefficient exactly), feedback bounded, the right channel's sweep offset by STEREO.

void Phaser::prepareEffect() {}

void Phaser::resetEffect() noexcept
{
    phase = 0.0;
    state = {};
    feedback = {};
}

void Phaser::set (const Settings& s, int slot) noexcept
{
    p = s.phaser[static_cast<std::size_t> (slot)];
    bpm = s.bpm;
    setCommon (p);
}

void Phaser::processWet (float* left, float* right, int n) noexcept
{
    const bool deep = p.mode == PhaserSettings::deep;
    int stages = stageCounts[static_cast<std::size_t> (std::clamp (p.stages, 0, 4))];
    if (deep)
        stages = std::min (12, stages + 4);
    const double hz = p.sync ? syncedHz (bpm, p.division) : std::clamp (static_cast<double> (p.rateHz), 0.03, 10.0);
    const double octaves = (deep ? 4.0 : 3.0) * std::clamp (static_cast<double> (p.depth), 0.0, 1.0);
    const float fb = std::clamp (p.feedback * (deep ? 1.1f : 1.0f), -0.85f, 0.85f);
    const double offset = std::clamp (static_cast<double> (p.stereoDegrees), 0.0, 180.0) / 360.0;
    const double centre = std::clamp (static_cast<double> (p.centreHz), 80.0, 8000.0);
    double coefNow = 0.0;
    for (int i = 0; i < n; ++i)
    {
        phase = wrap (phase + hz / rate);
        float io[2] { left[i], right[i] };
        for (int c = 0; c < 2; ++c)
        {
            const double ph = wrap (phase + (c == 1 ? offset : 0.0));
            const double f = std::clamp (centre * std::exp2 (octaves * std::sin (twoPi * ph)), 30.0, 0.45 * rate);
            const double t = std::tan (pi * f / rate);
            const float a = static_cast<float> ((t - 1.0) / (t + 1.0));
            if (c == 0)
                coefNow = f;
            float x = io[c] + fb * std::tanh (feedback[static_cast<std::size_t> (c)]);
            auto& st = state[static_cast<std::size_t> (c)];
            for (int s = 0; s < stages; ++s)
            {
                const float y = a * x + st[static_cast<std::size_t> (s)];
                st[static_cast<std::size_t> (s)] = flushDenormal (x - a * y);
                x = y;
            }
            feedback[static_cast<std::size_t> (c)] = x;
            io[c] = x;
        }
        left[i] = io[0];
        right[i] = io[1];
    }
    // Notches (where the stages' phase sums to an odd multiple of pi) for the graph.
    for (std::size_t k = 0; k < notchHz.size(); ++k)
    {
        const int idx = static_cast<int> (k) + 1;
        const double f = 2 * idx - 1 <= stages ? coefNow * std::tan (pi * (2.0 * idx - 1.0) / (2.0 * stages)) : 0.0;
        notchHz[k].store (static_cast<float> (f > 0.0 && f < 0.5 * rate ? f : 0.0), std::memory_order_relaxed);
    }
}

//==============================================================================
// FLANGER: a Hermite-interpolated delay swept by RATE, feedback clamped with a DC blocker in
// the loop. THROUGH-ZERO: the dry is held 6 ms back (reported latency) so the swept delay can
// pass through it - the wet goes "before" the dry by delaying the dry, not by reversing time.

namespace
{
    constexpr double throughZeroMs = 6.0;
}

void Flanger::prepareEffect()
{
    for (auto& l : line)
        l.prepare (static_cast<int> (0.03 * rate));
    // (the dry path sized for THROUGH-ZERO; set() says which mode reports it)
    setLatency (static_cast<int> (throughZeroMs * 0.001 * rate));
    setLatencyCeiling (static_cast<int> (throughZeroMs * 0.001 * rate));   // (CLASSIC is padded to it in a route)
    throughZero = true;   // (so the first set() in CLASSIC mode drops it to 0)
}

void Flanger::resetEffect() noexcept
{
    for (auto& l : line)
        l.reset();
    fb = dcIn = dcOut = {};
    phase = 0.0;
}

void Flanger::set (const Settings& s, int slot) noexcept
{
    p = s.flanger[static_cast<std::size_t> (slot)];
    bpm = s.bpm;
    setCommon (p);
    const bool tz = p.mode == FlangerSettings::throughZero;
    if (tz != throughZero)
    {
        throughZero = tz;
        setLatency (tz ? static_cast<int> (throughZeroMs * 0.001 * rate) : 0);
    }
}

void Flanger::processWet (float* left, float* right, int n) noexcept
{
    const bool tz = p.mode == FlangerSettings::throughZero;
    const double hz = p.sync ? syncedHz (bpm, p.division) : std::clamp (static_cast<double> (p.rateHz), 0.03, 10.0);
    const double base = std::clamp (static_cast<double> (p.delayMs), 0.2, 12.0) * 0.001 * rate;
    const double depth = std::clamp (static_cast<double> (p.depth), 0.0, 1.0);
    const float feedback = std::clamp (p.feedback, -0.92f, 0.92f);
    const double offset = std::clamp (static_cast<double> (p.stereoDegrees), 0.0, 180.0) / 360.0;
    const float width = std::clamp (p.width, 0.0f, 1.0f);
    const double tzDelay = throughZeroMs * 0.001 * rate;
    const float dcCoef = static_cast<float> (1.0 - twoPi * 10.0 / rate);
    double shown = 0.0;
    for (int i = 0; i < n; ++i)
    {
        phase = wrap (phase + hz / rate);
        float io[2] { left[i], right[i] };
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            const double lfo = std::sin (twoPi * wrap (phase + (c == 1 ? offset : 0.0)));
            double d = tz ? tzDelay + (base + 0.9 * depth * tzDelay) * lfo * 0.5 : base * (1.0 + 0.95 * depth * lfo);
            d = std::clamp (d, 1.0, 0.028 * rate);
            if (c == 0)
                shown = d / rate * 1000.0;
            auto& l = line[static_cast<std::size_t> (c)];
            const float y = l.read (static_cast<float> (d));
            // DC blocker on what is fed back; a soft limit so feedback can never run away.
            auto& in = dcIn[static_cast<std::size_t> (c)];
            auto& o = dcOut[static_cast<std::size_t> (c)];
            const float blocked = y - in + dcCoef * o;
            in = y;
            o = flushDenormal (blocked);
            l.push (io[c] + feedback * std::tanh (blocked));
            out[c] = y;
        }
        // WIDTH: 0 both sides the mid of the two sweeps, 1 each its own.
        const float m = 0.5f * (out[0] + out[1]);
        left[i] = m + width * (out[0] - m);
        right[i] = m + width * (out[1] - m);
    }
    delayNowMs.store (static_cast<float> (shown), std::memory_order_relaxed);
}

} // namespace andor::analog::fx
