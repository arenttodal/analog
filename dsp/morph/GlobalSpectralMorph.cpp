#include "GlobalSpectralMorph.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace andor::analog
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;
    constexpr float tiny = 1.0e-20f;
    constexpr double lowHz = 60.0, highHz = 16000.0;
    constexpr float maxCutDb = -36.0f, maxLiftDb = 24.0f;
    constexpr float fuseExponent = 0.35f;   ///< FUSE's power mean (0 geometric .. 1 arithmetic)

    float dbToPower (float db) noexcept { return std::pow (10.0f, 0.1f * db); }
    float powerToDb (float p) noexcept { return 10.0f * std::log10 (std::max (p, 1.0e-14f)); }
    float smoothstep (float x) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }
    /** A deterministic value in [0, 1) for WEAVE's region r (a fixed seed: every instance, every
        session and every render alike). */
    float hashed (std::uint32_t r) noexcept
    {
        std::uint32_t x = r * 0x9e3779b9u + 0x7f4a7c15u;
        x ^= x >> 16;
        x *= 0x85ebca6bu;
        x ^= x >> 13;
        x *= 0xc2b2ae35u;
        x ^= x >> 16;
        return static_cast<float> (x >> 8) / 16777216.0f;
    }
}

void GlobalSpectralMorph::prepare (double sampleRate, int)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    size = rate <= 50000.0 ? 1024 : (rate <= 100000.0 ? 2048 : 4096);
    hop = size / 4;
    bins = size / 2 + 1;
    fft.prepare (size);
    window.assign (static_cast<std::size_t> (size), 0.0f);
    for (int i = 0; i < size; ++i)   // sqrt of a periodic Hann: analysis x synthesis = Hann (sums to 2 at 75 % overlap)
        window[static_cast<std::size_t> (i)] = static_cast<float> (std::sqrt (0.5 - 0.5 * std::cos (twoPi * i / size)));
    const auto n = static_cast<std::size_t> (size), b = static_cast<std::size_t> (bins);
    for (auto& v : in)
        v.assign (n, 0.0f);
    for (auto& v : acc)
        v.assign (n, 0.0f);
    for (auto& v : ready)
        v.assign (static_cast<std::size_t> (hop), 0.0f);
    for (auto& v : dry)
        v.assign (n, 0.0f);
    for (auto* v : { &work, &kSpecL, &kSpecR, &dSpecL, &dSpecR, &mL, &mR, &prevL, &prevR })
        v->assign (n, Complex {});
    for (auto* v : { &powK, &powD, &envK, &envD, &envDShifted, &fineK, &fineD, &tonalK, &tonalD, &envDFollow, &envKFollow, &powDPrev })
        v->assign (b, 0.0f);
    prefix.assign (b + 1, 0.0f);
    binOctave.assign (b, 0.0f);
    for (int k = 1; k < bins; ++k)
        binOctave[static_cast<std::size_t> (k)] = static_cast<float> (std::log2 (std::max (1.0, k * rate / size) / lowHz));
    for (std::size_t r = 0; r < weaveThreshold.size(); ++r)
        weaveThreshold[r] = 0.08f + 0.84f * hashed (static_cast<std::uint32_t> (r));
    for (int e = 0; e <= displayBands; ++e)
        displayEdges[static_cast<std::size_t> (e)] = lowHz * std::pow (highHz / lowHz, static_cast<double> (e) / displayBands);
    positionStep = 1.0 / (0.015 * rate);
    levelSmooth = 1.0 - std::exp (-1.0 / (0.300 * rate));
    matchStep = 1.0 - std::exp (-1.0 / (0.050 * rate));
    reset();
}

void GlobalSpectralMorph::reset() noexcept
{
    for (auto& v : in)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : acc)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : ready)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : dry)
        std::fill (v.begin(), v.end(), 0.0f);
    for (auto* v : { &envDFollow, &envKFollow, &powDPrev })
        std::fill (v->begin(), v->end(), 0.0f);
    fill = size - hop;
    readyAt = 0;
    dryAt = 0;
    positionNow = framePosition = controls.position;
    previousMethod = controls.method;
    methodBlend = 0;
    presenceNow = 0.0f;
    holdSeconds = 0.0f;
    transientNow = 0.0f;
    powS = powDry = powOut = 0.0;
    matchDb = 0.0;
}

double GlobalSpectralMorph::displayBandHz (int band) const noexcept
{
    const auto b = static_cast<std::size_t> (std::clamp (band, 0, displayBands - 1));
    return std::sqrt (displayEdges[b] * displayEdges[b + 1]);
}

void GlobalSpectralMorph::process (float* sL, float* sR, const float* kL, const float* kR, const float* dL, const float* dR, int n) noexcept
{
    const double target = std::clamp (controls.position, 0.0, 1.0);
    for (int i = 0; i < n; ++i)
    {
        // The core M for this sample: the overlap-add's finished hop (read before this sample
        // can complete a frame, so M is exactly as late as the dry paths: N samples).
        const float coreL = ready[0][static_cast<std::size_t> (readyAt)], coreR = ready[1][static_cast<std::size_t> (readyAt)];
        readyAt = std::min (readyAt + 1, hop - 1);
        // The analysis frame: the carrier and D, hop by hop.
        const auto f = static_cast<std::size_t> (fill);
        in[0][f] = std::isfinite (kL[i]) ? kL[i] : 0.0f;
        in[1][f] = std::isfinite (kR[i]) ? kR[i] : 0.0f;
        in[2][f] = std::isfinite (dL[i]) ? dL[i] : 0.0f;
        in[3][f] = std::isfinite (dR[i]) ? dR[i] : 0.0f;
        if (++fill == size)
        {
            framePosition = positionNow;
            frame();
            for (auto& v : in)
                std::copy (v.begin() + hop, v.end(), v.begin());
            fill = size - hop;
            readyAt = 0;
        }
        // The dry paths, delayed by exactly the latency (read before write: N samples).
        const auto at = static_cast<std::size_t> (dryAt);
        const float sNowL = std::isfinite (sL[i]) ? sL[i] : 0.0f, sNowR = std::isfinite (sR[i]) ? sR[i] : 0.0f;
        const float drySL = dry[0][at], drySR = dry[1][at], dryKL = dry[2][at], dryKR = dry[3][at], dryDL = dry[4][at], dryDR = dry[5][at];
        dry[0][at] = sNowL;
        dry[1][at] = sNowR;
        dry[2][at] = in[0][static_cast<std::size_t> (fill - 1)];
        dry[3][at] = in[1][static_cast<std::size_t> (fill - 1)];
        dry[4][at] = in[2][static_cast<std::size_t> (fill - 1)];
        dry[5][at] = in[3][static_cast<std::size_t> (fill - 1)];
        dryAt = (dryAt + 1) % size;

        // The output law at the position now (glided 15 ms, linear: its ends reached exactly).
        positionNow = positionNow < target ? std::min (target, positionNow + positionStep) : std::max (target, positionNow - positionStep);
        const auto p = static_cast<float> (positionNow);
        const float b = std::sqrt (std::max (0.0f, 4.0f * p * (1.0f - p)));
        // (the ends exact in float: cos (pi / 2) is not 0)
        const float c = p <= 0.0f ? 1.0f : (p >= 1.0f ? 0.0f : std::cos (0.5f * std::numbers::pi_v<float> * p));
        const float s = p <= 0.0f ? 0.0f : (p >= 1.0f ? 1.0f : std::sin (0.5f * std::numbers::pi_v<float> * p));
        float outL, outR;
        if (controls.output == Output::morphOnly)
        {
            const float wRest = c, wK = (1.0f - p) * (1.0f - b), wD = p * (1.0f - b);
            outL = wRest * (drySL - dryKL) + wK * dryKL + b * coreL + wD * dryDL;
            outR = wRest * (drySR - dryKR) + wK * dryKR + b * coreR + wD * dryDR;
        }
        else
        {
            const float norm = 1.0f / std::sqrt (1.0f + b * b);
            outL = norm * (c * drySL + s * dryDL + b * coreL);
            outR = norm * (c * drySR + s * dryDR + b * coreR);
        }
        // LEVEL MATCH: between the ends, the loudness glides from S's to D's (dB), scaled by b.
        powS += levelSmooth * (0.5 * (static_cast<double> (drySL) * drySL + static_cast<double> (drySR) * drySR) - powS);
        powDry += levelSmooth * (0.5 * (static_cast<double> (dryDL) * dryDL + static_cast<double> (dryDR) * dryDR) - powDry);
        powOut += levelSmooth * (0.5 * (static_cast<double> (outL) * outL + static_cast<double> (outR) * outR) - powOut);
        double wantDb = 0.0;
        if (controls.levelMatch && b > 0.0f && powOut > 1.0e-12)
        {
            const double sDb = 10.0 * std::log10 (std::max (powS, 1.0e-12)), dDb = 10.0 * std::log10 (std::max (powDry, 1.0e-12));
            // (towards D only as far as D is there: a long-silent D leaves the synth's loudness)
            const double toD = static_cast<double> (p) * presenceNow;
            const double targetDb = (1.0 - toD) * sDb + toD * dDb, outDb = 10.0 * std::log10 (powOut);
            wantDb = std::clamp (targetDb - outDb, -12.0, 12.0);
            if (targetDb < -60.0)
                wantDb = std::min (wantDb, 0.0);   // (never lift near-silence)
            wantDb *= b;
        }
        matchDb += matchStep * (wantDb - matchDb);
        if (std::abs (matchDb) < 1.0e-6 && wantDb == 0.0)
            matchDb = 0.0;
        const float g = matchDb == 0.0 ? 1.0f : static_cast<float> (std::pow (10.0, matchDb / 20.0));
        sL[i] = g * outL;
        sR[i] = g * outR;
    }
}

void GlobalSpectralMorph::smoothAcross (const std::vector<float>& power, std::vector<float>& envelope, double octaves) noexcept
{
    // Each bin's mean power over [k 2^(-w/2), k 2^(w/2)] (at least its neighbours): prefix sums.
    prefix[0] = 0.0f;
    for (int k = 0; k < bins; ++k)
        prefix[static_cast<std::size_t> (k + 1)] = prefix[static_cast<std::size_t> (k)] + power[static_cast<std::size_t> (k)];
    const double half = std::exp2 (0.5 * octaves);
    for (int k = 0; k < bins; ++k)
    {
        const int lo = std::clamp (std::min (k - 1, static_cast<int> (std::floor (k / half))), 0, bins - 1);
        const int hi = std::clamp (std::max (k + 1, static_cast<int> (std::ceil (k * half))), 0, bins - 1);
        envelope[static_cast<std::size_t> (k)] = (prefix[static_cast<std::size_t> (hi + 1)] - prefix[static_cast<std::size_t> (lo)]) / static_cast<float> (hi - lo + 1);
    }
}

float GlobalSpectralMorph::weaveOwnership (int bin, double p) const noexcept
{
    // DETAIL: 6 .. 40 regions across 60 Hz .. 16 kHz (fractional counts blended, so a turn of
    // DETAIL never jumps). Each region turns to D over a span of 0.12 of the position, centred
    // on its seeded threshold; between region centres the ownership is blended (raised cosine).
    const double count = 6.0 + 34.0 * std::clamp (controls.detail, 0.0, 1.0);
    const double span = std::log2 (highHz / lowHz);
    const double x = std::clamp (static_cast<double> (binOctave[static_cast<std::size_t> (bin)]) / span, 0.0, 1.0);
    auto at = [&] (int regions) {
        const double pos = x * regions - 0.5;
        const int r0 = std::clamp (static_cast<int> (std::floor (pos)), 0, regions - 1), r1 = std::min (regions - 1, r0 + 1);
        const double t = std::clamp (pos - r0, 0.0, 1.0);
        const double blend = 0.5 - 0.5 * std::cos (std::numbers::pi * t);
        auto own = [&] (int r) { return smoothstep (static_cast<float> ((p - weaveThreshold[static_cast<std::size_t> (r)]) / 0.12 + 0.5)); };
        return static_cast<float> ((1.0 - blend) * own (r0) + blend * own (r1));
    };
    const int lo = static_cast<int> (std::floor (count)), hi = std::min (maxWeaveRegions, lo + 1);
    const auto frac = static_cast<float> (count - lo);
    return frac <= 0.0f ? at (lo) : (1.0f - frac) * at (lo) + frac * at (hi);
}

void GlobalSpectralMorph::method (Method m, double p, std::vector<Complex>& outL, std::vector<Complex>& outR) noexcept
{
    const auto t = static_cast<float> (std::clamp (controls.texture, 0.0, 1.0));
    // TEXTURE on the residual (the bins that are not spectral peaks): removed at 0, as is at
    // 0.5, +12 dB at 1.
    const float residualGain = t < 0.5f ? std::pow (2.0f * t, 2.0f) : std::pow (10.0f, 0.6f * (t - 0.5f) * 2.0f);
    const float addResidual = std::max (0.0f, 2.0f * t - 1.0f);   // (IMPRINT: D's own residual added above the middle)
    const float presence = presenceNow;
    const float minGain = std::sqrt (dbToPower (maxCutDb)), maxGain = std::sqrt (dbToPower (maxLiftDb));
    float peakK = 0.0f, peakD = 0.0f;
    for (int k = 0; k < bins; ++k)
    {
        peakK = std::max (peakK, envKFollow[static_cast<std::size_t> (k)]);
        peakD = std::max (peakD, envDShifted[static_cast<std::size_t> (k)]);
    }
    // Floors 40 dB under each source's own loudest band: nothing beneath them is lifted.
    const float floorK = 1.0e-4f * peakK + tiny, floorD = 1.0e-4f * peakD + tiny;
    const auto q = static_cast<float> (p) * presence;
    for (int k = 0; k < bins; ++k)
    {
        const auto ks = static_cast<std::size_t> (k);
        const float eK = envKFollow[ks] + floorK, eD = envDShifted[ks] + floorD;
        const float resK = 1.0f - tonalK[ks], resD = 1.0f - tonalD[ks];
        const float textureK = tonalK[ks] + resK * residualGain, textureD = tonalD[ks] + resD * residualGain;
        Complex l, r;
        switch (m)
        {
            case Method::imprint:
            {
                const float depth = std::min (1.0f, 2.0f * static_cast<float> (p)) * presence;
                float g = envKFollow[ks] < floorK ? 1.0f : std::pow (eD / eK, 0.5f * depth);
                g = std::clamp (g, minGain, maxGain) * textureK;
                l = g * kSpecL[ks];
                r = g * kSpecR[ks];
                if (addResidual > 0.0f)
                {
                    const float add = addResidual * depth * resD;
                    l += add * dSpecL[ks];
                    r += add * dSpecR[ks];
                }
                break;
            }
            case Method::fuse:
            {
                // One envelope for both: a power mean of theirs, each fine structure reshaped to
                // it, the two summed at equal power.
                const float shared = std::pow ((1.0f - q) * std::pow (eK, fuseExponent) + q * std::pow (eD, fuseExponent), 1.0f / fuseExponent);
                const float gK = std::clamp (std::sqrt (shared / eK), minGain, maxGain) * std::sqrt (1.0f - q) * textureK;
                const float gD = std::clamp (std::sqrt (shared / eD), minGain, maxGain) * std::sqrt (q) * textureD;
                l = gK * kSpecL[ks] + gD * dSpecL[ks];
                r = gK * kSpecR[ks] + gD * dSpecR[ks];
                break;
            }
            case Method::weave:
            {
                // D's regions in D's own sound, its envelope moved by FORMANTS (its fine
                // structure, so its pitch, kept).
                const float o = weaveOwnership (k, p) * presence;
                const float formant = std::clamp (std::sqrt (eD / (envDFollow[ks] + floorD)), minGain, maxGain);
                const float gK = (1.0f - o) * textureK, gD = o * textureD * formant;
                l = gK * kSpecL[ks] + gD * dSpecL[ks];
                r = gK * kSpecR[ks] + gD * dSpecR[ks];
                break;
            }
        }
        outL[ks] = l;
        outR[ks] = r;
    }
}

void GlobalSpectralMorph::frame() noexcept
{
    const auto n = static_cast<std::size_t> (size);
    // Forward transforms: the carrier's pair packed in one complex FFT, D's in another.
    auto analyse = [&] (const std::vector<float>& left, const std::vector<float>& right, std::vector<Complex>& outLeft, std::vector<Complex>& outRight) {
        for (std::size_t i = 0; i < n; ++i)
            work[i] = Complex (window[i] * left[i], window[i] * right[i]);
        fft.transform (work.data(), false);
        for (int k = 0; k < bins; ++k)
        {
            const auto a = work[static_cast<std::size_t> (k)], b = std::conj (work[static_cast<std::size_t> ((size - k) % size)]);
            outLeft[static_cast<std::size_t> (k)] = 0.5f * (a + b);
            outRight[static_cast<std::size_t> (k)] = Complex (0.0f, -0.5f) * (a - b);
        }
    };
    analyse (in[0], in[1], kSpecL, kSpecR);
    analyse (in[2], in[3], dSpecL, dSpecR);
    const double frameSeconds = static_cast<double> (hop) / rate;
    float energyD = 0.0f, flux = 0.0f, fluxNorm = 0.0f;
    for (int k = 0; k < bins; ++k)
    {
        const auto ks = static_cast<std::size_t> (k);
        powK[ks] = 0.5f * (std::norm (kSpecL[ks]) + std::norm (kSpecR[ks]));
        powD[ks] = 0.5f * (std::norm (dSpecL[ks]) + std::norm (dSpecR[ks]));
        energyD += powD[ks];
        // D's spectral flux (its onsets): the rise of each bin's level, weighted by its level.
        const float rise = powerToDb (powD[ks] + 1.0e-12f) - powerToDb (powDPrev[ks] + 1.0e-12f);
        if (rise > 0.0f)
            flux += std::min (rise, 30.0f) * powD[ks];
        fluxNorm += powD[ks];
        powDPrev[ks] = powD[ks];
    }
    // D's presence: held 1.5 s after its last frame above -70 dBFS (a loop between its hits is
    // still D), then gone over about 300 ms: a long-silent D no longer shapes the synth.
    const float frameDb = powerToDb (energyD * 4.0f / (static_cast<float> (size) * static_cast<float> (size)) + 1.0e-14f);
    holdSeconds = frameDb > -70.0f ? 1.5f : std::max (0.0f, holdSeconds - static_cast<float> (frameSeconds));
    const float presenceTarget = holdSeconds > 0.0f ? 1.0f : 0.0f;
    presenceNow += (presenceTarget > presenceNow ? 1.0f : static_cast<float> (frameSeconds / 0.3)) * (presenceTarget - presenceNow);
    presenceNow = std::clamp (presenceNow, 0.0f, 1.0f);
    // ATTACK: D's transients (flux, in dB weighted by level), and how fast D's envelope rises.
    const float fluxDb = fluxNorm > 1.0e-12f ? flux / fluxNorm : 0.0f;
    const float strength = std::clamp ((fluxDb - 3.0f) / 9.0f, 0.0f, 1.0f);
    transientNow = std::max (strength, transientNow * static_cast<float> (std::exp (-frameSeconds / 0.05)));
    const auto attack = static_cast<float> (std::clamp (controls.attack, 0.0, 1.0));
    const float riseSeconds = 0.080f * (1.0f - attack);
    const float rise = riseSeconds <= 0.0f ? 1.0f : static_cast<float> (1.0 - std::exp (-frameSeconds / riseSeconds));
    const auto fall = static_cast<float> (1.0 - std::exp (-frameSeconds / 0.250));
    const auto followK = static_cast<float> (1.0 - std::exp (-frameSeconds / 0.030));

    // Envelopes across frequency (DETAIL: an octave .. a semitone), then in time.
    const double octaves = 1.0 + (1.0 / 12.0 - 1.0) * std::clamp (controls.detail, 0.0, 1.0);
    smoothAcross (powK, envK, octaves);
    smoothAcross (powD, envD, octaves);
    smoothAcross (powK, fineK, 1.0 / 3.0);
    smoothAcross (powD, fineD, 1.0 / 3.0);
    for (int k = 0; k < bins; ++k)
    {
        const auto ks = static_cast<std::size_t> (k);
        envKFollow[ks] += (envK[ks] > envKFollow[ks] ? 1.0f : followK) * (envK[ks] - envKFollow[ks]);
        envDFollow[ks] += (envD[ks] > envDFollow[ks] ? rise : fall) * (envD[ks] - envDFollow[ks]);
        if (envKFollow[ks] < 1.0e-24f)
            envKFollow[ks] = 0.0f;
        if (envDFollow[ks] < 1.0e-24f)
            envDFollow[ks] = 0.0f;
        // Peaks (tonal) against the residual: a local maximum 3 dB over the third-octave mean,
        // and its main lobe's neighbours.
        auto tonal = [&] (const std::vector<float>& pw, const std::vector<float>& fine) {
            auto peak = [&] (int j) {
                if (j < 1 || j >= bins - 1)
                    return false;
                const auto js = static_cast<std::size_t> (j);
                return pw[js] >= pw[js - 1] && pw[js] >= pw[js + 1] && pw[js] > 2.0f * fine[js];
            };
            return peak (k) || peak (k - 1) || peak (k + 1) ? 1.0f : 0.0f;
        };
        tonalK[ks] = tonal (powK, fineK);
        tonalD[ks] = tonal (powD, fineD);
    }
    // FORMANTS: D's envelope read 2^(-st/12) along the frequency axis (clamped at its ends).
    const auto ratio = static_cast<float> (std::exp2 (-std::clamp (controls.formants, -12.0, 12.0) / 12.0));
    for (int k = 0; k < bins; ++k)
    {
        const float x = std::clamp (static_cast<float> (k) * ratio, 0.0f, static_cast<float> (bins - 1));
        const int x0 = static_cast<int> (x), x1 = std::min (bins - 1, x0 + 1);
        const float t = x - static_cast<float> (x0);
        envDShifted[static_cast<std::size_t> (k)] = (1.0f - t) * envDFollow[static_cast<std::size_t> (x0)] + t * envDFollow[static_cast<std::size_t> (x1)];
    }

    // The core: this frame's method (and, for a few frames after a change, the previous one,
    // crossfaded over about 64 ms: no click).
    const double p = framePosition;
    method (controls.method, p, mL, mR);
    if (controls.method != previousMethod)
    {
        blendMethod = previousMethod;   // (the old method keeps running, fading, for a few frames)
        methodBlend = methodBlendFrames;
        previousMethod = controls.method;
    }
    if (methodBlend > 0)
    {
        method (blendMethod, p, prevL, prevR);
        const float w = static_cast<float> (methodBlend) / static_cast<float> (methodBlendFrames + 1);
        for (int k = 0; k < bins; ++k)
        {
            const auto ks = static_cast<std::size_t> (k);
            mL[ks] = (1.0f - w) * mL[ks] + w * prevL[ks];
            mR[ks] = (1.0f - w) * mR[ks] + w * prevR[ks];
        }
        --methodBlend;
    }
    // ATTACK's transients: D's own frame into the core while D strikes, as much as ATTACK asks
    // (and as far as the position has gone towards D).
    const float strike = attack * transientNow * static_cast<float> (p) * presenceNow;
    if (strike > 0.001f)
        for (int k = 0; k < bins; ++k)
        {
            const auto ks = static_cast<std::size_t> (k);
            mL[ks] = (1.0f - 0.5f * strike) * mL[ks] + strike * dSpecL[ks];
            mR[ks] = (1.0f - 0.5f * strike) * mR[ks] + strike * dSpecR[ks];
        }

    // Telemetry (compact: 32 log bands), before the inverse transform uses the spectra.
    const double binHz = rate / size;
    for (int band = 0; band < displayBands; ++band)
    {
        const int k0 = std::max (1, static_cast<int> (displayEdges[static_cast<std::size_t> (band)] / binHz));
        const int k1 = std::clamp (static_cast<int> (displayEdges[static_cast<std::size_t> (band + 1)] / binHz), k0 + 1, bins);
        float a = 0.0f, d = 0.0f, mm = 0.0f, own = 0.0f;
        for (int k = k0; k < k1; ++k)
        {
            const auto ks = static_cast<std::size_t> (k);
            a += powK[ks];
            d += envDShifted[ks];
            mm += 0.5f * (std::norm (mL[ks]) + std::norm (mR[ks]));
            if (controls.method == Method::weave)
                own += weaveOwnership (k, p) * presenceNow;
        }
        const float count = static_cast<float> (k1 - k0);
        const float norm = 4.0f / (static_cast<float> (size) * static_cast<float> (size));   // (sqrt-Hann: about full scale for a full-scale sine)
        shownK[static_cast<std::size_t> (band)].store (powerToDb (norm * a), std::memory_order_relaxed);
        shownD[static_cast<std::size_t> (band)].store (powerToDb (norm * d), std::memory_order_relaxed);
        shownM[static_cast<std::size_t> (band)].store (powerToDb (norm * mm), std::memory_order_relaxed);
        shownOwner[static_cast<std::size_t> (band)].store (controls.method == Method::weave ? own / count : -1.0f, std::memory_order_relaxed);
    }
    shownTransient.store (transientNow, std::memory_order_relaxed);
    shownPresence.store (presenceNow, std::memory_order_relaxed);

    // Inverse: M's pair packed in one complex IFFT (left real, right imaginary), windowed and
    // overlap-added; the first hop is finished.
    for (int k = 0; k < bins; ++k)
    {
        const auto ks = static_cast<std::size_t> (k);
        work[ks] = mL[ks] + Complex (0.0f, 1.0f) * mR[ks];
        if (k > 0 && k < size / 2)
            work[static_cast<std::size_t> (size - k)] = std::conj (mL[ks]) + Complex (0.0f, 1.0f) * std::conj (mR[ks]);
    }
    fft.transform (work.data(), true);
    for (std::size_t i = 0; i < n; ++i)
    {
        acc[0][i] += 0.5f * window[i] * work[i].real();
        acc[1][i] += 0.5f * window[i] * work[i].imag();
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& a = acc[static_cast<std::size_t> (ch)];
        std::copy (a.begin(), a.begin() + hop, ready[static_cast<std::size_t> (ch)].begin());
        std::copy (a.begin() + hop, a.end(), a.begin());
        std::fill (a.end() - hop, a.end(), 0.0f);
        for (auto& x : ready[static_cast<std::size_t> (ch)])
            if (std::abs (x) < tiny)
                x = 0.0f;
    }
    frameCount.store (frameCount.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

} // namespace andor::analog
