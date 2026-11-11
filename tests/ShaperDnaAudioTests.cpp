// SHAPER DNA V1, gate 2: the per-note audio and the hand-over from lane A's SHAPER
// (docs/SHAPER_DNA_V1.md).

#include "TestUtil.h"

#include "fx/FxEngine.h"
#include "fx/FxGraph.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

using namespace testutil;
namespace fx = andor::analog::fx;
using andor::analog::ShaperDnaSettings;

namespace
{
    /** One saw, lane A's SHAPER (THREE) on VOL at full MOVEMENT; no other movement or effect. */
    Patch shaperPatch (osp::ShaperTarget target = osp::ShaperTarget::volume)
    {
        Patch p = measurementPatch();
        p.motion = 1.0;
        p.effects.movementMode = osp::MovementMode::shaper;
        p.effects.shaper.pattern = 3;
        p.effects.shaper.rate = osp::ShaperRate::sixteenth;
        p.effects.shaper.target = target;
        p.effects.shaper.smooth = 0.3;
        p.ampRelease = 0.05;
        return p;
    }

    Patch withDna (Patch p, ShaperDnaSettings::Assignment mode = ShaperDnaSettings::Assignment::cycle)
    {
        p.shaperDna.enabled = true;
        p.shaperDna.assignment = mode;
        return p;
    }

    /** The level in windows of `window` seconds from `from` to `to`. */
    std::vector<double> envelope (const Stereo& s, double from, double to, double window = 0.004)
    {
        std::vector<double> e;
        for (double t = from; t + window <= to; t += window)
            e.push_back (s.rms (t, t + window));
        return e;
    }

    /** The largest difference between two level envelopes, relative to the louder's peak. */
    double envelopeError (const Stereo& a, const Stereo& b, double from, double to)
    {
        const auto ea = envelope (a, from, to), eb = envelope (b, from, to);
        double peak = 1.0e-9, worst = 0.0;
        for (std::size_t i = 0; i < ea.size(); ++i)
        {
            peak = std::max ({ peak, ea[i], eb[i] });
            worst = std::max (worst, std::abs (ea[i] - eb[i]));
        }
        return worst / peak;
    }

    float maxDifference (const Stereo& a, const Stereo& b)
    {
        float d = 0.0f;
        for (std::size_t i = 0; i < std::min (a.left.size(), b.left.size()); ++i)
            d = std::max ({ d, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
        return d;
    }

    Stereo play (const Patch& p, std::vector<Event> events, double seconds = 1.5, const fx::Graph* graph = nullptr)
    {
        auto s = makeSynth (p);
        if (graph != nullptr)
            s->setFxGraph (fx::compile (*graph, 1));
        return render (*s, seconds, std::move (events));
    }

    const std::vector<Event> held { on (0, 60, 100) };

    /** Renders with the host's transport running (bpm, from `ppq0`), in blocks of `block`. */
    Stereo playWithTransport (const Patch& p, std::vector<Event> events, int block, double seconds = 1.5, double bpm = 120.0, double ppq0 = 0.0)
    {
        auto s = makeSynth (p, 48000.0, 4, 2048);
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });
        Stereo out;
        out.rate = 48000.0;
        const auto total = static_cast<int> (seconds * out.rate);
        out.left.assign (static_cast<std::size_t> (total), 0.0f);
        out.right.assign (static_cast<std::size_t> (total), 0.0f);
        std::size_t next = 0;
        for (int pos = 0; pos < total;)
        {
            const int end = std::min (total, pos + block);
            osp::HostTiming t;
            t.valid = t.playing = true;
            t.bpm = bpm;
            t.ppq = ppq0 + static_cast<double> (pos) / out.rate * bpm / 60.0;
            s->setTransport (t);
            int at = pos;
            while (at < end)
            {
                while (next < events.size() && events[next].sample <= at)
                {
                    s->handleMidi (events[next].bytes, events[next].size);
                    ++next;
                }
                int until = end;
                if (next < events.size() && events[next].sample < until)
                    until = events[next].sample;
                s->render (out.left.data() + at, out.right.data() + at, until - at);
                at = until;
            }
            pos = end;
        }
        return out;
    }
}

TEST_CASE ("SHAPER DNA: a FOLLOW profile plays what lane A's SHAPER plays - one SHAPER, never two", "[shaperdna][v8]")
{
    for (const auto target : { osp::ShaperTarget::volume, osp::ShaperTarget::both })
    {
        INFO ("target " << static_cast<int> (target));
        const auto global = play (shaperPatch (target), held);
        auto dna = withDna (shaperPatch (target));
        dna.shaperDna.rate[0] = ShaperDnaSettings::follow;   // A: the SHAPER's pattern and rate
        const auto perNote = play (dna, held);
        // The SHAPER gates the level; the per-note version gates it the same way, on the same
        // clock. Twice (a double SHAPER) would square the gate and miss by far more.
        CHECK (envelopeError (global, perNote, 0.1, 1.4) < 0.08);
        // The SHAPER really gates here (the comparison is not between two steady tones).
        auto still = shaperPatch (target);
        still.motion = 0.0;
        CHECK (envelopeError (global, play (still, held), 0.1, 1.4) > 0.3);
    }
}

TEST_CASE ("SHAPER DNA: each profile keeps its own rate - KEY ZONES, a low note at 1/16, a high one at 1/8T", "[shaperdna][v8]")
{
    auto dna = withDna (shaperPatch(), ShaperDnaSettings::Assignment::keyZones);
    dna.shaperDna.rate = { ShaperDnaSettings::follow, static_cast<int> (osp::ShaperRate::eighth), static_cast<int> (osp::ShaperRate::eighthTriplet) };
    const auto low = play (dna, { on (0, 48, 100) });    // A (below B3): the SHAPER's 1/16
    const auto high = play (dna, { on (0, 79, 100) });   // C (above B4): 1/8T
    auto at = [] (osp::ShaperRate r) {
        auto p = shaperPatch();
        p.effects.shaper.rate = r;
        return p;
    };
    const auto global16 = play (at (osp::ShaperRate::sixteenth), { on (0, 48, 100) });
    const auto global8T = play (at (osp::ShaperRate::eighthTriplet), { on (0, 79, 100) });
    CHECK (envelopeError (low, global16, 0.1, 1.4) < 0.08);
    CHECK (envelopeError (high, global8T, 0.1, 1.4) < 0.08);
    // ... and they really differ: the high note does not follow the 1/16 SHAPER.
    const auto global16High = play (at (osp::ShaperRate::sixteenth), { on (0, 79, 100) });
    CHECK (envelopeError (high, global16High, 0.1, 1.4) > 0.3);
}

TEST_CASE ("DNA-02: a note keeps its profile for its life - held, sustained, while others rotate and settings move", "[shaperdna][v8]")
{
    auto p = withDna (shaperPatch());
    auto s = makeSynth (p);
    auto step = [&] (std::vector<Event> e, double seconds = 0.05) { render (*s, seconds, std::move (e)); };
    step ({ on (0, 60, 100), on (0, 64, 100), on (0, 72, 100) });   // A, B, C in arrival order
    CHECK (s->shaperDnaProfileOf (60) == 0);
    CHECK (s->shaperDnaProfileOf (64) == 1);
    CHECK (s->shaperDnaProfileOf (72) == 2);
    // The chord under the held C5 changes: the new notes go on rotating; C5 stays C.
    step ({ off (0, 60), off (0, 64), on (10, 62, 100), on (10, 65, 100) });
    CHECK (s->shaperDnaProfileOf (72) == 2);
    CHECK (s->shaperDnaProfileOf (62) == 0);
    CHECK (s->shaperDnaProfileOf (65) == 1);
    // The sustain pedal holds it past its key; its release too keeps the profile.
    step ({ cc (0, 64, 127), off (0, 72) });
    CHECK (s->shaperDnaProfileOf (72) == 2);
    // Settings moved while it sounds: only new notes follow them.
    p.shaperDna.assignment = ShaperDnaSettings::Assignment::keyZones;
    p.shaperDna.splitAB = 100;
    s->setPatch (p);
    step ({});
    CHECK (s->shaperDnaProfileOf (72) == 2);
    step ({ on (0, 74, 100) });
    CHECK (s->shaperDnaProfileOf (74) == 0);   // (KEY ZONES now: below the new split)
    step ({ cc (0, 64, 0) });
    // A note played again is a new note: a new assignment.
    p.shaperDna.assignment = ShaperDnaSettings::Assignment::cycle;
    s->setPatch (p);
    step ({ off (0, 62), off (0, 65), off (0, 74) }, 0.3);
    step ({ on (0, 62, 100) });
    CHECK (s->shaperDnaProfileOf (62) >= 0);
}

TEST_CASE ("OLD-01 / DNA-07: OFF, or ON where it cannot play, renders bit for bit as before", "[shaperdna][v8]")
{
    const std::vector<Event> chord { on (0, 48, 100), on (300, 60, 90), on (900, 67, 80), off (40000, 48), off (40000, 60), off (40000, 67) };
    // Not SHAPER: PULSE (and DRIFT) with DNA on are exactly DNA off.
    for (const auto mode : { osp::MovementMode::pulse, osp::MovementMode::drift })
    {
        auto p = shaperPatch();
        p.effects.movementMode = mode;
        CHECK (maxDifference (play (p, chord), play (withDna (p), chord)) == 0.0f);
    }
    // Lane A off.
    {
        auto p = shaperPatch();
        p.movementA.on = false;
        CHECK (maxDifference (play (p, chord), play (withDna (p), chord)) == 0.0f);
    }
    // MOVEMENT bypassed in a route, or absent from MAIN (on SEND only): DNA suspended, the
    // setting kept.
    {
        auto g = fx::classicGraph();
        g.custom = true;
        REQUIRE (fx::setBypassed (g, { fx::Type::movement, 0 }, true));
        const auto p = shaperPatch();
        CHECK (maxDifference (play (p, chord, 1.5, &g), play (withDna (p), chord, 1.5, &g)) == 0.0f);
        fx::Graph onSend;
        onSend.custom = true;
        onSend.mainSend = true;
        onSend.chain = { fx::Node::effect ({ fx::Type::drive, 0 }) };
        onSend.send = { fx::Node::effect ({ fx::Type::movement, 0 }), fx::Node::effect ({ fx::Type::space, 0 }) };
        CHECK (maxDifference (play (p, chord, 1.5, &onSend), play (withDna (p), chord, 1.5, &onSend)) == 0.0f);
    }
    // DNA-06: at MOVEMENT 0 (the depth) it is transparent, exactly.
    {
        auto p = shaperPatch();
        p.motion = 0.0;
        CHECK (maxDifference (play (p, chord), play (withDna (p), chord)) == 0.0f);
    }
}

TEST_CASE ("OLD-04: switching DNA on and off mid-note hands over smoothly - never silence, never a doubled gate", "[shaperdna][v8]")
{
    for (const auto target : { osp::ShaperTarget::volume, osp::ShaperTarget::both })
    {
        INFO ("target " << static_cast<int> (target));
        const auto base = shaperPatch (target);
        auto dna = withDna (base);
        dna.shaperDna.rate[0] = ShaperDnaSettings::follow;
        const auto a = play (base, held), b = play (dna, held);
        // On at 0.6 s, off at 1.0 s, mid-note.
        auto s = makeSynth (base);
        auto first = render (*s, 0.6, held);
        s->setPatch (dna);
        auto middle = render (*s, 0.4, {});
        CHECK (s->shaperHandoff() > 0.999);
        CHECK (s->shaperDnaMix() > 0.999);
        s->setPatch (base);
        auto last = render (*s, 0.5, {});
        CHECK (s->shaperHandoff() < 1.0e-3);
        Stereo t;
        for (const auto* part : { &first, &middle, &last })
        {
            t.left.insert (t.left.end(), part->left.begin(), part->left.end());
            t.right.insert (t.right.end(), part->right.begin(), part->right.end());
        }
        // Across both hand-overs the level stays between the two (a little slack for the
        // glides): no dip to silence, no doubled gating. FILTER's share is split in octaves
        // between two LP12s in series, a little brighter than one for those 40 ms.
        const double above = target == osp::ShaperTarget::volume ? 1.25 : 1.45;
        const auto et = envelope (t, 0.5, 1.2, 0.002), ea = envelope (a, 0.5, 1.2, 0.002), eb = envelope (b, 0.5, 1.2, 0.002);
        double peak = 0.0;
        for (const auto x : ea)
            peak = std::max (peak, x);
        for (std::size_t i = 0; i < et.size(); ++i)
        {
            const double lo = std::min (ea[i], eb[i]), hi = std::max (ea[i], eb[i]);
            CHECK (et[i] >= 0.75 * lo - 0.03 * peak);
            CHECK (et[i] <= above * hi + 0.03 * peak);
        }
    }
}

TEST_CASE ("DNA-05: the same performance at block sizes 64 / 128 / 512 / 2048 with the host playing - the same sound", "[shaperdna][v8]")
{
    auto p = withDna (shaperPatch (osp::ShaperTarget::both));
    p.shaperDna.rate = { ShaperDnaSettings::follow, static_cast<int> (osp::ShaperRate::eighthTriplet), static_cast<int> (osp::ShaperRate::thirtySecond) };
    const std::vector<Event> notes { on (37, 48, 100), on (1031, 60, 90), on (5003, 67, 80), off (40001, 48), on (41017, 72, 100) };
    const auto reference = playWithTransport (p, notes, 64, 1.2, 117.0, 3.37);
    for (const int block : { 128, 512, 2048 })
    {
        INFO ("block " << block);
        CHECK (maxDifference (reference, playWithTransport (p, notes, block, 1.2, 117.0, 3.37)) < 1.0e-6f);
    }
    // DNA-04 on the host's grid: two notes on the same profile, started apart, gate together.
    auto same = withDna (shaperPatch());
    same.shaperDna.assignment = ShaperDnaSettings::Assignment::keyZones;   // (both in zone A)
    const auto early = playWithTransport (same, { on (0, 40, 100) }, 256, 1.2);
    const auto late = playWithTransport (same, { on (7001, 40, 100) }, 256, 1.2);
    CHECK (envelopeError (early, late, 0.3, 1.15) < 0.05);
}

TEST_CASE ("DNA-08: SUB stays the foundation - DNA never gates it, the handed-over SHAPER's trace no longer does", "[shaperdna][v8][sub]")
{
    auto p = shaperPatch();
    p.osc1Level = p.osc2Level = 0.0;
    p.sub.enabled = true;
    // Only SUB is heard (the voices are silent): its level in 10 ms windows.
    auto spread = [] (const Stereo& out) {
        double lo = 1.0e9, hi = 0.0;
        for (double t = 0.3; t < 1.3; t += 0.01)
        {
            const double r = out.rms (t, t + 0.01);
            lo = std::min (lo, r);
            hi = std::max (hi, r);
        }
        return hi > 0.0 ? lo / hi : 0.0;
    };
    // The note goes to the voices and to SUB (the musician's key), as in the plugin.
    auto playBoth = [] (const Patch& patch) {
        auto s = makeSynth (patch);
        Stereo out;
        out.rate = 48000.0;
        const int total = 72000;
        out.left.assign (total, 0.0f);
        out.right.assign (total, 0.0f);
        const auto e = on (0, 48, 100);
        s->handleMidi (e.bytes, e.size);
        s->handleSubMidi (e.bytes, e.size);
        for (int pos = 0; pos < total; pos += 256)
            s->render (out.left.data() + pos, out.right.data() + pos, std::min (256, total - pos));
        return out;
    };
    auto still = p;
    still.motion = 0.0;
    const auto gated = playBoth (p), steady = playBoth (withDna (p)), ungated = playBoth (still);
    CHECK (gated.rms (0.3, 1.3) > 1.0e-3);
    CHECK (spread (gated) < 0.5 * spread (ungated));   // the SHAPER (lane A, as before) gates SUB
    // DNA: SUB keeps its level, exactly as with no SHAPER at all (after the 40 ms hand-over).
    CHECK (envelopeError (steady, ungated, 0.1, 1.4) < 5.0e-3);
}

TEST_CASE ("DNA-09: every sounding note-on counts (cards' channels, arpeggiated notes); unison's voices of one note share its profile", "[shaperdna][v8]")
{
    // Notes on the cards' channels (the per-card arpeggiator's lanes) are notes like any other:
    // each its own assignment, in the order they sound.
    {
        auto p = withDna (shaperPatch());
        auto s = makeSynth (p);
        render (*s, 0.05, { on (0, 60, 100, 1), on (10, 64, 100, 2), on (20, 67, 100, 3), on (30, 71, 100, 1) });
        CHECK (s->shaperDnaProfileOf (60) == 0);
        CHECK (s->shaperDnaProfileOf (64) == 1);
        CHECK (s->shaperDnaProfileOf (67) == 2);
        CHECK (s->shaperDnaProfileOf (71) == 0);
        // An arpeggio (each step a note-on of its own): the profiles rotate with the steps.
        std::vector<Event> steps;
        for (int i = 0; i < 6; ++i)
        {
            steps.push_back (on (i * 2400, 48 + 2 * i, 100));
            steps.push_back (off (i * 2400 + 2000, 48 + 2 * i));
        }
        auto q = makeSynth (p);
        std::vector<int> seen;
        for (int i = 0; i < 6; ++i)
        {
            render (*q, 0.05, { on (0, 48 + 2 * i, 100) });
            seen.push_back (q->shaperDnaProfileOf (48 + 2 * i));
            render (*q, 0.01, { off (0, 48 + 2 * i) });
        }
        CHECK (seen == std::vector<int> { 0, 1, 2, 0, 1, 2 });
    }
    // Unison: one note's voices share one profile; the next note takes the next.
    {
        auto p = withDna (shaperPatch());
        p.unison = true;
        p.polyphony = 4;
        auto s = makeSynth (p);
        render (*s, 0.05, { on (0, 60, 100) });
        auto n = s->shaperDnaVoicesPerProfile();
        CHECK (n[0] >= 2);
        CHECK (n[1] == 0);
        CHECK (n[2] == 0);
    }
}

TEST_CASE ("CPU-01: SHAPER DNA's cost - 16 voices, 1x and 4x, three profiles, FILTER and BOTH (reported, not asserted)", "[shaperdna][cpu]")
{
    for (const int oversampling : { 1, 4 })
        for (int mode = 0; mode < 4; ++mode)
        {
            // 0: DNA off (lane A's SHAPER on the mix), 1: DNA on VOL, 2: FILTER, 3: BOTH.
            auto p = shaperPatch (mode == 2 ? osp::ShaperTarget::filter : (mode == 3 ? osp::ShaperTarget::both : osp::ShaperTarget::volume));
            p.polyphony = 16;
            if (mode > 0)
            {
                p = withDna (p);
                p.shaperDna.rate = { ShaperDnaSettings::follow, static_cast<int> (osp::ShaperRate::eighthTriplet), static_cast<int> (osp::ShaperRate::thirtySecond) };
                p.shaperDna.pattern = { ShaperDnaSettings::follow, 4, 9 };
            }
            auto s = makeSynth (p, 48000.0, oversampling, 512);
            for (int v = 0; v < 16; ++v)
            {
                const std::uint8_t note[3] { 0x90, static_cast<std::uint8_t> (40 + 2 * v), 100 };
                s->handleMidi (note, 3);
            }
            std::vector<float> l (512), r (512);
            for (int b = 0; b < 20; ++b)
                s->render (l.data(), r.data(), 512);   // (warm: the hand-over done)
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < 48000 * 4 / 512; ++b)
                s->render (l.data(), r.data(), 512);
            const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            static const char* names[] { "DNA off", "DNA VOL", "DNA FILTER", "DNA BOTH" };
            WARN (oversampling << "x, 16 voices, " << names[mode] << ": " << 100.0 * seconds / 4.0 << " % of one core");
        }
}
