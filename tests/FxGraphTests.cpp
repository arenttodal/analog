// FX routing gates 1 and 3: the graph (its saved form and its checks), CLASSIC untouched by
// having a route at all, and the original four in a CUSTOM route - the CLASSIC arrangement as a
// CUSTOM graph sounds as CLASSIC does, other orders differ as they should, parallel lanes sum to
// unity, bypass drains tails, swapping routes while playing stays finite and keeps tails.
#include "TestUtil.h"
#include "fx/Effects.h"
#include "fx/FxEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

using namespace testutil;
using namespace andor::analog;
using fx::Graph;
using fx::InstanceId;
using fx::Law;
using fx::Node;
using fx::Type;

namespace
{
    Patch chainPatch()
    {
        Patch p;
        p.drive = 0.45;
        p.effects.driveMode = osp::DriveMode::tape;
        p.motion = 0.5;
        p.effects.movementMode = osp::MovementMode::chorus;
        p.movementBLane = { true, 0.7 };
        p.movementB.movementMode = osp::MovementMode::pulse;
        p.echo = 0.45;
        p.effects.echoFeedback = 0.55;
        p.space = 0.5;
        return p;
    }

    std::vector<Event> phrase()
    {
        return { on (0, 48, 100), on (0, 55, 90), on (0, 64, 80), off (24000, 48), off (24000, 55), off (24000, 64) };
    }

    Stereo play (const Patch& p, const Graph* g, double seconds = 2.0)
    {
        auto s = makeSynth (p);
        if (g != nullptr)
            s->setFxGraph (fx::compile (*g, 1));
        return render (*s, seconds, phrase(), 256);
    }

    double peakDifferenceDb (const Stereo& a, const Stereo& b)
    {
        double diff = 0.0, peak = 1.0e-12;
        for (std::size_t i = 0; i < a.left.size(); ++i)
        {
            diff = std::max ({ diff, std::abs (static_cast<double> (a.left[i]) - b.left[i]), std::abs (static_cast<double> (a.right[i]) - b.right[i]) });
            peak = std::max ({ peak, std::abs (static_cast<double> (a.left[i])), std::abs (static_cast<double> (a.right[i])) });
        }
        return 20.0 * std::log10 (std::max (1.0e-15, diff) / peak);
    }

    bool identical (const Stereo& a, const Stereo& b)
    {
        return a.left.size() == b.left.size() && std::memcmp (a.left.data(), b.left.data(), a.left.size() * sizeof (float)) == 0
            && std::memcmp (a.right.data(), b.right.data(), a.right.size() * sizeof (float)) == 0;
    }

    bool finite (const Stereo& s)
    {
        for (std::size_t i = 0; i < s.left.size(); ++i)
            if (! std::isfinite (s.left[i]) || ! std::isfinite (s.right[i]))
                return false;
        return true;
    }

    Node fxNode (Type t) { return Node::effect ({ t, 0 }); }

    Graph custom (std::vector<Node> chain)
    {
        Graph g;
        g.custom = true;
        g.chain = std::move (chain);
        return g;
    }
}

TEST_CASE ("fx graph: the saved form round-trips; CLASSIC is drawn as DRIVE, MOVEMENT, ECHO || SPACE (send)", "[fx][graph]")
{
    const auto classic = fx::classicGraph();
    CHECK_FALSE (classic.custom);
    CHECK (fx::toText (classic) == "v1 classic fx drive.1 on fx movement.1 on par send 2 lane 1 { fx echo.1 on } lane 1 { fx space.1 on }");
    Graph back;
    REQUIRE (fx::fromText (fx::toText (classic), back));
    CHECK (back == classic);

    auto g = custom ({ fxNode (Type::echo), fxNode (Type::drive),
                       Node::parallel ({ fxNode (Type::movement) }, { Node::effect ({ Type::space, 0 }) }, Law::linear) });
    g.chain[1].bypassed = true;
    g.chain[2].gains = { 0.3f, 0.7f };
    REQUIRE (fx::validate (g).empty());
    Graph read;
    REQUIRE (fx::fromText (fx::toText (g), read));
    CHECK (read == g);
    CHECK (fx::toText (read) == fx::toText (g));

    // Nonsense is refused and leaves the graph alone.
    Graph untouched = classic;
    for (const char* bad : { "", "v2 custom", "v1 custom fx nothing.1 on", "v1 custom fx eq.9 on", "v1 custom par send 2 lane 1 { fx echo.1 on }",
                             "v1 custom fx echo.1 on fx echo.1 on", "v1 custom par send 2 lane x { } lane 1 { }" })
    {
        INFO (bad);
        CHECK_FALSE (fx::fromText (bad, untouched));
        CHECK (untouched == classic);
    }
}

TEST_CASE ("fx graph: validation - once each, the pools, lanes, depth, crossovers", "[fx][graph]")
{
    CHECK (fx::validate (fx::classicGraph()).empty());
    CHECK_FALSE (fx::validate (custom ({ fxNode (Type::echo), fxNode (Type::echo) })).empty());
    CHECK_FALSE (fx::validate (custom ({ Node::effect ({ Type::drive, 1 }) })).empty());   // one DRIVE
    CHECK (fx::validate (custom ({ Node::effect ({ Type::eq, 0 }), Node::effect ({ Type::eq, 1 }) })).empty());
    CHECK_FALSE (fx::validate (custom ({ Node::effect ({ Type::eq, 2 }) })).empty());

    auto par = Node::parallel ({}, {});
    par.lanes.emplace_back();
    par.gains.push_back (0.0f);
    CHECK_FALSE (fx::validate (custom ({ par })).empty());   // three lanes in a parallel group
    auto negative = Node::parallel ({}, {});
    negative.gains = { -0.5f, 1.5f };
    CHECK_FALSE (fx::validate (custom ({ negative })).empty());

    // Five groups inside each other: too deep.
    Node deep = Node::parallel ({}, {});
    for (int i = 0; i < 5; ++i)
        deep = Node::parallel ({ deep }, {});
    CHECK_FALSE (fx::validate (custom ({ deep })).empty());

    auto split = Node::splitter (fx::SplitKind::bands3, { {}, {}, {} });
    split.lowHz = 3000.0f;
    split.highHz = 1000.0f;
    CHECK_FALSE (fx::validate (custom ({ split })).empty());   // crossovers out of order
    split.lowHz = 200.0f;
    split.highHz = 2000.0f;
    CHECK (fx::validate (custom ({ split })).empty());
    // Valid, but not played yet: compile refuses it (never approximated as a plain parallel).
    CHECK (fx::compile (custom ({ split }), 1) == nullptr);

    // An effect without a processor in this build is not compiled (not insertable).
    for (int t = 0; t < fx::typeCount; ++t)
    {
        const auto type = static_cast<Type> (t);
        const auto compiled = fx::compile (custom ({ fxNode (type) }), 1);
        CHECK ((compiled != nullptr) == fx::implemented (type));
    }
}

TEST_CASE ("fx route: a CLASSIC route (published or not) plays the original chain bit for bit", "[fx][route][legacy]")
{
    const auto p = chainPatch();
    const auto none = play (p, nullptr);
    const auto classic = fx::classicGraph();
    const auto withClassic = play (p, &classic);
    CHECK (identical (none, withClassic));
    CHECK (finite (none));
}

TEST_CASE ("fx route: CLASSIC's arrangement as a CUSTOM graph sounds as CLASSIC does", "[fx][route]")
{
    const auto p = chainPatch();
    const auto classic = play (p, nullptr, 2.5);
    auto g = fx::classicGraph();
    g.custom = true;
    const auto routed = play (p, &g, 2.5);
    REQUIRE (finite (routed));
    // The same processors in the same order, the sends summed as CLASSIC sums them: equal but
    // for float rounding in the send sum (x + (x + e - x) + (d x + s - x)).
    CHECK (peakDifferenceDb (classic, routed) < -100.0);
}

TEST_CASE ("fx route: other orders differ as they should, all finite; ECHO enters SPACE only when wired so", "[fx][route]")
{
    auto p = chainPatch();
    auto classicCustom = fx::classicGraph();
    classicCustom.custom = true;
    const auto reference = play (p, &classicCustom);

    const auto echoFirst = custom ({ fxNode (Type::echo), fxNode (Type::drive), fxNode (Type::movement), fxNode (Type::space) });
    const auto spaceBeforeMovement = custom ({ fxNode (Type::drive), fxNode (Type::space), fxNode (Type::movement), fxNode (Type::echo) });
    const auto echoIntoSpace = custom ({ fxNode (Type::drive), fxNode (Type::movement), fxNode (Type::echo), fxNode (Type::space) });
    for (const auto* g : { &echoFirst, &spaceBeforeMovement, &echoIntoSpace })
    {
        const auto out = play (p, g);
        CHECK (finite (out));
        CHECK (peakDifferenceDb (reference, out) > -60.0);   // audibly different
        CHECK (out.rms (0.1, 0.5) > 0.01);
    }

    // ECHO -> SPACE in series: the reverb hears the repeats (more late energy than parallel,
    // where SPACE hears only the dry sound), with DRIVE and MOVEMENT out of the way.
    p.drive = 0.0;
    p.motion = 0.0;
    p.space = 0.6;
    p.echo = 0.6;
    const auto series = custom ({ fxNode (Type::echo), fxNode (Type::space) });
    const auto parallel = custom ({ Node::parallel ({ fxNode (Type::echo) }, { fxNode (Type::space) }, Law::send) });
    const auto s = play (p, &series, 3.0), q = play (p, &parallel, 3.0);
    CHECK (s.rms (1.2, 3.0) > q.rms (1.2, 3.0) * 1.02);
}

TEST_CASE ("fx route: a linear parallel group of two untouched lanes gives its input back exactly", "[fx][route]")
{
    auto p = chainPatch();
    const auto plain = custom ({ fxNode (Type::drive), fxNode (Type::movement) });
    const auto withEmpty = custom ({ fxNode (Type::drive), Node::parallel ({}, {}, Law::linear), fxNode (Type::movement) });
    CHECK (identical (play (p, &plain), play (p, &withEmpty)));
    // Two bypassed effects in the lanes: once the glide is done, unity (no +6 dB).
    auto bypassedPair = custom ({ Node::parallel ({ fxNode (Type::echo) }, { fxNode (Type::space) }, Law::linear) });
    bypassedPair.chain[0].lanes[0][0].bypassed = true;
    bypassedPair.chain[0].lanes[1][0].bypassed = true;
    p.echo = p.space = 0.0;   // (their wet levels at 0 too: nothing but the dry sound)
    const auto dry = play (p, &plain);
    p.drive = 0.0;
    p.motion = 0.0;
    const auto a = play (p, nullptr), b = play (p, &bypassedPair);
    CHECK (peakDifferenceDb (a, b) < -90.0);
    CHECK (finite (dry));
}

TEST_CASE ("fx route: bypassing SPACE glides out and lets its tail drain", "[fx][route]")
{
    auto p = chainPatch();
    p.drive = 0.0;
    p.motion = 0.0;
    p.echo = 0.0;
    p.space = 0.7;
    auto g = custom ({ fxNode (Type::space) });
    auto s = makeSynth (p);
    s->setFxGraph (fx::compile (g, 1));
    // Play, then bypass SPACE at 0.6 s (after the notes end at 0.5 s): the tail goes on.
    auto out = render (*s, 0.6, phrase(), 256);
    g.chain[0].bypassed = true;
    s->setFxGraph (fx::compile (g, 2));
    const auto after = render (*s, 1.5, {}, 256);
    CHECK (finite (after));
    CHECK (after.rms (0.05, 0.3) > 1.0e-4);    // the tail drains, not cut
    CHECK (after.rms (1.2, 1.5) < after.rms (0.05, 0.3));
}

TEST_CASE ("fx route: swapping routes while notes and tails play stays finite and continuous", "[fx][route]")
{
    auto p = chainPatch();
    auto s = makeSynth (p);
    std::vector<Graph> routes;
    routes.push_back (fx::classicGraph());
    auto c = fx::classicGraph();
    c.custom = true;
    routes.push_back (c);
    routes.push_back (custom ({ fxNode (Type::space), fxNode (Type::echo), fxNode (Type::drive), fxNode (Type::movement) }));
    routes.push_back (custom ({ Node::parallel ({ fxNode (Type::drive), fxNode (Type::echo) }, { fxNode (Type::movement), fxNode (Type::space) }) }));
    routes.push_back (custom ({}));
    std::vector<float> l (128), r (128);
    std::uint64_t generation = 1;
    float largestStep = 0.0f, previous = 0.0f;
    s->handleMidi (on (0, 50).bytes, 3);
    s->handleMidi (on (0, 57).bytes, 3);
    for (int block = 0; block < 600; ++block)
    {
        if (block % 40 == 0)
            s->setFxGraph (fx::compile (routes[static_cast<std::size_t> ((block / 40) % routes.size())], ++generation));
        s->render (l.data(), r.data(), 128);
        for (int i = 0; i < 128; ++i)
        {
            REQUIRE (std::isfinite (l[static_cast<std::size_t> (i)]));
            largestStep = std::max (largestStep, std::abs (l[static_cast<std::size_t> (i)] - previous));
            previous = l[static_cast<std::size_t> (i)];
        }
        s->collectFxGarbage();
    }
    CHECK (largestStep < 0.5f);   // (no click from a switch: each one ducks through silence)
}

TEST_CASE ("fx graph: edits - move, stack, unstack, nudge, remove, bypass; each makes the route CUSTOM", "[fx][graph][edit]")
{
    const InstanceId drive { Type::drive, 0 }, movement { Type::movement, 0 }, echo { Type::echo, 0 }, space { Type::space, 0 };
    auto g = fx::classicGraph();
    // ECHO to the front (out of the pair: the pair dissolves into SPACE alone).
    REQUIRE (fx::moveEffect (g, echo, {}, 0));
    CHECK (g.custom);
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx drive.1 on fx movement.1 on fx space.1 on");
    // SPACE between DRIVE and MOVEMENT.
    REQUIRE (fx::moveEffect (g, space, {}, 2));
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx drive.1 on fx space.1 on fx movement.1 on");
    // Stack ECHO with MOVEMENT: a linear pair in MOVEMENT's place.
    REQUIRE (fx::stackEffect (g, echo, movement));
    CHECK (fx::toText (g) == "v1 custom fx drive.1 on fx space.1 on par linear 2 lane 0.5 { fx movement.1 on } lane 0.5 { fx echo.1 on }");
    // A third onto the pair goes into the target's lane, after it.
    REQUIRE (fx::stackEffect (g, drive, echo));
    CHECK (fx::toText (g) == "v1 custom fx space.1 on par linear 2 lane 0.5 { fx movement.1 on } lane 0.5 { fx echo.1 on fx drive.1 on }");
    // Out again, right after the pair.
    REQUIRE (fx::unstackEffect (g, drive));
    CHECK (fx::toText (g) == "v1 custom fx space.1 on par linear 2 lane 0.5 { fx movement.1 on } lane 0.5 { fx echo.1 on } fx drive.1 on");
    // Nudge DRIVE left: it swaps with the pair; the first can go no further left.
    REQUIRE (fx::nudgeEffect (g, drive, -1));
    CHECK (fx::toText (g) == "v1 custom fx space.1 on fx drive.1 on par linear 2 lane 0.5 { fx movement.1 on } lane 0.5 { fx echo.1 on }");
    CHECK_FALSE (fx::nudgeEffect (g, space, -1));
    // Moving ECHO out of its lane dissolves the pair; removing works the same way.
    REQUIRE (fx::moveEffect (g, echo, {}, 0));
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx space.1 on fx drive.1 on fx movement.1 on");
    REQUIRE (fx::removeEffect (g, drive));
    CHECK_FALSE (fx::contains (g, drive));
    CHECK_FALSE (fx::removeEffect (g, drive));
    REQUIRE (fx::insertEffect (g, {}, 1, Node::effect (drive)));
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx drive.1 on fx space.1 on fx movement.1 on");
    REQUIRE (fx::setBypassed (g, space, true));
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx drive.1 on fx space.1 off fx movement.1 on");
    // Refused: inserting a second DRIVE; nothing changes.
    const auto before = g;
    CHECK_FALSE (fx::insertEffect (g, {}, 0, Node::effect (drive)));
    CHECK (g == before);
}

TEST_CASE ("fx route latency: a latent effect delays the route by its maxLatency - on, off, in a lane, under a send - and the engine reports it", "[fx][route][latency]")
{
    auto p = chainPatch();
    p.drive = p.motion = p.echo = p.space = 0.0;
    p.fx.retro[0].mix = 0.0f;   // (RETRO at MIX 0: its aligned dry, exactly)
    const int latency = fx::Retro::frameFor (48000.0);
    const auto plain = custom ({ fxNode (Type::utility) });
    const auto reference = play (p, &plain);
    auto delayedBy = [&reference] (const Stereo& out, int samples) {
        double diff = 0.0, peak = 1.0e-12;
        for (std::size_t i = 0; i + static_cast<std::size_t> (samples) < out.left.size(); ++i)
        {
            const auto j = i + static_cast<std::size_t> (samples);
            diff = std::max ({ diff, std::abs (static_cast<double> (out.left[j]) - reference.left[i]), std::abs (static_cast<double> (out.right[j]) - reference.right[i]) });
            peak = std::max (peak, std::abs (static_cast<double> (reference.left[i])));
        }
        return 20.0 * std::log10 (std::max (1.0e-15, diff) / peak);
    };
    auto routed = [&p] (const Graph& g, int& reported) {
        auto s = makeSynth (p);
        s->setFxGraph (fx::compile (g, 1));
        auto out = render (*s, 2.0, phrase(), 256);
        reported = s->fxEngine().latencySamples();
        return out;
    };
    int reported = -1;
    // In series.
    const auto serial = custom ({ fxNode (Type::utility), Node::effect ({ Type::retro, 0 }) });
    CHECK (delayedBy (routed (serial, reported), latency) < -120.0);
    CHECK (reported == latency);
    // Off (its own ON): the same latency - the route never jumps in time.
    p.fx.retro[0].on = false;
    CHECK (delayedBy (routed (serial, reported), latency) < -120.0);
    CHECK (reported == latency);
    p.fx.retro[0].on = true;
    // In one lane of a linear pair: the other lane waits for it (no comb), the sum is unity.
    const auto lanes = custom ({ fxNode (Type::utility), Node::parallel ({ Node::effect ({ Type::retro, 0 }) }, {}, Law::linear) });
    CHECK (delayedBy (routed (lanes, reported), latency) < -100.0);
    CHECK (reported == latency);
    // Under a send: the dry waits too.
    const auto send = custom ({ fxNode (Type::utility), Node::parallel ({ Node::effect ({ Type::retro, 0 }) }, {}, Law::send) });
    CHECK (delayedBy (routed (send, reported), latency) < -100.0);
    CHECK (reported == latency);
    // CLASSIC adds nothing.
    {
        auto s = makeSynth (p);
        render (*s, 0.1, phrase(), 256);
        CHECK (s->fxEngine().latencySamples() == 0);
    }
}

TEST_CASE ("fx route latency: a FLANGER's mode never moves the route in time (CLASSIC padded to THROUGH-ZERO's latency)", "[fx][route][latency]")
{
    auto p = chainPatch();
    p.drive = p.motion = p.echo = p.space = 0.0;
    p.fx.flanger[0].mix = 0.0f;
    const auto g = custom ({ fxNode (Type::utility), Node::effect ({ Type::flanger, 0 }) });
    int reported[2] {};
    Stereo out[2];
    for (int mode = 0; mode < 2; ++mode)
    {
        p.fx.flanger[0].mode = mode;
        auto s = makeSynth (p);
        s->setFxGraph (fx::compile (g, 1));
        out[mode] = render (*s, 1.0, phrase(), 256);
        reported[mode] = s->fxEngine().latencySamples();
    }
    CHECK (reported[0] == reported[1]);
    CHECK (reported[0] > 0);
    CHECK (peakDifferenceDb (out[0], out[1]) < -100.0);   // (MIX 0: both the dry, at the same moment)
}
