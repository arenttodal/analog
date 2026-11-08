// COMPRESSOR: CLEAN (feed-forward, soft knee), FET 76 (a feedback design inspired by the 1176 -
// the detector hears the compressed signal; input drive against a fixed reference), MULTIBAND
// (three LR4 bands, each with one stereo-linked detector, downward above and upward below its
// threshold, a deadband between, OTT-capable).
#include "Effects.h"

namespace andor::analog::fx
{

using namespace dsp;

namespace
{
    float coefficient (double rate, double seconds) noexcept
    {
        return static_cast<float> (std::exp (-1.0 / (std::max (1.0e-6, seconds) * rate)));
    }

    constexpr float fetReferenceDb = -18.0f;          // the FET's internal threshold (its INPUT drives into it)
    constexpr std::array<float, 5> fetRatios { 4.0f, 8.0f, 12.0f, 20.0f, 40.0f };
    constexpr float maxUpwardDb = 18.0f, floorDb = -70.0f;
}

float Compressor::cleanCurveDb (const CompSettings& s, float in) noexcept
{
    const float over = in - s.thresholdDb, knee = std::max (0.0f, s.kneeDb), slope = 1.0f / std::max (1.0f, s.ratio) - 1.0f;
    float gr = 0.0f;
    if (2.0f * over <= -knee)
        gr = 0.0f;
    else if (2.0f * std::abs (over) < knee)
        gr = slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
    else
        gr = slope * over;
    return in + gr;
}

float Compressor::bandGainDb (const CompSettings& s, int band, float level) noexcept
{
    const float threshold = s.mbThresholdDb + s.bandOffsetDb[static_cast<std::size_t> (band)];
    const float half = 0.5f * std::max (0.0f, s.deadbandDb), depth = std::clamp (s.depth, 0.0f, 1.0f);
    float g = 0.0f;
    if (level > threshold + half)
        g = -(1.0f - 1.0f / std::max (1.0f, s.mbDownRatio)) * (level - threshold - half);
    else if (level < threshold - half)
    {
        // Upward: raised towards the threshold, capped, and not at all below the noise floor
        // (digital silence and hiss are never pumped up).
        const float boost = std::min (maxUpwardDb, (1.0f - 1.0f / std::max (1.0f, s.mbUpRatio)) * (threshold - half - level));
        const float floorFade = std::clamp ((level - floorDb) / 10.0f, 0.0f, 1.0f);
        g = boost * floorFade;
    }
    return depth * g;
}

void Compressor::prepareEffect() { lastSidechain = lastLow = lastHigh = -1.0f; }

void Compressor::resetEffect() noexcept
{
    envL = envR = grSmooth = 0.0f;
    for (auto& f : sidechain)
        f.reset();
    fetGain = 1.0f;
    fetEnv = 0.0f;
    fetGainLR = { 1.0f, 1.0f };
    fetEnvLR = {};
    for (auto* set : { &lowSplit, &highSplit, &lowPhase })
        for (auto& c : *set)
            c.reset();
    bandEnv = {};
    bandGain = {};
    inEnergy = outEnergy = 0.0;
    match = 1.0f;
}

void Compressor::set (const Settings& s, int slot) noexcept
{
    p = s.comp[static_cast<std::size_t> (slot)];
    setCommon (p);
    if (p.mode != mode)
    {
        mode = p.mode;
        reset();   // (each engine starts clean; their settings stay their own)
    }
    if (p.sidechainHz != lastSidechain)
    {
        lastSidechain = p.sidechainHz;
        for (auto& f : sidechain)
            f.design (Biquad::Shape::highPass, rate, std::clamp (static_cast<double> (p.sidechainHz), 20.0, 400.0), 0.70710678, 0.0);
    }
    const float low = std::clamp (p.lowHz, 50.0f, 800.0f), high = std::clamp (std::max (p.highHz, low * 1.5f), 800.0f, 10000.0f);
    if (low != lastLow || high != lastHigh)
    {
        lastLow = low;
        lastHigh = high;
        for (int c = 0; c < 2; ++c)
        {
            lowSplit[static_cast<std::size_t> (c)].design (rate, low);
            highSplit[static_cast<std::size_t> (c)].design (rate, high);
            lowPhase[static_cast<std::size_t> (c)].design (rate, high);
        }
    }
}

void Compressor::processWet (float* left, float* right, int n) noexcept
{
    // The input's peak level (dB), held and falling 12 dB/s: the graph's operating point.
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    const float peakDb = peak > 1.0e-6f ? 20.0f * std::log10 (peak) : -120.0f;
    const float held = inputDb.load (std::memory_order_relaxed);
    inputDb.store (peakDb > held ? peakDb : held - 12.0f * static_cast<float> (n) / static_cast<float> (rate), std::memory_order_relaxed);
    switch (mode)
    {
        case CompSettings::fet: processFet (left, right, n); break;
        case CompSettings::multiband: processMultiband (left, right, n); break;
        default: processClean (left, right, n); break;
    }
}

void Compressor::processClean (float* left, float* right, int n) noexcept
{
    const float attack = coefficient (rate, 0.001 * std::clamp (p.attackMs, 0.1f, 100.0f));
    const float release = coefficient (rate, 0.001 * std::clamp (p.releaseMs, 10.0f, 1500.0f));
    const float rms = coefficient (rate, 0.01);
    const float link = std::clamp (p.link, 0.0f, 1.0f);
    float makeup = p.makeupDb;
    if (p.autoMakeup)
        makeup += -0.5f * (cleanCurveDb (p, 0.0f) - 0.0f);
    const float makeupGain = dbToGain (makeup);
    const bool filtered = p.sidechainHz > 20.5f;
    std::array<float, 2> grNow { grSmooth, grSmooth };
    float shown = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float dl = left[i], dr = right[i];
        if (filtered)
        {
            dl = sidechain[0].process (dl);
            dr = sidechain[1].process (dr);
        }
        if (p.peak)
        {
            envL = std::abs (dl);
            envR = std::abs (dr);
        }
        else
        {
            envL = rms * envL + (1.0f - rms) * dl * dl;
            envR = rms * envR + (1.0f - rms) * dr * dr;
        }
        float levelL = p.peak ? envL : std::sqrt (envL), levelR = p.peak ? envR : std::sqrt (envR);
        const float shared = std::max (levelL, levelR);
        levelL = link * shared + (1.0f - link) * levelL;
        levelR = link * shared + (1.0f - link) * levelR;
        float io[2] { left[i], right[i] };
        const float levels[2] { levelL, levelR };
        for (int c = 0; c < 2; ++c)
        {
            const float lvl = gainToDb (levels[c]);
            const float target = cleanCurveDb (p, lvl) - lvl;   // <= 0
            auto& g = grNow[static_cast<std::size_t> (c)];
            const float k = target < g ? attack : release;
            g = k * g + (1.0f - k) * target;
            io[c] *= dbToGain (g) * makeupGain;
        }
        left[i] = io[0];
        right[i] = io[1];
        shown = std::min (grNow[0], grNow[1]);
    }
    grSmooth = 0.5f * (grNow[0] + grNow[1]);
    reductionDb.store (-shown, std::memory_order_relaxed);
}

void Compressor::processFet (float* left, float* right, int n) noexcept
{
    // Feedback: the signal is scaled by the gain the detector set from the *previous* output.
    const float inGain = dbToGain (std::clamp (p.fetInputDb, -12.0f, 48.0f));
    const float outGain = dbToGain (std::clamp (p.fetOutputDb, -36.0f, 18.0f));
    const float attack = coefficient (rate, 1.0e-6 * std::clamp (p.fetAttackUs, 20.0f, 800.0f));
    const int ratioIndex = std::clamp (p.fetRatio, 0, 4);
    const bool all = ratioIndex == 4;
    const float ratio = fetRatios[static_cast<std::size_t> (ratioIndex)];
    const float color = std::clamp (p.fetColor, 0.0f, 1.0f);
    const bool link = p.fetLink;
    float shown = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        float io[2] { left[i] * inGain, right[i] * inGain };
        // The input stage's colour (a little odd-order saturation, more with COLOR).
        for (auto& x : io)
            x = x + color * 0.3f * (std::tanh (x) - x);
        std::array<float, 2> det {};
        for (int c = 0; c < 2; ++c)
        {
            const float g = link ? fetGain : fetGainLR[static_cast<std::size_t> (c)];
            io[c] *= p.fetColorOnly ? 1.0f : g;
            det[static_cast<std::size_t> (c)] = std::abs (io[c]) * (all ? 1.8f : 1.0f);   // ALL: the detector driven harder
        }
        auto update = [&] (float& env, float& g, float level) {
            const float k = level > env ? attack : 0.0f;
            const float grDb = -gainToDb (g);
            // Release: program-dependent (slower the more it is holding down).
            const float rel = coefficient (rate, 0.001 * std::clamp (p.fetReleaseMs, 50.0f, 1100.0f) * (1.0f + 0.04f * grDb));
            env = level > env ? k * env + (1.0f - k) * level : rel * env + (1.0f - rel) * level;
            const float over = gainToDb (env) - fetReferenceDb;
            // Soft knee of 6 dB, then the ratio; ALL: a near-limiter whose knee folds over.
            float target = 0.0f;
            if (over > -3.0f)
            {
                const float x = over < 3.0f ? (over + 3.0f) * (over + 3.0f) / 12.0f : over;
                target = (1.0f - 1.0f / ratio) * x * (all ? 1.15f : 1.0f);
            }
            g = dbToGain (-std::min (target, 40.0f));
        };
        if (link)
        {
            update (fetEnv, fetGain, std::max (det[0], det[1]));
            shown = -gainToDb (fetGain);
        }
        else
        {
            update (fetEnvLR[0], fetGainLR[0], det[0]);
            update (fetEnvLR[1], fetGainLR[1], det[1]);
            shown = std::max (-gainToDb (fetGainLR[0]), -gainToDb (fetGainLR[1]));
        }
        // The output stage: a touch of even-free warmth with COLOR (ALL: more), then OUTPUT.
        for (auto& x : io)
            x = outGain * (x + (color * 0.15f + (all ? 0.1f : 0.0f)) * (std::tanh (1.5f * x) / 1.5f - x));
        left[i] = std::isfinite (io[0]) ? io[0] : 0.0f;
        right[i] = std::isfinite (io[1]) ? io[1] : 0.0f;
    }
    reductionDb.store (p.fetColorOnly ? 0.0f : shown, std::memory_order_relaxed);
}

void Compressor::processMultiband (float* left, float* right, int n) noexcept
{
    const float timeScale = std::pow (4.0f, 1.0f - 2.0f * std::clamp (p.time, 0.0f, 1.0f));   // TIME 0: 4x slower, 1: 4x faster
    const float attack = coefficient (rate, 0.001 * std::clamp (p.mbAttackMs, 0.1f, 100.0f) * timeScale);
    const float release = coefficient (rate, 0.001 * std::clamp (p.mbReleaseMs, 10.0f, 1500.0f) * timeScale);
    const float tilt = (std::clamp (p.tone, 0.0f, 1.0f) - 0.5f) * 6.0f;
    const std::array<float, 3> bandTrim { p.bandGainDb[0] - tilt, p.bandGainDb[1], p.bandGainDb[2] + tilt };
    const float matchCoef = coefficient (rate, 1.0);
    std::array<float, 3> shownDown {}, shownUp {};
    for (int i = 0; i < n; ++i)
    {
        float bands[3][2];
        const float in[2] { left[i], right[i] };
        for (int c = 0; c < 2; ++c)
        {
            float low, rest, mid, high;
            lowSplit[static_cast<std::size_t> (c)].split (in[c], low, rest);
            highSplit[static_cast<std::size_t> (c)].split (rest, mid, high);
            bands[0][c] = lowPhase[static_cast<std::size_t> (c)].allPass (low);   // (the high crossover's phase)
            bands[1][c] = mid;
            bands[2][c] = high;
        }
        float out[2] { 0.0f, 0.0f };
        for (std::size_t b = 0; b < 3; ++b)
        {
            // One detector per band, stereo-linked, updated once per sample.
            const float level = std::max (std::abs (bands[b][0]), std::abs (bands[b][1]));
            auto& env = bandEnv[b];
            env = level > env ? attack * env + (1.0f - attack) * level : release * env + (1.0f - release) * level;
            const float target = bandGainDb (p, static_cast<int> (b), gainToDb (env));
            auto& g = bandGain[b];
            g = target < g ? attack * g + (1.0f - attack) * target : release * g + (1.0f - release) * target;
            const float gain = dbToGain (g + bandTrim[b]);
            out[0] += gain * bands[b][0];
            out[1] += gain * bands[b][1];
            shownDown[b] = std::max (0.0f, -g);
            shownUp[b] = std::max (0.0f, g);
        }
        if (p.loudnessMatch)
        {
            inEnergy = matchCoef * inEnergy + (1.0 - matchCoef) * 0.5 * (static_cast<double> (in[0]) * in[0] + static_cast<double> (in[1]) * in[1]);
            outEnergy = matchCoef * outEnergy + (1.0 - matchCoef) * 0.5 * (static_cast<double> (out[0]) * out[0] + static_cast<double> (out[1]) * out[1]);
            if (outEnergy > 1.0e-10 && inEnergy > 1.0e-10)
                match = std::clamp (static_cast<float> (std::sqrt (inEnergy / outEnergy)), 0.25f, 4.0f);
        }
        else
            match = 1.0f;
        left[i] = match * out[0];
        right[i] = match * out[1];
    }
    for (std::size_t b = 0; b < 3; ++b)
    {
        bandReductionDb[b].store (shownDown[b], std::memory_order_relaxed);
        bandBoostDb[b].store (shownUp[b], std::memory_order_relaxed);
    }
    reductionDb.store (std::max ({ shownDown[0], shownDown[1], shownDown[2] }), std::memory_order_relaxed);
}

} // namespace andor::analog::fx
