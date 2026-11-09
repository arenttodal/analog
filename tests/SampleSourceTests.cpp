// An imported sample source (the Sample Source importer, MVP A1) in ORGANIC's place: each note
// sounds the zones its key and velocity select (overlapping ones together), at the pitch its
// root and tune give, with the zone's loop (crossfaded, or back and forth), through ORGANIC's
// level and card B. Replacing the source mid-note never clicks or leaves a voice sounding.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using namespace testutil;
using Catch::Approx;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /** A recording: a sine at `hz` (or a ramp's harmonics), `seconds` long, at `rate`. */
    std::shared_ptr<osp::PlaybackSource> tone (double hz, double seconds, double rate = 44100.0, double root = 60.0, int channels = 1)
    {
        osp::AudioData audio;
        audio.sampleRate = rate;
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.5 * std::sin (2.0 * pi * hz * static_cast<double> (i) / rate));
        audio.channels.assign (static_cast<std::size_t> (channels), x);
        return std::make_shared<osp::PlaybackSource> (audio, root, 4);
    }

    SampleZone zone (std::shared_ptr<osp::PlaybackSource> audio, int lo, int hi, double root, int velLo = 1, int velHi = 127)
    {
        SampleZone z;
        z.audio = std::move (audio);
        z.keyLo = lo;
        z.keyHi = hi;
        z.root = root;
        z.velLo = velLo;
        z.velHi = velHi;
        return z;
    }

    /** Only ORGANIC's part (the oscillators off), the filter open, a plain gate. */
    Patch sourceOnly()
    {
        auto p = measurementPatch();
        p.osc1Enabled = p.osc2Enabled = false;
        return p;
    }

    Stereo play (const Patch& p, const SampleSource* source, double seconds, std::vector<Event> events, int block = 256)
    {
        auto s = makeSynth (p);
        s->setSampleSource (source);
        return render (*s, seconds, std::move (events), block);
    }

    double cents (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }
}

TEST_CASE ("sample source: a key and velocity select their zones; overlapping zones sound together", "[samplesource]")
{
    SampleSource s;
    s.zones = { zone (tone (220, 1), 40, 55, 57), zone (tone (440, 1), 54, 70, 69), zone (tone (330, 1), 60, 60, 64, 1, 60),
                zone (tone (330, 1), 60, 60, 64, 61, 127) };
    std::array<int, SampleSource::maxLayers> out {};
    CHECK (s.select (45, 100, out) == 1);
    CHECK (out[0] == 0);
    CHECK (s.select (54, 100, out) == 2);   // (54-55: both)
    CHECK (s.select (60, 40, out) == 2);
    CHECK (out[1] == 2);
    CHECK (s.select (60, 100, out) == 2);
    CHECK (out[1] == 3);
    CHECK (s.select (90, 100, out) == 0);

    auto synth = makeSynth (sourceOnly());
    synth->setSampleSource (&s);
    render (*synth, 0.1, { on (0, 54) });
    CHECK (synth->sampleZonesSounding() == 2);
    // A key no zone covers is silent (and starts nothing).
    const auto silent = play (sourceOnly(), &s, 0.3, { on (0, 90) });
    CHECK (silent.peak() == 0.0f);
}

TEST_CASE ("sample source: a zone plays at its root, transposes from it, and follows its tune", "[samplesource]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        INFO (rate);
        SampleSource s;
        s.zones = { zone (tone (220.0, 2.0, rate, 57.0), 30, 90, 57.0) };
        const auto atRoot = play (sourceOnly(), &s, 0.6, { on (0, 57) });
        CHECK (std::abs (cents (frequencyByCrossings (atRoot.mono(), atRoot.rate, 0.1, 0.5), 220.0)) < 1.0);
        const auto fifthUp = play (sourceOnly(), &s, 0.6, { on (0, 64) });
        CHECK (std::abs (cents (frequencyByCrossings (fifthUp.mono(), fifthUp.rate, 0.1, 0.5), 220.0 * std::exp2 (7.0 / 12.0))) < 1.0);
        s.zones[0].tuneSemitones = 0.25;   // +25 cents
        const auto tuned = play (sourceOnly(), &s, 0.6, { on (0, 57) });
        CHECK (cents (frequencyByCrossings (tuned.mono(), tuned.rate, 0.1, 0.5), 220.0) == Approx (25.0).margin (1.0));
    }
}

TEST_CASE ("sample source: a looped zone sustains past its recording, its joint without a click; LOOP off plays it out", "[samplesource][loop]")
{
    SampleSource s;
    auto z = zone (tone (220.0, 1.0, 44100.0, 57.0), 30, 90, 57.0);
    z.loop.on = true;
    z.loop.start = 0.3 * 44100.0;
    z.loop.length = 0.5 * 44100.0 + 37.0;   // (not whole periods: the crossfade has to hide the joint)
    z.loop.crossfade = 0.1 * 44100.0;
    s.zones = { z };
    const auto held = play (sourceOnly(), &s, 3.0, { on (0, 57) });
    REQUIRE (held.finite());
    CHECK (held.rms (2.5, 2.9) > 0.5 * held.rms (0.1, 0.25));   // (still sounding at 2.5 s of a 1 s recording)
    // No click: the steepest step stays a sine's (0.5 at 220 Hz: about 0.5 * 2 pi 220 / 48000).
    const auto m = held.mono();
    float steepest = 0.0f;
    for (std::size_t i = static_cast<std::size_t> (0.2 * held.rate); i < m.size(); ++i)
        steepest = std::max (steepest, std::abs (m[i] - m[i - 1]));
    CHECK (steepest < 0.03f);

    // LOOP off (the card's key): the recording plays out and the voice falls silent.
    auto p = sourceOnly();
    p.organic.loop = false;
    const auto out = play (p, &s, 2.0, { on (0, 57) });
    CHECK (out.rms (1.3, 1.9) < 1.0e-6);

    // Back and forth: sustains too, its turns without a step.
    s.zones[0].loop.alternating = true;
    const auto pingPong = play (sourceOnly(), &s, 2.5, { on (0, 57) });
    CHECK (pingPong.rms (2.0, 2.4) > 0.5 * pingPong.rms (0.1, 0.25));
}

TEST_CASE ("sample source: its zones' levels and the source's level; polyphony; nothing left sounding", "[samplesource]")
{
    SampleSource s;
    s.zones = { zone (tone (220, 2.0, 44100.0, 57.0), 30, 90, 57.0) };
    const auto unity = play (sourceOnly(), &s, 0.5, { on (0, 57) });
    s.zones[0].gain = 0.5f;
    const auto half = play (sourceOnly(), &s, 0.5, { on (0, 57) });
    CHECK (half.rms (0.1, 0.4) / unity.rms (0.1, 0.4) == Approx (0.5).margin (0.01));
    s.gain = 2.0f;
    const auto back = play (sourceOnly(), &s, 0.5, { on (0, 57) });
    CHECK (back.rms (0.1, 0.4) / unity.rms (0.1, 0.4) == Approx (1.0).margin (0.01));

    // A chord: three voices, then released: silent, every voice free.
    auto synth = makeSynth (sourceOnly());
    synth->setSampleSource (&s);
    const auto chord = render (*synth, 1.5, { on (0, 57), on (0, 61), on (0, 64), off (24000, 57), off (24000, 61), off (24000, 64) });
    CHECK (chord.rms (0.1, 0.4) > unity.rms (0.1, 0.4));
    CHECK (chord.rms (1.3, 1.5) < 1.0e-6);
    CHECK (synth->activeVoices() == 0);
    CHECK (synth->sampleZonesSounding() == 0);
}

TEST_CASE ("sample source: replaced or removed while notes sound: no click, no stuck voice; deterministic", "[samplesource][swap]")
{
    SampleSource first, second;
    first.zones = { zone (tone (220, 3.0, 44100.0, 57.0), 30, 90, 57.0) };
    second.zones = { zone (tone (330, 3.0, 44100.0, 57.0), 30, 90, 57.0) };
    auto synth = makeSynth (sourceOnly());
    synth->setSampleSource (&first);
    auto a = render (*synth, 0.5, { on (0, 57) });
    synth->setSampleSource (&second);   // (the old one is not read after this returns)
    auto b = render (*synth, 0.5, {});
    REQUIRE (b.finite());
    // The held note faded out of the old source (its zones are not restarted mid-note).
    CHECK (b.rms (0.2, 0.5) < 1.0e-6);
    CHECK (std::abs (b.left[0] - a.left.back()) < 0.05f);
    // New notes play the new source.
    const auto next = render (*synth, 0.5, { on (0, 57) });
    CHECK (std::abs (cents (frequencyByCrossings (next.mono(), next.rate, 0.1, 0.45), 330.0)) < 2.0);
    synth->setSampleSource (nullptr);
    render (*synth, 0.2, { off (0, 57) });
    CHECK (synth->sampleZonesSounding() == 0);

    // The same notes, the same output.
    const auto x = play (sourceOnly(), &first, 0.6, { on (0, 57), on (100, 60) });
    const auto y = play (sourceOnly(), &first, 0.6, { on (0, 57), on (100, 60) });
    CHECK (x.left == y.left);
}
