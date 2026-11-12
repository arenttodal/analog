// D / AUX (docs/aux), gates 1-5 in the synth: its own stereo bus beside the voices, through its
// FILTER (following the cards'), into the shared chorus / KALEIDOSCOPE / FX route.

#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace testutil;
using Catch::Approx;
using Mode = Patch::Aux::Mode;
namespace rhythm = andor::analog::rhythm;

namespace
{
    constexpr double rate = 48000.0;

    /** The instrument with A / B / C silent and every effect off: what is heard is D. */
    Patch auxOnly (Mode mode)
    {
        Patch p = measurementPatch();
        p.osc1Level = p.osc2Level = 0.0;
        p.osc1Enabled = p.osc2Enabled = false;
        p.aux.enabled = true;
        p.aux.mode = mode;
        p.aux.levelDb = 0.0;
        p.aux.noiseFree = true;   // (NOISE: always sounding, unless a test plays it)
        return p;
    }

    /** Renders, giving the played keys to D as well (the engine's played-key stream). */
    Stereo play (Synth& s, double seconds, std::vector<Event> events = {}, int block = 256)
    {
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });
        Stereo out;
        out.rate = rate;
        const auto total = static_cast<int> (seconds * rate);
        out.left.assign (static_cast<std::size_t> (total), 0.0f);
        out.right.assign (static_cast<std::size_t> (total), 0.0f);
        std::size_t next = 0;
        for (int pos = 0; pos < total;)
        {
            const int end = std::min (total, pos + block);
            int at = pos;
            while (at < end)
            {
                while (next < events.size() && events[next].sample <= at)
                {
                    s.handleMidi (events[next].bytes, events[next].size);
                    s.auxKeyEvent (events[next].bytes, events[next].size);
                    ++next;
                }
                int until = end;
                if (next < events.size() && events[next].sample < until)
                    until = events[next].sample;
                s.render (out.left.data() + at, out.right.data() + at, until - at);
                at = until;
            }
            pos = end;
        }
        return out;
    }

    double rmsOf (const std::vector<float>& x, double from, double to)
    {
        double s = 0.0;
        const auto a = static_cast<std::size_t> (from * rate), z = std::min (x.size(), static_cast<std::size_t> (to * rate));
        for (auto i = a; i < z; ++i)
            s += static_cast<double> (x[i]) * x[i];
        return std::sqrt (s / static_cast<double> (std::max<std::size_t> (1, z - a)));
    }

    /** Brightness: the first difference's level against the signal's. */
    double brightness (const std::vector<float>& x, double from, double to)
    {
        double s = 0.0, d = 0.0;
        for (auto i = std::max<std::size_t> (1, static_cast<std::size_t> (from * rate)); i < std::min (x.size(), static_cast<std::size_t> (to * rate)); ++i)
        {
            s += static_cast<double> (x[i]) * x[i];
            const double dx = static_cast<double> (x[i]) - x[i - 1];
            d += dx * dx;
        }
        return std::sqrt (d / std::max (1.0e-30, s));
    }

    float largestStep (const std::vector<float>& x, double from, double to)
    {
        float m = 0.0f;
        for (auto i = std::max<std::size_t> (1, static_cast<std::size_t> (from * rate)); i < std::min (x.size(), static_cast<std::size_t> (to * rate)); ++i)
            m = std::max (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    /** Ticks (2 kHz, 3 ms decay) on every beat at 120 BPM, 2 bars: a loop with a clear pulse. */
    const std::shared_ptr<const rhythm::PreparedLoop>& tickLoop()
    {
        static const auto loop = [] {
            const double beat = 0.5;
            std::vector<float> x (static_cast<std::size_t> (8 * beat * rate), 0.0f);
            for (int k = 0; k < 8; ++k)
                for (std::size_t i = 0; i < static_cast<std::size_t> (0.03 * rate); ++i)
                {
                    const double t = static_cast<double> (i) / rate;
                    x[static_cast<std::size_t> (k * beat * rate) + i] += static_cast<float> ((k % 4 == 0 ? 0.8 : 0.5) * std::exp (-t / 0.006) * std::sin (2.0 * std::numbers::pi * 2000.0 * t));
                }
            return rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
        }();
        return loop;
    }
}

TEST_CASE ("AUX G1: off (every older patch) is the instrument as before, bit for bit - its settings untouched or moved", "[aux][g1]")
{
    Patch p = measurementPatch();
    const std::vector<Event> notes { on (0, 57), on (9000, 64, 90), off (30000, 57), off (36000, 64) };
    const auto plain = render (*makeSynth (p), 1.0, notes);
    Patch moved = p;
    moved.aux.mode = Mode::noise;     // (every D setting away from its default, D off)
    moved.aux.levelDb = 3.0;
    moved.aux.noiseFree = true;
    moved.aux.modAmount = 1.0;
    moved.aux.filterFollow = false;
    auto s = makeSynth (moved);
    s->setAuxLoop (tickLoop().get());
    const auto out = play (*s, 1.0, notes);
    CHECK (out.left == plain.left);
    CHECK (out.right == plain.right);
    CHECK_FALSE (s->auxActive());
}

TEST_CASE ("AUX G1: POWER, MUTE and LEVEL ramp (no click); powered off it stops and the instrument is as before", "[aux][g1]")
{
    auto p = auxOnly (Mode::noise);
    auto s = makeSynth (p);
    const auto on1 = play (*s, 0.4);
    CHECK (rmsOf (on1.left, 0.1, 0.4) > 0.01);
    CHECK (rmsOf (on1.left, 0.0, 0.002) < 0.5 * rmsOf (on1.left, 0.1, 0.4));   // (powered: it fades in, 10 ms)
    // MUTE: silent within 10 ms, smoothly.
    p.aux.heard = false;
    s->setPatch (p);
    const auto muted = play (*s, 0.3);
    CHECK (rmsOf (muted.left, 0.05, 0.3) < 0.002 * rmsOf (on1.left, 0.1, 0.4));   // (only the shared effects' last tail)
    // POWER off: faded and stopped - the bus no longer runs.
    p.aux.heard = true;
    p.aux.enabled = false;
    s->setPatch (p);
    play (*s, 0.1);
    CHECK_FALSE (s->auxActive());
    // LEVEL: -6 dB is half.
    auto loud = auxOnly (Mode::noise), half = loud;
    half.aux.levelDb = -6.0206;
    const double rl = rmsOf (play (*makeSynth (loud), 0.6).left, 0.2, 0.6), rh = rmsOf (play (*makeSynth (half), 0.6).left, 0.2, 0.6);
    CHECK (rh / rl == Approx (0.5).epsilon (0.01));
}

TEST_CASE ("AUX G2: the cards' FILTER shapes D in every mode - TONE, RESONANCE and the model; FOLLOW off bypasses it", "[aux][g2][filter]")
{
    for (const auto mode : { Mode::noise, Mode::rhythm, Mode::kontakt })
    {
        INFO ("mode " << static_cast<int> (mode));
        // A broadband D: white noise; RHYTHM's ticks; KONTAKT's bus fed with noise.
        std::vector<float> busL (static_cast<std::size_t> (rate)), busR (busL.size());
        osp::Prng rng (3);
        for (std::size_t i = 0; i < busL.size(); ++i)
            busL[i] = busR[i] = static_cast<float> (0.3 * rng.bipolar());
        auto run = [&] (double tone, double resonance, FilterModel model, bool follow) {
            auto p = auxOnly (mode);
            p.tone = tone;
            p.resonance = resonance;
            p.filterModel = model;
            p.aux.filterFollow = follow;
            auto s = makeSynth (p);
            s->setAuxLoop (tickLoop().get());
            Stereo out;
            out.left.assign (static_cast<std::size_t> (0.8 * rate), 0.0f);
            out.right = out.left;
            for (std::size_t at = 0; at < out.left.size(); at += 256)
            {
                if (mode == Mode::kontakt)
                    s->setAuxExternal (busL.data() + at, busR.data() + at, 256, 0);
                s->render (out.left.data() + at, out.right.data() + at, 256);
            }
            return out.left;
        };
        const auto open = run (1.0, 0.0, FilterModel::ladder, true), closed = run (0.3, 0.0, FilterModel::ladder, true), bypassed = run (0.3, 0.0, FilterModel::ladder, false);
        const double bOpen = brightness (open, 0.2, 0.8), bClosed = brightness (closed, 0.2, 0.8), bBypass = brightness (bypassed, 0.2, 0.8);
        INFO ("brightness: open " << bOpen << ", TONE 30 % " << bClosed << ", FOLLOW off " << bBypass);
        CHECK (rmsOf (open, 0.2, 0.8) > 0.005);
        CHECK (bClosed < 0.5 * bOpen);          // (the big FILTER macro darkens D)
        CHECK (bBypass > 0.8 * bOpen);          // (FOLLOW off: D as it is)
        const auto resonant = run (0.5, 0.9, FilterModel::ladder, true), plain = run (0.5, 0.0, FilterModel::ladder, true), ota = run (0.5, 0.9, FilterModel::ota, true);
        double d1 = 0.0, d2 = 0.0, r = 0.0;
        for (std::size_t i = static_cast<std::size_t> (0.2 * rate); i < plain.size(); ++i)
        {
            d1 += std::pow (resonant[i] - plain[i], 2.0);
            d2 += std::pow (resonant[i] - ota[i], 2.0);
            r += std::pow (plain[i], 2.0);
        }
        CHECK (std::sqrt (d1 / r) > 0.1);   // (RESONANCE)
        CHECK (std::sqrt (d2 / r) > 0.1);   // (LADDER / OTA)
    }
}

TEST_CASE ("AUX G2: D goes through the shared effects - SPACE's tail and ECHO's repeats carry it; DRIVE colours it", "[aux][g2][fx]")
{
    // D NOISE played by one short note (its gate closes at 0.2 s + its release).
    auto base = auxOnly (Mode::noise);
    base.aux.noiseFree = false;
    base.aux.noiseRelease = 0.02;
    const std::vector<Event> note { on (0, 60), off (static_cast<int> (0.2 * rate), 60) };
    auto dry = base, wet = base, echo = base, drive = base;
    wet.space = 0.8;
    echo.echo = 0.8;
    drive.drive = 0.9;
    const auto a = play (*makeSynth (dry), 1.2, note), b = play (*makeSynth (wet), 1.2, note), c = play (*makeSynth (echo), 1.2, note), d = play (*makeSynth (drive), 1.2, note);
    INFO ("after the gate: dry " << rmsOf (a.left, 0.4, 1.0) << ", SPACE " << rmsOf (b.left, 0.4, 1.0) << ", ECHO " << rmsOf (c.left, 0.4, 1.0));
    CHECK (rmsOf (a.left, 0.4, 1.0) < 1.0e-4);         // (D alone: gone with its gate)
    CHECK (rmsOf (b.left, 0.4, 1.0) > 1.0e-3);         // (SPACE: its tail)
    CHECK (rmsOf (c.left, 0.4, 1.0) > 1.0e-3);         // (ECHO: its repeats)
    double diff = 0.0;
    for (std::size_t i = 0; i < static_cast<std::size_t> (0.2 * rate); ++i)
        diff += std::abs (d.left[i] - a.left[i]);
    CHECK (diff > 1.0);                                  // (DRIVE)
}

TEST_CASE ("AUX G3: RHYTHM - the loop on the grid, heard; its envelope moves FILTER (MOD AMOUNT), and keeps moving it with D muted", "[aux][g3][rhythm]")
{
    auto p = auxOnly (Mode::rhythm);
    auto s = makeSynth (p);
    s->setAuxLoop (tickLoop().get());
    const auto out = play (*s, 2.2);
    // Ticks every half second (the free clock at 120 BPM, standalone: no host transport).
    std::vector<double> starts;
    int quiet = 1 << 30;
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        if (std::abs (out.left[i]) > 0.05f && quiet > static_cast<int> (0.04 * rate))
            starts.push_back (static_cast<double> (i) / rate);
        quiet = std::abs (out.left[i]) > 0.05f ? 0 : quiet + 1;
    }
    REQUIRE (starts.size() >= 4);
    for (std::size_t k = 1; k < 4; ++k)
        CHECK (starts[k] - starts[k - 1] == Approx (0.5).margin (0.003));
    // Its envelope onto FILTER: an A saw (the cards' filter half closed) brightens with each tick.
    auto withSaw = [&] (double amount, bool heard) {
        Patch q = measurementPatch();
        q.tone = 0.35;
        q.aux.enabled = true;
        q.aux.mode = Mode::rhythm;
        q.aux.heard = heard;
        q.aux.modAmount = amount;
        auto t = makeSynth (q);
        t->setAuxLoop (tickLoop().get());
        return play (*t, 2.0, { on (0, 48) }).left;
    };
    const auto none = withSaw (0.0, false), moved = withSaw (1.0, false);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = static_cast<std::size_t> (0.5 * rate); i < none.size(); ++i)
    {
        diff += std::pow (moved[i] - none[i], 2.0);
        ref += std::pow (none[i], 2.0);
    }
    INFO ("D muted, MOD AMOUNT 100 % onto FILTER: the saw changes by " << std::sqrt (diff / ref));
    CHECK (std::sqrt (diff / ref) > 0.05);   // (MUTE silences D's audio, never its envelope)
    // POWER off: neither its audio nor its envelope.
    Patch off = measurementPatch();
    off.tone = 0.35;
    off.aux.mode = Mode::rhythm;
    off.aux.modAmount = 1.0;
    auto t = makeSynth (off);
    t->setAuxLoop (tickLoop().get());
    Patch ref2 = measurementPatch();
    ref2.tone = 0.35;
    CHECK (play (*t, 1.0, { on (0, 48) }).left == play (*makeSynth (ref2), 1.0, { on (0, 48) }).left);
}

TEST_CASE ("AUX G4: NOISE - played by the keys (a gate, legato one gate), FREE always; WHITE / PINK / BROWN differ", "[aux][g4][noise]")
{
    auto p = auxOnly (Mode::noise);
    p.aux.noiseFree = false;
    p.aux.noiseRelease = 0.05;
    auto s = makeSynth (p);
    const auto out = play (*s, 1.6, { on (static_cast<int> (0.3 * rate), 60), on (static_cast<int> (0.6 * rate), 64), off (static_cast<int> (0.7 * rate), 60),
                                       off (static_cast<int> (1.0 * rate), 64) });
    CHECK (rmsOf (out.left, 0.0, 0.29) == 0.0);           // (no key: silence, as an instrument)
    CHECK (rmsOf (out.left, 0.35, 0.95) > 0.01);           // (held, through the legato overlap)
    CHECK (rmsOf (out.left, 1.2, 1.6) < 1.0e-5);           // (released)
    // The colours: brightness falls WHITE > PINK > BROWN.
    std::array<double, 3> bright {};
    for (int c = 0; c < 3; ++c)
    {
        auto q = auxOnly (Mode::noise);
        q.aux.noiseColor = c;
        q.aux.noiseTone = 1.0;
        q.aux.filterFollow = false;
        bright[static_cast<std::size_t> (c)] = brightness (play (*makeSynth (q), 0.6).left, 0.1, 0.6);
    }
    INFO ("brightness WHITE " << bright[0] << ", PINK " << bright[1] << ", BROWN " << bright[2]);
    CHECK (bright[0] > 1.5 * bright[1]);
    CHECK (bright[1] > 1.5 * bright[2]);
    // Deterministic (the instrument's seed).
    CHECK (play (*makeSynth (auxOnly (Mode::noise)), 0.3).left == play (*makeSynth (auxOnly (Mode::noise)), 0.3).left);
}

TEST_CASE ("AUX G5: KONTAKT's bus is heard as given; its reported latency delays the voices to meet it", "[aux][g5][kontakt]")
{
    // D: an impulse at 0.1 s reported 256 samples late; A: a note at 0 (its own onset).
    std::vector<float> busL (static_cast<std::size_t> (rate), 0.0f), busR (busL.size(), 0.0f);
    busL[static_cast<std::size_t> (0.1 * rate) + 256] = busR[static_cast<std::size_t> (0.1 * rate) + 256] = 1.0f;
    Patch p = measurementPatch();
    p.aux.enabled = true;
    p.aux.mode = Mode::kontakt;
    p.aux.levelDb = 0.0;
    p.aux.filterFollow = false;
    auto s = makeSynth (p);
    s->handleMidi (on (0, 60).bytes, 3);
    Stereo out;
    out.left.assign (static_cast<std::size_t> (96 * 256), 0.0f);   // (whole blocks)
    out.right = out.left;
    for (std::size_t at = 0; at < out.left.size(); at += 256)
    {
        s->setAuxExternal (busL.data() + at, busR.data() + at, 256, 256);
        s->render (out.left.data() + at, out.right.data() + at, 256);
    }
    CHECK (s->auxLatency() == 256);
    // The voice's first sound: 256 samples later than without D (it waited).
    Patch plain = measurementPatch();
    const auto ref = render (*makeSynth (plain), 96 * 256 / rate, { on (0, 60) });
    auto onset = [] (const std::vector<float>& x) {
        for (std::size_t i = 0; i < x.size(); ++i)
            if (std::abs (x[i]) > 1.0e-4f)
                return static_cast<int> (i);
        return -1;
    };
    CHECK (onset (out.left) == onset (ref.left) + 256);
}
