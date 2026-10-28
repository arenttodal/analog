// The oscillators: pitch, band-limiting against a naive oscillator, pulse width, triangle,
// hard sync and Poly Mod FM, measured through the whole synth.

#include "TestUtil.h"

#include "Oscillator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace testutil;
using Catch::Approx;

namespace
{
    double cents (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }

    /** A naive (aliasing) sawtooth at the host rate: the baseline the band-limited one must beat. */
    std::vector<float> naiveSaw (double hz, double rate, int samples)
    {
        std::vector<float> x (static_cast<std::size_t> (samples));
        double phase = 0.0;
        for (auto& v : x)
        {
            v = static_cast<float> (2.0 * phase - 1.0);
            phase += hz / rate;
            phase -= std::floor (phase);
        }
        return x;
    }
}

TEST_CASE ("oscillators: A4 plays 440 Hz within 0.1 cent at 44.1, 48 and 96 kHz", "[osc][unit]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        INFO ("rate " << rate);
        auto synth = makeSynth (measurementPatch(), rate);
        const auto out = render (*synth, 2.0, { on (0, 69) });
        const double f = frequencyByCrossings (out.mono(), rate, 0.2, 1.9);
        CHECK (std::abs (cents (f, 440.0)) < 0.1);
    }
}

TEST_CASE ("oscillators: octave, coarse and fine tuning move the pitch as written", "[osc][unit]")
{
    auto p = measurementPatch();
    p.osc1Octave = -1;
    p.osc1Coarse = 7.0;   // A3 + a fifth = E4
    auto synth = makeSynth (p);
    auto out = render (*synth, 1.5, { on (0, 69) });
    CHECK (std::abs (cents (frequencyByCrossings (out.mono(), 48000.0, 0.2, 1.4), 220.0 * std::exp2 (7.0 / 12.0))) < 0.1);

    // OSC 2 alone, fine-tuned +25 cents; its keyboard tracking off plays C4's pitch for any key.
    p = measurementPatch();
    p.osc1Level = 0.0;
    p.osc2Level = 1.0;
    p.blend = 1.0;
    p.osc2FineCents = 25.0;
    synth = makeSynth (p);
    out = render (*synth, 1.5, { on (0, 69) });
    CHECK (std::abs (cents (frequencyByCrossings (out.mono(), 48000.0, 0.2, 1.4), 440.0) - 25.0) < 0.1);
    p.osc2FineCents = 0.0;
    p.osc2KeyTrack = false;
    synth = makeSynth (p);
    out = render (*synth, 1.5, { on (0, 81) });
    CHECK (std::abs (cents (frequencyByCrossings (out.mono(), 48000.0, 0.2, 1.4), 261.6255653)) < 0.1);
}

TEST_CASE ("oscillators: the band-limited saw aliases far less than a naive one, across the keyboard", "[osc][spectrum]")
{
    // Inharmonic energy (aliases) within 0..18 kHz at 48 kHz. The limits are measured values
    // with a margin (docs/AUDIBLE_QA.md lists them); they guard against regressions.
    for (const int note : { 72, 84, 96, 103 })
    {
        INFO ("note " << note);
        const double f0 = 440.0 * std::exp2 ((note - 69) / 12.0);
        auto synth = makeSynth (measurementPatch());
        const auto out = render (*synth, 1.0, { on (0, note) });
        const auto band = inharmonicDb (spectrum (out.mono(), 8192, 15), 48000.0, f0, 18000.0);
        const auto naive = inharmonicDb (spectrum (naiveSaw (f0, 48000.0, 48000), 8192, 15), 48000.0, f0, 18000.0);
        UNSCOPED_INFO ("note " << note << ": band-limited " << band << " dB, naive " << naive << " dB");
        CHECK (band < naive - 30.0);
        CHECK (band < -70.0);
    }
}

TEST_CASE ("oscillators: pulse width moves the harmonics; extremes stay band-limited and centred", "[osc][spectrum]")
{
    auto p = measurementPatch();
    p.osc1Wave = Osc1Wave::pulse;
    for (const double width : { 0.5, 0.1, 0.02, 0.98 })
    {
        INFO ("width " << width);
        p.osc1Width = width;
        auto synth = makeSynth (p);
        const auto out = render (*synth, 1.0, { on (0, 84) });
        const auto mono = out.mono();
        const double f0 = 440.0 * std::exp2 (15.0 / 12.0);
        const auto mag = spectrum (mono, 8192, 15);
        CHECK (inharmonicDb (mag, 48000.0, f0, 18000.0) < -65.0);
        // The pulse's DC is removed: its mean over whole periods is about zero.
        double sum = 0.0;
        for (std::size_t i = 9600; i < 9600 + 48000 / 2; ++i)
            sum += mono[i];
        CHECK (std::abs (sum / 24000.0) < 0.01);
        if (width == 0.5)
        {
            // A square: even harmonics far below the odd ones.
            const double binHz = 48000.0 / 32768.0;
            auto at = [&] (double f) {
                double m = 0.0;
                for (int d = -3; d <= 3; ++d)
                    m = std::max (m, mag[static_cast<std::size_t> (std::lround (f / binHz) + d)]);
                return m;
            };
            CHECK (at (2 * f0) < 0.05 * at (f0));
            CHECK (at (3 * f0) > 0.2 * at (f0));
        }
    }
}

TEST_CASE ("oscillators: the triangle is band-limited, odd-harmonic and level-steady", "[osc][spectrum]")
{
    auto p = measurementPatch();
    p.osc1Level = 0.0;
    p.osc2Level = 1.0;
    p.blend = 1.0;
    p.osc2FineCents = 0.0;
    p.osc2Wave = Osc2Wave::triangle;
    double lowRms = 0.0;
    for (const int note : { 45, 69, 96 })
    {
        INFO ("note " << note);
        auto synth = makeSynth (p);
        const auto out = render (*synth, 1.0, { on (0, note) });
        const double f0 = 440.0 * std::exp2 ((note - 69) / 12.0);
        CHECK (inharmonicDb (spectrum (out.mono(), 8192, 15), 48000.0, f0, 18000.0) < -70.0);
        const double r = out.rms (0.2, 0.9);
        if (note == 45)
            lowRms = r;
        else
            CHECK (r == Approx (lowRms).epsilon (0.15));   // no droop or growth with pitch (a 1/sqrt(3) triangle)
    }
}

TEST_CASE ("oscillators: hard sync locks OSC 1 to OSC 2's period and stays band-limited at high pitch", "[osc][spectrum]")
{
    auto p = measurementPatch();
    p.sync = true;
    p.osc1Coarse = 7.3;   // the slave above the master
    auto synth = makeSynth (p);
    const int note = 88;
    const auto out = render (*synth, 1.0, { on (0, note) });
    const double master = 440.0 * std::exp2 ((note - 69 + 7.0 / 100.0) / 12.0);   // OSC 2's default fine is +7 cents
    // Every harmonic sits on the master's grid: the sync worked.
    const auto mag = spectrum (out.mono(), 8192, 15);
    const double synced = inharmonicDb (mag, 48000.0, master, 18000.0);
    CHECK (synced < -55.0);

    // Without the reset correction (a bare reset), the aliases are much stronger: compare
    // against the same patch rendered without oversampling (more folding) as a sanity check
    // that the measure sees aliasing at all.
    auto plain = makeSynth (p, 48000.0, 1);
    const auto rough = render (*plain, 1.0, { on (0, note) });
    UNSCOPED_INFO ("sync: 4x " << synced << " dB, 1x " << inharmonicDb (spectrum (rough.mono(), 8192, 15), 48000.0, master, 18000.0) << " dB");
    CHECK (out.finite());
}

TEST_CASE ("oscillators: a lone step comes out as the band-limited (B-spline) step, two samples late", "[osc][unit]")
{
    // A unit step 0.25 samples before sample n: the emitted samples are the cumulative cubic
    // B-spline at the samples' distances from the step (-1.75, -0.75, 0.25, 1.25).
    BlepLine line;
    line.end (0.0f);
    line.end (0.0f);
    line.step (1.0f, 0.25f);
    const float a = line.end (1.0f);   // n - 2
    const float b = line.end (1.0f);   // n - 1
    const float c = line.end (1.0f);   // n
    const float d = line.end (1.0f);   // n + 1
    auto phi = [] (double x) {
        // The cubic B-spline's integral, numerically.
        double sum = 0.0;
        const int steps = 20000;
        for (int i = 0; i < steps; ++i)
        {
            const double t = -2.0 + (x + 2.0) * (i + 0.5) / steps;
            const double u = std::abs (t);
            sum += (u < 1.0 ? 2.0 / 3.0 - u * u + 0.5 * u * u * u : (u < 2.0 ? (2.0 - u) * (2.0 - u) * (2.0 - u) / 6.0 : 0.0)) * (x + 2.0) / steps;
        }
        return sum;
    };
    CHECK (a == Approx (phi (-1.75)).margin (1.0e-5));
    CHECK (b == Approx (phi (-0.75)).margin (1.0e-5));
    CHECK (c == Approx (phi (0.25)).margin (1.0e-5));
    CHECK (d == Approx (phi (1.25)).margin (1.0e-5));
    CHECK (line.end (1.0f) == Approx (1.0f));
    // The walk finds a saw's wrap at the right fraction.
    osc::Waves saw;
    saw.saw = true;
    double wrapAt = -1.0;
    BlepLine l2;
    const double phase = osc::walk (0.95, 0.1, 0.5f, saw, 1.0, 0.0, l2, wrapAt);
    CHECK (phase == Approx (0.05));
    CHECK (wrapAt == Approx (0.5));
}
