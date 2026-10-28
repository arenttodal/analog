// Voices: polyphony, stealing, repeated notes, the sustain pedal, all notes / sound off,
// mono and unison with last-note priority and glide, the wheels.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace testutil;
using Catch::Approx;

namespace
{
    int held (const Synth& s)
    {
        int n = 0;
        for (int i = 0; i < Synth::maxVoices; ++i)
            n += s.voiceView (i).held ? 1 : 0;
        return n;
    }
    int sounding (const Synth& s)
    {
        int n = 0;
        for (int i = 0; i < Synth::maxVoices; ++i)
            n += s.voiceView (i).active ? 1 : 0;
        return n;
    }
    bool playing (const Synth& s, int note)
    {
        for (int i = 0; i < Synth::maxVoices; ++i)
            if (s.voiceView (i).held && s.voiceView (i).note == note)
                return true;
        return false;
    }
}

TEST_CASE ("voices: never more than the polyphony; the oldest held note gives way", "[voices][unit]")
{
    auto p = measurementPatch();
    p.polyphony = 4;
    auto synth = makeSynth (p);
    std::vector<Event> events;
    for (int i = 0; i < 6; ++i)
        events.push_back (on (i * 480, 60 + i));
    render (*synth, 0.2, events);
    CHECK (sounding (*synth) == 4);
    CHECK_FALSE (playing (*synth, 60));
    CHECK_FALSE (playing (*synth, 61));
    for (int n = 62; n < 66; ++n)
        CHECK (playing (*synth, n));

    // 16 voices at most, whatever is asked.
    p.polyphony = 16;
    auto big = makeSynth (p);
    events.clear();
    for (int i = 0; i < 24; ++i)
        events.push_back (on (i * 10, 40 + i));
    render (*big, 0.2, events);
    CHECK (sounding (*big) == 16);
}

TEST_CASE ("voices: a stolen voice fades out instead of clicking", "[voices][unit]")
{
    auto p = measurementPatch();
    p.polyphony = 1;
    p.ampRelease = 2.0;
    auto synth = makeSynth (p, 48000.0, 4, 64);
    const auto out = render (*synth, 0.6, { on (0, 48), on (12000, 72) }, 64);
    float largest = 0.0f;
    for (int i = 11990; i < 12400; ++i)
        largest = std::max (largest, std::abs (out.left[static_cast<std::size_t> (i)] - out.left[static_cast<std::size_t> (i - 1)]));
    float typical = 0.0f;
    for (int i = 6000; i < 11000; ++i)
        typical = std::max (typical, std::abs (out.left[static_cast<std::size_t> (i)] - out.left[static_cast<std::size_t> (i - 1)]));
    CHECK (largest < 1.5f * typical + 0.01f);
    CHECK (playing (*synth, 72));
}

TEST_CASE ("voices: the same key twice keeps one note per press; off releases the oldest", "[voices][unit]")
{
    auto synth = makeSynth (measurementPatch());
    render (*synth, 0.1, { on (0, 60), on (100, 60), off (2000, 60) });
    CHECK (held (*synth) == 1);
    CHECK (playing (*synth, 60));
    render (*synth, 0.05, { off (0, 60) });
    CHECK (held (*synth) == 0);
    // A key played again while its note rings out takes that same voice again.
    auto p = measurementPatch();
    p.ampRelease = 3.0;
    auto r = makeSynth (p);
    render (*r, 0.3, { on (0, 64), off (4800, 64), on (9600, 64) });
    CHECK (sounding (*r) == 1);
}

TEST_CASE ("voices: the sustain pedal holds released notes until it lifts", "[voices][unit]")
{
    auto synth = makeSynth (measurementPatch());
    render (*synth, 0.1, { cc (0, 64, 127), on (10, 60), on (10, 64), off (2000, 60), off (2000, 64) });
    CHECK (held (*synth) == 2);
    // A key pressed and released again under the pedal is still one voice.
    render (*synth, 0.1, { on (0, 60), off (1000, 60) });
    CHECK (held (*synth) >= 2);
    render (*synth, 0.1, { cc (0, 64, 0) });
    CHECK (held (*synth) == 0);
    render (*synth, 0.2, {});
    CHECK (sounding (*synth) == 0);   // measurement release: 20 ms
}

TEST_CASE ("voices: all notes off releases, all sound off silences at once, reset clears the wheels", "[voices][unit]")
{
    auto p = measurementPatch();
    p.ampRelease = 5.0;
    auto synth = makeSynth (p);
    render (*synth, 0.1, { on (0, 60), on (0, 67) });
    render (*synth, 0.05, { cc (0, 123, 0) });
    CHECK (held (*synth) == 0);
    CHECK (sounding (*synth) == 2);   // still releasing
    const auto silent = render (*synth, 0.1, { cc (0, 120, 0) });
    CHECK (sounding (*synth) == 0);
    CHECK (silent.rms (0.05, 0.1) < 1.0e-6);

    // CC 121: the bend back to centre.
    auto q = makeSynth (measurementPatch());
    render (*q, 0.1, { bendTo (0, 16383), on (0, 69) });
    const auto back = render (*q, 1.0, { cc (0, 121, 0) });
    CHECK (std::abs (1200.0 * std::log2 (frequencyByCrossings (back.mono(), 48000.0, 0.2, 0.9) / 440.0)) < 0.5);
}

TEST_CASE ("voices: the pitch wheel bends by the bend range, sample-accurately", "[voices][unit]")
{
    auto p = measurementPatch();
    p.bendRangeSemitones = 7.0;
    auto synth = makeSynth (p);
    const auto out = render (*synth, 1.5, { on (0, 69), bendTo (24000, 16383) });
    const double before = frequencyByCrossings (out.mono(), 48000.0, 0.1, 0.45);
    const double after = frequencyByCrossings (out.mono(), 48000.0, 0.6, 1.4);
    CHECK (1200.0 * std::log2 (before / 440.0) == Approx (0.0).margin (0.1));
    CHECK (1200.0 * std::log2 (after / 440.0) == Approx (700.0 * 8191.0 / 8192.0).margin (0.5));
}

TEST_CASE ("voices: mono plays the newest key, returns to the held one, glides between them", "[voices][unit]")
{
    auto p = measurementPatch();
    p.voiceMode = VoiceMode::mono;
    auto synth = makeSynth (p);
    render (*synth, 0.1, { on (0, 60), on (1000, 64), on (2000, 67) });
    CHECK (sounding (*synth) == 1);
    CHECK (playing (*synth, 67));
    render (*synth, 0.05, { off (0, 67) });
    CHECK (playing (*synth, 64));
    render (*synth, 0.05, { off (0, 60) });   // not the sounding one: nothing changes
    CHECK (playing (*synth, 64));
    render (*synth, 0.05, { off (0, 64) });
    CHECK (held (*synth) == 0);

    // Glide: halfway through, the pitch lies between the two notes.
    p.glideSeconds = 0.5;
    auto g = makeSynth (p);
    const auto out = render (*g, 2.5, { on (0, 57), on (24000, 69) });
    const double mid = frequencyByCrossings (out.mono(), 48000.0, 0.55, 0.62);
    CHECK (mid > 225.0);
    CHECK (mid < 435.0);
    CHECK (1200.0 * std::log2 (frequencyByCrossings (out.mono(), 48000.0, 2.0, 2.4) / 440.0) == Approx (0.0).margin (0.5));
}

TEST_CASE ("voices: unison stacks the voices, detuned and spread, on one note", "[voices][unit]")
{
    auto p = measurementPatch();
    p.unison = true;
    p.polyphony = 4;
    p.unisonDetune = 1.0;
    p.stereoSpread = 1.0;
    auto synth = makeSynth (p);
    const auto out = render (*synth, 0.5, { on (0, 60) });
    CHECK (sounding (*synth) == 4);
    // Spread: the channels differ.
    double diff = 0.0;
    for (std::size_t i = 4800; i < 24000; ++i)
        diff += std::abs (out.left[i] - out.right[i]);
    CHECK (diff > 100.0);
    render (*synth, 0.1, { on (0, 64) });
    CHECK (sounding (*synth) == 4);
    CHECK (playing (*synth, 64));
}

TEST_CASE ("voices: notes and controllers land on their sample, whatever the block size", "[voices][render]")
{
    auto p = Patch();   // INIT, effects included
    auto a = makeSynth (p, 48000.0, 4, 2048);
    auto b = makeSynth (p, 48000.0, 4, 2048);
    const std::vector<Event> events { on (100, 60), on (777, 64, 70), cc (3000, 1, 90), bendTo (5000, 12000), off (20000, 60), cc (21000, 64, 127),
                                      off (22000, 64), cc (30000, 64, 0) };
    const auto x = render (*a, 1.0, events, 2048);
    const auto y = render (*b, 1.0, events, 7);
    bool same = true;
    for (std::size_t i = 0; i < x.left.size(); ++i)
        same = same && x.left[i] == y.left[i] && x.right[i] == y.right[i];
    CHECK (same);
}
