// EXPERIMENT: GLOBAL MORPH between the A + B bus and card C's external instrument's bus
// (docs/experiments/KONTAKT_EXTERNAL_MORPH.md). Headless: synthetic stereo buses.

#include "morph/GlobalMorph.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

using andor::analog::GlobalMorph;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;

    struct Bus
    {
        std::vector<float> l, r;
    };

    /** A + B: a bright saw chord (C3 E3 G3), slightly wide. */
    Bus carrier (double seconds)
    {
        Bus b;
        const auto n = static_cast<std::size_t> (seconds * rate);
        b.l.resize (n);
        b.r.resize (n);
        std::array<double, 3> phase {};
        const std::array<double, 3> hz { 130.81, 164.81, 196.0 };
        for (std::size_t i = 0; i < n; ++i)
        {
            double l = 0.0, r = 0.0;
            for (std::size_t k = 0; k < 3; ++k)
            {
                phase[k] += hz[k] * (k == 1 ? 1.003 : 1.0) / rate;
                phase[k] -= std::floor (phase[k]);
                const double saw = 2.0 * phase[k] - 1.0;
                l += saw * (k == 2 ? 0.6 : 1.0);
                r += saw * (k == 0 ? 0.6 : 1.0);
            }
            b.l[i] = static_cast<float> (0.08 * l);
            b.r[i] = static_cast<float> (0.08 * r);
        }
        return b;
    }

    /** C: a dark, vowel-like pad (A2, a formant near 700 Hz, little above 2 kHz). */
    Bus source (double seconds, float gain = 1.0f)
    {
        Bus b;
        const auto n = static_cast<std::size_t> (seconds * rate);
        b.l.resize (n);
        b.r.resize (n);
        double phase = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            phase += 110.0 / rate;
            double s = 0.0;
            for (int h = 1; h <= 30; ++h)
            {
                const double f = 110.0 * h;
                const double formant = std::exp (-0.5 * std::pow ((f - 700.0) / 180.0, 2.0)) + 0.3 * std::exp (-0.5 * std::pow ((f - 220.0) / 80.0, 2.0));
                s += formant * std::sin (2.0 * std::numbers::pi * phase * h) / std::sqrt (h);
            }
            b.l[i] = static_cast<float> (gain * 0.12 * s);
            b.r[i] = static_cast<float> (gain * 0.12 * s);
        }
        return b;
    }

    Bus morphed (const Bus& ab, const Bus& c, double position)
    {
        GlobalMorph m;
        m.prepare (rate, block);
        Bus out = ab;
        for (std::size_t at = 0; at < out.l.size(); at += block)
        {
            const int n = static_cast<int> (std::min<std::size_t> (block, out.l.size() - at));
            m.process (out.l.data() + at, out.r.data() + at, c.l.data() + at, c.r.data() + at, n, position);
        }
        return out;
    }

    /** Each third-octave band's level (dB) in the mono sum, over a window (Goertzel-free: a band-pass bank). */
    std::vector<double> profile (const Bus& b, double from, double to)
    {
        std::vector<double> out;
        for (int k = 0; k < 20; ++k)
        {
            const double hz = 100.0 * std::pow (2.0, k / 3.0);
            // RBJ band-pass, twice.
            const double w = 2.0 * std::numbers::pi * hz / rate, alpha = std::sin (w) / (2.0 * 4.3), a0 = 1.0 + alpha;
            const double b0 = alpha / a0, b2 = -alpha / a0, a1 = -2.0 * std::cos (w) / a0, a2 = (1.0 - alpha) / a0;
            double z1 = 0, z2 = 0, y1 = 0, y2 = 0, energy = 0.0;
            for (std::size_t i = 0; i < static_cast<std::size_t> (to * rate); ++i)
            {
                const double x = 0.5 * (b.l[i] + b.r[i]);
                const double y = b0 * x + z1;
                z1 = -a1 * y + z2;
                z2 = b2 * x - a2 * y;
                const double u = b0 * y + y1;
                y1 = -a1 * u + y2;
                y2 = b2 * y - a2 * u;
                if (i >= static_cast<std::size_t> (from * rate))
                    energy += u * u;
            }
            out.push_back (10.0 * std::log10 (energy + 1.0e-20));
        }
        return out;
    }

    /** The shapes' distance: RMS difference of two profiles after removing their mean (level). */
    double shapeDistance (std::vector<double> a, std::vector<double> b)
    {
        double ma = 0.0, mb = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            ma += a[i];
            mb += b[i];
        }
        ma /= static_cast<double> (a.size());
        mb /= static_cast<double> (b.size());
        double d = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            d += std::pow ((a[i] - ma) - (b[i] - mb), 2.0);
        return std::sqrt (d / static_cast<double> (a.size()));
    }

    double rms (const Bus& b, double from, double to)
    {
        double s = 0.0;
        const auto x = static_cast<std::size_t> (from * rate), z = static_cast<std::size_t> (to * rate);
        for (auto i = x; i < z; ++i)
            s += 0.5 * (static_cast<double> (b.l[i]) * b.l[i] + static_cast<double> (b.r[i]) * b.r[i]);
        return std::sqrt (s / static_cast<double> (z - x));
    }
}

TEST_CASE ("GLOBAL MORPH: 0 % is A + B exactly, 100 % is C exactly", "[globalmorph][unit]")
{
    const auto ab = carrier (1.0), c = source (1.0);
    const auto at0 = morphed (ab, c, 0.0), at1 = morphed (ab, c, 1.0);
    CHECK (at0.l == ab.l);
    CHECK (at0.r == ab.r);
    // (100 %: from the first samples the position glides 15 ms from 0; after it, C exactly)
    for (std::size_t i = static_cast<std::size_t> (0.2 * rate); i < c.l.size(); ++i)
    {
        REQUIRE (at1.l[i] == c.l[i]);
        REQUIRE (at1.r[i] == c.r[i]);
    }
}

TEST_CASE ("GLOBAL MORPH: 50 % - A + B take C's spectral envelope (not a crossfade), at C's level", "[globalmorph][unit]")
{
    const auto ab = carrier (2.0), c = source (2.0);
    const auto half = morphed (ab, c, 0.5);
    const auto pAb = profile (ab, 1.0, 2.0), pC = profile (c, 1.0, 2.0), pHalf = profile (half, 1.0, 2.0);
    // The crossfade a mixer would make at the middle (equal power).
    Bus mix = ab;
    for (std::size_t i = 0; i < mix.l.size(); ++i)
    {
        mix.l[i] = static_cast<float> (std::sqrt (0.5) * (ab.l[i] + c.l[i]));
        mix.r[i] = static_cast<float> (std::sqrt (0.5) * (ab.r[i] + c.r[i]));
    }
    const auto pMix = profile (mix, 1.0, 2.0);
    const double toC = shapeDistance (pHalf, pC), toAb = shapeDistance (pHalf, pAb), mixToC = shapeDistance (pMix, pC);
    INFO ("spectral shape distance (dB): hybrid-C " << toC << ", hybrid-A+B " << toAb << ", crossfade-C " << mixToC << ", A+B-C " << shapeDistance (pAb, pC)
                                                    << "; hybrid-crossfade " << shapeDistance (pHalf, pMix));
    CHECK (toC < 0.5 * toAb);                       // (its envelope is C's, not A + B's)
    CHECK (toC < 0.7 * mixToC);                     // (closer to C's envelope than a crossfade gets)
    CHECK (shapeDistance (pHalf, pMix) > 3.0);      // (measurably not the crossfade)
    // Its fine structure is A + B's: C's own 110 Hz fundamental (A2) is barely in it, the
    // carrier's C3 (131 Hz) is.
    auto bandAt = [] (const std::vector<double>& p, double hz) { return p[static_cast<std::size_t> (std::lround (3.0 * std::log2 (hz / 100.0)))]; };
    CHECK (bandAt (pHalf, 125.99) - bandAt (pC, 125.99) > -12.0);
    // Level: within 3 dB of C's (the imprint matches C's band energies).
    const double level = 20.0 * std::log10 (rms (half, 1.0, 2.0) / rms (c, 1.0, 2.0));
    INFO ("50 % against C: " << level << " dB");
    CHECK (std::abs (level) < 3.0);
}

TEST_CASE ("GLOBAL MORPH: the sweep's loudness is controlled; the carrier's stereo image is kept; a silent C leaves A + B alone", "[globalmorph][unit]")
{
    const auto ab = carrier (1.5), c = source (1.5);
    const double lo = std::min (rms (ab, 0.5, 1.5), rms (c, 0.5, 1.5)), hi = std::max (rms (ab, 0.5, 1.5), rms (c, 0.5, 1.5));
    for (const double x : { 0.25, 0.5, 0.75 })
    {
        const double level = rms (morphed (ab, c, x), 0.5, 1.5);
        INFO ("position " << x << ": " << 20.0 * std::log10 (level / hi) << " dB against the louder bus");
        CHECK (level < 1.41 * hi);   // (never more than +3 dB over the louder)
        CHECK (level > 0.5 * lo);    // (never a hole)
    }
    // A carrier on the left only stays on the left at 50 %.
    Bus left = ab;
    std::fill (left.r.begin(), left.r.end(), 0.0f);
    const auto image = morphed (left, c, 0.5);
    double l = 0.0, r = 0.0;
    for (std::size_t i = static_cast<std::size_t> (0.5 * rate); i < image.l.size(); ++i)
    {
        l += std::abs (image.l[i]);
        r += std::abs (image.r[i]);
    }
    CHECK (r < 1.0e-6 * l);
    // C silent: at 50 % (the hybrid) the carrier stays itself (no shaping by nothing, no noise).
    const auto silentC = source (1.5, 0.0f);
    const auto alone = morphed (ab, silentC, 0.5);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = static_cast<std::size_t> (0.3 * rate); i < alone.l.size(); ++i)
    {
        diff += std::pow (alone.l[i] - ab.l[i], 2.0);
        ref += std::pow (ab.l[i], 2.0);
    }
    CHECK (diff < 1.0e-6 * ref);
}

TEST_CASE ("GLOBAL MORPH: cost (one stereo pair, 48 kHz)", "[.][globalmorph-cpu]")
{
    const auto ab = carrier (10.0), c = source (10.0);
    for (const double x : { 0.0, 0.25, 0.5, 0.75 })
    {
        const auto t0 = std::chrono::steady_clock::now();
        morphed (ab, c, x);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        WARN ("position " << x << ": " << 100.0 * seconds / 10.0 << " % of one core");
    }
}
