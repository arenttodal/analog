// AMP 2 (state version 24, ANDOR ANALOG): the held-note attack - a note begun over held keys
// (or the pedal) takes AMP 2's ATTACK; a chord's keys (30 ms) and a phrase's first take AMP 1.

#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace testutil;

namespace
{
    Patch amp2Patch (double attack1, double attack2)
    {
        Patch p = measurementPatch();
        p.ampAttack = attack1;
        p.amp2Enabled = true;
        p.amp2Attack = attack2;
        return p;
    }

    /** The second note alone: the render with both less the render with the first only. */
    Stereo secondAlone (const Patch& p, double seconds, const std::vector<Event>& both, const std::vector<Event>& first)
    {
        auto a = render (*makeSynth (p), seconds, both);
        const auto b = render (*makeSynth (p), seconds, first);
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            a.left[i] -= b.left[i];
            a.right[i] -= b.right[i];
        }
        return a;
    }

    /** How long after `from` (s) the level first reaches half its final value. */
    double riseTime (const Stereo& s, double from, double to)
    {
        const double full = s.rms (to - 0.1, to);
        for (double t = from; t < to - 0.1; t += 0.005)
            if (s.rms (t, t + 0.01) > 0.5 * full)
                return t - from;
        return to - from;
    }
}

TEST_CASE ("AMP 2: off - exactly as before, whatever its attack", "[amp2][unit]")
{
    Patch p = measurementPatch();
    p.ampAttack = 0.01;
    Patch q = p;
    q.amp2Attack = 3.0;   // (off: never heard)
    const std::vector<Event> notes { on (0, 48, 100), on (24000, 55, 100), off (60000, 48), off (60000, 55) };
    const auto a = render (*makeSynth (p), 1.5, notes);
    const auto b = render (*makeSynth (q), 1.5, notes);
    float diff = 0.0f;
    for (std::size_t i = 0; i < a.left.size(); ++i)
        diff = std::max ({ diff, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
    CHECK (diff == 0.0f);
}

TEST_CASE ("AMP 2: a note over a held one swells in with AMP 2's attack; the first keeps AMP 1's", "[amp2][unit]")
{
    const Patch p = amp2Patch (0.005, 1.0);
    // The first note at once (AMP 1: 5 ms); the second at 0.5 s while the first is held.
    const auto first = render (*makeSynth (p), 2.5, { on (0, 48, 100) });
    CHECK (riseTime (first, 0.0, 0.6) < 0.05);
    const auto second = secondAlone (p, 2.5, { on (0, 48, 100), on (24000, 55, 100) }, { on (0, 48, 100) });
    const double rise = riseTime (second, 0.5, 2.5);
    INFO ("second note's rise " << rise << " s");
    CHECK (rise > 0.2);   // (AMP 2's second-long attack, not 5 ms)
    // The same two notes played apart (the first let go): AMP 1 for both.
    const auto apart = secondAlone (p, 2.5, { on (0, 48, 100), off (12000, 48), on (24000, 55, 100) }, { on (0, 48, 100), off (12000, 48) });
    CHECK (riseTime (apart, 0.5, 2.5) < 0.05);
}

TEST_CASE ("AMP 2: a chord's keys within 30 ms are one gesture (AMP 1); later keys take AMP 2", "[amp2][unit]")
{
    const Patch p = amp2Patch (0.005, 1.0);
    // 10 ms apart: one chord.
    const auto chord = secondAlone (p, 1.5, { on (0, 48, 100), on (480, 55, 100) }, { on (0, 48, 100) });
    CHECK (riseTime (chord, 0.01, 1.5) < 0.05);
    // 80 ms apart: the second over the held first.
    const auto late = secondAlone (p, 2.0, { on (0, 48, 100), on (3840, 55, 100) }, { on (0, 48, 100) });
    CHECK (riseTime (late, 0.08, 2.0) > 0.2);
}

TEST_CASE ("AMP 2: the sustain pedal holding a note counts as held", "[amp2][unit]")
{
    const Patch p = amp2Patch (0.005, 1.0);
    // The first key pressed and let go under the pedal (it sounds on), then a second key.
    const std::vector<Event> firstOnly { cc (0, 64, 127), on (100, 48, 100), off (12000, 48) };
    auto both = firstOnly;
    both.push_back (on (24000, 55, 100));
    const auto second = secondAlone (p, 2.5, both, firstOnly);
    CHECK (riseTime (second, 0.5, 2.5) > 0.2);
}

TEST_CASE ("AMP 2: with an arpeggiator the keys played decide, never its steps", "[amp2][unit]")
{
    // The synth hears steps (noteOn) while the musician's keys arrive as played keys.
    const Patch p = amp2Patch (0.005, 1.0);
    auto run = [&p] (bool addKey) {
        auto s = makeSynth (p);
        s->setPlayedKeysOnly (true);
        std::vector<float> l (96000), r (96000);
        auto key = [&s] (int note, bool down) {
            const std::uint8_t m[3] { static_cast<std::uint8_t> (down ? 0x90 : 0x80), static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (down ? 100 : 0) };
            s->handleSubMidi (m, 3);
        };
        auto step = [&s] (int note, bool down) {
            const std::uint8_t m[3] { static_cast<std::uint8_t> (down ? 0x90 : 0x80), static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (down ? 100 : 0) };
            s->handleMidi (m, 3);
        };
        int at = 0;
        auto to = [&] (int sample) { s->render (l.data() + at, r.data() + at, sample - at); at = sample; };
        key (48, true);
        step (48, true);
        to (12000);
        step (48, false);
        step (60, true);   // (a step an octave up: still the first key's - AMP 1)
        to (24000);
        step (60, false);
        if (addKey)
            key (55, true);   // (a key added over the held one, 0.5 s in)
        step (55, true);
        to (96000);
        Stereo out;
        out.left = l;
        out.right = r;
        return std::pair { std::move (out), s->heldAttackCount() };
    };
    const auto [added, countAdded] = run (true);
    const auto [stepOnly, countStep] = run (false);
    CHECK (countAdded == 1);   // (only the step of the key begun over a held one)
    CHECK (countStep == 0);    // (steps alone never)
    REQUIRE (added.finite());
    CHECK (riseTime (added, 0.55, 2.0) > 0.2);   // (after the octave step's 20 ms release)
    CHECK (riseTime (stepOnly, 0.55, 2.0) < 0.05);
}
