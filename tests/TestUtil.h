#pragma once

// Helpers for the engine's tests: renders with sample-placed MIDI, a "measurement" patch
// (no variation, no effects, the filter wide open), and simple signal measures.

#include "Synth.h"

#include "core/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace testutil
{
    using namespace andor::analog;

    struct Event
    {
        int sample = 0;
        std::uint8_t bytes[3] {};
        int size = 3;
    };
    inline Event on (int sample, int note, int velocity = 100, int channel = 1)
    {
        return { sample, { static_cast<std::uint8_t> (0x90 | (channel - 1)), static_cast<std::uint8_t> (note), static_cast<std::uint8_t> (velocity) }, 3 };
    }
    inline Event off (int sample, int note, int channel = 1)
    {
        return { sample, { static_cast<std::uint8_t> (0x80 | (channel - 1)), static_cast<std::uint8_t> (note), 0 }, 3 };
    }
    inline Event cc (int sample, int controller, int value)
    {
        return { sample, { 0xb0, static_cast<std::uint8_t> (controller), static_cast<std::uint8_t> (value) }, 3 };
    }
    inline Event bendTo (int sample, int value14)
    {
        return { sample, { 0xe0, static_cast<std::uint8_t> (value14 & 0x7f), static_cast<std::uint8_t> ((value14 >> 7) & 0x7f) }, 3 };
    }

    struct Stereo
    {
        std::vector<float> left, right;
        double rate = 48000.0;
        float peak() const
        {
            float p = 0.0f;
            for (std::size_t i = 0; i < left.size(); ++i)
                p = std::max ({ p, std::abs (left[i]), std::abs (right[i]) });
            return p;
        }
        double rms (double from, double to) const
        {
            const auto a = static_cast<std::size_t> (std::max (0.0, from * rate));
            const auto z = std::min (left.size(), static_cast<std::size_t> (to * rate));
            double sum = 0.0;
            for (std::size_t i = a; i < z; ++i)
                sum += 0.5 * (static_cast<double> (left[i]) * left[i] + static_cast<double> (right[i]) * right[i]);
            return z > a ? std::sqrt (sum / static_cast<double> (z - a)) : 0.0;
        }
        std::vector<float> mono() const
        {
            std::vector<float> m (left.size());
            for (std::size_t i = 0; i < m.size(); ++i)
                m[i] = 0.5f * (left[i] + right[i]);
            return m;
        }
        bool finite() const
        {
            for (std::size_t i = 0; i < left.size(); ++i)
                if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                    return false;
            return true;
        }
    };

    /** Renders `seconds` in blocks of `block`, applying each event at its sample. */
    inline Stereo render (Synth& synth, double seconds, std::vector<Event> events, int block = 256)
    {
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.sample < b.sample; });
        Stereo out;
        out.rate = synth.sampleRate();
        const auto total = static_cast<int> (seconds * out.rate);
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
                    synth.handleMidi (events[next].bytes, events[next].size);
                    ++next;
                }
                int until = end;
                if (next < events.size() && events[next].sample < until)
                    until = events[next].sample;
                synth.render (out.left.data() + at, out.right.data() + at, until - at);
                at = until;
            }
            pos = end;
        }
        return out;
    }

    /** A patch for measurements: one SAW on OSC 1, no variation, no movement, no effects, the
        filter fully open, the amplitude envelope a plain gate. */
    inline Patch measurementPatch()
    {
        Patch p;
        p.osc2Level = 0.0;
        p.blend = 0.0;
        p.analogAmount = 0.0;
        p.tone = 1.0;
        p.resonance = 0.0;
        p.keyTrack = 0.0;
        p.envDepthSemitones = 0.0;
        p.space = 0.0;
        p.echo = 0.0;
        p.ampAttack = 0.001;
        p.ampSustain = 1.0;
        p.ampRelease = 0.02;
        p.velocityRangeDb = 0.0;
        p.stereoSpread = 0.0;
        return p;
    }

    inline std::unique_ptr<Synth> makeSynth (const Patch& patch, double rate = 48000.0, int oversampling = 4, int block = 512)
    {
        auto s = std::make_unique<Synth>();
        s->prepare (rate, block, oversampling);
        s->setPatch (patch);
        s->reset();
        return s;
    }

    /** Frequency from rising zero crossings (linear interpolation) between two times. */
    inline double frequencyByCrossings (const std::vector<float>& x, double rate, double from, double to)
    {
        const auto a = static_cast<std::size_t> (from * rate), z = std::min (x.size(), static_cast<std::size_t> (to * rate));
        double first = -1.0, last = -1.0;
        int count = 0;
        for (std::size_t i = a + 1; i < z; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                const double t = static_cast<double> (i - 1) + x[i - 1] / static_cast<double> (x[i - 1] - x[i]);
                if (first < 0.0)
                    first = t;
                else
                    ++count;
                last = t;
            }
        return count > 0 ? rate * count / (last - first) : 0.0;
    }

    /** Magnitude spectrum (Blackman-Harris window) of x[from .. from + 2^order). */
    inline std::vector<double> spectrum (const std::vector<float>& x, std::size_t from, int order)
    {
        osp::Fft fft (order);
        const int n = fft.size();
        std::vector<std::complex<double>> data (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            const double t = 2.0 * 3.14159265358979323846 * i / (n - 1);
            const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
            data[static_cast<std::size_t> (i)] = x[from + static_cast<std::size_t> (i)] * w;
        }
        fft.forward (data.data());
        std::vector<double> mag (static_cast<std::size_t> (n / 2));
        for (int i = 0; i < n / 2; ++i)
            mag[static_cast<std::size_t> (i)] = std::abs (data[static_cast<std::size_t> (i)]);
        return mag;
    }

    /** Energy away from the harmonics of f0 (more than `guard` bins from any), relative to
        the total, in dB, within 0..`maxHz`. The aliasing measure. */
    inline double inharmonicDb (const std::vector<double>& mag, double rate, double f0, double maxHz, int guard = 6)
    {
        const double binHz = rate / (2.0 * static_cast<double> (mag.size()));
        double total = 0.0, stray = 0.0;
        for (std::size_t b = 1; b < mag.size(); ++b)
        {
            const double f = static_cast<double> (b) * binHz;
            if (f > maxHz)
                break;
            const double e = mag[b] * mag[b];
            total += e;
            const double h = std::round (f / f0);
            const double distance = std::abs (f - h * f0) / binHz;
            if (h < 1.0 || distance > guard)
                stray += e;
        }
        return 10.0 * std::log10 (std::max (stray, 1.0e-30) / std::max (total, 1.0e-30));
    }
}
