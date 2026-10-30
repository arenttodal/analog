// The MORPH analysis (dsp/morph): root and f0 accuracy, harmonic partials, residual noise,
// spectral envelope, keyframes, odd input, determinism, cancellation and interpolation, all
// on original synthetic fixtures (tests/fixtures/MorphFixtures.h). Timings are printed, never
// asserted.

#include "fixtures/MorphFixtures.h"
#include "morph/MorphAnalyzer.h"

#include "core/PitchMath.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using andor::analog::morph::analyse;
using andor::analog::morph::AnalysisOptions;
using andor::analog::morph::MorphProfile;
namespace fx = morphfixtures;

namespace
{
    constexpr double rate = 48000.0;
    constexpr double roots[] = { 55.0, 110.0, 220.0, 440.0, 880.0 };

    struct Timed
    {
        MorphProfile profile;
        double ms = 0.0;
    };

    Timed timedAnalyse (const std::vector<float>& x, double sampleRate, const AnalysisOptions& options = {})
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto p = analyse (x.data(), static_cast<std::int64_t> (x.size()), sampleRate, options);
        const auto t1 = std::chrono::steady_clock::now();
        return { std::move (p), std::chrono::duration<double, std::milli> (t1 - t0).count() };
    }

    MorphProfile run (const std::vector<float>& x, double sampleRate = rate, const AnalysisOptions& options = {})
    {
        return analyse (x.data(), static_cast<std::int64_t> (x.size()), sampleRate, options);
    }

    double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

    /** Last frame within 50 dB of the loudest (the analysis' own "end of sound"). */
    int lastAudible (const MorphProfile& p)
    {
        float peak = 0.0f;
        for (const auto& f : p.frames)
            peak = std::max (peak, f.level);
        for (int k = static_cast<int> (p.frames.size()) - 1; k >= 0; --k)
            if (p.frames[static_cast<std::size_t> (k)].level >= peak * 0.00316f)
                return k;
        return 0;
    }

    /** Median over the voiced body frames of partial h's level in dB (1-based h). */
    double bodyPartialDb (const MorphProfile& p, int h)
    {
        std::vector<double> v;
        for (int k = p.bodyFrame; k <= lastAudible (p); ++k)
        {
            const auto& f = p.frames[static_cast<std::size_t> (k)];
            if (f.voicing >= 0.5f)
                v.push_back (db (f.amp[static_cast<std::size_t> (h - 1)]));
        }
        if (v.empty())
            return -240.0;
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    }

    /** Mean (dB domain) over the body frames of each envelope band. */
    std::vector<double> bodyEnvelope (const MorphProfile& p)
    {
        std::vector<double> e (MorphProfile::envelopeBands, 0.0);
        int m = 0;
        for (int k = p.bodyFrame; k <= lastAudible (p); ++k)
        {
            const auto& f = p.frames[static_cast<std::size_t> (k)];
            if (f.voicing < 0.5f)
                continue;
            for (int b = 0; b < MorphProfile::envelopeBands; ++b)
                e[static_cast<std::size_t> (b)] += f.envelopeDb[static_cast<std::size_t> (b)];
            ++m;
        }
        for (auto& v : e)
            v /= std::max (1, m);
        return e;
    }

    /** Median over the voiced body frames of the total residual RMS (all bands). */
    double medianBodyResidual (const MorphProfile& p)
    {
        std::vector<double> v;
        for (int k = p.bodyFrame; k <= lastAudible (p); ++k)
        {
            const auto& f = p.frames[static_cast<std::size_t> (k)];
            if (f.voicing < 0.5f)
                continue;
            double e = 0.0;
            for (float r : f.residual)
                e += static_cast<double> (r) * r;
            v.push_back (std::sqrt (e));
        }
        if (v.empty())
            return 0.0;
        std::sort (v.begin(), v.end());
        return v[v.size() / 2];
    }

    /** Total residual RMS (all bands) averaged in power over the body. */
    double bodyResidualRms (const MorphProfile& p)
    {
        double e = 0.0;
        int m = 0;
        for (int k = p.bodyFrame; k <= lastAudible (p); ++k)
        {
            for (float r : p.frames[static_cast<std::size_t> (k)].residual)
                e += static_cast<double> (r) * r;
            ++m;
        }
        return std::sqrt (e / std::max (1, m));
    }

    int nearestEnvelopeBand (double hz)
    {
        int best = 0;
        for (int b = 1; b < MorphProfile::envelopeBands; ++b)
            if (std::abs (std::log2 (MorphProfile::envelopeBandCentreHz (b) / hz))
                < std::abs (std::log2 (MorphProfile::envelopeBandCentreHz (best) / hz)))
                best = b;
        return best;
    }

    bool sameFrames (const MorphProfile& a, const MorphProfile& b)
    {
        if (a.frames.size() != b.frames.size())
            return false;
        for (std::size_t k = 0; k < a.frames.size(); ++k)
        {
            const auto& x = a.frames[k];
            const auto& y = b.frames[k];
            if (std::memcmp (&x.seconds, &y.seconds, sizeof (float)) != 0 || std::memcmp (&x.f0Hz, &y.f0Hz, sizeof (float)) != 0
                || std::memcmp (&x.voicing, &y.voicing, sizeof (float)) != 0 || std::memcmp (&x.level, &y.level, sizeof (float)) != 0
                || std::memcmp (x.amp.data(), y.amp.data(), sizeof x.amp) != 0
                || std::memcmp (x.residual.data(), y.residual.data(), sizeof x.residual) != 0
                || std::memcmp (x.envelopeDb.data(), y.envelopeDb.data(), sizeof x.envelopeDb) != 0)
                return false;
        }
        return true;
    }
} // namespace

TEST_CASE ("MORPH root: sine, triangle and bowed string within 10 cents from 55 to 880 Hz", "[morph][analysis]")
{
    for (double root : roots)
    {
        const auto s = run (fx::sine (root, 1.5, rate));
        const auto t = run (fx::triangle (root, 1.5, rate));
        const auto b = run (fx::bowedString (root, 2.0, rate, 7));
        const double cs = osp::centsBetween (root, s.fundamentalHz);
        const double ct = osp::centsBetween (root, t.fundamentalHz);
        const double cb = osp::centsBetween (root, b.fundamentalHz);
        INFO (root << " Hz: sine " << cs << " c, triangle " << ct << " c, bowed " << cb << " c (confidence " << s.rootConfidence << ", "
                   << t.rootConfidence << ", " << b.rootConfidence << ")");
        WARN ("root " << root << " Hz: sine " << cs << " c, triangle " << ct << " c, bowed " << cb << " c");
        CHECK (s.kind == MorphProfile::Kind::pitched);
        CHECK (t.kind == MorphProfile::Kind::pitched);
        CHECK (b.kind == MorphProfile::Kind::pitched);
        CHECK (std::abs (cs) <= 10.0);
        CHECK (std::abs (ct) <= 10.0);
        CHECK (std::abs (cb) <= 10.0);
        CHECK (std::abs (s.rootMidi - osp::hzToMidi (root)) <= 0.1);
    }
}

TEST_CASE ("MORPH root: the pluck within 15 cents", "[morph][analysis]")
{
    for (double root : roots)
    {
        const auto p = run (fx::pluck (root, 1.5, rate, 11));
        const double c = osp::centsBetween (root, p.fundamentalHz);
        INFO (p.status);
        WARN ("pluck " << root << " Hz: " << c << " cents, confidence " << p.rootConfidence << ", B " << p.inharmonicity);
        CHECK (p.kind == MorphProfile::Kind::pitched);
        CHECK (std::abs (c) <= 15.0);
    }
}

TEST_CASE ("MORPH root: the override replaces the root, the frames are unchanged", "[morph][analysis]")
{
    const auto x = fx::bowedString (220.0, 1.5, rate, 3);
    const auto detected = run (x);
    AnalysisOptions options;
    options.rootOverrideMidi = 60.5;
    const auto overridden = run (x, rate, options);
    CHECK (overridden.rootMidi == 60.5);
    CHECK (std::abs (overridden.fundamentalHz - osp::midiToHz (60.5)) < 1.0e-9);
    CHECK (std::abs (osp::centsBetween (220.0, detected.fundamentalHz)) <= 10.0);
    CHECK (sameFrames (detected, overridden));   // partials still measured at the detected f0
    CHECK (detected.contentKey == overridden.contentKey);
    CHECK (overridden.status.find ("overridden") != std::string::npos);
}

TEST_CASE ("MORPH partials: a triangle's odd harmonics fall as 1/h^2, its even ones are absent", "[morph][analysis]")
{
    for (double root : { 110.0, 440.0 })
    {
        const auto p = run (fx::triangle (root, 1.5, rate));
        REQUIRE (p.kind == MorphProfile::Kind::pitched);
        const double h1 = bodyPartialDb (p, 1);
        for (int h : { 3, 5, 7 })
        {
            const double measured = bodyPartialDb (p, h) - h1;
            const double theory = -40.0 * std::log10 (h);
            WARN ("triangle " << root << " Hz, h" << h << ": " << measured << " dB re h1 (theory " << theory << ")");
            CHECK (std::abs (measured - theory) <= 1.5);
        }
        double worstEven = -240.0;
        for (int h : { 2, 4, 6, 8 })
            worstEven = std::max (worstEven, bodyPartialDb (p, h) - h1);
        WARN ("triangle " << root << " Hz: worst even harmonic " << worstEven << " dB re h1");
        CHECK (worstEven <= -30.0);
    }
}

TEST_CASE ("MORPH partials: a sine has energy only in partial 1", "[morph][analysis]")
{
    for (double root : { 110.0, 440.0 })
    {
        const auto p = run (fx::sine (root, 1.5, rate));
        REQUIRE (p.kind == MorphProfile::Kind::pitched);
        const double h1 = bodyPartialDb (p, 1);
        CHECK (std::abs (h1 - db (0.5)) <= 0.5);   // amplitude 0.5, measured
        double worst = -240.0;
        for (int h = 2; h <= MorphProfile::partials; ++h)
            worst = std::max (worst, bodyPartialDb (p, h) - h1);
        WARN ("sine " << root << " Hz: h1 " << h1 << " dBFS, strongest other partial " << worst << " dB re h1");
        CHECK (worst <= -50.0);
    }
}

TEST_CASE ("MORPH partials: the bowed string's partials peak near its body resonances", "[morph][analysis]")
{
    const double root = 110.0;
    const auto p = run (fx::bowedString (root, 2.5, rate, 5));
    REQUIRE (p.kind == MorphProfile::Kind::pitched);
    for (const auto& r : fx::bowedResonances)
    {
        // The strongest partial within a third of an octave of the resonance must be its
        // nearest harmonic (+/- 1) and stand clear of the partials half an octave away.
        const int centre = static_cast<int> (std::lround (r.hz / root));
        int strongest = centre;
        for (int h = std::max (1, static_cast<int> (r.hz / root / 1.26)); h <= static_cast<int> (r.hz / root * 1.26) + 1; ++h)
            if (bodyPartialDb (p, h) > bodyPartialDb (p, strongest))
                strongest = h;
        const int below = std::max (1, static_cast<int> (std::lround (r.hz / root / 1.41)));
        const int above = static_cast<int> (std::lround (r.hz / root * 1.41));
        const double peakDb = bodyPartialDb (p, strongest);
        WARN (r.hz << " Hz resonance: strongest partial h" << strongest << " (" << strongest * root << " Hz) " << peakDb << " dB; h"
                   << below << " " << bodyPartialDb (p, below) << " dB, h" << above << " " << bodyPartialDb (p, above) << " dB");
        CHECK (std::abs (strongest - centre) <= 1);
        CHECK (peakDb > bodyPartialDb (p, below) + 3.0);
        CHECK (peakDb > bodyPartialDb (p, above) + 3.0);
    }
}

TEST_CASE ("MORPH residual: white noise is unpitched and residual; bow noise shows above a sine's", "[morph][analysis]")
{
    const auto noise = run (fx::whiteNoise (1.5, rate, 99));
    INFO (noise.status);
    CHECK (noise.kind == MorphProfile::Kind::unpitched);
    int voiced = 0;
    for (const auto& f : noise.frames)
        voiced += f.voicing >= 0.5f ? 1 : 0;
    CHECK (voiced <= static_cast<int> (noise.frames.size()) / 20);
    // Uniform noise of amplitude 0.25 has RMS 0.25 / sqrt(3); bands cover 40 Hz .. 20 kHz.
    const double expected = 0.25 / std::sqrt (3.0);
    const double measured = bodyResidualRms (noise);
    WARN ("white noise: residual RMS " << db (measured) << " dBFS (signal " << db (expected) << " dBFS)");
    CHECK (std::abs (db (measured) - db (expected)) <= 1.5);

    const auto bowed = run (fx::bowedString (220.0, 2.0, rate, 5));
    const auto sine = run (fx::sine (220.0, 2.0, rate));
    const double bowedNoise = db (medianBodyResidual (bowed)), sineNoise = db (medianBodyResidual (sine));
    WARN ("residual RMS: bowed " << bowedNoise << " dBFS, sine " << sineNoise << " dBFS");
    CHECK (bowedNoise > sineNoise + 20.0);
    CHECK (bowedNoise < db (0.5) - 10.0);   // the bow noise is noise under a tone, not the tone
}

TEST_CASE ("MORPH envelope: the bowed string's resonances show within one band", "[morph][analysis]")
{
    for (double root : { 55.0, 110.0 })
    {
        const auto p = run (fx::bowedString (root, 2.5, rate, 5));
        REQUIRE (p.kind == MorphProfile::Kind::pitched);
        const auto env = bodyEnvelope (p);
        for (const auto& r : fx::bowedResonances)
        {
            const int band = nearestEnvelopeBand (r.hz);
            int peakBand = -1;
            for (int b = std::max (1, band - 1); b <= std::min (MorphProfile::envelopeBands - 2, band + 1); ++b)
                if (env[static_cast<std::size_t> (b)] >= env[static_cast<std::size_t> (b - 1)]
                    && env[static_cast<std::size_t> (b)] >= env[static_cast<std::size_t> (b + 1)])
                    peakBand = b;
            WARN (root << " Hz root, " << r.hz << " Hz resonance -> band " << band << ", local peak at band " << peakBand << " ("
                       << (peakBand >= 0 ? MorphProfile::envelopeBandCentreHz (peakBand) : 0.0) << " Hz)");
            CHECK (peakBand >= 0);
        }
    }
}

TEST_CASE ("MORPH keyframes: the bowed string sustains, the pluck does not, onsets are found", "[morph][analysis]")
{
    const auto bowed = run (fx::bowedString (220.0, 3.0, rate, 5));
    INFO (bowed.status);
    REQUIRE (bowed.sustains);
    const double loopSeconds = (bowed.loopEndFrame - bowed.loopStartFrame) * bowed.hopSeconds;
    WARN ("bowed: onset " << bowed.onsetFrame * bowed.hopSeconds << " s, body " << bowed.bodyFrame * bowed.hopSeconds << " s, loop "
                         << bowed.loopStartFrame * bowed.hopSeconds << " .. " << bowed.loopEndFrame * bowed.hopSeconds << " s");
    CHECK (bowed.loopStartFrame >= bowed.bodyFrame);
    CHECK (bowed.loopEndFrame <= lastAudible (bowed));
    CHECK (bowed.loopEndFrame > bowed.loopStartFrame);
    CHECK (loopSeconds >= 1.0);
    // The body comes after the 150 ms bowed attack, not in the middle of the note.
    CHECK (bowed.bodyFrame * bowed.hopSeconds >= 0.08);
    CHECK (bowed.bodyFrame * bowed.hopSeconds <= 0.5);

    const auto pluck = run (fx::pluck (220.0, 2.0, rate, 5));
    const double pluckLoop = pluck.sustains ? (pluck.loopEndFrame - pluck.loopStartFrame) * pluck.hopSeconds : 0.0;
    WARN ("pluck: sustains " << pluck.sustains << ", loop " << pluckLoop << " s, body " << pluck.bodyFrame * pluck.hopSeconds << " s");
    CHECK ((! pluck.sustains || pluckLoop < 0.4));

    for (const auto& tone : { fx::sine (330.0, 1.0, rate), fx::pluck (330.0, 1.0, rate, 2), fx::bowedString (330.0, 1.0, rate, 2) })
    {
        const auto p = run (fx::delayed (tone, 0.1, rate));
        const double onset = p.onsetFrame * p.hopSeconds;
        INFO ("onset " << onset << " s for a tone starting at 0.1 s");
        CHECK (std::abs (onset - 0.1) <= 0.010);
    }
}

TEST_CASE ("MORPH odd input: silence, 10 ms, a click, DC, NaN and 30 s at 96 kHz", "[morph][analysis]")
{
    const auto silent = run (fx::silence (1.0, rate));
    CHECK (silent.kind == MorphProfile::Kind::silent);
    CHECK (! silent.status.empty());
    CHECK (silent.frames.size() <= 1);

    const auto shortTone = run (fx::tooShort (rate));
    INFO (shortTone.status);
    CHECK (shortTone.kind == MorphProfile::Kind::tooShort);
    CHECK (! shortTone.status.empty());

    const auto click = run (fx::click (1.0, rate, 0.25));
    INFO (click.status);
    CHECK (click.kind == MorphProfile::Kind::unpitched);
    CHECK (! click.status.empty());
    CHECK (! click.sustains);
    REQUIRE (! click.transientFrames.empty());
    CHECK (std::abs (click.transientFrames.front() * click.hopSeconds - 0.25) <= 0.03);

    const auto dc = run (fx::dc (1.0, rate));
    INFO (dc.status);
    CHECK (dc.kind == MorphProfile::Kind::silent);
    CHECK (dc.status.find ("DC") != std::string::npos);

    auto broken = fx::sine (220.0, 0.5, rate);
    broken[1000] = std::nanf ("");
    broken[2000] = INFINITY;
    const auto nan = run (broken);
    INFO (nan.status);
    CHECK (nan.kind == MorphProfile::Kind::pitched);
    CHECK (nan.status.find ("non-finite") != std::string::npos);

    const auto empty = analyse (nullptr, 0, rate);
    CHECK (empty.kind == MorphProfile::Kind::tooShort);
    const auto badRate = run (fx::sine (220.0, 0.5, rate), 0.0);
    CHECK (! badRate.status.empty());

    std::vector<float> longFile = fx::sine (110.0, 30.0, 96000.0, 0.3);
    const auto timed = timedAnalyse (longFile, 96000.0);
    WARN ("30 s at 96 kHz: " << timed.profile.frames.size() << " frames, hop " << 1000.0 * timed.profile.hopSeconds << " ms, "
                             << timed.ms << " ms, " << timed.profile.status);
    CHECK (timed.profile.frames.size() <= 2000);
    CHECK (timed.profile.kind == MorphProfile::Kind::pitched);
    CHECK (std::abs (osp::centsBetween (110.0, timed.profile.fundamentalHz)) <= 10.0);
}

TEST_CASE ("MORPH cost: a 10 s 48 kHz bowed string", "[morph][analysis]")
{
    const auto x = fx::bowedString (196.0, 10.0, rate, 21);
    const auto timed = timedAnalyse (x, rate);
    const auto bytes = timed.profile.frames.size() * sizeof (MorphProfile::Frame);
    WARN ("10 s at 48 kHz: " << timed.profile.frames.size() << " frames x " << sizeof (MorphProfile::Frame) << " B = " << bytes / 1024.0
                             << " KiB, analysis " << timed.ms << " ms; " << timed.profile.status << "; B = " << timed.profile.inharmonicity);
    CHECK (timed.profile.frames.size() <= 2000);
    CHECK (timed.profile.kind == MorphProfile::Kind::pitched);
}

TEST_CASE ("MORPH determinism: identical profiles and keys; the key follows the content", "[morph][analysis]")
{
    const auto x = fx::bowedString (147.0, 1.5, rate, 13);
    const auto a = run (x);
    const auto b = run (x);
    CHECK (sameFrames (a, b));
    CHECK (a.contentKey == b.contentKey);
    CHECK (a.contentKey.size() == 16);
    CHECK (a.fundamentalHz == b.fundamentalHz);
    CHECK (a.transientFrames == b.transientFrames);
    CHECK (a.loopStartFrame == b.loopStartFrame);
    CHECK (a.loopEndFrame == b.loopEndFrame);
    CHECK (a.status == b.status);

    auto y = x;
    y[y.size() / 2] += 1.0e-6f;
    CHECK (run (y).contentKey != a.contentKey);
    CHECK (run (x, 44100.0).contentKey != a.contentKey);
    CHECK (fx::bowedString (147.0, 1.5, rate, 13) == x);   // the fixtures are seeded too
}

TEST_CASE ("MORPH cancellation: stops early when cancelled from the progress callback", "[morph][analysis]")
{
    const auto x = fx::bowedString (220.0, 4.0, rate, 1);
    std::atomic<bool> cancel { false };
    float lastProgress = 0.0f;
    int calls = 0;
    const auto p = analyse (x.data(), static_cast<std::int64_t> (x.size()), rate, {}, &cancel, [&] (float v) {
        CHECK (v >= lastProgress);
        lastProgress = v;
        ++calls;
        if (v >= 0.2f)
            cancel = true;
    });
    INFO (p.status);
    CHECK (p.status.rfind ("cancelled", 0) == 0);
    CHECK (p.frames.empty());
    CHECK (p.contentKey.empty());
    CHECK (lastProgress < 0.5f);

    float finalProgress = 0.0f;
    const auto full = analyse (x.data(), static_cast<std::int64_t> (x.size()), rate, {}, nullptr, [&] (float v) { finalProgress = v; });
    CHECK (finalProgress == 1.0f);
    CHECK (full.status.rfind ("cancelled", 0) != 0);
}

TEST_CASE ("MORPH Frame at(): interpolates between frames and clamps at the ends", "[morph][analysis]")
{
    const auto p = run (fx::pluck (220.0, 1.0, rate, 4));
    REQUIRE (p.frames.size() > 10);
    const auto& f0 = p.frames[5];
    const auto& f1 = p.frames[6];
    const auto exact = p.at (5 * p.hopSeconds);
    CHECK (exact.level == f0.level);
    CHECK (exact.amp == f0.amp);

    const auto mid = p.at (5.5 * p.hopSeconds);
    CHECK (std::abs (mid.seconds - 0.5f * (f0.seconds + f1.seconds)) < 1.0e-6f);
    CHECK (std::abs (mid.level - std::sqrt (f0.level * f1.level)) <= 1.0e-4f * f0.level);
    CHECK (mid.level >= std::min (f0.level, f1.level));
    CHECK (mid.level <= std::max (f0.level, f1.level));
    CHECK (std::abs (mid.envelopeDb[10] - 0.5f * (f0.envelopeDb[10] + f1.envelopeDb[10])) < 1.0e-3f);
    if (f0.f0Hz > 0.0f && f1.f0Hz > 0.0f)
        CHECK (std::abs (mid.f0Hz - std::sqrt (f0.f0Hz * f1.f0Hz)) < 1.0e-2f);

    const auto before = p.at (-1.0);
    const auto after = p.at (100.0);
    CHECK (before.level == p.frames.front().level);
    CHECK (after.level == p.frames.back().level);
    CHECK (after.amp == p.frames.back().amp);
    CHECK (MorphProfile {}.at (0.3).level == 0.0f);
}
