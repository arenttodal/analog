// GLOBAL MORPH 2.0 (docs/GLOBAL_MORPH_2.md): MORPH TO D's stage, headless. Synthetic stereo
// buses: S (a saw chord), K (its carrier part), D (a vowel-like source, a noise texture, a
// ticking loop). Every claim the stage's header makes is measured here.

#include "morph/GlobalSpectralMorph.h"
#include "fx/FxDsp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <vector>

using andor::analog::GlobalSpectralMorph;
using Method = GlobalSpectralMorph::Method;
using Output = GlobalSpectralMorph::Output;

namespace
{
    constexpr double rate = 48000.0;
    constexpr int block = 256;
    constexpr double twoPi = 2.0 * std::numbers::pi;

    struct Bus
    {
        std::vector<float> l, r;
        std::size_t size() const { return l.size(); }
    };

    Bus silence (double seconds)
    {
        const auto n = static_cast<std::size_t> (seconds * rate);
        return { std::vector<float> (n, 0.0f), std::vector<float> (n, 0.0f) };
    }

    /** A saw chord (C3 E3 G3 C4), band-limited by additive partials, slightly wide. */
    Bus sawChord (double seconds, double gain = 0.12)
    {
        auto b = silence (seconds);
        const std::array<double, 4> hz { 130.81, 164.81, 196.0, 261.63 };
        for (std::size_t i = 0; i < b.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate;
            double l = 0.0, r = 0.0;
            for (std::size_t v = 0; v < hz.size(); ++v)
                for (int h = 1; h * hz[v] < 12000.0; ++h)
                {
                    const double x = std::sin (twoPi * h * hz[v] * t + 0.3 * v) / h;
                    l += (v % 2 == 0 ? 1.0 : 0.8) * x;
                    r += (v % 2 == 0 ? 0.8 : 1.0) * x;
                }
            b.l[i] = static_cast<float> (gain * l);
            b.r[i] = static_cast<float> (gain * r);
        }
        return b;
    }

    /** A vowel-like D: a 110 Hz pulse train through formants at 700, 1200 and 2600 Hz. */
    Bus vowel (double seconds, double gain = 0.3, bool leftOnly = false)
    {
        auto b = silence (seconds);
        struct Resonator
        {
            double b1 = 0, b2 = 0, a = 0, y1 = 0, y2 = 0;
            Resonator (double hz, double bw)
            {
                const double r = std::exp (-std::numbers::pi * bw / rate);
                b1 = 2.0 * r * std::cos (twoPi * hz / rate);
                b2 = -r * r;
                a = 1.0 - r;
            }
            double process (double x)
            {
                const double y = a * x + b1 * y1 + b2 * y2;
                y2 = y1;
                y1 = y;
                return y;
            }
        };
        std::array<Resonator, 3> formants { Resonator (700.0, 80.0), Resonator (1200.0, 90.0), Resonator (2600.0, 120.0) };
        double phase = 0.0;
        for (std::size_t i = 0; i < b.size(); ++i)
        {
            phase += 110.0 / rate;
            const double pulse = phase >= 1.0 ? 1.0 : 0.0;
            phase -= std::floor (phase);
            double y = 0.0;
            for (auto& f : formants)
                y += f.process (pulse);
            b.l[i] = static_cast<float> (gain * 8.0 * y);
            b.r[i] = leftOnly ? 0.0f : b.l[i];
        }
        return b;
    }

    /** Noise (deterministic), stereo decorrelated, with a pink-ish tilt. */
    Bus noise (double seconds, double gain = 0.1)
    {
        auto b = silence (seconds);
        std::uint32_t s = 12345u;
        auto next = [&] { s = s * 1664525u + 1013904223u; return static_cast<double> (s >> 8) / 8388608.0 - 1.0; };
        double lowL = 0.0, lowR = 0.0;
        for (std::size_t i = 0; i < b.size(); ++i)
        {
            lowL += 0.2 * (next() - lowL);
            lowR += 0.2 * (next() - lowR);
            b.l[i] = static_cast<float> (gain * (lowL + 0.3 * next()));
            b.r[i] = static_cast<float> (gain * (lowR + 0.3 * next()));
        }
        return b;
    }

    /** Ticks: 2 kHz bursts, 30 ms, every 0.25 s. */
    Bus ticks (double seconds, double gain = 0.6)
    {
        auto b = silence (seconds);
        for (std::size_t i = 0; i < b.size(); ++i)
        {
            const double t = std::fmod (static_cast<double> (i) / rate, 0.25);
            const auto v = static_cast<float> (t < 0.03 ? gain * std::exp (-t / 0.006) * std::sin (twoPi * 2000.0 * t) : 0.0);
            b.l[i] = v;
            b.r[i] = v;
        }
        return b;
    }

    /** Runs the stage over whole buses (block by block); returns the output. */
    Bus run (GlobalSpectralMorph& g, const Bus& s, const Bus& k, const Bus& d, const GlobalSpectralMorph::Controls& c,
             const std::function<void (GlobalSpectralMorph&, std::size_t)>& each = {})
    {
        g.prepare (rate, block);
        g.setControls (c);
        g.reset();
        Bus out = s;
        for (std::size_t at = 0; at < s.size(); at += block)
        {
            const int n = static_cast<int> (std::min<std::size_t> (block, s.size() - at));
            if (each)
                each (g, at);
            g.process (out.l.data() + at, out.r.data() + at, k.l.data() + at, k.r.data() + at, d.l.data() + at, d.r.data() + at, n);
        }
        return out;
    }

    GlobalSpectralMorph::Controls controls (Method m, double p)
    {
        GlobalSpectralMorph::Controls c;
        c.method = m;
        c.position = p;
        return c;
    }

    /** Energy of a - b (delayed by `lag`) relative to b, dB, over [from, to). */
    double differenceDb (const Bus& a, const Bus& b, int lag, std::size_t from, std::size_t to)
    {
        double diff = 0.0, ref = 0.0;
        for (std::size_t i = from; i < to; ++i)
        {
            const auto j = i - static_cast<std::size_t> (lag);
            for (int ch = 0; ch < 2; ++ch)
            {
                const double x = ch == 0 ? a.l[i] : a.r[i], y = ch == 0 ? b.l[j] : b.r[j];
                diff += (x - y) * (x - y);
                ref += y * y;
            }
        }
        return 10.0 * std::log10 ((diff + 1.0e-30) / (ref + 1.0e-30));
    }

    double rms (const Bus& a, std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
            sum += 0.5 * (static_cast<double> (a.l[i]) * a.l[i] + static_cast<double> (a.r[i]) * a.r[i]);
        return std::sqrt (sum / std::max<std::size_t> (1, to - from));
    }

    /** A 48-band log magnitude spectrum (dB) of [from, from + 32768), Hann-windowed, both channels. */
    std::vector<double> spectrum (const Bus& a, std::size_t from)
    {
        constexpr int n = 32768;
        andor::analog::fx::dsp::Fft fft;
        fft.prepare (n);
        std::vector<double> bands (48, 0.0);
        for (int ch = 0; ch < 2; ++ch)
        {
            std::vector<std::complex<float>> x (n);
            for (int i = 0; i < n; ++i)
            {
                const float w = static_cast<float> (0.5 - 0.5 * std::cos (twoPi * i / n));
                x[static_cast<std::size_t> (i)] = w * (ch == 0 ? a.l[from + static_cast<std::size_t> (i)] : a.r[from + static_cast<std::size_t> (i)]);
            }
            fft.transform (x.data(), false);
            for (int k = 1; k < n / 2; ++k)
            {
                const double hz = k * rate / n;
                if (hz < 60.0 || hz >= 16000.0)
                    continue;
                const auto band = static_cast<std::size_t> (48.0 * std::log2 (hz / 60.0) / std::log2 (16000.0 / 60.0));
                bands[std::min<std::size_t> (band, 47)] += std::norm (x[static_cast<std::size_t> (k)]);
            }
        }
        for (auto& b : bands)
            b = 10.0 * std::log10 (b + 1.0e-12);
        return bands;
    }

    /** The mean absolute difference of two band spectra after removing their level offset (dB):
        a difference of spectral shape, not of loudness. */
    double shapeDistance (const std::vector<double>& a, const std::vector<double>& b)
    {
        double offset = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            offset += a[i] - b[i];
        offset /= static_cast<double> (a.size());
        double sum = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i)
            sum += std::abs (a[i] - b[i] - offset);
        return sum / static_cast<double> (a.size());
    }

    Bus crossfade (const Bus& s, const Bus& d, double p)
    {
        Bus out = s;
        const auto c = static_cast<float> (std::cos (0.5 * std::numbers::pi * p)), sn = static_cast<float> (std::sin (0.5 * std::numbers::pi * p));
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            out.l[i] = c * s.l[i] + sn * d.l[i];
            out.r[i] = c * s.r[i] + sn * d.r[i];
        }
        return out;
    }

    /** Least squares: out ~ a S + b D (both delayed by lag); the part left unexplained (dB re out). */
    double unexplainedDb (const Bus& out, const Bus& s, const Bus& d, int lag, std::size_t from, std::size_t to)
    {
        double ss = 0, dd = 0, sd = 0, os = 0, od = 0, oo = 0;
        for (std::size_t i = from; i < to; ++i)
        {
            const auto j = i - static_cast<std::size_t> (lag);
            for (int ch = 0; ch < 2; ++ch)
            {
                const double o = ch == 0 ? out.l[i] : out.r[i], x = ch == 0 ? s.l[j] : s.r[j], y = ch == 0 ? d.l[j] : d.r[j];
                ss += x * x; dd += y * y; sd += x * y; os += o * x; od += o * y; oo += o * o;
            }
        }
        const double det = ss * dd - sd * sd;
        const double a = (os * dd - od * sd) / det, b = (od * ss - os * sd) / det;
        const double residual = oo - a * os - b * od;
        return 10.0 * std::log10 (std::max (residual, 1.0e-30) / oo);
    }
}

TEST_CASE ("GLOBAL MORPH 2.0: the ends are the sources, sample for sample, as late as the stage says (every method, both outputs, LEVEL MATCH on and off)", "[gm2][unit]")
{
    const auto s = sawChord (1.5), d = vowel (1.5);
    GlobalSpectralMorph g;
    g.prepare (rate, block);
    const int lag = g.latencySamples();
    CHECK (lag == 1024);
    for (const auto m : { Method::imprint, Method::fuse, Method::weave })
        for (const auto o : { Output::morphOnly, Output::layered })
            for (const bool match : { true, false })
            {
                INFO ("method " << static_cast<int> (m) << " output " << static_cast<int> (o) << " match " << match);
                auto c = controls (m, 0.0);
                c.output = o;
                c.levelMatch = match;
                const auto zero = run (g, s, s, d, c);
                CHECK (differenceDb (zero, s, lag, 2048, s.size()) < -140.0);
                c.position = 1.0;
                const auto one = run (g, s, s, d, c);
                CHECK (differenceDb (one, d, lag, 2048, s.size()) < -140.0);
            }
}

TEST_CASE ("GLOBAL MORPH 2.0: the STFT reconstructs (D silent: the core is the carrier itself, within -80 dB)", "[gm2][unit]")
{
    const auto s = sawChord (1.0), d = silence (1.0);
    for (const auto m : { Method::imprint, Method::fuse, Method::weave })
    {
        GlobalSpectralMorph g;
        const auto out = run (g, s, s, d, controls (m, 0.5));
        // p 0.5, MORPH ONLY: b = 1, out = M; D silent (no presence): M = K = S.
        CHECK (differenceDb (out, s, g.latencySamples(), 4096, s.size()) < -80.0);
    }
}

TEST_CASE ("GLOBAL MORPH 2.0: at 50 % each method is its own sound - not a crossfade, not a weighted sum, not another method", "[gm2][unit]")
{
    const auto s = sawChord (2.0), d = vowel (2.0);
    GlobalSpectralMorph g;
    const int lag = 1024;
    const auto fade = crossfade (s, d, 0.5);
    std::array<std::vector<double>, 3> spectra;
    for (const auto m : { Method::imprint, Method::fuse, Method::weave })
    {
        auto c = controls (m, 0.5);
        c.levelMatch = false;
        const auto out = run (g, s, s, d, c);
        const auto spec = spectrum (out, static_cast<std::size_t> (lag) + 24000);
        const double vsFade = shapeDistance (spec, spectrum (fade, 24000));
        const double unexplained = unexplainedDb (out, s, d, lag, 24000, s.size());
        WARN ("method " << static_cast<int> (m) << ": spectral shape vs an equal-power crossfade " << vsFade << " dB (mean |diff|); unexplained by a S + b D "
                        << unexplained << " dB");
        CHECK (vsFade > 3.0);
        CHECK (unexplained > -10.0);   // (not a weighted sum of the inputs)
        spectra[static_cast<std::size_t> (m)] = spec;
    }
    const double imprintFuse = shapeDistance (spectra[0], spectra[1]), imprintWeave = shapeDistance (spectra[0], spectra[2]),
                 fuseWeave = shapeDistance (spectra[1], spectra[2]);
    WARN ("between methods (mean |dB|): IMPRINT-FUSE " << imprintFuse << ", IMPRINT-WEAVE " << imprintWeave << ", FUSE-WEAVE " << fuseWeave);
    CHECK (imprintFuse > 1.5);
    CHECK (imprintWeave > 1.5);
    CHECK (fuseWeave > 1.5);
}

TEST_CASE ("GLOBAL MORPH 2.0: IMPRINT takes D's spectral envelope (the synth's band levels move towards D's), keeps the synth's pitch", "[gm2][unit]")
{
    const auto s = sawChord (2.0), d = vowel (2.0);
    GlobalSpectralMorph g;
    auto c = controls (Method::imprint, 0.5);
    c.levelMatch = false;
    const auto out = run (g, s, s, d, c);
    const auto so = spectrum (s, 24000), dd = spectrum (d, 24000), o = spectrum (out, 24000 + 1024);
    CHECK (shapeDistance (o, dd) < shapeDistance (so, dd) - 2.0);   // (its shape nearer D's than the synth's was)
}

TEST_CASE ("GLOBAL MORPH 2.0: every control is heard - TEXTURE, ATTACK, FORMANTS, DETAIL, OUTPUT, LEVEL MATCH", "[gm2][unit]")
{
    GlobalSpectralMorph g;
    const int lag = 1024;
    auto distance = [&] (const Bus& s, const Bus& d, Method m, auto setA, auto setB) {
        auto a = controls (m, 0.5), b = controls (m, 0.5);
        setA (a);
        setB (b);
        const auto x = run (g, s, s, d, a), y = run (g, s, s, d, b);
        return differenceDb (x, y, 0, static_cast<std::size_t> (lag) + 4096, s.size());
    };
    const auto s = sawChord (2.0), v = vowel (2.0), n = noise (2.0), t = ticks (2.0);
    for (const auto m : { Method::imprint, Method::fuse, Method::weave })
    {
        INFO ("method " << static_cast<int> (m));
        const double texture = distance (s, n, m, [] (auto& c) { c.texture = 0.0; }, [] (auto& c) { c.texture = 1.0; });
        const double attack = distance (s, t, m, [] (auto& c) { c.attack = 0.0; }, [] (auto& c) { c.attack = 1.0; });
        const double formants = distance (s, v, m, [] (auto& c) { c.formants = -12.0; }, [] (auto& c) { c.formants = 12.0; });
        const double detail = distance (s, v, m, [] (auto& c) { c.detail = 0.0; }, [] (auto& c) { c.detail = 1.0; });
        const double output = distance (s, v, m, [] (auto& c) { c.output = Output::morphOnly; }, [] (auto& c) { c.output = Output::layered; });
        WARN ("method " << static_cast<int> (m) << " - change in dB re the first setting: TEXTURE 0/1 " << texture << ", ATTACK 0/1 " << attack << ", FORMANTS -12/+12 "
                        << formants << ", DETAIL 0/1 " << detail << ", OUTPUT " << output);
        CHECK (texture > -20.0);
        CHECK (attack > -30.0);
        CHECK (formants > -20.0);
        CHECK (detail > -30.0);
        CHECK (output > -20.0);
    }
    // LEVEL MATCH: a quiet D (-30 dB under the synth): on, 50 % sits between their loudness.
    const auto quiet = vowel (2.0, 0.01);
    auto on = controls (Method::imprint, 0.5), off = on;
    off.levelMatch = false;
    const auto matched = run (g, s, s, quiet, on), natural = run (g, s, s, quiet, off);
    const double sDb = 20.0 * std::log10 (rms (s, 24000, s.size())), dDb = 20.0 * std::log10 (rms (quiet, 24000, s.size()));
    const double onDb = 20.0 * std::log10 (rms (matched, 48000, s.size())), offDb = 20.0 * std::log10 (rms (natural, 48000, s.size()));
    WARN ("LEVEL MATCH: synth " << sDb << " dB, D " << dDb << " dB; IMPRINT 50 %: on " << onDb << " dB, off " << offDb << " dB");
    CHECK (std::abs (onDb - 0.5 * (sDb + dDb)) < 3.0);
    CHECK (std::abs (onDb - offDb) > 3.0);
}

TEST_CASE ("GLOBAL MORPH 2.0: FORMANTS moves D's envelope, not its pitch: +12 st raises the imprinted spectrum's centroid", "[gm2][unit]")
{
    const auto s = sawChord (2.0), d = vowel (2.0);
    GlobalSpectralMorph g;
    auto centroid = [&] (double st) {
        auto c = controls (Method::imprint, 0.5);
        c.formants = st;
        c.levelMatch = false;
        const auto out = run (g, s, s, d, c);
        const auto spec = spectrum (out, 24000 + 1024);
        double num = 0.0, den = 0.0;
        for (std::size_t b = 0; b < spec.size(); ++b)
        {
            const double p = std::pow (10.0, spec[b] / 10.0);
            num += p * static_cast<double> (b);
            den += p;
        }
        return num / den;
    };
    const double down = centroid (-12.0), flat = centroid (0.0), up = centroid (12.0);
    WARN ("IMPRINT 50 % spectral centroid (band index): FORMANTS -12 " << down << ", 0 " << flat << ", +12 " << up);
    CHECK (up > flat + 1.0);
    CHECK (down < flat - 1.0);
}

TEST_CASE ("GLOBAL MORPH 2.0: WEAVE gives regions to one source or the other, deterministically; FUSE blends every region", "[gm2][unit]")
{
    const auto s = sawChord (2.0), d = noise (2.0);
    GlobalSpectralMorph a, b;
    auto c = controls (Method::weave, 0.5);
    const auto x = run (a, s, s, d, c), y = run (b, s, s, d, c);
    CHECK (differenceDb (x, y, 0, 0, s.size()) < -200.0);   // (two instances: the same output, bit for bit)
    int toK = 0, toD = 0, between = 0;
    for (int band = 0; band < GlobalSpectralMorph::displayBands; ++band)
    {
        const float own = a.ownership (band);
        (own < 0.2f ? toK : (own > 0.8f ? toD : between)) += 1;
    }
    WARN ("WEAVE at 50 %: " << toK << " display bands owned by the synth, " << toD << " by D, " << between << " shared");
    CHECK (toK >= 6);
    CHECK (toD >= 6);
    auto f = controls (Method::fuse, 0.5);
    GlobalSpectralMorph fuse;
    run (fuse, s, s, d, f);
    CHECK (fuse.ownership (0) < 0.0f);   // (no ownership outside WEAVE)
}

TEST_CASE ("GLOBAL MORPH 2.0: stereo - a hard-left D stays left (100 %, and in FUSE / WEAVE at 50 %); IMPRINT keeps the synth's own image", "[gm2][unit]")
{
    const auto s = sawChord (1.5), d = vowel (1.5, 0.3, true), none = silence (1.5);
    GlobalSpectralMorph g;
    auto energy = [] (const std::vector<float>& x) {
        double e = 0.0;
        for (std::size_t i = 4096; i < x.size(); ++i)
            e += static_cast<double> (x[i]) * x[i];
        return e;
    };
    const auto one = run (g, s, s, d, controls (Method::fuse, 1.0));
    CHECK (energy (one.r) == 0.0);
    for (const auto m : { Method::fuse, Method::weave })
    {
        INFO ("method " << static_cast<int> (m));
        auto c = controls (m, 0.5);
        c.levelMatch = false;
        const auto dAlone = run (g, none, none, d, c);   // (the synth silent: D's part alone)
        CHECK (energy (dAlone.r) < 1.0e-10 * energy (dAlone.l));
    }
    // IMPRINT shapes the synth with one gain per bin for both channels: its balance is kept.
    auto c = controls (Method::imprint, 0.5);
    c.levelMatch = false;
    const auto half = run (g, s, s, d, c);
    const double balanceIn = 10.0 * std::log10 (energy (s.r) / energy (s.l)), balanceOut = 10.0 * std::log10 (energy (half.r) / energy (half.l));
    WARN ("IMPRINT 50 %, D hard left: the synth's balance R/L " << balanceIn << " dB in, " << balanceOut << " dB out");
    CHECK (std::abs (balanceOut - balanceIn) < 1.0);
}

TEST_CASE ("GLOBAL MORPH 2.0: robust - silence stays silence, non-finite input never reaches the output, extremes stay finite and bounded", "[gm2][unit]")
{
    GlobalSpectralMorph g;
    const auto quiet = silence (1.0);
    for (const auto m : { Method::imprint, Method::fuse, Method::weave })
    {
        const auto out = run (g, quiet, quiet, quiet, controls (m, 0.5));
        for (std::size_t i = 0; i < out.size(); ++i)
            REQUIRE (out.l[i] == 0.0f);
    }
    // 25 Hz and 15 kHz sines, noise, NaN bursts, every method, swept fast.
    auto low = silence (1.0), high = silence (1.0);
    for (std::size_t i = 0; i < low.size(); ++i)
    {
        low.l[i] = low.r[i] = static_cast<float> (0.5 * std::sin (twoPi * 25.0 * static_cast<double> (i) / rate));
        high.l[i] = high.r[i] = static_cast<float> (0.5 * std::sin (twoPi * 15000.0 * static_cast<double> (i) / rate));
    }
    auto broken = noise (1.0);
    for (std::size_t i = 10000; i < 10100; ++i)
        broken.l[i] = std::numeric_limits<float>::quiet_NaN();
    for (const auto m : { Method::imprint, Method::fuse, Method::weave })
        for (const auto* pair : { &low, &high, &broken })
        {
            auto c = controls (m, 0.0);
            const auto out = run (g, *pair, *pair, sawChord (1.0), c, [] (GlobalSpectralMorph& stage, std::size_t at) {
                auto now = stage.currentControls();
                now.position = 0.5 + 0.5 * std::sin (static_cast<double> (at) / 2000.0);
                now.formants = 12.0 * std::sin (static_cast<double> (at) / 3000.0);
                stage.setControls (now);
            });
            float peak = 0.0f;
            for (std::size_t i = 0; i < out.size(); ++i)
            {
                REQUIRE (std::isfinite (out.l[i]));
                REQUIRE (std::isfinite (out.r[i]));
                peak = std::max (peak, std::max (std::abs (out.l[i]), std::abs (out.r[i])));
            }
            CHECK (peak < 4.0f);
        }
}

TEST_CASE ("GLOBAL MORPH 2.0: switching METHOD while playing glides (no step larger than the music's own)", "[gm2][unit]")
{
    const auto s = sawChord (2.0), d = vowel (2.0);
    GlobalSpectralMorph g;
    auto c = controls (Method::imprint, 0.5);
    const auto steady = run (g, s, s, d, c);
    const auto switched = run (g, s, s, d, c, [] (GlobalSpectralMorph& stage, std::size_t at) {
        auto now = stage.currentControls();
        now.method = at < 48000 ? Method::imprint : (at < 72000 ? Method::weave : Method::fuse);
        stage.setControls (now);
    });
    auto maxStep = [] (const Bus& b, std::size_t from, std::size_t to) {
        float m = 0.0f;
        for (std::size_t i = from + 1; i < to; ++i)
            m = std::max (m, std::abs (b.l[i] - b.l[i - 1]));
        return m;
    };
    const float music = maxStep (steady, 20000, steady.size());
    CHECK (maxStep (switched, 47000, 50000 + 2048) < 1.5f * music);
    CHECK (maxStep (switched, 71000, 74000 + 2048) < 1.5f * music);
}

TEST_CASE ("GLOBAL MORPH 2.0: D between a loop's hits is held (no snap back between ticks); D long gone gives the synth back", "[gm2][unit]")
{
    const auto s = sawChord (5.0);
    auto d = ticks (5.0);
    for (std::size_t i = static_cast<std::size_t> (2.0 * rate); i < d.size(); ++i)
        d.l[i] = d.r[i] = 0.0f;   // (D stops at 2 s)
    GlobalSpectralMorph g;
    auto c = controls (Method::imprint, 0.5);
    std::vector<float> presence;
    run (g, s, s, d, c, [&] (GlobalSpectralMorph& stage, std::size_t) { presence.push_back (stage.presence()); });
    const auto at = [&] (double seconds) { return presence[static_cast<std::size_t> (seconds * rate / block)]; };
    CHECK (at (0.6) > 0.99f);    // (between ticks: held)
    CHECK (at (1.85) > 0.99f);
    CHECK (at (4.9) < 0.01f);    // (silent for longer than the hold (1.5 s) and its fall: the synth back)
}

TEST_CASE ("GLOBAL MORPH 2.0: CPU - one stereo stage at 44.1 / 48 / 96 kHz (WARN: real-time factor)", "[gm2][bench]")
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        GlobalSpectralMorph g;
        g.prepare (sr, 512);
        auto c = controls (Method::fuse, 0.5);
        g.setControls (c);
        const auto n = static_cast<std::size_t> (sr * 4.0);
        std::vector<float> l (n), r (n), kl (n), kr (n), dl (n), dr (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            l[i] = r[i] = kl[i] = kr[i] = static_cast<float> (0.2 * std::sin (0.01 * static_cast<double> (i)));
            dl[i] = dr[i] = static_cast<float> (0.2 * std::sin (0.031 * static_cast<double> (i)));
        }
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t at = 0; at < n; at += 512)
            g.process (l.data() + at, r.data() + at, kl.data() + at, kr.data() + at, dl.data() + at, dr.data() + at, static_cast<int> (std::min<std::size_t> (512, n - at)));
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        WARN (sr << " Hz: FFT " << g.fftSize() << ", latency " << g.latencySamples() << " samples (" << 1000.0 * g.latencySamples() / sr << " ms); 4 s of audio in "
                 << 1000.0 * seconds << " ms - " << 100.0 * seconds / 4.0 << " % of one core");
        CHECK (seconds < 4.0);
    }
}
