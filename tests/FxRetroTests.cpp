// RETRO (V2): every personality measured - the handoff's retro-* acceptance tests that a
// headless build can prove (docs/FX_EFFECTS_LIBRARY.md).

#include "fx/Effects.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <set>

using namespace andor::analog;
using namespace andor::analog::fx;
using Catch::Approx;

namespace
{
    constexpr double rate = 48000.0;

    struct Buffer
    {
        std::vector<float> l, r;
        explicit Buffer (int n = 0) : l (static_cast<std::size_t> (n)), r (static_cast<std::size_t> (n)) {}
        int size() const { return static_cast<int> (l.size()); }
    };

    Buffer sine (double hz, double seconds, float amp = 0.5f)
    {
        Buffer b (static_cast<int> (seconds * rate));
        for (int i = 0; i < b.size(); ++i)
            b.l[static_cast<std::size_t> (i)] = b.r[static_cast<std::size_t> (i)] = amp * static_cast<float> (std::sin (2.0 * dsp::pi * hz * i / rate));
        return b;
    }

    Buffer noise (double seconds, float amp = 0.3f, std::uint64_t seed = 7)
    {
        osp::Prng prng (seed);
        Buffer b (static_cast<int> (seconds * rate));
        for (int i = 0; i < b.size(); ++i)
        {
            b.l[static_cast<std::size_t> (i)] = amp * static_cast<float> (prng.bipolar());
            b.r[static_cast<std::size_t> (i)] = amp * static_cast<float> (prng.bipolar());
        }
        return b;
    }

    void run (Processor& p, Buffer& b, int block = 256)
    {
        for (int i = 0; i < b.size(); i += block)
            p.process (b.l.data() + i, b.r.data() + i, std::min (block, b.size() - i));
    }

    double rms (const std::vector<float>& x, int from = 0, int to = -1)
    {
        to = to < 0 ? static_cast<int> (x.size()) : to;
        double s = 0.0;
        for (int i = from; i < to; ++i)
            s += static_cast<double> (x[static_cast<std::size_t> (i)]) * x[static_cast<std::size_t> (i)];
        return std::sqrt (s / std::max (1, to - from));
    }

    double db (double x) { return 20.0 * std::log10 (std::max (1.0e-12, x)); }

    double toneAt (const std::vector<float>& x, double hz, int from, int to)
    {
        const double w = 2.0 * dsp::pi * hz / rate, c = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = from; i < to; ++i)
        {
            const double s0 = x[static_cast<std::size_t> (i)] + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return 2.0 * std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (to - from);
    }

    bool finite (const Buffer& b, float bound = 4.0f)
    {
        for (int i = 0; i < b.size(); ++i)
            for (const float v : { b.l[static_cast<std::size_t> (i)], b.r[static_cast<std::size_t> (i)] })
                if (! std::isfinite (v) || std::abs (v) > bound)
                    return false;
        return true;
    }

    /** A RETRO with its settings (slot 0), prepared and reset as the engine does. */
    std::unique_ptr<Retro> retro (const RetroSettings& r, double sampleRate = rate)
    {
        Settings s;
        s.retro[0] = r;
        auto p = std::make_unique<Retro>();
        p->prepare (sampleRate, 512);
        p->set (s, 0);
        p->reset();
        return p;
    }

    /** A plain machine: no wow, flutter, drift, saturation, character or wear. */
    RetroSettings still (int mode)
    {
        RetroSettings r;
        r.mode = mode;
        r.mix = 1.0f;
        r.character = r.wear = r.saturation = r.wowDepth = r.flutterDepth = r.randomness = r.headBump = 0.0f;
        return r;
    }
}

TEST_CASE ("retro: the macros offset the user's settings and never switch on what is off", "[fx][retro]")
{
    RetroSettings r;
    r.compAmount = 0.0f;
    r.character = 1.0f;
    r.wear = 1.0f;
    const auto d = deriveRetro (r);
    CHECK (d.compAmount == 0.0f);                 // CHARACTER never switches the compressor on
    CHECK (d.saturation > r.saturation);
    CHECK (d.wowDepth > r.wowDepth);
    CHECK (d.highHz < r.highHz);
    r.wear = 0.4f;
    CHECK (deriveRetro (r).dropoutRate == 0.0f);   // dropouts only past half WEAR (or by hand)
    r.character = 0.0f;
    r.wear = 0.0f;
    const auto plain = deriveRetro (r);
    CHECK (plain.saturation == r.saturation);
    CHECK (plain.wowDepth == r.wowDepth);
    CHECK (plain.highHz == r.highHz);
    CHECK (plain.codecLoss == r.codecLoss);
    CHECK (plain.codecSpeed == r.codecSpeed);
    r.character = r.wear = 1.0f;
    CHECK (deriveRetro (r).codecLoss > r.codecLoss);   // CHARACTER and WEAR are heard in CODEC too
    CHECK (deriveRetro (r).codecSpeed > r.codecSpeed);
    r.wear = 0.0f;
    r.compAmount = 0.3f;
    r.character = 1.0f;
    CHECK (deriveRetro (r).compAmount == Approx (0.6f));
}

TEST_CASE ("retro-latency: one codec frame in every personality; MIX 0 is the dry signal, aligned, exactly", "[fx][retro]")
{
    for (const double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        INFO (sr);
        for (int mode = 0; mode < 3; ++mode)
        {
            auto p = retro (still (mode), sr);
            CHECK (p->latency() == Retro::frameFor (sr));
        }
    }
    CHECK (Retro::frameFor (48000.0) == 1024);
    CHECK (Retro::frameFor (96000.0) == 2048);
    for (int personality = 0; personality < 4; ++personality)
    {
        INFO (personality);
        auto r = still (std::min (personality, 2));
        if (personality == 3)
            r.era = RetroSettings::codec;
        r.mix = 0.0f;
        r.noiseDb = -40.0f;   // (the noise is the machine's: MIX 0 has none)
        auto p = retro (r);
        const auto in = noise (0.5);
        auto b = in;
        run (*p, b);
        const int L = p->latency();
        bool exact = true;
        for (int i = 0; i + L < b.size(); ++i)
            exact = exact && b.l[static_cast<std::size_t> (i + L)] == in.l[static_cast<std::size_t> (i)] && b.r[static_cast<std::size_t> (i + L)] == in.r[static_cast<std::size_t> (i)];
        CHECK (exact);
    }
}

TEST_CASE ("retro-codec: at LOSS 0 the STFT reconstructs the input after its frame; MIX 50 % is not comb-filtered", "[fx][retro][codec]")
{
    auto r = still (RetroSettings::digital);
    r.era = RetroSettings::codec;
    r.codecLoss = 0.0f;
    auto p = retro (r);
    const auto in = sine (1000.0, 0.5);
    auto b = in;
    run (*p, b);
    const int L = p->latency();
    double err = 0.0, sig = 0.0;
    for (int i = 4 * L; i + L < b.size(); ++i)
    {
        const double e = b.l[static_cast<std::size_t> (i + L)] - in.l[static_cast<std::size_t> (i)];
        err += e * e;
        sig += static_cast<double> (in.l[static_cast<std::size_t> (i)]) * in.l[static_cast<std::size_t> (i)];
    }
    CHECK (10.0 * std::log10 (err / sig) < -60.0);   // (perfect reconstruction, to float precision)
    // Wet and dry aligned: half and half is the tone at full level (no comb).
    r.mix = 0.5f;
    auto half = retro (r);
    auto h = sine (3000.0, 0.5);
    run (*half, h);
    CHECK (toneAt (h.l, 3000.0, 8 * L, h.size()) == Approx (0.5).margin (0.01));
}

TEST_CASE ("retro-noise-idle: no input and the noise off is silence; with noise on, AUTO MUTE fades it when idle", "[fx][retro]")
{
    for (int mode = 0; mode < 3; ++mode)
    {
        INFO (mode);
        RetroSettings r;   // (as inserted: CHARACTER, WEAR, wow, flutter on - noise off)
        r.mode = mode;
        auto p = retro (r);
        Buffer b (static_cast<int> (rate));
        run (*p, b);
        CHECK (rms (b.l) == 0.0);
        CHECK (rms (b.r) == 0.0);
    }
    RetroSettings r;
    r.noiseDb = -40.0f;
    r.noiseAutoMute = false;
    auto on = retro (r);
    Buffer quiet (static_cast<int> (rate));
    run (*on, quiet);
    CHECK (db (rms (quiet.l, quiet.size() / 2)) > -70.0);   // heard (MIX 65 % of -40 dB, shaped)
    r.noiseAutoMute = true;
    auto muted = retro (r);
    Buffer idle (static_cast<int> (rate));
    run (*muted, idle);
    CHECK (rms (idle.l, idle.size() / 2) == 0.0);           // gone after 250 ms + its fade
}

TEST_CASE ("retro-tape: the medium's response (highs lost, head bump), its harmonics, everything finite", "[fx][retro][tape]")
{
    auto r = still (RetroSettings::tape);
    r.highHz = 8000.0f;
    auto p = retro (r);
    auto lo = sine (1000.0, 0.4), hi = sine (12000.0, 0.4);
    run (*p, lo);
    p->reset();
    run (*p, hi);
    const int from = 4800;
    CHECK (db (toneAt (hi.l, 12000.0, from, hi.size())) < db (toneAt (lo.l, 1000.0, from, lo.size())) - 6.0);
    CHECK (Retro::responseDb (r, rate, 12000.0) < -6.0);
    // Head bump: on the reel's 45 Hz, up with HEAD BUMP.
    r.headBump = 1.0f;
    r.tapeModel = 1;
    CHECK (Retro::responseDb (r, rate, 45.0) > Retro::responseDb (r, rate, 400.0) + 4.0);
    // Saturation: even and odd harmonics appear, more with SATURATION; nothing at 0.
    auto harmonics = [] (float saturation) {
        auto s = still (RetroSettings::tape);
        s.saturation = saturation;
        s.highHz = 20000.0f;
        auto q = retro (s);
        auto b = sine (200.0, 0.5, 0.7f);
        run (*q, b);
        const double f = toneAt (b.l, 200.0, 9600, b.size());
        return std::pair { db (toneAt (b.l, 400.0, 9600, b.size()) / f), db (toneAt (b.l, 600.0, 9600, b.size()) / f) };
    };
    const auto none = harmonics (0.0f), some = harmonics (0.8f);
    CHECK (none.first < -80.0);
    CHECK (none.second < -80.0);
    CHECK (some.first > -50.0);    // (the bias: even)
    CHECK (some.second > -40.0);   // (odd)
    // Heavy wear and every option on: finite, bounded.
    RetroSettings heavy;
    heavy.wear = heavy.character = heavy.saturation = 1.0f;
    heavy.dropoutRate = 12.0f;
    heavy.dropoutDepth = 1.0f;
    heavy.compAmount = 1.0f;
    heavy.noiseDb = -24.0f;
    heavy.inputDb = 18.0f;
    auto q = retro (heavy);
    auto b = noise (1.0, 0.9f);
    run (*q, b);
    CHECK (finite (b, 4.0f));
}

TEST_CASE ("retro-speed: VINYL's wow is its platter's turn - 33 1/3, 45 and 78 rpm measured", "[fx][retro][vinyl]")
{
    for (const auto& [rpm, hz] : { std::pair { 0, 100.0 / 3.0 / 60.0 }, std::pair { 1, 45.0 / 60.0 }, std::pair { 2, 78.0 / 60.0 } })
    {
        INFO (rpm);
        auto r = still (RetroSettings::vinyl);
        r.rpm = rpm;
        r.wowDepth = 1.0f;
        auto p = retro (r);
        // The transport's pitch deviation, block by block, over 12 s: its rate is the platter's.
        Buffer b (64);
        std::vector<float> cents;
        for (int i = 0; i < static_cast<int> (12.0 * rate / 64); ++i)
        {
            std::fill (b.l.begin(), b.l.end(), 0.0f);
            std::fill (b.r.begin(), b.r.end(), 0.0f);
            p->process (b.l.data(), b.r.data(), 64);
            cents.push_back (p->wowCents.load());
        }
        int crossings = 0;
        for (std::size_t i = 1; i < cents.size(); ++i)
            crossings += (cents[i - 1] < 0.0f) != (cents[i] < 0.0f) ? 1 : 0;
        CHECK (0.5 * crossings / 12.0 == Approx (hz).margin (0.12));
        float peak = 0.0f;
        for (const float c : cents)
            peak = std::max (peak, std::abs (c));
        CHECK (peak > 3.0f);    // audible wow
        CHECK (peak < 40.0f);   // never a pitch bend
    }
}

TEST_CASE ("retro-vinyl-event: crackle grows with its amount, seeded (the same twice), bounded; another seed differs", "[fx][retro][vinyl]")
{
    auto render = [] (float crackle, int seed) {
        auto r = still (RetroSettings::vinyl);
        r.crackle = crackle;
        r.pops = crackle * 0.5f;
        r.seed = seed;
        auto p = retro (r);
        Buffer b (static_cast<int> (3.0 * rate));
        run (*p, b);
        return std::pair { b, p->eventHead.load() };
    };
    const auto [few, fewEvents] = render (0.15f, 0);
    const auto [many, manyEvents] = render (0.8f, 0);
    CHECK (rms (many.l) > 2.0 * rms (few.l));
    CHECK (finite (many, 1.0f));
    const auto [again, againEvents] = render (0.8f, 0);
    CHECK (again.l == many.l);
    CHECK (again.r == many.r);
    (void) fewEvents; (void) manyEvents; (void) againEvents;
    const auto [other, otherEvents] = render (0.8f, 9);
    (void) otherEvents;
    CHECK (other.l != many.l);
}

TEST_CASE ("retro-digital: bits set the noise floor, the rate aliases (HARD) or is filtered (CLEAN), 80s and 90s differ", "[fx][retro][digital]")
{
    // The error against the same chain at 16 bits (the medium's response cancels): the floor.
    auto render = [] (float bits) {
        auto r = still (RetroSettings::digital);
        r.era = RetroSettings::nineties;
        r.bits = bits;
        r.rateKHz = 48.0f;
        auto p = retro (r);
        auto b = sine (997.0, 0.5, 0.5f);
        run (*p, b);
        return b;
    };
    const auto reference = render (16.0f);
    auto floorFor = [&reference, &render] (float bits) {
        const auto b = render (bits);
        const int from = 4800;
        double err = 0.0;
        for (int i = from; i < b.size(); ++i)
        {
            const double e = b.l[static_cast<std::size_t> (i)] - reference.l[static_cast<std::size_t> (i)];
            err += e * e;
        }
        return db (std::sqrt (err / (b.size() - from)));
    };
    const double six = floorFor (6.0f), twelve = floorFor (12.0f);
    INFO ("6 bits " << six << " dB, 12 bits " << twelve << " dB");
    CHECK (six - twelve > 30.0);   // ~6 dB a bit
    CHECK (twelve < -60.0);
    // An 8 kHz converter: 5 kHz folds to 3 kHz raw; CLEAN removes it.
    auto alias = [] (bool hard) {
        auto r = still (RetroSettings::digital);
        r.era = RetroSettings::nineties;
        r.bits = 16.0f;
        r.rateKHz = 8.0f;
        r.aliasHard = hard;
        auto p = retro (r);
        auto b = sine (5000.0, 0.5, 0.5f);
        run (*p, b);
        return db (toneAt (b.l, 3000.0, 4800, b.size()));
    };
    CHECK (alias (true) > -20.0);
    CHECK (alias (false) < -45.0);
    // 80s (truncation, no dither) and 90s (rounded, dithered, shaped) are different machines.
    auto era = [] (int which) {
        auto r = still (RetroSettings::digital);
        r.era = which;
        r.bits = 8.0f;
        auto p = retro (r);
        auto b = sine (440.0, 0.3, 0.3f);
        run (*p, b);
        return b;
    };
    CHECK (era (RetroSettings::eighties).l != era (RetroSettings::nineties).l);
}

TEST_CASE ("retro-codec-mask: LOSS masks and quantises (holes, not a bit-crusher); DROP and REPEAT are distinct; finite", "[fx][retro][codec]")
{
    auto codec = [] (int mode, float loss, float speed = 0.2f) {
        auto r = still (RetroSettings::digital);
        r.era = RetroSettings::codec;
        r.codecMode = mode;
        r.codecLoss = loss;
        r.codecSpeed = speed;
        auto p = retro (r);
        auto b = noise (1.0, 0.3f);
        run (*p, b);
        return std::tuple { b, p->keptFraction.load(), p->eventHead.load() };
    };
    const auto [light, lightKept, e1] = codec (RetroSettings::standard, 0.1f);
    const auto [heavy, heavyKept, e2] = codec (RetroSettings::standard, 0.9f);
    CHECK (lightKept > heavyKept);
    CHECK (heavyKept < 0.6f);
    CHECK (finite (heavy, 2.0f));
    // The band limit: at LOSS 90 % nothing near 18 kHz survives.
    CHECK (db (toneAt (heavy.l, 18000.0, 9600, heavy.size())) < db (toneAt (light.l, 18000.0, 9600, light.size())) - 20.0);
    const auto [dropped, dk, dropEvents] = codec (RetroSettings::drop, 1.0f);
    const auto [repeated, rk, repeatEvents] = codec (RetroSettings::repeat, 1.0f);
    CHECK (dropEvents > 0);
    CHECK (repeatEvents > 0);
    CHECK (rms (dropped.l) < rms (repeated.l));   // a dropped frame is silent, a repeated one is not
    const auto [jittered, jk, je] = codec (RetroSettings::jitter, 1.0f);
    CHECK (finite (jittered, 2.0f));
    CHECK (jittered.l != light.l);
    (void) e1; (void) e2; (void) dk; (void) rk; (void) jk; (void) je;
}

TEST_CASE ("retro-artifact-only: the aligned difference - silence at LOSS 0, bounded content at LOSS 80 %", "[fx][retro][codec]")
{
    for (const float loss : { 0.0f, 0.8f })
    {
        auto r = still (RetroSettings::digital);
        r.era = RetroSettings::codec;
        r.codecLoss = loss;
        r.artifactOnly = true;
        auto p = retro (r);
        auto b = sine (700.0, 0.6, 0.5f);
        run (*p, b);
        const double level = rms (b.l, 4 * p->latency());
        if (loss == 0.0f)
            CHECK (db (level) < -70.0);
        else
        {
            CHECK (db (level) > -50.0);
            CHECK (finite (b, 1.0f));
        }
    }
}

TEST_CASE ("retro-mono: one image in mono with the transports in phase; a stereo offset keeps the low end", "[fx][retro]")
{
    auto r = still (RetroSettings::tape);
    r.wowDepth = 0.8f;
    r.flutterDepth = 0.6f;
    auto p = retro (r);
    auto b = sine (100.0, 1.0);
    run (*p, b);
    CHECK (b.l == b.r);
    r.stereoPhaseDeg = 90.0f;
    auto q = retro (r);
    auto c = sine (100.0, 1.0);
    run (*q, c);
    std::vector<float> mono (c.l.size());
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = 0.5f * (c.l[i] + c.r[i]);
    CHECK (db (rms (mono, 9600)) > db (rms (c.l, 9600)) - 1.0);
}

TEST_CASE ("retro-automation: every setting moved every block, blocks of 1 to 2048 samples, personalities switched - finite", "[fx][retro]")
{
    osp::Prng prng (11);
    Settings s;
    auto p = std::make_unique<Retro>();
    p->prepare (rate, 2048);
    p->set (s, 0);
    p->reset();
    auto in = noise (0.1, 0.5f);
    std::vector<float> l (2048), r (2048);
    bool ok = true;
    for (int round = 0; round < 400; ++round)
    {
        auto& x = s.retro[0];
        x.mode = static_cast<int> (prng.nextBelow (3));
        x.era = static_cast<int> (prng.nextBelow (3));
        x.codecMode = static_cast<int> (prng.nextBelow (5));
        x.codecStereo = static_cast<int> (prng.nextBelow (3));
        x.rpm = static_cast<int> (prng.nextBelow (3));
        x.preserve = static_cast<int> (prng.nextBelow (3));
        x.noiseType = static_cast<int> (prng.nextBelow (5));
        for (float* v : { &x.character, &x.wear, &x.saturation, &x.wowDepth, &x.flutterDepth, &x.randomness, &x.noiseFollow, &x.crackle, &x.pops,
                          &x.dropoutDepth, &x.headBump, &x.compAmount, &x.digitalMotion, &x.codecLoss, &x.codecSpeed, &x.codecArtifact, &x.codecAmbience, &x.mix })
            *v = static_cast<float> (prng.nextDouble());
        x.inputDb = static_cast<float> (prng.uniform (-18.0, 18.0));
        x.toneDb = static_cast<float> (prng.uniform (-12.0, 12.0));
        x.noiseDb = static_cast<float> (prng.uniform (-96.0, -24.0));
        x.dropoutRate = static_cast<float> (prng.uniform (0.0, 12.0));
        x.bits = static_cast<float> (prng.uniform (4.0, 16.0));
        x.rateKHz = static_cast<float> (prng.uniform (4.0, 48.0));
        x.lowHz = static_cast<float> (prng.uniform (20.0, 800.0));
        x.highHz = static_cast<float> (prng.uniform (2000.0, 20000.0));
        x.artifactOnly = prng.nextDouble() < 0.2;
        x.aliasHard = prng.nextDouble() < 0.5;
        x.compLink = prng.nextDouble() < 0.5;
        p->set (s, 0);
        const int n = 1 + static_cast<int> (prng.nextBelow (2048));
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<std::size_t> (i)] = in.l[static_cast<std::size_t> (i % in.size())];
            r[static_cast<std::size_t> (i)] = in.r[static_cast<std::size_t> (i % in.size())];
        }
        p->process (l.data(), r.data(), n);
        for (int i = 0; i < n; ++i)
            ok = ok && std::isfinite (l[static_cast<std::size_t> (i)]) && std::isfinite (r[static_cast<std::size_t> (i)]) && std::abs (l[static_cast<std::size_t> (i)]) < 16.0f;
    }
    CHECK (ok);
}

TEST_CASE ("retro: the same settings render the same samples (a host's offline bounce); RETRO is in the library", "[fx][retro]")
{
    RetroSettings r;
    r.wear = 0.8f;
    r.dropoutRate = 4.0f;
    r.dropoutDepth = 0.7f;
    r.noiseDb = -50.0f;
    r.randomness = 1.0f;
    auto a = retro (r), b = retro (r);
    auto x = noise (1.0), y = noise (1.0);
    run (*a, x, 128);
    run (*b, y, 333);   // (another block size: the same)
    CHECK (x.l == y.l);
    CHECK (implemented (Type::retro));
    CHECK (capacity[static_cast<std::size_t> (Type::retro)] == 2);
    CHECK (std::string (typeKey (Type::retro)) == "retro");
}

TEST_CASE ("retro-cpu: one instance's cost per personality (a benchmark; prints, asserts only a bound)", "[.][benchmark][retro]")
{
    struct Case { const char* name; int mode, era; };
    for (const auto& c : { Case { "TAPE", RetroSettings::tape, 1 }, Case { "VINYL", RetroSettings::vinyl, 1 }, Case { "DIGITAL 80s", RetroSettings::digital, 0 },
                           Case { "DIGITAL 90s", RetroSettings::digital, 1 }, Case { "CODEC", RetroSettings::digital, 2 } })
    {
        RetroSettings r;
        r.mode = c.mode;
        r.era = c.era;
        r.noiseDb = -60.0f;
        r.crackle = 0.3f;
        r.compAmount = 0.3f;
        auto p = retro (r);
        auto b = noise (10.0, 0.3f);
        const auto start = std::chrono::steady_clock::now();
        run (*p, b, 256);
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        const double percent = 100.0 * seconds / 10.0;   // (of one core, at 48 kHz stereo)
        WARN (c.name << ": " << percent << " % of a core (latency " << p->latency() << " samples)");
        CHECK (percent < 25.0);
    }
}
