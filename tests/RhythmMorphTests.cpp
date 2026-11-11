// RHYTHMIC MORPH, gates 5-6 in the synth: card C's loop heard on the grid and felt as C RHYTHM
// (docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md).

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <numbers>

using namespace testutil;
using Catch::Approx;
namespace mod = osp::mod;
namespace rhythm = andor::analog::rhythm;

namespace
{
    constexpr double rate = 48000.0;

    /** Ticks (2 kHz, 3 ms) on every beat of `bars` bars at `bpm`. */
    std::vector<float> ticks (double bpm, int bars)
    {
        const double beat = 60.0 / bpm;
        const auto length = static_cast<std::size_t> (std::lround (bars * 4 * beat * rate));
        std::vector<float> x (length, 0.0f);
        for (int k = 0; k < bars * 4; ++k)
        {
            const auto first = static_cast<std::size_t> (std::lround (k * beat * rate));
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.03 * rate) && first + i < length; ++i)
            {
                const double t = static_cast<double> (i) / rate;
                x[first + i] += static_cast<float> ((k % 4 == 0 ? 0.8 : 0.5) * std::exp (-t / 0.006) * std::sin (2.0 * std::numbers::pi * 2000.0 * t));
            }
        }
        return x;
    }

    const std::shared_ptr<const rhythm::PreparedLoop>& tickLoop()
    {
        static const auto loop = [] {
            const auto x = ticks (120.0, 4);
            return rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
        }();
        return loop;
    }

    /** One quiet saw on card A (C's own sample voice silent: the loop is C here). */
    Patch rhythmPatch (Patch::Rhythm::Audio audio = Patch::Rhythm::Audio::audio)
    {
        Patch p = measurementPatch();
        p.blend = 0.5;
        p.osc1Level = 0.0;
        p.osc2Level = 0.0;
        p.organic.levelDb = 0.0;
        p.ampRelease = 0.03;
        p.rhythm.enabled = true;
        p.rhythm.audio = audio;
        return p;
    }

    std::unique_ptr<Synth> loopSynth (const Patch& p, const rhythm::PreparedLoop* loop = tickLoop().get())
    {
        auto s = makeSynth (p);
        s->setRhythmLoop (loop);
        return s;
    }

    std::vector<double> tickStarts (const std::vector<float>& x, float threshold = 0.05f)
    {
        std::vector<double> t;
        int quiet = 1 << 30;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            if (std::abs (x[i]) > threshold && quiet > static_cast<int> (0.04 * rate))
                t.push_back (static_cast<double> (i) / rate);
            quiet = std::abs (x[i]) > threshold ? 0 : quiet + 1;
        }
        return t;
    }

    double rmsOf (const std::vector<float>& x, double from, double to)
    {
        double s = 0.0;
        const auto a = static_cast<std::size_t> (from * rate), z = static_cast<std::size_t> (to * rate);
        for (auto i = a; i < z; ++i)
            s += static_cast<double> (x[i]) * x[i];
        return std::sqrt (s / static_cast<double> (z - a));
    }
}

TEST_CASE ("RHYTHMIC MORPH: off (or on with no recording) is the instrument as before, bit for bit", "[rhythm][synth]")
{
    Patch p = measurementPatch();
    p.blend = 0.5;
    const std::vector<Event> notes { on (0, 57, 100), on (9000, 64, 90), off (30000, 57), off (36000, 64) };
    const auto plain = render (*makeSynth (p), 1.0, notes);
    auto withLoopOff = makeSynth (p);
    withLoopOff->setRhythmLoop (tickLoop().get());   // (loaded, RHYTHM off)
    CHECK (render (*withLoopOff, 1.0, notes).left == plain.left);
    Patch on = p;
    on.rhythm.enabled = true;
    CHECK (render (*loopSynth (on, nullptr), 1.0, notes).left == plain.left);   // (on, nothing loaded)
}

TEST_CASE ("RHYTHMIC MORPH: AUDIO - the loop on the grid while card C's notes sound, gone with their release", "[rhythm][synth]")
{
    auto s = loopSynth (rhythmPatch());
    const auto out = render (*s, 3.0, { on (0, 60, 100), off (static_cast<int> (2.2 * rate), 60) });
    const auto t = tickStarts (out.left);
    REQUIRE (t.size() >= 4);
    for (std::size_t k = 0; k < t.size(); ++k)
        CHECK (t[k] == Approx (0.5 * static_cast<double> (k)).margin (0.001));   // (the local clock: 120 BPM from the note)
    CHECK (t.back() < 2.25);
    CHECK (rmsOf (out.left, 2.4, 3.0) < 1.0e-4);   // (released: silent)
    // AUDIO is the sound alone: C RHYTHM rests (a route from it does nothing).
    auto again = loopSynth (rhythmPatch());
    const unsigned char noteOn[3] { 0x90, 60, 100 };
    again->handleMidi (noteOn, 3);
    std::vector<float> l (480), r (480);
    float peak = 0.0f;
    for (int b = 0; b < 200; ++b)
    {
        again->render (l.data(), r.data(), 480);
        peak = std::max (peak, again->rhythmValue());
    }
    CHECK (peak == 0.0f);
}

TEST_CASE ("RHYTHMIC MORPH: the host's tempo - ticks on its beats at 90 BPM", "[rhythm][synth]")
{
    auto s = loopSynth (rhythmPatch());
    const unsigned char noteOn[3] { 0x90, 60, 100 };
    s->handleMidi (noteOn, 3);
    osp::HostTiming timing;
    timing.valid = timing.playing = true;
    timing.bpm = 90.0;
    Stereo out;
    out.left.reserve (static_cast<std::size_t> (4.0 * rate));
    std::vector<float> l (480), r (480);
    for (int b = 0; b < 400; ++b)
    {
        timing.ppq = b * 480.0 / rate * 90.0 / 60.0;
        s->setTransport (timing);
        s->render (l.data(), r.data(), 480);
        out.left.insert (out.left.end(), l.begin(), l.end());
    }
    const auto t = tickStarts (out.left);
    REQUIRE (t.size() >= 5);
    for (std::size_t k = 0; k < t.size(); ++k)
        CHECK (t[k] == Approx (static_cast<double> (k) * 60.0 / 90.0).margin (0.001));
}

TEST_CASE ("RHYTHMIC MORPH: MOD - not a sound of the loop leaks, A pulses with it (C RHYTHM -> LEVEL A)", "[rhythm][synth]")
{
    Patch p = rhythmPatch (Patch::Rhythm::Audio::mod);
    p.osc1Level = 0.6;   // (A: a steady saw)
    p.rhythm.attackMs = 1.0;
    p.rhythm.releaseMs = 60.0;
    mod::Settings routes;
    routes.routes[0] = { mod::Source::cRhythm, mod::Dest::levelA, -0.9, true, -1 };   // (ducked on each tick)
    auto s = loopSynth (p);
    s->setModulation (routes, { 2, 11, 16, 17 });
    const auto out = render (*s, 2.0, { on (0, 45, 100) });
    // No 2 kHz tick anywhere: the loop's audio is muted (the modulation is not).
    {
        auto withoutRoute = loopSynth (p);
        const auto plain = render (*withoutRoute, 2.0, { on (0, 45, 100) });
        Patch off = p;
        off.rhythm.enabled = false;
        CHECK (render (*makeSynth (off), 2.0, { on (0, 45, 100) }).left == plain.left);   // (MOD with no route: as if off)
    }
    // A dips right after each beat and recovers between them.
    for (const double beat : { 0.5, 1.0, 1.5 })
    {
        const double dipped = rmsOf (out.left, beat + 0.005, beat + 0.025), recovered = rmsOf (out.left, beat + 0.3, beat + 0.45);
        INFO ("beat at " << beat);
        CHECK (20.0 * std::log10 (dipped / recovered) < -3.0);
    }
}

TEST_CASE ("RHYTHMIC MORPH: BOTH - C RHYTHM rises with each tick (the same head reads both)", "[rhythm][synth]")
{
    Patch p = rhythmPatch (Patch::Rhythm::Audio::both);
    p.rhythm.shape = Patch::Rhythm::Shape::transient;
    p.rhythm.attackMs = 0.5;
    p.rhythm.releaseMs = 40.0;
    auto s = loopSynth (p);
    const unsigned char noteOn[3] { 0x90, 60, 100 };
    s->handleMidi (noteOn, 3);
    std::vector<float> l (32), r (32), audio;
    std::vector<float> value;
    for (int b = 0; b < static_cast<int> (2.0 * rate / 32); ++b)
    {
        s->render (l.data(), r.data(), 32);
        audio.insert (audio.end(), l.begin(), l.end());
        value.push_back (s->rhythmValue());
    }
    const auto t = tickStarts (audio);
    REQUIRE (t.size() >= 3);
    for (std::size_t k = 1; k < t.size(); ++k)
    {
        // The first 32-sample block in which C RHYTHM is up: within 2 ms of the tick's start.
        const auto first = static_cast<std::size_t> (t[k] * rate / 32.0);
        std::size_t up = first;
        while (up < value.size() && value[up] < 0.2f)
            ++up;
        INFO ("tick " << k);
        CHECK ((static_cast<double> (up) * 32.0 / rate - t[k]) < 0.002);
        // Before it (20 ms earlier) it was down.
        CHECK (value[static_cast<std::size_t> ((t[k] - 0.02) * rate / 32.0)] < 0.2f);
    }
}

TEST_CASE ("RHYTHMIC MORPH: SATELLITE C moves the loop; card C muted silences it but keeps C RHYTHM", "[rhythm][synth]")
{
    {
        Patch p = rhythmPatch();
        p.satellite[2].enabled = true;
        p.satellite[2].depth = 0.0;
        p.satellite[2].offset = 1.0;
        const auto out = render (*loopSynth (p), 1.5, { on (0, 60, 100) });
        CHECK (rmsOf (out.right, 0.2, 1.5) > 1.0e-3);
        CHECK (rmsOf (out.left, 0.2, 1.5) < 1.0e-6);
    }
    {
        Patch p = rhythmPatch (Patch::Rhythm::Audio::both);
        p.organic.heard = false;   // (C's mute)
        auto s = loopSynth (p);
        const unsigned char noteOn[3] { 0x90, 60, 100 };
        s->handleMidi (noteOn, 3);
        std::vector<float> l (480), r (480);
        float peak = 0.0f, loudest = 0.0f;
        for (int b = 0; b < 200; ++b)
        {
            s->render (l.data(), r.data(), 480);
            peak = std::max (peak, s->rhythmValue());
            for (const float v : l)
                loudest = std::max (loudest, std::abs (v));
        }
        CHECK (loudest < 1.0e-6f);   // (muted)
        CHECK (peak > 0.2f);         // (felt)
    }
}


TEST_CASE ("RHYTHMIC MORPH: CPU (16 held voices; off, AUDIO, BOTH with a route)", "[.][rhythm-cpu]")
{
    auto measure = [] (const Patch& p, bool route, int oversampling) {
        auto s = makeSynth (p, 48000.0, oversampling, 512);
        s->setRhythmLoop (tickLoop().get());
        if (route)
        {
            mod::Settings routes;
            routes.routes[0] = { mod::Source::cRhythm, mod::Dest::levelA, -0.6, true, -1 };
            s->setModulation (routes, { 2, 11, 16, 17 });
        }
        for (int i = 0; i < 16; ++i)
        {
            const std::uint8_t noteOn[3] { 0x90, static_cast<std::uint8_t> (36 + 3 * i), 100 };
            s->handleMidi (noteOn, 3);
        }
        std::vector<float> l (512), r (512);
        for (int b = 0; b < 20; ++b)
            s->render (l.data(), r.data(), 512);
        const int blocks = 400;
        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
            s->render (l.data(), r.data(), 512);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        return 100.0 * seconds / (blocks * 512.0 / 48000.0);
    };
    Patch off = measurementPatch();
    off.blend = 0.5;
    Patch audio = off, both = off;
    audio.rhythm.enabled = both.rhythm.enabled = true;
    both.rhythm.audio = Patch::Rhythm::Audio::both;
    for (const int os : { 1, 4 })
        for (int run = 0; run < 3; ++run)
            WARN ("oversampling " << os << ": off " << measure (off, false, os) << " %, AUDIO " << measure (audio, false, os) << " %, BOTH + route " << measure (both, true, os) << " %");
}

TEST_CASE ("RHYTHMIC MORPH: MORPH acts on the heard loop - 0 % A + B, 50 % the loop's imprint on A + B, 100 % the loop", "[rhythm][synth][morph]")
{
    // A sustained saw on card A and the ticks heard as card C's loop (AUDIO). The keys hand the
    // recording to the loop, so the voices' own MORPH has no C: the GLOBAL MORPH takes the loop.
    auto patch = [] (bool rhythmOn, bool morphOn, double position, double osc1) {
        Patch p = rhythmPatch();
        p.osc1Level = osc1;
        p.rhythm.enabled = rhythmOn;
        p.morph.enabled = morphOn;
        p.morph.position = position;
        return p;
    };
    const std::vector<Event> held { on (0, 48, 100), off (static_cast<int> (2.6 * rate), 48) };
    auto play = [&] (const Patch& p) { return render (*loopSynth (p), 3.0, held).left; };
    const auto aAlone = play (patch (false, false, 0.5, 0.3));      // (A, no loop)
    const auto beside = play (patch (true, false, 0.5, 0.3));       // MORPH off: A and the loop side by side
    const auto loopAlone = play (patch (true, false, 0.5, 0.0));    // (the loop, no A)
    const auto at0 = play (patch (true, true, 0.0, 0.3));
    const auto at50 = play (patch (true, true, 0.5, 0.3));
    const auto at100 = play (patch (true, true, 1.0, 0.3));
    auto distance = [] (const std::vector<float>& x, const std::vector<float>& y) {
        double d = 0.0, r = 0.0;
        for (std::size_t i = static_cast<std::size_t> (0.3 * rate); i < static_cast<std::size_t> (2.4 * rate); ++i)
        {
            d += std::pow (x[i] - y[i], 2.0);
            r += std::pow (y[i], 2.0);
        }
        return std::sqrt (d / std::max (1.0e-30, r));
    };
    INFO ("0 % from A " << distance (at0, aAlone) << "; 100 % from the loop " << distance (at100, loopAlone) << "; 50 % from A " << distance (at50, aAlone)
                        << ", from A beside the loop " << distance (at50, beside));
    CHECK (distance (beside, aAlone) > 0.1);       // (the loop is heard with MORPH off, as before)
    CHECK (distance (at0, aAlone) < 1.0e-3);       // 0 %: A + B alone
    CHECK (distance (at100, loopAlone) < 1.0e-3);  // 100 %: the loop alone
    CHECK (distance (at50, aAlone) > 0.1);         // 50 %: neither A ...
    CHECK (distance (at50, beside) > 0.1);         // ... nor A beside the loop: A in the loop's imprint
    CHECK (distance (at50, loopAlone) > 0.1);
    // Between the ticks (the loop silent) A sounds; on them A takes the tick's colour: A's
    // level moves with the loop's rhythm.
    double onTicks = 0.0, between = 0.0;
    for (int k = 2; k < 4; ++k)
    {
        onTicks += rmsOf (at50, 0.5 * k + 0.001, 0.5 * k + 0.02);
        between += rmsOf (at50, 0.5 * k + 0.25, 0.5 * k + 0.45);
    }
    INFO ("on the ticks " << onTicks << ", between " << between);
    CHECK (std::abs (onTicks - between) > 0.1 * between);
}

TEST_CASE ("RHYTHMIC MORPH: MORPH in MOD is the voices' own (the loop silent) - unchanged", "[rhythm][synth][morph]")
{
    Patch p = rhythmPatch (Patch::Rhythm::Audio::mod);
    p.osc1Level = 0.3;
    p.morph.enabled = true;
    Patch noRhythm = p;
    noRhythm.rhythm.enabled = false;
    const std::vector<Event> held { on (0, 48, 100), off (static_cast<int> (1.6 * rate), 48) };
    CHECK (render (*loopSynth (p), 2.0, held).left == render (*loopSynth (noRhythm), 2.0, held).left);
}
