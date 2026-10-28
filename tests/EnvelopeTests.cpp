// The envelopes: times as written, retrigger without a click, sustain following the knob,
// release ending, and the filter envelope independent of the amplitude one.

#include "TestUtil.h"

#include "Envelope.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace testutil;
using Catch::Approx;

namespace
{
    /** Samples until the envelope first reaches `level` (rising) or falls below it. */
    int samplesUntil (Envelope& e, float level, bool rising, int limit)
    {
        for (int i = 0; i < limit; ++i)
        {
            const float v = e.next();
            if (rising ? v >= level : v <= level)
                return i + 1;
        }
        return -1;
    }
}

TEST_CASE ("envelope: attack reaches the top in the attack time; decay and release take theirs", "[envelope][unit]")
{
    for (const double attack : { 0.002, 0.05, 1.0, 10.0 })
    {
        INFO ("attack " << attack);
        Envelope e;
        e.setSampleRate (48000.0);
        e.setTimes (attack, 0.5, 0.5, 0.5);
        e.noteOn();
        const int n = samplesUntil (e, 1.0f, true, 48000 * 20);
        CHECK (n / 48000.0 == Approx (attack).epsilon (0.02).margin (0.0002));
    }
    Envelope e;
    e.setSampleRate (48000.0);
    e.setTimes (0.001, 0.4, 0.0, 0.8);
    e.noteOn();
    samplesUntil (e, 1.0f, true, 48000);
    // DECAY: 99 % of the way to the sustain level in the decay time.
    CHECK (samplesUntil (e, 0.01f, false, 48000 * 5) / 48000.0 == Approx (0.4).epsilon (0.02));
    e.setTimes (0.001, 0.4, 1.0, 0.8);
    e.noteOn();
    samplesUntil (e, 1.0f, true, 48000);
    for (int i = 0; i < 4800; ++i)
        e.next();
    e.noteOff();
    CHECK (samplesUntil (e, 0.01f, false, 48000 * 5) / 48000.0 == Approx (0.8).epsilon (0.02));
    // RELEASE ends: the envelope goes idle (below -100 dB) and stays at 0.
    CHECK (samplesUntil (e, 0.0f, false, 48000 * 10) > 0);
    CHECK_FALSE (e.isActive());
}

TEST_CASE ("envelope: a new note starts from where the envelope is (no jump); very short times are stable", "[envelope][unit]")
{
    Envelope e;
    e.setSampleRate (192000.0);
    e.setTimes (0.0, 0.0, 0.3, 0.0);   // clamped to the shortest times
    e.noteOn();
    float last = 0.0f, largestStep = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        const float v = e.next();
        CHECK (std::isfinite (v));
        if (i == 1000)
            e.noteOff();
        if (i == 1500)
            e.noteOn();
        largestStep = std::max (largestStep, std::abs (v - last));
        last = v;
    }
    CHECK (largestStep < 0.02f);   // 0.5 ms attack at 192 kHz: about 100 samples

    // Retrigger mid-release: the next value continues from the current one.
    Envelope r;
    r.setSampleRate (48000.0);
    r.setTimes (0.1, 0.2, 0.8, 1.0);
    r.noteOn();
    for (int i = 0; i < 24000; ++i)
        r.next();
    r.noteOff();
    for (int i = 0; i < 4800; ++i)
        r.next();
    const float before = r.value();
    r.noteOn();
    CHECK (std::abs (r.next() - before) < 0.01f);
}

TEST_CASE ("envelope: SUSTAIN follows a knob turned while the note holds", "[envelope][unit]")
{
    Envelope e;
    e.setSampleRate (48000.0);
    e.setTimes (0.001, 0.05, 0.8, 0.2);
    e.noteOn();
    for (int i = 0; i < 48000; ++i)
        e.next();
    CHECK (e.value() == Approx (0.8f).margin (0.001));
    e.setTimes (0.001, 0.05, 0.3, 0.2);
    for (int i = 0; i < 48000; ++i)
        e.next();
    CHECK (e.value() == Approx (0.3f).margin (0.001));
}

TEST_CASE ("envelope: the filter envelope runs independently of the amplitude envelope", "[envelope][unit]")
{
    // A slow filter envelope with a fast gate: the brightness keeps rising after the
    // amplitude has reached its top.
    auto p = measurementPatch();
    p.tone = math::hzToTone (200.0);
    p.envDepthSemitones = 48.0;
    p.filterAttack = 1.0;
    p.filterDecay = 1.0;
    p.filterSustain = 1.0;
    p.ampAttack = 0.001;
    auto synth = makeSynth (p);
    const auto out = render (*synth, 1.2, { on (0, 48) });
    CHECK (out.rms (0.9, 1.1) > 1.4 * out.rms (0.05, 0.1));

    // And the other way: a slow amplitude with a fast filter envelope.
    p.filterAttack = 0.001;
    p.filterDecay = 0.05;
    p.filterSustain = 0.0;
    p.ampAttack = 1.0;
    auto other = makeSynth (p);
    const auto b = render (*other, 1.2, { on (0, 48) });
    CHECK (b.rms (0.9, 1.1) > 3.0 * b.rms (0.05, 0.1));
}
