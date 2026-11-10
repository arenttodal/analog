// Cinematic V6 gate 3: card C's second layer (C2). It plays beside C1 inside card C: card C's
// lane, its M / S, its LOOP / FOLLOW, its EQ and DAMPEN, MORPH, the filter and every effect hear
// C1 and C2 together. Its own: what it plays (OSC, NOISE, or its own loaded source), its LEVEL,
// pitch and VELO. Off, nothing of it is heard or computed: exactly the old sound (C2-03).

#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace testutil;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    std::shared_ptr<osp::PlaybackSource> tone (double hz, double seconds, double root, double rate = 44100.0)
    {
        osp::AudioData audio;
        audio.sampleRate = rate;
        std::vector<float> x (static_cast<std::size_t> (seconds * rate));
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (0.5 * std::sin (2.0 * pi * hz * static_cast<double> (i) / rate));
        audio.channels.assign (1, x);
        return std::make_shared<osp::PlaybackSource> (audio, root, 4);
    }

    SampleSource oneZone (double hz, double root, double seconds = 2.0)
    {
        SampleSource s;
        s.name = "tone";
        SampleZone z;
        z.audio = tone (hz, seconds, root);
        z.root = root;
        s.zones.push_back (z);
        return s;
    }

    /** Card C alone: the oscillators off, the filter open, a plain gate. */
    Patch cardCOnly()
    {
        auto p = measurementPatch();
        p.osc1Enabled = p.osc2Enabled = false;
        p.hybridType = HybridType::sample;   // (C1: nothing loaded, silent)
        return p;
    }

    float maxDifference (const Stereo& a, const Stereo& b)
    {
        float d = 0.0f;
        for (std::size_t i = 0; i < a.left.size(); ++i)
            d = std::max ({ d, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
        return d;
    }

    Stereo play (const Patch& p, std::vector<Event> events, double seconds = 0.8, const SampleSource* c1 = nullptr, const SampleSource* c2 = nullptr)
    {
        auto s = makeSynth (p);
        s->setSampleSource (c1);
        s->setSampleSource2 (c2);
        return render (*s, seconds, std::move (events));
    }

    const std::vector<Event> chord { on (0, 57, 100), on (0, 64, 90), on (4000, 69, 80), off (24000, 57), off (24000, 64), off (30000, 69) };
}

TEST_CASE ("C2-03: card C's second layer off (whatever its settings) renders exactly as before", "[hybrid2][v6]")
{
    for (const auto type : { HybridType::sample, HybridType::oscillator, HybridType::noise })
    {
        INFO ("C1 type " << static_cast<int> (type));
        Patch p = measurementPatch();
        p.hybridType = type;
        const auto plain = play (p, chord);
        p.hybrid2.type = HybridType::noise;
        p.hybrid2.levelDb = 6.0;
        p.hybrid2.octave = 1;
        p.hybrid2.model = OscModel::wide;
        const auto source = oneZone (440.0, 69.0);
        CHECK (maxDifference (plain, play (p, chord, 0.8, nullptr, &source)) == 0.0f);   // (its source loaded, it off)
    }
}

TEST_CASE ("C2: card C's second layer as an oscillator plays at its own pitch beside C1", "[hybrid2][v6]")
{
    Patch p = cardCOnly();
    p.hybrid2.enabled = true;
    p.hybrid2.type = HybridType::oscillator;
    p.hybrid2.wave = Osc2Wave::saw;
    const auto base = play (p, { on (0, 57, 100) });
    CHECK (base.rms (0.1, 0.8) > 0.01);
    CHECK (std::abs (frequencyByCrossings (base.mono(), 48000.0, 0.2, 0.8) - 220.0) < 1.0);
    p.hybrid2.octave = 1;
    p.hybrid2.coarse = 7.0;
    const auto up = play (p, { on (0, 57, 100) });
    CHECK (std::abs (frequencyByCrossings (up.mono(), 48000.0, 0.2, 0.8) - 220.0 * 2.0 * std::exp2 (7.0 / 12.0)) < 2.0);
    // LEVEL: -48 dB and below silent; VELO: soft notes quieter.
    p.hybrid2.levelDb = -48.0;
    CHECK (play (p, { on (0, 57, 100) }).peak() < 1.0e-6f);
    p.hybrid2.levelDb = 0.0;
    p.hybrid2.velo = 1.0;
    CHECK (play (p, { on (0, 57, 30) }).rms (0.1, 0.8) < 0.3 * play (p, { on (0, 57, 127) }).rms (0.1, 0.8));
}

TEST_CASE ("C2: its NOISE and its own loaded source play; C1 and C2 layer (both heard)", "[hybrid2][v6]")
{
    Patch p = cardCOnly();
    p.hybrid2.enabled = true;
    p.hybrid2.type = HybridType::noise;
    CHECK (play (p, { on (0, 60, 100) }).rms (0.1, 0.8) > 0.01);

    // Its loaded source: a 440 Hz tone rooted at A4 plays A3 an octave down.
    const auto source = oneZone (440.0, 69.0);
    p.hybrid2.type = HybridType::instrumentPatch;
    const auto zone = play (p, { on (0, 57, 100) }, 0.8, nullptr, &source);
    CHECK (std::abs (frequencyByCrossings (zone.mono(), 48000.0, 0.2, 0.8) - 220.0) < 1.0);
    p.hybrid2.type = HybridType::sample;   // (SAMPLE: the same loaded source)
    CHECK (maxDifference (zone, play (p, { on (0, 57, 100) }, 0.8, nullptr, &source)) == 0.0f);
    // Not loaded: silent (no source, nothing to play).
    CHECK (play (p, { on (0, 57, 100) }).peak() < 1.0e-6f);

    // Layered: C1 as OSC 3 and C2's zones both heard.
    Patch both = p;
    both.hybridType = HybridType::oscillator;
    both.hybrid2.coarse = 7.0;   // (a fifth apart: no antiphase between OSC 3's saw and the tone)
    const auto c1 = [&] {
        Patch q = both;
        q.hybrid2.enabled = false;
        return play (q, { on (0, 57, 100) }, 0.8, nullptr, &source);
    }();
    const auto layered = play (both, { on (0, 57, 100) }, 0.8, nullptr, &source);
    const auto c2 = [&] {
        Patch q = both;
        q.hybridType = HybridType::sample;   // (C1 empty)
        return play (q, { on (0, 57, 100) }, 0.8, nullptr, &source);
    }();
    CHECK (c1.rms (0.1, 0.8) > 0.01);
    CHECK (c2.rms (0.1, 0.8) > 0.01);
    // Two unrelated pitches: their powers add.
    const double expected = std::hypot (c1.rms (0.1, 0.8), c2.rms (0.1, 0.8));
    CHECK (std::abs (layered.rms (0.1, 0.8) - expected) < 0.1 * expected);
}

TEST_CASE ("C2: it follows card C - its lane, its M / S, its LOOP and FOLLOW", "[hybrid2][v6]")
{
    Patch p = cardCOnly();
    p.hybrid2.enabled = true;
    p.hybrid2.type = HybridType::oscillator;
    // Card C muted (or another card soloed): C2 silent too.
    p.organic.heard = false;
    CHECK (play (p, { on (0, 57, 100) }).peak() < 1.0e-6f);
    p.organic.heard = true;
    // The arpeggiator's card lanes: a note for cards A and B only (channel 1 + 0b011) leaves C2 out;
    // one for card C alone (1 + 0b100) plays it.
    {
        auto s = makeSynth (p);
        s->setCardRouting (true);
        CHECK (render (*s, 0.5, { on (0, 57, 100, 1 + 3) }).peak() < 1.0e-6f);
    }
    {
        auto s = makeSynth (p);
        s->setCardRouting (true);
        CHECK (render (*s, 0.5, { on (0, 57, 100, 1 + 4) }).rms (0.1, 0.5) > 0.01);
    }
    // FOLLOW off: its zones play out after the key is released (a short tone, no loop).
    const auto shortTone = oneZone (440.0, 69.0, 0.6);
    p.hybrid2.type = HybridType::instrumentPatch;
    p.ampRelease = 0.01;
    p.organic.follow = false;
    const auto out = play (p, { on (0, 69, 100), off (2400, 69) }, 1.0, nullptr, &shortTone);
    CHECK (out.rms (0.3, 0.45) > 0.01);   // (still playing well after the key came up at 50 ms)
    CHECK (out.rms (0.85, 1.0) < 1.0e-4);   // (and then over)
}

TEST_CASE ("C2-02: replacing C2's source mid-chord: no crash, no click, the chord keeps sounding", "[hybrid2][v6]")
{
    Patch p = cardCOnly();   // (C1 empty: the steps measured are C2's own)
    p.hybrid2.enabled = true;
    p.hybrid2.type = HybridType::instrumentPatch;
    const auto a = oneZone (440.0, 69.0, 3.0), b = oneZone (330.0, 64.0, 3.0);
    auto s = makeSynth (p);
    s->setSampleSource2 (&a);
    const std::vector<Event> held { on (0, 57, 100), on (0, 64, 100) };
    const auto before = render (*s, 0.4, held);
    s->setSampleSource2 (&b);   // (mid-chord: the old zones fade from their tails)
    const auto after = render (*s, 0.4, {});
    CHECK (after.finite());
    auto largestStep = [] (const Stereo& x) {
        float m = 0.0f;
        for (std::size_t i = 1; i < x.left.size(); ++i)
            m = std::max (m, std::abs (x.left[i] - x.left[i - 1]));
        return m;
    };
    const float steady = largestStep (before);
    CHECK (largestStep (after) < 1.5f * steady);
    CHECK (std::abs (after.left.front() - before.left.back()) < 1.5f * steady);
    // The held keys keep sounding: the old zones' tails fade, the new source's start with the next
    // notes (C2's new source is heard on the next chord).
    const auto next = render (*s, 0.4, { off (0, 57), off (0, 64), on (2000, 57, 100), on (2000, 64, 100) });
    CHECK (next.rms (0.2, 0.4) > 0.01);
    // Removed (null) mid-chord: its zones fade, nothing stuck.
    s->setSampleSource2 (nullptr);
    const auto gone = render (*s, 0.4, { off (0, 57), off (0, 64) });
    CHECK (gone.finite());
    CHECK (gone.rms (0.3, 0.4) < 1.0e-4);
}

TEST_CASE ("MORPH-01: MORPH hears card C as a whole - C2 alone (C1 empty) is morphed", "[hybrid2][v6][morph]")
{
    Patch p = measurementPatch();
    p.osc1Level = 0.8;
    p.hybridType = HybridType::sample;   // (C1 empty)
    p.hybrid2.enabled = true;
    p.hybrid2.type = HybridType::oscillator;
    p.hybrid2.wave = Osc2Wave::triangle;
    p.hybrid2.octave = 1;
    p.morph.enabled = false;
    const auto dry = play (p, chord);
    p.morph.enabled = true;
    p.morph.position = 0.5;
    const auto morphed = play (p, chord);
    CHECK (morphed.finite());
    CHECK (maxDifference (dry, morphed) > 1.0e-3f);
    // C2 off with MORPH on: as before (C1 empty: nothing for MORPH to hear, the oscillators alone).
    Patch off = p;
    off.hybrid2.enabled = false;
    Patch plain = off;
    plain.morph.enabled = false;
    CHECK (maxDifference (play (off, chord), play (plain, chord)) == 0.0f);
}
