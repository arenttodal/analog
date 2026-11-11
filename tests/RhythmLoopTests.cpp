// RHYTHMIC MORPH, gate 5: card C's loop on the host's grid, its pitch and transients its own
// (docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md).

#include "rhythm/RhythmLoop.h"

#include "core/Prng.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <numbers>
#include <vector>

namespace rhythm = andor::analog::rhythm;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    /** Ticks (2 kHz, 3 ms) on every beat of `bars` bars at `bpm`, the downbeat louder. */
    std::vector<float> ticks (double bpm, int bars, double hz = 2000.0)
    {
        const double beat = 60.0 / bpm;
        const auto length = static_cast<std::size_t> (std::lround (bars * 4 * beat * rate));
        std::vector<float> x (length, 0.0f);
        for (int k = 0; k < bars * 4; ++k)
        {
            const auto first = static_cast<std::size_t> (std::lround (k * beat * rate));
            for (std::size_t i = 0; i < static_cast<std::size_t> (0.03 * rate) && first + i < length; ++i)
            {
                const double t = static_cast<double> (i) / rate;
                x[first + i] += static_cast<float> ((k % 4 == 0 ? 0.8 : 0.5) * std::exp (-t / 0.006) * std::sin (2.0 * std::numbers::pi * hz * t));
            }
        }
        return x;
    }

    struct Played
    {
        std::vector<float> left, right, level;
    };

    /** Plays a loop on a host: `seconds` at a tempo given per block (bpm (q) at the block's start). */
    template <typename TempoAt>
    Played play (const rhythm::PreparedLoop& loop, double seconds, TempoAt tempoAt, int block = 512, double startPpq = 0.0, rhythm::LoopPlayer::Settings settings = {})
    {
        rhythm::LoopPlayer player;
        player.prepare (rate);
        player.setLoop (&loop);
        Played out;
        const auto total = static_cast<int> (seconds * rate);
        out.left.assign (static_cast<std::size_t> (total), 0.0f);
        out.right.assign (static_cast<std::size_t> (total), 0.0f);
        out.level.assign (static_cast<std::size_t> (total), 0.0f);
        std::vector<float> transient (static_cast<std::size_t> (block));
        double q = startPpq;
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            const double dq = tempoAt (q) / 60.0 / rate;
            player.render (out.left.data() + pos, out.right.data() + pos, 1.0f, out.level.data() + pos, transient.data(), n, q, dq, true, settings);
            q += dq * n;
        }
        return out;
    }

    /** The samples where a tick starts (the first above a threshold after 40 ms of quiet). */
    std::vector<double> tickTimes (const std::vector<float>& x, float threshold = 0.05f)
    {
        std::vector<double> t;
        int quiet = 1 << 30;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            if (std::abs (x[i]) > threshold && quiet > static_cast<int> (0.04 * rate))
                t.push_back (static_cast<double> (i) / rate);
            quiet = std::abs (x[i]) > threshold ? 0 : quiet + 1;
        }
        return t;
    }

    /** The frequency of the tick starting at `t` (zero crossings over 2 ms). */
    double tickHz (const std::vector<float>& x, double t)
    {
        const auto a = static_cast<std::size_t> (t * rate) + 10, z = a + static_cast<std::size_t> (0.002 * rate);
        int crossings = 0;
        for (std::size_t i = a + 1; i < z && i < x.size(); ++i)
            crossings += (x[i - 1] < 0.0f) != (x[i] < 0.0f) ? 1 : 0;
        return 0.5 * crossings / 0.002;
    }
}

TEST_CASE ("RHYTHM LOOP: a 120 BPM tick loop on a host at 90, 120 and 140 - every tick on the host's beat, its pitch kept", "[rhythmloop][unit]")
{
    const auto source = ticks (120.0, 4);
    const auto loop = rhythm::prepareLoop (source.data(), source.data(), source.size(), rate);
    REQUIRE (loop != nullptr);
    REQUIRE (loop->rhythmic());
    CHECK (loop->analysis.beats == 16);
    for (const double bpm : { 90.0, 120.0, 140.0 })
    {
        INFO (bpm << " BPM");
        const auto out = play (*loop, 12.0, [bpm] (double) { return bpm; });
        const auto t = tickTimes (out.left);
        const double beat = 60.0 / bpm;
        REQUIRE (t.size() >= 16);
        for (std::size_t k = 0; k < t.size(); ++k)
        {
            INFO ("tick " << k);
            CHECK (t[k] == Approx (static_cast<double> (k) * beat).margin (0.0005));   // (within half a millisecond)
            if (t[k] < 11.9)
                CHECK (tickHz (out.left, t[k]) == Approx (2000.0).epsilon (0.03));        // (pitch: unchanged)
        }
    }
}

TEST_CASE ("RHYTHM LOOP: ten minutes at 137 BPM - no drift", "[rhythmloop][unit]")
{
    const auto source = ticks (120.0, 2);
    const auto loop = rhythm::prepareLoop (source.data(), source.data(), source.size(), rate);
    REQUIRE (loop->rhythmic());
    const double bpm = 137.0;
    const auto out = play (*loop, 600.0, [bpm] (double) { return bpm; }, 1024);
    const auto t = tickTimes (out.left);
    REQUIRE (t.size() > 1300);
    const double beat = 60.0 / bpm;
    const double last = t.back();
    CHECK (std::abs (last / beat - std::round (last / beat)) * beat < 0.0005);
}

TEST_CASE ("RHYTHM LOOP: a tempo ramp 90 -> 120 -> 140, a host loop jumping back, and a seek - always on the grid", "[rhythmloop][unit]")
{
    const auto source = ticks (120.0, 4);
    const auto loop = rhythm::prepareLoop (source.data(), source.data(), source.size(), rate);
    REQUIRE (loop->rhythmic());
    // A ramp, block by block: each tick where the host's quarter note is a whole number.
    {
        rhythm::LoopPlayer player;
        player.prepare (rate);
        player.setLoop (loop.get());
        std::vector<float> l (512), r (512), lev (512), tr (512);
        double q = 0.0;
        std::vector<double> qAtTick;
        int quiet = 1 << 30;
        for (int b = 0; b < 4000; ++b)
        {
            const double bpm = q < 16.0 ? 90.0 : (q < 32.0 ? 90.0 + 30.0 * (q - 16.0) / 16.0 : (q < 48.0 ? 120.0 + 20.0 * (q - 32.0) / 16.0 : 140.0));
            const double dq = bpm / 60.0 / rate;
            std::fill (l.begin(), l.end(), 0.0f);
            player.render (l.data(), r.data(), 1.0f, lev.data(), tr.data(), 512, q, dq, true, {});
            for (int i = 0; i < 512; ++i)
            {
                if (std::abs (l[static_cast<std::size_t> (i)]) > 0.05f && quiet > static_cast<int> (0.04 * rate))
                    qAtTick.push_back (q + i * dq);
                quiet = std::abs (l[static_cast<std::size_t> (i)]) > 0.05f ? 0 : quiet + 1;
            }
            q += 512 * dq;
        }
        REQUIRE (qAtTick.size() > 50);
        for (const double at : qAtTick)
            CHECK (std::abs (at - std::round (at)) < 0.002);   // (a 500th of a beat: ~1 ms)
    }
    // The host's loop: bars 1-2 again and again (ppq jumping from 8 back to 0); then a seek.
    {
        rhythm::LoopPlayer player;
        player.prepare (rate);
        player.setLoop (loop.get());
        std::vector<float> l (480), r (480), lev (480), tr (480);
        const double dq = 120.0 / 60.0 / rate;
        double q = 0.0;
        int ticksSeen = 0, offGrid = 0, quiet = 1 << 30;
        for (int b = 0; b < 1200; ++b)
        {
            if (b == 900)
                q = 5.0;   // (a seek)
            std::fill (l.begin(), l.end(), 0.0f);
            player.render (l.data(), r.data(), 1.0f, lev.data(), tr.data(), 480, q, dq, true, {});
            for (int i = 0; i < 480; ++i)
            {
                if (std::abs (l[static_cast<std::size_t> (i)]) > 0.05f && quiet > static_cast<int> (0.04 * rate))
                {
                    ++ticksSeen;
                    const double at = q + i * dq;
                    offGrid += std::abs (at - std::round (at)) > 0.002 ? 1 : 0;
                }
                quiet = std::abs (l[static_cast<std::size_t> (i)]) > 0.05f ? 0 : quiet + 1;
            }
            q += 480 * dq;
            if (q >= 8.0 && b < 900)
                q -= 8.0;   // (the loop's turn)
        }
        CHECK (ticksSeen >= 24);   // (12 s at 120: 24 beats, each one heard through the loop's turns and the seek)
        CHECK (offGrid == 0);
    }
}

TEST_CASE ("RHYTHM LOOP: the same sound whatever the host's blocks", "[rhythmloop][unit]")
{
    const auto source = ticks (110.0, 2);
    const auto loop = rhythm::prepareLoop (source.data(), source.data(), source.size(), rate);
    REQUIRE (loop->rhythmic());
    const auto big = play (*loop, 4.0, [] (double) { return 133.0; }, 512);
    for (const int block : { 32, 64, 333 })
    {
        const auto small = play (*loop, 4.0, [] (double) { return 133.0; }, block);
        float d = 0.0f;
        for (std::size_t i = 0; i < big.left.size(); ++i)
            d = std::max (d, std::abs (big.left[i] - small.left[i]));
        INFO ("block " << block);
        CHECK (d < 1.0e-5f);
    }
}

TEST_CASE ("RHYTHM LOOP: FREE (an unsure source, or SYNC off) - its own speed, round and round; the envelope with it", "[rhythmloop][unit]")
{
    // Rain-like: no pulse.
    osp::Prng rng (9);
    std::vector<float> rain (static_cast<std::size_t> (2.0 * rate));
    float lp = 0.0f;
    for (auto& v : rain)
    {
        lp += 0.1f * (static_cast<float> (rng.bipolar()) - lp);
        v = 0.3f * lp;
    }
    const auto loop = rhythm::prepareLoop (rain.data(), rain.data(), rain.size(), rate);
    REQUIRE_FALSE (loop->rhythmic());
    const auto out = play (*loop, 5.0, [] (double) { return 128.0; });
    // Its own samples (after the first 3 ms fade-in), whatever the host's tempo.
    for (std::size_t i = 200; i < 2000; ++i)
        REQUIRE (out.left[i] == Approx (rain[i]).margin (1.0e-6));
    // Round its end, without a hole.
    double quietest = 1.0;
    for (std::size_t i = static_cast<std::size_t> (1.9 * rate); i < static_cast<std::size_t> (2.1 * rate); i += 480)
    {
        double e = 0.0;
        for (std::size_t k = i; k < i + 480; ++k)
            e += static_cast<double> (out.left[k]) * out.left[k];
        quietest = std::min (quietest, std::sqrt (e / 480.0));
    }
    CHECK (quietest > 0.01);
    // The level envelope follows what plays (never zero while it sounds).
    CHECK (out.level[static_cast<std::size_t> (rate)] > 0.1f);
    // SYNC off on a rhythmic loop: FREE as well (the ticks at 120, the host at 90).
    const auto source = ticks (120.0, 2);
    const auto tickLoop = rhythm::prepareLoop (source.data(), source.data(), source.size(), rate);
    rhythm::LoopPlayer::Settings off;
    off.sync = false;
    const auto free = play (*tickLoop, 3.0, [] (double) { return 90.0; }, 512, 0.0, off);
    const auto t = tickTimes (free.left);
    REQUIRE (t.size() >= 5);
    CHECK (t[1] - t[0] == Approx (0.5).margin (0.001));   // (its own beat: 120 BPM)
}

TEST_CASE ("RHYTHM LOOP: the envelope and the sound are one - the level rises with each tick", "[rhythmloop][unit]")
{
    const auto source = ticks (120.0, 4);
    const auto loop = rhythm::prepareLoop (source.data(), source.data(), source.size(), rate);
    const auto out = play (*loop, 6.0, [] (double) { return 100.0; });
    const auto t = tickTimes (out.left);
    REQUIRE (t.size() > 6);
    for (std::size_t k = 1; k < 6; ++k)
    {
        // The level within 10 ms after the tick is high; 150 ms after, it has fallen.
        const auto at = static_cast<std::size_t> (t[k] * rate);
        float peak = 0.0f;
        for (std::size_t i = at; i < at + static_cast<std::size_t> (0.01 * rate); ++i)
            peak = std::max (peak, out.level[i]);
        CHECK (peak > 0.3f);
        CHECK (out.level[at + static_cast<std::size_t> (0.15 * rate)] < 0.5f * peak);
    }
}

TEST_CASE ("RHYTHM LOOP: a new recording mid-play, none at all, stopped - no click, no NaN", "[rhythmloop][unit]")
{
    const auto a = ticks (120.0, 2), b = ticks (95.0, 2, 1200.0);
    const auto loopA = rhythm::prepareLoop (a.data(), a.data(), a.size(), rate);
    const auto loopB = rhythm::prepareLoop (b.data(), b.data(), b.size(), rate);
    rhythm::LoopPlayer player;
    player.prepare (rate);
    player.setLoop (loopA.get());
    std::vector<float> l (256), r (256), lev (256), tr (256);
    double q = 0.0;
    const double dq = 2.0 / rate;
    float largestStep = 0.0f, previous = 0.0f;
    for (int blk = 0; blk < 600; ++blk)
    {
        if (blk == 150)
            player.setLoop (loopB.get());
        if (blk == 300)
            player.setLoop (nullptr);
        if (blk == 350)
            player.setLoop (loopA.get());
        const bool running = blk < 450 || blk > 500;
        std::fill (l.begin(), l.end(), 0.0f);
        player.render (l.data(), r.data(), 1.0f, lev.data(), tr.data(), 256, q, dq, running, {});
        for (const float v : l)
        {
            REQUIRE (std::isfinite (v));
            largestStep = std::max (largestStep, std::abs (v - previous));
            previous = v;
        }
        q += running ? 256 * dq : 0.0;
    }
    CHECK (largestStep < 0.9f);   // (the ticks' own steepest edge, never a jump of the whole level)
}

