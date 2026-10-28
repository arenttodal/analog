// The ladder filter: slope, tuning of the resonance, stability at the corners, and the
// synth's filter controls (TONE, key tracking, envelope, DRIVE).

#include "TestUtil.h"

#include "LadderFilter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace testutil;
using Catch::Approx;

namespace
{
    /** The filter's steady-state gain for a small sine (its linear region). */
    double gainAt (double freq, double cutoff, double k, double rate = 192000.0)
    {
        LadderFilter f;
        const auto g = static_cast<float> (std::tan (3.14159265358979323846 * cutoff / rate));
        const int n = static_cast<int> (rate * 0.5);
        double sumIn = 0.0, sumOut = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const auto x = static_cast<float> (0.001 * std::sin (2.0 * 3.14159265358979323846 * freq * i / rate));
            const float y = f.process (x, g, static_cast<float> (k), 0.0f, 0.0f);
            if (i > n / 2)
            {
                sumIn += static_cast<double> (x) * x;
                sumOut += static_cast<double> (y) * y;
            }
        }
        return 10.0 * std::log10 (sumOut / sumIn);
    }
}

TEST_CASE ("filter: four poles, 24 dB per octave above the cutoff; flat below", "[filter][unit]")
{
    CHECK (gainAt (100.0, 1000.0, 0.0) == Approx (0.0).margin (0.2));
    CHECK (gainAt (1000.0, 1000.0, 0.0) == Approx (-12.0).margin (0.5));   // four poles, -3 dB each at fc
    const double oneOctave = gainAt (8000.0, 1000.0, 0.0), twoOctaves = gainAt (16000.0, 1000.0, 0.0);
    CHECK (oneOctave - twoOctaves == Approx (24.0).margin (1.5));
}

TEST_CASE ("filter: resonance peaks at the cutoff (tuned), and rises with k", "[filter][unit]")
{
    for (const double fc : { 500.0, 4000.0, 15000.0 })
    {
        INFO ("cutoff " << fc);
        const double atCutoff = gainAt (fc, fc, 3.6);
        CHECK (atCutoff > gainAt (fc * 0.7, fc, 3.6));
        CHECK (atCutoff > gainAt (fc * 1.4, fc, 3.6));
        CHECK (atCutoff > gainAt (fc, fc, 2.0));
    }
}

TEST_CASE ("filter: never blows up at the corners, under abrupt changes, with loud input", "[filter][unit]")
{
    LadderFilter f;
    osp::Prng rng (7);
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < 400000; ++i)
    {
        // Cutoff and resonance jump every 37 samples across their whole range.
        const int step = i / 37;
        const float g = static_cast<float> (std::tan (3.14159265358979323846 * std::min (0.45, 0.0001 + (step * 7919 % 1000) / 2200.0)));
        const float k = static_cast<float> ((step * 104729 % 100) / 100.0 * 4.2);
        const auto x = static_cast<float> (8.0 * rng.bipolar());
        const float y = f.process (x, g, k, 0.36f, std::tanh (0.36f));
        finite = finite && std::isfinite (y);
        peak = std::max (peak, std::abs (y));
    }
    CHECK (finite);
    CHECK (peak < 4.0f);
}

TEST_CASE ("filter: at full resonance it sings by itself, at a bounded level", "[filter][unit]")
{
    auto p = measurementPatch();
    p.osc1Level = 0.0;
    p.tone = math::hzToTone (1000.0);
    p.resonance = 1.0;
    p.noiseLevel = 0.02;   // a breath to start it
    auto synth = makeSynth (p);
    const auto out = render (*synth, 2.0, { on (0, 60) });
    CHECK (out.finite());
    CHECK (out.rms (1.0, 2.0) > 0.01);
    CHECK (out.peak() < 1.0f);
    // It rings near the cutoff.
    const double f = frequencyByCrossings (out.mono(), 48000.0, 1.0, 1.9);
    CHECK (std::abs (1200.0 * std::log2 (f / 1000.0)) < 150.0);
}

TEST_CASE ("filter: TONE, key tracking and the envelope move the brightness", "[filter][unit]")
{
    auto brightness = [] (const Patch& p, int note, double from, double to) {
        auto synth = makeSynth (p);
        const auto out = render (*synth, to + 0.05, { on (0, note) });
        // The high band's share: energy of the first difference over the energy.
        const auto x = out.mono();
        double d = 0.0, e = 0.0;
        for (auto i = static_cast<std::size_t> (from * 48000.0); i < static_cast<std::size_t> (to * 48000.0); ++i)
        {
            d += (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
            e += x[i] * x[i];
        }
        return d / e;
    };
    auto p = measurementPatch();
    p.tone = math::hzToTone (600.0);
    const double dark = brightness (p, 48, 0.2, 0.6);
    p.tone = math::hzToTone (4000.0);
    CHECK (brightness (p, 48, 0.2, 0.6) > 2.0 * dark);

    p.tone = math::hzToTone (600.0);
    p.keyTrack = 1.0;
    auto noTrack = p;
    noTrack.keyTrack = 0.0;
    // Tracking opens the filter two octaves at C6: much more of the note passes.
    auto level = [] (const Patch& patch) {
        auto synth = makeSynth (patch);
        return render (*synth, 0.65, { on (0, 84) }).rms (0.2, 0.6);
    };
    CHECK (level (p) > 4.0 * level (noTrack));

    auto env = measurementPatch();
    env.tone = math::hzToTone (300.0);
    env.envDepthSemitones = 48.0;
    env.filterAttack = 0.001;
    env.filterDecay = 0.3;
    env.filterSustain = 0.0;
    // Bright at the start, dark once the envelope has decayed.
    CHECK (brightness (env, 48, 0.02, 0.06) > 3.0 * brightness (env, 48, 0.8, 1.0));
}

TEST_CASE ("filter: DRIVE adds harmonics; BIAS adds even ones; TRIM sets the level", "[filter][unit]")
{
    auto p = measurementPatch();
    p.osc1Level = 0.0;
    p.osc2Level = 1.0;
    p.blend = 1.0;
    p.osc2Wave = Osc2Wave::triangle;   // nearly a sine: what DRIVE adds shows
    p.osc2FineCents = 0.0;
    p.tone = math::hzToTone (12000.0);
    auto harmonic = [] (const Stereo& s, double f0, int h) {
        const auto mag = spectrum (s.mono(), 9600, 15);
        const double binHz = 48000.0 / 32768.0;
        double m = 0.0;
        for (int d = -3; d <= 3; ++d)
            m = std::max (m, mag[static_cast<std::size_t> (std::lround (h * f0 / binHz) + d)]);
        return m;
    };
    const double f0 = 220.0;
    auto clean = makeSynth (p);
    const auto a = render (*clean, 1.0, { on (0, 57) });
    p.filterDrive = 1.0;
    auto driven = makeSynth (p);
    const auto b = render (*driven, 1.0, { on (0, 57) });
    CHECK (harmonic (b, f0, 3) / harmonic (b, f0, 1) > 1.5 * harmonic (a, f0, 3) / harmonic (a, f0, 1));
    CHECK (harmonic (b, f0, 2) / harmonic (b, f0, 1) < 0.01);   // symmetric: hardly any even
    p.driveBias = 1.0;
    auto biased = makeSynth (p);
    const auto c = render (*biased, 1.0, { on (0, 57) });
    CHECK (harmonic (c, f0, 2) / harmonic (c, f0, 1) > 0.02);
    p.driveTrimDb = -6.0;
    auto trimmed = makeSynth (p);
    const auto d = render (*trimmed, 1.0, { on (0, 57) });
    CHECK (d.rms (0.2, 0.9) == Approx (c.rms (0.2, 0.9) * 0.501).epsilon (0.03));
}
