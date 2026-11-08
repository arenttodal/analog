// FX library processors (gates 5, 7, 8): each measured against what it claims - identity when
// neutral, responses against their coefficients, compressor curves and time behaviour, the
// spread's detune as pitch, the shifter's frequency and suppressed image, convolution as
// convolution, all finite at their extremes.
#include "fx/Effects.h"
#include "fx/FxEngine.h"
#include "TestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

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

    Buffer sine (double hz, double seconds, float amp = 0.5f, bool stereo = true)
    {
        Buffer b (static_cast<int> (seconds * rate));
        for (int i = 0; i < b.size(); ++i)
        {
            b.l[static_cast<std::size_t> (i)] = amp * static_cast<float> (std::sin (2.0 * dsp::pi * hz * i / rate));
            b.r[static_cast<std::size_t> (i)] = stereo ? b.l[static_cast<std::size_t> (i)] : 0.0f;
        }
        return b;
    }

    Buffer noise (double seconds, float amp = 0.3f, std::uint64_t seed = 3)
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

    /** The energy at one frequency (Goertzel), as an amplitude. */
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
        const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
        return 2.0 * std::sqrt (std::max (0.0, power)) / (to - from);
    }

    bool finite (const Buffer& b)
    {
        for (int i = 0; i < b.size(); ++i)
            if (! std::isfinite (b.l[static_cast<std::size_t> (i)]) || ! std::isfinite (b.r[static_cast<std::size_t> (i)]))
                return false;
        return true;
    }

    template <typename T>
    std::unique_ptr<T> make (const Settings& s)
    {
        auto p = std::make_unique<T>();
        p->prepare (rate, 512);
        p->set (s, 0);
        p->reset();
        return p;
    }
}

TEST_CASE ("fx utility: neutral passes bit for bit; width 0 is mono; polarity; mono bass; balance never boosts", "[fx][effects][utility]")
{
    Settings s;
    auto u = make<Utility> (s);
    auto in = noise (0.2), out = in;
    run (*u, out);
    CHECK (std::memcmp (in.l.data(), out.l.data(), in.l.size() * sizeof (float)) == 0);
    CHECK (std::memcmp (in.r.data(), out.r.data(), in.r.size() * sizeof (float)) == 0);

    s.utility[0].width = 0.0f;
    u = make<Utility> (s);
    out = noise (0.2);
    run (*u, out);
    for (int i = 0; i < out.size(); ++i)
        REQUIRE (out.l[static_cast<std::size_t> (i)] == Approx (out.r[static_cast<std::size_t> (i)]).margin (1.0e-6));

    s = {};
    s.utility[0].invertLeft = true;
    u = make<Utility> (s);
    out = in;
    run (*u, out);
    CHECK (out.l[100] == Approx (-in.l[100]));
    CHECK (out.r[100] == Approx (in.r[100]));

    // MONO BASS: a 50 Hz tone on the left only comes out on both sides; 2 kHz stays left.
    s = {};
    s.utility[0].monoBass = true;
    s.utility[0].monoBassHz = 150.0f;
    u = make<Utility> (s);
    auto low = sine (50.0, 1.0, 0.5f, false), high = sine (2000.0, 0.5, 0.5f, false);
    run (*u, low);
    u->reset();
    run (*u, high);
    CHECK (rms (low.r, 24000) == Approx (rms (low.l, 24000)).epsilon (0.05));
    CHECK (rms (high.r, 12000) < 0.02 * rms (high.l, 12000));

    // Balance: the near side stays at unity (never boosted), the far side falls.
    s = {};
    s.utility[0].pan = 0.5f;
    u = make<Utility> (s);
    out = sine (440.0, 0.3);
    run (*u, out);
    CHECK (rms (out.r, 9600) == Approx (rms (sine (440.0, 0.3).r, 9600)).epsilon (0.01));
    CHECK (rms (out.l, 9600) == Approx (0.5 * rms (sine (440.0, 0.3).l, 9600)).epsilon (0.02));
}

TEST_CASE ("fx equalizer: flat is a null; each band's measured gain matches the response it reports", "[fx][effects][eq]")
{
    Settings s;
    auto eq = make<Equalizer> (s);
    auto in = noise (0.2), out = in;
    run (*eq, out);
    CHECK (std::memcmp (in.l.data(), out.l.data(), in.l.size() * sizeof (float)) == 0);

    s.eq[0].bands[1] = { true, EqBand::peak, 1000.0f, 9.0f, 1.0f };
    s.eq[0].bands[3] = { true, EqBand::highShelf, 6000.0f, -6.0f, 0.7f };
    s.eq[0].hpOn = true;
    s.eq[0].hpHz = 80.0f;
    eq = make<Equalizer> (s);
    for (const double hz : { 60.0, 300.0, 1000.0, 3000.0, 12000.0 })
    {
        eq->reset();
        auto tone = sine (hz, 0.6, 0.1f);
        run (*eq, tone);
        const double measured = db (toneAt (tone.l, hz, 14400, 28800) / 0.1);
        INFO (hz << " Hz: measured " << measured << " dB, response " << eq->responseDb (hz) << " dB");
        CHECK (measured == Approx (eq->responseDb (hz)).margin (0.3));
    }
    CHECK (eq->responseDb (1000.0) == Approx (9.0).margin (0.2));
}

TEST_CASE ("fx filter: LP24 and HP12 slopes; the graph is the filter; resonance and comb stay finite; formants peak", "[fx][effects][filter]")
{
    Settings s;
    s.filter[0].type = FilterSettings::lp24;
    s.filter[0].hz = 1000.0f;
    auto f = make<FxFilter> (s);
    auto settle = sine (100.0, 0.1);
    run (*f, settle);   // (settle the glides)
    for (const double hz : { 200.0, 1000.0, 4000.0 })
    {
        f->reset();
        auto tone = sine (hz, 0.5, 0.1f);
        run (*f, tone);
        const double measured = db (toneAt (tone.l, hz, 12000, 24000) / 0.1);
        INFO (hz << " Hz: " << measured << " dB vs " << f->responseDb (hz));
        CHECK (measured == Approx (f->responseDb (hz)).margin (0.5));
    }
    CHECK (f->responseDb (4000.0) < -40.0);   // two octaves above: 24 dB / octave
    s.filter[0].type = FilterSettings::hp12;
    f = make<FxFilter> (s);
    CHECK (f->responseDb (250.0) < -20.0);

    for (const int type : { (int) FilterSettings::lp12, (int) FilterSettings::bp12, (int) FilterSettings::notch, (int) FilterSettings::allpass, (int) FilterSettings::combPlus,
                            (int) FilterSettings::combMinus, (int) FilterSettings::formant1, (int) FilterSettings::formant3 })
    {
        s.filter[0].type = type;
        s.filter[0].resonance = 0.95f;
        s.filter[0].driveDb = 24.0f;
        f = make<FxFilter> (s);
        auto b = noise (0.5, 0.8f);
        run (*f, b);
        INFO ("type " << type);
        CHECK (finite (b));
        CHECK (rms (b.l) < 4.0);
    }
    // FORMANT I: its peaks where its table says (the graph shows them).
    s.filter[0] = {};
    s.filter[0].type = FilterSettings::formant1;
    s.filter[0].hz = 20.0f;   // morph 0: the first vowel
    f = make<FxFilter> (s);
    const auto fs = FxFilter::formants (0, 0.0, 0.0);
    CHECK (f->responseDb (fs[0]) > f->responseDb (0.5 * (fs[0] + fs[1])) + 3.0);
}

TEST_CASE ("fx compressor: CLEAN's static curve and silence; FET's feedback differs; COLOR ONLY reduces nothing; MULTIBAND at 0 depth is flat, upward lifts, silence stays silent", "[fx][effects][comp]")
{
    Settings s;
    auto& c = s.comp[0];
    c.mode = CompSettings::clean;
    c.thresholdDb = -18.0f;
    c.ratio = 4.0f;
    c.kneeDb = 0.0f;
    c.attackMs = 1.0f;
    c.peak = false;
    auto comp = make<Compressor> (s);
    auto tone = sine (1000.0, 1.0, std::pow (10.0f, -6.0f / 20.0f) * std::sqrt (2.0f));   // -6 dB RMS
    run (*comp, tone);
    const double outDb = db (rms (tone.l, 36000));
    CHECK (outDb == Approx (-15.0).margin (0.6));   // -18 + 12 / 4
    CHECK (Compressor::cleanCurveDb (c, -6.0f) == Approx (-15.0f));
    CHECK (comp->reductionDb.load() == Approx (9.0f).margin (0.6));
    Buffer silence (24000);
    comp->reset();
    run (*comp, silence);
    CHECK (rms (silence.l) == 0.0);

    // FET: same input, a different (feedback, fixed-reference) result; COLOR ONLY: no reduction.
    c.mode = CompSettings::fet;
    comp = make<Compressor> (s);
    auto fet = sine (1000.0, 1.0, 0.5f);
    run (*comp, fet);
    CHECK (comp->reductionDb.load() > 3.0f);
    c.fetColorOnly = true;
    comp = make<Compressor> (s);
    auto colour = sine (1000.0, 0.5, 0.5f);
    run (*comp, colour);
    CHECK (comp->reductionDb.load() == 0.0f);
    c.fetColorOnly = false;
    c.fetRatio = 4;   // ALL
    comp = make<Compressor> (s);
    auto all = sine (1000.0, 1.0, 0.5f);
    run (*comp, all);
    CHECK (std::abs (rms (all.l, 36000) - rms (fet.l, 36000)) > 0.01 * rms (fet.l, 36000));
    CHECK (finite (all));

    // MULTIBAND, DEPTH 0: the bands sum back flat (an all-pass: the level, not the waveform).
    c = {};
    c.mode = CompSettings::multiband;
    c.depth = 0.0f;
    c.loudnessMatch = false;
    comp = make<Compressor> (s);
    for (const double hz : { 80.0, 600.0, 5000.0 })
    {
        comp->reset();
        auto t = sine (hz, 0.6, 0.2f);
        run (*comp, t);
        INFO (hz << " Hz");
        CHECK (db (toneAt (t.l, hz, 14400, 28800) / 0.2) == Approx (0.0).margin (0.1));
    }
    // Upward: a quiet tone is lifted; digital silence is not.
    c.depth = 1.0f;
    c.mbThresholdDb = -20.0f;
    c.mbUpRatio = 3.0f;
    comp = make<Compressor> (s);
    auto quiet = sine (600.0, 1.0, 0.01f);   // -40 dBFS
    run (*comp, quiet);
    CHECK (db (rms (quiet.l, 36000) * std::sqrt (2.0) / 0.01) > 6.0);
    comp->reset();
    Buffer zeros (24000);
    run (*comp, zeros);
    CHECK (rms (zeros.l) == 0.0);
}

TEST_CASE ("fx spread: WIDTH 0 is mono; HYPER detunes (a pitch, measured); DIMENSION widens; everything finite", "[fx][effects][spread]")
{
    Settings s;
    auto& sp = s.spread[0];
    sp.mode = SpreadSettings::hyper;
    sp.voices = 0;   // two voices: -DETUNE left, +DETUNE right
    sp.detuneCents = 30.0f;
    sp.hyperRateHz = 0.03f;
    sp.lowMono = false;
    sp.mix = 1.0f;
    auto spread = make<StereoSpread> (s);
    auto t = sine (1000.0, 1.0, 0.4f);
    run (*spread, t);
    const double lowSide = 1000.0 * std::exp2 (-30.0 / 1200.0), highSide = 1000.0 * std::exp2 (30.0 / 1200.0);
    CHECK (toneAt (t.l, lowSide, 12000, 48000) > 3.0 * toneAt (t.l, 1000.0, 12000, 48000));
    CHECK (toneAt (t.r, highSide, 12000, 48000) > 3.0 * toneAt (t.r, 1000.0, 12000, 48000));

    sp.mode = SpreadSettings::both;
    sp.width = 0.0f;
    spread = make<StereoSpread> (s);
    auto mono = noise (0.3);
    run (*spread, mono);
    for (int i = 0; i < mono.size(); i += 7)
        REQUIRE (mono.l[static_cast<std::size_t> (i)] == Approx (mono.r[static_cast<std::size_t> (i)]).margin (1.0e-5));

    // DIMENSION on a mono source: it comes out wider (correlation below 1), still finite.
    sp = {};
    sp.mode = SpreadSettings::dimension;
    sp.dimAmount = 1.0f;
    sp.mix = 1.0f;
    sp.lowMono = false;
    spread = make<StereoSpread> (s);
    auto centre = sine (440.0, 0.5, 0.3f);
    for (int i = 0; i < centre.size(); ++i)
        centre.l[static_cast<std::size_t> (i)] += 0.1f * static_cast<float> (std::sin (i * 0.37)), centre.r[static_cast<std::size_t> (i)] = centre.l[static_cast<std::size_t> (i)];
    run (*spread, centre);
    CHECK (finite (centre));
    CHECK (spread->correlation.load() < 0.98f);
}

TEST_CASE ("fx phaser and flanger: MIX 0 and DEPTH 0 nulls, notches where they say, extremes finite; through-zero reports its latency", "[fx][effects][motion]")
{
    Settings s;
    s.phaser[0].mix = 0.0f;
    auto ph = make<Phaser> (s);
    auto in = noise (0.2), out = in;
    run (*ph, out);
    for (int i = 0; i < out.size(); i += 13)
        REQUIRE (out.l[static_cast<std::size_t> (i)] == Approx (in.l[static_cast<std::size_t> (i)]).margin (1.0e-6));

    // A still phaser (DEPTH 0) at 50 %: its first notch where it says (deeply cut).
    s.phaser[0] = {};
    s.phaser[0].depth = 0.0f;
    s.phaser[0].feedback = 0.0f;
    s.phaser[0].stages = 1;   // 4
    s.phaser[0].centreHz = 1000.0f;
    ph = make<Phaser> (s);
    Buffer warm (4800);
    run (*ph, warm);
    const double notch = ph->notchHz[0].load();
    REQUIRE (notch > 100.0);
    auto atNotch = sine (notch, 0.5, 0.2f), away = sine (notch * 3.0, 0.5, 0.2f);
    run (*ph, atNotch);
    ph->reset();
    run (*ph, away);
    CHECK (toneAt (atNotch.l, notch, 12000, 24000) < 0.1 * toneAt (away.l, notch * 3.0, 12000, 24000));

    for (const float fb : { -0.85f, 0.85f })
    {
        s.phaser[0] = {};
        s.phaser[0].feedback = fb;
        s.phaser[0].stages = 4;
        s.phaser[0].mode = PhaserSettings::deep;
        s.flanger[0] = {};
        s.flanger[0].feedback = fb > 0 ? 0.92f : -0.92f;
        s.flanger[0].depth = 1.0f;
        s.flanger[0].rateHz = 10.0f;
        auto a = make<Phaser> (s);
        auto b = make<Flanger> (s);
        auto x = noise (1.0, 0.9f), y = noise (1.0, 0.9f);
        run (*a, x);
        run (*b, y);
        CHECK (finite (x));
        CHECK (finite (y));
        CHECK (rms (y.l) < 3.0);
    }
    s.flanger[0] = {};
    s.flanger[0].mix = 0.0f;
    auto fl = make<Flanger> (s);
    CHECK (fl->latency() == 0);
    out = in;
    run (*fl, out);
    for (int i = 0; i < out.size(); i += 13)
        REQUIRE (out.l[static_cast<std::size_t> (i)] == Approx (in.l[static_cast<std::size_t> (i)]).margin (1.0e-6));
    s.flanger[0].mode = FlangerSettings::throughZero;
    fl->set (s, 0);
    CHECK (fl->latency() == static_cast<int> (0.006 * rate));
}

TEST_CASE ("fx frequency shifter: +100 Hz moves 1 kHz to 1.1 kHz with the image at 900 Hz suppressed; BLUR is finite at its latency", "[fx][effects][shift]")
{
    Settings s;
    s.shifter[0].mode = ShifterSettings::shift;
    s.shifter[0].shiftHz = 100.0f;
    s.shifter[0].mix = 1.0f;
    auto sh = make<FrequencyShifter> (s);
    auto t = sine (1000.0, 1.0, 0.4f);
    run (*sh, t);
    const double up = toneAt (t.l, 1100.0, 12000, 48000), image = toneAt (t.l, 900.0, 12000, 48000), original = toneAt (t.l, 1000.0, 12000, 48000);
    CHECK (up > 0.3);
    CHECK (db (up / std::max (image, 1.0e-9)) > 30.0);
    CHECK (db (up / std::max (original, 1.0e-9)) > 30.0);
    CHECK (sh->latency() == FrequencyShifter::window);
    s.shifter[0].down = true;
    sh = make<FrequencyShifter> (s);
    auto d = sine (1000.0, 1.0, 0.4f);
    run (*sh, d);
    CHECK (toneAt (d.l, 900.0, 12000, 48000) > 10.0 * toneAt (d.l, 1100.0, 12000, 48000));

    s.shifter[0].mode = ShifterSettings::blur;
    s.shifter[0].blurAmount = 1.0f;
    sh = make<FrequencyShifter> (s);
    auto b = noise (1.0, 0.5f);
    const double inLevel = rms (b.l);
    run (*sh, b);
    CHECK (finite (b));
    CHECK (rms (b.l, 12000) == Approx (inLevel).epsilon (0.5));
}

TEST_CASE ("fx convolution: a unit impulse returns the input after its latency; a delayed one across partitions; factory impulses decay", "[fx][effects][conv]")
{
    Settings s;
    s.conv[0].mix = 1.0f;
    s.conv[0].normalize = false;
    auto conv = make<Convolution> (s);
    // An impulse response of a single 1 at 1000 samples (crosses partitions of 256).
    std::vector<std::vector<float>> ir (2, std::vector<float> (2000, 0.0f));
    ir[0][1000] = ir[1][1000] = 1.0f;
    conv->setImpulse (Impulse::make (ir, rate, s.conv[0], rate, 1, "test"));
    auto in = noise (0.5, 0.3f), out = in;
    run (*conv, out);
    const int lag = 1000 + Convolution::partition;
    // (out[i] = in[i - 1000 - latency]): compare exactly that.
    double err = 0.0;
    for (int i = lag + 10; i < out.size(); ++i)
        err = std::max (err, std::abs (static_cast<double> (out.l[static_cast<std::size_t> (i)]) - in.l[static_cast<std::size_t> (i - lag)]));
    CHECK (err < 1.0e-4);
    CHECK (conv->latency() == Convolution::partition);

    for (int k = 0; k < Impulse::factoryCount; ++k)
    {
        const auto samples = Impulse::factory (k, rate);
        REQUIRE (samples.size() == 2);
        const auto n = static_cast<int> (samples[0].size());
        INFO (Impulse::factoryName (k));
        CHECK (rms (samples[0], 0, n / 4) > 4.0 * rms (samples[0], 3 * n / 4, n));
        // Deterministic: the same twice.
        CHECK (Impulse::factory (k, rate)[1] == samples[1]);
    }
}

TEST_CASE ("fx engine: a new effect in a CUSTOM route plays; its ON glides it out; every type is implemented now", "[fx][effects][route]")
{
    for (int t = 0; t < typeCount; ++t)
        CHECK (implemented (static_cast<Type> (t)));
    auto p = testutil::measurementPatch();
    p.fx.eq[0].bands[1] = { true, EqBand::peak, 440.0f, 12.0f, 1.0f };
    auto plain = testutil::makeSynth (p);
    Graph g;
    g.custom = true;
    g.chain = { Node::effect ({ Type::eq, 0 }) };
    auto routed = testutil::makeSynth (p);
    routed->setFxGraph (compile (g, 1));
    const std::vector<testutil::Event> note { testutil::on (0, 69, 100) };
    const auto a = testutil::render (*plain, 0.5, note), b = testutil::render (*routed, 0.5, note);
    CHECK (toneAt (b.left, 440.0, 12000, 24000) > 3.0 * toneAt (a.left, 440.0, 12000, 24000));
    // ON off: back to the plain sound after the glide.
    p.fx.eq[0].on = false;
    routed->setPatch (p);
    const auto c = testutil::render (*routed, 0.3, {});
    const auto d = testutil::render (*plain, 0.3, {});
    CHECK (toneAt (c.left, 440.0, 7200, 14400) == Approx (toneAt (d.left, 440.0, 7200, 14400)).epsilon (0.05));
}
