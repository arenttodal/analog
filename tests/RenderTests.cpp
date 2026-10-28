// Whole renders: determinism, silence, headroom, every modulation route doing something
// (and nothing when off), ANALOG variation, the effects, sample rates and the factory sounds.

#include "TestUtil.h"

#include "FactoryPatches.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace testutil;
using Catch::Approx;

namespace
{
    bool identical (const Stereo& a, const Stereo& b)
    {
        if (a.left.size() != b.left.size())
            return false;
        for (std::size_t i = 0; i < a.left.size(); ++i)
            if (a.left[i] != b.left[i] || a.right[i] != b.right[i])
                return false;
        return true;
    }
    double difference (const Stereo& a, const Stereo& b)
    {
        double d = 0.0;
        for (std::size_t i = 0; i < std::min (a.left.size(), b.left.size()); ++i)
            d += std::abs (a.left[i] - b.left[i]) + std::abs (a.right[i] - b.right[i]);
        return d / static_cast<double> (a.left.size());
    }
    const std::vector<Event> phrase { on (0, 48), on (0, 55), on (0, 60), on (0, 64), off (36000, 48), off (36000, 55), off (36000, 60), off (36000, 64) };
}

TEST_CASE ("render: the same seed, patch and notes give the same output, bit for bit", "[render][determinism]")
{
    for (const auto& factory : factoryPatches())
    {
        INFO (factory.name);
        auto a = makeSynth (factory.patch);
        auto b = makeSynth (factory.patch);
        CHECK (identical (render (*a, 1.0, phrase), render (*b, 1.0, phrase)));
    }
    // reset() returns to the same start.
    auto s = makeSynth (factoryPatches()[1].patch);
    const auto first = render (*s, 1.0, phrase);
    s->reset();
    CHECK (identical (first, render (*s, 1.0, phrase)));
    // Another seed is another instrument (ANALOG's tolerances differ).
    auto p = factoryPatches()[1].patch;
    p.seed = 77;
    auto other = makeSynth (p);
    CHECK_FALSE (identical (first, render (*other, 1.0, phrase)));
}

TEST_CASE ("render: no notes, no sound", "[render][unit]")
{
    for (const auto& factory : factoryPatches())
    {
        INFO (factory.name);
        auto s = makeSynth (factory.patch);
        const auto out = render (*s, 0.5, {});
        CHECK (out.peak() == 0.0f);
    }
}

TEST_CASE ("render: the INIT patch and every factory sound play a five-note chord without clipping", "[render][headroom]")
{
    const std::vector<Event> chord { on (0, 48, 127), on (0, 55, 127), on (0, 60, 127), on (0, 64, 127), on (0, 67, 127) };
    for (const auto& factory : factoryPatches())
    {
        INFO (factory.name);
        auto s = makeSynth (factory.patch);
        const auto out = render (*s, 2.5, chord);
        UNSCOPED_INFO (factory.name << ": peak " << 20.0 * std::log10 (std::max (1.0e-9f, out.peak())) << " dBFS, rms "
                                    << 20.0 * std::log10 (std::max (1.0e-9, out.rms (0.2, 2.0))) << " dBFS");
        CHECK (out.finite());
        CHECK (out.peak() < 0.8f);           // below the guard's knee: never shaped
        CHECK (out.rms (0.0, 2.5) > 0.005);  // and clearly audible
    }
}

TEST_CASE ("render: every modulation route moves the sound when on, and nothing when off", "[render][modulation]")
{
    auto base = measurementPatch();
    base.osc1Wave = Osc1Wave::pulse;
    base.osc2Level = 0.5;
    base.blend = 0.5;
    base.tone = math::hzToTone (2000.0);
    base.envDepthSemitones = 0.0;
    base.filterAttack = 0.3;
    auto play = [] (const Patch& p) {
        auto s = makeSynth (p);
        return render (*s, 0.8, { on (0, 57) });
    };
    const auto reference = play (base);

    struct Route
    {
        const char* name;
        void (*on) (Patch&);
    };
    const Route routes[] = {
        { "poly mod env -> pitch 1", [] (Patch& p) { p.polyModEnv = 0.6; p.polyModToPitch1 = true; } },
        { "poly mod env -> width 1", [] (Patch& p) { p.polyModEnv = 0.6; p.polyModToWidth1 = true; } },
        { "poly mod env -> cutoff", [] (Patch& p) { p.polyModEnv = 0.6; p.polyModToCutoff = true; } },
        { "poly mod osc 2 -> pitch 1", [] (Patch& p) { p.polyModOsc2 = 0.5; p.polyModToPitch1 = true; } },
        { "poly mod osc 2 -> width 1", [] (Patch& p) { p.polyModOsc2 = 0.5; p.polyModToWidth1 = true; } },
        { "poly mod osc 2 -> cutoff", [] (Patch& p) { p.polyModOsc2 = 0.5; p.polyModToCutoff = true; } },
        { "lfo -> pitch 1", [] (Patch& p) { p.lfoAmount = 1.0; p.lfoPitch1 = 2.0; p.lfoPitch2 = 0.0; p.lfoWidth1 = p.lfoWidth2 = 0.0; } },
        { "lfo -> pitch 2", [] (Patch& p) { p.lfoAmount = 1.0; p.lfoPitch2 = 2.0; p.lfoPitch1 = 0.0; p.lfoWidth1 = p.lfoWidth2 = 0.0; } },
        { "lfo -> width 1", [] (Patch& p) { p.lfoAmount = 1.0; p.lfoWidth1 = 0.3; p.lfoPitch1 = p.lfoPitch2 = 0.0; p.lfoWidth2 = 0.0; } },
        { "lfo -> width 2", [] (Patch& p) { p.lfoAmount = 1.0; p.osc2Wave = Osc2Wave::pulse; p.lfoWidth2 = 0.3; p.lfoPitch1 = p.lfoPitch2 = 0.0; p.lfoWidth1 = 0.0; } },
        { "lfo -> cutoff", [] (Patch& p) { p.lfoAmount = 1.0; p.lfoCutoff = 24.0; p.lfoPitch1 = p.lfoPitch2 = 0.0; p.lfoWidth1 = p.lfoWidth2 = 0.0; } },
        { "filter envelope", [] (Patch& p) { p.envDepthSemitones = 30.0; } },
        { "sync", [] (Patch& p) { p.sync = true; p.osc1Coarse = 5.0; } },
        { "noise", [] (Patch& p) { p.noiseLevel = 0.5; } },
        { "blend", [] (Patch& p) { p.blend = 0.9; } },
        { "filter drive", [] (Patch& p) { p.filterDrive = 0.8; } },
        { "analog", [] (Patch& p) { p.analogAmount = 1.0; } },
        { "space", [] (Patch& p) { p.space = 0.6; } },
        { "echo", [] (Patch& p) { p.echo = 0.6; p.effects.echoSync = false; p.effects.echoTimeMs = 120.0; } },
    };
    for (const auto& r : routes)
    {
        INFO (r.name);
        auto p = base;
        r.on (p);
        CHECK (difference (play (p), reference) > 1.0e-3);
    }

    // The amounts without a destination do nothing; destinations without an amount neither.
    auto amounts = base;
    amounts.polyModEnv = 0.8;
    amounts.polyModOsc2 = 0.8;
    CHECK (identical (play (amounts), reference));
    auto destinations = base;
    destinations.polyModToPitch1 = destinations.polyModToWidth1 = destinations.polyModToCutoff = true;
    CHECK (identical (play (destinations), reference));
    auto depths = base;
    depths.lfoPitch1 = 5.0;
    depths.lfoCutoff = 20.0;
    depths.lfoAmount = 0.0;
    CHECK (identical (play (depths), reference));
}

TEST_CASE ("render: the mod wheel brings in the LFO's depths", "[render][modulation]")
{
    auto p = measurementPatch();
    p.lfoAmount = 0.0;
    p.lfoPitch1 = 1.0;
    auto quiet = makeSynth (p);
    const auto still = render (*quiet, 1.0, { on (0, 69) });
    auto moving = makeSynth (p);
    const auto wobble = render (*moving, 1.0, { cc (0, 1, 127), on (0, 69) });
    CHECK (difference (still, wobble) > 1.0e-3);
}

TEST_CASE ("render: ANALOG spreads the voices within the specified range, from the seed", "[render][analog]")
{
    // 50 %: OSC 1 is +-10 cents x 0.5 x the slot's tolerance (-1..1), plus a +-1.5 cent wander.
    auto p = measurementPatch();
    p.analogAmount = 0.5;
    p.polyphony = 4;
    double largest = 0.0;
    for (int slot = 0; slot < 4; ++slot)
    {
        auto s = makeSynth (p);
        std::vector<Event> events;
        for (int i = 0; i <= slot; ++i)
            events.push_back (on (i * 100, 40 + i));   // the slot `slot` gets note 69 next
        events.push_back (on (2000, 69));
        for (int i = 0; i <= slot; ++i)
            events.push_back (off (2100, 40 + i));
        const auto out = render (*s, 1.5, events);
        const double c = 1200.0 * std::log2 (frequencyByCrossings (out.mono(), 48000.0, 0.4, 1.4) / 440.0);
        UNSCOPED_INFO ("slot " << slot << ": " << c << " cents (tolerance " << 0.5 * s->slotPitchOffsetCents ((slot + 1) % 4) << ")");
        CHECK (std::abs (c) <= 6.6);
        largest = std::max (largest, std::abs (c));
    }
    CHECK (largest > 0.5);   // it does vary
}

TEST_CASE ("render: works at every common sample rate and with 1x, 2x and 4x oversampling", "[render][unit]")
{
    for (const double rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (const int os : { 1, 2, 4 })
        {
            INFO ("rate " << rate << " oversampling " << os);
            auto s = makeSynth (factoryPatches()[2].patch, rate, os);
            const auto out = render (*s, 0.5, { on (0, 60), on (0, 64) });
            CHECK (out.finite());
            CHECK (out.rms (0.1, 0.5) > 0.005);
            CHECK (out.peak() < 0.8f);
        }
}

TEST_CASE ("render: blocks of 0, 1, 7, 64, 2048 samples render the same audio", "[render][unit]")
{
    std::vector<Stereo> outs;
    for (const int block : { 1, 7, 64, 2048 })
    {
        auto s = makeSynth (factoryPatches()[0].patch, 48000.0, 4, 2048);
        float dummyL = 0.0f, dummyR = 0.0f;
        s->render (&dummyL, &dummyR, 0);   // nothing, no harm
        outs.push_back (render (*s, 0.5, { on (0, 60), off (12000, 60) }, block));
    }
    for (std::size_t i = 1; i < outs.size(); ++i)
        CHECK (identical (outs[0], outs[i]));
}

TEST_CASE ("render: the factory sounds are many and distinct", "[render][presets]")
{
    const auto& list = factoryPatches();
    CHECK (list.size() >= 20);
    for (std::size_t i = 0; i < list.size(); ++i)
        for (std::size_t j = i + 1; j < list.size(); ++j)
            CHECK (list[i].name != list[j].name);
}
