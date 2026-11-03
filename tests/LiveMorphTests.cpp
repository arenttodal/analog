// MORPH's live core (morph::LiveMorph): two sources heard as they play and turned into one.
// The ends are the sources exactly; each side's harmonics and residual are measured; the
// middle is a fusion (not a crossfade) of their partials, its pitch between theirs; a noise
// source brings its noise; silence, NaN and moves are safe; deterministic.

#include "morph/LiveMorph.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <functional>
#include <vector>

using Catch::Approx;
namespace morph = andor::analog::morph;
using andor::analog::MorphMethod;

namespace
{
    constexpr double internal = 192000.0, host = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    const morph::LiveMorph::Shared& shared()
    {
        static const morph::LiveMorph::Shared s;
        return s;
    }

    using Source = std::function<float (int)>;   // sample n at the internal rate

    /** A band-limited wave (harmonics up to 20 kHz, as the synth's oscillators are), from a
        one-period table: `odd` only for a square. */
    Source bandlimited (double hz, float a, bool squareWave)
    {
        constexpr int size = 4096;
        auto table = std::make_shared<std::vector<float>> (size + 1);
        for (int i = 0; i <= size; ++i)
        {
            double v = 0.0;
            for (int h = 1; h * hz < 20000.0; ++h)
            {
                if (squareWave && h % 2 == 0)
                    continue;
                // saw: 2 / (pi h) sin; square: 4 / (pi h) sin (odd h)
                v += (squareWave ? 4.0 : 2.0) / (pi * h) * std::sin (2.0 * pi * h * i / size);
            }
            (*table)[static_cast<std::size_t> (i)] = static_cast<float> (v);
        }
        return [table, hz, a] (int n) {
            const double t = n * hz / internal;
            const double x = (t - std::floor (t)) * size;
            const auto i = static_cast<std::size_t> (x);
            const auto f = static_cast<float> (x - static_cast<double> (i));
            return a * ((*table)[i] * (1.0f - f) + (*table)[i + 1] * f);
        };
    }
    Source saw (double hz, float a = 0.5f) { return bandlimited (hz, a, false); }
    Source square (double hz, float a = 0.5f) { return bandlimited (hz, a, true); }
    Source sine (double hz, float a = 0.5f)
    {
        return [hz, a] (int n) { return a * static_cast<float> (std::sin (2.0 * pi * hz * n / internal)); };
    }
    Source whiteNoise (float a = 0.3f)
    {
        auto rng = std::make_shared<osp::Prng> (77);
        return [rng, a] (int) { return a * static_cast<float> (rng->bipolar()); };
    }

    morph::LiveMorph::Control control (double position, double fx, double fy, MorphMethod method = MorphMethod::fuse)
    {
        morph::LiveMorph::Control c;
        c.position = position;
        c.method = method;
        c.carrierHz = fx;
        c.sourceHz = fy;
        c.attack = 0.0;
        return c;
    }

    /** Runs the morph `seconds`, the control every 32 host samples (as the synth does);
        returns the output at the host rate (every fourth sample). `move` may change the
        control as time goes (seconds). */
    std::vector<float> run (morph::LiveMorph& m, const Source& x, const Source& y, morph::LiveMorph::Control c, double seconds,
                            const std::function<void (morph::LiveMorph::Control&, double)>& move = {}, std::uint64_t seed = 5)
    {
        m.prepare (internal, 4, &shared());
        m.start (seed);
        std::vector<float> out;
        const int total = static_cast<int> (seconds * internal);
        for (int n = 0; n < total; ++n)
        {
            if (n % 128 == 0)
            {
                if (move)
                    move (c, n / internal);
                m.control (c);
            }
            const float v = m.process (x (n), y (n));
            if (n % 4 == 3)
                out.push_back (v);
        }
        return out;
    }

    /** Level (dB) of the strongest component within 3 % of `hz` in the last `seconds`. */
    double levelAt (const std::vector<float>& s, double hz, double seconds = 0.4)
    {
        const auto n = static_cast<std::size_t> (seconds * host);
        const std::size_t from = s.size() - n;
        double best = 0.0;
        for (double f = hz * 0.97; f <= hz * 1.03; f += hz * 0.002)
        {
            std::complex<double> acc;
            for (std::size_t i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (n));
                acc += w * s[from + i] * std::polar (1.0, -2.0 * pi * f * static_cast<double> (i) / host);
            }
            best = std::max (best, std::abs (acc) * 4.0 / static_cast<double> (n));
        }
        return 20.0 * std::log10 (best + 1.0e-12);
    }

    double rmsDb (const std::vector<float>& s, double seconds = 0.4)
    {
        const auto n = static_cast<std::size_t> (seconds * host);
        double sum = 0.0;
        for (std::size_t i = s.size() - n; i < s.size(); ++i)
            sum += static_cast<double> (s[i]) * s[i];
        return 10.0 * std::log10 (sum / static_cast<double> (n) + 1.0e-20);
    }
}

TEST_CASE ("live morph: at 0 % the carrier, at 100 % card C, sample for sample (every method)", "[morph][live][endpoints]")
{
    for (int method = 0; method < 3; ++method)
        for (double p : { 0.0, 1.0 })
        {
            morph::LiveMorph m;
            m.prepare (internal, 4, &shared());
            m.start (3);
            auto c = control (p, 220.0, 330.0, static_cast<MorphMethod> (method));
            const auto x = saw (220.0), y = square (330.0);
            double worst = 0.0;
            for (int n = 0; n < static_cast<int> (0.5 * internal); ++n)
            {
                if (n % 128 == 0)
                    m.control (c);
                const float expected = p == 0.0 ? x (n) : y (n);
                worst = std::max (worst, static_cast<double> (std::abs (m.process (x (n), y (n)) - expected)));
            }
            INFO ("method " << method << " position " << p);
            CHECK (worst == 0.0);
        }
}

TEST_CASE ("live morph: each side's harmonics are measured as they play (a saw's 1/h, a square's odd ones)", "[morph][live][analysis]")
{
    morph::LiveMorph m;
    run (m, saw (220.0, 0.5f), square (330.0, 0.5f), control (0.5, 220.0, 330.0), 0.3);
    const auto& x = m.carrierPartialsDb();
    const auto& y = m.sourcePartialsDb();
    for (int h = 1; h <= 10; ++h)
    {
        INFO ("harmonic " << h);
        // A saw of amplitude 0.5: harmonic h at 0.5 * 2 / (pi h).
        CHECK (x[static_cast<std::size_t> (h - 1)] == Approx (20.0 * std::log10 (1.0 / (pi * h))).margin (1.0));
    }
    // The square (amplitude 0.5): odd harmonics at 0.5 * 4 / (pi h), even ones far below.
    CHECK (y[0] == Approx (20.0 * std::log10 (2.0 / pi)).margin (1.0));
    CHECK (y[2] == Approx (20.0 * std::log10 (2.0 / (3.0 * pi))).margin (1.0));
    CHECK (y[1] < y[0] - 40.0f);
    CHECK (y[3] < y[2] - 40.0f);
}

TEST_CASE ("live morph: the middle fuses the partials (a saw into a square: the even ones fade in dB, not mixed)", "[morph][live][fusion]")
{
    morph::LiveMorph m;
    const auto out = run (m, saw (220.0), square (220.0), control (0.5, 220.0, 220.0), 1.0);
    REQUIRE (m.coreReady());
    const double h1 = levelAt (out, 220.0), h2 = levelAt (out, 440.0), h3 = levelAt (out, 660.0);
    INFO ("h1 " << h1 << " h2 " << h2 << " h3 " << h3);
    // A crossfade would hold the saw's second harmonic at -15.6 dB under the first; the fusion
    // puts it half way, in dB, between the saw's -6 dB and the square's absence.
    CHECK (h2 - h1 < -25.0);
    CHECK (h2 - h1 > -55.0);
    // The odd harmonics both have: half way between the saw's and the square's.
    const double sawH3 = 20.0 * std::log10 (1.0 / (3.0 * pi)), squareH3 = 20.0 * std::log10 (2.0 / (3.0 * pi));
    const double sawH1 = 20.0 * std::log10 (1.0 / pi), squareH1 = 20.0 * std::log10 (2.0 / pi);
    CHECK ((h3 - h1) == Approx (0.5 * (sawH3 + squareH3) - 0.5 * (sawH1 + squareH1)).margin (2.0));
}

TEST_CASE ("live morph: the pitch morphs too (an octave apart: the fifth-and-a-bit between at 50 %)", "[morph][live][pitch]")
{
    morph::LiveMorph m;
    const auto out = run (m, saw (220.0), saw (440.0), control (0.5, 220.0, 440.0), 1.0);
    CHECK (m.morphFundamental() == Approx (220.0 * std::sqrt (2.0)).epsilon (0.001));
    // The core's fundamental sounds; IMPRINT keeps the carrier's pitch.
    CHECK (levelAt (out, 220.0 * std::sqrt (2.0)) > levelAt (out, 330.0) + 20.0);
    morph::LiveMorph imprint;
    run (imprint, saw (220.0), saw (440.0), control (0.5, 220.0, 440.0, MorphMethod::imprint), 0.5);
    CHECK (imprint.morphFundamental() == Approx (220.0));
}

TEST_CASE ("live morph: a noise source brings its noise; silence and NaN are safe", "[morph][live][noise]")
{
    {
        // Card C as NOISE: the morph of a saw and noise has noise between the saw's harmonics.
        morph::LiveMorph withNoise, alone;
        const auto a = run (withNoise, saw (220.0), whiteNoise(), control (0.5, 220.0, 220.0), 1.0);
        const auto b = run (alone, saw (220.0), [] (int) { return 0.0f; }, control (0.5, 220.0, 220.0), 1.0);
        INFO ("between harmonics: with noise " << levelAt (a, 330.0) << ", without " << levelAt (b, 330.0));
        CHECK (levelAt (a, 330.0) > levelAt (b, 330.0) + 20.0);
        CHECK (withNoise.sourceResidualDb()[4] > -60.0f);   // (the 2 kHz band)
    }
    {
        // Card C silent: finite, the carrier's partials (quieter, as the level between says).
        morph::LiveMorph m;
        const auto out = run (m, saw (220.0), [] (int) { return 0.0f; }, control (0.5, 220.0, 220.0), 0.6);
        for (float v : out)
            REQUIRE (std::isfinite (v));
        CHECK (m.morphFundamental() == Approx (220.0));
    }
    {
        morph::LiveMorph m;
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const auto out = run (m, [nan] (int n) { return n % 1000 == 0 ? nan : 0.3f; }, saw (330.0), control (0.5, 220.0, 330.0), 0.4);
        for (float v : out)
            REQUIRE (std::isfinite (v));
    }
}

TEST_CASE ("live morph: a note begins on the sources as played, then glides into the morph", "[morph][live][attack]")
{
    morph::LiveMorph m;
    m.prepare (internal, 4, &shared());
    m.start (9);
    auto c = control (0.5, 220.0, 220.0);
    c.attack = 0.5;   // 75 ms on top of the window
    const auto x = saw (220.0), y = square (220.0);
    double worstEarly = 0.0;
    for (int n = 0; n < static_cast<int> (0.3 * internal); ++n)
    {
        if (n % 128 == 0)
            m.control (c);
        const float v = m.process (x (n), y (n));
        if (n < static_cast<int> (0.02 * internal))
            worstEarly = std::max (worstEarly, static_cast<double> (std::abs (v - 0.5f * (x (n) + y (n)))));
    }
    CHECK (worstEarly < 1.0e-6);   // the first 20 ms: exactly the sources, half and half
    CHECK (m.coreReady());
}

TEST_CASE ("live morph: LEVEL MATCH holds the loudness between the two; moving the position never clicks", "[morph][live][smooth]")
{
    {
        morph::LiveMorph m;
        const auto out = run (m, saw (220.0, 0.5f), square (220.0, 0.25f), control (0.5, 220.0, 220.0), 1.0);
        // The saw at 0.5 (-9.5 dB RMS) and the square at 0.25 (-12 dB RMS): about -10.8 dB between.
        CHECK (rmsDb (out) == Approx (-10.8).margin (2.5));
    }
    {
        // Sines, the position swept 0 .. 1 over a second: no step larger than the sources' own.
        morph::LiveMorph m;
        const auto out = run (m, sine (220.0), sine (330.0), control (0.0, 220.0, 330.0), 1.4,
                              [] (morph::LiveMorph::Control& c, double t) { c.position = std::clamp ((t - 0.2) / 1.0, 0.0, 1.0); });
        double worst = 0.0;
        for (std::size_t i = 1; i < out.size(); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (out[i] - out[i - 1])));
        const double sourceStep = 2.0 * pi * 330.0 / host * 0.5;
        INFO ("largest step " << worst << " against the sources' " << sourceStep);
        CHECK (worst < 3.0 * sourceStep);
    }
}

TEST_CASE ("live morph: the same note gives the same output (seeded noise and WEAVE order)", "[morph][live][regression]")
{
    for (int method = 0; method < 3; ++method)
    {
        morph::LiveMorph a, b;
        const auto c = control (0.4, 220.0, 330.0, static_cast<MorphMethod> (method));
        CHECK (run (a, saw (220.0), whiteNoise(), c, 0.4) == run (b, saw (220.0), whiteNoise(), c, 0.4));
    }
}
