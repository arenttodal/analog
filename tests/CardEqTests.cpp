// Each card's EQ and DAMPEN (ANDOR OSP's), inside every voice, before the analog filter: card A's
// acts on the oscillators (and the noise), card B's on ORGANIC. Off they touch nothing (the
// output is exactly the synth's), and each card's leaves the other card's part as it is.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
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

namespace
{
    /** Both oscillators sounding (OSC 1 a saw, OSC 2 a pulse a fifth up). */
    Patch bothOscillators()
    {
        auto p = measurementPatch();
        p.osc2Level = 1.0;
        p.blend = 0.5;
        p.osc2Wave = Osc2Wave::pulse;
        p.osc2Coarse = 7.0;
        return p;
    }

    Stereo playLegacy (const Patch& p, std::vector<Event> events)
    {
        auto s = makeSynth (p);
        s->setLegacyCardsForTest (true);
        return render (*s, 0.5, std::move (events));
    }
}

TEST_CASE ("card eq (three engines): card A's EQ shapes OSC 1 alone, card B's (layer C) OSC 2 alone", "[cardeq][threeengines]")
{
    const std::vector<Event> note { on (0, 57) };
    // OSC 1 only: card B's EQ (OSC 2's) touches nothing.
    auto one = oscillatorsOnly();
    auto oneB = one;
    oneB.eqC = lowPass (300.0);
    CHECK (maxDifference (play (one, nullptr, note), play (oneB, nullptr, note)) < 1.0e-6f);
    // OSC 2 only: card A's EQ (OSC 1's and the noise mixer's) touches nothing; card B's shapes it.
    auto two = bothOscillators();
    two.osc1Enabled = false;
    auto twoA = two;
    twoA.eqA = lowPass (300.0);
    CHECK (maxDifference (play (two, nullptr, note), play (twoA, nullptr, note)) < 1.0e-6f);
    auto twoB = two;
    twoB.eqC = lowPass (300.0);
    CHECK (db (play (twoB, nullptr, note).rms (0.2, 0.45) / play (two, nullptr, note).rms (0.2, 0.45)) < -3.0);
}

TEST_CASE ("card eq (three engines): a migrated pair, the same EQ and DAMPEN on both cards, sounds as the old pair's", "[cardeq][threeengines][state]")
{
    // Before state version 8 card A's EQ and DAMPEN processed the oscillators' sum; the
    // migration copies them to card B. The curves are linear: EQ (1) + EQ (2) = EQ (1 + 2).
    const std::vector<Event> notes { on (0, 57), on (2400, 64) };
    for (const bool ring : { false, true })
    {
        INFO ((ring ? "with RING" : "no ring"));
        auto old = bothOscillators();
        old.ringMix = ring ? 0.4 : 0.0;
        old.noiseLevel = 0.2;
        old.eqA = lowPass (900.0);
        old.dampenA.enabled = true;
        old.dampenA.amount = 0.5;
        auto migrated = old;
        migrated.eqC = old.eqA;
        migrated.dampenC = old.dampenA;
        const auto before = playLegacy (old, notes);
        const auto after = play (migrated, nullptr, notes);
        REQUIRE (before.rms (0.1, 0.45) > 1.0e-3);
        CHECK (maxDifference (before, after) < 1.0e-4f);
    }
}

TEST_CASE ("three engines: each oscillator's card M / S silences only its oscillator", "[threeengines]")
{
    const std::vector<Event> note { on (0, 57) };
    const auto both = bothOscillators();
    auto noOne = both;
    noOne.osc1Level = 0.0;
    auto muteOne = both;
    muteOne.osc1Heard = false;
    CHECK (maxDifference (play (noOne, nullptr, note), play (muteOne, nullptr, note)) < 1.0e-6f);
    auto noTwo = both;
    noTwo.osc2Level = 0.0;
    auto muteTwo = both;
    muteTwo.osc2Heard = false;
    CHECK (maxDifference (play (noTwo, nullptr, note), play (muteTwo, nullptr, note)) < 1.0e-6f);
    // OSC 1's fine tune moves OSC 1.
    auto fine = both;
    fine.osc1FineCents = 30.0;
    CHECK (maxDifference (play (both, nullptr, note), play (fine, nullptr, note)) > 1.0e-3f);
}

// Card C GRANULAR (its own REGULAR / GRANULAR switch): its recording as ANDOR OSP's grains.
namespace
{
    double rmsBetween (const std::vector<float>& x, double from, double to, double rate = 48000.0)
    {
        double s = 0.0;
        const auto a = static_cast<std::size_t> (from * rate), z = std::min (x.size(), static_cast<std::size_t> (to * rate));
        for (auto i = a; i < z; ++i)
            s += static_cast<double> (x[i]) * x[i];
        return std::sqrt (s / static_cast<double> (std::max<std::size_t> (1, z - a)));
    }

    double zeroCrossingHz (const std::vector<float>& x, double from, double to, double rate = 48000.0)
    {
        int crossings = 0;
        const auto a = static_cast<std::size_t> (from * rate), z = std::min (x.size(), static_cast<std::size_t> (to * rate));
        for (auto i = a + 1; i < z; ++i)
            crossings += (x[i - 1] < 0.0f) != (x[i] < 0.0f) ? 1 : 0;
        return 0.5 * crossings / (static_cast<double> (z - a) / rate);
    }
}

TEST_CASE ("card C GRANULAR: its recording as grains - sustains past its end while held, at the note's pitch, deterministic; off as before", "[cardeq][granularc]")
{
    // A short recording (0.4 s) with LOOP off: read through it ends; as grains it holds.
    const auto model = sineRecording (220.0, 0.4, 57.0);
    auto regular = organicOnly();
    regular.organic.loop = false;
    auto granular = regular;
    granular.granularC = true;
    granular.grainsC.position = 0.5;
    const std::vector<Event> held { on (0, 69), off (static_cast<int> (1.4 * 48000), 69) };
    const auto a = play (regular, model.get(), held, 1.5);
    const auto b = play (granular, model.get(), held, 1.5);
    const double regularLate = rmsBetween (a.left, 0.8, 1.3), granularLate = rmsBetween (b.left, 0.8, 1.3);
    INFO ("at 0.8-1.3 s: regular " << regularLate << ", granular " << granularLate);
    CHECK (regularLate < 1.0e-4);           // (played out)
    CHECK (granularLate > 0.02);            // (grains while the key is held)
    // At the note's pitch: A4 on a 220 Hz recording rooted at A3 -> 440 Hz.
    CHECK (zeroCrossingHz (b.left, 0.8, 1.3) == Catch::Approx (440.0).epsilon (0.06));
    // Deterministic (seeded per note).
    CHECK (play (granular, model.get(), held, 1.5).left == b.left);
    // Off: exactly as before (the switch's default).
    CHECK (play (regular, model.get(), held, 1.5).left == a.left);
    // Switched on while a note plays: its grains take over (sound past the recording's end).
    auto s = makeSynth (regular);
    s->setOrganic (model.get());
    const unsigned char noteOn[3] { 0x90, 69, 100 };
    s->handleMidi (noteOn, 3);
    std::vector<float> l (480), r (480), late;
    for (int block = 0; block < 130; ++block)
    {
        if (block == 8)   // (0.08 s: the recording, read an octave up, ends at 0.2 s)
            s->setPatch (granular);
        s->render (l.data(), r.data(), 480);
        if (block >= 80)
            late.insert (late.end(), l.begin(), l.end());
    }
    INFO ("after the switch, 0.8-1.3 s: " << rmsBetween (late, 0.0, 1.0));
    CHECK (rmsBetween (late, 0.0, 1.0) > 0.02);
}
