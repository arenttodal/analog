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
        p.aux.rhythmFree = true;  // (RHYTHM: GATE FREE, on the grid, unless a test plays it)
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

//==============================================================================
// AUX Addendum 03: D RHYTHM's PITCH / OCT (Patch::Aux::pitchSemis = PITCH + 12 x OCT).

namespace
{
    std::vector<double> tickStarts (const std::vector<float>& x, float threshold = 0.02f)
    {
        std::vector<double> starts;
        int quiet = 1 << 30;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            if (std::abs (x[i]) > threshold && quiet > static_cast<int> (0.04 * rate))
                starts.push_back (static_cast<double> (i) / rate);
            quiet = std::abs (x[i]) > threshold ? 0 : quiet + 1;
        }
        return starts;
    }

    /** The burst's frequency after `from` (s): its zero crossings while it is clearly sounding. */
    double burstHz (const std::vector<float>& x, double from, double seconds)
    {
        const auto a = static_cast<std::size_t> (from * rate), z = std::min (x.size(), static_cast<std::size_t> ((from + seconds) * rate));
        std::vector<double> crossings;
        for (auto i = a + 1; i < z; ++i)
            if ((x[i - 1] < 0.0f) != (x[i] < 0.0f) && std::max (std::abs (x[i - 1]), std::abs (x[i])) > 0.01f)
                crossings.push_back (static_cast<double> (i - 1) + x[i - 1] / (x[i - 1] - x[i]));
        if (crossings.size() < 3)
            return 0.0;
        return 0.5 * rate * static_cast<double> (crossings.size() - 1) / (crossings.back() - crossings.front());
    }
}

TEST_CASE ("AUX Addendum 03: RHYTHM PITCH / OCT move the pitch, never the tempo - the ticks on the same grid, the loop as long", "[aux][pitch]")
{
    std::vector<double> reference;
    for (const double semis : { 0.0, 12.0, -12.0, 7.0, 24.0 })
    {
        auto p = auxOnly (Mode::rhythm);
        p.tone = 1.0;   // (the FILTER open: the ticks' pitch is measured, not the filter's)
        p.aux.pitchSemis = semis;
        auto s = makeSynth (p);
        s->setAuxLoop (tickLoop().get());
        const auto out = play (*s, 4.2);
        const auto starts = tickStarts (out.left);
        INFO ("PITCH " << semis << " st: " << starts.size() << " ticks");
        REQUIRE (starts.size() >= 8);
        if (reference.empty())
            reference = starts;
        // The same grid: every tick where it was (the 120 BPM clock; 0.5 s apart), the loop
        // (8 beats) as long - the 9th tick is the loop's first again, 4 s later.
        for (std::size_t k = 0; k < 8; ++k)
            CHECK (starts[k] == Approx (reference[k]).margin (0.004));
        for (std::size_t k = 1; k < 8; ++k)
            CHECK (starts[k] - starts[k - 1] == Approx (0.5).margin (0.004));
        // The pitch: the 2 kHz tick at 2 kHz x 2^(semis / 12).
        const double want = 2000.0 * std::exp2 (semis / 12.0);
        const double hz = burstHz (out.left, starts[1], 0.004);
        INFO ("tick at " << hz << " Hz (want " << want << ")");
        CHECK (hz == Approx (want).epsilon (0.08));
    }
}

TEST_CASE ("AUX Addendum 03: RHYTHM's envelope keeps the loop's own timing whatever the pitch (the modulation's pulses never move)", "[aux][pitch]")
{
    // The player alone: its level output at pitch 1 and 2 (envelopeOnGrid), on a 120 BPM clock.
    auto levels = [] (double ratio) {
        rhythm::LoopPlayer player;
        player.prepare (rate);
        player.setLoop (tickLoop().get());
        rhythm::LoopPlayer::Settings st;
        st.sync = true;
        st.pitchRatio = ratio;
        st.envelopeOnGrid = true;
        const int total = static_cast<int> (2.0 * rate), block = 256;
        std::vector<float> l (static_cast<std::size_t> (total)), r (l.size()), level (l.size()), transient (l.size());
        const double perSample = 2.0 / rate;   // (120 BPM: 2 beats a second)
        for (int pos = 0; pos < total; pos += block)
            player.render (l.data() + pos, r.data() + pos, 1.0f, level.data() + pos, transient.data() + pos, std::min (block, total - pos),
                           pos * perSample, perSample, true, st);
        return level;
    };
    const auto plain = levels (1.0), raised = levels (2.0);
    // Each pulse's peak: at the same sample (within 1 ms), as tall.
    for (int beat = 1; beat < 4; ++beat)
    {
        const auto a = static_cast<std::size_t> ((beat * 0.5 - 0.01) * rate), z = static_cast<std::size_t> ((beat * 0.5 + 0.02) * rate);
        auto peakAt = [&] (const std::vector<float>& x) {
            return std::distance (x.begin(), std::max_element (x.begin() + static_cast<std::ptrdiff_t> (a), x.begin() + static_cast<std::ptrdiff_t> (z)));
        };
        INFO ("beat " << beat);
        CHECK (std::abs (peakAt (raised) - peakAt (plain)) <= static_cast<std::ptrdiff_t> (0.001 * rate));
        CHECK (raised[static_cast<std::size_t> (peakAt (raised))] == Approx (plain[static_cast<std::size_t> (peakAt (plain))]).epsilon (0.1));
    }
}

TEST_CASE ("AUX Addendum 03: automating PITCH / OCT is smooth - no spike, no stop, no jump of the grid", "[aux][pitch]")
{
    auto p = auxOnly (Mode::rhythm);
    auto s = makeSynth (p);
    s->setAuxLoop (tickLoop().get());
    const auto steady = play (*s, 3.0);
    // A sweep -12 .. +12 st over 3 s, one value a block, then a jump of an octave (OCT).
    auto t = makeSynth (p);
    t->setAuxLoop (tickLoop().get());
    Stereo out;
    const int block = 256, total = static_cast<int> (3.0 * rate);
    out.left.assign (static_cast<std::size_t> (total), 0.0f);
    out.right.assign (out.left.size(), 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        p.aux.pitchSemis = pos < total / 2 ? -12.0 + 24.0 * pos / (total / 2) : 12.0 - 12.0;   // (then OCT -1 from +12: a jump)
        t->setPatch (p);
        t->render (out.left.data() + pos, out.right.data() + pos, std::min (block, total - pos));
    }
    float peak = 0.0f, peakSteady = 0.0f;
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        REQUIRE (std::isfinite (out.left[i]));
        peak = std::max (peak, std::abs (out.left[i]));
        peakSteady = std::max (peakSteady, std::abs (steady.left[i]));
    }
    INFO ("peak " << peak << " (steady " << peakSteady << ")");
    CHECK (peak < 1.6f * peakSteady);   // (no spike: never far above the loop's own level)
    const auto starts = tickStarts (out.left), plainStarts = tickStarts (steady.left);
    REQUIRE (starts.size() >= 5);
    for (std::size_t k = 0; k < 5; ++k)
        CHECK (starts[k] == Approx (plainStarts[k]).margin (0.004));   // (the grid kept, never reset)
}

TEST_CASE ("AUX Addendum 03: with PITCH up, a host tempo change moves the ticks to the new grid at once - the pitch never moves the tempo", "[aux][pitch]")
{
    // The host's transport: 100 BPM for 2.4 s, then 140 BPM; D RHYTHM at +12 st.
    auto p = auxOnly (Mode::rhythm);
    p.tone = 1.0;
    p.aux.pitchSemis = 12.0;
    auto s = makeSynth (p);
    s->setAuxLoop (tickLoop().get());
    const int block = 256, total = static_cast<int> (4.8 * rate);
    std::vector<float> l (static_cast<std::size_t> (total)), r (l.size());
    double ppq = 0.0;
    for (int pos = 0; pos < total; pos += block)
    {
        osp::HostTiming t;
        t.valid = t.playing = true;
        t.bpm = pos < static_cast<int> (2.4 * rate) ? 100.0 : 140.0;
        t.ppq = ppq;
        s->setTransport (t);
        const int n = std::min (block, total - pos);
        s->render (l.data() + pos, r.data() + pos, n);
        ppq += n * t.bpm / 60.0 / rate;
    }
    const auto starts = tickStarts (l);
    REQUIRE (starts.size() >= 8);
    // Beats at 100 BPM (0.6 s apart), then at 140 (0.4286 s apart): the host's grid, not the pitch's.
    int slow = 0, fast = 0;
    for (std::size_t k = 1; k < starts.size(); ++k)
    {
        const double gap = starts[k] - starts[k - 1];
        if (starts[k] < 2.3)
            slow += std::abs (gap - 0.6) < 0.005 ? 1 : 0;
        else if (starts[k - 1] > 2.5)
            fast += std::abs (gap - 60.0 / 140.0) < 0.005 ? 1 : 0;
    }
    CHECK (slow >= 2);
    CHECK (fast >= 3);
    CHECK (burstHz (l, starts[1], 0.003) == Approx (4000.0).epsilon (0.08));
}

//==============================================================================
// D MORPH (docs/aux/MORPH_DECISION.md, Option D): the second MORPH stage, A/B/C x D.

namespace
{
    /** A saw on card A (a held C3) and D NOISE (FREE) together: the stage's two inputs. */
    Patch morphPatch (double morph)
    {
        Patch p = measurementPatch();
        p.aux.enabled = true;
        p.aux.mode = Mode::noise;
        p.aux.noiseFree = true;
        p.aux.levelDb = -6.0;
        p.aux.morph = morph;
        return p;
    }

    double rmsDiff (const std::vector<float>& a, const std::vector<float>& b, double from, double to)
    {
        double s = 0.0;
        const auto i0 = static_cast<std::size_t> (from * rate), i1 = std::min (a.size(), static_cast<std::size_t> (to * rate));
        for (auto i = i0; i < i1; ++i)
            s += std::pow (static_cast<double> (a[i]) - b[i], 2.0);
        return std::sqrt (s / static_cast<double> (std::max<std::size_t> (1, i1 - i0)));
    }
}

TEST_CASE ("D MORPH: at 0 % the layered sound bit for bit (the stage never runs); at 100 % D alone; at 50 % a hybrid of both", "[aux][morph]")
{
    const std::vector<Event> held { on (0, 48) };
    auto layered = makeSynth (morphPatch (0.0));
    const auto plain = play (*layered, 1.2, held);
    CHECK_FALSE (layered->auxMorphActive());
    // 0 %: exactly the layered instrument (D MORPH never touched it).
    Patch before = morphPatch (0.0);
    before.aux.morph = 0.0;
    CHECK (play (*makeSynth (before), 1.2, held).left == plain.left);

    // D alone (the cards silent): what 100 % should become.
    Patch dOnly = morphPatch (0.0);
    dOnly.osc1Level = dOnly.osc2Level = 0.0;
    dOnly.osc1Enabled = dOnly.osc2Enabled = false;
    const auto d = play (*makeSynth (dOnly), 1.2, held);
    auto full = makeSynth (morphPatch (1.0));
    const auto morphed = play (*full, 1.2, held);
    CHECK (full->auxMorphActive());
    const double toD = rmsDiff (morphed.left, d.left, 0.4, 1.2), dLevel = rmsOf (d.left, 0.4, 1.2);
    INFO ("100 %: " << toD << " from D alone (D at " << dLevel << ")");
    CHECK (toD < 0.1 * dLevel);
    // 50 %: neither the layered sound nor D - the cards' saw with D's spectrum (its noise's
    // colour) imprinted; finite, near the layered level.
    const auto half = play (*makeSynth (morphPatch (0.5)), 1.2, held);
    const double fromLayered = rmsDiff (half.left, plain.left, 0.4, 1.2), fromD = rmsDiff (half.left, d.left, 0.4, 1.2);
    INFO ("50 %: " << fromLayered << " from layered, " << fromD << " from D");
    CHECK (fromLayered > 0.05 * rmsOf (plain.left, 0.4, 1.2));
    CHECK (fromD > 0.05 * dLevel);
    for (const float x : half.left)
        REQUIRE (std::isfinite (x));
    CHECK (rmsOf (half.left, 0.4, 1.2) < 2.0 * rmsOf (plain.left, 0.4, 1.2));
}

TEST_CASE ("D MORPH: a silent D (off, muted, nothing loaded) never takes the cards away", "[aux][morph]")
{
    const std::vector<Event> held { on (0, 48) };
    Patch ref = measurementPatch();
    const auto cards = play (*makeSynth (ref), 1.0, held);
    // D on in RHYTHM with no loop (silent), D MORPH at 100 %: the cards as they are.
    Patch p = morphPatch (1.0);
    p.aux.mode = Mode::rhythm;
    const auto out = play (*makeSynth (p), 1.0, held);
    INFO ("difference " << rmsDiff (out.left, cards.left, 0.2, 1.0) << " of " << rmsOf (cards.left, 0.2, 1.0));
    CHECK (rmsDiff (out.left, cards.left, 0.2, 1.0) < 0.02 * rmsOf (cards.left, 0.2, 1.0));
    // MUTED D at 100 %: the same.
    Patch m = morphPatch (1.0);
    m.aux.heard = false;
    const auto muted = play (*makeSynth (m), 1.0, held);
    CHECK (rmsDiff (muted.left, cards.left, 0.3, 1.0) < 0.02 * rmsOf (cards.left, 0.3, 1.0));
}

TEST_CASE ("D MORPH: moved up and back to 0, it glides (no click) and then stops - the layered sound again", "[aux][morph]")
{
    auto p = morphPatch (0.0);
    auto s = makeSynth (p);
    const int block = 256, total = static_cast<int> (2.0 * rate);
    std::vector<float> l (static_cast<std::size_t> (total)), r (l.size());
    s->handleMidi (on (0, 48).bytes, 3);
    float worstStep = 0.0f;
    for (int pos = 0; pos < total; pos += block)
    {
        p.aux.morph = pos > static_cast<int> (0.4 * rate) && pos < static_cast<int> (1.0 * rate) ? 0.8 : 0.0;
        s->setPatch (p);
        s->render (l.data() + pos, r.data() + pos, std::min (block, total - pos));
    }
    for (std::size_t i = 1; i < l.size(); ++i)
        worstStep = std::max (worstStep, std::abs (l[i] - l[i - 1]));
    float peak = 0.0f;
    for (const float x : l)
        peak = std::max (peak, std::abs (x));
    INFO ("largest sample step " << worstStep << ", peak " << peak);
    CHECK (worstStep < 0.6f * peak + 0.05f);   // (noise and a saw: no step beyond their own)
    CHECK_FALSE (s->auxMorphActive());         // (back at 0: the stage stopped)
    CHECK (s->auxMorphPosition() == 0.0f);
}

//==============================================================================
// D RHYTHM's GATE (KEYS: a key starts the loop from its first beat), its RATE's direction and
// a recording with no beat grid.

namespace
{
    float peakIn (const std::vector<float>& x, double from, double to)
    {
        float m = 0.0f;
        for (auto i = static_cast<std::size_t> (from * rate); i < std::min (x.size(), static_cast<std::size_t> (to * rate)); ++i)
            m = std::max (m, std::abs (x[i]));
        return m;
    }

    bool tickNear (const std::vector<double>& starts, double at, double margin)
    {
        return std::any_of (starts.begin(), starts.end(), [&] (double s) { return std::abs (s - at) <= margin; });
    }
}

TEST_CASE ("D RHYTHM GATE KEYS: silent until a key, then the loop from its first beat; released it fades with the AMP's release; the next key starts it from the top", "[aux][rhythm][keys]")
{
    auto p = auxOnly (Mode::rhythm);
    p.tone = 1.0;
    p.aux.rhythmFree = false;   // (KEYS: the default)
    p.ampRelease = 0.1;
    auto s = makeSynth (p);
    s->setAuxLoop (tickLoop().get());
    // The host's transport stopped: the keys start it all the same, at the host's tempo (120).
    osp::HostTiming t;
    t.valid = true;
    t.playing = false;
    t.bpm = 120.0;
    s->setTransport (t);
    const auto out = play (*s, 4.4, { on (static_cast<int> (0.7 * rate), 60), off (static_cast<int> (2.0 * rate), 60),
                                      on (static_cast<int> (3.0 * rate), 64), off (static_cast<int> (3.9 * rate), 64) });
    // Nothing before the first key.
    CHECK (peakIn (out.left, 0.0, 0.7) == 0.0f);
    const auto starts = tickStarts (out.left);
    INFO ("ticks at " << (starts.empty() ? -1.0 : starts.front()));
    REQUIRE (starts.size() >= 4);
    // The first beat at the key (2 ms after it: the slice's pre-roll), then every half second
    // while it is held.
    CHECK (starts[0] == Approx (0.702).margin (0.002));
    CHECK (tickNear (starts, 1.202, 0.003));
    CHECK (tickNear (starts, 1.702, 0.003));
    // Released at 2.0 s: -60 dB at 2.1 s, silent well before the next key.
    CHECK (peakIn (out.left, 2.25, 2.99) < 1.0e-4f);
    // The next key: from the top again - its first tick is the loop's downbeat (the louder one).
    CHECK (tickNear (starts, 3.002, 0.002));
    CHECK (peakIn (out.left, 3.0, 3.03) > 1.3f * peakIn (out.left, 3.5, 3.53));
    CHECK (peakIn (out.left, 0.7, 0.73) > 1.3f * peakIn (out.left, 1.2, 1.23));
}

TEST_CASE ("D RHYTHM GATE KEYS: legato - a second key while one is held never restarts the loop", "[aux][rhythm][keys]")
{
    auto p = auxOnly (Mode::rhythm);
    p.tone = 1.0;
    p.aux.rhythmFree = false;
    auto s = makeSynth (p);
    s->setAuxLoop (tickLoop().get());
    const auto out = play (*s, 2.2, { on (static_cast<int> (0.5 * rate), 60), on (static_cast<int> (0.9 * rate), 64), off (static_cast<int> (1.2 * rate), 60) });
    const auto starts = tickStarts (out.left);
    REQUIRE (starts.size() >= 3);
    CHECK (starts[0] == Approx (0.502).margin (0.002));
    CHECK_FALSE (tickNear (starts, 0.9, 0.01));   // (no restart at the second key)
    CHECK (tickNear (starts, 1.002, 0.003));
    CHECK (tickNear (starts, 1.502, 0.003));      // (one key still held: it plays on)
}

TEST_CASE ("D RHYTHM RATE: 2x plays the loop twice as fast on the grid, 1/2x half as fast - the ticks' pitch untouched", "[aux][rhythm][rate]")
{
    for (const auto& [factor, gap] : { std::pair { 1.0, 0.5 }, std::pair { 2.0, 0.25 }, std::pair { 0.5, 1.0 } })
    {
        auto p = auxOnly (Mode::rhythm);   // (GATE FREE: the 120 BPM clock, standalone)
        p.tone = 1.0;
        p.aux.rate = factor;
        auto s = makeSynth (p);
        s->setAuxLoop (tickLoop().get());
        const auto out = play (*s, 4.2);
        const auto starts = tickStarts (out.left);
        INFO ("RATE " << factor << "x: " << starts.size() << " ticks");
        REQUIRE (starts.size() >= 4);
        for (std::size_t k = 1; k < 4; ++k)
            CHECK (starts[k] - starts[k - 1] == Approx (gap).margin (0.004));
        CHECK (burstHz (out.left, starts[1], 0.004) == Approx (2000.0).epsilon (0.08));
    }
}

TEST_CASE ("D RHYTHM: a recording with no beat grid found is still a loop (whole bars from its start); card C's reading is unchanged", "[aux][rhythm][assumed]")
{
    // Ticks at irregular times (no pulse to hear), 4 s long.
    std::vector<float> x (static_cast<std::size_t> (4.0 * rate), 0.0f);
    osp::Prng rng (11);
    for (double at = 0.0; at < 3.9;)
    {
        for (std::size_t i = 0; i < static_cast<std::size_t> (0.03 * rate) && static_cast<std::size_t> (at * rate) + i < x.size(); ++i)
        {
            const double tt = static_cast<double> (i) / rate;
            x[static_cast<std::size_t> (at * rate) + i] += static_cast<float> (0.6 * std::exp (-tt / 0.006) * std::sin (2.0 * std::numbers::pi * 2000.0 * tt));
        }
        at += 0.13 + 0.34 * rng.nextDouble();
    }
    const auto plain = rhythm::prepareLoop (x.data(), x.data(), x.size(), rate);
    REQUIRE (plain != nullptr);
    REQUIRE_FALSE (plain->rhythmic());   // (no pulse: card C plays it FREE, as before)
    const auto assumed = rhythm::prepareLoop (x.data(), x.data(), x.size(), rate, true);
    REQUIRE (assumed != nullptr);
    CHECK (assumed->rhythmic());
    CHECK (assumed->assumedGrid);
    CHECK (assumed->loopStart == 0.0);
    CHECK (assumed->loopFrames == Approx (static_cast<double> (x.size())).margin (2.0));
    const int b = assumed->analysis.beats;
    CHECK ((b == 1 || b == 2 || b == 4 || (b >= 8 && b % 4 == 0)));
    CHECK (assumed->analysis.bpm == Approx (60.0 * b / 4.0).epsilon (1.0e-6));
    // RATE moves it: at 1x it comes round once a loop (4 s of recording on b beats of the
    // 120 BPM clock); at 1/2x every tick lands twice as late (2x crowds its ticks closer than
    // this detector can part them).
    auto render = [&] (double factor, double seconds) {
        auto p = auxOnly (Mode::rhythm);
        p.tone = 1.0;
        p.aux.rate = factor;
        auto s = makeSynth (p);
        s->setAuxLoop (assumed.get());
        return play (*s, seconds).left;
    };
    const double loopSeconds = 60.0 * b / 120.0;
    const auto s1 = tickStarts (render (1.0, loopSeconds + 0.5)), sh = tickStarts (render (0.5, loopSeconds + 0.5));
    REQUIRE (s1.size() >= 6);
    REQUIRE (sh.size() >= 3);
    INFO ("beats " << b << ", loop " << loopSeconds << " s");
    CHECK (tickNear (s1, s1.front() + loopSeconds, 0.005));
    int checked = 0;
    for (std::size_t k = 1; k < s1.size() && 2.0 * (s1[k] - s1.front()) < loopSeconds; ++k, ++checked)
        CHECK (tickNear (sh, sh.front() + 2.0 * (s1[k] - s1.front()), 0.006));
    CHECK (checked >= 2);
}
