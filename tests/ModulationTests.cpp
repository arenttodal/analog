// The modulation in the synth: ANDOR OSP's sources and routes reaching ANDOR OSP's destinations
// the synth plays and the product's own controls (through a ControlModulator), per note where
// a source is per note, deterministic, and without allocating on the audio thread.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace testutil;
namespace mod = osp::mod;
using Catch::Approx;

namespace
{
    /** The product's side, as the plugin's Reader does it: destination 0 is TONE (0..1). */
    struct ToneModulator final : ControlModulator
    {
        double base = 0.5;
        void moveControl (Patch& p, int k, double offset) const noexcept override
        {
            if (k == 0)
                p.tone = std::clamp (base + offset, 0.0, 1.0);
        }
    };

    void registerTone()
    {
        mod::setProductDestinations ({ { "test.tone", "TONE", mod::Owner::voice, mod::Update::continuous } });
    }

    mod::Settings withRoute (mod::Source source, mod::Dest dest, double depth)
    {
        mod::Settings s;
        s.routes[0].source = source;
        s.routes[0].dest = dest;
        s.routes[0].depth = depth;
        return s;
    }

    Patch tonePatch()
    {
        auto p = measurementPatch();
        p.tone = 0.5;   // the filter half open: TONE is heard
        return p;
    }

    double difference (const Stereo& a, const Stereo& b)
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            const double d = static_cast<double> (a.left[i]) - b.left[i];
            sum += d * d;
        }
        return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, a.left.size())));
    }

    bool identical (const Stereo& a, const Stereo& b)
    {
        return a.left == b.left && a.right == b.right;
    }

    const std::array<int, mod::ccSlots> controllers { 2, 11, 16, 17 };
}

TEST_CASE ("modulation: a route moves what it names; without it the synth plays exactly as before", "[modulation][routes]")
{
    registerTone();
    ToneModulator tone;
    const std::vector<Event> chord { on (0, 57, 60), on (0, 64, 110), off (12000, 57), off (12000, 64) };

    auto play = [&] (const mod::Settings* settings) {
        auto s = makeSynth (tonePatch());
        s->setControlModulator (&tone);
        if (settings != nullptr)
            s->setModulation (*settings, controllers);
        return render (*s, 0.4, chord);
    };
    const auto plain = play (nullptr);
    const mod::Settings none;
    CHECK (identical (plain, play (&none)));   // routes empty: nothing moves

    using D = mod::Dest;
    using S = mod::Source;
    // The product's own control (per note), and ANDOR OSP's destinations the voices play
    // (the shared stages' - DRIVE, MOVEMENT, SPACE, ECHO - are moved by the plugin's engine).
    for (const auto dest : { mod::productDest (0), D::levelA, D::panA, D::fineTuneA, D::ampSustain, D::ampAttack, D::ampRelease })
    {
        INFO ("destination " << mod::destInfo (dest).id);
        // (Towards where the knob has room: ATTACK and RELEASE are at their shortest.)
        const bool up = dest == D::ampAttack || dest == D::ampRelease;
        const auto settings = withRoute (S::vel, dest, up ? 0.5 : -0.5);
        const auto moved = play (&settings);
        CHECK (moved.finite());
        CHECK (difference (plain, moved) > 1.0e-5);
        CHECK (identical (moved, play (&settings)));   // the same every time
    }
}

TEST_CASE ("modulation: VEL is each note's own; a per-note source never reaches a shared stage", "[modulation][routes]")
{
    registerTone();
    ToneModulator tone;
    auto play = [&] (int velocity, bool route) {
        auto s = makeSynth (tonePatch());
        s->setControlModulator (&tone);
        if (route)
            s->setModulation (withRoute (mod::Source::vel, mod::productDest (0), -0.5), controllers);
        return render (*s, 0.3, { on (0, 60, velocity) });
    };
    // Soft and hard notes: VEL closes each one's TONE by its own amount (velocity's level
    // aside: VELOCITY RANGE 0), so the hard note loses more of its level.
    auto kept = [&] (int velocity) { return play (velocity, true).rms (0.05, 0.3) / play (velocity, false).rms (0.05, 0.3); };
    const double soft = kept (20), hard = kept (120);
    CHECK (soft < 1.0);
    CHECK (hard < 0.8 * soft);
    // The rules ANDOR OSP's routes follow: a POLY LFO has no single value on a shared stage.
    mod::Settings poly;
    poly.lfo[1].scope = mod::Scope::poly;
    CHECK_FALSE (mod::compatible (poly, mod::Source::lfo2, mod::Dest::drive));
    CHECK (mod::compatible (poly, mod::Source::lfo2, mod::productDest (0)));
}

TEST_CASE ("modulation: the controllers, the wheel and pressure drive their routes", "[modulation][routes]")
{
    registerTone();
    ToneModulator tone;
    auto play = [&] (mod::Source source, std::vector<Event> events) {
        auto s = makeSynth (tonePatch());
        s->setControlModulator (&tone);
        s->setModulation (withRoute (source, mod::productDest (0), -0.6), controllers);
        events.push_back (on (0, 60, 100));
        return render (*s, 0.3, events);
    };
    const auto still = play (mod::Source::cc2, {});
    // CC 2's slot follows controller 11 (expression); another controller does nothing to it.
    CHECK (difference (still, play (mod::Source::cc2, { cc (0, 11, 127) })) > 1.0e-5);
    CHECK (identical (still, play (mod::Source::cc2, { cc (0, 12, 127) })));
    CHECK (difference (still, play (mod::Source::modWheel, { cc (0, 1, 127) })) > 1.0e-5);
    const Event pressure { 0, { 0xd0, 127, 0 }, 2 };
    CHECK (difference (still, play (mod::Source::pressure, { pressure })) > 1.0e-5);
}

TEST_CASE ("modulation: LFO 2 and the envelopes run; ARP destinations reach the shared offsets", "[modulation][routes]")
{
    registerTone();
    ToneModulator tone;
    auto s = makeSynth (tonePatch());
    s->setControlModulator (&tone);
    auto settings = withRoute (mod::Source::lfo2, mod::productDest (0), 0.5);
    settings.lfo[1].rateHz = 5.0;
    settings.routes[1] = { mod::Source::env1, mod::Dest::levelA, -0.3, true, -1 };
    settings.routes[2] = { mod::Source::lfo2, mod::Dest::arpGate, 0.4, true, -1 };
    s->setModulation (settings, controllers);
    const auto out = render (*s, 0.5, { on (0, 60, 100) });
    CHECK (out.finite());
    const auto& rt = s->modulation();
    CHECK (rt.compiled.has (mod::productDest (0)));
    double offset = 0.0;
    CHECK (s->sharedOffset (mod::Dest::arpGate, offset));
    CHECK (std::abs (offset) <= 0.4 * mod::destInfo (mod::Dest::arpGate).span + 1.0e-9);
    CHECK_FALSE (s->sharedOffset (mod::Dest::arpRate, offset));
    const auto* newest = s->newestModulation();
    REQUIRE (newest != nullptr);
    CHECK (newest->values[mod::slot::env1] > 0.0f);   // the note's envelope is running
}

TEST_CASE ("modulation: reset starts the modulation over (the same events, the same sound)", "[modulation][determinism]")
{
    registerTone();
    ToneModulator tone;
    auto s = makeSynth (tonePatch());
    s->setControlModulator (&tone);
    auto settings = withRoute (mod::Source::lfo2, mod::productDest (0), 0.5);
    settings.lfo[1].rateHz = 3.0;
    s->setModulation (settings, controllers);
    const auto first = render (*s, 0.4, { on (0, 60, 100), cc (100, 11, 90) });
    s->reset();
    CHECK (identical (first, render (*s, 0.4, { on (0, 60, 100), cc (100, 11, 90) })));
}

TEST_CASE ("modulation: APPLY TO AMP shapes the notes' loudness by VEL's curve; a straight curve changes nothing", "[modulation]")
{
    auto play = [] (bool toAmp, bool curved) {
        auto p = measurementPatch();
        p.velocityRangeDb = 30.0;
        auto s = makeSynth (p);
        mod::Settings settings;
        settings.velocityToAmp = toAmp;
        if (curved)
            for (std::size_t i = 0; i < settings.velCurve.size(); ++i)
                settings.velCurve[i] = 1.0f;   // every note as loud as the hardest
        s->setModulation (settings, controllers);
        return render (*s, 0.3, { on (0, 60, 40) });
    };
    const auto plain = play (false, false);
    CHECK (identical (plain, play (true, false)));
    CHECK (identical (plain, play (false, true)));
    CHECK (play (true, true).rms (0.05, 0.3) > 2.0 * plain.rms (0.05, 0.3));
}
