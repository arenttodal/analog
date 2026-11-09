// FREQUENCY SHIFTER (SHIFT: single-sideband by an analytic signal; BLUR: an STFT smear) and
// CONVOLUTION (uniform partitioned FFT convolution of an impulse prepared off the audio thread).
#include "Effects.h"

#include <cstring>

namespace andor::analog::fx
{

using namespace dsp;

namespace
{
    // A 90-degree phase-difference pair of all-pass chains (Olli Niemitalo's published
    // public-domain coefficients a; each section y = a^2 (x + y[n-2]) - x[n-2]): branch B gives
    // I, branch A one sample later gives Q, 90 degrees (+-0.7) apart from 30 Hz to 20 kHz at
    // 48 kHz (checked: the test's image rejection).
    constexpr std::array<float, 4> branchA { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
    constexpr std::array<float, 4> branchB { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };

    float allPassChain (const std::array<float, 4>& coefs, std::array<float, 16>& st, float x) noexcept
    {
        for (std::size_t s = 0; s < 4; ++s)
        {
            float* z = &st[4 * s];   // x1 x2 y1 y2
            const float y = coefs[s] * coefs[s] * (x + z[3]) - z[1];
            z[1] = z[0];
            z[0] = x;
            z[3] = z[2];
            z[2] = flushDenormal (y);
            x = y;
        }
        return x;
    }
}

//==============================================================================
void FrequencyShifter::prepareEffect()
{
    fft.prepare (window);
    hann.resize (window);
    for (int i = 0; i < window; ++i)
        hann[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * i / window));
    for (int c = 0; c < 2; ++c)
    {
        inRing[static_cast<std::size_t> (c)].assign (window, 0.0f);
        outAccum[static_cast<std::size_t> (c)].assign (window, 0.0f);
        smoothMag[static_cast<std::size_t> (c)].assign (window / 2 + 1, 0.0f);
        align[static_cast<std::size_t> (c)].prepare (window + 8);
    }
    spectrum.assign (window, {});
    setLatency (window);   // both modes: switching never moves the sound in time
}

void FrequencyShifter::resetEffect() noexcept
{
    apState = {};
    delayed = {};
    carrier = 0.0;
    fb = {};
    for (int c = 0; c < 2; ++c)
    {
        std::fill (inRing[static_cast<std::size_t> (c)].begin(), inRing[static_cast<std::size_t> (c)].end(), 0.0f);
        std::fill (outAccum[static_cast<std::size_t> (c)].begin(), outAccum[static_cast<std::size_t> (c)].end(), 0.0f);
        std::fill (smoothMag[static_cast<std::size_t> (c)].begin(), smoothMag[static_cast<std::size_t> (c)].end(), 0.0f);
        align[static_cast<std::size_t> (c)].reset();
    }
    ringPos = hopCount = 0;
    prng = osp::Prng (0x5ebd);
}

void FrequencyShifter::set (const Settings& s, int slot) noexcept
{
    p = s.shifter[static_cast<std::size_t> (slot)];
    setCommon (p);
}

float FrequencyShifter::hilbert (int c, float x, float& q) noexcept
{
    auto& st = apState[static_cast<std::size_t> (c)];
    const float a = allPassChain (branchA, st[0], x);
    const float i = allPassChain (branchB, st[1], x);
    q = delayed[static_cast<std::size_t> (c)];
    delayed[static_cast<std::size_t> (c)] = a;
    return i;
}

void FrequencyShifter::processWet (float* left, float* right, int n) noexcept
{
    if (p.mode == ShifterSettings::shift)
    {
        const double shift = std::clamp (static_cast<double> (p.shiftHz), 0.0, 2000.0) * (p.down ? -1.0 : 1.0);
        const float w = std::clamp (p.width, 0.0f, 2.0f);
        // The right side: as the left up to WIDTH 1, then towards the opposite shift at 2.
        const double rightShift = w <= 1.0f ? shift : shift * (3.0 - 2.0 * w);
        const float feedback = std::clamp (p.feedback, 0.0f, 0.8f);
        const double step = 2.0 * pi / rate;
        for (int i = 0; i < n; ++i)
        {
            carrier += step;
            if (carrier > 2.0 * pi * 1.0e6)
                carrier -= 2.0 * pi * 1.0e6;   // (whole cycles of both shifts, rounded: never a jump in practice)
            float io[2] { left[i], right[i] };
            for (int c = 0; c < 2; ++c)
            {
                const double theta = carrier * (c == 0 ? shift : rightShift);
                float q;
                const float x = io[c] + feedback * std::tanh (fb[static_cast<std::size_t> (c)]);
                const float in = hilbert (c, x, q);
                // I cos - Q sin: the upper sideband only (a single-sideband shift by `shift` Hz).
                const float y = static_cast<float> (in * std::cos (theta) - q * std::sin (theta));
                fb[static_cast<std::size_t> (c)] = y;
                io[c] = y;
            }
            if (w < 1.0f)
            {
                const float m = 0.5f * (io[0] + io[1]), s = 0.5f * (io[0] - io[1]) * w;
                io[0] = m + s;
                io[1] = m - s;
            }
            // Delayed to the BLUR window, so the two modes share one latency.
            align[0].push (io[0]);
            align[1].push (io[1]);
            left[i] = align[0].readInteger (window + 1);
            right[i] = align[1].readInteger (window + 1);
        }
        return;
    }
    // BLUR: frames of `window` every `hop` samples; each bin's magnitude smoothed over
    // BLUR TIME and its phase scattered by BLUR AMOUNT; Hann in and out, overlap-added.
    const float alpha = static_cast<float> (std::exp (-static_cast<double> (hop) / (std::max (10.0f, p.blurMs) * 0.001 * rate)));
    const float amount = std::clamp (p.blurAmount, 0.0f, 1.0f);
    // Scattered phases add incoherently between overlapping frames (and smoothed magnitudes
    // sit near the mean): measured on noise, ~-7 dB at AMOUNT 1 - restored here.
    const float restore = 1.0f + 1.3f * amount;
    for (int i = 0; i < n; ++i)
    {
        float io[2] { left[i], right[i] };
        for (int c = 0; c < 2; ++c)
            inRing[static_cast<std::size_t> (c)][static_cast<std::size_t> (ringPos)] = io[c];
        ringPos = (ringPos + 1) % window;
        if (++hopCount >= hop)
        {
            hopCount = 0;
            for (int c = 0; c < 2; ++c)
            {
                auto& ring = inRing[static_cast<std::size_t> (c)];
                for (int k = 0; k < window; ++k)
                    spectrum[static_cast<std::size_t> (k)] = { ring[static_cast<std::size_t> ((ringPos + k) % window)] * hann[static_cast<std::size_t> (k)], 0.0f };
                fft.transform (spectrum.data(), false);
                auto& mags = smoothMag[static_cast<std::size_t> (c)];
                for (int k = 0; k <= window / 2; ++k)
                {
                    const auto bin = spectrum[static_cast<std::size_t> (k)];
                    auto& m = mags[static_cast<std::size_t> (k)];
                    m = alpha * m + (1.0f - alpha) * std::abs (bin);
                    const float scatter = amount * static_cast<float> (pi) * (2.0f * static_cast<float> (prng.nextDouble()) - 1.0f);
                    const float phase = std::arg (bin) + scatter;
                    const auto out = std::polar (m, phase);
                    spectrum[static_cast<std::size_t> (k)] = out;
                    if (k > 0 && k < window / 2)
                        spectrum[static_cast<std::size_t> (window - k)] = std::conj (out);
                }
                fft.transform (spectrum.data(), true);
                auto& acc = outAccum[static_cast<std::size_t> (c)];
                for (int k = 0; k < window; ++k)
                    acc[static_cast<std::size_t> ((ringPos + k) % window)] += spectrum[static_cast<std::size_t> (k)].real() * hann[static_cast<std::size_t> (k)] * (2.0f / 3.0f) * restore;
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            auto& acc = outAccum[static_cast<std::size_t> (c)];
            io[c] = acc[static_cast<std::size_t> (ringPos)];
            acc[static_cast<std::size_t> (ringPos)] = 0.0f;
        }
        left[i] = io[0];
        right[i] = io[1];
    }
}

//==============================================================================
// CONVOLUTION.

const char* Impulse::factoryName (int index) noexcept
{
    static const char* names[factoryCount] { "Small Room", "Wood Hall", "Plate", "Spring Tank", "Stone Nave", "Body Resonator" };
    return names[std::clamp (index, 0, factoryCount - 1)];
}

std::vector<std::vector<float>> Impulse::factory (int index, double sampleRate)
{
    // Designed for ANDOR (not recordings of anything): seeded, so every machine makes the same.
    struct Design
    {
        double seconds, brightness, early, density;
        bool spring, modal;
    };
    const Design designs[factoryCount] { { 0.6, 0.55, 0.9, 1.0, false, false }, { 2.2, 0.4, 0.6, 1.0, false, false }, { 1.8, 0.8, 0.2, 1.0, false, false },
                                         { 2.5, 0.5, 0.3, 0.6, true, false },   { 4.0, 0.3, 0.5, 1.0, false, false }, { 0.45, 0.6, 0.0, 1.0, false, true } };
    const auto& d = designs[static_cast<std::size_t> (std::clamp (index, 0, factoryCount - 1))];
    const int length = static_cast<int> (d.seconds * sampleRate);
    std::vector<std::vector<float>> out (2, std::vector<float> (static_cast<std::size_t> (length), 0.0f));
    for (int c = 0; c < 2; ++c)
    {
        osp::Prng prng (osp::Prng::deriveSeed (0x1f00 + static_cast<std::uint64_t> (index), static_cast<std::uint64_t> (c), 7));
        float low = 0.0f;
        const double decay = std::log (1000.0) / d.seconds;   // -60 dB at the end
        for (int i = 0; i < length; ++i)
        {
            const double t = i / sampleRate;
            float noise = 2.0f * static_cast<float> (prng.nextDouble()) - 1.0f;
            // Darker as it decays (high frequencies die first).
            const float k = static_cast<float> (std::clamp (d.brightness * std::exp (-1.5 * t / d.seconds), 0.02, 1.0));
            low += k * (noise - low);
            float x = low * static_cast<float> (std::exp (-decay * t));
            if (d.spring)
                x *= static_cast<float> (0.6 + 0.4 * std::sin (2.0 * pi * (40.0 + 900.0 * std::exp (-6.0 * std::fmod (t, 0.031) / 0.031)) * t));
            if (d.modal)
            {
                x = 0.0f;
                for (const double f : { 196.0, 311.0, 452.0, 663.0, 980.0 })
                    x += static_cast<float> (std::sin (2.0 * pi * f * (1.0 + 0.002 * c) * t) * std::exp (-(3.0 + f / 200.0) * t));
                x *= 0.2f;
            }
            out[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = x;
        }
        // Early reflections: a few sparse taps in the first 60 ms.
        for (int r = 0; r < 8 && d.early > 0.0; ++r)
        {
            const auto at = static_cast<std::size_t> ((0.004 + 0.007 * r + 0.003 * prng.nextDouble()) * sampleRate);
            if (at < out[static_cast<std::size_t> (c)].size())
                out[static_cast<std::size_t> (c)][at] += static_cast<float> (d.early * 0.6 * std::pow (0.78, r) * (prng.nextDouble() > 0.5 ? 1.0 : -1.0));
        }
        out[static_cast<std::size_t> (c)][0] += 0.15f;   // (the direct sound, faint)
    }
    return out;
}

std::shared_ptr<const Impulse> Impulse::make (const std::vector<std::vector<float>>& samples, double sourceRate, const ConvSettings& s, double playRate,
                                              std::uint64_t generation, std::string name)
{
    auto ir = std::make_shared<Impulse>();
    ir->generation = generation;
    ir->sampleRate = playRate;
    ir->name = std::move (name);
    if (samples.empty() || samples[0].empty())
        return ir;
    constexpr double maxSeconds = 4.0;   // (a fixed ceiling: CPU and memory stay bounded)
    const double scale = std::clamp (static_cast<double> (s.size), 0.5, 2.0);   // SIZE: time-scaled
    const double ratio = sourceRate / playRate / scale;
    const int inLength = static_cast<int> (samples[0].size());
    const double decay = std::clamp (static_cast<double> (s.decaySeconds), 0.1, 20.0);
    const int length = std::min ({ static_cast<int> (inLength / ratio), static_cast<int> (maxSeconds * playRate), static_cast<int> (decay * playRate) });
    const bool truncated = length < static_cast<int> (inLength / ratio);
    const int fadeLength = std::max (1, std::min (length / 4, static_cast<int> (0.05 * playRate)));
    std::vector<std::vector<float>> data (2, std::vector<float> (static_cast<std::size_t> (std::max (1, length)), 0.0f));
    for (int c = 0; c < 2; ++c)
    {
        const auto& src = samples[std::min (samples.size() - 1, static_cast<std::size_t> (c))];
        for (int i = 0; i < length; ++i)
        {
            const double at = i * ratio;
            const int a = static_cast<int> (at);
            const double f = at - a;
            const float x0 = src[static_cast<std::size_t> (std::min (a, inLength - 1))], x1 = src[static_cast<std::size_t> (std::min (a + 1, inLength - 1))];
            float v = static_cast<float> (x0 + f * (x1 - x0));
            if (truncated && i > length - fadeLength)
                v *= static_cast<float> (length - i) / static_cast<float> (fadeLength);   // DECAY: truncated with a fade
            data[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = v;
        }
        if (s.reverse)
            std::reverse (data[static_cast<std::size_t> (c)].begin(), data[static_cast<std::size_t> (c)].end());
    }
    if (s.normalize)
    {
        double energy = 0.0;
        for (const auto& ch : data)
            for (const float v : ch)
                energy += static_cast<double> (v) * v;
        const float g = energy > 1.0e-12 ? static_cast<float> (1.0 / std::sqrt (0.5 * energy)) : 1.0f;
        for (auto& ch : data)
            for (auto& v : ch)
                v *= g;
    }
    ir->lengthSamples = length;
    ir->channels = 2;
    // The envelope for the graph: 256 points of peak level.
    ir->envelope.assign (256, 0.0f);
    for (int i = 0; i < length; ++i)
    {
        auto& e = ir->envelope[static_cast<std::size_t> (std::min (255, static_cast<int> (256.0 * i / std::max (1, length))))];
        e = std::max ({ e, std::abs (data[0][static_cast<std::size_t> (i)]), std::abs (data[1][static_cast<std::size_t> (i)]) });
    }
    // Partitions: each block of B samples, zero-padded to 2B, transformed.
    const int B = ir->partitionSize, count = (length + B - 1) / B;
    Fft fft;
    fft.prepare (2 * B);
    std::vector<std::complex<float>> buf (static_cast<std::size_t> (2 * B));
    for (int c = 0; c < 2; ++c)
    {
        auto& parts = ir->partitions[c];
        parts.resize (static_cast<std::size_t> (count));
        for (int k = 0; k < count; ++k)
        {
            std::fill (buf.begin(), buf.end(), std::complex<float> {});
            for (int i = 0; i < B && k * B + i < length; ++i)
                buf[static_cast<std::size_t> (i)] = data[static_cast<std::size_t> (c)][static_cast<std::size_t> (k * B + i)];
            fft.transform (buf.data(), false);
            parts[static_cast<std::size_t> (k)].assign (buf.begin(), buf.begin() + B + 1);
        }
    }
    return ir;
}

void Convolution::prepareEffect()
{
    if (exchangeStore == nullptr)
    {
        exchangeStore = std::make_unique<osp::ModelExchange<Impulse>>();
        exchange = exchangeStore.get();
    }
    fft.prepare (2 * partition);
    constexpr int maxPartitions = static_cast<int> (4.0 * 192000.0 / partition) + 2;
    for (int c = 0; c < 2; ++c)
    {
        inputBlock[static_cast<std::size_t> (c)].assign (2 * partition, 0.0f);
        outputBlock[static_cast<std::size_t> (c)].assign (partition, 0.0f);
        prevOutput[static_cast<std::size_t> (c)].assign (partition, 0.0f);
        const int parts = std::min (maxPartitions, static_cast<int> (4.0 * rate / partition) + 2);
        history[static_cast<std::size_t> (c)].assign (static_cast<std::size_t> (parts), std::vector<std::complex<float>> (partition + 1));
    }
    work.assign (2 * partition, {});
    acc.assign (partition + 1, {});
    preDelayL.prepare (static_cast<int> (0.26 * rate));
    preDelayR.prepare (static_cast<int> (0.26 * rate));
    widthGlide.prepare (rate, 0.03);
    setLatency (partition);
}

void Convolution::resetEffect() noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        std::fill (inputBlock[static_cast<std::size_t> (c)].begin(), inputBlock[static_cast<std::size_t> (c)].end(), 0.0f);
        std::fill (outputBlock[static_cast<std::size_t> (c)].begin(), outputBlock[static_cast<std::size_t> (c)].end(), 0.0f);
        std::fill (prevOutput[static_cast<std::size_t> (c)].begin(), prevOutput[static_cast<std::size_t> (c)].end(), 0.0f);
        for (auto& h : history[static_cast<std::size_t> (c)])
            std::fill (h.begin(), h.end(), std::complex<float> {});
    }
    historyPos = blockPos = 0;
    preDelayL.reset();
    preDelayR.reset();
    for (auto* set : { &lowCut, &highCut })
        for (auto& f : *set)
            f.reset();
    widthGlide.jump (p.width);
}

void Convolution::set (const Settings& s, int slot) noexcept
{
    p = s.conv[static_cast<std::size_t> (slot)];
    setCommon (p);
    widthGlide.set (std::clamp (p.width, 0.0f, 2.0f));
    if (p.lowCutHz != lastLow)
    {
        lastLow = p.lowCutHz;
        for (auto& f : lowCut)
            f.design (Biquad::Shape::highPass, rate, std::clamp (static_cast<double> (p.lowCutHz), 20.0, 2000.0), 0.70710678, 0.0);
    }
    if (p.highCutHz != lastHigh)
    {
        lastHigh = p.highCutHz;
        for (auto& f : highCut)
            f.design (Biquad::Shape::lowPass, rate, std::clamp (static_cast<double> (p.highCutHz), 1000.0, 20000.0), 0.70710678, 0.0);
    }
}

void Convolution::setImpulse (std::shared_ptr<const Impulse> impulse)
{
    if (exchange != nullptr && impulse != nullptr)
        exchange->publish (std::move (impulse));
}

void Convolution::collectGarbage()
{
    if (exchange != nullptr)
        exchange->collectGarbage();
}

std::shared_ptr<const Impulse> Convolution::impulse() const
{
    return exchange != nullptr ? exchange->latestModel() : nullptr;
}

void Convolution::convolve (const Impulse& ir, int channel, float* out) noexcept
{
    // Overlap-save: the sum over partitions of the input's past spectra times the impulse's.
    const auto& parts = ir.partitions[channel];
    const auto& hist = history[static_cast<std::size_t> (channel)];
    const int count = std::min (static_cast<int> (parts.size()), static_cast<int> (hist.size()));
    std::fill (acc.begin(), acc.end(), std::complex<float> {});
    for (int k = 0; k < count; ++k)
    {
        const auto& x = hist[static_cast<std::size_t> ((historyPos - k + static_cast<int> (hist.size())) % static_cast<int> (hist.size()))];
        const auto& h = parts[static_cast<std::size_t> (k)];
        for (int b = 0; b <= partition; ++b)
            acc[static_cast<std::size_t> (b)] += x[static_cast<std::size_t> (b)] * h[static_cast<std::size_t> (b)];
    }
    for (int b = 0; b <= partition; ++b)
        work[static_cast<std::size_t> (b)] = acc[static_cast<std::size_t> (b)];
    for (int b = 1; b < partition; ++b)
        work[static_cast<std::size_t> (2 * partition - b)] = std::conj (acc[static_cast<std::size_t> (b)]);
    fft.transform (work.data(), true);
    for (int i = 0; i < partition; ++i)
        out[i] = work[static_cast<std::size_t> (partition + i)].real();
}

void Convolution::processWet (float* left, float* right, int n) noexcept
{
    if (exchange != nullptr)
        if (const auto* next = exchange->takePending(); next != nullptr && next != current)
        {
            previous = current;
            current = next;
            fade = previous != nullptr ? static_cast<int> (0.05 * rate) : 0;
        }
    const int fadeLength = std::max (1, static_cast<int> (0.05 * rate));
    const float preDelay = static_cast<float> (std::clamp (static_cast<double> (p.preDelayMs), 0.0, 250.0) * 0.001 * rate);
    for (int i = 0; i < n; ++i)
    {
        preDelayL.push (left[i]);
        preDelayR.push (right[i]);
        const float in[2] { preDelay >= 1.0f ? preDelayL.read (preDelay) : left[i], preDelay >= 1.0f ? preDelayR.read (preDelay) : right[i] };
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            inputBlock[static_cast<std::size_t> (c)][static_cast<std::size_t> (partition + blockPos)] = in[c];
            const float w = fade > 0 ? static_cast<float> (fade) / static_cast<float> (fadeLength) : 0.0f;
            out[c] = outputBlock[static_cast<std::size_t> (c)][static_cast<std::size_t> (blockPos)] * (1.0f - w)
                     + prevOutput[static_cast<std::size_t> (c)][static_cast<std::size_t> (blockPos)] * w;
        }
        if (fade > 0)
            --fade;
        if (++blockPos >= partition)
        {
            blockPos = 0;
            historyPos = (historyPos + 1) % static_cast<int> (history[0].size());
            for (int c = 0; c < 2; ++c)
            {
                auto& block = inputBlock[static_cast<std::size_t> (c)];
                for (int k = 0; k < 2 * partition; ++k)
                    work[static_cast<std::size_t> (k)] = { block[static_cast<std::size_t> (k)], 0.0f };
                fft.transform (work.data(), false);
                auto& h = history[static_cast<std::size_t> (c)][static_cast<std::size_t> (historyPos)];
                std::copy (work.begin(), work.begin() + partition + 1, h.begin());
                // Slide: the block just taken becomes the first half of the next.
                std::memmove (block.data(), block.data() + partition, sizeof (float) * static_cast<std::size_t> (partition));
                if (current != nullptr && ! current->partitions[c].empty())
                    convolve (*current, c, outputBlock[static_cast<std::size_t> (c)].data());
                else
                    std::fill (outputBlock[static_cast<std::size_t> (c)].begin(), outputBlock[static_cast<std::size_t> (c)].end(), 0.0f);
                if (fade > 0 && previous != nullptr && ! previous->partitions[c].empty())
                    convolve (*previous, c, prevOutput[static_cast<std::size_t> (c)].data());
                else
                    std::fill (prevOutput[static_cast<std::size_t> (c)].begin(), prevOutput[static_cast<std::size_t> (c)].end(), 0.0f);
            }
        }
        // Tone on the wet, then WIDTH.
        float l = out[0], r = out[1];
        if (p.lowCutHz > 20.5f)   // (open: no filter at all)
        {
            l = lowCut[0].process (l);
            r = lowCut[1].process (r);
        }
        if (p.highCutHz < 19999.0f)
        {
            l = highCut[0].process (l);
            r = highCut[1].process (r);
        }
        const float w = widthGlide.next();
        if (w != 1.0f)
        {
            const float m = 0.5f * (l + r), s = 0.5f * (l - r) * w;
            l = m + s;
            r = m - s;
        }
        left[i] = l;
        right[i] = r;
    }
    if (current != nullptr)
        exchange->publishOldestInUse (previous != nullptr && fade > 0 ? std::min (previous->generation, current->generation) : current->generation);
}

} // namespace andor::analog::fx
