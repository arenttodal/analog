// The MOD WHEEL's VIBRATO in ANDOR ANALOG (ANDOR OSP's shared osp::vibrato, per voice): off -
// and on with nothing to play - is the old sound sample for sample; on, each note's oscillators
// swing by DEPTH at RATE after DELAY; only the sources it was given (OSC 1, OSC 2, card C, SUB);
// its routes (VIBRATO DEPTH / AMOUNT); the classic WHEEL-to-LFO link untouched beside it.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace testutil;
using Catch::Approx;

namespace
{
    bool same (const Stereo& a, const Stereo& b)
    {
        return a.left == b.left && a.right == b.right;
    }

    osp::vibrato::Settings vibratoOn (osp::vibrato::Control control = osp::vibrato::Control::alwaysOn)
    {
        osp::vibrato::Settings v;
        v.enabled = true;
        v.depthCents = 50.0;
        v.rateHz = 5.0;
        v.delaySeconds = 0.25;
        v.riseSeconds = 0.25;
        v.control = control;
        return v;
    }

    Stereo play (const Patch& p, std::vector<Event> events, double seconds = 1.5, const osp::mod::Settings* routes = nullptr)
    {
        auto s = makeSynth (p);
        if (routes != nullptr)
            s->setModulation (*routes, { 2, 11, 16, 17 });
        return render (*s, seconds, std::move (events));
    }

    /** The cents between the lowest and highest local frequency (15 ms windows). */
    double swingCents (const Stereo& s, double from, double to)
    {
        const auto mono = s.mono();
        double lo = 1.0e9, hi = 0.0;
        for (double t = from; t + 0.015 <= to; t += 0.005)
        {
            const double hz = frequencyByCrossings (mono, s.rate, t, t + 0.015);
            if (hz <= 0.0)
                continue;
            lo = std::min (lo, hz);
            hi = std::max (hi, hz);
        }
        return 1200.0 * std::log2 (hi / lo);
    }
}

TEST_CASE ("vibrato: off, and on with nothing to play, is every patch's old sound", "[vibrato]")
{
    auto p = measurementPatch();
    const std::vector<Event> note { on (0, 57, 100) };
    const auto plain = play (p, note);
    CHECK (Patch().vibrato.enabled == false);   // (every older patch and preset: off)
    // MOD WHEEL with the wheel down, ASSIGNED with AMOUNT 0, no source taking part: untouched.
    p.vibrato = vibratoOn (osp::vibrato::Control::modWheel);
    CHECK (same (play (p, note), plain));
    p.vibrato = vibratoOn (osp::vibrato::Control::assigned);
    CHECK (same (play (p, note), plain));
    p.vibrato = vibratoOn();
    p.vibrato.sources = { false, true, true, false };   // (OSC 1 alone sounds here)
    CHECK (same (play (p, note), plain));
}

TEST_CASE ("vibrato: each note's oscillators swing by DEPTH at RATE after DELAY", "[vibrato]")
{
    auto p = measurementPatch();
    p.vibrato = vibratoOn();
    const auto moving = play (p, { on (0, 57, 100) });
    CHECK (swingCents (moving, 0.03, 0.24) < 4.0);   // DELAY: steady
    const double swing = swingCents (moving, 0.6, 1.45);
    INFO ("swing " << swing << " cents");
    CHECK (swing == Approx (100.0).margin (12.0));   // +-50 cents
    CHECK (frequencyByCrossings (moving.mono(), moving.rate, 0.6, 1.4) == Approx (220.0).epsilon (0.01));   // centred on A3
    CHECK (same (play (p, { on (0, 57, 100) }), moving));   // deterministic

    // MOD WHEEL: CC 1 up plays it; the wheel's own WHEEL-to-LFO link still does what it did.
    p.vibrato = vibratoOn (osp::vibrato::Control::modWheel);
    const auto wheel = play (p, { cc (0, 1, 127), on (0, 57, 100) });
    CHECK (swingCents (wheel, 0.6, 1.45) == Approx (100.0).margin (12.0));
    auto linked = p;
    linked.wheelToLfo = true;
    linked.lfoAmount = 0.0;
    CHECK (! same (play (linked, { cc (0, 1, 127), on (0, 57, 100) }), wheel));   // (the LFO's depths join in)

    // ASSIGNED: a route to VIBRATO AMOUNT (per voice: VEL) plays it.
    p.vibrato = vibratoOn (osp::vibrato::Control::assigned);
    osp::mod::Settings velocity;
    velocity.routes[0] = { osp::mod::Source::vel, osp::mod::Dest::vibratoAmount, 1.0, true };
    CHECK (swingCents (play (p, { on (0, 57, 127) }, 1.5, &velocity), 0.6, 1.45) > 70.0);
}

TEST_CASE ("vibrato: SUB takes part only when chosen", "[vibrato][sub]")
{
    auto p = measurementPatch();
    p.osc1Level = 0.0;   // (SUB alone)
    p.sub.enabled = true;
    p.sub.levelDb = -12.0;
    auto renderSub = [] (const Patch& patch) {
        auto s = makeSynth (patch, 48000.0, 4, 256);
        Stereo out;
        out.rate = s->sampleRate();
        const auto total = static_cast<int> (1.5 * out.rate);
        out.left.assign (static_cast<std::size_t> (total), 0.0f);
        out.right.assign (static_cast<std::size_t> (total), 0.0f);
        const std::uint8_t noteOn[3] { 0x90, 60, 100 };
        s->handleSubMidi (noteOn, 3);
        s->handleMidi (noteOn, 3);
        for (int pos = 0; pos < total; pos += 256)
            s->render (out.left.data() + pos, out.right.data() + pos, std::min (256, total - pos));
        return out;
    };
    const auto plain = renderSub (p);
    p.vibrato = vibratoOn();   // (sources: OSC 1, OSC 2 and C - SUB off by default)
    CHECK (same (renderSub (p), plain));
    p.vibrato.sources[3] = true;
    const auto swung = renderSub (p);
    CHECK (! same (swung, plain));
    // C2 (65.4 Hz) swinging +-50 cents (wider windows for the low note).
    const auto mono = swung.mono();
    double lo = 1.0e9, hi = 0.0;
    for (double t = 0.6; t + 0.05 <= 1.45; t += 0.01)
    {
        const double hz = frequencyByCrossings (mono, swung.rate, t, t + 0.05);
        if (hz > 0.0)
        {
            lo = std::min (lo, hz);
            hi = std::max (hi, hz);
        }
    }
    INFO ("SUB " << lo << " .. " << hi);
    CHECK (1200.0 * std::log2 (hi / lo) > 50.0);
}

TEST_CASE ("vibrato: listening demos (ANALOG_VIBRATO_DEMO_DIR)", "[vibrato][demo]")
{
    // The owner's listening test: OSC 1, OSC 2 alone of the pair, card C as OSC 3, the wheel
    // (CC 1) bringing it in over a held note, and the default patch untouched.
    const char* dir = std::getenv ("ANALOG_VIBRATO_DEMO_DIR");
    if (dir == nullptr)
        return;
    auto writeWav = [dir] (const char* name, const Stereo& s) {
        std::FILE* f = std::fopen ((std::string (dir) + "/" + name).c_str(), "wb");
        REQUIRE (f != nullptr);
        auto u32 = [f] (std::uint32_t v) { std::fwrite (&v, 4, 1, f); };
        auto u16 = [f] (std::uint16_t v) { std::fwrite (&v, 2, 1, f); };
        const auto bytes = static_cast<std::uint32_t> (s.left.size() * 4);
        const auto sr = static_cast<std::uint32_t> (s.rate);
        std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
        u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16);
        std::fwrite ("data", 1, 4, f); u32 (bytes);
        for (std::size_t i = 0; i < s.left.size(); ++i)
            for (const float x : { s.left[i], s.right[i] })
                u16 (static_cast<std::uint16_t> (static_cast<std::int16_t> (std::clamp (x * 2.0f, -1.0f, 1.0f) * 32767.0f)));
        std::fclose (f);
    };
    auto vib = vibratoOn();
    vib.depthCents = 25.0;
    vib.rateHz = 5.5;
    const std::vector<Event> held { on (0, 57, 100), off (static_cast<int> (3.4 * 48000.0), 57) };

    auto p = measurementPatch();
    p.tone = 0.7;
    writeWav ("analog-osc1-no-vibrato.wav", play (p, held, 4.0));
    p.vibrato = vib;
    writeWav ("analog-osc1-vibrato.wav", play (p, held, 4.0));

    // OSC 1 and OSC 2 together, VIBRATO on OSC 2 only: one steady, one singing.
    auto duo = measurementPatch();
    duo.tone = 0.7;
    duo.osc2Level = 1.0;
    duo.blend = 0.5;
    duo.osc2Coarse = 7.0;
    duo.vibrato = vib;
    duo.vibrato.sources = { false, true, false, false };
    writeWav ("analog-osc2-only-vibrato.wav", play (duo, held, 4.0));

    // Card C as OSC 3 (pitched C), the oscillators off.
    auto c = measurementPatch();
    c.tone = 0.7;
    c.osc1Enabled = c.osc2Enabled = false;
    c.hybridType = HybridType::oscillator;
    c.vibrato = vib;
    writeWav ("analog-hybrid-c-vibrato.wav", play (c, held, 4.0));

    // MOD WHEEL: CC 1 rising from 0 to full over three seconds, a note held throughout.
    auto wheel = measurementPatch();
    wheel.tone = 0.7;
    wheel.vibrato = vib;
    wheel.vibrato.control = osp::vibrato::Control::modWheel;
    wheel.vibrato.delaySeconds = 0.0;
    std::vector<Event> ramp { cc (0, 1, 0), on (0, 57, 100) };
    for (int k = 1; k <= 60; ++k)
        ramp.push_back (cc (k * 2400, 1, std::min (127, k * 127 / 60)));
    ramp.push_back (off (static_cast<int> (3.6 * 48000.0), 57));
    writeWav ("analog-cc1-wheel-ramp.wav", play (wheel, ramp, 4.2));

    // The default patch (every older preset's case: VIBRATO off) - identical with or without it.
    Patch defaults;
    const auto plain = play (defaults, held, 4.0);
    CHECK (same (plain, play (defaults, held, 4.0)));
    writeWav ("analog-default-patch-unchanged.wav", plain);
}
