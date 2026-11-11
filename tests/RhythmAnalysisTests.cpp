// RHYTHMIC MORPH, gate 4: the rhythm analyser against synthetic recordings with a known truth
// (docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md).

#include "rhythm/RhythmAnalysis.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "core/Prng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace rhythm = andor::analog::rhythm;
using Catch::Approx;

namespace
{
    enum class Drum { click, kick, snare, hat };
    struct Hit
    {
        double seconds;
        double level;
        Drum drum;
    };

    /** A recording of hits (deterministic noise), `seconds` long; hits past the end wrap. */
    std::vector<float> render (const std::vector<Hit>& hits, double seconds, double rate, std::uint64_t seed = 7)
    {
        const auto length = static_cast<std::size_t> (std::lround (seconds * rate));
        std::vector<float> out (length, 0.0f);
        osp::Prng noise (seed);
        for (const auto& h : hits)
        {
            const double start = std::fmod (h.seconds, seconds);
            const auto first = static_cast<std::size_t> (std::lround (start * rate));
            const double decay = h.drum == Drum::click ? 0.003 : (h.drum == Drum::kick ? 0.12 : (h.drum == Drum::snare ? 0.08 : 0.025));
            const auto n = static_cast<std::size_t> (6.0 * decay * rate);
            double hp = 0.0, phase = 0.0;
            for (std::size_t k = 0; k < n; ++k)
            {
                const double t = static_cast<double> (k) / rate, env = std::exp (-t / decay);
                double v = 0.0;
                switch (h.drum)
                {
                    case Drum::click: v = std::sin (2.0 * std::numbers::pi * 2000.0 * t); break;
                    case Drum::kick:
                        phase += 2.0 * std::numbers::pi * (50.0 + 90.0 * std::exp (-t / 0.03)) / rate;
                        v = std::sin (phase);
                        break;
                    case Drum::snare: v = 0.6 * noise.bipolar() + 0.4 * std::sin (2.0 * std::numbers::pi * 190.0 * t); break;
                    case Drum::hat:
                    {
                        const double x = noise.bipolar();
                        v = x - hp;   // (a crude high-pass: brightness)
                        hp = x;
                        break;
                    }
                }
                out[(first + k) % length] += static_cast<float> (0.5 * h.level * env * v);
            }
        }
        return out;
    }

    /** A metronome: `bars` of `meter` beats at `bpm`, the downbeat accented, the grid starting at `offset`. */
    std::vector<Hit> metronome (double bpm, int bars, int meter = 4, double offset = 0.0)
    {
        std::vector<Hit> hits;
        const double beat = 60.0 / bpm;
        for (int i = 0; i < bars * meter; ++i)
            hits.push_back ({ offset + i * beat, i % meter == 0 ? 1.0 : 0.55, Drum::click });
        return hits;
    }

    rhythm::Analysis analyseHits (const std::vector<Hit>& hits, double seconds, double rate = 48000.0)
    {
        const auto x = render (hits, seconds, rate);
        return rhythm::analyse (x.data(), x.size(), rate);
    }
}

TEST_CASE ("RHYTHM: a metronome at 90, 120 and 140 BPM - tempo exact from the loop, 4 bars, the downbeat, every onset", "[rhythm][unit]")
{
    for (const double bpm : { 90.0, 120.0, 140.0 })
    {
        INFO (bpm << " BPM");
        const double seconds = 16 * 60.0 / bpm;
        const auto hits = metronome (bpm, 4);
        const auto a = analyseHits (hits, seconds);
        CHECK (a.kind == rhythm::Kind::rhythmic);
        CHECK (a.loopFits);
        CHECK (a.bpm == Approx (bpm).epsilon (1.0e-6));
        CHECK (a.beats == 16);
        CHECK (a.beatsPerBar == 4);
        CHECK (a.bars == Approx (4.0));
        CHECK (a.downbeatSeconds == Approx (0.0).margin (0.002));
        CHECK (a.confidence > 0.5);
        // Every click found, at its attack (within a millisecond: well inside one 5 ms hop).
        REQUIRE (a.onsets.size() == hits.size());
        for (std::size_t i = 0; i < hits.size(); ++i)
            CHECK (a.onsets[i].seconds == Approx (hits[i].seconds).margin (0.001));
    }
}

TEST_CASE ("RHYTHM: the same metronome at 44.1, 48 and 96 kHz", "[rhythm][unit]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        INFO (rate << " Hz");
        const auto a = analyseHits (metronome (120.0, 4), 8.0, rate);
        CHECK (a.kind == rhythm::Kind::rhythmic);
        CHECK (a.bpm == Approx (120.0).epsilon (1.0e-6));
        CHECK (a.beats == 16);
    }
}

TEST_CASE ("RHYTHM: a loop that starts before its downbeat (a pickup) - the downbeat is where the accent is", "[rhythm][unit]")
{
    // 4 bars at 120, the grid shifted by 0.3 s (the bar wraps round the loop's end).
    const auto a = analyseHits (metronome (120.0, 4, 4, 0.3), 8.0);
    CHECK (a.kind == rhythm::Kind::rhythmic);
    CHECK (a.bpm == Approx (120.0).epsilon (1.0e-6));
    CHECK (a.downbeatSeconds == Approx (0.3).margin (0.002));
}

TEST_CASE ("RHYTHM: swung drums at 100 BPM and a syncopated 4-bar break at 92", "[rhythm][unit]")
{
    {
        // Kick on 1 and 3, snare on 2 and 4, hats on swung eighths (the offbeat at 60 %).
        const double beat = 0.6;
        std::vector<Hit> hits;
        for (int b = 0; b < 8; ++b)
        {
            hits.push_back ({ b * beat, 1.0, b % 2 == 0 ? Drum::kick : Drum::snare });
            hits.push_back ({ b * beat, 0.4, Drum::hat });
            hits.push_back ({ (b + 0.6) * beat, 0.3, Drum::hat });
        }
        const auto a = analyseHits (hits, 8 * beat);
        CHECK (a.kind == rhythm::Kind::rhythmic);
        CHECK (a.bpm == Approx (100.0).epsilon (0.01));
        CHECK (a.downbeatSeconds == Approx (0.0).margin (0.01));
    }
    {
        const double beat = 60.0 / 92.0;
        std::vector<Hit> hits;
        for (int bar = 0; bar < 4; ++bar)
        {
            const double b0 = bar * 4 * beat;
            for (const double k : { 0.0, 1.5, 2.5 })
                hits.push_back ({ b0 + k * beat, 1.0, Drum::kick });
            for (const double sn : { 1.0, 3.0 })
                hits.push_back ({ b0 + sn * beat, 0.9, Drum::snare });
            for (int s = 0; s < 16; ++s)
                hits.push_back ({ b0 + s * 0.25 * beat, s % 2 == 0 ? 0.35 : 0.2, Drum::hat });
        }
        const auto a = analyseHits (hits, 16 * beat);
        CHECK (a.kind == rhythm::Kind::rhythmic);
        CHECK (a.bpm == Approx (92.0).epsilon (1.0e-6));
        CHECK (a.bars == Approx (4.0));
        CHECK (a.downbeatSeconds == Approx (0.0).margin (0.01));
    }
}

TEST_CASE ("RHYTHM: half / double - an even hat at 140 BPM is read as 140 or 70; HALF and DOUBLE correct it", "[rhythm][unit]")
{
    std::vector<Hit> hits;
    const double eighth = 60.0 / 140.0 / 2.0;
    for (int i = 0; i < 32; ++i)
        hits.push_back ({ i * eighth, 0.6, Drum::hat });
    const auto a = analyseHits (hits, 32 * eighth);
    CHECK (a.loopFits);
    const bool asWritten = std::abs (std::log2 (a.bpm / 140.0)) < 0.01, asHalf = std::abs (std::log2 (a.bpm / 70.0)) < 0.01, asDouble = std::abs (std::log2 (a.bpm / 280.0)) < 0.01;
    CHECK ((asWritten || asHalf || asDouble));
    const auto doubled = rhythm::withTempoFactor (a, 2.0), halved = rhythm::withTempoFactor (a, 0.5);
    CHECK (doubled.bpm == Approx (2.0 * a.bpm));
    CHECK (doubled.beats == 2 * a.beats);
    CHECK (halved.bpm == Approx (0.5 * a.bpm));
    CHECK (doubled.loopSeconds() == Approx (a.loopSeconds()));   // (the loop's length never changes)
}

TEST_CASE ("RHYTHM: a waltz - three beats a bar", "[rhythm][unit]")
{
    const auto a = analyseHits (metronome (120.0, 4, 3), 12 * 0.5);
    CHECK (a.kind == rhythm::Kind::rhythmic);
    CHECK (a.beatsPerBar == 3);
    CHECK (a.beats == 12);
    CHECK (a.bpm == Approx (120.0).epsilon (1.0e-6));
}

TEST_CASE ("RHYTHM: rain is not a metronome - UNSURE, with envelopes to modulate with", "[rhythm][unit]")
{
    // A soft wash and random drops (Poisson, ~25 a second, random levels).
    const double rate = 48000.0, seconds = 6.0;
    osp::Prng rng (42);
    std::vector<Hit> drops;
    for (double t = 0.0; t < seconds;)
    {
        t += -std::log (std::max (1.0e-9, rng.nextDouble())) / 25.0;
        drops.push_back ({ t, 0.1 + 0.5 * rng.nextDouble(), Drum::hat });
    }
    auto x = render (drops, seconds, rate, 3);
    osp::Prng wash (5);
    float lp = 0.0f;
    for (auto& v : x)
    {
        lp += 0.05f * (static_cast<float> (wash.bipolar()) - lp);
        v += 0.15f * lp;
    }
    const auto a = rhythm::analyse (x.data(), x.size(), rate);
    CHECK (a.kind == rhythm::Kind::unsure);
    CHECK (a.envelope.size() > 1000);
    CHECK (a.envelopePeak > 0.0f);
    for (const float e : a.transient)
        REQUIRE ((std::isfinite (e) && e >= 0.0f && e <= 1.0f));
}

TEST_CASE ("RHYTHM: silence, nothing, a crumb, NaN, a bad rate - reported, never fatal", "[rhythm][unit]")
{
    {
        const std::vector<float> silence (48000 * 2, 0.0f);
        CHECK (rhythm::analyse (silence.data(), silence.size(), 48000.0).kind == rhythm::Kind::silent);
    }
    CHECK (rhythm::analyse (nullptr, 0, 48000.0).kind == rhythm::Kind::silent);
    {
        const auto crumb = render ({ { 0.0, 1.0, Drum::click } }, 0.03, 48000.0);
        const auto a = rhythm::analyse (crumb.data(), crumb.size(), 48000.0);
        CHECK (a.kind != rhythm::Kind::rhythmic);
    }
    {
        auto x = render (metronome (120.0, 2), 4.0, 48000.0);
        for (std::size_t i = 0; i < x.size(); i += 997)
            x[i] = std::numeric_limits<float>::quiet_NaN();
        const auto a = rhythm::analyse (x.data(), x.size(), 48000.0);
        CHECK (std::isfinite (a.bpm));
        CHECK (a.bpm == Approx (120.0).epsilon (0.01));
    }
    {
        const auto x = render (metronome (120.0, 2), 4.0, 48000.0);
        const auto a = rhythm::analyse (x.data(), x.size(), 0.0);   // (no rate: 48 kHz assumed)
        CHECK (a.sampleRate == 48000.0);
    }
}

TEST_CASE ("RHYTHM: a long recording (60 s) - correct, and quick enough for a loader thread", "[rhythm][unit]")
{
    const auto x = render (metronome (120.0, 30), 60.0, 48000.0);
    const auto start = std::chrono::steady_clock::now();
    const auto a = rhythm::analyse (x.data(), x.size(), 48000.0);
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    WARN ("60 s analysed in " << seconds << " s");
    CHECK (a.bpm == Approx (120.0).epsilon (1.0e-6));
    CHECK (a.beats == 120);
    CHECK (seconds < 10.0);
}

TEST_CASE ("RHYTHM: a sung vowel with vibrato is not a beat - its flux moves with no attack: UNSURE", "[rhythm][unit]")
{
    // (5.2 Hz vibrato sweeps the harmonics through the formants; the two channels' 3 cents beat
    // in the mono sum - a periodic flux curve, but its level never jumps)
    for (const int note : { 45, 57, 64 })
    {
        const auto v = osp::testsignals::vowel (osp::midiToHz (note), 2.0, 48000.0, 3);
        std::vector<float> m (v.channels[0].size());
        for (std::size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (v.channels[0][i] + v.channels[1][i]);
        const auto a = rhythm::analyse (m.data(), m.size(), 48000.0);
        INFO ("note " << note << ": " << a.bpm << " BPM, confidence " << a.confidence);
        CHECK (a.kind == rhythm::Kind::unsure);
        CHECK (a.envelope.size() > 0);   // (still modulates, FREE)
    }
}
