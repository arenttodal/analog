// MORPH in the synth: the ORGANIC recording turned into the analog carrier by IMPRINT, FUSE
// and WEAVE. The ends are exact (0: the recording, 1: the carrier). The middle is a fusion, not
// a crossfade: it carries the recording's spectral envelope on the carrier's harmonics, and the
// three methods differ. Moves are click-free, every carrier pairing works, and an unavailable
// morph leaves the sources as they are. Deterministic, at a stated CPU cost.

#include "TestUtil.h"
#include "fixtures/MorphFixtures.h"
#include "morph/MorphAnalyzer.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>

using namespace testutil;
namespace morph = andor::analog::morph;

namespace
{
    struct Recording
    {
        std::shared_ptr<osp::InstrumentModel> model;
        morph::MorphProfile profile;
    };

    /** The bowed-string stand-in (original, synthesized) at A3, its model and its profile. */
    const Recording& bowed()
    {
        static const Recording r = [] {
            Recording x;
            const double rate = 48000.0;
            auto mono = morphfixtures::bowedString (220.0, 3.0, rate, 7);
            osp::AudioData audio;
            audio.sampleRate = rate;
            audio.channels = { mono };
            x.model = std::make_shared<osp::InstrumentModel>();
            x.model->original.source = std::make_shared<osp::PlaybackSource> (audio, 57.0, 4);
            x.profile = morph::analyse (mono.data(), static_cast<std::int64_t> (mono.size()), rate);
            return x;
        }();
        return r;
    }

    Patch morphPatch (MorphMethod method, double position, MorphCarrier carrier = MorphCarrier::both)
    {
        auto p = measurementPatch();
        p.osc2Level = 0.6;
        p.osc2Wave = Osc2Wave::pulse;
        p.morph.enabled = true;
        p.morph.method = method;
        p.morph.position = position;
        p.morph.carrier = carrier;
        p.morph.attack = 0.0;   // (the measures below look at the body)
        return p;
    }

    Stereo play (const Patch& p, bool profile = true, double seconds = 1.2, std::vector<Event> events = { on (0, 57, 100) }, int block = 256)
    {
        auto s = makeSynth (p);
        s->setOrganic (bowed().model.get());
        if (profile)
            s->setMorphProfile (&bowed().profile, bowed().model.get());
        return render (*s, seconds, std::move (events), block);
    }

    double relativeDifference (const Stereo& a, const Stereo& b, double from, double to)
    {
        double d = 0.0, e = 0.0;
        for (auto i = static_cast<std::size_t> (from * a.rate); i < static_cast<std::size_t> (to * a.rate); ++i)
        {
            d += (static_cast<double> (a.left[i]) - b.left[i]) * (static_cast<double> (a.left[i]) - b.left[i]);
            e += static_cast<double> (b.left[i]) * b.left[i];
        }
        return std::sqrt (d / std::max (e, 1.0e-30));
    }

    /** Third-octave band levels (dB), 100 Hz .. 10 kHz, of [from, from + 2^15). */
    std::vector<double> bandsDb (const Stereo& x, double from)
    {
        const auto mag = spectrum (x.mono(), static_cast<std::size_t> (from * x.rate), 15);
        const double binHz = x.rate / (2.0 * static_cast<double> (mag.size()));
        std::vector<double> out;
        for (double lo = 100.0; lo < 10000.0; lo *= std::exp2 (1.0 / 3.0))
        {
            double e = 1.0e-20;
            for (auto b = static_cast<std::size_t> (lo / binHz); b < static_cast<std::size_t> (lo * std::exp2 (1.0 / 3.0) / binHz); ++b)
                e += mag[b] * mag[b];
            out.push_back (10.0 * std::log10 (e));
        }
        return out;
    }

    /** A band profile with its overall tilt (a straight line over the bands) taken out: what
        is left is its shape (resonances, formants). */
    std::vector<double> detrended (std::vector<double> x)
    {
        const double n = static_cast<double> (x.size());
        double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            sx += static_cast<double> (i);
            sy += x[i];
            sxx += static_cast<double> (i * i);
            sxy += static_cast<double> (i) * x[i];
        }
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx), offset = (sy - slope * sx) / n;
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] -= offset + slope * static_cast<double> (i);
        return x;
    }

    /** How alike two band profiles' shapes are (Pearson correlation of the detrended dB values). */
    double shapeCorrelation (std::vector<double> a, std::vector<double> b)
    {
        a = detrended (std::move (a));
        b = detrended (std::move (b));
        double ma = 0.0, mb = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            ma += a[i];
            mb += b[i];
        }
        ma /= static_cast<double> (a.size());
        mb /= static_cast<double> (b.size());
        double sab = 0.0, saa = 0.0, sbb = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            sab += (a[i] - ma) * (b[i] - mb);
            saa += (a[i] - ma) * (a[i] - ma);
            sbb += (b[i] - mb) * (b[i] - mb);
        }
        return sab / std::sqrt (std::max (saa * sbb, 1.0e-30));
    }

    /** The levels (dB re the strongest) of harmonics 1..24 of A3 (220 Hz), from 0.4 s. */
    std::vector<double> harmonicProfile (const Stereo& x)
    {
        const auto mag = spectrum (x.mono(), static_cast<std::size_t> (0.4 * x.rate), 15);
        const double binHz = x.rate / (2.0 * static_cast<double> (mag.size()));
        std::vector<double> h;
        double top = 1.0e-20;
        for (int k = 1; k <= 24; ++k)
        {
            const auto centre = static_cast<std::size_t> (std::lround (220.0 * k / binHz));
            double peak = 1.0e-20;
            for (std::size_t b = centre - 4; b <= centre + 4; ++b)
                peak = std::max (peak, mag[b]);
            h.push_back (peak);
            top = std::max (top, peak);
        }
        for (auto& v : h)
            v = std::max (-80.0, 20.0 * std::log10 (v / top));
        return h;
    }

    double profileDistance (const std::vector<double>& a, const std::vector<double>& b)
    {
        double d = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            d += std::abs (a[i] - b[i]);
        return d / static_cast<double> (a.size());
    }

    /** How far the stand-in's three body resonances stand above their surroundings (dB, summed):
        each resonance's third-octave level against the mean of the bands 0.7 octave either side. */
    double formantProminence (const Stereo& x)
    {
        const auto mag = spectrum (x.mono(), static_cast<std::size_t> (0.4 * x.rate), 15);
        const double binHz = x.rate / (2.0 * static_cast<double> (mag.size()));
        auto level = [&] (double hz) {
            double e = 1.0e-20;
            for (auto b = static_cast<std::size_t> (hz * std::exp2 (-1.0 / 6.0) / binHz); b < static_cast<std::size_t> (hz * std::exp2 (1.0 / 6.0) / binHz); ++b)
                e += mag[b] * mag[b];
            return 10.0 * std::log10 (e);
        };
        double sum = 0.0;
        for (const double f : { 450.0, 1250.0, 2900.0 })
            sum += level (f) - 0.5 * (level (f * std::exp2 (-0.7)) + level (f * std::exp2 (0.7)));
        return sum;
    }

    /** The energy at the harmonics of f0 (+-2 bins) against the total: how harmonic it is. */
    double harmonicShare (const Stereo& x, double from, double f0)
    {
        const auto mag = spectrum (x.mono(), static_cast<std::size_t> (from * x.rate), 15);
        return 1.0 - std::pow (10.0, inharmonicDb (mag, x.rate, f0, 10000.0, 2) / 10.0);
    }
}

TEST_CASE ("morph: the analysis finds the stand-in's pitch, so MORPH is available", "[morph]")
{
    CHECK (bowed().profile.kind == morph::MorphProfile::Kind::pitched);
    CHECK (std::abs (bowed().profile.rootMidi - 57.0) < 0.1);
}

TEST_CASE ("morph: at 0 % the output is the recording, at 100 % the carrier (every method, both outputs' ends)", "[morph][endpoints]")
{
    // The references: the recording alone (oscillators off) and the carrier alone (no recording).
    auto organicOnly = measurementPatch();
    organicOnly.osc1Enabled = organicOnly.osc2Enabled = false;
    const auto recording = play (organicOnly);
    auto carrierPatch = morphPatch (MorphMethod::imprint, 0.0);
    carrierPatch.morph.enabled = false;
    auto noRecording = makeSynth (carrierPatch);
    const auto carrier = render (*noRecording, 1.2, { on (0, 57, 100) });

    for (const auto method : { MorphMethod::imprint, MorphMethod::fuse, MorphMethod::weave })
    {
        INFO ("method " << static_cast<int> (method));
        // 0 %: MORPH ONLY plays the recording (the carrier pair replaced by the morph).
        const auto zero = play (morphPatch (method, 0.0));
        CHECK (relativeDifference (zero, recording, 0.05, 1.1) < 1.0e-5);
        // 100 %: the carrier, as the oscillators play it without any recording.
        const auto one = play (morphPatch (method, 1.0));
        CHECK (relativeDifference (one, carrier, 0.05, 1.1) < 3.0e-4);   // (-70 dB: the model path's rounding against CLASSIC's own)
        CHECK (zero.finite());
        CHECK (one.finite());
    }
}

TEST_CASE ("morph: the middle is a fusion: the recording's envelope on the carrier's harmonics, not a crossfade", "[morph][fusion]")
{
    auto organicOnly = measurementPatch();
    organicOnly.osc1Enabled = organicOnly.osc2Enabled = false;
    const auto recording = play (organicOnly);
    const auto carrier = play (morphPatch (MorphMethod::imprint, 1.0));
    // An equal-power crossfade of the two, the thing a morph must not be.
    Stereo crossfade = recording;
    for (std::size_t i = 0; i < crossfade.left.size(); ++i)
    {
        crossfade.left[i] = 0.7071f * (recording.left[i] + carrier.left[i]);
        crossfade.right[i] = 0.7071f * (recording.right[i] + carrier.right[i]);
    }
    std::vector<Stereo> middles;
    for (const auto method : { MorphMethod::imprint, MorphMethod::fuse, MorphMethod::weave })
    {
        INFO ("method " << static_cast<int> (method));
        const auto middle = play (morphPatch (method, 0.5));
        CHECK (middle.finite());
        CHECK (middle.peak() < 0.9f);
        CHECK (middle.rms (0.3, 1.1) > 0.3 * std::min (recording.rms (0.3, 1.1), carrier.rms (0.3, 1.1)));
        // Not the crossfade.
        CHECK (relativeDifference (middle, crossfade, 0.3, 1.1) > 0.3);
        // Both sources' traits: its harmonic profile (the level of each of the first 24
        // harmonics, dB) lies between theirs: nearer the recording's than the carrier's is, and
        // nearer the carrier's than the recording's is.
        const auto hm = harmonicProfile (middle), hr = harmonicProfile (recording), hc = harmonicProfile (carrier);
        const double toRecording = profileDistance (hm, hr), toCarrier = profileDistance (hm, hc), apart = profileDistance (hr, hc);
        UNSCOPED_INFO ("method " << static_cast<int> (method) << ": harmonic distance to the recording " << toRecording << " dB, to the carrier "
                                 << toCarrier << " dB (they are " << apart << " dB apart)");
        CHECK (toRecording < apart);
        CHECK (toCarrier < apart);
        // The carrier's harmonics: its energy sits on the carrier's harmonic series (A3).
        CHECK (harmonicShare (middle, 0.4, 220.0) > 0.5);
        middles.push_back (middle);
    }
    // Three methods, three sounds.
    CHECK (relativeDifference (middles[0], middles[1], 0.3, 1.1) > 0.1);
    CHECK (relativeDifference (middles[1], middles[2], 0.3, 1.1) > 0.1);
    CHECK (relativeDifference (middles[0], middles[2], 0.3, 1.1) > 0.1);
}

TEST_CASE ("morph: moving the position, the method or the carrier while a note plays never clicks", "[morph][smooth]")
{
    for (const auto method : { MorphMethod::imprint, MorphMethod::fuse, MorphMethod::weave })
    {
        INFO ("method " << static_cast<int> (method));
        auto s = makeSynth (morphPatch (method, 0.0));
        s->setOrganic (bowed().model.get());
        s->setMorphProfile (&bowed().profile, bowed().model.get());
        const std::uint8_t note[3] { 0x90, 57, 100 };
        s->handleMidi (note, 3);
        Stereo out;
        out.left.assign (96000, 0.0f);
        out.right.assign (96000, 0.0f);
        for (int b = 0; b < 375; ++b)   // 2 s in 256-sample blocks: 0 -> 1 -> 0, a method change half way
        {
            const double t = b / 375.0;
            auto p = morphPatch (b < 190 ? method : MorphMethod::fuse, t < 0.5 ? 2.0 * t : 2.0 - 2.0 * t);
            if (b > 300)
                p.morph.carrier = MorphCarrier::osc1;
            s->setPatch (p);
            s->render (out.left.data() + b * 256, out.right.data() + b * 256, 256);
        }
        CHECK (out.finite());
        float worst = 0.0f;
        for (std::size_t i = 2400; i + 1 < out.left.size(); ++i)
            worst = std::max (worst, std::abs (out.left[i + 1] - out.left[i]));
        // The largest sample-to-sample step stays that of the sounds themselves (the pulse's
        // edges, the recording's): no jump on top.
        const auto ends = play (morphPatch (method, 1.0), true, 2.0);
        float own = 0.0f;
        for (std::size_t i = 2400; i + 1 < ends.left.size(); ++i)
            own = std::max (own, std::abs (ends.left[i + 1] - ends.left[i]));
        CHECK (worst < 1.6f * own);
    }
}

TEST_CASE ("morph: every carrier pairing and output, chords, held notes and the unavailable case", "[morph][pairs]")
{
    const std::vector<Event> chord { on (0, 45, 100), on (0, 52, 90), on (0, 57, 90), on (0, 60, 80), on (0, 64, 80), on (0, 67, 70), on (0, 69, 70),
                                     on (0, 72, 60) };
    for (const auto m1 : { OscModel::classic, OscModel::dco, OscModel::wide })
        for (const auto m2 : { OscModel::classic, OscModel::dco, OscModel::wide })
            for (const auto carrier : { MorphCarrier::osc1, MorphCarrier::osc2, MorphCarrier::both })
            {
                INFO ("models " << static_cast<int> (m1) << "+" << static_cast<int> (m2) << ", carrier " << static_cast<int> (carrier));
                auto p = morphPatch (static_cast<MorphMethod> ((static_cast<int> (m1) + static_cast<int> (carrier)) % 3), 0.5, carrier);
                p.osc1Model = m1;
                p.osc2Model = m2;
                p.filterModel = m2 == OscModel::dco ? FilterModel::ota : FilterModel::ladder;
                p.tone = 0.8;
                const auto out = play (p, true, 0.6, chord);
                CHECK (out.finite());
                CHECK (out.peak() < 1.0f);
                CHECK (out.rms (0.1, 0.5) > 0.01);
            }
    // LAYERED: the dry sources and the morph's core together (louder than MORPH ONLY's middle).
    auto layered = morphPatch (MorphMethod::fuse, 0.5);
    layered.morph.output = MorphOutput::layered;
    CHECK (play (layered).rms (0.3, 1.1) > play (morphPatch (MorphMethod::fuse, 0.5)).rms (0.3, 1.1));
    // Unavailable (no profile for this recording): the sources as they are, never a morph
    // that is not there.
    auto dry = morphPatch (MorphMethod::fuse, 0.5);
    dry.morph.enabled = false;
    CHECK (relativeDifference (play (morphPatch (MorphMethod::fuse, 0.5), false), play (dry, false), 0.05, 1.1) < 1.0e-6);
    // The same every time, whatever the block size.
    CHECK (play (morphPatch (MorphMethod::weave, 0.4), true, 0.8).left == play (morphPatch (MorphMethod::weave, 0.4), true, 0.8, { on (0, 57, 100) }, 61).left);
}

TEST_CASE ("morph: CPU of each method at 8 and 16 voices (reported, not asserted)", "[morph][cpu]")
{
    for (const auto method : { MorphMethod::imprint, MorphMethod::fuse, MorphMethod::weave })
        for (const int voices : { 8, 16 })
        {
            auto p = morphPatch (method, 0.5);
            p.polyphony = 16;
            auto s = makeSynth (p);
            s->setOrganic (bowed().model.get());
            s->setMorphProfile (&bowed().profile, bowed().model.get());
            for (int v = 0; v < voices; ++v)
            {
                const std::uint8_t note[3] { 0x90, static_cast<std::uint8_t> (45 + 2 * v), 100 };
                s->handleMidi (note, 3);
            }
            std::vector<float> l (512), r (512);
            const auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < 48000 * 2 / 512; ++b)
                s->render (l.data(), r.data(), 512);
            const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            WARN ("MORPH method " << static_cast<int> (method) << ", " << voices << " voices: " << 100.0 * seconds / 2.0 << " % of one core");
        }
}
