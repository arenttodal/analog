// The Juno-style BBD chorus (dsp/JunoChorus): exact bypass, the documented LFO rates and
// sweeps, the stereo pair, click-free mode changes, the hiss, the loudness, determinism.

#include "JunoChorus.h"

#include "core/Prng.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using andor::analog::JunoChorus;

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double rates[] { 44100.0, 48000.0, 96000.0, 192000.0 };

    struct Change
    {
        int sample = 0;
        int mode = 0;
    };

    struct Pair
    {
        std::vector<float> left, right;
    };

    std::vector<float> sine (double hz, double rate, int length, double amplitude)
    {
        std::vector<float> x (static_cast<std::size_t> (length));
        for (int i = 0; i < length; ++i)
            x[static_cast<std::size_t> (i)] = static_cast<float> (amplitude * std::sin (2.0 * pi * hz * i / rate));
        return x;
    }

    /** A band-limited saw chord (additive, so it has no aliasing of its own). */
    std::vector<float> sawChord (double rate, int length)
    {
        std::vector<float> x (static_cast<std::size_t> (length), 0.0f);
        for (const double f0 : { 130.81, 164.81, 196.0, 261.63 })
            for (int h = 1; h * f0 < 0.5 * rate; ++h)
            {
                const double a = 0.1 * 2.0 / pi / h * (h % 2 != 0 ? 1.0 : -1.0);
                for (int i = 0; i < length; ++i)
                    x[static_cast<std::size_t> (i)] += static_cast<float> (a * std::sin (2.0 * pi * h * f0 * i / rate));
            }
        return x;
    }

    std::vector<float> whiteNoise (int length, double amplitude, std::uint64_t seed)
    {
        osp::Prng prng (seed);
        std::vector<float> x (static_cast<std::size_t> (length));
        for (auto& v : x)
            v = static_cast<float> (amplitude * prng.bipolar());
        return x;
    }

    /** Pink-ish noise (Paul Kellet's economy filter on white noise). */
    std::vector<float> pinkNoise (int length, std::uint64_t seed)
    {
        osp::Prng prng (seed);
        std::vector<float> x (static_cast<std::size_t> (length));
        double b0 = 0.0, b1 = 0.0, b2 = 0.0;
        for (auto& v : x)
        {
            const double w = prng.bipolar();
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            v = static_cast<float> (0.1 * (b0 + b1 + b2 + w * 0.1848));
        }
        return x;
    }

    /** Runs the chorus over (left, right) in blocks of `block`, applying each mode change
        at its sample (blocks are split there, as a host's sample-accurate events are). */
    Pair run (JunoChorus& chorus, std::vector<float> left, std::vector<float> right, int block, std::vector<Change> changes = {})
    {
        const int total = static_cast<int> (left.size());
        std::size_t next = 0;
        for (int at = 0; at < total;)
        {
            while (next < changes.size() && changes[next].sample <= at)
                chorus.setMode (changes[next++].mode);
            int until = std::min (total, at + block);
            if (next < changes.size() && changes[next].sample < until)
                until = changes[next].sample;
            chorus.process (left.data() + at, right.data() + at, until - at);
            at = until;
        }
        return { std::move (left), std::move (right) };
    }

    Pair runMono (JunoChorus& chorus, const std::vector<float>& x, int block = 512, std::vector<Change> changes = {})
    {
        return run (chorus, x, x, block, std::move (changes));
    }

    bool bitIdentical (const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
    }

    double meanSquare (const std::vector<float>& x, std::size_t from)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            sum += static_cast<double> (x[i]) * x[i];
        return sum / static_cast<double> (x.size() - from);
    }

    double correlation (const std::vector<float>& a, const std::vector<float>& b, std::size_t from)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (std::size_t i = from; i < a.size(); ++i)
        {
            ab += static_cast<double> (a[i]) * b[i];
            aa += static_cast<double> (a[i]) * a[i];
            bb += static_cast<double> (b[i]) * b[i];
        }
        return ab / std::sqrt (aa * bb);
    }

    /** The wet tap's delay over time, in ms, from a steady chorus fed a mono sine of
        exactly `period` samples: the dry part is removed (its gain is known), then the wet
        part's phase against the input is demodulated (I/Q, averaged over one period, which
        nulls the 2f products exactly). The carrier's period (8 ms) exceeds the longest
        delay, so the phase has no 2 pi ambiguity. Includes the BBD filters' small group
        delay (about 0.05 ms at 125 Hz). */
    std::vector<double> delayTrace (const std::vector<float>& input, const std::vector<float>& output, int period, double rate)
    {
        const std::size_t n = input.size();
        const double w = 2.0 * pi / period;
        std::vector<double> c (n), s (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double wet = (output[i] - JunoChorus::dryGain * input[i]) / JunoChorus::wetGain;
            c[i] = wet * std::cos (w * static_cast<double> (i));
            s[i] = wet * std::sin (w * static_cast<double> (i));
        }
        std::vector<double> delays;
        double sumC = 0.0, sumS = 0.0, previous = 0.0, offset = 0.0;
        const auto p = static_cast<std::size_t> (period);
        const auto skip = static_cast<std::size_t> (0.05 * rate) + p;
        for (std::size_t i = 0; i < n; ++i)
        {
            sumC += c[i];
            sumS += s[i];
            if (i >= p)
            {
                sumC -= c[i - p];
                sumS -= s[i - p];
            }
            if (i < skip)
                continue;   // the line filling up and the filters settling
            // The input is sin (w n): the wet part sin (w (n - d)) correlates with it as
            // cos (w d) / 2, and with cos (w n) as -sin (w d) / 2.
            double phase = std::atan2 (-sumC, sumS);
            if (delays.empty())
                offset = phase < 0.0 ? 2.0 * pi : 0.0;
            else if (phase + offset - previous > pi)
                offset -= 2.0 * pi;
            else if (phase + offset - previous < -pi)
                offset += 2.0 * pi;
            previous = phase + offset;
            delays.push_back (1000.0 * previous / w / rate);
        }
        return delays;
    }

    struct Modulation
    {
        double rateHz = 0.0, peakToPeakMs = 0.0, meanMs = 0.0, midMs = 0.0;
    };

    /** The trace's rate (rising crossings of its mean, with hysteresis), sweep and mean. */
    Modulation analyse (const std::vector<double>& trace, double rate)
    {
        Modulation m;
        const auto [lo, hi] = std::minmax_element (trace.begin(), trace.end());
        m.peakToPeakMs = *hi - *lo;
        m.midMs = 0.5 * (*hi + *lo);   // the sweep's centre (the mean depends on where the window cuts the cycles)
        double sum = 0.0;
        for (const double d : trace)
            sum += d;
        m.meanMs = sum / static_cast<double> (trace.size());
        const double band = 0.2 * m.peakToPeakMs;
        bool below = trace.front() < m.meanMs;
        double first = -1.0, last = -1.0;
        int cycles = 0;
        for (std::size_t i = 1; i < trace.size(); ++i)
        {
            if (trace[i] < m.meanMs - band)
                below = true;
            else if (below && trace[i] >= m.meanMs && trace[i - 1] < m.meanMs)
            {
                below = false;
                const double t = static_cast<double> (i - 1) + (m.meanMs - trace[i - 1]) / (trace[i] - trace[i - 1]);
                if (first < 0.0)
                    first = t;
                else
                    ++cycles;
                last = t;
            }
        }
        m.rateHz = cycles > 0 ? rate * cycles / (last - first) : 0.0;
        return m;
    }

    struct Measured
    {
        Modulation leftTap, rightTap;
        double sumSpreadMs = 0.0;   ///< how far (left + right) / 2 strays from its mean
    };

    Measured measureMode (int mode, double rate, double seconds)
    {
        JunoChorus chorus;
        chorus.prepare (rate, 512);
        chorus.setMode (mode);
        chorus.reset();
        const int period = static_cast<int> (std::lround (rate / 125.0));
        const auto x = sine (rate / period, rate, static_cast<int> (seconds * rate), 0.5);
        const auto out = runMono (chorus, x);
        const auto dl = delayTrace (x, out.left, period, rate);
        const auto dr = delayTrace (x, out.right, period, rate);
        Measured m;
        m.leftTap = analyse (dl, rate);
        m.rightTap = analyse (dr, rate);
        double meanSum = 0.0;
        for (std::size_t i = 0; i < dl.size(); ++i)
            meanSum += 0.5 * (dl[i] + dr[i]);
        meanSum /= static_cast<double> (dl.size());
        for (std::size_t i = 0; i < dl.size(); ++i)
            m.sumSpreadMs = std::max (m.sumSpreadMs, std::abs (0.5 * (dl[i] + dr[i]) - meanSum));
        return m;
    }
}

TEST_CASE ("chorus: OFF is a bit-exact bypass, also after I -> OFF and its fade", "[chorus]")
{
    const double rate = 48000.0;
    JunoChorus chorus;
    chorus.prepare (rate, 512);
    chorus.setNoise (true, 99);   // the hiss must not leak into the bypass either
    CHECK_FALSE (chorus.isActive());

    auto left = whiteNoise (24000, 0.8, 1);
    auto right = whiteNoise (24000, 0.8, 2);
    left[100] = -0.0f;   // even the sign of zero survives
    const auto out = run (chorus, left, right, 333);
    CHECK (bitIdentical (out.left, left));
    CHECK (bitIdentical (out.right, right));
    CHECK (std::signbit (out.left[100]));

    // I for half a second, then OFF: after the 30 ms fade (+ margin) it is the input again.
    const int switchAt = 24000, settled = switchAt + static_cast<int> (0.031 * rate) + 1;
    const auto l2 = whiteNoise (48000, 0.8, 3), r2 = whiteNoise (48000, 0.8, 4);
    chorus.setMode (JunoChorus::one);
    const auto out2 = run (chorus, l2, r2, 256, { { switchAt, JunoChorus::off } });
    CHECK_FALSE (chorus.isActive());
    bool changedWhileOn = false, exactAfter = true;
    for (std::size_t i = 0; i < l2.size(); ++i)
    {
        if (static_cast<int> (i) < switchAt && (out2.left[i] != l2[i] || out2.right[i] != r2[i]))
            changedWhileOn = true;
        if (static_cast<int> (i) >= settled && (std::memcmp (&out2.left[i], &l2[i], sizeof (float)) != 0 || std::memcmp (&out2.right[i], &r2[i], sizeof (float)) != 0))
            exactAfter = false;
    }
    CHECK (changedWhileOn);
    CHECK (exactAfter);

    // And later blocks pass straight through.
    const auto out3 = run (chorus, l2, r2, 4096);
    CHECK (bitIdentical (out3.left, l2));
    CHECK (bitIdentical (out3.right, r2));
}

TEST_CASE ("chorus: each mode sweeps the delay at its documented rate and depth, at every rate", "[chorus]")
{
    for (const double rate : rates)
        for (const int mode : { 1, 2, 3 })
        {
            const auto sp = JunoChorus::spec (mode);
            // At least ~5 LFO cycles (mode I: 10 s); 192 kHz with I+II needs less.
            const double seconds = mode == 3 ? 2.0 : 10.0;
            const auto m = measureMode (mode, rate, seconds);
            INFO ("rate " << rate << " mode " << mode << ": left " << m.leftTap.rateHz << " Hz, " << m.leftTap.peakToPeakMs
                          << " ms p-p, centre " << m.leftTap.midMs << " ms; right " << m.rightTap.rateHz << " Hz, "
                          << m.rightTap.peakToPeakMs << " ms p-p; (L+R)/2 spread " << m.sumSpreadMs << " ms");
            for (const auto& tap : { m.leftTap, m.rightTap })
            {
                CHECK (std::abs (tap.rateHz / sp.rateHz - 1.0) < 0.02);
                CHECK (std::abs (tap.peakToPeakMs / (2.0 * sp.depthMs) - 1.0) < 0.05);
                // The centre, plus the BBD filters' group delay (~0.05 ms at 125 Hz).
                CHECK (tap.midMs > sp.centreMs);
                CHECK (tap.midMs < sp.centreMs + 0.1);
            }
            // Opposite phase: the two taps' delays mirror each other about the centre.
            CHECK (m.sumSpreadMs < 0.02 * 2.0 * sp.depthMs + 0.005);
        }
}

TEST_CASE ("chorus: I and II differ in rate; I+II is faster and shallower", "[chorus]")
{
    const auto one = measureMode (1, 48000.0, 10.0).leftTap;
    const auto two = measureMode (2, 48000.0, 10.0).leftTap;
    const auto both = measureMode (3, 48000.0, 2.0).leftTap;
    INFO ("I " << one.rateHz << " Hz / " << one.peakToPeakMs << " ms, II " << two.rateHz << " Hz / " << two.peakToPeakMs
               << " ms, I+II " << both.rateHz << " Hz / " << both.peakToPeakMs << " ms");
    CHECK (two.rateHz > 1.4 * one.rateHz);
    CHECK (both.rateHz > 5.0 * two.rateHz);
    CHECK (both.peakToPeakMs < 0.25 * one.peakToPeakMs);
    CHECK (both.peakToPeakMs < 0.25 * two.peakToPeakMs);
}

TEST_CASE ("chorus: a stereo image when on; a mono input gives a symmetric pair", "[chorus]")
{
    const double rate = 48000.0;
    const int length = static_cast<int> (9.0 * rate);
    const auto x = sawChord (rate, length);
    const auto from = static_cast<std::size_t> (rate);
    for (const int mode : { 1, 2, 3 })
    {
        JunoChorus chorus;
        chorus.prepare (rate, 512);
        chorus.setMode (mode);
        chorus.reset();
        const auto out = runMono (chorus, x);
        const double r = correlation (out.left, out.right, from);
        // Over whole LFO cycles the two sides carry the same energy (mode I: 4 cycles
        // = 7.797 s; the others fit a whole number of cycles closely enough in 7 s).
        const double cycles = mode == 1 ? 4.0 : (mode == 2 ? 6.0 : 58.0);
        const auto span = static_cast<std::size_t> (cycles / JunoChorus::spec (mode).rateHz * rate);
        double el = 0.0, er = 0.0;
        for (std::size_t i = from; i < from + span && i < out.left.size(); ++i)
        {
            el += static_cast<double> (out.left[i]) * out.left[i];
            er += static_cast<double> (out.right[i]) * out.right[i];
        }
        const double balanceDb = 10.0 * std::log10 (el / er);
        INFO ("mode " << mode << ": L/R correlation " << r << ", L/R balance " << balanceDb << " dB");
        CHECK (r < 0.95);
        CHECK (r > 0.0);
        CHECK (std::abs (balanceDb) < 0.1);
    }
}

TEST_CASE ("chorus: mode switches never click", "[chorus]")
{
    for (const double rate : { 48000.0, 192000.0 })
    {
        const int length = static_cast<int> (6.0 * rate);
        const auto x = sine (440.0, rate, length, 0.5);
        const auto at = [rate] (double seconds) { return static_cast<int> (seconds * rate); };
        // Every kind of change, plus one asked in the middle of another's fade.
        const std::vector<Change> changes { { at (0.5), 1 }, { at (1.5), 2 }, { at (2.5), 3 }, { at (3.0), 1 },
                                            { at (3.5), 0 }, { at (4.0), 3 }, { at (4.5), 2 }, { at (4.51), 0 },
                                            { at (5.0), 2 } };
        JunoChorus chorus;
        chorus.prepare (rate, 512);
        const auto out = runMono (chorus, x, 512, changes);

        const auto window = static_cast<std::size_t> (0.1 * rate);
        double steady = 0.0, around = 0.0;
        for (std::size_t i = 1; i < out.left.size(); ++i)
        {
            const double delta = std::max (std::abs (out.left[i] - out.left[i - 1]), std::abs (out.right[i] - out.right[i - 1]));
            bool near = false;
            for (const auto& c : changes)
                near = near || (i + window / 2 > static_cast<std::size_t> (c.sample) && i < static_cast<std::size_t> (c.sample) + window);
            (near ? around : steady) = std::max (near ? around : steady, delta);
        }
        INFO ("rate " << rate << ": max sample step near switches " << around << ", in steady states " << steady);
        CHECK (around < 1.1 * steady);
    }
}

TEST_CASE ("chorus: the hiss is off by default, seeded, and about -66 dBFS", "[chorus]")
{
    for (const double rate : rates)
    {
        const int length = static_cast<int> (3.0 * rate);
        const std::vector<float> silence (static_cast<std::size_t> (length), 0.0f);
        const auto from = static_cast<std::size_t> (0.5 * rate);

        JunoChorus quiet;
        quiet.prepare (rate, 512);
        quiet.setMode (JunoChorus::two);
        quiet.reset();
        const auto none = runMono (quiet, silence);
        CHECK (meanSquare (none.left, 0) == 0.0);
        CHECK (meanSquare (none.right, 0) == 0.0);

        const auto hissy = [&] (std::uint32_t seed, int mode)
        {
            JunoChorus chorus;
            chorus.prepare (rate, 512);
            chorus.setMode (mode);
            chorus.setNoise (true, seed);
            chorus.reset();
            return runMono (chorus, silence, 480);
        };
        const auto a = hissy (7, JunoChorus::one), b = hissy (7, JunoChorus::one), c = hissy (8, JunoChorus::one);
        CHECK (bitIdentical (a.left, b.left));
        CHECK (bitIdentical (a.right, b.right));
        CHECK_FALSE (bitIdentical (a.left, c.left));
        for (const auto* out : { &a, &c })
            for (const auto* channel : { &out->left, &out->right })
            {
                const double dbfs = 10.0 * std::log10 (meanSquare (*channel, from));
                INFO ("rate " << rate << ": hiss " << dbfs << " dBFS RMS");
                CHECK (dbfs > -70.0);
                CHECK (dbfs < -60.0);
            }
        // Two BBDs, two independent hisses.
        CHECK (std::abs (correlation (a.left, a.right, from)) < 0.1);

        // OFF with the hiss enabled is still silence (the bypass).
        const auto offHiss = hissy (7, JunoChorus::off);
        CHECK (meanSquare (offHiss.left, 0) == 0.0);
    }
}

TEST_CASE ("chorus: engaging it changes the loudness by less than 1.5 dB on broadband input", "[chorus]")
{
    const double rate = 48000.0;
    const int length = static_cast<int> (6.0 * rate);
    const auto from = static_cast<std::size_t> (rate);
    struct Signal
    {
        const char* name;
        std::vector<float> x;
    };
    const Signal signals[] { { "saw chord", sawChord (rate, length) },
                             { "white noise", whiteNoise (length, 0.3, 5) },
                             { "pink noise", pinkNoise (length, 6) } };
    for (const auto& signal : signals)
        for (const int mode : { 1, 2, 3 })
        {
            JunoChorus chorus;
            chorus.prepare (rate, 512);
            chorus.setMode (mode);
            chorus.reset();
            const auto out = runMono (chorus, signal.x);
            const double changeDb = 10.0 * std::log10 (0.5 * (meanSquare (out.left, from) + meanSquare (out.right, from)) / meanSquare (signal.x, from));
            INFO (signal.name << ", mode " << mode << ": " << changeDb << " dB");
            CHECK (std::abs (changeDb) < 1.5);
        }
}

TEST_CASE ("chorus: the block size never changes the output; reset() reproduces it", "[chorus]")
{
    const double rate = 48000.0;
    const int length = static_cast<int> (2.0 * rate);
    std::vector<float> left = sawChord (rate, length), right = whiteNoise (length, 0.2, 9);
    for (std::size_t i = 0; i < left.size(); ++i)
        left[i] += 0.5f * right[i];
    const std::vector<Change> changes { { 1000, 1 }, { 30000, 2 }, { 30500, 3 }, { 52000, 0 }, { 60001, 3 }, { 80000, 1 } };

    const auto render = [&] (int block)
    {
        JunoChorus chorus;
        chorus.prepare (rate, 2048);
        chorus.setNoise (true, 1234);
        chorus.reset();
        return run (chorus, left, right, block, changes);
    };
    const auto reference = render (2048);
    for (const int block : { 1, 7, 64, 512 })
    {
        INFO ("block " << block);
        const auto out = render (block);
        CHECK (bitIdentical (out.left, reference.left));
        CHECK (bitIdentical (out.right, reference.right));
    }

    JunoChorus chorus;
    chorus.prepare (rate, 2048);
    chorus.setNoise (true, 1234);
    chorus.reset();
    const auto first = run (chorus, left, right, 300, changes);
    chorus.setMode (JunoChorus::off);   // the starting state of the reference render
    chorus.reset();
    const auto second = run (chorus, left, right, 300, changes);
    CHECK (bitIdentical (first.left, reference.left));
    CHECK (bitIdentical (second.left, reference.left));
    CHECK (bitIdentical (second.right, reference.right));
}

TEST_CASE ("chorus: finite and bounded under full-scale noise at every supported rate", "[chorus]")
{
    for (const double rate : rates)
    {
        const int length = static_cast<int> (1.5 * rate);
        const auto at = [rate] (double seconds) { return static_cast<int> (seconds * rate); };
        for (const double amplitude : { 1.0, 8.0 })
        {
            JunoChorus chorus;
            chorus.prepare (rate, 512);
            chorus.setNoise (true, 5);
            const auto out = run (chorus, whiteNoise (length, amplitude, 11), whiteNoise (length, amplitude, 12), 512,
                                  { { 0, 1 }, { at (0.3), 2 }, { at (0.6), 3 }, { at (0.9), 0 }, { at (1.2), 3 } });
            bool finite = true;
            float peak = 0.0f;
            for (std::size_t i = 0; i < out.left.size(); ++i)
            {
                finite = finite && std::isfinite (out.left[i]) && std::isfinite (out.right[i]);
                peak = std::max ({ peak, std::abs (out.left[i]), std::abs (out.right[i]) });
            }
            INFO ("rate " << rate << ", input peak " << amplitude << ": output peak " << peak);
            CHECK (finite);
            // Dry x 0.74 plus a saturated (|x| <= 6) and low-passed wet x 0.74.
            CHECK (peak < 0.74f * static_cast<float> (amplitude) + 6.0f);
        }
    }
}
