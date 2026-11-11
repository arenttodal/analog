// SATELLITE, gates 1-2: each card's own stereo motion (docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md).
// Headless: three distinguishable cards (A low, B middle, C high), measured by their own
// frequencies in each channel.

#include "TestUtil.h"

#include "fx/FxEngine.h"
#include "fx/FxGraph.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numbers>
#include <string>

using namespace testutil;
using Catch::Approx;
namespace sat = osp::satellite;

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

    /** Three cards, each its own: A OSC 1 (saw), B OSC 2 (saw), C OSC 3 (the hybrid's
        oscillator); the filter open, no effects, no spread. */
    Patch threeCards()
    {
        Patch p = measurementPatch();
        p.blend = 0.5;   // (BLEND in the middle: both oscillators at their levels)
        p.osc1Level = 0.6;
        p.osc2Level = 0.6;
        p.hybridType = HybridType::oscillator;
        p.organic.levelDb = -4.0;
        return p;
    }

    sat::Settings fixedAt (double offset)
    {
        sat::Settings s;
        s.enabled = true;
        s.depth = 0.0;
        s.offset = offset;
        return s;
    }

    /** Card channels (the per-card arpeggiator's lanes): 1 + the card mask. */
    constexpr int chA = 2, chB = 3, chC = 5;

    Stereo playCards (const Patch& p, std::vector<Event> events, double seconds, bool routing = true, int block = 256)
    {
        auto s = makeSynth (p);
        s->setCardRouting (routing);
        return render (*s, seconds, std::move (events), block);
    }

    float maxDifference (const Stereo& a, const Stereo& b, double from = 0.0)
    {
        float d = 0.0f;
        for (auto i = static_cast<std::size_t> (from * a.rate); i < std::min (a.left.size(), b.left.size()); ++i)
            d = std::max ({ d, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
        return d;
    }

    /** The place heard (-1..1) of a steady sound in a window: the law's own inverse. */
    double placeOf (const Stereo& s, double from, double to)
    {
        double l = 0.0, r = 0.0;
        for (auto i = static_cast<std::size_t> (from * s.rate); i < static_cast<std::size_t> (to * s.rate); ++i)
        {
            l += static_cast<double> (s.left[i]) * s.left[i];
            r += static_cast<double> (s.right[i]) * s.right[i];
        }
        return std::atan2 (std::sqrt (r), std::sqrt (l)) / (0.25 * std::numbers::pi) - 1.0;
    }
}

TEST_CASE ("SATELLITE: all off is the original path - settings that are off change nothing, bit for bit", "[satellite][unit]")
{
    Patch p = threeCards();
    const std::vector<Event> notes { on (0, 48, 100), on (2400, 61, 90, chB), on (4800, 74, 90, chC), off (36000, 48), off (36000, 61, chB),
                                     off (36000, 74, chC) };
    const auto plain = playCards (p, notes, 1.0);
    for (auto& s : p.satellite)
    {
        s.mode = sat::Mode::orbit;
        s.depth = 1.0;
        s.offset = 0.7;
        s.width = 0.2;
        s.timing = sat::Timing::free;
        s.rateHz = 3.0;
    }   // (enabled stays false)
    const auto stillOff = playCards (p, notes, 1.0);
    CHECK (maxDifference (plain, stillOff) == 0.0f);
}

TEST_CASE ("SATELLITE: each card's own notes at its own place - no energy where it was not sent", "[satellite][unit]")
{
    Patch p = threeCards();
    p.satellite[0] = fixedAt (-1.0);   // A hard left
    p.satellite[2] = fixedAt (1.0);    // C hard right; B not moving (centre)
    const auto s = playCards (p, { on (0, 48, 100, chA), on (0, 61, 100, chB), on (0, 74, 100, chC) }, 0.8);
    REQUIRE (s.finite());
    const double fa = hz (48), fb = hz (61), fc = hz (74);
    // A only on the left, C only on the right (the law's zero: nothing at all on the far side).
    CHECK (db (powerAt (s.right, s.rate, fa, 0.3, 0.8) / powerAt (s.left, s.rate, fa, 0.3, 0.8)) < -60.0);
    CHECK (db (powerAt (s.left, s.rate, fc, 0.3, 0.8) / powerAt (s.right, s.rate, fc, 0.3, 0.8)) < -60.0);
    // B where it was: in the middle.
    CHECK (std::abs (db (powerAt (s.left, s.rate, fb, 0.3, 0.8) / powerAt (s.right, s.rate, fb, 0.3, 0.8))) < 0.1);
    // Moving C did not move A or B (their power in each channel as with C still).
    Patch still = p;
    still.satellite[2].enabled = false;
    const auto ref = playCards (still, { on (0, 48, 100, chA), on (0, 61, 100, chB), on (0, 74, 100, chC) }, 0.8);
    CHECK (std::abs (db (powerAt (s.left, s.rate, fb, 0.3, 0.8) / powerAt (ref.left, ref.rate, fb, 0.3, 0.8))) < 0.05);
    CHECK (std::abs (db (powerAt (s.left, s.rate, fa, 0.3, 0.8) / powerAt (ref.left, ref.rate, fa, 0.3, 0.8))) < 0.05);
}

TEST_CASE ("SATELLITE: a note on all three cards - only the moving card leaves; the rest keeps its place", "[satellite][unit]")
{
    Patch p = threeCards();
    p.osc2Coarse = 13.0;   // (B a ninth up, C two octaves and a tone: each its own frequency)
    p.osc3Coarse = 26.0;
    p.satellite[1] = fixedAt (1.0);   // B hard right
    const auto s = playCards (p, { on (0, 48, 100) }, 0.8, false);
    REQUIRE (s.finite());
    const double fa = hz (48), fb = hz (61), fc = hz (74);
    // B leaves the left channel. What stays is the shared filter's interplay: the ladder
    // saturates the three cards' sum a little more than B alone, so a few percent of B (anti-
    // phase) stays at the voice's place - about -30 dB at these hot levels, and it falls away
    // as the levels fall (the filter is then nearly linear).
    const double leak = db (powerAt (s.left, s.rate, fb, 0.3, 0.8) / powerAt (s.right, s.rate, fb, 0.3, 0.8));
    CHECK (leak < -25.0);
    {
        Patch quiet = p;
        quiet.osc1Level = quiet.osc2Level = 0.15;
        quiet.organic.levelDb = -16.0;
        const auto q = playCards (quiet, { on (0, 48, 100) }, 0.8, false);
        const double quietLeak = db (powerAt (q.left, q.rate, fb, 0.3, 0.8) / powerAt (q.right, q.rate, fb, 0.3, 0.8));
        UNSCOPED_INFO ("B left behind: " << leak << " dB hot, " << quietLeak << " dB quiet");
        CHECK (quietLeak < leak - 8.0);
    }
    // A and C stay in the middle.
    CHECK (std::abs (db (powerAt (s.left, s.rate, fa, 0.3, 0.8) / powerAt (s.right, s.rate, fa, 0.3, 0.8))) < 0.2);
    CHECK (std::abs (db (powerAt (s.left, s.rate, fc, 0.3, 0.8) / powerAt (s.right, s.rate, fc, 0.3, 0.8))) < 0.2);
    // The sum is the voice: B's power kept (equal-power law, unity at the centre).
    Patch still = p;
    still.satellite[1].enabled = false;
    const auto ref = playCards (still, { on (0, 48, 100) }, 0.8, false);
    const double moved = powerAt (s.left, s.rate, fb, 0.3, 0.8) + powerAt (s.right, s.rate, fb, 0.3, 0.8);
    const double centred = powerAt (ref.left, ref.rate, fb, 0.3, 0.8) + powerAt (ref.right, ref.rate, fb, 0.3, 0.8);
    CHECK (std::abs (db (moved / centred)) < 0.3);
}

TEST_CASE ("SATELLITE: a note on three cards, all three moving - each lands where its own settings put it", "[satellite][unit]")
{
    Patch p = threeCards();
    p.osc1Level = p.osc2Level = 0.3;   // (moderate levels: the shared filter near linear)
    p.organic.levelDb = -10.0;
    p.osc2Coarse = 13.0;
    p.osc3Coarse = 26.0;
    p.satellite[0] = fixedAt (-1.0);
    p.satellite[1] = fixedAt (1.0);
    p.satellite[2] = fixedAt (0.5);   // (C: the voice less A and B - no twin of its own)
    const auto s = playCards (p, { on (0, 48, 100) }, 0.8, false);
    REQUIRE (s.finite());
    const double fa = hz (48), fb = hz (61), fc = hz (74);
    CHECK (db (powerAt (s.right, s.rate, fa, 0.3, 0.8) / powerAt (s.left, s.rate, fa, 0.3, 0.8)) < -30.0);
    CHECK (db (powerAt (s.left, s.rate, fb, 0.3, 0.8) / powerAt (s.right, s.rate, fb, 0.3, 0.8)) < -30.0);
    // C at +0.5: the law's right-over-left for 0.5 (tan (3 pi / 8) squared: +15.3 dB).
    const double cRight = db (powerAt (s.right, s.rate, fc, 0.3, 0.8) / powerAt (s.left, s.rate, fc, 0.3, 0.8));
    CHECK (cRight == Approx (20.0 * std::log10 (std::tan (3.0 * std::numbers::pi / 8.0))).margin (1.0));
}

TEST_CASE ("SATELLITE: DEPTH 0 holds at OFFSET; PAN travels the panorama at constant power, in time", "[satellite][unit]")
{
    Patch p = threeCards();
    p.osc2Level = 0.0;
    p.organic.heard = false;   // (card A alone)
    p.satellite[0] = fixedAt (0.5);
    auto still = playCards (p, { on (0, 57, 100, chA) }, 1.0);
    for (double t = 0.2; t < 0.9; t += 0.1)
        CHECK (placeOf (still, t, t + 0.05) == Approx (0.5).margin (0.02));

    // PAN, a quarter note at 120 BPM (the local clock starts with the note): 0.5 s a cycle.
    p.satellite[0] = {};
    p.satellite[0].enabled = true;
    p.satellite[0].depth = 1.0;
    p.satellite[0].division = 7;   // 1/4
    REQUIRE (std::string (sat::divisions[7].name) == "1/4");
    const auto pan = playCards (p, { on (0, 57, 100, chA) }, 2.1);
    REQUIRE (pan.finite());
    // Right at a quarter of the cycle, left at three quarters (6 ms of smoothing behind).
    for (const double cycle : { 1.0, 2.0, 3.0 })
    {
        const double t = cycle * 0.5;
        CHECK (placeOf (pan, t + 0.125 - 0.01, t + 0.125 + 0.01) > 0.9);
        CHECK (placeOf (pan, t + 0.375 - 0.01, t + 0.375 + 0.01) < -0.9);
    }
    // Constant power: L^2 + R^2 the same wherever it is (within 0.25 dB; whole cycles of the note).
    double lo = 1.0e9, hi = 0.0;
    const double cycles = 4.0 / hz (57);
    for (double t = 0.3; t < 2.0; t += 0.02)
    {
        const double e = pan.rms (t, t + cycles);
        lo = std::min (lo, e);
        hi = std::max (hi, e);
    }
    CHECK (20.0 * std::log10 (hi / lo) < 0.25);
}

TEST_CASE ("SATELLITE: ORBIT is not PAN - the far half of the circle is quieter and darker", "[satellite][unit]")
{
    Patch p = threeCards();
    p.osc2Level = 0.0;
    p.organic.heard = false;
    p.satellite[0].enabled = true;
    p.satellite[0].depth = 1.0;
    p.satellite[0].division = 2;   // 1/1: 2 s a cycle at 120 BPM
    const auto pan = playCards (p, { on (0, 45, 100, chA) }, 2.2);
    p.satellite[0].mode = sat::Mode::orbit;
    const auto orbit = playCards (p, { on (0, 45, 100, chA) }, 2.2);
    REQUIRE (orbit.finite());
    // Behind (half a cycle, 1 s): ORBIT at least 2 dB down; in front (2 s) the same as PAN.
    CHECK (20.0 * std::log10 (orbit.rms (0.95, 1.05) / pan.rms (0.95, 1.05)) < -2.0);
    CHECK (std::abs (20.0 * std::log10 (orbit.rms (2.0, 2.1) / pan.rms (2.0, 2.1))) < 0.3);
    // ... and darker: a high harmonic loses more than the fundamental.
    const double f0 = hz (45);
    const double lowDrop = db ((powerAt (orbit.left, orbit.rate, f0, 0.9, 1.1) + powerAt (orbit.right, orbit.rate, f0, 0.9, 1.1))
                               / (powerAt (pan.left, pan.rate, f0, 0.9, 1.1) + powerAt (pan.right, pan.rate, f0, 0.9, 1.1)));
    const double highDrop = db ((powerAt (orbit.left, orbit.rate, 32 * f0, 0.9, 1.1) + powerAt (orbit.right, orbit.rate, 32 * f0, 0.9, 1.1))
                                / (powerAt (pan.left, pan.rate, 32 * f0, 0.9, 1.1) + powerAt (pan.right, pan.rate, 32 * f0, 0.9, 1.1)));
    CHECK (highDrop < lowDrop - 3.0);
    // Mono: the sum never cancels (no phase tricks): its level within 3 dB of PAN's everywhere.
    const auto mo = orbit.mono(), mp = pan.mono();
    for (double t = 0.2; t < 2.1; t += 0.05)
    {
        Stereo a, b;
        a.left = a.right = mo;
        b.left = b.right = mp;
        CHECK (std::abs (20.0 * std::log10 (a.rms (t, t + 0.05) / b.rms (t, t + 0.05))) < 3.5);
    }
}

TEST_CASE ("SATELLITE: BOUNCE with NOTE - each note of the card takes the other side and keeps it", "[satellite][unit]")
{
    Patch p = threeCards();
    p.osc2Level = 0.0;
    p.organic.heard = false;
    p.satellite[0].enabled = true;
    p.satellite[0].mode = sat::Mode::bounce;
    p.satellite[0].retrigger = sat::Retrigger::note;
    p.satellite[0].depth = 0.8;
    std::vector<Event> notes;
    for (int i = 0; i < 6; ++i)
    {
        notes.push_back (on (i * 7200, 60 + i, 100, chA));
        notes.push_back (off (i * 7200 + 6000, 60 + i, chA));
    }
    const auto s = playCards (p, notes, 0.95);
    for (int i = 0; i < 6; ++i)
    {
        const double t = i * 0.15 + 0.03;
        INFO ("note " << i);
        const double place = placeOf (s, t, t + 0.08);
        CHECK (std::abs (place) > 0.6);
        CHECK ((place > 0.0) == (i % 2 == 0));   // right, left, right ...
        CHECK (placeOf (s, t + 0.02, t + 0.06) == Approx (place).margin (0.05));   // (held: no travel within the note)
    }
}

TEST_CASE ("SATELLITE: switched on and off mid-note - no jump; off again is the original path, bit for bit", "[satellite][unit]")
{
    Patch p = threeCards();
    p.osc2Coarse = 7.0;
    auto runWith = [&] (bool move) {
        auto s = makeSynth (p);
        Stereo out;
        out.rate = 48000.0;
        const auto part = [&] (Patch q, double seconds, std::vector<Event> ev) {
            s->setPatch (q);
            const auto r = render (*s, seconds, std::move (ev));
            out.left.insert (out.left.end(), r.left.begin(), r.left.end());
            out.right.insert (out.right.end(), r.right.begin(), r.right.end());
        };
        Patch moving = p;
        moving.satellite[1] = fixedAt (1.0);
        part (p, 0.3, { on (0, 48, 100) });
        part (move ? moving : p, 0.3, {});
        part (p, 0.4, {});
        return out;
    };
    const auto plain = runWith (false), moved = runWith (true);
    REQUIRE (moved.finite());
    // The move glides in over ~20 ms: the right channel's level against the still render's
    // (a whole cycle of the note per window) rises in small steps, never a jump.
    auto rightRms = [] (const Stereo& x, double from, double to) {
        Stereo r = x;
        r.left = r.right;
        return r.rms (from, to);
    };
    const double cycle = 1.0 / hz (48);
    double last = 1.0;
    for (double t = 0.29; t < 0.36; t += 0.002)
    {
        const double gain = rightRms (moved, t, t + cycle) / rightRms (plain, t, t + cycle);
        CHECK (std::abs (20.0 * std::log10 (gain / last)) < 1.0);
        last = gain;
    }
    CHECK (20.0 * std::log10 (last) > 1.0);   // (and B did arrive on the right)
    // Back off: once the 20 ms glide is over, the original path (the voices bit for bit; what
    // remains is the output's DC blocker and decimators forgetting the moved past, decaying).
    CHECK (maxDifference (plain, moved, 0.65) < 1.0e-4f);
    CHECK (maxDifference (plain, moved, 0.95) < maxDifference (plain, moved, 0.65));
    CHECK (maxDifference (plain, moved) > 1.0e-3f);   // (it did move)
}

TEST_CASE ("SATELLITE: MORPH fuses C into its carrier - C's own motion does not tear the fused note", "[satellite][unit]")
{
    Patch p = threeCards();
    p.morph.enabled = true;
    const auto fused = playCards (p, { on (0, 48, 100) }, 0.6, false);
    p.satellite[2] = fixedAt (1.0);   // C hard right: C's notes alone would move; this one is fused
    const auto still = playCards (p, { on (0, 48, 100) }, 0.6, false);
    CHECK (maxDifference (fused, still) == 0.0f);
    // A C-only note (its own lane) does move.
    const auto alone = playCards (p, { on (0, 74, 100, chC) }, 0.6);
    CHECK (placeOf (alone, 0.2, 0.5) > 0.95);
}

TEST_CASE ("SATELLITE: MAIN + SEND - a moving card's send moves with it (one card or split from three)", "[satellite][unit]")
{
    namespace fx = andor::analog::fx;
    fx::Graph g;
    g.custom = true;
    g.mainSend = true;   // (MAIN and SEND both empty: what returns is the dry and the cards' sends)
    for (const bool routing : { true, false })
    {
        INFO ((routing ? "C's own notes (whole voices)" : "one note on three cards (C split by its twin)"));
        Patch p = threeCards();
        p.osc2Coarse = 13.0;
        p.osc3Coarse = 26.0;
        p.sendA = 0.0;
        p.sendB = 0.0;
        p.sendC = 1.0;
        p.satellite[2] = fixedAt (1.0);
        auto synth = makeSynth (p);
        synth->setCardRouting (routing);
        auto compiled = fx::compile (g, 1);
        REQUIRE (compiled != nullptr);
        synth->setFxGraph (std::move (compiled));
        const auto s = render (*synth, 0.8, { routing ? on (0, 74, 100, chC) : on (0, 48, 100) });
        REQUIRE (s.finite());
        const double fc = hz (74);
        // C, its dry and its send, on the right only (the send unpanned would put it back left).
        CHECK (db (powerAt (s.left, s.rate, fc, 0.3, 0.8) / powerAt (s.right, s.rate, fc, 0.3, 0.8)) < (routing ? -60.0 : -25.0));
    }
}

TEST_CASE ("SATELLITE: SUB is never moved", "[satellite][unit]")
{
    Patch p = threeCards();
    p.sub.enabled = true;
    p.sub.levelDb = -6.0;
    p.osc1Level = p.osc2Level = 0.0;
    p.organic.heard = false;   // (no card sounds: only SUB)
    // (SUB hears the played keys on its own input, before the arpeggiator.)
    auto withSub = [] (const Patch& q) {
        auto synth = makeSynth (q);
        const unsigned char noteOn[3] = { 0x90, 48, 100 };
        synth->handleSubMidi (noteOn, 3);
        synth->handleMidi (noteOn, 3);
        return render (*synth, 0.5, {});
    };
    const auto plain = withSub (p);
    for (auto& s : p.satellite)
        s = fixedAt (1.0);
    const auto moved = withSub (p);
    REQUIRE (plain.rms (0.1, 0.5) > 1.0e-3);
    CHECK (maxDifference (plain, moved) == 0.0f);
}

TEST_CASE ("SATELLITE: block sizes and FREE vs SYNC - the same render whatever the host's blocks", "[satellite][unit]")
{
    Patch p = threeCards();
    p.satellite[0].enabled = true;
    p.satellite[0].depth = 0.9;
    p.satellite[0].division = 9;   // 1/8
    p.satellite[1].enabled = true;
    p.satellite[1].mode = sat::Mode::orbit;
    p.satellite[1].timing = sat::Timing::free;
    p.satellite[1].rateHz = 1.7;
    p.satellite[1].phase = 0.33;
    p.satellite[2].enabled = true;
    p.satellite[2].mode = sat::Mode::bounce;
    const std::vector<Event> notes { on (0, 48, 100, chA), on (1000, 61, 100, chB), on (2000, 74, 100, chC), on (3000, 55, 90) };
    const auto big = playCards (p, notes, 1.2, true, 512);
    for (const int block : { 32, 64, 128, 333 })
    {
        INFO ("block " << block);
        CHECK (maxDifference (big, playCards (p, notes, 1.2, true, block)) < 2.0e-4f);
    }
    // Deterministic: the same render twice.
    CHECK (maxDifference (big, playCards (p, notes, 1.2, true, 512)) == 0.0f);
}

TEST_CASE ("SATELLITE: no NaN or runaway at the extremes", "[satellite][unit]")
{
    Patch p = threeCards();
    p.resonance = 0.95;
    p.filterDrive = 1.0;
    for (auto& s : p.satellite)
    {
        s.enabled = true;
        s.mode = sat::Mode::orbit;
        s.depth = 1.0;
        s.offset = -1.0;
        s.width = 0.0;
        s.timing = sat::Timing::free;
        s.rateHz = 10.0;
    }
    p.satellite[1].offset = 1.0;
    p.satellite[2].mode = sat::Mode::bounce;
    p.satellite[2].timing = sat::Timing::sync;
    p.satellite[2].division = 12;   // 1/32
    std::vector<Event> notes;
    for (int i = 0; i < 16; ++i)
        notes.push_back (on (i * 300, 36 + 5 * i, 127, 1 + (i % 7) + 1));
    const auto s = playCards (p, notes, 1.0);
    REQUIRE (s.finite());
    CHECK (s.peak() < 4.0f);
}

TEST_CASE ("SATELLITE: demo renders (ANALOG_SATELLITE_DIR)", "[.][satellite-renders]")
{
    const char* dir = std::getenv ("ANALOG_SATELLITE_DIR");
    REQUIRE (dir != nullptr);
    const std::string out (dir);
    Patch p = threeCards();
    p.osc2Coarse = 0.0;
    p.ampRelease = 0.3;
    // Dry renders, 2 bars at 120 BPM: A a held low chord, B a slow arpeggio, C a fast high one.
    std::vector<Event> notes;
    for (const int n : { 45, 52, 57, 60 })
        notes.push_back (on (0, n, 90, chA));
    for (int i = 0; i < 16; ++i)
    {
        const int b = 64 + (i % 4) * 3;
        notes.push_back (on (i * 12000, b, 80, chB));
        notes.push_back (off (i * 12000 + 9000, b, chB));
    }
    for (int i = 0; i < 64; ++i)
    {
        const int c = 76 + (i % 6) * 2 + 12 * ((i / 6) % 3);
        notes.push_back (on (i * 3000, c, 70, chC));
        notes.push_back (off (i * 3000 + 2000, c, chC));
    }
    auto save = [&] (const Patch& q, const std::string& name) {
        const auto s = playCards (q, notes, 8.5);
        REQUIRE (s.finite());
        REQUIRE (writeWav (out + "/" + name, s));
    };
    save (p, "satellite_off_reference.wav");
    Patch pan = p;
    pan.satellite[2].enabled = true;
    pan.satellite[2].depth = 0.9;
    pan.satellite[2].division = 4;   // 1/2
    save (pan, "satellite_pan_C.wav");
    Patch orbit = pan;
    orbit.satellite[2].mode = sat::Mode::orbit;
    orbit.satellite[2].division = 2;
    save (orbit, "satellite_orbit_C.wav");
    Patch bounce = pan;
    bounce.satellite[2].mode = sat::Mode::bounce;
    bounce.satellite[2].retrigger = sat::Retrigger::note;
    save (bounce, "satellite_bounce_note_C.wav");
    Patch all = orbit;
    all.satellite[0] = fixedAt (-0.4);
    all.satellite[0].width = 1.0;
    all.satellite[1].enabled = true;
    all.satellite[1].mode = sat::Mode::pan;
    all.satellite[1].depth = 0.35;
    all.satellite[1].division = 0;   // 4 bars: a slow drift
    all.satellite[1].phase = 0.5;
    save (all, "satellite_a_chord_b_center_c_orbit_120bpm.wav");
}

TEST_CASE ("SATELLITE: CPU (16 held voices; off, whole voices, three twins per voice)", "[.][satellite-cpu]")
{
    auto measure = [] (const Patch& p, bool routing, int oversampling) {
        auto s = makeSynth (p, 48000.0, oversampling, 512);
        s->setCardRouting (routing);
        for (int i = 0; i < 16; ++i)
        {
            const int channel = routing ? 1 + (1 << (i % 3)) : 1;
            const std::uint8_t noteOn[3] { static_cast<std::uint8_t> (0x90 | (channel - 1)), static_cast<std::uint8_t> (36 + 3 * i), 100 };
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
        return 100.0 * seconds / (blocks * 512.0 / 48000.0);   // % of one core
    };
    Patch p = threeCards();
    Patch moving = p;
    for (auto& s : moving.satellite)
    {
        s.enabled = true;
        s.mode = sat::Mode::orbit;
        s.depth = 0.8;
    }
    for (const int os : { 1, 4 })
        for (int run = 0; run < 3; ++run)
        {
            const double off = measure (p, false, os), whole = measure (moving, true, os), twins = measure (moving, false, os);
            WARN ("oversampling " << os << ": off " << off << " %, whole voices " << whole << " %, three twins " << twins << " %");
        }
}
