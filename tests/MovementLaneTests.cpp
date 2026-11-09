// MOVEMENT's two lanes (state version 11, the effects rack): lane A is MOVEMENT itself, lane B
// a second, independent MOVEMENT in series after it, off by default. Lane B off (and lane A on
// at 100 %) plays exactly as before lanes. Both lanes are real processing, not a view; their
// combination is checked for level across every pair of modes; switching a lane on or off
// never clicks; two DRIFTs add in power; renders are deterministic; the cost is reported.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

using namespace testutil;

namespace
{
    const char* const modeNames[] = { "DRIFT", "TAPE", "CHORUS", "PULSE", "SHAPER" };

    /** A held chord (saws), MOVEMENT at `motion`, lane A in `a`, lane B in `b` (-1: off). */
    Patch lanes (int a, int b, double motion = 1.0)
    {
        auto p = measurementPatch();
        p.osc2Level = 0.5;
        p.motion = motion;
        p.effects.movementMode = static_cast<osp::MovementMode> (std::max (0, a));
        p.movementA.on = a >= 0;
        p.movementBLane.on = b >= 0;
        p.movementB.movementMode = static_cast<osp::MovementMode> (std::max (0, b));
        p.movementB.pulseRate = 0.7;   // (lane B moves at its own pace: not lane A's)
        p.movementB.chorusRate = 0.6;
        p.movementB.shaper.pattern = 5;
        return p;
    }

    Stereo play (const Patch& p, double seconds = 2.0, int block = 256)
    {
        auto s = makeSynth (p);
        return render (*s, seconds, { on (0, 45, 100), on (0, 52, 100), on (0, 57, 100) }, block);
    }

    double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }
}

TEST_CASE ("movement lanes: lane B off and lane A on at 100 % play exactly as one MOVEMENT", "[movement][lanes]")
{
    for (int mode = 0; mode < 5; ++mode)
    {
        auto p = lanes (mode, -1, 0.7);
        auto one = p;
        one.movementB = osp::Shaping();   // (lane B's settings make no difference while it is off)
        const auto a = play (p, 1.0), b = play (one, 1.0);
        INFO (modeNames[mode]);
        CHECK (a.left == b.left);
        CHECK (a.right == b.right);
    }
}

TEST_CASE ("movement lanes: lane B is processing of its own, after lane A", "[movement][lanes]")
{
    const auto dry = play (lanes (-1, -1));
    const auto onlyB = play (lanes (-1, 3));   // PULSE on lane B alone
    const auto onlyA = play (lanes (3, -1));
    CHECK (onlyB.left != dry.left);
    CHECK (onlyB.left != onlyA.left);   // its own rate
    // Lane B's INTENSITY scales it: at 0 % it is the dry sound again.
    auto silentB = lanes (-1, 3);
    silentB.movementBLane.level = 0.0;
    const auto zero = play (silentB);
    CHECK (std::abs (db (zero.rms (0.5, 2.0)) - db (dry.rms (0.5, 2.0))) < 0.05);
    // The MOVEMENT macro scales both lanes.
    auto noMacro = lanes (3, 3, 0.0);
    CHECK (std::abs (db (play (noMacro).rms (0.5, 2.0)) - db (dry.rms (0.5, 2.0))) < 0.05);
}

TEST_CASE ("movement lanes: every pair of modes combines at a sane level, never louder than its loudest lane", "[movement][lanes][level]")
{
    const double dryRms = play (lanes (-1, -1)).rms (0.5, 2.0);
    for (int a = 0; a < 5; ++a)
        for (int b = 0; b < 5; ++b)
        {
            const auto both = play (lanes (a, b));
            const double rA = play (lanes (a, -1)).rms (0.5, 2.0), rB = play (lanes (-1, b)).rms (0.5, 2.0);
            const double r = both.rms (0.5, 2.0);
            INFO (modeNames[a] << " + " << modeNames[b] << ": A " << db (rA / dryRms) << " dB, B " << db (rB / dryRms) << " dB, both " << db (r / dryRms) << " dB, peak "
                               << both.peak());
            for (float x : both.left)
                REQUIRE (std::isfinite (x));
            // Never louder than the louder lane by more than 1 dB (two choruses or tapes
            // in a row do not build up: lane B's guard).
            CHECK (db (r) <= db (std::max (rA, rB)) + 1.0);
            // Movement may dip (PULSE and SHAPER after each other multiply), but the sound
            // stays: never more than 9 dB under the quieter lane alone.
            CHECK (db (r) >= db (std::min (rA, rB)) - 9.0);
            CHECK (both.peak() < 1.5f);
        }
}

TEST_CASE ("movement lanes: switching lane B on and off while a chord plays never clicks", "[movement][lanes][smooth]")
{
    for (const int mode : { 1, 2, 3, 4 })
    {
        auto p = lanes (2, mode);
        auto s = makeSynth (p);
        const std::uint8_t notes[3][3] { { 0x90, 45, 100 }, { 0x90, 52, 100 }, { 0x90, 57, 100 } };
        for (const auto& n : notes)
            s->handleMidi (n, 3);
        std::vector<float> l (256), r (256);
        // The largest sample step within 50 ms of each switch, against the largest step of the
        // steady sound on either side (B off before, B on between the switches).
        float previous = 0.0f, worstStep = 0.0f, typicalStep = 0.0f;
        for (int block = 0; block < 300; ++block)
        {
            if (block == 100 || block == 200)
            {
                p.movementBLane.on = ! p.movementBLane.on;
                s->setPatch (p);
            }
            s->render (l.data(), r.data(), 256);
            const bool nearSwitch = (block >= 100 && block < 110) || (block >= 200 && block < 210);
            const bool steady = (block >= 40 && block < 100) || (block >= 130 && block < 200);
            for (float x : l)
            {
                const float step = std::abs (x - previous);
                if (nearSwitch)
                    worstStep = std::max (worstStep, step);
                else if (steady)
                    typicalStep = std::max (typicalStep, step);
                previous = x;
            }
        }
        INFO (modeNames[mode] << ": largest step around the switches " << worstStep << ", elsewhere " << typicalStep);
        CHECK (worstStep < 1.25f * typicalStep);
    }
}

TEST_CASE ("movement lanes: two DRIFTs wander independently and add in power, not amplitude", "[movement][lanes][drift]")
{
    auto measure = [] (const Patch& p) {
        auto s = makeSynth (p);
        const std::uint8_t note[3] { 0x90, 57, 100 };
        s->handleMidi (note, 3);
        std::vector<float> l (256), r (256);
        double sumSquares = 0.0, largest = 0.0;
        int n = 0;
        for (int block = 0; block < 48000 * 20 / 256; ++block)
        {
            s->render (l.data(), r.data(), 256);
            const double c = s->voiceView (0).driftCents;
            sumSquares += c * c;
            largest = std::max (largest, std::abs (c));
            ++n;
        }
        return std::pair { std::sqrt (sumSquares / n), largest };
    };
    auto a = lanes (0, -1);
    a.effects.driftPitch = 0.6;
    auto b = lanes (-1, 0);
    b.movementB.driftPitch = 0.6;
    auto both = lanes (0, 0);
    both.effects.driftPitch = 0.6;
    both.movementB.driftPitch = 0.6;
    const auto [rmsA, maxA] = measure (a);
    const auto [rmsB, maxB] = measure (b);
    const auto [rmsBoth, maxBoth] = measure (both);
    INFO ("A " << rmsA << " c (max " << maxA << "), B " << rmsB << " c (max " << maxB << "), both " << rmsBoth << " c (max " << maxBoth << ")");
    CHECK (rmsA > 0.5);
    CHECK (rmsB > 0.5);
    // Its own wander: lane B is not lane A again.
    CHECK (std::abs (rmsA - rmsB) / rmsA < 0.6);
    // Together: wider than one, but never the two depths added (sqrt 2 of one at most, with room).
    CHECK (maxBoth <= 1.5 * std::max (maxA, maxB));
    CHECK (rmsBoth < 1.42 * std::max (rmsA, rmsB));
}

TEST_CASE ("movement lanes: deterministic, whatever the block size", "[movement][lanes]")
{
    const auto p = lanes (4, 2);
    CHECK (play (p, 1.0, 256).left == play (p, 1.0, 256).left);
    CHECK (play (p, 1.0, 256).left == play (p, 1.0, 61).left);
}

TEST_CASE ("movement lanes: SYNC and PHASE move PULSE and SHAPER on the tempo grid", "[movement][lanes][sync]")
{
    auto base = lanes (3, -1);
    base.effects.pulseSync = true;
    base.effects.pulseDivision = 4;   // 1/4 at 120 BPM: 2 Hz
    auto shifted = base;
    shifted.effects.movementPhase = 0.5;
    const auto x = play (base, 1.5), y = play (shifted, 1.5);
    CHECK (x.left != y.left);
    // Half a cycle later: the same level a quarter of a second later (2 Hz).
    CHECK (std::abs (db (x.rms (0.5, 0.75)) - db (y.rms (0.75, 1.0))) < 0.6);
    auto shaper = lanes (4, -1);
    auto shaperShifted = shaper;
    shaperShifted.effects.movementPhase = 0.25;
    CHECK (play (shaper, 1.0).left != play (shaperShifted, 1.0).left);
}

TEST_CASE ("movement lanes: CPU of one lane and of two (reported, not asserted)", "[movement][lanes][cpu]")
{
    for (const auto& [a, b] : { std::pair { 2, -1 }, std::pair { 2, 1 }, std::pair { 1, -1 }, std::pair { 4, 3 }, std::pair { 0, 0 } })
    {
        auto p = lanes (a, b);
        p.polyphony = 16;
        auto s = makeSynth (p);
        for (int v = 0; v < 16; ++v)
        {
            const std::uint8_t note[3] { 0x90, static_cast<std::uint8_t> (40 + 2 * v), 100 };
            s->handleMidi (note, 3);
        }
        std::vector<float> l (512), r (512);
        const auto t0 = std::chrono::steady_clock::now();
        for (int block = 0; block < 48000 * 2 / 512; ++block)
            s->render (l.data(), r.data(), 512);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        WARN ("MOVEMENT A " << (a >= 0 ? modeNames[a] : "off") << " + B " << (b >= 0 ? modeNames[b] : "off") << ", 16 voices: " << 100.0 * seconds / 2.0 << " % of one core");
    }
}
