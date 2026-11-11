#include "GlobalMorph.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace andor::analog
{

namespace
{
    constexpr double lowestHz = 70.0, highestHz = 14000.0;
    constexpr double bandQ = 3.0;                       // (each of the two cascaded band-passes)
    constexpr double minRatio = 0.0158489, maxRatio = 7.943;   // -36 dB .. +18 dB
    constexpr double energyFloor = 1.0e-10;             // (-100 dB: below it a band is empty)
    constexpr int controlPeriod = 32;                   // samples between the gains' targets
    constexpr double eqQ = 4.3;                         // (third-octave graphic EQ)
}

void GlobalMorph::Band::setPeak (double hz, double db, double sampleRate) noexcept
{
    if (std::abs (db) < 1.0e-4)
    {
        b0 = 1.0;
        b1 = b2 = a1 = a2 = 0.0;
        return;
    }
    const double A = std::pow (10.0, db / 40.0), w = 2.0 * std::numbers::pi * hz / sampleRate, alpha = std::sin (w) / (2.0 * eqQ), cw = std::cos (w);
    const double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0;
    b1 = -2.0 * cw / a0;
    b2 = (1.0 - alpha * A) / a0;
    a1 = -2.0 * cw / a0;
    a2 = (1.0 - alpha / A) / a0;
}

void GlobalMorph::prepare (double sampleRate, int)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double top = std::min (highestHz, 0.42 * rate);
    for (int b = 0; b < bandCount; ++b)
    {
        const double hz = lowestHz * std::pow (top / lowestHz, static_cast<double> (b) / (bandCount - 1));
        centres[static_cast<std::size_t> (b)] = hz;
        // RBJ band-pass, 0 dB at the centre.
        const double w = 2.0 * std::numbers::pi * hz / rate, alpha = std::sin (w) / (2.0 * bandQ), a0 = 1.0 + alpha;
        Biquad proto;
        proto.b0 = alpha / a0;
        proto.b1 = 0.0;
        proto.b2 = -alpha / a0;
        proto.a1 = -2.0 * std::cos (w) / a0;
        proto.a2 = (1.0 - alpha) / a0;
        auto& band = bands[static_cast<std::size_t> (b)];
        for (auto* chain : { &band.l, &band.r, &band.c })
            for (auto& f : *chain)
                f = proto;
    }
    attack = 1.0 - std::exp (-1.0 / (0.003 * rate));
    release = 1.0 - std::exp (-1.0 / (0.060 * rate));
    gainStep = 1.0 - std::exp (-static_cast<double> (controlPeriod) / (0.010 * rate));   // (per control period)
    positionStep = 1.0 / (0.015 * rate);   // (a linear glide: the endpoints are reached exactly)
    activityStep = 1.0 - std::exp (-static_cast<double> (controlPeriod) / (0.050 * rate));
    levelSmooth = 1.0 - std::exp (-1.0 / (0.080 * rate));
    reset();
}

void GlobalMorph::reset() noexcept
{
    for (auto& band : bands)
    {
        for (auto* chain : { &band.l, &band.r, &band.c })
            for (auto& f : *chain)
                f.z1 = f.z2 = 0.0;
        band.energyAb = band.energyC = 0.0;
        band.gainDb = band.targetDb = band.setDb = 0.0;
        band.setPeak (1000.0, 0.0, rate);
        band.xl1 = band.xl2 = band.yl1 = band.yl2 = band.xr1 = band.xr2 = band.yr1 = band.yr2 = 0.0;
    }
    shaping = false;
    outEnergy = abEnergy = cEnergy = 0.0;
    makeUpDb = makeUpTarget = 0.0;
    makeUp = 1.0;
    positionNow = 0.0;
    activityNow = 0.0;
    act = 0.0f;
    control = 0;
}

float GlobalMorph::bandGainDb (int band) const noexcept
{
    return static_cast<float> (bands[static_cast<std::size_t> (band)].gainDb);
}

void GlobalMorph::process (float* left, float* right, const float* cLeft, const float* cRight, int n, double position) noexcept
{
    position = std::clamp (position, 0.0, 1.0);
    for (int i = 0; i < n; ++i)
    {
        positionNow = positionNow < position ? std::min (position, positionNow + positionStep) : std::max (position, positionNow - positionStep);
        const double x = positionNow;
        const double ab[2] { left[i], right[i] };
        const double c[2] { cLeft[i], cRight[i] };
        const double cMono = 0.5 * (c[0] + c[1]);
        // Analysis: both buses' band energies.
        for (auto& band : bands)
        {
            const double yl = band.l[1].process (band.l[0].process (ab[0]));
            const double yr = band.r[1].process (band.r[0].process (ab[1]));
            const double yc = band.c[1].process (band.c[0].process (cMono));
            const double eAb = 0.5 * (yl * yl + yr * yr), eC = yc * yc;
            band.energyAb += (eAb > band.energyAb ? attack : release) * (eAb - band.energyAb);
            band.energyC += (eC > band.energyC ? attack : release) * (eC - band.energyC);
        }
        // The gains (every 32 samples): their targets from the imprint's depth and C's activity,
        // glided, and the graphic EQ's filters set from them.
        if (++control >= controlPeriod)
        {
            control = 0;
            double total = 0.0;
            for (const auto& band : bands)
                total += band.energyC;
            // Activity: 0 below -66 dBFS (C silent: the carrier left alone), 1 above -54 dBFS.
            const double level = 10.0 * std::log10 (std::max (total, 1.0e-20));
            const double activeTarget = std::clamp ((level + 66.0) / 12.0, 0.0, 1.0);
            activityNow += activityStep * (activeTarget - activityNow);
            act = static_cast<float> (activityNow);
            const double depth = std::min (1.0, 2.0 * x) * activityNow;
            // The make-up: the hybrid's energy steered to E_AB^(1-d) * E_C^d (only while shaping).
            if (depth > 0.0 && outEnergy > energyFloor && abEnergy > energyFloor)
            {
                const double want = std::exp ((1.0 - depth) * std::log (abEnergy) + depth * std::log (std::max (cEnergy, energyFloor)));
                makeUpTarget = std::clamp (10.0 * std::log10 (want / outEnergy), -12.0, 12.0);   // (outEnergy is before the make-up)
            }
            else
                makeUpTarget = 0.0;
            makeUpDb += gainStep * (makeUpTarget - makeUpDb);
            makeUp = std::pow (10.0, makeUpDb / 20.0);
            shaping = false;
            for (std::size_t b = 0; b < bands.size(); ++b)
            {
                auto& band = bands[b];
                if (band.energyAb < energyFloor)
                    band.targetDb = 0.0;   // (nothing of the carrier here to shape)
                else
                {
                    const double ratio = std::clamp (std::sqrt ((band.energyC + energyFloor) / (band.energyAb + energyFloor)), minRatio, maxRatio);
                    band.targetDb = depth * 20.0 * std::log10 (ratio);
                }
                band.gainDb += gainStep * (band.targetDb - band.gainDb);
                if (std::abs (band.gainDb) < 0.01 && std::abs (band.targetDb) < 0.01)
                    band.gainDb = 0.0;
                if (std::abs (band.gainDb - band.setDb) > 0.02 || (band.gainDb == 0.0 && band.setDb != 0.0))
                {
                    band.setDb = band.gainDb;
                    band.setPeak (centres[b], band.gainDb, rate);
                }
                shaping = shaping || band.setDb != 0.0;
            }
        }
        abEnergy += levelSmooth * (0.5 * (ab[0] * ab[0] + ab[1] * ab[1]) - abEnergy);
        cEnergy += levelSmooth * (0.5 * (c[0] * c[0] + c[1] * c[1]) - cEnergy);
        if (x <= 0.0)
        {
            outEnergy = abEnergy;
            continue;   // (0 %: the carrier exactly - the analysis keeps running)
        }
        // The hybrid: the carrier through the graphic EQ.
        double hL = ab[0], hR = ab[1];
        if (shaping)
            for (auto& band : bands)
            {
                if (band.setDb == 0.0)
                    continue;
                const double yl = band.b0 * hL + band.b1 * band.xl1 + band.b2 * band.xl2 - band.a1 * band.yl1 - band.a2 * band.yl2;
                band.xl2 = band.xl1;
                band.xl1 = hL;
                band.yl2 = band.yl1;
                band.yl1 = yl;
                const double yr = band.b0 * hR + band.b1 * band.xr1 + band.b2 * band.xr2 - band.a1 * band.yr1 - band.a2 * band.yr2;
                band.xr2 = band.xr1;
                band.xr1 = hR;
                band.yr2 = band.yr1;
                band.yr1 = yr;
                hL = yl;
                hR = yr;
            }
        // (its energy before the make-up: what the make-up corrects)
        outEnergy += levelSmooth * (0.5 * (hL * hL + hR * hR) - outEnergy);
        hL *= makeUp;
        hR *= makeUp;
        if (x <= 0.5)
        {
            left[i] = static_cast<float> (hL);
            right[i] = static_cast<float> (hR);
        }
        else if (x >= 1.0)
        {
            left[i] = static_cast<float> (c[0]);
            right[i] = static_cast<float> (c[1]);
        }
        else
        {
            const double t = (2.0 * x - 1.0) * 0.5 * std::numbers::pi;
            const double toHybrid = std::cos (t), toC = std::sin (t);
            left[i] = static_cast<float> (toHybrid * hL + toC * c[0]);
            right[i] = static_cast<float> (toHybrid * hR + toC * c[1]);
        }
    }
}

} // namespace andor::analog
