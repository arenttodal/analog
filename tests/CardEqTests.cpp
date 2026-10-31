// Each card's EQ and DAMPEN (ANDOR OSP's), inside every voice, before the analog filter: card A's
// acts on the oscillators (and the noise), card B's on ORGANIC. Off they touch nothing (the
// output is exactly the synth's), and each card's leaves the other card's part as it is.

#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using namespace testutil;

namespace
{
    std::shared_ptr<osp::InstrumentModel> sineRecording (double hz, double seconds, double root, double rate = 48000.0)
    {
        osp::AudioData audio;
        audio.sampleRate = rate;
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.5 * std::sin (2.0 * 3.14159265358979 * hz * static_cast<double> (i) / rate));
        audio.channels = { x, x };
        auto model = std::make_shared<osp::InstrumentModel>();
        model->rootMidi = root;
        model->original.source = std::make_shared<osp::PlaybackSource> (audio, root, 4);
        return model;
    }

    Stereo play (const Patch& p, const osp::InstrumentModel* model, std::vector<Event> events, double seconds = 0.5)
    {
        auto s = makeSynth (p);
        if (model != nullptr)
            s->setOrganic (model);
        return render (*s, seconds, std::move (events));
    }

    float maxDifference (const Stereo& a, const Stereo& b)
    {
        float d = a.left.size() == b.left.size() ? 0.0f : 1.0f;
        for (std::size_t i = 0; i < std::min (a.left.size(), b.left.size()); ++i)
            d = std::max ({ d, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
        return d;
    }

    double db (double ratio) { return 20.0 * std::log10 (std::max (1.0e-12, ratio)); }

    osp::eq::Settings highPass (double hz)
    {
        osp::eq::Settings e;
        e.enabled = true;
        auto& b = e.bands[static_cast<std::size_t> (osp::eq::Band::highPass)];
        b.enabled = true;
        b.frequencyHz = hz;
        b.steep = true;
        return e;
    }

    osp::eq::Settings lowPass (double hz)
    {
        osp::eq::Settings e;
        e.enabled = true;
        auto& b = e.bands[static_cast<std::size_t> (osp::eq::Band::lowPass)];
        b.enabled = true;
        b.frequencyHz = hz;
        b.steep = true;
        return e;
    }

    /** The oscillators (a saw at A2) only. */
    Patch oscillatorsOnly() { return measurementPatch(); }

    /** ORGANIC only (the oscillators off). */
    Patch organicOnly()
    {
        auto p = measurementPatch();
        p.osc1Enabled = p.osc2Enabled = false;
        return p;
    }
}

TEST_CASE ("card eq: switched off (or no band on) the output is exactly the synth's", "[cardeq]")
{
    const auto model = sineRecording (220.0, 2.0, 57.0);
    const auto plain = play (oscillatorsOnly(), model.get(), { on (0, 57), on (4800, 64) });

    auto off = oscillatorsOnly();
    off.eqA = lowPass (300.0);
    off.eqA.enabled = false;            // the EQ's own switch off, its bands set
    off.eqB = highPass (3000.0);
    off.eqB.bands[0].enabled = false;   // switched on, no band on
    CHECK (maxDifference (plain, play (off, model.get(), { on (0, 57), on (4800, 64) })) == 0.0f);

    auto dampenOff = oscillatorsOnly();
    dampenOff.dampenA.enabled = dampenOff.dampenB.enabled = true;
    dampenOff.dampenA.amount = dampenOff.dampenB.amount = 0.0;   // nothing to damp
    CHECK (maxDifference (plain, play (dampenOff, model.get(), { on (0, 57), on (4800, 64) })) == 0.0f);
}

TEST_CASE ("card eq: card A's EQ shapes the oscillators and leaves ORGANIC exactly as it was", "[cardeq]")
{
    const auto model = sineRecording (220.0, 2.0, 57.0);

    // The oscillators (a bright saw) through a low-pass at 300 Hz: much quieter.
    auto lp = oscillatorsOnly();
    lp.eqA = lowPass (300.0);
    const auto open = play (oscillatorsOnly(), nullptr, { on (0, 69) });
    const auto shaped = play (lp, nullptr, { on (0, 69) });
    CHECK (db (shaped.rms (0.2, 0.45) / open.rms (0.2, 0.45)) < -6.0);

    // ORGANIC alone: card A's EQ does not touch it.
    auto organicLp = organicOnly();
    organicLp.eqA = highPass (5000.0);
    CHECK (maxDifference (play (organicOnly(), model.get(), { on (0, 57) }), play (organicLp, model.get(), { on (0, 57) })) == 0.0f);
}

TEST_CASE ("card eq: card B's EQ shapes ORGANIC and leaves the oscillators exactly as they were", "[cardeq]")
{
    const auto model = sineRecording (220.0, 2.0, 57.0);

    // ORGANIC's sine at 220 Hz under a steep high-pass at 4 kHz: nearly gone.
    auto hp = organicOnly();
    hp.eqB = highPass (4000.0);
    const auto open = play (organicOnly(), model.get(), { on (0, 57) });
    const auto cut = play (hp, model.get(), { on (0, 57) });
    REQUIRE (open.rms (0.2, 0.45) > 1.0e-3);
    CHECK (db (cut.rms (0.2, 0.45) / open.rms (0.2, 0.45)) < -40.0);

    // The oscillators alone (no recording: card B is off): card B's EQ does not touch them.
    auto oscHp = oscillatorsOnly();
    oscHp.eqB = highPass (4000.0);
    CHECK (maxDifference (play (oscillatorsOnly(), nullptr, { on (0, 57) }), play (oscHp, nullptr, { on (0, 57) })) == 0.0f);

    // Layered: B's EQ removes ORGANIC's part; the oscillators' part stays.
    auto layeredHp = oscillatorsOnly();
    layeredHp.eqB = highPass (4000.0);
    const auto layered = play (oscillatorsOnly(), model.get(), { on (0, 57) });
    const auto withoutOrganic = play (oscillatorsOnly(), nullptr, { on (0, 57) });
    const auto layeredCut = play (layeredHp, model.get(), { on (0, 57) });
    // (The oscillators' part is the same in all three: what differs is ORGANIC's part.)
    const auto organicPart = [&] (const Stereo& x) {
        Stereo d = x;
        for (std::size_t i = 0; i < d.left.size(); ++i)
        {
            d.left[i] -= withoutOrganic.left[i];
            d.right[i] -= withoutOrganic.right[i];
        }
        return d.rms (0.2, 0.45);
    };
    REQUIRE (organicPart (layered) > 1.0e-3);
    CHECK (db (organicPart (layeredCut) / organicPart (layered)) < -40.0);
}

TEST_CASE ("card eq: DAMPEN damps by key (the default curve: the top keys, not the low ones)", "[cardeq][dampen]")
{
    auto damped = oscillatorsOnly();
    damped.dampenA.enabled = true;
    damped.dampenA.mode = osp::dampen::Mode::lowPass;
    damped.dampenA.amount = 1.0;
    damped.dampenA.minCutoffHz = 300.0;

    const auto loss = [&] (int note) {
        const auto open = play (oscillatorsOnly(), nullptr, { on (0, note) });
        const auto shut = play (damped, nullptr, { on (0, note) });
        return db (shut.rms (0.2, 0.45) / open.rms (0.2, 0.45));
    };
    const double low = loss (40), high = loss (110);
    CHECK (std::abs (low) < 0.1);   // below C4: no damping
    CHECK (high < -3.0);

    // Card B's DAMPEN leaves the oscillators alone.
    auto onB = oscillatorsOnly();
    onB.dampenB = damped.dampenA;
    CHECK (maxDifference (play (oscillatorsOnly(), nullptr, { on (0, 110) }), play (onB, nullptr, { on (0, 110) })) == 0.0f);
}

TEST_CASE ("card eq: moving a band while a note plays glides into place, the same every time", "[cardeq]")
{
    const auto run = [] {
        auto p = oscillatorsOnly();
        p.eqA = lowPass (8000.0);
        auto s = makeSynth (p);
        const auto first = render (*s, 0.25, { on (0, 57) });
        p.eqA = lowPass (400.0);
        s->setPatch (p);
        const auto second = render (*s, 0.25, {});
        Stereo all;
        all.left = first.left;
        all.left.insert (all.left.end(), second.left.begin(), second.left.end());
        all.right = first.right;
        all.right.insert (all.right.end(), second.right.begin(), second.right.end());
        return all;
    };
    const auto a = run();
    REQUIRE (a.finite());
    CHECK (maxDifference (a, run()) == 0.0f);

    // No click at the move: no sample-to-sample step larger than the saw's own before it.
    const auto move = static_cast<std::size_t> (0.25 * 48000.0);
    float before = 0.0f, after = 0.0f;
    for (std::size_t i = move / 2; i < move; ++i)
        before = std::max (before, std::abs (a.left[i] - a.left[i - 1]));
    for (std::size_t i = move; i < move + 2400; ++i)
        after = std::max (after, std::abs (a.left[i] - a.left[i - 1]));
    CHECK (after <= before * 1.05f);
    // Gliding (about 20 ms): the first 5 ms after the move are brighter than the settled sound.
    const auto settledRms = a.rms (0.40, 0.48);
    const auto justAfter = a.rms (0.25, 0.255);
    CHECK (justAfter > settledRms);
}
