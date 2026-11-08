// FX ROUTING gate 0: the frozen baseline of the sound after the voices. Every factory sound and
// a matrix of the post-voice chain (DRIVE's three modes, MOVEMENT's 25 lane pairs, ECHO's three
// machines in each stereo mode, SPACE's seven rooms and engines, the chorus, KALEIDOSCOPE, 1 / 8 /
// 16 voices, three sample rates, 1x / 2x / 4x oversampling, silence, repeated notes, tails)
// rendered and compared with tests/golden/fx-baseline-renders.txt, written before the FX
// routing work began (ANALOG 51910c6, OSP b5655b2).
//
// The CLASSIC route must keep every one of these renders: exact hashes on the platform that
// stored them (osp::platformTag), a coarse loudness contour (16 RMS windows, 0.1 %) everywhere.
// OSP_UPDATE_GOLDEN=1 rewrites the file: only for an intentional change of the legacy sound,
// explained in the commit - never to make the CLASSIC route pass.
#include "FactoryPatches.h"
#include "TestUtil.h"
#include "core/PlatformTag.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace testutil;

namespace
{
    struct Case
    {
        std::string name;
        Patch patch;
        osp::keydna::Settings keyDna;
        std::vector<Event> events;
        double seconds = 1.0, rate = 48000.0;
        int oversampling = 4;
    };

    std::vector<Event> phrase()
    {
        // A chord, a line over it, everything released at 0.55 s (the rest is the tails).
        return { on (0, 48, 100), on (0, 55, 90), on (0, 64, 70), on (9600, 72, 120), off (19200, 72), off (26400, 48), off (26400, 55), off (26400, 64) };
    }

    std::vector<Event> sixteen()
    {
        std::vector<Event> e;
        for (int i = 0; i < 16; ++i)
            e.push_back (on (i * 120, 36 + 3 * i, 60 + 3 * i));
        for (int i = 0; i < 16; ++i)
            e.push_back (off (24000 + i * 60, 36 + 3 * i));
        return e;
    }

    std::vector<Event> repeated()
    {
        std::vector<Event> e;
        for (int i = 0; i < 12; ++i)
        {
            e.push_back (on (i * 3000, 60, 40 + 6 * i));
            e.push_back (off (i * 3000 + 1500, 60));
        }
        return e;
    }

    /** A plain patch with every post-voice effect off but the one a case turns on. */
    Patch dryPatch()
    {
        Patch p;
        p.motion = 0.0;
        p.space = 0.0;
        p.echo = 0.0;
        p.drive = 0.0;
        return p;
    }

    std::vector<Case> cases()
    {
        std::vector<Case> list;
        for (const auto& f : factoryPatches())
        {
            auto name = "factory/" + f.name;
            std::replace (name.begin(), name.end(), ' ', '_');   // (keys without spaces)
            list.push_back ({ name, f.patch, keyDnaOf (f), phrase(), 1.0 });
        }

        auto add = [&list] (const std::string& name, auto&& change, double seconds = 1.0, std::vector<Event> events = phrase()) {
            Patch p = dryPatch();
            change (p);
            list.push_back ({ "fx/" + name, p, {}, std::move (events), seconds });
        };
        add ("dry", [] (Patch&) {});
        // DRIVE: its three modes at two amounts.
        const char* drives[] { "tube", "tape", "crunch" };
        for (int m = 0; m < 3; ++m)
            for (const double amount : { 0.35, 0.9 })
                add (std::string ("drive-") + drives[m] + "-" + std::to_string (static_cast<int> (100 * amount)), [m, amount] (Patch& p) {
                    p.drive = amount;
                    p.effects.driveMode = static_cast<osp::DriveMode> (m);
                    p.effects.driveTone = 0.7;
                });
        // MOVEMENT: every lane A x lane B pair (lane B on), and lane A alone.
        const char* moves[] { "drift", "tape", "chorus", "pulse", "shaper" };
        for (int a = 0; a < 5; ++a)
        {
            add (std::string ("movement-") + moves[a], [a] (Patch& p) {
                p.motion = 0.7;
                p.effects.movementMode = static_cast<osp::MovementMode> (a);
            });
            for (int b = 0; b < 5; ++b)
                add (std::string ("movement-") + moves[a] + "+" + moves[b], [a, b] (Patch& p) {
                    p.motion = 0.6;
                    p.effects.movementMode = static_cast<osp::MovementMode> (a);
                    p.movementBLane = { true, 0.8 };
                    p.movementB.movementMode = static_cast<osp::MovementMode> (b);
                });
        }
        // ECHO: each machine in each stereo mode, with its tail.
        const char* echoes[] { "tape", "bbd", "ascend" };
        const char* stereos[] { "mono", "ping", "wide" };
        for (int t = 0; t < 3; ++t)
            for (int s = 0; s < 3; ++s)
                add (std::string ("echo-") + echoes[t] + "-" + stereos[s], [t, s] (Patch& p) {
                    p.echo = 0.55;
                    p.effects.echoType = static_cast<osp::EchoType> (t);
                    p.effects.echoStereo = static_cast<osp::EchoStereo> (s);
                    p.effects.echoFeedback = 0.6;
                }, 2.0);
        add ("echo-free-time", [] (Patch& p) {
            p.echo = 0.5;
            p.effects.echoSync = false;
            p.effects.echoTimeMs = 230.0;
        }, 2.0);
        // SPACE: the four rooms, ABYSS (and frozen), VINTAGE, BLOOM, with their tails.
        const char* rooms[] { "room", "hall", "plate", "spring" };
        for (int r = 0; r < 4; ++r)
            add (std::string ("space-") + rooms[r], [r] (Patch& p) {
                p.space = 0.5;
                p.effects.spaceEngine = osp::SpaceEngine::rooms;
                p.effects.spaceType = static_cast<osp::SpaceType> (r);
            }, 2.5);
        const char* engines[] { "rooms", "abyss", "vintage", "bloom" };
        for (int e = 1; e < 4; ++e)
            add (std::string ("space-") + engines[e], [e] (Patch& p) {
                p.space = 0.5;
                p.effects.spaceEngine = static_cast<osp::SpaceEngine> (e);
            }, 2.5);
        add ("space-abyss-freeze", [] (Patch& p) {
            p.space = 0.6;
            p.effects.spaceEngine = osp::SpaceEngine::abyss;
            p.effects.abyssFreeze = true;
        }, 2.5);
        // The whole chain at once (ECHO and SPACE are parallel sends: echo never enters SPACE).
        auto full = [] (Patch& p) {
            p.drive = 0.4;
            p.motion = 0.5;
            p.effects.movementMode = osp::MovementMode::chorus;
            p.movementBLane = { true, 0.7 };
            p.movementB.movementMode = osp::MovementMode::pulse;
            p.echo = 0.4;
            p.space = 0.45;
        };
        add ("full-chain", full, 2.5);
        add ("full-chain-sixteen", full, 2.0, sixteen());
        add ("full-chain-repeated", full, 2.5, repeated());
        add ("full-chain-silence", full, 1.0, {});
        // Upstream of the routing (locked): the chorus and KALEIDOSCOPE, with the chain on.
        for (int c = 1; c <= 3; ++c)
            add ("chorus-" + std::to_string (c), [c, full] (Patch& p) {
                full (p);
                p.chorusMode = c;
                p.chorusNoise = c == 2;
            }, 1.5);
        add ("kaleidoscope", [full] (Patch& p) {
            full (p);
            p.reimagined = 0.7;
            p.reimaginedSettings.mode = osp::ReimaginedMode::kaleidoscope;
        }, 1.5);
        // Voices.
        for (const int voices : { 1, 8, 16 })
            add ("voices-" + std::to_string (voices), [voices, full] (Patch& p) {
                full (p);
                p.polyphony = voices;
            }, 1.5, sixteen());
        // Rates and oversampling, the whole chain.
        for (const double rate : { 44100.0, 96000.0 })
        {
            Patch p = dryPatch();
            full (p);
            list.push_back ({ "rate/" + std::to_string (static_cast<int> (rate)), p, {}, phrase(), 2.0, rate });
        }
        for (const int os : { 1, 2 })
        {
            Patch p = dryPatch();
            full (p);
            list.push_back ({ "oversampling/" + std::to_string (os) + "x", p, {}, phrase(), 2.0, 48000.0, os });
        }
        return list;
    }

    struct Fingerprint
    {
        std::uint64_t hash = 0;
        std::array<double, 16> rms {};
    };

    Fingerprint fingerprintOf (const Case& c)
    {
        auto s = makeSynth (c.patch, c.rate, c.oversampling);
        if (c.keyDna.enabled)
            s->setKeyDna (c.keyDna);
        auto events = c.events;
        for (auto& e : events)   // (the events are written at 48 kHz)
            e.sample = static_cast<int> (std::llround (e.sample * c.rate / 48000.0));
        const auto out = render (*s, c.seconds, events, 173);
        Fingerprint f;
        std::uint64_t h = 1469598103934665603ull;   // FNV-1a over the samples' bits
        for (std::size_t i = 0; i < out.left.size(); ++i)
            for (const float x : { out.left[i], out.right[i] })
            {
                std::uint32_t bits;
                std::memcpy (&bits, &x, sizeof bits);
                h = (h ^ bits) * 1099511628211ull;
            }
        f.hash = h;
        for (std::size_t w = 0; w < f.rms.size(); ++w)
            f.rms[w] = out.rms (c.seconds * static_cast<double> (w) / 16.0, c.seconds * static_cast<double> (w + 1) / 16.0);
        return f;
    }

    std::string goldenPath() { return std::string (ANALOG_TEST_DIR) + "/golden/fx-baseline-renders.txt"; }
}

TEST_CASE ("fx baseline: the post-voice chain renders exactly as before the FX routing (CLASSIC)", "[fx][legacy][regression]")
{
    const auto platform = osp::platformTag();
    const bool update = std::getenv ("OSP_UPDATE_GOLDEN") != nullptr;
    std::map<std::string, std::string> stored;
    {
        std::ifstream in (goldenPath());
        std::string line;
        while (std::getline (in, line))
        {
            const auto space = line.find (' ', line.find (' ') + 1);
            if (space != std::string::npos)
                stored[line.substr (0, space)] = line.substr (space + 1);
        }
    }
    const auto all = cases();
    CHECK (all.size() > 100);
    std::ostringstream written;
    int compared = 0;
    for (const auto& c : all)
    {
        INFO (c.name);
        const auto f = fingerprintOf (c);
        std::ostringstream rms;
        rms.precision (9);
        for (std::size_t i = 0; i < f.rms.size(); ++i)
            rms << (i ? " " : "") << f.rms[i];
        const auto rmsKey = "rms " + c.name, hashKey = "hash:" + platform + " " + c.name;
        written << rmsKey << " " << rms.str() << "\n" << hashKey << " " << f.hash << "\n";
        if (update)
            continue;
        const auto r = stored.find (rmsKey);
        REQUIRE (r != stored.end());
        std::istringstream in (r->second);
        for (const double expected : f.rms)
        {
            double was = 0.0;
            in >> was;
            CHECK (std::abs (expected - was) <= 1.0e-3 * std::max (1.0e-6, was));
        }
        if (const auto h = stored.find (hashKey); h != stored.end())
        {
            CHECK (std::to_string (f.hash) == h->second);
            ++compared;
        }
    }
    if (update)
    {
        std::ofstream (goldenPath()) << written.str();
        WARN ("fx baseline renders rewritten: " << goldenPath());
    }
    else
        INFO ("exact hashes compared: " << compared);
}

TEST_CASE ("fx baseline: CPU of the post-voice chain at 1, 8 and 16 voices (reported, not asserted)", "[fx][cpu][.]")
{
    // Seconds of audio per second of computing, 48 kHz, 4x, blocks of 256: the chain off (DRIVE,
    // MOVEMENT, ECHO and SPACE at 0) and on (the full chain), one core.
    for (const bool chain : { false, true })
        for (const int voices : { 1, 8, 16 })
        {
            Patch p = dryPatch();
            p.polyphony = voices;
            if (chain)
            {
                p.drive = 0.4;
                p.motion = 0.5;
                p.effects.movementMode = osp::MovementMode::chorus;
                p.movementBLane = { true, 0.7 };
                p.movementB.movementMode = osp::MovementMode::pulse;
                p.echo = 0.4;
                p.space = 0.45;
            }
            auto s = makeSynth (p, 48000.0, 4, 256);
            std::vector<Event> held;
            for (int i = 0; i < voices; ++i)
                held.push_back (on (0, 40 + 3 * i, 90));
            const auto start = std::chrono::steady_clock::now();
            const auto out = render (*s, 4.0, held, 256);
            const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            const double percent = 100.0 * took / 4.0;
            std::printf ("[fx cpu] chain %s, %2d voices: %.1f %% of one core (render %.3f s for 4 s)\n", chain ? "on " : "off", voices, percent, took);
            CHECK (out.left.size() > 0);
        }
}
