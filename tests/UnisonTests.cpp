// UNISON (state version 24): each oscillator as up to eight detuned copies inside each voice.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <numbers>

using namespace testutil;
using Catch::Approx;

namespace
{
    /** The power of one frequency in a channel between two times (Goertzel, Hann window). */
    double powerAt (const std::vector<float>& x, double rate, double hz, double from, double to)
    {
        const auto a = static_cast<std::size_t> (from * rate), z = std::min (x.size(), static_cast<std::size_t> (to * rate));
        const double w = 2.0 * std::numbers::pi * hz / rate, coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        const double n = static_cast<double> (z - a);
        for (std::size_t i = a; i < z; ++i)
        {
            const double hann = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i - a) / n);
            const double s0 = hann * static_cast<double> (x[i]) + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    }

    double db (double ratio) { return 10.0 * std::log10 (std::max (ratio, 1.0e-30)); }

    double hz (double note) { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

    UnisonSettings unisonOf (int voices, double detune = 0.5, double blend = 0.75)
    {
        UnisonSettings u;
        u.voices = voices;
        u.detune = detune;
        u.blend = blend;
        return u;
    }

    /** The level (RMS of the left channel) of one sustained note between two times. */
    double levelOf (const Stereo& s, double from, double to) { return s.rms (from, to); }
}

TEST_CASE ("UNISON: the copies sit on an even grid from -1 to +1, the outer ones exactly at the ends", "[unison][unit]")
{
    const std::vector<std::vector<double>> expected {
        { -1.0, 1.0 },
        { -1.0, 0.0, 1.0 },
        { -1.0, -1.0 / 3.0, 1.0 / 3.0, 1.0 },
        { -1.0, -0.5, 0.0, 0.5, 1.0 },
        { -1.0, -2.0 / 3.0, -1.0 / 3.0, 0.0, 1.0 / 3.0, 2.0 / 3.0, 1.0 },
    };
    for (const auto& row : expected)
    {
        const int n = static_cast<int> (row.size());
        INFO ("n " << n);
        for (int i = 0; i < n; ++i)
            CHECK (unisonmath::position (i, n) == Approx (row[static_cast<std::size_t> (i)]).margin (1.0e-12));
    }
    for (int n = 2; n <= unisonmath::maxVoices; ++n)
    {
        CHECK (unisonmath::position (0, n) == Approx (-1.0));
        CHECK (unisonmath::position (n - 1, n) == Approx (1.0));
    }
    // The middle copy (odd), the two middle copies (even).
    CHECK (unisonmath::isCentre (1, 3));
    CHECK_FALSE (unisonmath::isCentre (0, 3));
    CHECK (unisonmath::isCentre (1, 4));
    CHECK (unisonmath::isCentre (2, 4));
    CHECK_FALSE (unisonmath::isCentre (3, 4));
}

TEST_CASE ("UNISON: every spread curve keeps the ends and only moves the inner copies", "[unison][unit]")
{
    using S = UnisonSpread;
    for (const auto s : { S::linear, S::super, S::exp, S::inv })
    {
        CHECK (unisonmath::curve (s, 1.0, 0.0) == Approx (1.0));
        CHECK (unisonmath::curve (s, -1.0, 0.0) == Approx (-1.0));
        CHECK (unisonmath::curve (s, 0.0, 0.0) == Approx (0.0).margin (1.0e-12));
    }
    CHECK (unisonmath::curve (S::linear, 0.5, 0.0) == Approx (0.5));
    CHECK (unisonmath::curve (S::super, 0.5, 0.0) == Approx (0.3505).margin (1.0e-3));   // inner copies drawn in
    CHECK (unisonmath::curve (S::exp, 0.5, 0.0) == Approx (0.25));
    CHECK (unisonmath::curve (S::exp, -0.5, 0.0) == Approx (-0.25));
    CHECK (unisonmath::curve (S::inv, 0.5, 0.0) == Approx (0.7071).margin (1.0e-3));     // pushed out
    // RANDOM: the copy's own draw, on its own side.
    CHECK (unisonmath::curve (S::random, 0.4, 0.3) == Approx (0.3));
    CHECK (unisonmath::curve (S::random, -0.4, 0.3) == Approx (-0.3));
}

TEST_CASE ("UNISON: DETUNE tapers (detune^2.5 x range): most of the knob is fine detune", "[unison][unit]")
{
    CHECK (unisonmath::detuneSemitones (0.0, 1.0) == 0.0);
    CHECK (unisonmath::detuneSemitones (0.25, 1.0) == Approx (0.03125).margin (1.0e-5));
    CHECK (unisonmath::detuneSemitones (0.5, 1.0) == Approx (0.17678).margin (1.0e-5));
    CHECK (unisonmath::detuneSemitones (0.75, 1.0) == Approx (0.48714).margin (1.0e-5));
    CHECK (unisonmath::detuneSemitones (1.0, 1.0) == Approx (1.0));
    CHECK (unisonmath::detuneSemitones (1.0, 2.0) == Approx (2.0));
    // The layout: the outer copies at the full amount, the odd count's middle copy at none.
    UnisonSettings s = unisonOf (5, 1.0);
    s.rangeSemitones = 0.5;
    const auto l = unisonmath::layout (s, {});
    CHECK (l.semitones[0] == Approx (-0.5));
    CHECK (l.semitones[4] == Approx (0.5));
    CHECK (l.semitones[2] == 0.0);
    CHECK (l.semitones[1] == Approx (-0.25));
    CHECK (l.centre == 2);
}

TEST_CASE ("UNISON: BLEND sets the outer copies against the centre, and the loudness never changes with it", "[unison][unit]")
{
    for (int n = 1; n <= unisonmath::maxVoices; ++n)
        for (const double blend : { 0.0, 0.25, 0.5, 0.75, 1.0 })
        {
            INFO ("n " << n << " blend " << blend);
            const auto l = unisonmath::layout (unisonOf (n, 0.5, blend), {});
            double power = 0.0;
            for (int i = 0; i < n; ++i)
                power += static_cast<double> (l.gain[static_cast<std::size_t> (i)]) * l.gain[static_cast<std::size_t> (i)];
            CHECK (power == Approx (1.0).margin (1.0e-5));   // (an incoherent sum: one oscillator's loudness)
        }
    // One copy: the oscillator itself at full level.
    CHECK (unisonmath::layout (unisonOf (1), {}).gain[0] == 1.0f);
    // Two: both are the centre, equal.
    const auto two = unisonmath::layout (unisonOf (2, 0.5, 0.0), {});
    CHECK (two.gain[0] == Approx (two.gain[1]));
    // Seven: faint outer copies at BLEND 0, louder than the centre at 100 %.
    const auto low = unisonmath::layout (unisonOf (7, 0.5, 0.0), {});
    const auto high = unisonmath::layout (unisonOf (7, 0.5, 1.0), {});
    CHECK (low.gain[0] < 0.1f * low.gain[3]);
    CHECK (high.gain[0] > high.gain[3]);
}

TEST_CASE ("UNISON: WIDTH sends the copies left and right by turns; 0 is mono", "[unison][unit]")
{
    UnisonSettings s = unisonOf (5);
    s.width = 1.0;
    const auto wide = unisonmath::layout (s, {});
    CHECK (wide.place[2] == 0.5f);   // (the middle copy stays in the middle)
    CHECK (wide.place[0] == 1.0f);
    CHECK (wide.place[1] == 0.0f);
    CHECK (wide.place[3] == 1.0f);
    CHECK (wide.place[4] == 0.0f);
    s.width = 0.0;
    const auto mono = unisonmath::layout (s, {});
    for (int i = 0; i < 5; ++i)
        CHECK (mono.place[static_cast<std::size_t> (i)] == 0.5f);
}

TEST_CASE ("UNISON: one copy is the oscillator exactly as before, on every model", "[unison][unit]")
{
    // An explicit one-copy setting (any detune, blend, width) is the default path, bit for bit.
    for (const auto model : { OscModel::classic, OscModel::dco, OscModel::wide })
    {
        INFO ("model " << static_cast<int> (model));
        Patch p = measurementPatch();
        p.osc1Model = model;
        p.osc2Level = 0.5;
        p.blend = 0.5;
        Patch q = p;
        for (auto& u : q.oscUnison)
            u = unisonOf (1, 0.9, 0.1);
        const std::vector<Event> notes { on (0, 48, 100), on (4800, 55, 90), off (30000, 48), off (34000, 55) };
        const auto a = render (*makeSynth (p), 1.0, notes);
        const auto b = render (*makeSynth (q), 1.0, notes);
        REQUIRE (a.left.size() == b.left.size());
        float diff = 0.0f;
        for (std::size_t i = 0; i < a.left.size(); ++i)
            diff = std::max ({ diff, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
        CHECK (diff == 0.0f);
    }
}

TEST_CASE ("UNISON: the copies sound at their detuned pitches", "[unison][unit]")
{
    // Three copies of a saw, the outer ones a semitone either side (DETUNE 100 %, RANGE 1):
    // energy at the note and a semitone either side, little in between.
    Patch p = measurementPatch();
    p.oscUnison[0] = unisonOf (3, 1.0, 1.0);
    const auto s = render (*makeSynth (p), 1.5, { on (0, 57, 100) });
    REQUIRE (s.finite());
    const double f = hz (57);
    const double centre = powerAt (s.left, s.rate, f, 0.3, 1.4);
    const double up = powerAt (s.left, s.rate, f * std::exp2 (1.0 / 12.0), 0.3, 1.4);
    const double down = powerAt (s.left, s.rate, f * std::exp2 (-1.0 / 12.0), 0.3, 1.4);
    const double between = powerAt (s.left, s.rate, f * std::exp2 (0.5 / 12.0), 0.3, 1.4);
    INFO ("centre " << db (centre) << " up " << db (up) << " down " << db (down) << " between " << db (between));
    CHECK (db (up / between) > 15.0);
    CHECK (db (down / between) > 15.0);
    CHECK (db (centre / between) > 15.0);
    // One copy: nothing a semitone up.
    Patch one = measurementPatch();
    const auto o = render (*makeSynth (one), 1.5, { on (0, 57, 100) });
    CHECK (db (powerAt (o.left, o.rate, f * std::exp2 (1.0 / 12.0), 0.3, 1.4) / up) < -20.0);
}

TEST_CASE ("UNISON: as loud as one oscillator, on every model, whatever BLEND", "[unison][unit]")
{
    for (const auto model : { OscModel::classic, OscModel::dco, OscModel::wide })
        for (const double blend : { 0.0, 0.75, 1.0 })
        {
            INFO ("model " << static_cast<int> (model) << " blend " << blend);
            Patch one = measurementPatch();
            one.osc1Model = model;
            Patch seven = one;
            seven.oscUnison[0] = unisonOf (7, 0.5, blend);
            const double a = levelOf (render (*makeSynth (one), 1.2, { on (0, 50, 100) }), 0.2, 1.2);
            const auto s = render (*makeSynth (seven), 1.2, { on (0, 50, 100) });
            REQUIRE (s.finite());
            const double b = levelOf (s, 0.2, 1.2);
            CHECK (std::abs (20.0 * std::log10 (b / a)) < 2.0);
        }
}

TEST_CASE ("UNISON: polyphonic - every note keeps its own attack with its copies", "[unison][unit]")
{
    // The beta tester's case: a long attack, a second note while the first is held. With the
    // copies inside each voice the second note swells in from silence on its own.
    Patch p = measurementPatch();
    p.ampAttack = 2.0;
    p.oscUnison[0] = unisonOf (7);
    const auto both = render (*makeSynth (p), 3.0, { on (0, 48, 100), on (48000, 54, 100) });
    const auto first = render (*makeSynth (p), 3.0, { on (0, 48, 100) });
    REQUIRE (both.finite());
    // The second note alone: both notes less the first (each voice its own; no effects).
    Stereo second = both;
    for (std::size_t i = 0; i < second.left.size(); ++i)
    {
        second.left[i] -= first.left[i];
        second.right[i] -= first.right[i];
    }
    auto at = [&] (double t) { return second.rms (t, t + 0.1); };
    CHECK (at (0.5) == 0.0);   // (nothing before it is played)
    // (1.0 s: its first 100 ms - still near silence; then it rises.)
    CHECK (20.0 * std::log10 (at (1.0) / at (2.8)) < -15.0);
    CHECK (at (1.4) < at (2.0));
    CHECK (at (2.0) < at (2.8));
}

TEST_CASE ("UNISON: SUPER SAW, DCO sub, sync and OSC 3 - copies on every path, finite and heard", "[unison][unit]")
{
    Patch wide = measurementPatch();
    wide.osc1Model = OscModel::wide;
    wide.wide1 = { WideWave::superSaw, 0.7, 0.5 };
    Patch dco = measurementPatch();
    dco.osc1Model = OscModel::dco;
    dco.dco1.subLevel = 0.6;
    Patch synced = measurementPatch();
    synced.sync = true;
    synced.osc2Level = 0.6;
    synced.osc1Coarse = 7.0;
    Patch osc3 = measurementPatch();
    osc3.osc1Level = 0.0;
    osc3.hybridType = HybridType::oscillator;
    osc3.organic.levelDb = 0.0;
    for (auto* p : { &wide, &dco, &synced, &osc3 })
    {
        Patch one = *p;
        for (auto& u : p->oscUnison)
            u = unisonOf (6, 0.6, 0.8);
        const auto a = render (*makeSynth (one), 1.0, { on (0, 48, 100) });
        const auto b = render (*makeSynth (*p), 1.0, { on (0, 48, 100) });
        REQUIRE (b.finite());
        CHECK (b.peak() < 4.0f);
        CHECK (levelOf (b, 0.2, 1.0) > 0.25 * levelOf (a, 0.2, 1.0));
        float diff = 0.0f;
        for (std::size_t i = 0; i < a.left.size(); ++i)
            diff = std::max (diff, std::abs (a.left[i] - b.left[i]));
        CHECK (diff > 1.0e-3f);   // (the copies are heard)
    }
}

TEST_CASE ("UNISON: deterministic - the same render twice; RANDOM gives each note its own spread", "[unison][unit]")
{
    Patch p = measurementPatch();
    p.oscUnison[0] = unisonOf (5, 0.8);
    p.oscUnison[0].spread = UnisonSpread::random;
    const std::vector<Event> notes { on (0, 48, 100), off (20000, 48), on (24000, 48, 100) };
    const auto a = render (*makeSynth (p), 1.0, notes);
    const auto b = render (*makeSynth (p), 1.0, notes);
    float diff = 0.0f;
    for (std::size_t i = 0; i < a.left.size(); ++i)
        diff = std::max (diff, std::abs (a.left[i] - b.left[i]));
    CHECK (diff == 0.0f);
}


namespace
{
    /** The largest sample difference between two channels. */
    float maxDiff (const std::vector<float>& a, const std::vector<float>& b)
    {
        float d = 0.0f;
        for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            d = std::max (d, std::abs (a[i] - b[i]));
        return d;
    }

    /** The side's level against the mid's (dB), between two times. */
    double sideToMidDb (const Stereo& s, double from, double to)
    {
        const auto a = static_cast<std::size_t> (from * s.rate), z = std::min (s.left.size(), static_cast<std::size_t> (to * s.rate));
        double mid = 0.0, side = 0.0;
        for (std::size_t i = a; i < z; ++i)
        {
            const double m = 0.5 * (s.left[i] + s.right[i]), d = 0.5 * (s.right[i] - s.left[i]);
            mid += m * m;
            side += d * d;
        }
        return db (side / std::max (mid, 1.0e-30));
    }
}

TEST_CASE ("UNISON WIDTH: 0 is mono; wider spreads the copies apart, equal power (never louder)", "[unison][unit]")
{
    for (const auto model : { OscModel::classic, OscModel::dco, OscModel::wide })
    {
        INFO ("model " << static_cast<int> (model));
        Patch narrow = measurementPatch();
        narrow.osc1Model = model;
        narrow.oscUnison[0] = unisonOf (5, 0.4);
        narrow.oscUnison[0].width = 0.0;
        Patch half = narrow, full = narrow;
        half.oscUnison[0].width = 0.5;
        full.oscUnison[0].width = 1.0;
        const std::vector<Event> notes { on (0, 50, 100), on (9600, 57, 90), off (40000, 50), off (44000, 57) };
        const auto n = render (*makeSynth (narrow), 1.2, notes);
        const auto h = render (*makeSynth (half), 1.2, notes);
        const auto f = render (*makeSynth (full), 1.2, notes);
        REQUIRE (f.finite());
        CHECK (maxDiff (n.left, n.right) == 0.0f);   // (mono: no side at all)
        const double sideHalf = sideToMidDb (h, 0.3, 0.8), sideFull = sideToMidDb (f, 0.3, 0.8);
        INFO ("side/mid: half " << sideHalf << " dB, full " << sideFull << " dB");
        CHECK (sideFull > -12.0);       // (the copies clearly apart)
        CHECK (sideHalf < sideFull - 3.0);
        // As loud, left and right together.
        CHECK (std::abs (20.0 * std::log10 (f.rms (0.3, 1.0) / n.rms (0.3, 1.0))) < 0.75);
        CHECK (std::abs (20.0 * std::log10 (h.rms (0.3, 1.0) / n.rms (0.3, 1.0))) < 0.75);
    }
}

TEST_CASE ("UNISON WIDTH: the side goes through the voice's filter (ladder and OTA) and card C's OSC 3 spreads too", "[unison][unit]")
{
    // A dark filter: the side as dark as the mid (it is filtered, not bypassing FILTER).
    for (const auto filter : { FilterModel::ladder, FilterModel::ota })
    {
        INFO ("filter " << static_cast<int> (filter));
        Patch p = measurementPatch();
        p.filterModel = filter;
        p.oscUnison[0] = unisonOf (6, 0.5);
        Patch dark = p;
        dark.tone = 0.15;
        const auto open = render (*makeSynth (p), 1.0, { on (0, 60, 100) });
        const auto closed = render (*makeSynth (dark), 1.0, { on (0, 60, 100) });
        REQUIRE (closed.finite());
        const double f8 = hz (60) * 8.0;
        auto sideAt = [&] (const Stereo& s, double freq) {
            std::vector<float> side (s.left.size());
            for (std::size_t i = 0; i < side.size(); ++i)
                side[i] = s.right[i] - s.left[i];
            return powerAt (side, s.rate, freq, 0.3, 0.95);
        };
        CHECK (db (sideAt (closed, f8) / sideAt (open, f8)) < -20.0);
        CHECK (sideToMidDb (open, 0.3, 0.95) > -15.0);
    }
    // Card C's OSC 3.
    Patch c = measurementPatch();
    c.osc1Level = 0.0;
    c.hybridType = HybridType::oscillator;
    c.organic.levelDb = 0.0;
    c.oscUnison[2] = unisonOf (4, 0.5);
    const auto s = render (*makeSynth (c), 1.0, { on (0, 48, 100) });
    REQUIRE (s.finite());
    CHECK (sideToMidDb (s, 0.3, 0.95) > -15.0);
    c.oscUnison[2].width = 0.0;
    const auto m = render (*makeSynth (c), 1.0, { on (0, 48, 100) });
    CHECK (maxDiff (m.left, m.right) == 0.0f);
}

TEST_CASE ("UNISON: CPU of eight voices, off / seven copies mono / seven copies wide, on A and on A + B (reported, not asserted)", "[unison][cpu][.]")
{
    struct Case { const char* name; int voices; double width; bool both; };
    for (const auto& c : { Case { "off", 1, 0.0, false }, Case { "A x7 mono", 7, 0.0, false }, Case { "A x7 wide", 7, 1.0, false },
                           Case { "A+B x7 wide", 7, 1.0, true }, Case { "A+B x8 wide", 8, 1.0, true } })
    {
        Patch p = measurementPatch();
        p.polyphony = 8;
        p.osc2Level = c.both ? 0.6 : 0.0;
        p.oscUnison[0] = unisonOf (c.voices);
        p.oscUnison[0].width = c.width;
        if (c.both)
            p.oscUnison[1] = p.oscUnison[0];
        auto s = makeSynth (p);
        for (int v = 0; v < 8; ++v)
        {
            const std::uint8_t note[3] { 0x90, static_cast<std::uint8_t> (45 + 3 * v), 100 };
            s->handleMidi (note, 3);
        }
        std::vector<float> l (512), r (512);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 48000 * 2 / 512; ++b)
            s->render (l.data(), r.data(), 512);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        WARN ("UNISON " << c.name << ", 8 voices: " << 100.0 * seconds / 2.0 << " % of one core");
    }
}
