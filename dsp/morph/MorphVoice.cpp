#include "MorphVoice.h"

#include <algorithm>
#include <cmath>

namespace andor::analog::morph
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr float floorAmp = 1.0e-5f;

    float dbToGain (float db) noexcept { return std::pow (10.0f, 0.05f * db); }
    float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }

    /** The frame's spectral envelope (dB) at `hz`, interpolated in log frequency between band
        centres; flat beyond the ends. */
    float envelopeAt (const MorphProfile::Frame& f, double hz) noexcept
    {
        constexpr int n = MorphProfile::envelopeBands;
        const double lo = MorphProfile::envelopeBandCentreHz (0), hi = MorphProfile::envelopeBandCentreHz (n - 1);
        if (hz <= lo)
            return f.envelopeDb[0];
        if (hz >= hi)
            return f.envelopeDb[n - 1];
        const double x = std::log (hz / lo) / std::log (hi / lo) * (n - 1);
        const int i = std::min (n - 2, static_cast<int> (x));
        const auto t = static_cast<float> (x - i);
        return f.envelopeDb[static_cast<std::size_t> (i)] * (1.0f - t) + f.envelopeDb[static_cast<std::size_t> (i + 1)] * t;
    }

    float smoothstep (float x) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    /** Moves `now` towards `target`, landing exactly on it (so the ends are exact). */
    void glide (float& now, float target, float k) noexcept
    {
        now += k * (target - now);
        if (std::abs (target - now) < 1.0e-6f)
            now = target;
    }
}

void addCarrierSpectrum (CarrierSpectrum& out, bool saw, bool pulse, bool triangle, float width, float gain) noexcept
{
    for (int h = 1; h <= MorphProfile::partials; ++h)
    {
        double a = 0.0;
        if (saw)
            a += 2.0 / (pi * h);
        if (pulse)
            a += 4.0 / (pi * h) * std::abs (std::sin (pi * h * width));
        if (triangle && h % 2 == 1)
            a += 8.0 / (pi * pi * h * h);
        out[static_cast<std::size_t> (h - 1)] += static_cast<float> (a) * gain;
    }
}

void MorphVoice::Svf::set (double hz, double q, double sampleRate) noexcept
{
    g = static_cast<float> (std::tan (pi * std::min (hz, 0.45 * sampleRate) / sampleRate));
    k = static_cast<float> (1.0 / q);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float MorphVoice::Svf::band (float x) noexcept
{
    const float v3 = x - s2;
    const float v1 = a1 * s1 + a2 * v3;
    const float v2 = s2 + a2 * s1 + a3 * v3;
    s1 = 2.0f * v1 - s1;
    s2 = 2.0f * v2 - s2;
    return k * v1;   // (peak gain 1)
}

void MorphVoice::prepare (double internalRate, int oversampling) noexcept
{
    rate = internalRate;
    factor = std::max (1, oversampling);
    hostRate = rate / factor;
    for (int b = 0; b < bands; ++b)
    {
        bandCentre[static_cast<std::size_t> (b)] = static_cast<float> (100.0 * std::exp2 (b * 2.0 / 3.0));
        bank[static_cast<std::size_t> (b)].set (bandCentre[static_cast<std::size_t> (b)], 2.1, rate);
    }
    mappedShift = -1.0;
    for (int b = 0; b < noiseBands; ++b)
        noiseBank[static_cast<std::size_t> (b)].set (80.0 * std::exp2 (b), 1.4, hostRate);
    energyCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.03 * rate)));
}

void MorphVoice::start (const MorphProfile* p, std::uint64_t seed) noexcept
{
    profile = p != nullptr && p->kind == MorphProfile::Kind::pitched && ! p->frames.empty() ? p : nullptr;
    noise.reseed (osp::Prng::deriveSeed (seed, 0x6d6f7270u, 0));
    for (auto& s : bank)
        s.s1 = s.s2 = 0.0f;
    for (auto& s : noiseBank)
        s.s1 = s.s2 = 0.0f;
    carrierEnergy.fill (0.0f);
    gain.fill (1.0f);
    gainTarget.fill (1.0f);
    amp.fill (0.0f);
    ampTarget.fill (0.0f);
    noiseGain.fill (0.0f);
    noiseTarget.fill (0.0f);
    // The partials start at the seed's own phases (an acoustic tone's are never aligned).
    for (int h = 0; h < maxPartials; ++h)
    {
        const double phase = 2.0 * pi * noise.nextDouble();
        re[static_cast<std::size_t> (h)] = static_cast<float> (std::cos (phase));
        im[static_cast<std::size_t> (h)] = static_cast<float> (std::sin (phase));
        weaveOrder[static_cast<std::size_t> (h)] = static_cast<float> (0.1 + 0.8 * noise.nextDouble());
    }
    additivePrev = additiveNow = noisePrev = noiseNow = 0.0f;
    organicLevel = carrierLevel = coreLevel = 0.0f;
    matchGain = matchTarget = 1.0f;
    phaseInHost = 0;
    wOrganic = tOrganic = 1.0f;
    wCore = tCore = wCarrier = tCarrier = 0.0f;
    coreOut = 0.0f;
    fresh = true;
}

void MorphVoice::control (const Control& c) noexcept
{
    if (profile == nullptr)
        return;
    method = c.method;
    const auto p = static_cast<float> (std::clamp (c.position, 0.0, 1.0));
    // The endpoint law, and the recording's attack kept (ATTACK) while it is in its onset.
    const float b = std::sqrt (std::max (0.0f, 4.0f * p * (1.0f - p)));
    float a = (1.0f - p) * (1.0f - b), cw = p * (1.0f - b);
    const double body = profile->bodyFrame * profile->hopSeconds;
    const double t = c.profileSeconds;
    const float onset = t < body ? 1.0f : static_cast<float> (std::exp (-(t - body) / 0.06));
    attackKeep = static_cast<float> (std::clamp (c.attack, 0.0, 1.0)) * (1.0f - p) * onset;
    float bw = b;
    if (attackKeep > 0.0f && a < 1.0f)
    {
        const float a2 = a + attackKeep * (1.0f - a);
        const float scale = (1.0f - a2) / (1.0f - a);
        bw *= scale;
        cw *= scale;
        a = a2;
    }
    tOrganic = a;
    tCore = bw;
    tCarrier = cw;

    // The frame nearest the playhead (frames are ~11 ms apart; the gains glide between them).
    const auto frameIndex = static_cast<std::size_t> (std::clamp (std::lround (t / std::max (1.0e-6, profile->hopSeconds)), 0L,
                                                                 static_cast<long> (profile->frames.size()) - 1));
    const auto& frame = profile->frames[frameIndex];
    const double shift = std::exp2 (std::clamp (c.formants, -12.0, 12.0) / 12.0);
    const float texture = static_cast<float> (std::clamp (c.texture, 0.0, 1.0));

    if (method == MorphMethod::imprint)
    {
        // G = the recording's envelope over the carrier's measured band energy, its overall
        // level taken out (the shape moves, not the loudness), clamped, then G^s.
        std::array<float, bands> logG {};
        float weighted = 0.0f, weights = 0.0f;
        for (int k = 0; k < bands; ++k)
        {
            const float acoustic = envelopeAt (frame, bandCentre[static_cast<std::size_t> (k)] / shift);
            const float carrierDb = gainToDb (std::sqrt (carrierEnergy[static_cast<std::size_t> (k)]));
            logG[static_cast<std::size_t> (k)] = acoustic - carrierDb;
            const float w = carrierEnergy[static_cast<std::size_t> (k)] + 1.0e-9f;
            weighted += w * logG[static_cast<std::size_t> (k)];
            weights += w;
        }
        const float mean = weighted / weights;
        const float s = std::min (1.0f, 2.0f * (1.0f - p));
        // DETAIL: how finely the envelope is followed (0: a broad tilt, 1: every band).
        const float detail = static_cast<float> (std::clamp (c.detail, 0.0, 1.0));
        for (int k = 0; k < bands; ++k)
        {
            float g = logG[static_cast<std::size_t> (k)] - mean;
            if (detail < 1.0f && k > 0 && k < bands - 1)
                g = detail * g + (1.0f - detail) * (logG[static_cast<std::size_t> (k - 1)] + g + logG[static_cast<std::size_t> (k + 1)] - 2.0f * mean) / 3.0f;
            gainTarget[static_cast<std::size_t> (k)] = dbToGain (s * std::clamp (g, -24.0f, 18.0f));
        }
    }
    else
    {
        // FUSE / WEAVE: the partials on the carrier's fundamental, below the host Nyquist.
        partialCount = 8 + static_cast<int> (std::lround (40.0 * std::clamp (c.detail, 0.0, 1.0)));
        const double f0 = std::max (20.0, c.fundamentalHz);
        std::array<float, maxPartials> acoustic {}, analog {};
        float sumA = 0.0f, sumC = 0.0f;
        for (int h = 1; h <= partialCount; ++h)
        {
            const auto i = static_cast<std::size_t> (h - 1);
            const double fh = f0 * h;
            if (fh > 0.45 * hostRate)
                break;
            // The recording's partial h, moved by how its body's envelope differs at this
            // pitch (formants stay put, FORMANTS shifts them), or the envelope alone where the
            // frame has no pitch.
            float ac;
            if (frame.f0Hz > 0.0f)
            {
                const float correction = std::clamp (envelopeAt (frame, fh / shift) - envelopeAt (frame, frame.f0Hz * h), -18.0f, 18.0f);
                ac = frame.amp[i] * dbToGain (correction);
            }
            else
                ac = dbToGain (envelopeAt (frame, fh / shift)) / static_cast<float> (h);
            acoustic[i] = std::max (ac, 0.0f);
            analog[i] = c.carrier != nullptr ? (*c.carrier)[i] : 1.0f / static_cast<float> (h);
            sumA += acoustic[i] * acoustic[i];
            sumC += analog[i] * analog[i];
            stepRe[i] = static_cast<float> (std::cos (2.0 * pi * fh / hostRate));
            stepIm[i] = static_cast<float> (std::sin (2.0 * pi * fh / hostRate));
        }
        const float na = 1.0f / std::sqrt (std::max (sumA, 1.0e-12f)), nc = 1.0f / std::sqrt (std::max (sumC, 1.0e-12f));
        const int groupSize = std::max (1, static_cast<int> (std::lround (6.0 - 5.0 * std::clamp (c.detail, 0.0, 1.0))));
        for (int h = 1; h <= maxPartials; ++h)
        {
            const auto i = static_cast<std::size_t> (h - 1);
            if (h > partialCount || f0 * h > 0.45 * hostRate)
            {
                ampTarget[i] = 0.0f;
                continue;
            }
            const float x = std::max (acoustic[i] * na, floorAmp), y = std::max (analog[i] * nc, floorAmp);
            if (method == MorphMethod::fuse)
                ampTarget[i] = std::exp ((1.0f - p) * std::log (x) + p * std::log (y));   // every partial between the two
            else
            {
                // WEAVE: this partial's group belongs to the recording while the position is
                // below its threshold, to the carrier above it (a narrow, smooth seam).
                const float threshold = weaveOrder[static_cast<std::size_t> ((h - 1) / groupSize)];
                const float m = smoothstep ((threshold - p) / 0.12f + 0.5f);
                ampTarget[i] = m * x + (1.0f - m) * y;
            }
        }
        // (The rotators drift a little in magnitude: put back on the unit circle.)
        for (int h = 0; h < maxPartials; ++h)
        {
            const auto i = static_cast<std::size_t> (h);
            const float r = std::sqrt (re[i] * re[i] + im[i] * im[i]);
            if (r > 0.0f)
            {
                re[i] /= r;
                im[i] /= r;
            }
        }
    }

    // The residual's texture: its noise per octave (TEXTURE, fading towards the carrier).
    if (shift != mappedShift)
    {
        mappedShift = shift;
        for (int r = 0; r < MorphProfile::residualBands; ++r)
        {
            const double octave = std::log2 (MorphProfile::residualBandCentreHz (r) * shift / 80.0);
            const auto k = static_cast<int> (std::lround (octave));
            noiseBandOf[static_cast<std::size_t> (r)] = static_cast<std::int8_t> (k >= 0 && k < noiseBands ? k : -1);
        }
    }
    std::array<float, noiseBands> energy {};
    for (int r = 0; r < MorphProfile::residualBands; ++r)
        if (const int k = noiseBandOf[static_cast<std::size_t> (r)]; k >= 0)
            energy[static_cast<std::size_t> (k)] += frame.residual[static_cast<std::size_t> (r)] * frame.residual[static_cast<std::size_t> (r)];
    for (int k = 0; k < noiseBands; ++k)
    {
        const float level = frame.level > 0.0f ? std::sqrt (energy[static_cast<std::size_t> (k)]) / frame.level : 0.0f;
        noiseTarget[static_cast<std::size_t> (k)] = 2.0f * texture * (1.0f - p) * std::min (level, 2.0f);
    }
    // IMPRINT's gains glide here (the control rate), not per sample.
    for (int k = 0; k < bands; ++k)
        glide (gain[static_cast<std::size_t> (k)], gainTarget[static_cast<std::size_t> (k)], 0.35f);

    // Loudness: the core held at the ends' level between them (dB interpolated, LEVEL MATCH)
    // or their linear mix.
    const float lo = std::sqrt (organicLevel), lc = std::sqrt (carrierLevel), lm = std::sqrt (coreLevel);
    const float wanted = c.levelMatch ? dbToGain ((1.0f - p) * gainToDb (lo) + p * gainToDb (lc)) : (1.0f - p) * lo + p * lc;
    matchTarget = lm > 1.0e-6f ? std::clamp (wanted / lm, 0.05f, 20.0f) : 1.0f;
    if (fresh)
    {
        // A note's first control: the weights, gains and partials where they belong now.
        fresh = false;
        wOrganic = tOrganic;
        wCore = tCore;
        wCarrier = tCarrier;
        gain = gainTarget;
        amp = ampTarget;
        noiseGain = noiseTarget;
    }
}

void MorphVoice::renderHost() noexcept
{
    additivePrev = additiveNow;
    noisePrev = noiseNow;
    float sum = 0.0f;
    if (method != MorphMethod::imprint)
        for (int h = 0; h < partialCount; ++h)
        {
            const auto i = static_cast<std::size_t> (h);
            amp[i] += 0.02f * (ampTarget[i] - amp[i]);
            const float r = re[i] * stepRe[i] - im[i] * stepIm[i];
            im[i] = re[i] * stepIm[i] + im[i] * stepRe[i];
            re[i] = r;
            sum += amp[i] * im[i];
        }
    additiveNow = sum;
    float n = 0.0f;
    const auto white = static_cast<float> (noise.bipolar());
    for (int k = 0; k < noiseBands; ++k)
    {
        const auto i = static_cast<std::size_t> (k);
        noiseGain[i] += 0.02f * (noiseTarget[i] - noiseGain[i]);
        if (noiseGain[i] > 1.0e-5f)
            n += noiseGain[i] * noiseBank[i].band (white);
    }
    noiseNow = n;
}

float MorphVoice::process (float organic, float carrier) noexcept
{
    if (profile == nullptr)
        return organic;
    const float k = 0.0015f;   // (a few ms: a position move or a method switch never clicks)
    glide (wOrganic, tOrganic, k);
    glide (wCore, tCore, k);
    glide (wCarrier, tCarrier, k);
    glide (matchGain, matchTarget, k);
    organicLevel += energyCoef * (organic * organic - organicLevel);
    carrierLevel += energyCoef * (carrier * carrier - carrierLevel);

    if (phaseInHost == 0)
        renderHost();
    const float frac = static_cast<float> (phaseInHost + 1) / static_cast<float> (factor);
    phaseInHost = (phaseInHost + 1) % factor;
    const float texture = noisePrev + frac * (noiseNow - noisePrev);

    float core;
    if (method == MorphMethod::imprint)
    {
        core = carrier;
        for (int b = 0; b < bands; ++b)
        {
            const auto i = static_cast<std::size_t> (b);
            const float y = bank[i].band (carrier);
            carrierEnergy[i] += energyCoef * (y * y - carrierEnergy[i]);
            core += (gain[i] - 1.0f) * y;
        }
        core += texture * std::sqrt (carrierLevel);
    }
    else
        core = additivePrev + frac * (additiveNow - additivePrev) + texture * 0.3f;
    coreLevel += energyCoef * (core * core - coreLevel);
    coreOut = wCore * matchGain * core;
    return wOrganic * organic + coreOut + wCarrier * carrier;
}

} // namespace andor::analog::morph
