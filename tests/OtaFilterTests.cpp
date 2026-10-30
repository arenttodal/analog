// The OTA-cascade low-pass (Juno / Jupiter calibrations, 24 and 12 dB/oct) and the per-voice
// high-pass: tuning, slopes, the resonance law, self-oscillation, stability at the extremes,
// determinism, and the high-pass positions and their switching.

#include "HighPassFilter.h"
#include "OtaFilter.h"

#include "core/Fft.h"
#include "core/Prng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

using namespace andor::analog;
using Catch::Approx;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double rate = 192000.0;

    float gFor (double hz) { return static_cast<float> (std::tan (pi * hz / rate)); }
    double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }
    const char* name (OtaCharacter c) { return c == OtaCharacter::jupiter ? "jupiter" : "juno"; }

    /** The filter's small-signal frequency response, from the FFT of its response to a small
        impulse (1e-3: every curve is linear to better than 1e-6 there). 2^18 samples at
        192 kHz: 0.73 Hz per bin, long enough for the impulse response to decay below -80 dB
        at the resonances measured here. */
    struct Response
    {
        std::vector<double> mag;
        double binHz = 0.0;

        double db (double hz) const
        {
            const double b = hz / binHz;
            const auto i = static_cast<std::size_t> (b);
            const double t = b - static_cast<double> (i);
            return 20.0 * std::log10 ((1.0 - t) * mag[i] + t * mag[i + 1]);
        }

        /** The first frequency above `from` where the response falls through `targetDb`. */
        double crossing (double targetDb, double from) const
        {
            for (auto i = static_cast<std::size_t> (from / binHz) + 1; i + 1 < mag.size(); ++i)
            {
                const double a = 20.0 * std::log10 (mag[i - 1]), z = 20.0 * std::log10 (mag[i]);
                if (a >= targetDb && z < targetDb)
                    return binHz * (static_cast<double> (i - 1) + (a - targetDb) / (a - z));
            }
            return 0.0;
        }

        /** The highest point (dB) and where it is, within lo..hi Hz. */
        std::pair<double, double> peak (double lo, double hi) const
        {
            double best = -1.0e9, where = 0.0;
            for (auto i = static_cast<std::size_t> (lo / binHz); i < static_cast<std::size_t> (hi / binHz); ++i)
                if (20.0 * std::log10 (mag[i]) > best)
                {
                    best = 20.0 * std::log10 (mag[i]);
                    where = binHz * static_cast<double> (i);
                }
            return { best, where };
        }
    };

    Response otaResponse (double fc, float resonance, OtaCharacter character, bool twoPole)
    {
        constexpr int order = 18;
        osp::Fft fft (order);
        const int n = fft.size();
        std::vector<std::complex<double>> data (static_cast<std::size_t> (n));
        OtaFilter f;
        const float g = gFor (fc);
        constexpr float impulse = 1.0e-3f;
        for (int i = 0; i < n; ++i)
            data[static_cast<std::size_t> (i)] = f.process (i == 0 ? impulse : 0.0f, g, resonance, character, twoPole) / impulse;
        fft.forward (data.data());
        Response r;
        r.binHz = rate / n;
        r.mag.resize (static_cast<std::size_t> (n / 2));
        for (std::size_t i = 0; i < r.mag.size(); ++i)
            r.mag[i] = std::abs (data[i]);
        return r;
    }

    /** Frequency from rising zero crossings (linear interpolation) in x[a .. z). */
    double frequencyByCrossings (const std::vector<float>& x, std::size_t a, std::size_t z)
    {
        double first = -1.0, last = -1.0;
        int count = 0;
        for (std::size_t i = a + 1; i < z; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = static_cast<double> (i - 1) + x[i - 1] / static_cast<double> (x[i - 1] - x[i]);
                if (first < 0.0)
                    first = t;
                else
                    ++count;
                last = t;
            }
        return count > 0 ? rate * count / (last - first) : 0.0;
    }

    double rmsOf (const std::vector<float>& x, std::size_t a, std::size_t z)
    {
        double sum = 0.0;
        for (std::size_t i = a; i < z; ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return std::sqrt (sum / static_cast<double> (z - a));
    }

    /** The high-pass's steady-state gain (dB) for a sine, measured over whole cycles. */
    double hpfGainDb (VoiceHighPass& hpf, double hz)
    {
        hpf.reset();
        const int settle = static_cast<int> (rate * 0.2);
        const int cycles = std::max (10, static_cast<int> (hz * 0.25));
        const int n = static_cast<int> (std::lround (cycles * rate / hz));
        double in = 0.0, out = 0.0;
        for (int i = 0; i < settle + n; ++i)
        {
            const auto x = static_cast<float> (0.5 * std::sin (2.0 * pi * hz * i / rate));
            const float y = hpf.process (x);
            if (i >= settle)
            {
                in += static_cast<double> (x) * x;
                out += static_cast<double> (y) * y;
            }
        }
        return 10.0 * std::log10 (out / in);
    }
}

//==============================================================================
TEST_CASE ("ota: the cutoff sits at fc at resonance 0 (each stage -3 dB there)", "[filter][ota]")
{
    // Convention (as LadderFilter): fc is the stages' corner and where the resonance rings, so
    // at resonance 0 the 4-pole output is -12.04 dB at fc (four stages at -3.01 dB) and the
    // 2-pole tap -6.02 dB; the overall -3 dB point is then 0.435 fc (4-pole), 0.644 fc (2-pole).
    for (const auto character : { OtaCharacter::juno, OtaCharacter::jupiter })
        for (const bool twoPole : { false, true })
            for (const double fc : { 100.0, 1000.0, 5000.0 })
            {
                const auto r = otaResponse (fc, 0.0f, character, twoPole);
                const double poles = twoPole ? 2.0 : 4.0;
                const double atCorner = r.crossing (-3.0103 * poles, fc * 0.1);
                const double minus3 = r.crossing (-3.0103, fc * 0.05);
                const double expected3 = fc * std::sqrt (std::pow (2.0, 1.0 / poles) - 1.0);
                INFO (name (character) << (twoPole ? " 2-pole" : " 4-pole") << " fc " << fc << ": stage corner at "
                                       << atCorner << " Hz (" << cents (atCorner, fc) << " cents), overall -3 dB at " << minus3 << " Hz");
                CHECK (std::abs (cents (atCorner, fc)) < 100.0);
                CHECK (std::abs (cents (minus3, expected3)) < 100.0);
                CHECK (r.db (fc * 0.05) == Approx (0.0).margin (0.1));   // unity pass band
            }
}

TEST_CASE ("ota: 24 dB/oct above the cutoff, 12 dB/oct from the 2-pole tap", "[filter][ota]")
{
    for (const auto character : { OtaCharacter::juno, OtaCharacter::jupiter })
        for (const double fc : { 100.0, 1000.0 })
        {
            INFO (name (character) << " fc " << fc);
            // Three to four octaves above fc, where each pole is within 0.1 dB of 6 dB/oct.
            const auto four = otaResponse (fc, 0.0f, character, false);
            const auto two = otaResponse (fc, 0.0f, character, true);
            const double slope4 = four.db (8.0 * fc) - four.db (16.0 * fc);
            const double slope2 = two.db (8.0 * fc) - two.db (16.0 * fc);
            INFO ("4-pole " << slope4 << " dB/oct, 2-pole " << slope2 << " dB/oct");
            CHECK (slope4 == Approx (24.0).margin (1.0));
            CHECK (slope2 == Approx (12.0).margin (0.6));
            // One to two octaves above (the poles not yet asymptotic: 21.3 and 10.6 dB in theory).
            CHECK (four.db (2.0 * fc) - four.db (4.0 * fc) == Approx (21.3).margin (1.0));
            CHECK (two.db (2.0 * fc) - two.db (4.0 * fc) == Approx (10.6).margin (0.6));
        }
}

TEST_CASE ("ota: the resonance peak grows with resonance and closes on fc; JUPITER above JUNO", "[filter][ota]")
{
    // Below self-oscillation the 4-pole's magnitude peak sits a little under fc (the low-pass
    // tilts it) and reaches fc as the loop gain approaches 4; the tuning at fc itself is what
    // the self-oscillation test measures.
    const double fc = 1000.0;
    double previous[2] = { -1.0e9, -1.0e9 }, previousOffset[2] = { 1.0e9, 1.0e9 };
    for (const float res : { 0.0f, 0.2f, 0.4f, 0.6f, 0.7f, 0.8f, 0.85f })
    {
        double peaks[2] {};
        for (const auto character : { OtaCharacter::juno, OtaCharacter::jupiter })
        {
            const int c = character == OtaCharacter::jupiter ? 1 : 0;
            const auto r = otaResponse (fc, res, character, false);
            const auto [peakDb, where] = r.peak (0.3 * fc, 3.0 * fc);
            // The emphasis: the response at fc relative to the pass band (fc / 20).
            const double emphasis = r.db (fc) - r.db (fc * 0.05);
            INFO (name (character) << " resonance " << res << ": peak " << peakDb << " dB at " << where << " Hz; "
                                   << "fc vs pass band " << emphasis << " dB");
            CHECK (emphasis > previous[c]);
            previous[c] = emphasis;
            peaks[c] = peakDb;
            if (res >= 0.6f)
            {
                const double offset = std::abs (cents (where, fc));
                CHECK (offset < previousOffset[c]);   // the peak closes on fc
                previousOffset[c] = offset;
            }
            if (res >= 0.85f)
                CHECK (std::abs (cents (where, fc)) < 100.0);
        }
        if (res > 0.0f)
            CHECK (peaks[1] > peaks[0]);
    }
}

TEST_CASE ("ota: the pass band follows the compensation law (1 + comp k) / (1 + k)", "[filter][ota]")
{
    // At resonance 0.8: JUNO k = 4.2 * 0.8 * (0.6 + 0.4 * 0.8) = 3.0912, comp 0.25;
    // JUPITER k = 4.6 * 0.8 * (0.85 + 0.15 * 0.8) = 3.5696, comp 0.4.
    const double kJuno = 3.0912, kJupiter = 3.5696;
    const double juno = 20.0 * std::log10 ((1.0 + 0.25 * kJuno) / (1.0 + kJuno));
    const double jupiter = 20.0 * std::log10 ((1.0 + 0.4 * kJupiter) / (1.0 + kJupiter));
    const double measuredJuno = otaResponse (2000.0, 0.8f, OtaCharacter::juno, false).db (20.0);
    const double measuredJupiter = otaResponse (2000.0, 0.8f, OtaCharacter::jupiter, false).db (20.0);
    INFO ("pass band at resonance 0.8: juno " << measuredJuno << " dB (law " << juno << "), jupiter " << measuredJupiter << " dB (law " << jupiter << ")");
    CHECK (measuredJuno == Approx (juno).margin (0.1));
    CHECK (measuredJupiter == Approx (jupiter).margin (0.1));
    CHECK (measuredJuno > 20.0 * std::log10 (1.0 / (1.0 + kJuno)) + 4.0);   // partly restored
}

TEST_CASE ("ota: at resonance 1 it sings by itself, tuned, at a steady bounded level", "[filter][ota]")
{
    for (const auto character : { OtaCharacter::juno, OtaCharacter::jupiter })
        for (const bool twoPole : { false, true })
            for (const double fc : { 100.0, 300.0, 1000.0, 3000.0, 5000.0 })
            {
                OtaFilter f;
                const float g = gFor (fc);
                const auto n = static_cast<std::size_t> (rate * 3.0);
                std::vector<float> y (n);
                bool finite = true;
                for (std::size_t i = 0; i < n; ++i)
                {
                    y[i] = f.process (i == 0 ? 0.01f : 0.0f, g, 1.0f, character, twoPole);
                    finite = finite && std::isfinite (y[i]);
                }
                const auto second = static_cast<std::size_t> (rate);
                const double freq = frequencyByCrossings (y, n - second, n);
                const double late = rmsOf (y, n - second / 2, n), earlier = rmsOf (y, n - second, n - second / 2);
                float peak = 0.0f;
                for (const float v : y)
                    peak = std::max (peak, std::abs (v));
                INFO (name (character) << (twoPole ? " 2-pole" : " 4-pole") << " fc " << fc << ": rings at " << freq << " Hz ("
                                       << cents (freq, fc) << " cents), rms " << late << ", peak " << peak);
                CHECK (finite);
                CHECK (std::abs (cents (freq, fc)) < 50.0);
                CHECK (late > 0.02);                               // it does not die out
                CHECK (peak < 0.5f);                               // nor blow up
                CHECK (late == Approx (earlier).epsilon (0.02));   // and it has settled
            }
}

TEST_CASE ("ota: no NaN, no Inf, bounded output at the extremes", "[filter][ota]")
{
    for (const auto character : { OtaCharacter::juno, OtaCharacter::jupiter })
        for (const bool twoPole : { false, true })
        {
            INFO (name (character) << (twoPole ? " 2-pole" : " 4-pole"));
            bool finite = true;
            float peak = 0.0f;
            auto run = [&] (OtaFilter& f, float x, float g, float res) {
                const float y = f.process (x, g, res, character, twoPole);
                finite = finite && std::isfinite (y);
                peak = std::max (peak, std::abs (y));
            };

            // A full-scale (and an 8x) square at 20 kHz, resonance 1, at several cutoffs.
            for (const float amplitude : { 1.0f, 8.0f })
                for (const double fc : { 50.0, 2000.0, 19000.0, 40000.0 })
                {
                    OtaFilter f;
                    for (int i = 0; i < static_cast<int> (rate * 0.25); ++i)
                        run (f, ((i / 4) % 2 == 0 ? amplitude : -amplitude) * ((i % 48) < 24 ? 1.0f : -1.0f), gFor (fc), 1.0f);
                }

            // g jumping between 0.001 and 1.4 every few samples, resonance 1 (and jumping), noise at +-8.
            OtaFilter f;
            osp::Prng rng (11);
            for (int i = 0; i < 400000; ++i)
            {
                const int step = i / 5;
                const float g = (step % 2 == 0) ? 0.001f : 1.4f;
                const float res = (i / 97) % 3 == 0 ? static_cast<float> ((step * 7919 % 100) / 100.0) : 1.0f;
                run (f, static_cast<float> (8.0 * (rng.nextDouble() * 2.0 - 1.0)), g, res);
            }
            // A g sweep across the whole range, any value in (0, 1.5), with +-8 steps.
            for (int i = 0; i < 200000; ++i)
                run (f, (i / 300) % 2 == 0 ? 8.0f : -8.0f, static_cast<float> (0.0005 + 1.4995 * ((i * 7919) % 10007) / 10007.0), 1.0f);

            INFO ("peak " << peak);
            CHECK (finite);
            // Measured 7.7 .. 8.9 with three solves per sample, up to 14.6 with the two that ship
            // (the 16-voice budget): bounded either way; the synth never jumps g like this.
            CHECK (peak < 16.0f);

            // Silence with the resonance down: it decays to quiet (no stuck state, no denormal tail).
            for (int i = 0; i < static_cast<int> (rate); ++i)
                run (f, 0.0f, gFor (1000.0), 0.0f);
            CHECK (f.isQuiet());
        }
}

TEST_CASE ("ota: deterministic; reset() restores the initial state", "[filter][ota]")
{
    std::vector<float> input (48000);
    osp::Prng rng (3);
    for (auto& v : input)
        v = static_cast<float> (2.0 * (rng.nextDouble() * 2.0 - 1.0));
    auto renderWith = [&] (OtaFilter& f, OtaCharacter character, bool twoPole) {
        std::vector<float> out (input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
            out[i] = f.process (input[i], gFor (300.0 + 50.0 * static_cast<double> (i % 200)), 0.95f, character, twoPole);
        return out;
    };
    for (const auto character : { OtaCharacter::juno, OtaCharacter::jupiter })
        for (const bool twoPole : { false, true })
        {
            OtaFilter a, b;
            const auto first = renderWith (a, character, twoPole);
            const auto second = renderWith (b, character, twoPole);
            CHECK (std::memcmp (first.data(), second.data(), first.size() * sizeof (float)) == 0);
            a.reset();
            const auto again = renderWith (a, character, twoPole);
            CHECK (std::memcmp (first.data(), again.data(), first.size() * sizeof (float)) == 0);
        }
}

TEST_CASE ("ota: CPU cost (informational)", "[filter][ota]")
{
    OtaFilter f;
    const int n = 2000000;
    float sum = 0.0f;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i)
        sum += f.process ((i % 400) < 200 ? 0.8f : -0.8f, 0.05f + 0.00001f * static_cast<float> (i % 1000), 0.9f, OtaCharacter::jupiter, false);
    const double ns = std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - start).count() / n;
    // 16 voices at 4 x 48 kHz = 3.07 M filter samples per second.
    WARN ("OtaFilter: " << ns << " ns per sample; 16 voices at 192 kHz ~ " << ns * 3.072e6 * 1.0e-7 << " % of one core");
    CHECK (std::isfinite (sum));
}

//==============================================================================
TEST_CASE ("hpf: FLAT positions are an exact pass-through", "[filter][hpf]")
{
    std::vector<float> input (20000);
    osp::Prng rng (5);
    for (auto& v : input)
        v = static_cast<float> (rng.nextDouble() * 2.0 - 1.0);
    input[100] = -0.0f;
    const std::pair<HpfVariant, int> flats[] = {
        { HpfVariant::juno106, 1 }, { HpfVariant::juno6, 0 }, { HpfVariant::juno60, 0 }, { HpfVariant::jupiter, 0 }
    };
    for (const auto& [variant, position] : flats)
    {
        VoiceHighPass hpf;
        hpf.setMode (variant, position);
        hpf.prepare (rate);
        std::vector<float> out (input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
            out[i] = hpf.process (input[i]);
        CHECK (std::memcmp (out.data(), input.data(), input.size() * sizeof (float)) == 0);

        // Away and back while playing: exact again once the 20 ms ramp has finished.
        hpf.setMode (variant, position == 0 ? 3 : 2);
        for (int i = 0; i < 10000; ++i)
            hpf.process (input[static_cast<std::size_t> (i)]);
        hpf.setMode (variant, position);
        for (int i = 0; i < static_cast<int> (rate * VoiceHighPass::rampSeconds) + 1; ++i)
            hpf.process (input[static_cast<std::size_t> (i)]);
        for (std::size_t i = 0; i < input.size(); ++i)
            out[i] = hpf.process (input[i]);
        CHECK (std::memcmp (out.data(), input.data(), input.size() * sizeof (float)) == 0);
    }
}

TEST_CASE ("hpf: the Juno-106's position 0 boosts the bass", "[filter][hpf]")
{
    VoiceHighPass hpf;
    hpf.prepare (rate);
    hpf.setMode (HpfVariant::juno106, 0);
    hpf.reset();
    const double lift = hpfGainDb (hpf, 50.0) - hpfGainDb (hpf, 1000.0);
    INFO ("50 Hz relative to 1 kHz: " << lift << " dB; at 20 Hz " << hpfGainDb (hpf, 20.0) << " dB; at 10 kHz " << hpfGainDb (hpf, 10000.0) << " dB");
    CHECK (lift > 4.0);
    CHECK (lift < 6.5);
    CHECK (hpfGainDb (hpf, 10000.0) == Approx (0.0).margin (0.1));   // a shelf: the top untouched
}

TEST_CASE ("hpf: the cut positions take progressively more bass, -3 dB at their corners", "[filter][hpf]")
{
    struct Case
    {
        HpfVariant variant;
        int firstCut;
        double corners[3];
    };
    const Case cases[] = {
        { HpfVariant::juno6, 1, { 225.0, 339.0, 720.0 } },
        { HpfVariant::juno60, 1, { 225.0, 339.0, 720.0 } },
        { HpfVariant::juno106, 2, { 240.0, 720.0, 0.0 } },
        { HpfVariant::jupiter, 1, { 120.0, 350.0, 1000.0 } },
    };
    for (const auto& c : cases)
    {
        VoiceHighPass hpf;
        hpf.prepare (rate);
        double previous = 0.1;   // FLAT: 0 dB
        for (int position = c.firstCut; position <= 3; ++position)
        {
            hpf.setMode (c.variant, position);
            hpf.reset();
            const double at100 = hpfGainDb (hpf, 100.0);
            const double corner = c.corners[position - c.firstCut];
            const double atCorner = hpfGainDb (hpf, corner);
            INFO ("variant " << static_cast<int> (c.variant) << " position " << position << ": 100 Hz " << at100 << " dB, "
                             << corner << " Hz " << atCorner << " dB");
            CHECK (at100 < previous - 2.0);
            CHECK (atCorner == Approx (-3.01).margin (0.05));
            CHECK (hpfGainDb (hpf, 15000.0) == Approx (0.0).margin (0.05));
            previous = at100;
        }
    }
}

TEST_CASE ("hpf: the Jupiter-8 high-pass is 6 dB/oct", "[filter][hpf]")
{
    VoiceHighPass hpf;
    hpf.prepare (rate);
    hpf.setMode (HpfVariant::jupiter, 3);   // 1 kHz
    hpf.reset();
    const double slope = hpfGainDb (hpf, 125.0) - hpfGainDb (hpf, 62.5);
    INFO ("slope three to four octaves below the corner: " << slope << " dB/oct");
    CHECK (slope == Approx (6.0).margin (0.1));
}

TEST_CASE ("hpf: switching positions while playing makes no step", "[filter][hpf]")
{
    // A steady 50 Hz sine through a cycle of switches, 100 ms apart. Without smoothing a switch
    // between FLAT and 720 Hz would jump by ~0.9 at once; with it, the largest sample-to-sample
    // change stays that of the sine itself at the loudest position's gain (the boost lifts it).
    const double hz = 50.0;
    const double naturalStep = 2.0 * pi * hz / rate;
    const int positions[] = { 3, 0, 2, 1, 3, 2, 0 };
    for (const auto variant : { HpfVariant::juno106, HpfVariant::juno60, HpfVariant::jupiter })
    {
        VoiceHighPass hpf;
        hpf.prepare (rate);
        double loudest = 1.0;
        for (int position = 0; position < 4; ++position)
        {
            hpf.setMode (variant, position);
            loudest = std::max (loudest, std::pow (10.0, hpfGainDb (hpf, hz) / 20.0));
        }
        hpf.setMode (variant, 1);
        hpf.reset();
        float last = 0.0f, maxStep = 0.0f;
        const int segment = static_cast<int> (rate * 0.1);
        for (int i = 0; i < segment * 8; ++i)
        {
            if (i % segment == 0 && i > 0)
                hpf.setMode (variant, positions[i / segment - 1]);
            const auto x = static_cast<float> (std::sin (2.0 * pi * hz * i / rate));
            const float y = hpf.process (x);
            if (i > 0)
                maxStep = std::max (maxStep, std::abs (y - last));
            last = y;
        }
        INFO ("variant " << static_cast<int> (variant) << ": largest step " << maxStep << "; the sine's own at the loudest position "
                         << naturalStep * loudest);
        CHECK (maxStep < 1.15 * naturalStep * loudest);
    }
}
