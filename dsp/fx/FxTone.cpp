// The FX library's insert base, UTILITY, EQUALIZER and FX FILTER.
#include "Effects.h"

#include <cstring>

namespace andor::analog::fx
{

using namespace dsp;

//==============================================================================
void InsertBase::prepare (double sampleRate, int maximumBlockSize)
{
    rate = sampleRate;
    maxBlock = std::max (1, maximumBlockSize);
    dryL.assign (static_cast<std::size_t> (maxBlock), 0.0f);
    dryR.assign (static_cast<std::size_t> (maxBlock), 0.0f);
    mixGlide.prepare (rate, 0.03);
    outGlide.prepare (rate, 0.03);
    mixGlide.jump (1.0f);
    outGlide.jump (1.0f);
    latencySamples = 0;
    prepareEffect();
    dryDelayL.prepare (latencySamples + 8);
    dryDelayR.prepare (latencySamples + 8);
    reset();
}

void InsertBase::setLatency (int samples) { latencySamples = std::max (0, samples); }

void InsertBase::reset() noexcept
{
    mixGlide.jump (mixGlide.target);
    outGlide.jump (outGlide.target);
    dryDelayL.reset();
    dryDelayR.reset();
    resetEffect();
}

void InsertBase::setCommon (const Common& c) noexcept
{
    mixGlide.set (std::clamp (c.mix, 0.0f, 1.0f));
    outGlide.set (dbToGain (c.outputDb));
}

void InsertBase::process (float* left, float* right, int numSamples) noexcept
{
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (maxBlock, numSamples - done);
        float* l = left + done;
        float* r = right + done;
        const bool full = ! mixGlide.moving() && mixGlide.value >= 1.0f;
        if (! full)
        {
            for (int i = 0; i < n; ++i)
            {
                if (latencySamples > 0)
                {
                    dryDelayL.push (l[i]);
                    dryDelayR.push (r[i]);
                    dryL[static_cast<std::size_t> (i)] = dryDelayL.readInteger (latencySamples + 1);
                    dryR[static_cast<std::size_t> (i)] = dryDelayR.readInteger (latencySamples + 1);
                }
                else
                {
                    dryL[static_cast<std::size_t> (i)] = l[i];
                    dryR[static_cast<std::size_t> (i)] = r[i];
                }
            }
        }
        else if (latencySamples > 0)
            for (int i = 0; i < n; ++i)
            {
                dryDelayL.push (l[i]);
                dryDelayR.push (r[i]);
            }
        processWet (l, r, n);
        const bool unityOut = ! outGlide.moving() && outGlide.value == 1.0f;
        if (full && unityOut)
        {
            if (scopeOn)
                for (int i = 0; i < n; ++i)
                    scope.push (l[i], r[i]);
            done += n;
            continue;   // (MIX 100 %, OUTPUT 0 dB: the effect's own output, untouched)
        }
        for (int i = 0; i < n; ++i)
        {
            const float m = mixGlide.next(), g = outGlide.next();
            const auto k = static_cast<std::size_t> (i);
            const float wl = full ? l[i] : dryL[k] + m * (l[i] - dryL[k]);
            const float wr = full ? r[i] : dryR[k] + m * (r[i] - dryR[k]);
            l[i] = g * wl;
            r[i] = g * wr;
            if (scopeOn)
                scope.push (l[i], r[i]);
        }
        done += n;
    }
}

//==============================================================================
// UTILITY: gain, balance, M / S width, polarity, mono bass (LR4), high / low pass. All
// neutral: the input passes untouched (bit for bit).

void Utility::prepareEffect()
{
    scopeOn = true;   // (its rack draws a goniometer)
    for (auto* g : { &gain, &panL, &panR, &width })
        g->prepare (rate, 0.02);
}

void Utility::resetEffect() noexcept
{
    for (auto& c : bass)
        c.reset();
    for (auto* set : { &hp, &lp })
        for (auto& f : *set)
            f.reset();
    gain.jump (dbToGain (p.gainDb));
    float bl, br;
    balance (p.pan, bl, br);
    panL.jump (bl);
    panR.jump (br);
    width.jump (p.width);
}

void Utility::set (const Settings& s, int slot) noexcept
{
    p = s.utility[static_cast<std::size_t> (slot)];
    setCommon (p);
    gain.set (dbToGain (std::clamp (p.gainDb, -36.0f, 24.0f)));
    float bl, br;
    balance (std::clamp (p.pan, -1.0f, 1.0f), bl, br);
    panL.set (bl);
    panR.set (br);
    width.set (std::clamp (p.width, 0.0f, 2.0f));
    if (p.monoBassHz != lastBassHz)
    {
        lastBassHz = p.monoBassHz;
        for (auto& c : bass)
            c.design (rate, std::clamp (static_cast<double> (p.monoBassHz), 40.0, 400.0));
    }
    if (p.hpHz != lastHp)
    {
        lastHp = p.hpHz;
        for (auto& f : hp)
            f.design (Biquad::Shape::highPass, rate, std::clamp (static_cast<double> (p.hpHz), 20.0, 200.0), 0.70710678, 0.0);
    }
    if (p.lpHz != lastLp)
    {
        lastLp = p.lpHz;
        for (auto& f : lp)
            f.design (Biquad::Shape::lowPass, rate, std::clamp (static_cast<double> (p.lpHz), 2000.0, 20000.0), 0.70710678, 0.0);
    }
}

void Utility::processWet (float* left, float* right, int n) noexcept
{
    const bool neutral = ! gain.moving() && gain.value == 1.0f && ! panL.moving() && ! panR.moving() && panL.value == 1.0f && panR.value == 1.0f
                         && ! width.moving() && width.value == 1.0f && ! p.invertLeft && ! p.invertRight && ! p.monoBass && ! p.hpOn && ! p.lpOn;
    for (int i = 0; i < n; ++i)
    {
        float l = left[i], r = right[i];
        if (! neutral)
        {
            if (p.hpOn)
            {
                l = hp[0].process (l);
                r = hp[1].process (r);
            }
            if (p.lpOn)
            {
                l = lp[0].process (l);
                r = lp[1].process (r);
            }
            if (p.monoBass)
            {
                float lowL, highL, lowR, highR;
                bass[0].split (l, lowL, highL);
                bass[1].split (r, lowR, highR);
                const float lowMono = 0.5f * (lowL + lowR);
                l = highL + lowMono;
                r = highR + lowMono;
            }
            const float w = width.next();
            if (w != 1.0f)
            {
                // M = (L + R) / 2, S = (L - R) / 2: width scales S; M untouched (no headroom change).
                const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
                l = m + s;
                r = m - s;
            }
            const float g = gain.next();
            l *= g * panL.next() * (p.invertLeft ? -1.0f : 1.0f);
            r *= g * panR.next() * (p.invertRight ? -1.0f : 1.0f);
            left[i] = l;
            right[i] = r;
        }
        sumLL += static_cast<double> (l) * l;
        sumRR += static_cast<double> (r) * r;
        sumLR += static_cast<double> (l) * r;
    }
    // Meters: levels and correlation over ~100 ms.
    constexpr double keep = 0.9;
    meterL.store (static_cast<float> (std::sqrt (sumLL / std::max (1, n))), std::memory_order_relaxed);
    meterR.store (static_cast<float> (std::sqrt (sumRR / std::max (1, n))), std::memory_order_relaxed);
    const double denominator = std::sqrt (sumLL * sumRR);
    if (denominator > 1.0e-12)
        correlation.store (static_cast<float> (sumLR / denominator), std::memory_order_relaxed);
    sumLL *= keep;
    sumRR *= keep;
    sumLR *= keep;
}

//==============================================================================
// EQUALIZER: four bands (each PEAK, SHELF, HP or LP) and optional high / low pass (12 or 24
// dB). Flat (gains 0, HP / LP off): every stage is skipped - the input passes untouched.

void Equalizer::prepareEffect() { dirty = true; }

void Equalizer::resetEffect() noexcept
{
    designed = p;
    design();
    for (auto& b : bands)
        for (auto& f : b)
            f.reset();
    for (auto* set : { &hpf, &lpf })
        for (auto& stage : *set)
            for (auto& f : stage)
                f.reset();
}

void Equalizer::set (const Settings& s, int slot) noexcept
{
    const auto& next = s.eq[static_cast<std::size_t> (slot)];
    setCommon (next);
    p = next;
}

void Equalizer::design() noexcept
{
    // Glide each band towards its target (frequency in octaves, gain in dB) a step per 32
    // samples, so automation sweeps rather than jumps.
    auto glide = [] (float now, float target, float maxStep) { return now + std::clamp (target - now, -maxStep, maxStep); };
    for (std::size_t b = 0; b < 4; ++b)
    {
        auto& d = designed.bands[b];
        const auto& t = p.bands[b];
        if (d.type != t.type || d.on != t.on)
            d = t;
        d.hz = std::exp2 (glide (std::log2 (std::max (10.0f, d.hz)), std::log2 (std::max (10.0f, t.hz)), 0.25f));
        d.gainDb = glide (d.gainDb, t.gainDb, 1.5f);
        d.q = glide (d.q, t.q, 0.5f);
        const bool flatShape = d.type == EqBand::peak || d.type == EqBand::lowShelf || d.type == EqBand::highShelf;
        bandActive[b] = d.on && ! (flatShape && std::abs (d.gainDb) < 1.0e-4f);
        const auto shape = d.type == EqBand::lowShelf ? Biquad::Shape::lowShelf
                         : d.type == EqBand::highShelf ? Biquad::Shape::highShelf
                         : d.type == EqBand::highPass ? Biquad::Shape::highPass
                         : d.type == EqBand::lowPass ? Biquad::Shape::lowPass : Biquad::Shape::peak;
        for (auto& f : bands[b])
            f.design (shape, rate, d.hz, std::clamp (d.q, 0.25f, 12.0f), d.gainDb);
    }
    designed.hpHz = std::exp2 (glide (std::log2 (std::max (10.0f, designed.hpHz)), std::log2 (std::max (10.0f, p.hpHz)), 0.25f));
    designed.lpHz = std::exp2 (glide (std::log2 (std::max (10.0f, designed.lpHz)), std::log2 (std::max (10.0f, p.lpHz)), 0.25f));
    hpStages = p.hpOn ? (p.hpSteep ? 2 : 1) : 0;
    lpStages = p.lpOn ? (p.lpSteep ? 2 : 1) : 0;
    for (auto& stage : hpf)
        for (auto& f : stage)
            f.design (Biquad::Shape::highPass, rate, std::clamp (static_cast<double> (designed.hpHz), 20.0, 500.0), 0.70710678, 0.0);
    for (auto& stage : lpf)
        for (auto& f : stage)
            f.design (Biquad::Shape::lowPass, rate, std::clamp (static_cast<double> (designed.lpHz), 1000.0, 20000.0), 0.70710678, 0.0);
    designed.hpOn = p.hpOn;
    designed.lpOn = p.lpOn;
    designed.hpSteep = p.hpSteep;
    designed.lpSteep = p.lpSteep;
}

void Equalizer::processWet (float* left, float* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        if (--countdown <= 0)
        {
            countdown = 32;
            design();
        }
        float l = left[i], r = right[i];
        for (int s = 0; s < hpStages; ++s)
        {
            l = hpf[static_cast<std::size_t> (s)][0].process (l);
            r = hpf[static_cast<std::size_t> (s)][1].process (r);
        }
        for (std::size_t b = 0; b < 4; ++b)
            if (bandActive[b])
            {
                l = bands[b][0].process (l);
                r = bands[b][1].process (r);
            }
        for (int s = 0; s < lpStages; ++s)
        {
            l = lpf[static_cast<std::size_t> (s)][0].process (l);
            r = lpf[static_cast<std::size_t> (s)][1].process (r);
        }
        left[i] = l;
        right[i] = r;
    }
}

double Equalizer::responseDb (double hz) const noexcept
{
    double m = 1.0;
    for (int s = 0; s < hpStages; ++s)
        m *= hpf[static_cast<std::size_t> (s)][0].magnitude (rate, hz);
    for (std::size_t b = 0; b < 4; ++b)
        if (bandActive[b])
            m *= bands[b][0].magnitude (rate, hz);
    for (int s = 0; s < lpStages; ++s)
        m *= lpf[static_cast<std::size_t> (s)][0].magnitude (rate, hz);
    return 20.0 * std::log10 (std::max (1.0e-9, m));
}

//==============================================================================
// FX FILTER: TPT SVF modes (12 dB; 24 dB as two stages), feedback combs, three-formant vowel
// banks. DRIVE: a tanh before the filter, its level restored. Its stereo PAN: balance after.

namespace
{
    // Vowel formants (Hz, F1 F2 F3) - three tables of two vowels each, designed for ANDOR from
    // textbook adult averages: I  A -> E, II  E -> I, III  O -> U.
    constexpr double vowels[3][2][3] { { { 730, 1090, 2440 }, { 530, 1840, 2480 } },
                                        { { 530, 1840, 2480 }, { 270, 2290, 3010 } },
                                        { { 570, 840, 2410 }, { 300, 870, 2240 } } };
}

std::array<double, 3> FxFilter::formants (int table, double morph, double shiftSemitones) noexcept
{
    const auto& v = vowels[std::clamp (table, 0, 2)];
    std::array<double, 3> f {};
    const double m = std::clamp (morph, 0.0, 1.0), shift = std::exp2 (shiftSemitones / 12.0);
    for (std::size_t i = 0; i < 3; ++i)
        f[i] = shift * std::exp (std::log (v[0][i]) + m * (std::log (v[1][i]) - std::log (v[0][i])));
    return f;
}

void FxFilter::prepareEffect()
{
    for (auto& c : comb)
        c.prepare (static_cast<int> (0.03 * rate) + 8);
    hz.prepare (rate, 0.02);
    drive.prepare (rate, 0.02);
}

void FxFilter::resetEffect() noexcept
{
    for (auto& stage : svf)
        for (auto& f : stage)
            f.reset();
    for (auto& c : comb)
        c.reset();
    combLow = {};
    hz.jump (std::clamp (p.hz, 20.0f, 20000.0f));
    drive.jump (p.driveDb);
    countdown = 0;
    design();   // (the graph is right before any sound)
}

void FxFilter::set (const Settings& s, int slot) noexcept
{
    p = s.filter[static_cast<std::size_t> (slot)];
    setCommon (p);
    hz.set (std::clamp (p.hz, 20.0f, 20000.0f));
    drive.set (std::clamp (p.driveDb, 0.0f, 24.0f));
}

void FxFilter::design() noexcept
{
    const double f = hz.value, reso = std::clamp (static_cast<double> (p.resonance), 0.0, 0.95);
    const double damping = 2.0 * (1.0 - reso) + 0.02;
    if (p.type >= FilterSettings::formant1)
    {
        const double morph = std::log (std::clamp (f, 20.0, 20000.0) / 20.0) / std::log (1000.0);
        const auto fs = formants (p.type - FilterSettings::formant1, morph, p.formantShift);
        for (std::size_t k = 0; k < 3; ++k)
            for (auto& s : svf[k])
                s.design (rate, fs[k], 0.12 + 0.2 * (1.0 - reso));   // narrow resonances
        return;
    }
    if (p.type == FilterSettings::combPlus || p.type == FilterSettings::combMinus)
    {
        combDelay = static_cast<float> (std::clamp (rate / std::clamp (f, 40.0, 2000.0), 0.0005 * rate, 0.025 * rate));
        combGain = static_cast<float> ((p.type == FilterSettings::combPlus ? 1.0 : -1.0) * 0.97 * reso);
        return;
    }
    // 24 dB: two stages, the resonance on the second (the first Butterworth-damped).
    const bool steep = p.type == FilterSettings::lp24 || p.type == FilterSettings::hp24;
    // (a 4-pole Butterworth's two dampings, 2 cos (pi/8) and 2 cos (3 pi/8); RESO narrows the second)
    for (auto& s : svf[0])
        s.design (rate, f, steep ? 1.84776 : damping);
    for (auto& s : svf[1])
        s.design (rate, f, steep ? 0.76537 * (1.0 - reso) + 0.02 : damping);
}

void FxFilter::processWet (float* left, float* right, int n) noexcept
{
    float bl, br;
    balance (std::clamp (p.pan, -1.0f, 1.0f), bl, br);
    for (int i = 0; i < n; ++i)
    {
        hz.next();
        if (--countdown <= 0 || hz.moving())
        {
            if (countdown <= 0)
                countdown = 16;
            design();
        }
        const float d = drive.next();
        const float pre = dbToGain (d);
        float io[2] { left[i], right[i] };
        for (int c = 0; c < 2; ++c)
        {
            float x = io[c];
            if (d > 0.01f)
                x = 0.25f * std::tanh (pre * x) / std::tanh (0.25f * pre);   // (calibrated: -12 dBFS stays -12 dBFS)
            float y = x;
            float low, band, high;
            switch (p.type)
            {
                case FilterSettings::lp12: svf[0][static_cast<std::size_t> (c)].process (x, low, band, high); y = low; break;
                case FilterSettings::hp12: svf[0][static_cast<std::size_t> (c)].process (x, low, band, high); y = high; break;
                case FilterSettings::bp12: svf[0][static_cast<std::size_t> (c)].process (x, low, band, high); y = band * static_cast<float> (svf[0][0].k); break;
                case FilterSettings::notch: svf[0][static_cast<std::size_t> (c)].process (x, low, band, high); y = low + high; break;
                case FilterSettings::allpass: svf[0][static_cast<std::size_t> (c)].process (x, low, band, high); y = low + high - static_cast<float> (svf[0][0].k) * band; break;
                case FilterSettings::lp24:
                    svf[0][static_cast<std::size_t> (c)].process (x, low, band, high);
                    svf[1][static_cast<std::size_t> (c)].process (low, low, band, high);
                    y = low;
                    break;
                case FilterSettings::hp24:
                    svf[0][static_cast<std::size_t> (c)].process (x, low, band, high);
                    svf[1][static_cast<std::size_t> (c)].process (high, low, band, high);
                    y = high;
                    break;
                case FilterSettings::combPlus:
                case FilterSettings::combMinus:
                {
                    auto& line = comb[static_cast<std::size_t> (c)];
                    auto& lowState = combLow[static_cast<std::size_t> (c)];
                    const float back = line.read (combDelay);
                    lowState += (1.0f - 0.85f * p.combDamping) * (back - lowState);   // damping in the loop
                    const float fed = x + combGain * lowState;
                    line.push (std::clamp (fed, -4.0f, 4.0f));
                    y = 0.5f * (x + fed) * (1.0f - 0.5f * std::abs (combGain));
                    break;
                }
                default:   // FORMANT: three resonances in parallel
                {
                    float sum = 0.0f;
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        svf[k][static_cast<std::size_t> (c)].process (x, low, band, high);
                        sum += band * (k == 0 ? 1.0f : (k == 1 ? 0.7f : 0.45f)) * static_cast<float> (svf[k][0].k);
                    }
                    y = 1.6f * sum;
                    break;
                }
            }
            io[c] = std::isfinite (y) ? y : 0.0f;
        }
        left[i] = io[0] * bl;
        right[i] = io[1] * br;
    }
}

double FxFilter::responseDb (double f) const noexcept
{
    using C = std::complex<double>;
    C h (1.0, 0.0);
    auto out = [&] (const Svf& s, int o) { return s.response (rate, f, o); };
    switch (p.type)
    {
        case FilterSettings::lp12: h = out (svf[0][0], 0); break;
        case FilterSettings::hp12: h = out (svf[0][0], 2); break;
        case FilterSettings::bp12: h = out (svf[0][0], 1) * svf[0][0].k; break;
        case FilterSettings::notch: h = out (svf[0][0], 0) + out (svf[0][0], 2); break;
        case FilterSettings::allpass: h = out (svf[0][0], 0) + out (svf[0][0], 2) - svf[0][0].k * out (svf[0][0], 1); break;
        case FilterSettings::lp24: h = out (svf[0][0], 0) * out (svf[1][0], 0); break;
        case FilterSettings::hp24: h = out (svf[0][0], 2) * out (svf[1][0], 2); break;
        case FilterSettings::combPlus:
        case FilterSettings::combMinus:
        {
            const C z = std::polar (1.0, -2.0 * pi * f / rate * combDelay);
            const double damp = 1.0 - 0.85 * p.combDamping;
            const C loop = static_cast<double> (combGain) * damp * z / (1.0 - (1.0 - damp) * std::polar (1.0, -2.0 * pi * f / rate));
            const C fed = 1.0 / (1.0 - loop);
            h = 0.5 * (1.0 + fed) * (1.0 - 0.5 * std::abs (combGain));
            break;
        }
        default:
        {
            C sum (0.0, 0.0);
            for (std::size_t k = 0; k < 3; ++k)
                sum += out (svf[k][0], 1) * svf[k][0].k * (k == 0 ? 1.0 : (k == 1 ? 0.7 : 0.45));
            h = 1.6 * sum;
            break;
        }
    }
    return 20.0 * std::log10 (std::max (1.0e-9, std::abs (h)));
}

} // namespace andor::analog::fx
