#include "RhythmAnalysis.h"

#include "core/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>

namespace andor::analog::rhythm
{

namespace
{
    constexpr double minBpm = 50.0, maxBpm = 220.0;

    /** Linear interpolation of a per-hop curve at a time (seconds); 0 outside. */
    double at (const std::vector<double>& curve, double seconds, double hop) noexcept
    {
        const double f = seconds / hop;
        if (f < 0.0 || f >= static_cast<double> (curve.size() - 1))
            return 0.0;
        const auto i = static_cast<std::size_t> (f);
        const double t = f - static_cast<double> (i);
        return curve[i] + t * (curve[i + 1] - curve[i]);
    }

    double acfAt (const std::vector<double>& acf, double lag) noexcept
    {
        if (lag < 0.0 || lag >= static_cast<double> (acf.size() - 1))
            return 0.0;
        const auto i = static_cast<std::size_t> (lag);
        const double t = lag - static_cast<double> (i);
        return acf[i] + t * (acf[i + 1] - acf[i]);
    }
}

Analysis analyse (const float* mono, std::size_t length, double sampleRate)
{
    Analysis a;
    a.sampleRate = std::isfinite (sampleRate) && sampleRate >= 1000.0 ? sampleRate : 48000.0;
    const double sr = a.sampleRate;
    a.durationSeconds = static_cast<double> (length) / sr;
    if (mono == nullptr || length == 0)
        return a;

    // A clean copy (no NaN / infinity) and its peak.
    std::vector<float> s (length);
    float peak = 0.0f;
    for (std::size_t i = 0; i < length; ++i)
    {
        const float v = std::isfinite (mono[i]) ? std::clamp (mono[i], -16.0f, 16.0f) : 0.0f;
        s[i] = v;
        peak = std::max (peak, std::abs (v));
    }

    const int hop = std::max (32, static_cast<int> (std::lround (sr * 0.005)));
    a.hopSeconds = hop / sr;
    const std::size_t frames = length / static_cast<std::size_t> (hop) + 1;

    // The level: RMS over two hops, centred.
    a.envelope.assign (frames, 0.0f);
    for (std::size_t n = 0; n < frames; ++n)
    {
        const auto c = static_cast<std::ptrdiff_t> (n) * hop;
        const auto from = std::max<std::ptrdiff_t> (0, c - hop), to = std::min<std::ptrdiff_t> (static_cast<std::ptrdiff_t> (length), c + hop);
        double sum = 0.0;
        for (auto i = from; i < to; ++i)
            sum += static_cast<double> (s[static_cast<std::size_t> (i)]) * s[static_cast<std::size_t> (i)];
        a.envelope[n] = to > from ? static_cast<float> (std::sqrt (sum / static_cast<double> (to - from))) : 0.0f;
        a.envelopePeak = std::max (a.envelopePeak, a.envelope[n]);
    }
    a.transient.assign (frames, 0.0f);
    if (peak < 1.0e-4f)
        return a;   // silent
    if (a.durationSeconds < 0.25)
    {
        a.kind = Kind::unsure;   // (too short to hear a pulse in)
        return a;
    }

    // Spectral flux on a log-compressed magnitude (~23 ms Hann), and its low band (kicks).
    const int order = osp::Fft::orderForSize (static_cast<int> (std::lround (sr * 0.023)));
    const int n = 1 << order;
    osp::Fft fft (order);
    std::vector<double> window (static_cast<std::size_t> (n));
    for (int k = 0; k < n; ++k)
        window[static_cast<std::size_t> (k)] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * k / n);
    const int maxBin = std::min (n / 2, static_cast<int> (16000.0 / sr * n));
    const int lowBin = std::max (2, static_cast<int> (120.0 / sr * n));
    std::vector<std::complex<double>> buffer (static_cast<std::size_t> (n));
    std::vector<double> previous (static_cast<std::size_t> (n / 2), 0.0), flux (frames, 0.0), lowFlux (frames, 0.0);
    for (std::size_t f = 0; f < frames; ++f)
    {
        const auto c = static_cast<std::ptrdiff_t> (f) * hop;
        for (int k = 0; k < n; ++k)
        {
            const auto i = c - n / 2 + k;
            const double v = i >= 0 && i < static_cast<std::ptrdiff_t> (length) ? s[static_cast<std::size_t> (i)] : 0.0;
            buffer[static_cast<std::size_t> (k)] = { v * window[static_cast<std::size_t> (k)], 0.0 };
        }
        fft.forward (buffer.data());
        double total = 0.0, low = 0.0;
        for (int k = 1; k < maxBin; ++k)
        {
            const double mag = std::log1p (1000.0 * std::abs (buffer[static_cast<std::size_t> (k)]) / n);
            const double d = mag - previous[static_cast<std::size_t> (k)];
            previous[static_cast<std::size_t> (k)] = mag;
            if (d > 0.0)
            {
                total += d;
                if (k < lowBin)
                    low += d;
            }
        }
        flux[f] = total;
        lowFlux[f] = low;
    }

    // Novelty: the flux above its local mean (±100 ms), normalised.
    const auto meanSpan = static_cast<std::ptrdiff_t> (std::lround (0.1 / a.hopSeconds));
    std::vector<double> novelty (frames, 0.0), lowNovelty (frames, 0.0);
    {
        std::vector<double> prefix (frames + 1, 0.0), lowPrefix (frames + 1, 0.0);
        for (std::size_t f = 0; f < frames; ++f)
        {
            prefix[f + 1] = prefix[f] + flux[f];
            lowPrefix[f + 1] = lowPrefix[f] + lowFlux[f];
        }
        double top = 0.0, lowTop = 0.0;
        for (std::size_t f = 0; f < frames; ++f)
        {
            const auto lo = static_cast<std::size_t> (std::max<std::ptrdiff_t> (0, static_cast<std::ptrdiff_t> (f) - meanSpan));
            const auto hi = static_cast<std::size_t> (std::min<std::ptrdiff_t> (static_cast<std::ptrdiff_t> (frames), static_cast<std::ptrdiff_t> (f) + meanSpan + 1));
            const double count = static_cast<double> (hi - lo);
            novelty[f] = std::max (0.0, flux[f] - (prefix[hi] - prefix[lo]) / count);
            lowNovelty[f] = std::max (0.0, lowFlux[f] - (lowPrefix[hi] - lowPrefix[lo]) / count);
            top = std::max (top, novelty[f]);
            lowTop = std::max (lowTop, lowNovelty[f]);
        }
        for (std::size_t f = 0; f < frames; ++f)
        {
            novelty[f] = top > 0.0 ? novelty[f] / top : 0.0;
            lowNovelty[f] = lowTop > 0.0 ? lowNovelty[f] / lowTop : 0.0;
            a.transient[f] = static_cast<float> (novelty[f]);
        }
    }

    // Onsets: peaks over an adaptive threshold, 50 ms apart; each refined on the waveform to
    // its attack (where a fast envelope first reaches half its height). Each onset's own
    // level (its attack's height) is kept: accents decide the meter and the downbeat.
    std::vector<float> levels;
    {
        const auto span = static_cast<std::ptrdiff_t> (std::lround (0.15 / a.hopSeconds));
        const auto gap = static_cast<std::ptrdiff_t> (std::lround (0.05 / a.hopSeconds));
        std::vector<float> fast (length);
        {
            const float attack = static_cast<float> (1.0 - std::exp (-1.0 / (0.0002 * sr))), release = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * sr)));
            float e = 0.0f;
            for (std::size_t i = 0; i < length; ++i)
            {
                const float v = std::abs (s[i]);
                e += (v > e ? attack : release) * (v - e);
                fast[i] = e;
            }
        }
        std::ptrdiff_t last = -gap;
        for (std::ptrdiff_t f = 0; f + 1 < static_cast<std::ptrdiff_t> (frames); ++f)
        {
            const auto uf = static_cast<std::size_t> (f);
            const double before = f > 0 ? novelty[uf - 1] : 0.0;
            if (! (novelty[uf] >= before && novelty[uf] > novelty[uf + 1]))
                continue;
            double local = 0.0;
            const auto lo = std::max<std::ptrdiff_t> (0, f - span), hi = std::min<std::ptrdiff_t> (static_cast<std::ptrdiff_t> (frames), f + span + 1);
            for (auto k = lo; k < hi; ++k)
                local += novelty[static_cast<std::size_t> (k)];
            local /= static_cast<double> (hi - lo);
            if (novelty[uf] < 1.4 * local + 0.08)
                continue;
            if (f - last < gap)
            {
                if (! a.onsets.empty() && novelty[uf] > a.onsets.back().strength)
                {
                    a.onsets.pop_back();   // (the stronger of two close peaks)
                    levels.pop_back();
                }
                else
                    continue;
            }
            // The attack: in the frame's window, the fast envelope's peak, then back to its half.
            const auto c = f * hop;
            const auto from = std::max<std::ptrdiff_t> (0, c - n / 2 - hop), to = std::min<std::ptrdiff_t> (static_cast<std::ptrdiff_t> (length) - 1, c + n / 2);
            auto m = from;
            for (auto i = from; i <= to; ++i)
                if (fast[static_cast<std::size_t> (i)] > fast[static_cast<std::size_t> (m)])
                    m = i;
            auto i0 = m;
            const float half = 0.5f * fast[static_cast<std::size_t> (m)];
            while (i0 > from && fast[static_cast<std::size_t> (i0 - 1)] >= half)
                --i0;
            a.onsets.push_back ({ static_cast<double> (i0) / sr, static_cast<float> (novelty[uf]) });
            levels.push_back (fast[static_cast<std::size_t> (m)]);
            last = f;
        }
    }

    // Tempo: the onset curve's autocorrelation, each lag scored with its octaves (twice, four
    // times), under a gentle prior around 120 BPM.
    double mean = 0.0;
    for (const double v : novelty)
        mean += v;
    mean /= static_cast<double> (frames);
    std::vector<double> centred (frames);
    for (std::size_t f = 0; f < frames; ++f)
        centred[f] = novelty[f] - mean;
    const double minLag = 60.0 / maxBpm / a.hopSeconds, maxLag = 60.0 / minBpm / a.hopSeconds;
    const auto acfSize = std::min<std::size_t> (frames - 1, static_cast<std::size_t> (std::ceil (4.0 * maxLag)) + 2);
    std::vector<double> acf (acfSize + 1, 0.0);
    for (std::size_t lag = 0; lag <= acfSize; ++lag)
    {
        double sum = 0.0;
        for (std::size_t f = 0; f + lag < frames; ++f)
            sum += centred[f] * centred[f + lag];
        acf[lag] = sum / static_cast<double> (frames - lag);
    }
    const double zero = acf[0] > 0.0 ? acf[0] : 1.0;
    auto prior = [] (double bpm) { const double o = std::log2 (bpm / 120.0); return std::exp (-0.5 * o * o / (1.1 * 1.1)); };
    auto score = [&] (double lag) { return acfAt (acf, lag) + 0.5 * acfAt (acf, 2.0 * lag) + 0.25 * acfAt (acf, 4.0 * lag); };
    std::vector<std::pair<double, double>> peaks;   // (lag, weighted score)
    {
        const auto lo = static_cast<std::size_t> (std::floor (minLag)), hi = std::min<std::size_t> (acfSize - 1, static_cast<std::size_t> (std::ceil (maxLag)));
        std::vector<double> sc (acfSize + 1, 0.0);
        for (std::size_t lag = std::max<std::size_t> (1, lo - 1); lag <= std::min (acfSize, hi + 1); ++lag)
            sc[lag] = score (static_cast<double> (lag));
        for (std::size_t lag = std::max<std::size_t> (2, lo); lag < hi; ++lag)
            if (sc[lag] > sc[lag - 1] && sc[lag] >= sc[lag + 1] && sc[lag] > 0.0)
            {
                // (the peak between the lags: a parabola through three)
                const double y0 = sc[lag - 1], y1 = sc[lag], y2 = sc[lag + 1];
                const double d = y0 - 2.0 * y1 + y2;
                const double refined = static_cast<double> (lag) + (std::abs (d) > 1.0e-12 ? 0.5 * (y0 - y2) / d : 0.0);
                const double bpm = 60.0 / (refined * a.hopSeconds);
                peaks.push_back ({ refined, score (refined) * prior (bpm) });
            }
    }
    if (peaks.empty())
    {
        a.kind = Kind::unsure;
        return a;
    }
    std::sort (peaks.begin(), peaks.end(), [] (const auto& x, const auto& y) { return x.second > y.second; });
    const double bestLag = peaks.front().first;
    const double bestScore = peaks.front().second;
    for (const auto& [lag, sc] : peaks)
    {
        const double bpm = 60.0 / (lag * a.hopSeconds);
        bool distinct = true;
        for (const auto& c : a.candidates)
            distinct = distinct && std::abs (std::log2 (bpm / c.bpm)) > 0.04;
        if (distinct && bestScore > 0.0)
            a.candidates.push_back ({ bpm, sc / bestScore });
        if (a.candidates.size() == 4)
            break;
    }
    double meanAcf = 0.0;
    {
        int count = 0;
        for (double lag = minLag; lag <= maxLag; lag += 1.0, ++count)
            meanAcf += acfAt (acf, lag);
        meanAcf /= std::max (1, count);
    }
    a.confidence = std::clamp ((acfAt (acf, bestLag) - meanAcf) / std::max (1.0e-12, zero - meanAcf), 0.0, 1.0);
    const double bpm0 = 60.0 / (bestLag * a.hopSeconds);

    // The loop: a whole number of beats at the tempo, its half or its double - the usual loop
    // lengths (2 .. 128 beats in powers of two, or 3 .. 96 for three-beat bars) within 4 %, any
    // other whole number of beats only within 0.3 % (a long recording, not a loop).
    {
        double bestError = 1.0e9;
        auto consider = [&] (int beats, double tolerance, double preference) {
            const double fit = 60.0 * beats / a.durationSeconds;
            if (fit < minBpm || fit > maxBpm)
                return;
            for (const double t : { bpm0, 0.5 * bpm0, 2.0 * bpm0 })
            {
                const double off = std::abs (std::log2 (fit / t));
                const double error = off + (t == bpm0 ? 0.0 : 0.01) + preference;   // (the measured octave, then the usual lengths, preferred)
                if (off < std::log2 (1.0 + tolerance) && error < bestError)
                {
                    bestError = error;
                    a.bpm = fit;
                    a.beats = beats;
                    a.loopFits = true;
                }
            }
        };
        for (const int beats : { 2, 4, 8, 16, 32, 64, 128 })
            consider (beats, 0.04, 0.0);
        for (const int beats : { 3, 6, 12, 24, 48, 96 })
            consider (beats, 0.04, 0.002);
        for (int beats = 2; beats <= 512; ++beats)
            consider (beats, 0.003, 0.004);
        if (! a.loopFits)
        {
            a.bpm = bpm0;
            a.beats = std::max (1, static_cast<int> (std::lround (a.durationSeconds * a.bpm / 60.0)));
        }
    }

    // The grid's phase (the beats' sum of onset strength).
    const double beat = 60.0 / a.bpm;
    double phase = 0.0, phaseScore = -1.0;
    for (double p = 0.0; p < beat; p += 0.5 * a.hopSeconds)
    {
        double sum = 0.0;
        for (double t = p; t < a.durationSeconds; t += beat)
            sum += at (novelty, t, a.hopSeconds);
        if (sum > phaseScore)
        {
            phaseScore = sum;
            phase = p;
        }
    }
    // (the novelty peaks a little before the attack: the beat's own onset, when one is near)
    for (const auto& o : a.onsets)
    {
        const double d = std::fmod (o.seconds - phase + 10.0 * beat, beat);
        if (std::min (d, beat - d) <= 0.025)
        {
            phase = std::fmod (o.seconds + 10.0 * beat, beat);
            break;
        }
    }
    const int beatCount = std::max (1, static_cast<int> (std::floor ((a.durationSeconds - phase) / beat + 1.0e-6)) + 1);

    // Each beat's accent: the attack level of the onset on it (if any), and its low band (kicks).
    std::vector<double> accent (static_cast<std::size_t> (beatCount), 0.0);
    float levelTop = 1.0e-9f;
    for (const float l : levels)
        levelTop = std::max (levelTop, l);
    for (int k = 0; k < beatCount; ++k)
    {
        const double t = phase + k * beat;
        double level = 0.0;
        for (std::size_t i = 0; i < a.onsets.size(); ++i)
            if (std::abs (a.onsets[i].seconds - t) <= std::max (0.025, 0.08 * beat))
                level = std::max (level, static_cast<double> (levels[i] / levelTop));
        double low = 0.0;
        for (double dt = -0.02; dt <= 0.02; dt += 0.5 * a.hopSeconds)
            low = std::max (low, at (lowNovelty, t + dt, a.hopSeconds));
        accent[static_cast<std::size_t> (k)] = level + 1.5 * low;
    }
    // The meter and the downbeat: the grouping (3 or 4) whose one beat stands out most.
    auto contrast = [&] (int meter, int& bestOffset) {
        double best = -1.0e9;
        bestOffset = 0;
        for (int o = 0; o < meter; ++o)
        {
            double on = 0.0, off = 0.0;
            int nOn = 0, nOff = 0;
            for (int k = 0; k < beatCount; ++k)
                if (k % meter == o)
                    on += accent[static_cast<std::size_t> (k)], ++nOn;
                else
                    off += accent[static_cast<std::size_t> (k)], ++nOff;
            const double c = (nOn > 0 ? on / nOn : 0.0) - (nOff > 0 ? off / nOff : 0.0);
            if (c > best + 1.0e-9)
            {
                best = c;
                bestOffset = o;
            }
        }
        return best;
    };
    int offset3 = 0, offset4 = 0;
    const double c3 = contrast (3, offset3), c4 = contrast (4, offset4);
    a.beatsPerBar = c3 > 0.05 && c3 > 1.25 * std::max (0.0, c4) && a.beats % 3 == 0 ? 3 : 4;
    const int downbeatIndex = a.beatsPerBar == 3 ? offset3 : offset4;
    a.bars = static_cast<double> (a.beats) / a.beatsPerBar;
    a.downbeatSeconds = std::fmod (phase + downbeatIndex * beat, beat * a.beatsPerBar);
    if (a.downbeatSeconds > beat * a.beatsPerBar - a.hopSeconds)
        a.downbeatSeconds = 0.0;

    // Rhythmic: a clear pulse, enough onsets, and most of them on a sixteenth or triplet grid.
    int strong = 0, onGrid = 0;
    for (const auto& o : a.onsets)
    {
        if (o.strength < 0.2f)
            continue;
        ++strong;
        const double b = (o.seconds - a.downbeatSeconds) / beat;
        const double sixteenth = std::abs (b * 4.0 - std::round (b * 4.0)) / 4.0, triplet = std::abs (b * 3.0 - std::round (b * 3.0)) / 3.0;
        if (std::min (sixteenth, triplet) <= 0.08)
            ++onGrid;
    }
    const double gridFit = strong > 0 ? static_cast<double> (onGrid) / strong : 0.0;
    const bool pulse = a.confidence >= 0.3 && strong >= 4 && gridFit >= 0.6 && (a.loopFits || a.confidence >= 0.5);
    a.kind = pulse ? Kind::rhythmic : Kind::unsure;
    return a;
}

Analysis withTempoFactor (const Analysis& a, double factor)
{
    if (! (factor > 0.0) || a.bpm <= 0.0)
        return a;
    Analysis b = a;
    b.bpm = a.bpm * factor;
    b.beats = std::max (1, static_cast<int> (std::lround (a.beats * factor)));
    b.bars = static_cast<double> (b.beats) / b.beatsPerBar;
    return b;
}

} // namespace andor::analog::rhythm
