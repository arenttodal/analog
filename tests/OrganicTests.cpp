// ORGANIC in the synth: a recording (ANDOR OSP's prepared model) played by each voice beside
// the oscillators, in tune over two octaves and beyond, its root overridable, held through
// ANDOR OSP's loop, reversed, played out (FOLLOW off), replaced without a click, layered.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace testutil;

namespace
{
    double cents (double measured, double expected) { return 1200.0 * std::log2 (measured / expected); }

    /** A recording: a sine (or harmonic tone) at `hz`, `seconds` long, rooted at `root`; a loop
        from `loopFrom` back to `loopTo` seconds (as ANDOR OSP's continuation would mark it). */
    std::shared_ptr<osp::InstrumentModel> recording (double hz, double seconds, double root, double rate = 44100.0, double loopFrom = -1.0,
                                                     double loopTo = 0.0, bool harmonics = false)
    {
        osp::AudioData audio;
        audio.sampleRate = rate;
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate;
            double v = std::sin (2.0 * 3.14159265358979 * hz * t);
            if (harmonics)
                for (int h = 2; h <= 6; ++h)
                    v += std::sin (2.0 * 3.14159265358979 * hz * h * t) / h;
            x[i] = static_cast<float> (0.5 * v);
        }
        audio.channels = { x, x };   // (stereo: summed to mono in the voice)
        auto model = std::make_shared<osp::InstrumentModel>();
        model->rootMidi = root;
        model->original.source = std::make_shared<osp::PlaybackSource> (audio, root, 4);
        if (loopFrom > 0.0)
        {
            auto& c = model->original.continuation;
            c.canSustain = true;
            // (whole periods apart: the crossfade joins matching waveforms)
            const double period = rate / hz;
            const double from = std::round (loopFrom * rate / period) * period, to = std::round (loopTo * rate / period) * period;
            c.jumps.push_back ({ from, to, 0.01 * rate, 1.0f, 1.0f });
            c.backstop = 0;
            c.bestLoop = 0;
        }
        return model;
    }

    /** ORGANIC alone (both oscillators off), the filter open, a plain gate. */
    Patch organicOnly()
    {
        auto p = measurementPatch();
        p.osc1Enabled = p.osc2Enabled = false;
        return p;
    }

    Stereo play (const Patch& p, const osp::InstrumentModel* model, double seconds, std::vector<Event> events)
    {
        auto s = makeSynth (p);
        s->setOrganic (model);
        return render (*s, seconds, std::move (events));
    }
}

TEST_CASE ("organic: a recording plays in tune two octaves around its root and beyond, at every rate", "[organic]")
{
    const auto model = recording (440.0, 3.0, 69.0);
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int note : { 45, 57, 69, 81, 93 })   // two octaves either side
        {
            INFO ("rate " << rate << ", note " << note);
            auto s = makeSynth (organicOnly(), rate);
            s->setOrganic (model.get());
            const auto out = render (*s, 1.0, { on (0, note) });
            CHECK (out.finite());
            const double error = cents (frequencyByCrossings (out.mono(), rate, 0.2, 0.9), 440.0 * std::exp2 ((note - 69) / 12.0));
            UNSCOPED_INFO ("note " << note << " at " << rate << ": " << error << " cents");
            // Within an octave of the root 1 cent; at two octaves (the sweet spot's edge) 5.
            CHECK (std::abs (error) < (std::abs (note - 69) <= 12 ? 1.0 : 5.0));
        }
    // Far outside: three octaves up still plays, finite (the source aliases nothing: Hermite at 4x).
    const auto high = play (organicOnly(), model.get(), 0.5, { on (0, 105) });
    CHECK (high.finite());
    CHECK (high.rms (0.1, 0.4) > 0.01);
}

TEST_CASE ("organic: the root override and the card's TUNE move the pitch; LEVEL and MUTE its loudness", "[organic]")
{
    const auto model = recording (440.0, 2.0, 69.0);
    auto p = organicOnly();
    p.organic.rootOverride = 57;   // the recording taken as A3: A4 plays it an octave up
    auto out = play (p, model.get(), 1.0, { on (0, 69) });
    CHECK (std::abs (cents (frequencyByCrossings (out.mono(), 48000.0, 0.2, 0.9), 880.0)) < 1.0);
    p.organic.rootOverride = -1;
    p.organic.tuneSemitones = 7.0;
    out = play (p, model.get(), 1.0, { on (0, 69) });
    CHECK (std::abs (cents (frequencyByCrossings (out.mono(), 48000.0, 0.2, 0.9), 440.0 * std::exp2 (7.0 / 12.0))) < 1.0);
    p.organic.tuneSemitones = 0.0;
    const auto unity = play (p, model.get(), 0.6, { on (0, 69) });
    p.organic.levelDb = -12.0;
    const auto quieter = play (p, model.get(), 0.6, { on (0, 69) });
    CHECK (20.0 * std::log10 (quieter.rms (0.1, 0.5) / unity.rms (0.1, 0.5)) == Catch::Approx (-12.0).margin (0.3));
    p.organic.heard = false;
    CHECK (play (p, model.get(), 0.6, { on (0, 69) }).peak() == 0.0f);
}

TEST_CASE ("organic: a held note cycles through the recording's loop; without LOOP it ends with the recording", "[organic][loop]")
{
    const auto model = recording (220.0, 0.6, 57.0, 44100.0, 0.45, 0.15);
    auto p = organicOnly();
    const auto looped = play (p, model.get(), 2.5, { on (0, 57) });
    CHECK (looped.rms (2.0, 2.4) > 0.5 * looped.rms (0.1, 0.4));
    // Across the loop's joins: no step larger than the sine's own.
    float steady = 0.0f, worst = 0.0f;
    for (std::size_t i = 4800; i < 19000; ++i)
        steady = std::max (steady, std::abs (looped.left[i + 1] - looped.left[i]));
    for (std::size_t i = 19000; i + 1 < looped.left.size(); ++i)
        worst = std::max (worst, std::abs (looped.left[i + 1] - looped.left[i]));
    CHECK (worst < 1.3f * steady);
    p.organic.loop = false;
    const auto once = play (p, model.get(), 2.5, { on (0, 57) });
    CHECK (once.rms (1.0, 2.4) < 1.0e-4);
}

TEST_CASE ("organic: REVERSE plays it backwards; FOLLOW off lets a released note play the recording out", "[organic]")
{
    // A tone that rises: backwards it falls.
    const auto model = recording (220.0, 0.8, 57.0, 44100.0, -1.0, 0.0, true);
    auto p = organicOnly();
    p.organic.reverse = true;
    const auto backwards = play (p, model.get(), 1.0, { on (0, 57) });
    CHECK (backwards.finite());
    CHECK (backwards.rms (0.1, 0.6) > 0.01);
    CHECK (backwards.rms (0.9, 1.0) < 1.0e-4);   // (from the end to the start: 0.8 s)
    p.organic.reverse = false;
    p.ampRelease = 0.02;
    const auto follows = play (p, model.get(), 1.0, { on (0, 57), off (4800, 57) });
    CHECK (follows.rms (0.3, 0.5) < 1.0e-4);
    p.organic.follow = false;
    const auto playsOut = play (p, model.get(), 1.0, { on (0, 57), off (4800, 57) });
    CHECK (playsOut.rms (0.3, 0.5) > 0.01);       // after the key, before the recording's end
    CHECK (playsOut.rms (0.9, 1.0) < 1.0e-4);     // then released
}

TEST_CASE ("organic: a new recording replaces the old without a click; notes then play the new one", "[organic][swap]")
{
    const auto first = recording (220.0, 3.0, 57.0);
    const auto second = recording (330.0, 3.0, 57.0);
    auto s = makeSynth (organicOnly());
    s->setOrganic (first.get());
    Stereo out;
    out.left.assign (48000, 0.0f);
    out.right.assign (48000, 0.0f);
    const std::uint8_t note[3] { 0x90, 57, 100 };
    s->handleMidi (note, 3);
    s->render (out.left.data(), out.right.data(), 20000);
    s->setOrganic (second.get());   // (the old model is not read after this returns)
    s->render (out.left.data() + 20000, out.right.data() + 20000, 28000);
    float steady = 0.0f, around = 0.0f;
    for (int i = 5000; i < 19000; ++i)
        steady = std::max (steady, std::abs (out.left[static_cast<std::size_t> (i + 1)] - out.left[static_cast<std::size_t> (i)]));
    for (int i = 19900; i < 22000; ++i)
        around = std::max (around, std::abs (out.left[static_cast<std::size_t> (i + 1)] - out.left[static_cast<std::size_t> (i)]));
    CHECK (around <= 1.2f * steady);
    CHECK (out.rms (0.6, 0.9) < 1.0e-4);   // the old note faded; the new recording waits for a note
    const auto next = play (organicOnly(), second.get(), 0.8, { on (0, 57) });
    CHECK (std::abs (cents (frequencyByCrossings (next.mono(), 48000.0, 0.2, 0.7), 330.0)) < 1.0);
}

TEST_CASE ("organic: layered with the oscillators, through the filter; none loaded: exactly the synth", "[organic]")
{
    const auto model = recording (220.0, 2.0, 57.0);
    auto p = measurementPatch();
    const auto synth = play (p, nullptr, 0.6, { on (0, 57) });
    const auto layered = play (p, model.get(), 0.6, { on (0, 57) });
    const auto alone = play (organicOnly(), model.get(), 0.6, { on (0, 57) });
    CHECK (layered.rms (0.1, 0.5) > synth.rms (0.1, 0.5));
    CHECK (layered.rms (0.1, 0.5) > alone.rms (0.1, 0.5));
    // No recording: the synth bit for bit (the original path).
    auto s = makeSynth (p);
    const auto plain = render (*s, 0.6, { on (0, 57) });
    CHECK (plain.left == synth.left);
    // The filter shapes it: closed, the recording is darker.
    auto closed = organicOnly();
    closed.tone = 0.3;
    CHECK (play (closed, model.get(), 0.6, { on (0, 81) }).rms (0.1, 0.5) < 0.5 * play (organicOnly(), model.get(), 0.6, { on (0, 81) }).rms (0.1, 0.5));
}
