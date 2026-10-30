#include "morph/MorphAnalyzer.h"

#include "analysis/pitch/YinPitchTracker.h"
#include "core/Fft.h"
#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <deque>
#include <numbers>

// How the analysis works (and why), in pipeline order:
//
//  1. Content key (FNV-1a 64 over the samples' bits, the rate and analyzerVersion), then a
//     working copy with non-finite samples zeroed and the global mean (DC) removed, so a
//     DC offset never reads as level and a pure DC "recording" is reported as silent.
//  2. A uniform frame grid. Hop = 512 samples at 48 kHz (10.7 ms, scaled with the rate), so
//     an onset lands within ~5 ms of a frame centre; grown for long files so there are never
//     more than 2000 frames (memory and time stay bounded: ~1.4 MB of frames at most).
//  3. Per-frame RMS level (42 ms window) and YIN f0 + aperiodicity (osp::YinPitchTracker,
//     38 Hz .. 2.5 kHz, window centred on the frame). Octave guards: a frame with no dip
//     under YIN's threshold takes the shortest lag near the global minimum (sub-octave
//     errors in noise); a preliminary root (median of confident frames) then moves frames
//     an octave or two away from it to its octave when YIN's own difference function agrees
//     there. Frames where YIN fails at the file's edges borrow a confident neighbour's f0.
//  4. Pass 1, short window (4 periods of the preliminary root, 1024..4096 samples at 48 kHz):
//     spectral flux (transients) and spectral centroid (the attack settling).
//  5. Keyframes from level + centroid: onset (first 64-sample block within 50 dB of the
//     loudest frame), body (the attack's level has stopped rising and its centroid is steady
//     for 50 ms), the end of the sound (last frame within 50 dB).
//  6. Pass 2, multi-resolution STFT (Hann, 2x zero padding): the short window from the onset
//     to the body and around transients, a long window (6 periods, 4096..16384 samples at
//     48 kHz, i.e. >= 75 % overlap at the hop) for the sustain. Per frame, harmonics are
//     picked near h f0 sqrt(1 + B h^2) with parabolic (dB) interpolation; f0 and B are
//     re-fitted as the search climbs (weighted least squares of (f_h/h)^2 against h^2, which
//     is exactly linear in the stiff-string model), so the search follows inharmonic and
//     slightly mistuned series up to the 96th harmonic. Residual = the spectrum with the
//     found peaks' main lobes notched and their side lobes subtracted (the Hann shape), as
//     noise RMS per band. Envelope = the upper hull of the partial peaks, max-pooled into
//     48 bands, gaps interpolated in log frequency, lightly smoothed.
//  7. Voicing = YIN periodicity x harmonic energy share x a level gate. Root = median of
//     the body's strongly voiced f0 (log domain); confidence = level-weighted share of the
//     body that is voiced and within 100 cents of it.
//  8. Loop region: the longest stretch after the body whose level stays within 4 dB and
//     whose vibrato-averaged f0 stays within 50 cents (>= 150 ms to count as sustaining).
//
// Cost (measured by tests/MorphAnalysisTests.cpp "MORPH cost", Release, x86-64): a 10 s mono
// 48 kHz file gives 938 frames x 720 B = 660 KiB of profile and analyses in a few hundred ms.
// Transient memory is the working copy (4 bytes per sample) plus a few FFT buffers (< 1 MB).
// Frames never exceed 2000 (1.4 MB), whatever the length.
//
// Not realtime: this file allocates freely and is meant for a worker thread.

namespace andor::analog::morph
{
namespace
{
    using Frame = MorphProfile::Frame;
    constexpr int numPartials = MorphProfile::partials;

    constexpr double minF0Hz = 38.0;               // E1 is 41.2 Hz; lower costs a longer YIN window
    constexpr double maxF0Hz = 2500.0;
    constexpr double yinThreshold = 0.15;
    constexpr int maxAnalysisFrames = 2000;
    constexpr double baseHopSeconds = 512.0 / 48000.0;
    constexpr double levelWindowSeconds = 2048.0 / 48000.0;
    constexpr double tooShortSeconds = 0.04;       // shorter than one short analysis window
    constexpr double silentPeak = 1.0e-5;          // -100 dBFS after DC removal
    constexpr double floorDb = -120.0;
    constexpr double audibleDb = -50.0;            // re the loudest frame: onset and end of sound
    constexpr int zeroPad = 2;                     // FFT size = 2 x window: halves the interpolation bias
    constexpr double maxInharmonicity = 2.0e-3;

    double toDb (double x) noexcept { return x > 1.0e-6 ? 20.0 * std::log10 (x) : floorDb; }

    double smoothStep (double x, double lo, double hi) noexcept
    {
        const double t = std::clamp ((x - lo) / (hi - lo), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    double median (std::vector<double> v)
    {
        if (v.empty())
            return 0.0;
        std::sort (v.begin(), v.end());
        const auto mid = v.size() / 2;
        return v.size() % 2 == 1 ? v[mid] : 0.5 * (v[mid - 1] + v[mid]);
    }

    std::string contentKeyFor (const float* x, std::int64_t n, double sampleRate)
    {
        // Byte order is spelled out (little-endian words) so the key is the same on any host.
        std::uint64_t h = 0xcbf29ce484222325ull;
        const auto mix = [&h] (std::uint64_t word, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
            {
                h ^= (word >> (8 * i)) & 0xffu;
                h *= 0x100000001b3ull;
            }
        };
        for (std::int64_t i = 0; i < n; ++i)
        {
            std::uint32_t bits = 0;
            std::memcpy (&bits, x + i, sizeof bits);
            mix (bits, 4);
        }
        std::uint64_t rateBits = 0;
        std::memcpy (&rateBits, &sampleRate, sizeof rateBits);
        mix (rateBits, 8);
        mix (static_cast<std::uint64_t> (MorphProfile::analyzerVersion), 4);
        char text[24];
        std::snprintf (text, sizeof text, "%016llx", static_cast<unsigned long long> (h));
        return text;
    }

    int pow2AtLeast (double x) noexcept
    {
        int n = 1;
        while (n < x && n < (1 << 20))
            n <<= 1;
        return n;
    }

    /** Nearest power of two to `base` samples at 48 kHz, at this rate. */
    int scaledPow2 (int base, double sampleRate) noexcept
    {
        const double x = std::max (1.0, base * sampleRate / 48000.0);
        return std::max (256, 1 << static_cast<int> (std::lround (std::log2 (x))));
    }

    /**
        When no dip falls under YIN's threshold (noisy or breathy frames), the tracker reports
        the global minimum, which is as likely to sit at 2T or 3T as at the period T: cmnd is
        nearly equal at every multiple, so noise decides. The shortest lag whose local minimum
        is within 0.08 of the global one is the period (the sub-octave guard of pYIN-like
        multi-threshold trackers, in one step).
    */
    osp::YinFrame firstNearMinimum (const osp::YinPitchTracker& yin, osp::YinFrame frame)
    {
        const auto& cmnd = yin.lastCmnd();
        const int lo = yin.minLagValue(), hi = std::min (yin.maxLagValue(), static_cast<int> (cmnd.size()) - 2);
        if (hi <= lo + 1)
            return frame;
        double global = 1.0e9;
        for (int t = lo; t < hi; ++t)
            global = std::min (global, cmnd[static_cast<std::size_t> (t)]);
        for (int t = lo + 1; t < hi; ++t)
        {
            const double c = cmnd[static_cast<std::size_t> (t)];
            if (c > global + 0.08 || c > cmnd[static_cast<std::size_t> (t - 1)] || c > cmnd[static_cast<std::size_t> (t + 1)])
                continue;
            const double y0 = cmnd[static_cast<std::size_t> (t - 1)], y2 = cmnd[static_cast<std::size_t> (t + 1)];
            const double denom = y0 - 2.0 * c + y2;
            const double refined = t + (std::abs (denom) > 1.0e-12 ? std::clamp (0.5 * (y0 - y2) / denom, -0.5, 0.5) : 0.0);
            frame.hz = yin.rate() / refined;
            frame.aperiodicity = std::clamp (c, 0.0, 1.0);
            return frame;
        }
        return frame;
    }

    Frame silentFrame()
    {
        Frame f;
        f.envelopeDb.fill (static_cast<float> (floorDb));
        return f;
    }

    /** One windowed, zero-padded power spectrum at a time (owns its scratch). */
    struct Stft
    {
        Stft (int windowLength, double rate)
            : n (windowLength), fft (osp::Fft::orderForSize (windowLength * zeroPad)), m (fft.size()), binHz (rate / m)
        {
            window.resize (static_cast<std::size_t> (n));
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * i / n);
                window[static_cast<std::size_t> (i)] = w;
                sum += w;
                sumSquares += w * w;
            }
            buffer.resize (static_cast<std::size_t> (m));
            power.resize (static_cast<std::size_t> (m / 2 + 1));
        }

        void analyse (const float* x, std::int64_t length, std::int64_t centre)
        {
            const std::int64_t start = centre - n / 2;
            double energy = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const auto index = start + i;
                const double v = index >= 0 && index < length ? x[index] * window[static_cast<std::size_t> (i)] : 0.0;
                energy += v * v;
                buffer[static_cast<std::size_t> (i)] = { v, 0.0 };
            }
            std::fill (buffer.begin() + n, buffer.end(), std::complex<double> {});
            fft.forward (buffer.data());
            for (int b = 0; b <= m / 2; ++b)
                power[static_cast<std::size_t> (b)] = std::norm (buffer[static_cast<std::size_t> (b)]);
            framePower = energy / sumSquares;
        }

        /** Peak bin power -> sinusoid amplitude (the window's coherent gain undone). */
        double amplitude (double binPower) const noexcept { return 2.0 * std::sqrt (std::max (0.0, binPower)) / sum; }

        int n;
        osp::Fft fft;
        int m;
        double binHz;
        std::vector<double> window;
        double sum = 0.0, sumSquares = 0.0;
        std::vector<std::complex<double>> buffer;
        std::vector<double> power;
        double framePower = 0.0;   ///< mean square of the windowed frame / mean square of the window
    };

    struct PartialFit
    {
        double f0 = 0.0;
        double inharmonicity = -1.0;   ///< < 0: not enough strong partials to fit
        double harmonicPower = 0.0;
    };

    /** Picks the harmonic peaks of one spectrum; fills frame.amp, `freqs` and `present`. */
    PartialFit measurePartials (const Stft& s, double f0Guess, double bPrior, double sampleRate, Frame& frame,
                                std::array<double, numPartials>& freqs, std::array<bool, numPartials>& present)
    {
        frame.amp.fill (0.0f);
        freqs.fill (0.0);
        present.fill (false);
        PartialFit fit;
        const double limitHz = std::min (0.48 * sampleRate, 20000.0);
        const int last = s.m / 2;
        double a = f0Guess, b = bPrior;
        double sw = 0.0, sx = 0.0, sz = 0.0, sxx = 0.0, sxz = 0.0, szPrior = 0.0;
        int strong = 0, highest = 0;
        bool slopeFitted = false;
        double maxAmp = 0.0;

        for (int h = 1; h <= numPartials; ++h)
        {
            const double predicted = h * a * std::sqrt (1.0 + b * h * h);
            if (predicted > limitHz)
                break;
            // +/- a quarter of the spacing: wide enough for vibrato and a mistuned guess, and
            // narrower than half the spacing so a neighbour's main lobe is never inside.
            const double half = 0.25 * a;
            int lo = std::max (1, static_cast<int> (std::floor ((predicted - half) / s.binHz)));
            int hi = std::min (last - 1, static_cast<int> (std::ceil ((predicted + half) / s.binHz)));
            if (hi - lo < 2)
            {
                const int c = std::clamp (static_cast<int> (std::lround (predicted / s.binHz)), 2, last - 2);
                lo = c - 1;
                hi = c + 1;
            }
            int best = lo;
            for (int k = lo + 1; k <= hi; ++k)
                if (s.power[static_cast<std::size_t> (k)] > s.power[static_cast<std::size_t> (best)])
                    best = k;
            const bool peak = best > lo && best < hi;
            double amp = 0.0, freq = predicted;
            if (peak)
            {
                const double ya = 10.0 * std::log10 (s.power[static_cast<std::size_t> (best - 1)] + 1.0e-300);
                const double yb = 10.0 * std::log10 (s.power[static_cast<std::size_t> (best)] + 1.0e-300);
                const double yc = 10.0 * std::log10 (s.power[static_cast<std::size_t> (best + 1)] + 1.0e-300);
                const double denom = ya - 2.0 * yb + yc;
                const double offset = std::abs (denom) > 1.0e-12 ? std::clamp (0.5 * (ya - yc) / denom, -0.5, 0.5) : 0.0;
                const double peakDb = yb - 0.25 * (ya - yc) * offset;
                amp = s.amplitude (std::pow (10.0, peakDb / 10.0));
                freq = (best + offset) * s.binHz;
            }
            else
            {
                // No peak in range (only a neighbour's skirt): what is there is an upper bound.
                const int c = std::clamp (static_cast<int> (std::lround (predicted / s.binHz)), 1, last);
                amp = s.amplitude (s.power[static_cast<std::size_t> (c)]);
            }
            frame.amp[static_cast<std::size_t> (h - 1)] = static_cast<float> (amp);
            freqs[static_cast<std::size_t> (h - 1)] = freq;
            maxAmp = std::max (maxAmp, amp);
            if (! peak)
                continue;

            present[static_cast<std::size_t> (h - 1)] = true;
            fit.harmonicPower += 0.5 * amp * amp;

            // Strong peaks (within 40 dB of the loudest so far) steer f0 and B. Weak ones
            // (and window side lobes, ~-46 dB at worst here) would only add noise.
            if (amp < 0.01 * maxAmp || amp < 1.0e-6)
                continue;
            const double x = static_cast<double> (h) * h;
            const double z = (freq / h) * (freq / h);
            sw += amp;
            sx += amp * x;
            sz += amp * z;
            sxx += amp * x * x;
            sxz += amp * x * z;
            szPrior += amp * z / (1.0 + bPrior * x);
            ++strong;
            highest = h;
            double next = a;
            if (strong >= 4 && highest >= 6)
            {
                const double den = sw * sxx - sx * sx;
                const double slope = den > 1.0e-12 * sw * sxx ? (sw * sxz - sx * sz) / den : 0.0;
                const double intercept = (sz - slope * sx) / sw;
                if (slope > 0.0 && intercept > 0.0)
                {
                    b = std::min (slope / intercept, maxInharmonicity);
                    next = std::sqrt (intercept);
                }
                else
                {
                    b = 0.0;
                    next = std::sqrt (sz / sw);
                }
                slopeFitted = true;
            }
            else
            {
                next = std::sqrt (szPrior / sw);
            }
            // A fit that wandered more than ~70 cents from YIN is a bad fit, not a better f0.
            if (std::abs (std::log2 (next / f0Guess)) < 0.06)
                a = next;
        }

        fit.f0 = strong > 0 ? a : f0Guess;
        if (slopeFitted && strong >= 6 && highest >= 8)
            fit.inharmonicity = b;
        return fit;
    }

    /**
        Removes the found partials from a copy of the power spectrum: their main lobes are
        excluded outright, and their side lobes (Hann: -31 dB at 2.5 bins, falling 18 dB per
        octave) are subtracted from the window's own shape. Without the subtraction a pure
        sine "leaks" a residual only ~30 dB under itself, louder than a real bow's noise.
    */
    void notchPartials (const Stft& s, const Frame& frame, const std::array<double, numPartials>& freqs,
                        const std::array<bool, numPartials>& present, std::vector<double>& residualPower, std::vector<char>& excluded)
    {
        residualPower = s.power;
        std::fill (excluded.begin(), excluded.end(), 0);
        const int last = s.m / 2;
        constexpr double mainLobe = 2.5;     // bins: the main lobe (2) and a margin for vibrato smear
        constexpr double sideLobes = 16.0;   // bins: beyond this the side lobes are < -80 dB
        static_assert (zeroPad == 2, "the side-lobe recurrence below steps half a bin per FFT bin");
        for (int h = 0; h < numPartials; ++h)
        {
            if (! present[static_cast<std::size_t> (h)])
                continue;
            const double centre = freqs[static_cast<std::size_t> (h)] / s.binHz;
            const double peakPower = std::pow (0.5 * frame.amp[static_cast<std::size_t> (h)] * s.sum, 2.0);
            const int b0 = std::max (0, static_cast<int> (std::floor (centre - sideLobes * zeroPad)));
            const int b1 = std::min (last, static_cast<int> (std::ceil (centre + sideLobes * zeroPad)));
            // Hann: |W(x)| / W(0) = |sin(pi x)| / (pi |x| |1 - x^2|), x in bins. Stepping one padded
            // bin moves pi x by pi/2, so |sin(pi x)| alternates between |sin| and |cos| of the start.
            const double theta = std::numbers::pi * (b0 - centre) / zeroPad;
            const double sines[2] = { std::abs (std::sin (theta)), std::abs (std::cos (theta)) };
            for (int b = b0; b <= b1; ++b)
            {
                const double x = (b - centre) / zeroPad;
                const double ax = std::abs (x);
                if (ax <= mainLobe)
                {
                    excluded[static_cast<std::size_t> (b)] = 1;
                    continue;
                }
                const double shape = sines[(b - b0) & 1] / (std::numbers::pi * ax * (ax * ax - 1.0));
                auto& p = residualPower[static_cast<std::size_t> (b)];
                p = std::max (0.0, p - peakPower * shape * shape);
            }
        }
    }

    /** Noise RMS per residual band from `power` (the Stft's own, or a notched copy) with
        `excluded` bins skipped. */
    void measureResidual (const Stft& s, const std::vector<double>& power, const std::vector<char>* excluded, double sampleRate,
                          Frame& frame)
    {
        const int last = s.m / 2;
        const double nyquist = 0.5 * sampleRate;
        for (int band = 0; band < MorphProfile::residualBands; ++band)
        {
            const double lo = MorphProfile::residualBandEdgeHz (band);
            const double hi = std::min (MorphProfile::residualBandEdgeHz (band + 1), nyquist);
            float value = 0.0f;
            if (hi > lo)
            {
                const int b0 = std::clamp (static_cast<int> (std::ceil (lo / s.binHz)), 1, last);
                const int b1 = std::clamp (static_cast<int> (std::ceil (hi / s.binHz)) - 1, 1, last);
                const auto isExcluded = [excluded] (int b) { return excluded != nullptr && (*excluded)[static_cast<std::size_t> (b)] != 0; };
                double sum = 0.0;
                int count = 0;
                for (int b = b0; b <= b1; ++b)
                {
                    if (isExcluded (b))
                        continue;
                    sum += power[static_cast<std::size_t> (b)];
                    ++count;
                }
                double mean = 0.0;
                if (count > 0)
                {
                    mean = sum / count;
                }
                else
                {
                    // The band lies inside one partial's main lobe (narrow low bands): the floor
                    // just outside it, on both sides, is the best guess for the noise there.
                    int left = std::min (b0, b1) - 1, right = std::max (b0, b1) + 1;
                    while (left >= 1 && isExcluded (left))
                        --left;
                    while (right <= last && isExcluded (right))
                        ++right;
                    int found = 0;
                    if (left >= 1)
                        mean += power[static_cast<std::size_t> (left)], ++found;
                    if (right <= last)
                        mean += power[static_cast<std::size_t> (right)], ++found;
                    mean = found > 0 ? mean / found : 0.0;
                }
                // Noise of variance v spread over B of the m/2 bins has E|X|^2 = v_total sumSquares,
                // so the band's variance is mean * 2B / (m sumSquares) (B may be fractional).
                const double widthBins = (hi - lo) / s.binHz;
                const double variance = mean * 2.0 * widthBins / (s.m * s.sumSquares);
                value = static_cast<float> (std::sqrt (std::max (0.0, variance)));
            }
            frame.residual[static_cast<std::size_t> (band)] = value;
        }
    }

    /** Linear interpolation of (x, y) points (x ascending) at `x`, holding the ends. */
    double interpolatePoints (const std::vector<double>& xs, const std::vector<double>& ys, double x)
    {
        if (x <= xs.front())
            return ys.front();
        if (x >= xs.back())
            return ys.back();
        const auto it = std::upper_bound (xs.begin(), xs.end(), x);
        const auto i = static_cast<std::size_t> (it - xs.begin());
        const double t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
        return ys[i - 1] + t * (ys[i] - ys[i - 1]);
    }

    void smoothEnvelope (std::array<float, MorphProfile::envelopeBands>& env)
    {
        auto copy = env;
        for (int j = 0; j < MorphProfile::envelopeBands; ++j)
        {
            const float l = copy[static_cast<std::size_t> (std::max (0, j - 1))];
            const float r = copy[static_cast<std::size_t> (std::min (MorphProfile::envelopeBands - 1, j + 1))];
            env[static_cast<std::size_t> (j)] = std::max (static_cast<float> (floorDb), 0.25f * l + 0.5f * copy[static_cast<std::size_t> (j)] + 0.25f * r);
        }
    }

    void envelopeFromResidual (Frame& frame, double sampleRate)
    {
        std::vector<double> xs, ys;
        for (int b = 0; b < MorphProfile::residualBands; ++b)
        {
            if (MorphProfile::residualBandEdgeHz (b) >= 0.5 * sampleRate)
                break;
            xs.push_back (std::log2 (MorphProfile::residualBandCentreHz (b)));
            ys.push_back (toDb (frame.residual[static_cast<std::size_t> (b)]));
        }
        if (xs.empty())
        {
            frame.envelopeDb.fill (static_cast<float> (floorDb));
            return;
        }
        for (int j = 0; j < MorphProfile::envelopeBands; ++j)
            frame.envelopeDb[static_cast<std::size_t> (j)] = static_cast<float> (interpolatePoints (xs, ys, std::log2 (MorphProfile::envelopeBandCentreHz (j))));
        smoothEnvelope (frame.envelopeDb);
    }

    void envelopeFromPartials (Frame& frame, const std::array<double, numPartials>& freqs, const std::array<bool, numPartials>& present,
                               double sampleRate)
    {
        float maxAmp = 0.0f;
        for (int h = 0; h < numPartials; ++h)
            if (present[static_cast<std::size_t> (h)])
                maxAmp = std::max (maxAmp, frame.amp[static_cast<std::size_t> (h)]);
        std::vector<double> xs, ys;
        for (int h = 0; h < numPartials; ++h)
        {
            const float a = frame.amp[static_cast<std::size_t> (h)];
            if (present[static_cast<std::size_t> (h)] && a > 1.0e-6f && a >= maxAmp * 3.0e-4f)
            {
                xs.push_back (std::log2 (freqs[static_cast<std::size_t> (h)]));
                ys.push_back (toDb (a));
            }
        }
        // Upper hull, loosely: a partial far below the line through its neighbours is a gap in
        // the series (a triangle's even harmonics, a pluck position's node), not a formant valley.
        std::vector<double> hx, hy;
        for (std::size_t i = 0; i < xs.size(); ++i)
        {
            if (i > 0 && i + 1 < xs.size())
            {
                const double t = (xs[i] - xs[i - 1]) / std::max (1.0e-9, xs[i + 1] - xs[i - 1]);
                const double line = ys[i - 1] + t * (ys[i + 1] - ys[i - 1]);
                if (ys[i] < line - 12.0)
                    continue;
            }
            if (! hx.empty() && xs[i] <= hx.back())
                continue;   // keep x strictly ascending for the interpolation
            hx.push_back (xs[i]);
            hy.push_back (ys[i]);
        }
        if (hx.empty())
        {
            envelopeFromResidual (frame, sampleRate);
            return;
        }
        for (int j = 0; j < MorphProfile::envelopeBands; ++j)
        {
            const double lo = std::log2 (MorphProfile::envelopeBandEdgeHz (j));
            const double hi = std::log2 (MorphProfile::envelopeBandEdgeHz (j + 1));
            double best = -1.0e9;
            for (std::size_t i = 0; i < hx.size(); ++i)
                if (hx[i] >= lo && hx[i] < hi)
                    best = std::max (best, hy[i]);
            if (best < -1.0e8)
                best = interpolatePoints (hx, hy, 0.5 * (lo + hi));
            frame.envelopeDb[static_cast<std::size_t> (j)] = static_cast<float> (best);
        }
        smoothEnvelope (frame.envelopeDb);
    }

    const char* kindName (MorphProfile::Kind k)
    {
        switch (k)
        {
            case MorphProfile::Kind::pitched:   return "pitched";
            case MorphProfile::Kind::unpitched: return "unpitched";
            case MorphProfile::Kind::silent:    return "silent";
            case MorphProfile::Kind::tooShort:  return "too short";
        }
        return "?";
    }
} // namespace

MorphProfile::Frame MorphProfile::at (double seconds) const
{
    if (frames.empty())
        return {};
    if (frames.size() == 1 || ! (hopSeconds > 0.0) || ! std::isfinite (seconds))
        return frames.front();
    const double pos = std::clamp (seconds / hopSeconds, 0.0, static_cast<double> (frames.size() - 1));
    const auto i0 = static_cast<std::size_t> (std::floor (pos));
    const auto i1 = std::min (i0 + 1, frames.size() - 1);
    const double t = pos - static_cast<double> (i0);
    if (t <= 0.0 || i0 == i1)
        return frames[i0];
    const Frame& a = frames[i0];
    const Frame& b = frames[i1];
    const auto lerp = [t] (double x, double y) { return static_cast<float> (x + t * (y - x)); };
    // Log-domain with a -140 dB floor, so a partial fading in from nothing still interpolates.
    const auto logLerp = [t] (float x, float y)
    {
        constexpr double eps = 1.0e-7;
        if (x <= 0.0f && y <= 0.0f)
            return 0.0f;
        const double v = std::exp (std::log (std::max<double> (x, eps)) + t * (std::log (std::max<double> (y, eps)) - std::log (std::max<double> (x, eps))));
        return v <= eps * 1.0001 ? 0.0f : static_cast<float> (v);
    };
    Frame r;
    r.seconds = lerp (a.seconds, b.seconds);
    r.voicing = lerp (a.voicing, b.voicing);
    r.level = logLerp (a.level, b.level);
    if (a.f0Hz > 0.0f && b.f0Hz > 0.0f)
        r.f0Hz = static_cast<float> (std::exp (std::log (a.f0Hz) + t * (std::log (b.f0Hz) - std::log (a.f0Hz))));
    else
        r.f0Hz = t < 0.5 ? a.f0Hz : b.f0Hz;
    for (std::size_t i = 0; i < r.amp.size(); ++i)
        r.amp[i] = logLerp (a.amp[i], b.amp[i]);
    for (std::size_t i = 0; i < r.residual.size(); ++i)
        r.residual[i] = logLerp (a.residual[i], b.residual[i]);
    for (std::size_t i = 0; i < r.envelopeDb.size(); ++i)
        r.envelopeDb[i] = lerp (a.envelopeDb[i], b.envelopeDb[i]);
    return r;
}

MorphProfile analyse (const float* mono, std::int64_t numFrames, double sampleRate, const AnalysisOptions& options,
                      const std::atomic<bool>* cancel, const std::function<void (float)>& progress)
{
    MorphProfile p;
    p.sampleRate = sampleRate;
    p.numFrames = std::max<std::int64_t> (0, numFrames);

    float reported = -1.0f;
    const auto report = [&] (float value)
    {
        if (progress && (value >= reported + 0.01f || (value >= 1.0f && reported < 1.0f)))
        {
            reported = value;
            progress (value);
        }
    };
    const auto cancelled = [cancel] { return cancel != nullptr && cancel->load (std::memory_order_relaxed); };
    const auto cancelledProfile = [&p] (const char* stage)
    {
        MorphProfile c;
        c.sampleRate = p.sampleRate;
        c.numFrames = p.numFrames;
        c.status = std::string ("cancelled during ") + stage + ": nothing analysed";
        return c;
    };
    const auto applyOverride = [&options] (MorphProfile& profile)
    {
        if (options.rootOverrideMidi >= 0.0 && std::isfinite (options.rootOverrideMidi))
        {
            profile.rootMidi = std::clamp (options.rootOverrideMidi, 0.0, 135.0);
            profile.fundamentalHz = osp::midiToHz (profile.rootMidi);
            profile.rootConfidence = 1.0;
        }
    };

    if (! (sampleRate >= 1000.0 && sampleRate <= 1.0e6))
    {
        p.kind = MorphProfile::Kind::silent;
        char text[96];
        std::snprintf (text, sizeof text, "invalid sample rate (%g Hz): nothing analysed", sampleRate);
        p.status = text;
        p.frames.push_back (silentFrame());
        applyOverride (p);
        return p;
    }
    if (mono == nullptr || numFrames <= 0)
    {
        p.kind = MorphProfile::Kind::tooShort;
        p.status = "no samples: nothing analysed";
        p.frames.push_back (silentFrame());
        applyOverride (p);
        return p;
    }

    const std::int64_t n = numFrames;
    const double sr = sampleRate;
    const double duration = static_cast<double> (n) / sr;
    report (0.0f);
    p.contentKey = contentKeyFor (mono, n, sr);
    report (0.02f);
    if (cancelled())
        return cancelledProfile ("hashing");

    // ---- 1. Working copy: finite, DC removed ----------------------------------------------
    std::vector<float> work (static_cast<std::size_t> (n));
    double sum = 0.0, peak = 0.0;
    std::int64_t nonFinite = 0;
    for (std::int64_t i = 0; i < n; ++i)
    {
        const float x = mono[i];
        if (! std::isfinite (x))
        {
            ++nonFinite;
            continue;
        }
        sum += x;
        peak = std::max (peak, static_cast<double> (std::abs (x)));
    }
    // The mean of a fragment shorter than a window is part of its waveform, not an offset.
    const double mean = duration >= tooShortSeconds ? sum / static_cast<double> (n) : 0.0;
    double workPeak = 0.0;
    for (std::int64_t i = 0; i < n; ++i)
    {
        const float x = mono[i];
        const float v = std::isfinite (x) ? static_cast<float> (x - mean) : 0.0f;
        work[static_cast<std::size_t> (i)] = v;
        workPeak = std::max (workPeak, static_cast<double> (std::abs (v)));
    }
    p.peak = static_cast<float> (peak);
    const std::string nonFiniteNote = nonFinite > 0 ? "; " + std::to_string (nonFinite) + " non-finite samples zeroed" : "";

    if (workPeak < silentPeak)
    {
        p.kind = MorphProfile::Kind::silent;
        char text[160];
        if (peak >= silentPeak)
            std::snprintf (text, sizeof text, "silent: DC offset only (%.1f dBFS), no audible content", toDb (std::abs (mean)));
        else
            std::snprintf (text, sizeof text, "silent: peak %.1f dBFS", peak > 0.0 ? 20.0 * std::log10 (peak) : -999.0);
        p.status = text + nonFiniteNote;
        p.hopSeconds = duration;
        p.frames.push_back (silentFrame());
        applyOverride (p);
        report (1.0f);
        return p;
    }

    // ---- 2. Frame grid and levels ----------------------------------------------------------
    const std::int64_t hop = std::max<std::int64_t> ({ 1, std::llround (baseHopSeconds * sr),
                                                       (n + maxAnalysisFrames - 2) / (maxAnalysisFrames - 1) });
    const int count = static_cast<int> ((n - 1) / hop + 1);
    p.hopSeconds = static_cast<double> (hop) / sr;
    p.frames.assign (static_cast<std::size_t> (count), Frame {});
    const auto framesFor = [&p] (double seconds) { return std::max (1, static_cast<int> (std::lround (seconds / p.hopSeconds))); };

    const std::int64_t levelHalf = std::max<std::int64_t> (1, std::llround (0.5 * levelWindowSeconds * sr));
    std::vector<double> levelDb (static_cast<std::size_t> (count));
    double peakLevel = 0.0;
    for (int k = 0; k < count; ++k)
    {
        const std::int64_t c = k * hop;
        double e = 0.0;
        for (std::int64_t i = std::max<std::int64_t> (0, c - levelHalf); i < std::min (n, c + levelHalf); ++i)
            e += static_cast<double> (work[static_cast<std::size_t> (i)]) * work[static_cast<std::size_t> (i)];
        const double level = std::sqrt (e / (2.0 * levelHalf));
        p.frames[static_cast<std::size_t> (k)].level = static_cast<float> (level);
        p.frames[static_cast<std::size_t> (k)].seconds = static_cast<float> (k * p.hopSeconds);
        levelDb[static_cast<std::size_t> (k)] = toDb (level);
        peakLevel = std::max (peakLevel, level);
    }
    const double peakDb = toDb (peakLevel);
    report (0.05f);

    // ---- 3. YIN f0 per frame, preliminary root, octave guard -------------------------------
    osp::YinPitchTracker yin (sr, minF0Hz, std::min (maxF0Hz, 0.4 * sr), yinThreshold);
    // YinPitchTracker compares the window [c - L/2, c - L/2 + W) with its shifts up to maxLag;
    // asking for c + (maxLag + 2) / 2 centres that window on the frame instead of ~13 ms early.
    const std::int64_t yinOffset = (yin.maxLagValue() + 2) / 2;
    std::vector<double> yinHz (static_cast<std::size_t> (count), 0.0), yinAp (static_cast<std::size_t> (count), 1.0);
    for (int k = 0; k < count; ++k)
    {
        if (cancelled())
            return cancelledProfile ("pitch tracking");
        if (levelDb[static_cast<std::size_t> (k)] >= peakDb - 60.0)
        {
            auto r = yin.analyse (work.data(), n, k * hop + yinOffset);
            if (r.hz > 0.0 && r.aperiodicity >= yinThreshold && ! yin.lastFrameSilent())
                r = firstNearMinimum (yin, r);
            yinHz[static_cast<std::size_t> (k)] = r.hz;
            yinAp[static_cast<std::size_t> (k)] = r.aperiodicity;
        }
        report (0.05f + 0.35f * static_cast<float> (k + 1) / static_cast<float> (count));
    }

    double preliminaryRoot = 0.0;
    {
        std::vector<double> logs;
        for (int k = 0; k < count; ++k)
            if (yinHz[static_cast<std::size_t> (k)] > 0.0 && yinAp[static_cast<std::size_t> (k)] < 0.25
                && levelDb[static_cast<std::size_t> (k)] >= peakDb - 30.0)
                logs.push_back (std::log2 (yinHz[static_cast<std::size_t> (k)]));
        if (logs.size() >= 3)
            preliminaryRoot = std::exp2 (median (logs));
    }
    if (preliminaryRoot > 0.0)
    {
        for (int k = 0; k < count; ++k)
        {
            const double hz = yinHz[static_cast<std::size_t> (k)];
            if (! (hz > 0.0))
                continue;
            const double octaves = std::log2 (hz / preliminaryRoot);
            const double whole = std::round (octaves);
            if (whole == 0.0 || std::abs (whole) > 2.0 || std::abs (octaves - whole) > 0.08)
                continue;
            // Every multiple of a true period is periodic too, so for a frame above the root the
            // difference function cannot tell an error from a real leap: trust a confident frame.
            if (whole > 0.0 && yinAp[static_cast<std::size_t> (k)] < yinThreshold)
                continue;
            // YIN's own difference function decides: move only if the candidate period is periodic too.
            yin.analyse (work.data(), n, k * hop + yinOffset);
            if (yin.lastFrameSilent())
                continue;
            const auto& cmnd = yin.lastCmnd();
            const double lag = sr / (hz / std::exp2 (whole));
            const int lo = std::max (yin.minLagValue(), static_cast<int> (std::floor (lag * 0.97)));
            const int hi = std::min (yin.maxLagValue() - 1, static_cast<int> (std::ceil (lag * 1.03)));
            if (hi <= lo)
                continue;
            int best = lo;
            for (int t = lo + 1; t <= hi; ++t)
                if (cmnd[static_cast<std::size_t> (t)] < cmnd[static_cast<std::size_t> (best)])
                    best = t;
            const double value = cmnd[static_cast<std::size_t> (best)];
            if (value > std::max (0.25, yinAp[static_cast<std::size_t> (k)] + 0.1))
                continue;
            double refined = best;
            if (best > 1 && best + 1 < static_cast<int> (cmnd.size()))
            {
                const double y0 = cmnd[static_cast<std::size_t> (best - 1)], y1 = value, y2 = cmnd[static_cast<std::size_t> (best + 1)];
                const double denom = y0 - 2.0 * y1 + y2;
                if (std::abs (denom) > 1.0e-12)
                    refined = best + std::clamp (0.5 * (y0 - y2) / denom, -0.5, 0.5);
            }
            yinHz[static_cast<std::size_t> (k)] = sr / refined;
            yinAp[static_cast<std::size_t> (k)] = std::clamp (value, 0.0, 1.0);
        }
    }

    // YIN fails where its window hangs off either end of the recording (or straddles a hard
    // onset) even when the spectrum is clearly harmonic. Such frames borrow the f0 of a
    // confident neighbour within 60 ms and let the harmonic energy share decide; noise next
    // to a tone stays unvoiced because its share is far below the threshold.
    std::vector<char> borrowed (static_cast<std::size_t> (count), 0);
    {
        const int reach = framesFor (0.06);
        std::vector<double> sourceHz = yinHz, sourceAp = yinAp;
        for (int k = 0; k < count; ++k)
        {
            if (sourceHz[static_cast<std::size_t> (k)] > 0.0 && sourceAp[static_cast<std::size_t> (k)] < 0.45)
                continue;
            for (int d = 1; d <= reach; ++d)
            {
                int j = -1;
                if (k - d >= 0 && sourceHz[static_cast<std::size_t> (k - d)] > 0.0 && sourceAp[static_cast<std::size_t> (k - d)] < 0.2)
                    j = k - d;
                else if (k + d < count && sourceHz[static_cast<std::size_t> (k + d)] > 0.0 && sourceAp[static_cast<std::size_t> (k + d)] < 0.2)
                    j = k + d;
                if (j >= 0)
                {
                    yinHz[static_cast<std::size_t> (k)] = sourceHz[static_cast<std::size_t> (j)];
                    borrowed[static_cast<std::size_t> (k)] = 1;
                    break;
                }
            }
        }
    }

    // Window sizes follow the recording's pitch (enough periods to resolve the harmonics).
    int shortN = scaledPow2 (2048, sr), longN = scaledPow2 (4096, sr);
    if (preliminaryRoot > 0.0)
    {
        shortN = std::clamp (pow2AtLeast (4.0 * sr / preliminaryRoot), scaledPow2 (1024, sr), scaledPow2 (4096, sr));
        longN = std::clamp (pow2AtLeast (6.0 * sr / preliminaryRoot), scaledPow2 (4096, sr), scaledPow2 (16384, sr));
    }
    shortN = std::min (shortN, longN);

    // ---- 4. Pass 1: short window, flux and centroid ---------------------------------------
    Stft shortStft (shortN, sr);
    std::vector<double> flux (static_cast<std::size_t> (count), 0.0), centroidOct (static_cast<std::size_t> (count), 0.0);
    {
        std::vector<double> previous (static_cast<std::size_t> (shortStft.m / 2 + 1), 0.0);
        for (int k = 0; k < count; ++k)
        {
            if (cancelled())
                return cancelledProfile ("onset analysis");
            shortStft.analyse (work.data(), n, k * hop);
            double f = 0.0, weighted = 0.0, total = 0.0;
            for (int b = 1; b <= shortStft.m / 2; ++b)
            {
                const double power = shortStft.power[static_cast<std::size_t> (b)];
                // Log-compressed magnitude so quiet onsets count, half-wave rectified (rises only).
                const double c = std::log10 (1.0 + 1000.0 * shortStft.amplitude (power));
                f += std::max (0.0, c - previous[static_cast<std::size_t> (b)]);
                previous[static_cast<std::size_t> (b)] = c;
                weighted += b * shortStft.binHz * power;
                total += power;
            }
            flux[static_cast<std::size_t> (k)] = f;
            centroidOct[static_cast<std::size_t> (k)] = std::log2 (std::max (20.0, total > 0.0 ? weighted / total : 20.0));
            report (0.40f + 0.15f * static_cast<float> (k + 1) / static_cast<float> (count));
        }
    }

    // ---- 5. Keyframes: onset, end, body, transients ----------------------------------------
    const double audibleLevel = peakLevel * std::pow (10.0, audibleDb / 20.0);
    std::int64_t onsetSample = 0;
    {
        constexpr int block = 64;
        for (std::int64_t s = 0; s < n; s += block)
        {
            double e = 0.0;
            const auto end = std::min (n, s + block);
            for (auto i = s; i < end; ++i)
                e += static_cast<double> (work[static_cast<std::size_t> (i)]) * work[static_cast<std::size_t> (i)];
            if (std::sqrt (e / static_cast<double> (end - s)) >= audibleLevel)
            {
                onsetSample = s;
                break;
            }
        }
    }
    p.onsetFrame = std::clamp (static_cast<int> ((onsetSample + hop / 2) / hop), 0, count - 1);
    int lastAudible = p.onsetFrame;
    for (int k = count - 1; k >= p.onsetFrame; --k)
        if (p.frames[static_cast<std::size_t> (k)].level >= audibleLevel)
        {
            lastAudible = k;
            break;
        }

    {
        const int onset = p.onsetFrame;
        const int searchEnd = std::min (lastAudible, onset + framesFor (0.6));
        int loudest = onset;
        for (int k = onset; k <= searchEnd; ++k)
            if (levelDb[static_cast<std::size_t> (k)] > levelDb[static_cast<std::size_t> (loudest)])
                loudest = k;
        int attackPeak = onset;
        while (attackPeak < loudest && levelDb[static_cast<std::size_t> (attackPeak)] < levelDb[static_cast<std::size_t> (loudest)] - 2.0)
            ++attackPeak;
        const int limit = std::max (attackPeak, std::min ({ lastAudible, onset + framesFor (1.0), onset + (lastAudible - onset) / 2 }));
        const int settleFrames = std::max (2, framesFor (0.05));
        // Settled: the level has stopped rising (a decay is fine: plucks settle at once) and
        // the brightness is steady.
        const auto settled = [&] (int k)
        {
            for (int m = 1; m <= settleFrames && k + m <= lastAudible; ++m)
            {
                const auto j = static_cast<std::size_t> (k + m);
                if (levelDb[j] - levelDb[static_cast<std::size_t> (k)] > 1.0)
                    return false;
                if (std::abs (centroidOct[j] - centroidOct[static_cast<std::size_t> (k)]) > 0.2)
                    return false;
            }
            return true;
        };
        int body = attackPeak;
        while (body < limit && ! settled (body))
            ++body;
        p.bodyFrame = body;
    }

    {
        double maxFlux = 0.0;
        for (int k = p.onsetFrame; k <= lastAudible; ++k)
            maxFlux = std::max (maxFlux, flux[static_cast<std::size_t> (k)]);
        const int window = framesFor (0.1), minGap = framesFor (0.05);
        for (int k = 0; k < count && maxFlux > 0.0; ++k)
        {
            const double f = flux[static_cast<std::size_t> (k)];
            if (levelDb[static_cast<std::size_t> (k)] < peakDb - 40.0 || f < 0.25 * maxFlux)
                continue;
            if ((k > 0 && f < flux[static_cast<std::size_t> (k - 1)]) || (k + 1 < count && f <= flux[static_cast<std::size_t> (k + 1)]))
                continue;
            double local = 0.0;
            int localCount = 0;
            for (int j = std::max (0, k - window); j <= std::min (count - 1, k + window); ++j)
                if (j != k)
                {
                    local += flux[static_cast<std::size_t> (j)];
                    ++localCount;
                }
            if (localCount > 0 && f < 1.5 * local / localCount)
                continue;
            if (! p.transientFrames.empty() && k - p.transientFrames.back() < minGap)
            {
                if (f > flux[static_cast<std::size_t> (p.transientFrames.back())])
                    p.transientFrames.back() = k;
                continue;
            }
            p.transientFrames.push_back (k);
        }
    }

    // ---- 6. Pass 2: partials, residual, envelope ------------------------------------------
    Stft longStft (longN, sr);
    std::vector<char> shortExcluded (static_cast<std::size_t> (shortStft.m / 2 + 1)), longExcluded (static_cast<std::size_t> (longStft.m / 2 + 1));
    std::array<double, numPartials> freqs {};
    std::array<bool, numPartials> present {};
    std::vector<double> residualPower;
    std::vector<double> frameB (static_cast<std::size_t> (count), -1.0);
    std::vector<char> voiced (static_cast<std::size_t> (count), 0);
    std::vector<char> nearTransient (static_cast<std::size_t> (count), 0);
    for (int t : p.transientFrames)
        for (int j = std::max (0, t - 1); j <= std::min (count - 1, t + 1); ++j)
            nearTransient[static_cast<std::size_t> (j)] = 1;
    double bPrior = 0.0;
    for (int k = 0; k < count; ++k)
    {
        if (cancelled())
            return cancelledProfile ("partial analysis");
        auto& f = p.frames[static_cast<std::size_t> (k)];
        // The short window where the long one would straddle the onset or hang off the end of
        // the recording: a hard edge inside the window smears every partial into "noise".
        const std::int64_t centre = k * hop;
        const bool straddles = (centre >= onsetSample && centre - longN / 2 < onsetSample) || centre + longN / 2 > n;
        const bool useShort = (k >= p.onsetFrame && k < p.bodyFrame) || nearTransient[static_cast<std::size_t> (k)] != 0 || straddles;
        Stft& s = useShort ? shortStft : longStft;
        auto& excluded = useShort ? shortExcluded : longExcluded;
        s.analyse (work.data(), n, k * hop);

        const double gate = smoothStep (levelDb[static_cast<std::size_t> (k)], peakDb - 60.0, peakDb - 45.0);
        const double periodicity = borrowed[static_cast<std::size_t> (k)] != 0
                                       ? 0.85
                                       : std::clamp ((0.45 - yinAp[static_cast<std::size_t> (k)]) / 0.30, 0.0, 1.0);
        bool isVoiced = false;
        if (gate > 0.0 && periodicity > 0.0 && yinHz[static_cast<std::size_t> (k)] > 0.0)
        {
            const auto fit = measurePartials (s, yinHz[static_cast<std::size_t> (k)], bPrior, sr, f, freqs, present);
            const double harmonicity = s.framePower > 0.0 ? std::min (1.0, fit.harmonicPower / s.framePower) : 0.0;
            // Noise "partials" (the largest bin near each bogus harmonic) carry well under 30 %
            // of a frame's power; any tonal sound carries most of it.
            const double harmonicScore = std::clamp ((harmonicity - 0.3) / 0.3, 0.0, 1.0);
            f.voicing = static_cast<float> (gate * periodicity * harmonicScore);
            isVoiced = f.voicing >= 0.5f;
            if (isVoiced)
            {
                f.f0Hz = static_cast<float> (fit.f0);
                if (fit.inharmonicity >= 0.0)
                {
                    frameB[static_cast<std::size_t> (k)] = fit.inharmonicity;
                    bPrior = fit.inharmonicity;
                }
            }
            else
            {
                f.amp.fill (0.0f);
            }
        }
        voiced[static_cast<std::size_t> (k)] = isVoiced ? 1 : 0;
        if (isVoiced)
        {
            notchPartials (s, f, freqs, present, residualPower, excluded);
            measureResidual (s, residualPower, &excluded, sr, f);
        }
        else
        {
            measureResidual (s, s.power, nullptr, sr, f);
        }
        if (isVoiced)
            envelopeFromPartials (f, freqs, present, sr);
        else
            envelopeFromResidual (f, sr);
        report (0.55f + 0.43f * static_cast<float> (k + 1) / static_cast<float> (count));
    }

    // ---- 7. Root, confidence, kind ---------------------------------------------------------
    const int bodyStart = std::min (p.bodyFrame, lastAudible);
    {
        std::vector<double> logs;
        for (int k = bodyStart; k <= lastAudible; ++k)
            if (voiced[static_cast<std::size_t> (k)] && p.frames[static_cast<std::size_t> (k)].voicing >= 0.6f)
                logs.push_back (std::log2 (p.frames[static_cast<std::size_t> (k)].f0Hz));
        if (logs.size() < 3)
        {
            logs.clear();
            for (int k = 0; k < count; ++k)
                if (voiced[static_cast<std::size_t> (k)])
                    logs.push_back (std::log2 (p.frames[static_cast<std::size_t> (k)].f0Hz));
        }
        if (logs.size() >= 3)
        {
            p.fundamentalHz = std::exp2 (median (logs));
            p.rootMidi = osp::hzToMidi (p.fundamentalHz);
            double agree = 0.0, total = 0.0;
            for (int k = bodyStart; k <= lastAudible; ++k)
            {
                const auto& f = p.frames[static_cast<std::size_t> (k)];
                if (levelDb[static_cast<std::size_t> (k)] < peakDb - 40.0)
                    continue;
                total += f.level;
                if (voiced[static_cast<std::size_t> (k)] && std::abs (osp::centsBetween (p.fundamentalHz, f.f0Hz)) <= 100.0)
                    agree += f.level * f.voicing;
            }
            p.rootConfidence = total > 0.0 ? agree / total : 0.0;
        }
    }
    const bool pitched = p.fundamentalHz > 0.0 && p.rootConfidence >= 0.35;
    p.kind = pitched ? MorphProfile::Kind::pitched : MorphProfile::Kind::unpitched;
    if (! pitched)
        p.fundamentalHz = 0.0, p.rootMidi = 60.0;

    // ---- 8. Loop region --------------------------------------------------------------------
    {
        // Vibrato-averaged pitch (+/- 100 ms) so a singer's or a cellist's vibrato is "stable".
        std::vector<double> cents (static_cast<std::size_t> (count), 0.0);
        const int smooth = framesFor (0.1);
        for (int k = 0; k < count; ++k)
        {
            double s = 0.0;
            int m = 0;
            for (int j = std::max (0, k - smooth); j <= std::min (count - 1, k + smooth); ++j)
                if (voiced[static_cast<std::size_t> (j)])
                {
                    s += 1200.0 * std::log2 (p.frames[static_cast<std::size_t> (j)].f0Hz);
                    ++m;
                }
            cents[static_cast<std::size_t> (k)] = m > 0 ? s / m : 0.0;
        }
        // Pitch counts when the body is mostly voiced, pitched or not: a glide is voiced but
        // has no root, and must not be looped as if it were steady.
        int bodyVoiced = 0;
        for (int k = bodyStart; k <= lastAudible; ++k)
            bodyVoiced += voiced[static_cast<std::size_t> (k)];
        const bool usePitch = pitched || 2 * bodyVoiced >= lastAudible - bodyStart + 1;
        const auto valid = [&] (int k)
        {
            return levelDb[static_cast<std::size_t> (k)] >= peakDb - 30.0 && (! usePitch || voiced[static_cast<std::size_t> (k)]);
        };
        // Longest window with range(level) <= 4 dB and range(pitch) <= 50 cents: the condition
        // survives shrinking, so two pointers with monotonic deques find it in linear time.
        std::deque<int> levelMax, levelMin, pitchMax, pitchMin;
        int start = bodyStart, bestStart = 0, bestLength = 0;
        for (int k = bodyStart; k <= lastAudible; ++k)
        {
            if (! valid (k))
            {
                levelMax.clear(), levelMin.clear(), pitchMax.clear(), pitchMin.clear();
                start = k + 1;
                continue;
            }
            const auto push = [k] (std::deque<int>& d, const std::vector<double>& v, bool keepMax)
            {
                while (! d.empty() && (keepMax ? v[static_cast<std::size_t> (d.back())] <= v[static_cast<std::size_t> (k)]
                                               : v[static_cast<std::size_t> (d.back())] >= v[static_cast<std::size_t> (k)]))
                    d.pop_back();
                d.push_back (k);
            };
            push (levelMax, levelDb, true);
            push (levelMin, levelDb, false);
            push (pitchMax, cents, true);
            push (pitchMin, cents, false);
            const auto range = [] (const std::deque<int>& hiD, const std::deque<int>& loD, const std::vector<double>& v)
            { return v[static_cast<std::size_t> (hiD.front())] - v[static_cast<std::size_t> (loD.front())]; };
            while (range (levelMax, levelMin, levelDb) > 4.0 || (usePitch && range (pitchMax, pitchMin, cents) > 50.0))
            {
                ++start;
                for (auto* d : { &levelMax, &levelMin, &pitchMax, &pitchMin })
                    while (! d->empty() && d->front() < start)
                        d->pop_front();
            }
            if (k - start + 1 > bestLength)
            {
                bestLength = k - start + 1;
                bestStart = start;
            }
        }
        if (bestLength > 0 && (bestLength - 1) * p.hopSeconds >= 0.15)
        {
            const int trim = std::min (framesFor (0.02), bestLength / 8);
            p.sustains = true;
            p.loopStartFrame = bestStart + trim;
            p.loopEndFrame = bestStart + bestLength - 1 - trim;
        }
    }

    // ---- Summary values ---------------------------------------------------------------------
    {
        double e = 0.0;
        int m = 0;
        for (int k = bodyStart; k <= lastAudible; ++k)
        {
            e += static_cast<double> (p.frames[static_cast<std::size_t> (k)].level) * p.frames[static_cast<std::size_t> (k)].level;
            ++m;
        }
        p.loudnessDb = static_cast<float> (m > 0 ? toDb (std::sqrt (e / m)) : floorDb);

        std::vector<double> bs;
        for (int k = bodyStart; k <= lastAudible; ++k)
            if (voiced[static_cast<std::size_t> (k)] && frameB[static_cast<std::size_t> (k)] >= 0.0)
                bs.push_back (frameB[static_cast<std::size_t> (k)]);
        const double b = bs.size() >= 3 ? median (bs) : 0.0;
        p.inharmonicity = static_cast<float> (b < 1.0e-6 ? 0.0 : b);
    }

    if (duration < tooShortSeconds)
        p.kind = MorphProfile::Kind::tooShort;
    applyOverride (p);

    {
        char text[400];
        const int voicedCount = static_cast<int> (std::count (voiced.begin(), voiced.end(), 1));
        if (p.kind == MorphProfile::Kind::tooShort)
            std::snprintf (text, sizeof text, "too short: %.1f ms (< %.0f ms); %d frames, %d voiced", 1000.0 * duration,
                           1000.0 * tooShortSeconds, count, voicedCount);
        else if (pitched)
            std::snprintf (text, sizeof text, "pitched: root %s %+.0f cents (%.2f Hz, confidence %.2f), %d frames @ %.1f ms, %d voiced, body %.2f s, %s",
                           osp::midiNoteName (static_cast<int> (std::lround (osp::hzToMidi (p.fundamentalHz)))).c_str(),
                           100.0 * (osp::hzToMidi (p.fundamentalHz) - std::round (osp::hzToMidi (p.fundamentalHz))), p.fundamentalHz,
                           p.rootConfidence, count, 1000.0 * p.hopSeconds, voicedCount, p.bodyFrame * p.hopSeconds,
                           p.sustains ? "sustains" : "does not sustain");
        else
            std::snprintf (text, sizeof text, "%s: no stable pitch (confidence %.2f), %d frames @ %.1f ms, %d voiced, %s", kindName (p.kind),
                           p.rootConfidence, count, 1000.0 * p.hopSeconds, voicedCount, p.sustains ? "sustains" : "does not sustain");
        p.status = text + nonFiniteNote;
        if (options.rootOverrideMidi >= 0.0 && std::isfinite (options.rootOverrideMidi))
        {
            char extra[96];
            std::snprintf (extra, sizeof extra, "; root overridden to MIDI %.2f", p.rootMidi);
            p.status += extra;
        }
    }
    report (1.0f);
    return p;
}

} // namespace andor::analog::morph
