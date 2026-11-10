// Cinematic V6 gate 1: each oscillator card's own REIMAGINED (state version 14). Card A's
// (OSC 1, the instrument's `reimagined`) and card B's (OSC 2, ANDOR OSP's layerC.reimagined*)
// each transform only their own card: a card heard alone sounds the same whatever the other
// card's REIMAGINED is set to, in every live mode. Card C is never transformed.

#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace testutil;

namespace
{
    const std::vector<Event> chord { on (0, 57, 100), on (0, 64, 90), on (0, 69, 80), on (6000, 72, 100), off (14000, 57), off (14000, 64),
                                     off (20000, 69), off (24000, 72) };

    Patch twoCards()
    {
        Patch p = measurementPatch();
        p.osc1Level = 0.8;
        p.osc2Level = 0.7;
        p.osc2Coarse = 7.0;
        p.osc1Enabled = p.osc2Enabled = true;
        p.blend = 0.5;   // (both oscillators at full: BLEND's equal-power middle)
        return p;
    }

    float maxDifference (const Stereo& a, const Stereo& b)
    {
        REQUIRE (a.left.size() == b.left.size());
        float d = 0.0f;
        for (std::size_t i = 0; i < a.left.size(); ++i)
            d = std::max ({ d, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
        return d;
    }

    float peak (const Stereo& s)
    {
        float m = 0.0f;
        for (std::size_t i = 0; i < s.left.size(); ++i)
            m = std::max ({ m, std::abs (s.left[i]), std::abs (s.right[i]) });
        return m;
    }

    Stereo play (const Patch& p) { return render (*makeSynth (p), 0.75, chord); }

    const osp::ReimaginedMode liveModes[] { osp::ReimaginedMode::kaleidoscope, osp::ReimaginedMode::tapeFrame, osp::ReimaginedMode::toybox,
                                            osp::ReimaginedMode::mirage };
}

TEST_CASE ("reimagined per card: card B heard alone is untouched by card A's REIMAGINED, in every live mode (RI-01)", "[reimagined][v6]")
{
    for (const auto mode : liveModes)
    {
        INFO (osp::reimagined::modeName (mode));
        Patch p = twoCards();
        p.osc1Heard = false;   // (card B soloed: A muted)
        p.reimaginedSettings.mode = mode;
        p.reimagined2 = 0.0;
        const auto zero = play (p);
        p.reimagined = 1.0;
        const auto full = play (p);
        CHECK (peak (zero) > 1.0e-3f);
        CHECK (maxDifference (zero, full) < 1.0e-6f);
    }
}

TEST_CASE ("reimagined per card: card A heard alone is untouched by card B's REIMAGINED, in every live mode (RI-02)", "[reimagined][v6]")
{
    for (const auto mode : liveModes)
    {
        INFO (osp::reimagined::modeName (mode));
        Patch p = twoCards();
        p.osc2Heard = false;   // (card A soloed: B muted)
        p.reimaginedSettings2.mode = mode;
        p.reimagined = 0.0;
        const auto zero = play (p);
        p.reimagined2 = 1.0;
        const auto full = play (p);
        CHECK (peak (zero) > 1.0e-3f);
        CHECK (maxDifference (zero, full) < 1.0e-6f);
    }
}

TEST_CASE ("reimagined per card: each card's own REIMAGINED changes its own card's sound", "[reimagined][v6]")
{
    for (const auto mode : liveModes)
    {
        INFO (osp::reimagined::modeName (mode));
        for (const int card : { 0, 1 })
        {
            INFO ("card " << (card == 0 ? "A" : "B"));
            Patch p = twoCards();
            (card == 0 ? p.osc2Heard : p.osc1Heard) = false;   // (the card alone)
            (card == 0 ? p.reimaginedSettings : p.reimaginedSettings2).mode = mode;
            const auto dry = play (p);
            (card == 0 ? p.reimagined : p.reimagined2) = 0.8;
            const auto wet = play (p);
            CHECK (maxDifference (dry, wet) > 1.0e-3f);
        }
    }
}

TEST_CASE ("reimagined per card: both cards at 0 % render exactly as before the cards had their own", "[reimagined][v6]")
{
    // (Zero on both, whatever the modes and their settings: bit for bit the plain voice.)
    Patch p = twoCards();
    const auto plain = play (p);
    p.reimaginedSettings.mode = osp::ReimaginedMode::tapeFrame;
    p.reimaginedSettings2.mode = osp::ReimaginedMode::toybox;
    p.reimaginedSettings2.kaleidoscope = { 0.9, 0.1 };
    CHECK (maxDifference (plain, play (p)) == 0.0f);
}

TEST_CASE ("reimagined per card: KALEIDOSCOPE's shared stage weighs each card's amount by how loud the card is heard", "[reimagined][v6]")
{
    Synth s;
    s.prepare (48000.0, 512, 4);
    Patch p = twoCards();
    p.reimaginedSettings.mode = p.reimaginedSettings2.mode = osp::ReimaginedMode::kaleidoscope;
    p.reimagined = 0.6;
    p.reimagined2 = 0.6;
    p.osc1Level = p.osc2Level = 0.5;
    s.setPatch (p);
    // One amount on both cards (every older patch): that amount.
    CHECK (std::abs (s.kaleidoscopeAmount() - 0.6) < 1.0e-12);
    // A alone at 100 %, B at 0 %, equal levels: half; B soloed: none of A's.
    p.reimagined = 1.0;
    p.reimagined2 = 0.0;
    s.setPatch (p);
    CHECK (std::abs (s.kaleidoscopeAmount() - 0.5) < 1.0e-12);
    p.osc1Heard = false;
    s.setPatch (p);
    CHECK (s.kaleidoscopeAmount() == 0.0);
}
