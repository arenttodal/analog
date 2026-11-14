#include "EqSpectrum.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

EqSpectrum::EqSpectrum()
{
    buffer.resize (static_cast<std::size_t> (fftSize));
    for (int n = 0; n < fftSize; ++n)
        window[static_cast<std::size_t> (n)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (n) / static_cast<float> (fftSize - 1));
    postDb.fill (floorDb);
    preDb.fill (floorDb);
}

bool EqSpectrum::analyse (const fx::SpectrumTap& input, const fx::SpectrumTap& output)
{
    const int head = output.head.load (std::memory_order_acquire);
    const bool fresh = head != lastHead;
    lastHead = head;
    return transform (output, postDb, fresh) | transform (input, preDb, fresh);
}

float EqSpectrum::peakDb() const noexcept { return *std::max_element (postDb.begin(), postDb.end()); }

void EqSpectrum::paint (juce::Graphics& g, juce::Rectangle<float> plot, const std::function<double (float)>& hzForX, double rate, juce::Colour lineColour,
                        juce::Colour ink) const
{
    // The output filled (the device's ink), the input a quiet line.
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (plot.toNearestInt());
    auto spectrumPath = [&] (const std::array<float, fftSize / 2>& db, bool closed) {
        juce::Path path;
        bool any = false;
        const int x0 = static_cast<int> (plot.getX()), x1 = static_cast<int> (plot.getRight());
        for (int x = x0; x <= x1; x += 2)
        {
            const double f0 = hzForX (static_cast<float> (x)), f1 = hzForX (static_cast<float> (x + 2));
            const double kf0 = f0 * fftSize / rate, kf1 = f1 * fftSize / rate;
            float v = floorDb;
            if (kf1 - kf0 < 1.0)
            {
                // (the low end: bins wider than pixels - between two bins, a smooth
                // cosine blend, not steps)
                const double kf = juce::jlimit (1.0, static_cast<double> (fftSize / 2 - 2), 0.5 * (kf0 + kf1));
                const int k = static_cast<int> (kf);
                const float t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * static_cast<float> (kf - k));
                v = db[static_cast<std::size_t> (k)] + t * (db[static_cast<std::size_t> (k + 1)] - db[static_cast<std::size_t> (k)]);
            }
            else
            {
                const int k0 = juce::jlimit (1, fftSize / 2 - 1, static_cast<int> (kf0));
                const int k1 = juce::jlimit (k0, fftSize / 2 - 1, static_cast<int> (kf1));
                for (int k = k0; k <= k1; ++k)
                    v = std::max (v, db[static_cast<std::size_t> (k)]);
            }
            any = any || v > floorDb + 1.0f;
            const float y = plot.getBottom() - plot.getHeight() * (v - floorDb) / (0.0f - floorDb);
            if (x == x0)
                path.startNewSubPath (static_cast<float> (x), closed ? plot.getBottom() : y);
            path.lineTo (static_cast<float> (x), y);
        }
        if (closed)
        {
            path.lineTo (plot.getRight(), plot.getBottom());
            path.closeSubPath();
        }
        return any ? path : juce::Path();
    };
    const auto pre = spectrumPath (preDb, false);
    g.setColour (lineColour.withAlpha (0.35f));
    g.strokePath (pre, juce::PathStrokeType (1.0f));
    const auto post = spectrumPath (postDb, true);
    g.setGradientFill (juce::ColourGradient (ink.withAlpha (0.24f), 0.0f, plot.getY(), ink.withAlpha (0.04f), 0.0f, plot.getBottom(), false));
    g.fillPath (post);
    g.setColour (ink.withAlpha (0.45f));
    g.strokePath (post, juce::PathStrokeType (0.9f));
}

bool EqSpectrum::transform (const fx::SpectrumTap& tap, std::array<float, fftSize / 2>& out, bool fresh)
{
    bool moved = false;
    if (fresh)
    {
        const int head = tap.head.load (std::memory_order_acquire);
        for (int n = 0; n < fftSize; ++n)
        {
            const int at = (head - fftSize + n + fx::SpectrumTap::size) & (fx::SpectrumTap::size - 1);
            buffer[static_cast<std::size_t> (n)] = { tap.x[static_cast<std::size_t> (at)].load (std::memory_order_relaxed) * window[static_cast<std::size_t> (n)], 0.0f };
        }
        fft (buffer);
    }
    for (int k = 1; k < fftSize / 2; ++k)
    {
        auto& v = out[static_cast<std::size_t> (k)];
        float db = floorDb;
        if (fresh)
        {
            const float mag = std::abs (buffer[static_cast<std::size_t> (k)]) * 4.0f / static_cast<float> (fftSize);   // (Hann: x2, one-sided: x2)
            db = std::max (floorDb, 20.0f * std::log10 (mag + 1.0e-9f));
        }
        const float next = db > v ? db : std::max (db, v - 1.5f);   // (up at once, down 45 dB/s)
        if (std::abs (next - v) > 0.01f)
            moved = true;
        v = next;
    }
    return moved;
}
void EqSpectrum::fft (std::vector<std::complex<float>>& a)
{
    const int n = static_cast<int> (a.size());
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[static_cast<std::size_t> (i)], a[static_cast<std::size_t> (j)]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const float angle = -juce::MathConstants<float>::twoPi / static_cast<float> (len);
        const std::complex<float> wl (std::cos (angle), std::sin (angle));
        for (int i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k)
            {
                const auto u = a[static_cast<std::size_t> (i + k)], v = a[static_cast<std::size_t> (i + k + len / 2)] * w;
                a[static_cast<std::size_t> (i + k)] = u + v;
                a[static_cast<std::size_t> (i + k + len / 2)] = u - v;
                w *= wl;
            }
        }
    }
}

} // namespace andor::analog
