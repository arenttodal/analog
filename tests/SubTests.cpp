// Cinematic V6 gate 2: SUB, an independent polyphonic sub oscillator on the musician's own keys
// (the MIDI before the arpeggiator), joined after the effects (docs/CINEMATIC_V6_GATE2.md).
// SUB-01..05 of the spec; RT-01's SUB part is in RealtimeTests.cpp.

#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace testutil;

namespace
{
    /** Renders with two streams, as the plugin does: `notes` (what the voices play, the
        arpeggiator's when it runs) and `played` (the musician's own keys, SUB's). */
    Stereo renderBoth (Synth& synth, double seconds, std::vector<Event> notes, std::vector<Event> played, int block = 256)
    {
        struct Tagged
        {
            Event e;
            bool sub;
        };
        std::vector<Tagged> all;
        for (const auto& e : played)
            all.push_back ({ e, true });
        for (const auto& e : notes)
            all.push_back ({ e, false });
        std::stable_sort (all.begin(), all.end(), [] (const Tagged& a, const Tagged& b) { return a.e.sample < b.e.sample; });
        Stereo out;
        out.rate = synth.sampleRate();
        const auto total = static_cast<int> (seconds * out.rate);
        out.left.assign (static_cast<std::size_t> (total), 0.0f);
        out.right.assign (static_cast<std::size_t> (total), 0.0f);
        std::size_t next = 0;
        for (int pos = 0; pos < total;)
        {
            const int end = std::min (total, pos + block);
            int at = pos;
            while (at < end)
            {
                for (; next < all.size() && all[next].e.sample <= at; ++next)
                    (synth.*(all[next].sub ? &Synth::handleSubMidi : &Synth::handleMidi)) (all[next].e.bytes, all[next].e.size);
                int until = end;
                if (next < all.size() && all[next].e.sample < until)
                    until = all[next].e.sample;
                synth.render (out.left.data() + at, out.right.data() + at, until - at);
                at = until;
            }
            pos = end;
        }
        return out;
    }

    Patch withSub (Patch p)
    {
        p.sub.enabled = true;
        p.sub.levelDb = -12.0;
        return p;
    }

    float maxDifference (const Stereo& a, const Stereo& b, std::size_t from = 0)
    {
        float d = 0.0f;
        for (std::size_t i = from; i < a.left.size(); ++i)
            d = std::max ({ d, std::abs (a.left[i] - b.left[i]), std::abs (a.right[i] - b.right[i]) });
        return d;
    }

    /** The largest step between neighbouring samples (a click shows as a jump). */
    float largestStep (const Stereo& s, std::size_t from, std::size_t to)
    {
        float m = 0.0f;
        for (std::size_t i = std::max<std::size_t> (from, 1); i < std::min (to, s.left.size()); ++i)
            m = std::max (m, std::abs (s.left[i] - s.left[i - 1]));
        return m;
    }

    // The chord the musician holds (C3 E3 G3, in SUB's default range) and an arpeggio over it.
    const std::vector<Event> chord { on (0, 48, 100), on (0, 52, 100), on (0, 55, 100), off (36000, 48), off (36000, 52), off (36000, 55) };
    std::vector<Event> arpeggio()
    {
        std::vector<Event> e;
        const int notes[] { 60, 64, 67, 72 };
        for (int step = 0; step < 16; ++step)
        {
            e.push_back (on (step * 2400, notes[step % 4], 90));
            e.push_back (off (step * 2400 + 1800, notes[step % 4]));
        }
        return e;
    }
}

TEST_CASE ("SUB-01: SUB sustains the chord the musician holds, not the arpeggiator's notes", "[sub][v6]")
{
    auto sOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& s = *sOwned;
    s.prepare (48000.0, 256, 4);
    s.setPatch (withSub (measurementPatch()));
    s.reset();
    // Half a second in: the arpeggio moves, the chord is held.
    renderBoth (s, 0.5, arpeggio(), chord);
    CHECK (s.sub().activeVoices() == 3);
    std::vector<int> keys;
    for (int i = 0; i < SubOsc::maxVoices; ++i)
        if (s.sub().voiceKey (i) >= 0)
            keys.push_back (s.sub().voiceKey (i));
    std::sort (keys.begin(), keys.end());
    CHECK (keys == std::vector<int> { 48, 52, 55 });

    // SUB alone (the voices given nothing): a steady chord, at the chord's pitch two octaves
    // down (C1 is 32.7 Hz: the lowest note's crossings).
    auto aloneOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& alone = *aloneOwned;
    alone.prepare (48000.0, 256, 4);
    auto p = withSub (measurementPatch());
    alone.setPatch (p);
    alone.reset();
    const auto out = renderBoth (alone, 0.7, {}, chord);
    CHECK (out.rms (0.1, 0.35) > 0.02);
    CHECK (std::abs (out.rms (0.1, 0.35) - out.rms (0.4, 0.65)) < 0.15 * out.rms (0.1, 0.35));
    // A single key: its pitch.
    auto oneOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& one = *oneOwned;
    one.prepare (48000.0, 256, 4);
    one.setPatch (p);
    one.reset();
    const auto c = renderBoth (one, 0.8, {}, { on (0, 60, 100) });
    CHECK (std::abs (frequencyByCrossings (c.mono(), 48000.0, 0.2, 0.8) - 65.406) < 0.5);   // C4 two octaves down: C2
}

TEST_CASE ("SUB-01: the keys released, SUB's voices end; the pedal holds them; ALL NOTES OFF and ALL SOUND OFF stop them", "[sub][v6]")
{
    SubOsc sub;
    sub.prepare (48000.0);
    SubSettings set;
    set.enabled = true;
    sub.setSettings (set);
    std::vector<float> l (4800), r (4800);
    auto midi = [&sub] (std::uint8_t a, std::uint8_t b, std::uint8_t c) {
        const std::uint8_t m[3] { a, b, c };
        sub.handleMidi (m, 3);
    };
    midi (0x90, 60, 100);
    midi (0x90, 64, 100);
    sub.render (l.data(), r.data(), 4800);
    CHECK (sub.activeVoices() == 2);
    midi (0xb0, 64, 127);   // pedal down
    midi (0x80, 60, 0);
    sub.render (l.data(), r.data(), 4800);
    CHECK (sub.activeVoices() == 2);
    midi (0xb0, 64, 0);   // pedal up: 60 fades
    CHECK (sub.activeVoices() == 1);
    for (int i = 0; i < 10; ++i)
        sub.render (l.data(), r.data(), 4800);
    CHECK (sub.voiceKey (0) != 60);
    midi (0xb0, 123, 0);   // ALL NOTES OFF: a release
    CHECK (sub.activeVoices() == 0);
    for (int i = 0; i < 10; ++i)
        sub.render (l.data(), r.data(), 4800);
    CHECK (! sub.sounding());

    midi (0x90, 50, 100);
    sub.render (l.data(), r.data(), 4800);
    midi (0xb0, 120, 0);   // ALL SOUND OFF: 5 ms
    sub.render (l.data(), r.data(), 960);
    CHECK (sub.activeVoices() == 0);
    sub.render (l.data(), r.data(), 4800);
    CHECK (! sub.sounding());

    // A repeated key (two note ons, one off) is still held.
    midi (0x90, 55, 100);
    midi (0x90, 55, 100);
    midi (0x80, 55, 0);
    CHECK (sub.activeVoices() == 1);
    midi (0x80, 55, 0);
    CHECK (sub.activeVoices() == 0);
}

TEST_CASE ("SUB-02: dragging the range while keys are held: keys join and leave smoothly, none stuck", "[sub][v6]")
{
    auto sOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& s = *sOwned;
    s.prepare (48000.0, 256, 4);
    auto p = withSub (measurementPatch());
    p.sub.lowKey = 48;
    p.sub.highKey = 84;
    s.setPatch (p);
    s.reset();
    const std::vector<Event> held { on (0, 40, 100), on (0, 60, 100), on (0, 90, 100) };
    renderBoth (s, 0.3, {}, held);
    CHECK (s.sub().activeVoices() == 1);   // (60 only: 40 and 90 outside C3..C6)

    // Widen: 40 and 90 join (an attack each, no click).
    p.sub.lowKey = 36;
    p.sub.highKey = 96;
    s.setPatch (p);
    CHECK (s.sub().activeVoices() == 3);
    auto wide = renderBoth (s, 0.3, {}, {});
    CHECK (wide.finite());
    CHECK (largestStep (wide, 0, wide.left.size()) < 0.02f);

    // Narrow past every key: they all fade, nothing is stuck.
    p.sub.lowKey = 100;
    p.sub.highKey = 110;
    s.setPatch (p);
    CHECK (s.sub().activeVoices() == 0);
    auto narrow = renderBoth (s, 1.0, {}, {});
    CHECK (largestStep (narrow, 0, narrow.left.size()) < 0.02f);
    CHECK (! s.sub().sounding());

    // And back: the still-held keys return.
    p.sub.lowKey = 36;
    p.sub.highKey = 96;
    s.setPatch (p);
    CHECK (s.sub().activeVoices() == 3);
    // Released while out of range: nothing returns.
    renderBoth (s, 0.1, {}, { off (0, 40), off (0, 60), off (0, 90) });
    p.sub.lowKey = 0;
    p.sub.highKey = 127;
    s.setPatch (p);
    CHECK (s.sub().activeVoices() == 0);
}

TEST_CASE ("SUB-02: turning SUB on while keys are held starts them; off fades them", "[sub][v6]")
{
    auto sOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
    Synth& s = *sOwned;
    s.prepare (48000.0, 256, 4);
    auto p = measurementPatch();
    s.setPatch (p);
    s.reset();
    renderBoth (s, 0.2, {}, { on (0, 50, 100), on (0, 57, 100) });
    CHECK (s.sub().activeVoices() == 0);
    s.setPatch (withSub (p));
    CHECK (s.sub().activeVoices() == 2);
    const auto on = renderBoth (s, 0.2, {}, {});
    CHECK (on.rms (0.1, 0.2) > 0.01);
    s.setPatch (p);
    CHECK (s.sub().activeVoices() == 0);
    const auto off = renderBoth (s, 1.0, {}, {});
    CHECK (largestStep (off, 0, off.left.size()) < 0.02f);
    CHECK (! s.sub().sounding());
}

TEST_CASE ("SUB-03: FLAT and TAPER shape the level against the key; every wave and GRIT stays finite and bounded", "[sub][v6]")
{
    SubOsc sub;
    sub.prepare (48000.0);
    SubSettings set;
    set.enabled = true;
    set.levelDb = -12.0;
    set.taper = false;
    sub.setSettings (set);
    const float flat60 = sub.keyGain (60), flat84 = sub.keyGain (84);
    CHECK (std::abs (flat60 - flat84) < 1.0e-6f);
    CHECK (sub.keyGain (47) == 0.0f);   // (out of range)
    CHECK (sub.keyGain (85) == 0.0f);
    set.taper = true;
    sub.setSettings (set);
    // TAPER from C4 at 6 dB/oct: C4 itself and below unchanged, C6 12 dB down, C5 6 dB.
    CHECK (std::abs (sub.keyGain (60) - flat60) < 1.0e-6f);
    CHECK (std::abs (sub.keyGain (50) - flat60) < 1.0e-6f);
    CHECK (std::abs (20.0 * std::log10 (sub.keyGain (72) / flat60) + 6.0) < 1.0e-3);
    CHECK (std::abs (20.0 * std::log10 (sub.keyGain (84) / flat60) + 12.0) < 1.0e-3);
    for (int k = 61; k <= 84; ++k)
        CHECK (sub.keyGain (k) < sub.keyGain (k - 1));

    for (const auto wave : { SubSettings::Wave::sine, SubSettings::Wave::triangle, SubSettings::Wave::square, SubSettings::Wave::saw })
        for (const double grit : { 0.0, 0.5, 1.0 })
            for (const int octave : { -1, -2 })
            {
                INFO ("wave " << static_cast<int> (wave) << " grit " << grit << " octave " << octave);
                auto sOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
                Synth& s = *sOwned;
                s.prepare (48000.0, 256, 4);
                auto p = withSub (measurementPatch());
                p.sub.wave = wave;
                p.sub.grit = grit;
                p.sub.octave = octave;
                p.sub.levelDb = 0.0;   // (the loudest it goes)
                s.setPatch (p);
                s.reset();
                // Every key in range at once (eight voices: the newest win), and a bend.
                std::vector<Event> keys;
                for (int k = 48; k <= 84; k += 3)
                    keys.push_back (on (0, k, 127));
                keys.push_back (bendTo (12000, 16383));
                const auto out = renderBoth (s, 0.6, {}, keys);
                CHECK (out.finite());
                CHECK (out.peak() <= 1.0f);
                CHECK (out.rms (0.1, 0.6) > 0.01);
            }
}

TEST_CASE ("SUB-03: the waves differ, and GRIT adds harmonics", "[sub][v6]")
{
    auto play = [] (SubSettings::Wave wave, double grit) {
        auto sOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
        Synth& s = *sOwned;
        s.prepare (48000.0, 256, 4);
        auto p = withSub (measurementPatch());
        p.sub.wave = wave;
        p.sub.grit = grit;
        s.setPatch (p);
        s.reset();
        return renderBoth (s, 0.4, {}, { on (0, 60, 127) });
    };
    const auto sine = play (SubSettings::Wave::sine, 0.0);
    CHECK (maxDifference (sine, play (SubSettings::Wave::triangle, 0.0)) > 1.0e-3f);
    CHECK (maxDifference (sine, play (SubSettings::Wave::square, 0.0)) > 1.0e-3f);
    CHECK (maxDifference (sine, play (SubSettings::Wave::saw, 0.0)) > 1.0e-3f);
    // GRIT on a sine: the steepest slope grows (new upper harmonics).
    CHECK (largestStep (play (SubSettings::Wave::sine, 1.0), 4800, 19200) > 1.2f * largestStep (sine, 4800, 19200));
}

TEST_CASE ("SUB-04: the oscillators silent, SUB played: no ECHO or SPACE, even at full wet and feedback", "[sub][v6]")
{
    auto play = [] (bool wet) {
        auto sOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
        Synth& s = *sOwned;
        s.prepare (48000.0, 256, 4);
        auto p = withSub (measurementPatch());
        p.osc1Level = p.osc2Level = 0.0;
        if (wet)
        {
            p.space = 1.0;
            p.echo = 1.0;
            p.effects.echoFeedback = 0.95;
            p.drive = 1.0;
            p.motion = 1.0;
        }
        s.setPatch (p);
        s.reset();
        // The voices are given notes too (muted oscillators): only SUB is heard.
        return renderBoth (s, 2.0, { on (0, 60, 100), off (24000, 60) }, { on (0, 48, 100), on (0, 55, 100), off (24000, 48), off (24000, 55) });
    };
    const auto dry = play (false), wet = play (true);
    CHECK (dry.rms (0.1, 0.4) > 0.01);
    CHECK (maxDifference (dry, wet) < 1.0e-5f);
    // After the release, the tail is SUB's own 220 ms and nothing more: no echo, no room.
    CHECK (wet.rms (1.2, 2.0) < 1.0e-5);
}

TEST_CASE ("SUB-05: CLASSIC with SUB on: SUB is added dry, the old signal is untouched; SUB off renders exactly as before", "[sub][v6]")
{
    auto patch = [] {
        Patch p = measurementPatch();
        p.space = 0.6;
        p.echo = 0.5;
        p.osc1Level = 0.4;
        return p;
    };
    const std::vector<Event> notes { on (0, 60, 100), on (6000, 64, 90), off (30000, 60), off (30000, 64) };
    const std::vector<Event> played { on (0, 48, 100), on (6000, 55, 90), off (30000, 48), off (30000, 55) };
    auto play = [&] (const Patch& p, bool withNotes) {
        auto sOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
        Synth& s = *sOwned;
        s.prepare (48000.0, 256, 4);
        s.setPatch (p);
        s.reset();
        return renderBoth (s, 1.5, withNotes ? notes : std::vector<Event> {}, played);
    };
    const auto old = play (patch(), true);
    // SUB off: the musician's keys reach it, nothing changes (bit for bit the voices alone).
    {
        auto sOwned = std::make_unique<Synth>();   // (on the heap: a Synth is larger than some platforms' stacks)
        Synth& s = *sOwned;
        s.prepare (48000.0, 256, 4);
        s.setPatch (patch());
        s.reset();
        CHECK (maxDifference (old, render (s, 1.5, notes)) == 0.0f);
    }
    // SUB on: what it adds is exactly SUB alone (after every effect, before a linear output).
    auto p = patch();
    p.sub.enabled = true;
    const auto both = play (p, true);
    const auto subAlone = play (p, false);
    CHECK (subAlone.rms (0.1, 0.5) > 0.005);
    float worst = 0.0f;
    for (std::size_t i = 0; i < both.left.size(); ++i)
        worst = std::max (worst, std::abs (both.left[i] - old.left[i] - subAlone.left[i]));
    CHECK (worst < 2.0e-6f);
}

TEST_CASE ("SUB: the on-screen pitch wheel bends SUB as MIDI's does", "[sub][v6]")
{
    auto owned = std::make_unique<Synth>();
    Synth& s = *owned;
    s.prepare (48000.0, 256, 4);
    auto p = withSub (measurementPatch());
    p.bendRangeSemitones = 12.0;
    s.setPatch (p);
    s.reset();
    s.setPitchBend (1.0f);   // (an octave up, no MIDI)
    const auto c = renderBoth (s, 0.8, {}, { on (0, 60, 100) });
    CHECK (std::abs (frequencyByCrossings (c.mono(), 48000.0, 0.2, 0.8) - 130.81) < 1.0);   // C2 an octave up: C3
}
