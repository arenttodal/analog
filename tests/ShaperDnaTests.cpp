// SHAPER DNA V1, gate 1: the pure model (docs/SHAPER_DNA_V1.md) - assignment, profiles, phase.

#include "ShaperDna.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace andor::analog;
using Catch::Approx;

namespace
{
    std::string cycleString (ShaperDnaSettings s, int notes)
    {
        ShaperDnaAssigner a;
        std::string out;
        for (int i = 0; i < notes; ++i)
            out += static_cast<char> ('A' + a.assign (s, 60));
        return out;
    }
}

TEST_CASE ("DNA-01: CYCLE gives A / B / C in order, 1, 2 or 3 notes per profile, A / B only or A / B / C", "[shaperdna][unit]")
{
    ShaperDnaSettings s;
    s.enabled = true;
    CHECK (cycleString (s, 7) == "ABCABCA");
    s.notesPerProfile = 2;
    CHECK (cycleString (s, 8) == "AABBCCAA");
    s.notesPerProfile = 3;
    CHECK (cycleString (s, 10) == "AAABBBCCCA");
    s.profiles = 2;
    s.notesPerProfile = 1;
    CHECK (cycleString (s, 5) == "ABABA");
    s.notesPerProfile = 2;
    CHECK (cycleString (s, 6) == "AABBAA");
    // A chord's notes in the order they arrive: each its own logical note-on.
    s.profiles = 3;
    s.notesPerProfile = 1;
    ShaperDnaAssigner a;
    CHECK (a.assign (s, 48) == 0);
    CHECK (a.assign (s, 52) == 1);
    CHECK (a.assign (s, 55) == 2);
    CHECK (a.notesAssigned() == 3);
    a.reset();
    CHECK (a.assign (s, 60) == 0);   // (reset: from A again)
    // Out-of-range settings are clamped, never undefined.
    s.notesPerProfile = 9;
    s.profiles = 7;
    CHECK (cycleString (s, 4) == "AAAB");
}

TEST_CASE ("DNA-03: KEY ZONES - inclusive splits, keys 0 and 127, two or three zones, splits kept ordered", "[shaperdna][unit]")
{
    ShaperDnaSettings s;
    s.assignment = ShaperDnaSettings::Assignment::keyZones;
    s.splitAB = 59;
    s.splitBC = 71;
    CHECK (shaperDna::zoneOf (s, 0) == 0);
    CHECK (shaperDna::zoneOf (s, 59) == 0);
    CHECK (shaperDna::zoneOf (s, 60) == 1);
    CHECK (shaperDna::zoneOf (s, 71) == 1);
    CHECK (shaperDna::zoneOf (s, 72) == 2);
    CHECK (shaperDna::zoneOf (s, 127) == 2);
    s.profiles = 2;   // one split: B above it, to the top
    CHECK (shaperDna::zoneOf (s, 72) == 1);
    CHECK (shaperDna::zoneOf (s, 127) == 1);
    // The splits are ordered and inside the keyboard whatever is asked.
    s.profiles = 3;
    s.splitAB = 127;
    s.splitBC = 3;
    const auto clean = s.sanitised();
    CHECK (clean.splitAB == 126);
    CHECK (clean.splitBC == 127);
    CHECK (shaperDna::zoneOf (s, 126) == 0);
    CHECK (shaperDna::zoneOf (s, 127) == 1);
    // The assigner reads the key in KEY ZONES (no counter).
    ShaperDnaAssigner a;
    s.splitAB = 59;
    s.splitBC = 71;
    CHECK (a.assign (s, 84) == 2);
    CHECK (a.assign (s, 36) == 0);
    CHECK (a.notesAssigned() == 0);
}

TEST_CASE ("SHAPER DNA: FOLLOW resolves to the SHAPER's own pattern (CUSTOM too) and rate; a library choice is its own", "[shaperdna][unit]")
{
    osp::ShaperParams shaper;
    shaper.pattern = 5;
    shaper.rate = osp::ShaperRate::sixteenth;
    shaper.smooth = 0.3;
    ShaperDnaSettings s;
    s.pattern = { ShaperDnaSettings::follow, 2, ShaperDnaSettings::follow };
    s.rate = { ShaperDnaSettings::follow, static_cast<int> (osp::ShaperRate::eighth), static_cast<int> (osp::ShaperRate::eighthTriplet) };
    const auto a = shaperDna::resolve (s, 0, shaper);
    CHECK (a.steps == osp::RhythmicShaper::patternSteps (5));
    CHECK (a.rate == osp::ShaperRate::sixteenth);
    const auto b = shaperDna::resolve (s, 1, shaper);
    CHECK (b.steps == osp::RhythmicShaper::patternSteps (2));
    CHECK (b.rate == osp::ShaperRate::eighth);
    CHECK (shaperDna::resolve (s, 2, shaper).rate == osp::ShaperRate::eighthTriplet);
    // CUSTOM: FOLLOW plays the musician's own steps.
    shaper.custom = true;
    shaper.customSteps = osp::RhythmicShaper::patternSteps (0);
    shaper.customSteps[3] = { 0.0f, 0.0f, osp::StepShape::hold };
    CHECK (shaperDna::resolve (s, 0, shaper).steps == shaper.customSteps);
    CHECK (shaperDna::resolve (s, 1, shaper).steps == osp::RhythmicShaper::patternSteps (2));   // (its own)
    // A FOLLOW profile plays exactly what the SHAPER plays (the same curve on the same clock).
    shaper.custom = false;
    const auto f = shaperDna::resolve (s, 0, shaper);
    for (double q = 0.0; q < 8.0; q += 0.137)
        CHECK (shaperDna::shapeAt (f, q, shaper.smooth, 0.0) == Approx (osp::RhythmicShaper::evaluate (shaper, shaperDna::cyclePhase (q, shaper.rate, 0.0))).margin (1.0e-6));
}

TEST_CASE ("DNA-04: one clock - the rates stay tempo locked (triplets included), PHASE moves them all alike", "[shaperdna][unit]")
{
    using osp::ShaperRate;
    // A 16-step cycle lasts 16 steps of its rate: 1/16 = 4 quarters, 1/8 = 8, 1/8T = 16/3.
    CHECK (shaperDna::cyclePhase (4.0, ShaperRate::sixteenth, 0.0) == Approx (0.0));
    CHECK (shaperDna::cyclePhase (2.0, ShaperRate::sixteenth, 0.0) == Approx (0.5));
    CHECK (shaperDna::cyclePhase (4.0, ShaperRate::eighth, 0.0) == Approx (0.5));
    CHECK (shaperDna::cyclePhase (16.0 / 3.0, ShaperRate::eighthTriplet, 0.0) == Approx (0.0).margin (1.0e-12));
    // Every rate meets the bar line every 16 quarter notes (4 bars of 4/4), whatever the triplets.
    for (const auto r : { ShaperRate::quarter, ShaperRate::eighth, ShaperRate::eighthTriplet, ShaperRate::sixteenth, ShaperRate::sixteenthTriplet, ShaperRate::thirtySecond })
    {
        const double p = shaperDna::cyclePhase (16.0 * 3.0, r, 0.0);
        CHECK ((p < 1.0e-9 || p > 1.0 - 1.0e-9));
    }
    // PHASE: the same offset on every profile.
    CHECK (shaperDna::cyclePhase (0.0, ShaperRate::sixteenth, 0.25) == Approx (0.25));
    CHECK (shaperDna::cyclePhase (0.0, ShaperRate::eighth, 0.25) == Approx (0.25));
    // Two notes on one profile read the same value at the same clock (no per-note state).
    ShaperDnaSettings s;
    osp::ShaperParams shaper;
    const auto p = shaperDna::resolve (s, 1, shaper);
    CHECK (shaperDna::shapeAt (p, 3.3, 0.3, 0.0) == shaperDna::shapeAt (p, 3.3, 0.3, 0.0));
}
