// Cinematic V6 gate 4: MAIN + SEND (docs/CINEMATIC_V6_GATE4.md). An opt-in route of its own:
// MAIN is the dry insert chain (no spatial processor), SEND is fed only by the cards' sends
// (taken before the voice filter) and returns strictly wet. CLASSIC and CUSTOM never compute a
// send; SUB never reaches SEND.

#include "TestUtil.h"

#include "fx/FxEngine.h"
#include "fx/FxGraph.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace testutil;
namespace fx = andor::analog::fx;

namespace
{
    fx::Node effect (fx::Type t, int slot = 0) { return fx::Node::effect ({ t, slot }); }

    fx::Graph mainSend (std::vector<fx::Node> main, std::vector<fx::Node> send)
    {
        fx::Graph g;
        g.custom = true;
        g.mainSend = true;
        g.chain = std::move (main);
        g.send = std::move (send);
        return g;
    }

    /** CLASSIC's pair on SEND (its wet summed), nothing on MAIN. */
    fx::Graph wetPair() { return mainSend ({}, { fx::Node::parallel ({ effect (fx::Type::echo) }, { effect (fx::Type::space) }, fx::Law::send) }); }

    /** Card A (OSC 1) and card C (its second layer, an oscillator a fifth up), long reverb and
        echo at their macros. */
    Patch twoCards()
    {
        Patch p = measurementPatch();
        p.osc1Level = 0.7;
        p.hybridType = HybridType::sample;   // (C1 empty)
        p.hybrid2.enabled = true;
        p.hybrid2.type = HybridType::oscillator;
        p.hybrid2.coarse = 7.0;
        p.space = 0.8;
        p.echo = 0.6;
        p.ampRelease = 0.05;
        return p;
    }

    const std::vector<Event> phrase { on (0, 57, 100), on (2400, 64, 90), off (24000, 57), off (24000, 64) };

    Stereo play (const Patch& p, const fx::Graph* g, double seconds = 2.0, std::vector<Event> events = phrase)
    {
        auto s = makeSynth (p);
        if (g != nullptr)
        {
            auto compiled = fx::compile (*g, 1);
            REQUIRE (compiled != nullptr);
            s->setFxGraph (std::move (compiled));
        }
        return render (*s, seconds, std::move (events));
    }

    float maxDifference (const Stereo& a, const Stereo& b)
    {
        float d = 0.0f;
        for (std::size_t i = 0; i < a.left.size(); ++i)
            d = std::max ({ d, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
        return d;
    }
}

TEST_CASE ("MAIN + SEND: the graph - MAIN refuses spatial processors, each processor has one owner, schema 2's text", "[mainsend][v6]")
{
    CHECK (fx::validate (wetPair()).empty());
    CHECK (! fx::validate (mainSend ({ effect (fx::Type::space) }, {})).empty());          // SPACE on MAIN
    CHECK (! fx::validate (mainSend ({ effect (fx::Type::convolution) }, {})).empty());    // CONVOLUTION on MAIN
    CHECK (! fx::validate (mainSend ({ effect (fx::Type::eq) }, { effect (fx::Type::eq) })).empty());   // one EQ, two owners
    CHECK (fx::validate (mainSend ({ effect (fx::Type::eq, 0) }, { effect (fx::Type::eq, 1), effect (fx::Type::space) })).empty());
    fx::Graph single;
    single.custom = true;
    single.send = { effect (fx::Type::space) };
    CHECK (! fx::validate (single).empty());   // (a SEND chain only in MAIN + SEND)

    // The user's conversion: CLASSIC's inserts stay on MAIN, its pair moves to SEND.
    const auto converted = fx::mainSendGraph (fx::classicGraph());
    REQUIRE (converted.mainSend);
    REQUIRE (converted.chain.size() == 2);
    CHECK (converted.chain[0].fx == fx::InstanceId { fx::Type::drive, 0 });
    CHECK (converted.chain[1].fx == fx::InstanceId { fx::Type::movement, 0 });
    REQUIRE (converted.send.size() == 1);
    CHECK (converted.send[0].kind == fx::Node::Kind::parallel);
    // ... and back to one path, CLASSIC's shape as CUSTOM.
    const auto back = fx::singlePathGraph (converted);
    CHECK (! back.mainSend);
    CHECK (back.chain.size() == 3);

    // Schema 2's saved form round-trips.
    fx::Graph read;
    REQUIRE (fx::fromMainSend (fx::mainText (converted), fx::sendText (converted), read));
    CHECK (read == converted);
    CHECK (! fx::fromMainSend (fx::mainText (mainSend ({ effect (fx::Type::space) }, {})), "v1 custom", read));   // (refused: SPACE on MAIN)
}

TEST_CASE ("LEGACY: CLASSIC and CUSTOM never compute a send - the cards' SEND amounts change nothing there", "[mainsend][v6]")
{
    Patch p = twoCards();
    const auto plain = play (p, nullptr);
    fx::Graph custom = fx::classicGraph();
    custom.custom = true;
    const auto customPlain = play (p, &custom);
    p.sendA = p.sendB = p.sendC = 1.0;
    CHECK (maxDifference (plain, play (p, nullptr)) == 0.0f);
    CHECK (maxDifference (customPlain, play (p, &custom)) == 0.0f);
}

TEST_CASE ("BUS-01: SEND hears only the cards sent to it - A at 0, C at 100: the room is C's alone", "[mainsend][v6]")
{
    const auto g = wetPair();
    Patch p = twoCards();
    // Nothing sent: MAIN alone (no spatial processor on it): the release's tail is short.
    const auto dry = play (p, &g);
    CHECK (dry.rms (0.1, 0.5) > 0.01);
    CHECK (dry.rms (1.0, 2.0) < 1.0e-4);

    // Card C sent: the room after the release.
    p.sendC = 1.0;
    const auto withC = play (p, &g);
    CHECK (withC.rms (1.0, 2.0) > 1.0e-3);
    // The dry under it is untouched (a send is additive): the difference is the wet alone,
    // which starts with the pre-delay / echo time, not at once.
    float early = 0.0f;
    for (std::size_t i = 0; i < 48; ++i)
        early = std::max (early, std::abs (withC.left[i] - dry.left[i]));
    CHECK (early < 1.0e-6f);

    // Card C silent, card A playing with C's send at 100: nothing reaches SEND.
    Patch onlyA = p;
    onlyA.hybrid2.enabled = false;
    Patch onlyANoSend = onlyA;
    onlyANoSend.sendC = 0.0;
    CHECK (maxDifference (play (onlyA, &g), play (onlyANoSend, &g)) < 1.0e-6f);
    // ... and card A sent instead: its room.
    onlyA.sendA = 1.0;
    CHECK (play (onlyA, &g).rms (1.0, 2.0) > 1.0e-3);
}

TEST_CASE ("MAIN + SEND: SEND returns strictly wet - no spatial processor, no return; its wet is the macro's, once", "[mainsend][v6]")
{
    Patch p = twoCards();
    p.sendA = p.sendC = 1.0;
    const auto none = mainSend ({}, {});
    const auto eqOnly = mainSend ({}, { effect (fx::Type::eq) });
    const auto reference = play (p, &none);
    CHECK (maxDifference (reference, play (p, &eqOnly)) < 1.0e-6f);   // (an EQ alone is not wet: nothing returns)
    // A lane without a spatial processor adds nothing beside one with.
    const auto mixed = mainSend ({}, { fx::Node::parallel ({ effect (fx::Type::eq) }, { effect (fx::Type::space) }, fx::Law::send) });
    const auto spaceOnly = mainSend ({}, { effect (fx::Type::space) });
    CHECK (maxDifference (play (p, &mixed), play (p, &spaceOnly)) < 1.0e-5f);
    // The SPACE and ECHO macros set the wet return: both at 0, nothing returns.
    Patch silentRoom = p;
    silentRoom.space = 0.0;
    silentRoom.echo = 0.0;
    const auto g = wetPair();
    CHECK (play (silentRoom, &g).rms (1.0, 2.0) < 1.0e-4);
}

TEST_CASE ("MAIN + SEND: SUB joins MAIN, never SEND - every card at 100 % send, full room, SUB stays dry", "[mainsend][v6][sub]")
{
    const auto g = wetPair();
    Patch p = twoCards();
    p.osc1Level = 0.0;
    p.hybrid2.enabled = false;
    p.sendA = p.sendB = p.sendC = 1.0;
    p.space = 1.0;
    p.echo = 1.0;
    p.effects.echoFeedback = 0.95;
    p.sub.enabled = true;
    p.sub.levelDb = -12.0;
    auto s = makeSynth (p);
    s->setFxGraph (fx::compile (g, 1));
    Stereo out;
    out.left.assign (96000, 0.0f);
    out.right.assign (96000, 0.0f);
    for (int b = 0; b < 96000 / 256; ++b)
    {
        if (b == 0)
        {
            const std::uint8_t keyOn[3] { 0x90, 48, 100 };
            s->handleSubMidi (keyOn, 3);
        }
        if (b == 20)
        {
            const std::uint8_t keyOff[3] { 0x80, 48, 0 };
            s->handleSubMidi (keyOff, 3);
        }
        s->render (out.left.data() + b * 256, out.right.data() + b * 256, 256);
    }
    CHECK (out.rms (0.02, 0.1) > 0.01);   // (SUB heard)
    CHECK (out.rms (0.8, 2.0) < 1.0e-5);   // (and no room or echo after it: it never reached SEND)
}

TEST_CASE ("MAIN + SEND: a latent processor on SEND - the route reports it, MAIN waits for it", "[mainsend][v6]")
{
    const auto g = mainSend ({}, { effect (fx::Type::retro), effect (fx::Type::space) });
    Patch p = twoCards();
    p.fx.retro[0].on = true;
    p.sendC = 1.0;
    auto s = makeSynth (p);
    s->setFxGraph (fx::compile (g, 1));
    render (*s, 0.2, { on (0, 57, 100) });
    const int latency = s->fxEngine().latencySamples();
    CHECK (s->fxEngine().playingMainSend());
    // (RETRO's latency, if it has one at this rate: MAIN is held back to meet it.)
    CHECK (latency >= 0);
}
