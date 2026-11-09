#include "LiveMorph.h"

#include <algorithm>
#include <cmath>

namespace andor::analog::morph
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double octaveQ = 1.41421356;   // an octave band
    constexpr double noiseCentreHz[LiveMorph::noiseBands] { 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
    /** Below this a source is silent (dBFS): it gives the morph nothing to take. */
    constexpr float silentDb = -80.0f;
    /** How far below a source's strongest partial its partials are still taken as heard. */
    constexpr float rangeDb = 70.0f;

    float dbToGain (float db) noexcept { return db <= LiveMorph::floorDb + 0.5f ? 0.0f : std::pow (10.0f, 0.05f * db); }
    float powerToDb (double p) noexcept { return p > 1.0e-20 ? static_cast<float> (10.0 * std::log10 (p)) : LiveMorph::floorDb; }

    float smoothstep (float x) noexcept
    {
        x = std::clamp (x, 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    /** Moves `now` towards `target`, landing exactly on it (so the ends are exact). */
    void glide (float& now, float target, float k) noexcept
    {
        now += k * (target - now);
        if (std::abs (target - now) < 1.0e-6f)
            now = target;
    }

    void hann (std::vector<float>& w, float& sum, float& power, int n)
    {
        w.resize (static_cast<std::size_t> (n));
        sum = power = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const auto v = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * (i + 0.5) / n));
            w[static_cast<std::size_t> (i)] = v;
            sum += v;
            power += v * v;
        }
    }
}

LiveMorph::Shared::Shared()
{
    for (int o = smallOrder; o <= largeOrder; ++o)
    {
        const auto i = static_cast<std::size_t> (o - smallOrder);
        hann (windows[i], windowSum[i], windowPower[i], 1 << o);
    }
    scratch.resize (static_cast<std::size_t> (1 << largeOrder));
}

int LiveMorph::windowOrder (double lowestHz, double hostRate) noexcept
{
    const double needed = 8.0 * hostRate / std::max (20.0, lowestHz);
    int order = smallOrder;
    while (order < largeOrder && (1 << order) < needed)
        ++order;
    return order;
}

void LiveMorph::Svf::set (double hz, double q, double sampleRate) noexcept
{
    g = static_cast<float> (std::tan (pi * std::min (hz, 0.45 * sampleRate) / sampleRate));
    k = static_cast<float> (1.0 / q);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float LiveMorph::Svf::band (float x) noexcept
{
    const float v3 = x - s2;
    const float v1 = a1 * s1 + a2 * v3;
    const float v2 = s2 + a2 * s1 + a3 * v3;
    s1 = 2.0f * v1 - s1;
    s2 = 2.0f * v2 - s2;
    return k * v1;   // (peak gain 1)
}

void LiveMorph::prepare (double internalRate, int oversampling, const Shared* sharedState)
{
    shared = sharedState;
    rate = internalRate;
    factor = std::max (1, oversampling);
    hostRate = rate / factor;
    const int size = 1 << largeOrder;
    ringX.assign (static_cast<std::size_t> (size), 0.0f);
    ringY.assign (static_cast<std::size_t> (size), 0.0f);
    ringMask = size - 1;
    smoothing = static_cast<float> (1.0 - std::exp (-1.0 / (0.008 * hostRate)));   // ~8 ms (between analyses)
    levelCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.06 * rate)));          // ~60 ms (LEVEL MATCH's gains)
    for (int b = 0; b < noiseBands; ++b)
    {
        const auto i = static_cast<std::size_t> (b);
        noiseBank[i].set (noiseCentreHz[b], octaveQ, hostRate);
        // Unit white noise through a peak-1 band-pass of Q: variance pi fc / (Q fs).
        const double fc = std::min (noiseCentreHz[b], 0.45 * hostRate);
        noiseNorm[i] = static_cast<float> (std::sqrt (pi * fc / (octaveQ * hostRate)));
    }
    start (1);
}

void LiveMorph::start (std::uint64_t seed) noexcept
{
    std::fill (ringX.begin(), ringX.end(), 0.0f);
    std::fill (ringY.begin(), ringY.end(), 0.0f);
    write = sinceAnalysis = analyses = age = 0;
    accX = accY = 0.0f;
    phaseInHost = 0;
    xDb.fill (floorDb);
    yDb.fill (floorDb);
    xNoiseDb.fill (floorDb);
    yNoiseDb.fill (floorDb);
    xLevelDb = yLevelDb = floorDb;
    xEnvelope.fill (floorDb);
    yEnvelope.fill (floorDb);
    stepsF0 = -1.0;
    stepsCount = -1;
    noise.reseed (seed ^ 0x6d6f7270ull);
    for (int h = 0; h < maxPartials; ++h)
    {
        const auto i = static_cast<std::size_t> (h);
        re[i] = 1.0f;
        im[i] = 0.0f;
        amp[i] = ampTarget[i] = 0.0f;
        weaveOrder[i] = static_cast<float> (0.08 + 0.84 * noise.nextDouble());
    }
    for (auto& s : noiseBank)
        s.s1 = s.s2 = 0.0f;
    noiseGain.fill (0.0f);
    noiseTarget.fill (0.0f);
    coreNow = corePrev = coreOut = 0.0f;
    matchGain = matchTarget = 1.0f;
    levelX = levelY = levelXTarget = levelYTarget = 1.0f;
    warm = 0.0f;
    renormCountdown = 0;
    fresh = true;
}

void LiveMorph::control (const Control& c) noexcept
{
    // The targets follow the analyses; between them only a change of the controls (or the
    // pitch) recomputes them.
    const bool changed = fresh || c.method != settings.method || c.levelMatch != settings.levelMatch || std::abs (c.position - settings.position) > 1.0e-5
                         || std::abs (c.texture - settings.texture) > 1.0e-5 || std::abs (c.formants - settings.formants) > 1.0e-5
                         || std::abs (c.detail - settings.detail) > 1.0e-5 || std::abs (c.carrierHz - settings.carrierHz) > 1.0e-6 * c.carrierHz
                         || std::abs (c.sourceHz - settings.sourceHz) > 1.0e-6 * c.sourceHz;
    if (c.method == MorphMethod::imprint && settings.method != MorphMethod::imprint)
    {
        makeEnvelope (xDb, c.carrierHz, xEnvelope);
        makeEnvelope (yDb, c.sourceHz, yEnvelope);
    }
    settings = c;
    settings.position = std::clamp (c.position, 0.0, 1.0);
    partialCount = std::clamp (8 + static_cast<int> (std::lround (40.0 * std::clamp (c.detail, 0.0, 1.0))), 8, maxPartials);
    // The attack: the sources as played until one analysis window has heard the note, and
    // ATTACK's time on top (0 .. 150 ms); then a 20 ms glide into the morph.
    const int window = 1 << windowOrder (std::min (c.carrierHz, c.sourceHz), hostRate);
    attackSamples = window + hop + static_cast<int> (0.15 * std::clamp (c.attack, 0.0, 1.0) * hostRate);
    warmStep = static_cast<float> (1.0 / (0.02 * hostRate));

    const auto p = static_cast<float> (settings.position);
    const float b = std::sqrt (std::max (0.0f, 4.0f * p * (1.0f - p)));
    const float bw = b * warm;
    tX = (1.0f - p) * (1.0f - bw);
    tCore = bw;
    tY = p * (1.0f - bw);
    if (const float norm = std::sqrt (tX * tX + tCore * tCore + tY * tY); norm > 1.0e-6f)
    {
        tX /= norm;   // (equal power, as process() keeps them)
        tCore /= norm;
        tY /= norm;
    }
    if (fresh)
    {
        wX = tX;
        wCore = tCore;
        wY = tY;
        fresh = false;
    }
    if (changed)
        updateTargets();
}

float LiveMorph::envelopeAt (const std::array<float, maxPartials>& partials, double f0, double hz) const noexcept
{
    // A source's spectral envelope at `hz`: its harmonics' levels, interpolated in dB between
    // the two nearest (flat beyond the ends).
    if (f0 <= 0.0)
        return floorDb;
    const double x = hz / f0 - 1.0;
    if (x <= 0.0)
        return partials[0];
    const int i = static_cast<int> (x);
    if (i >= maxPartials - 1)
        return partials[maxPartials - 1];
    const auto t = static_cast<float> (x - i);
    return partials[static_cast<std::size_t> (i)] * (1.0f - t) + partials[static_cast<std::size_t> (i + 1)] * t;
}

void LiveMorph::makeEnvelope (const std::array<float, maxPartials>& partials, double f0, std::array<float, envelopePoints>& table) const noexcept
{
    // The broad shape: at each point (8 per octave from 50 Hz), the power average of the
    // harmonics within a third of an octave each side (the nearest one alone where none is).
    for (int k = 0; k < envelopePoints; ++k)
    {
        const double hz = 50.0 * std::exp2 (k / 8.0);
        const double lo = hz * 0.7937, hi = hz * 1.2599;
        double sum = 0.0;
        int n = 0;
        for (int h = std::max (1, static_cast<int> (std::ceil (lo / f0))); h <= std::min (maxPartials, static_cast<int> (hi / f0)); ++h)
        {
            sum += std::pow (10.0, 0.1 * partials[static_cast<std::size_t> (h - 1)]);
            ++n;
        }
        table[static_cast<std::size_t> (k)] = n == 0 ? envelopeAt (partials, f0, hz) : std::max (floorDb, static_cast<float> (10.0 * std::log10 (sum / n + 1.0e-30)));
    }
}

float LiveMorph::smoothEnvelopeAt (const std::array<float, envelopePoints>& table, double hz) const noexcept
{
    const double x = 8.0 * std::log2 (std::max (hz, 50.0) / 50.0);
    const int i = std::min (envelopePoints - 2, static_cast<int> (x));
    const auto t = static_cast<float> (std::min (1.0, x - i));
    return table[static_cast<std::size_t> (i)] * (1.0f - t) + table[static_cast<std::size_t> (i + 1)] * t;
}

void LiveMorph::updateTargets() noexcept
{
    const auto p = static_cast<float> (settings.position);
    const bool xHeard = xLevelDb > silentDb, yHeard = yLevelDb > silentDb;
    const double fx = std::max (20.0, settings.carrierHz), fy = std::max (20.0, settings.sourceHz);
    // The pitch morphs with the sound (IMPRINT keeps the carrier's); a silent side gives none.
    if (settings.method == MorphMethod::imprint || ! yHeard)
        f0Morph = fx;
    else if (! xHeard)
        f0Morph = fy;
    else
        f0Morph = std::exp ((1.0 - p) * std::log (fx) + p * std::log (fy));

    const double shift = std::exp2 (-settings.formants / 12.0);
    const int count = std::min (partialCount, std::max (1, static_cast<int> (0.45 * hostRate / f0Morph)));
    double partialPower = 0.0;
    for (int h = 0; h < maxPartials; ++h)
    {
        const auto i = static_cast<std::size_t> (h);
        if (h >= count)
        {
            ampTarget[i] = 0.0f;
            continue;
        }
        const float dx = xDb[i];
        // Card C's partial, its envelope moved by FORMANTS.
        const float dy = settings.formants == 0.0 ? yDb[i] : envelopeAt (yDb, fy, (h + 1) * fy * shift);
        float db;
        switch (settings.method)
        {
            case MorphMethod::imprint:
            {
                // The carrier's own partials (its fine structure kept), their smooth envelope
                // traded for card C's (a vocoder's imprint, at the carrier's pitch): complete
                // from the middle on (the endpoint law then leads it to card C itself).
                const double hz = (h + 1) * fx;
                db = dx + std::min (1.0f, 2.0f * p) * (smoothEnvelopeAt (yEnvelope, hz * shift) - smoothEnvelopeAt (xEnvelope, hz));
                break;
            }
            case MorphMethod::weave:
            {
                const float s = smoothstep ((p - weaveOrder[i]) / 0.12f + 0.5f);
                db = (1.0f - s) * dx + s * dy;
                break;
            }
            case MorphMethod::fuse:
            default:
                db = (1.0f - p) * dx + p * dy;
                break;
        }
        const float a = dbToGain (db);
        ampTarget[i] = a;
        partialPower += 0.5 * static_cast<double> (a) * a;
    }
    // The partials' frequencies: only when the pitch moved (a bend, a glide, the position).
    if (std::abs (f0Morph - stepsF0) > 1.0e-7 * f0Morph || count != stepsCount)
    {
        stepsF0 = f0Morph;
        stepsCount = count;
        for (int h = 0; h < count; ++h)
        {
            const double w = 2.0 * pi * (h + 1) * f0Morph / hostRate;
            stepRe[static_cast<std::size_t> (h)] = static_cast<float> (std::cos (w));
            stepIm[static_cast<std::size_t> (h)] = static_cast<float> (std::sin (w));
        }
    }
    // The residual: each band's level between the two, scaled by TEXTURE (50 %: as heard).
    const auto texture = static_cast<float> (2.0 * std::clamp (settings.texture, 0.0, 1.0));
    double noisePower = 0.0;
    for (int b = 0; b < noiseBands; ++b)
    {
        const auto i = static_cast<std::size_t> (b);
        const float r = texture * dbToGain ((1.0f - p) * xNoiseDb[i] + p * yNoiseDb[i]);
        noiseTarget[i] = r / noiseNorm[i];
        noisePower += static_cast<double> (r) * r;
    }
    // LEVEL MATCH: both sides are brought to the louder one's loudness (a quieter card C is
    // raised to the oscillators', or the reverse), so the knob changes the sound, not the
    // volume; the morph is held at that loudness. Off: each side as it plays, the morph at the
    // loudness between them (in dB).
    matchTarget = 1.0f;
    levelXTarget = levelYTarget = 1.0f;
    const float louder = std::max (xLevelDb, yLevelDb);
    if (settings.levelMatch && louder > silentDb)
    {
        if (xHeard)
            levelXTarget = std::clamp (dbToGain (louder - xLevelDb), 1.0f, 16.0f);
        if (yHeard)
            levelYTarget = std::clamp (dbToGain (louder - yLevelDb), 1.0f, 16.0f);
    }
    if (settings.levelMatch)
    {
        const float target = dbToGain (louder > silentDb ? louder : (1.0f - p) * std::max (xLevelDb, floorDb) + p * std::max (yLevelDb, floorDb));
        const double predicted = std::sqrt (partialPower + noisePower);
        if (predicted > 1.0e-7 && target > 0.0f)
            matchTarget = std::clamp (static_cast<float> (target / predicted), 0.1f, 10.0f);
    }
}

void LiveMorph::measure (const std::vector<std::complex<double>>& z, int order, bool imaginary, double f0, std::array<float, maxPartials>& partials,
                         std::array<float, noiseBands>& residual) const noexcept
{
    const int n = 1 << order;
    // One side's spectrum from the packed transform (x real, y imaginary).
    auto bin = [&z, n, imaginary] (int k) {
        const auto a = z[static_cast<std::size_t> (k)];
        const auto b = std::conj (z[static_cast<std::size_t> ((n - k) & (n - 1))]);
        return imaginary ? (a - b) * std::complex<double> (0.0, -0.5) : (a + b) * 0.5;
    };
    const auto w = static_cast<std::size_t> (order - smallOrder);
    const double windowSum = shared->windowSum[w];
    const double windowPower = shared->windowPower[w];
    const double binHz = hostRate / n;
    const int half = n / 2;

    // Each harmonic: the strongest bin within a quarter harmonic of h f0, interpolated.
    std::array<std::uint8_t, (1 << largeOrder) / 2 + 1> taken {};
    float strongest = floorDb;
    for (int h = 0; h < maxPartials; ++h)
    {
        const auto i = static_cast<std::size_t> (h);
        const double centre = (h + 1) * f0 / binHz;
        if ((h + 1) * f0 > 0.45 * hostRate || centre < 1.0)
        {
            partials[i] = floorDb;
            continue;
        }
        const int reach = std::max (1, static_cast<int> (0.25 * f0 / binHz));
        const int lo = std::max (1, static_cast<int> (centre) - reach), hi = std::min (half - 1, static_cast<int> (centre) + reach + 1);
        int best = lo;
        double bestMag = 0.0;
        for (int k = lo; k <= hi; ++k)
            if (const double m = std::abs (bin (k)); m > bestMag)
            {
                bestMag = m;
                best = k;
            }
        // Parabolic interpolation of the peak in dB.
        const double a = 20.0 * std::log10 (std::abs (bin (best - 1)) + 1.0e-12);
        const double b = 20.0 * std::log10 (bestMag + 1.0e-12);
        const double c = 20.0 * std::log10 (std::abs (bin (std::min (half, best + 1))) + 1.0e-12);
        // (The peak's offset, at most half a bin: a flat top never divides by nothing.)
        const double d = a - 2.0 * b + c;
        const double delta = d < -1.0e-9 ? std::clamp (0.5 * (a - c) / d, -0.5, 0.5) : 0.0;
        const double peakDb = b - 0.25 * (a - c) * delta;
        // A sine of amplitude A peaks at A sum (w) / 2.
        const auto db = static_cast<float> (peakDb - 20.0 * std::log10 (0.5 * windowSum));
        partials[i] = std::max (floorDb, db);
        strongest = std::max (strongest, partials[i]);
        for (int k = std::max (0, best - 2); k <= std::min (half, best + 2); ++k)
            taken[static_cast<std::size_t> (k)] = 1;
    }
    for (auto& v : partials)
        v = v < strongest - rangeDb ? floorDb : v;

    // The residual: in each octave band, the bins the harmonics did not take, their quieter
    // half averaged (a window's leakage between harmonics sits in the louder half, true noise
    // fills all of it: for noise the quieter half averages 0.307 of the mean). What stays
    // within 45 dB of the band's own harmonics is leakage, not noise.
    std::array<double, (1 << largeOrder) / 2 + 1> powers {};
    for (int b = 0; b < noiseBands; ++b)
    {
        const double fc = noiseCentreHz[b];
        const int lo = std::max (1, static_cast<int> (fc / std::sqrt (2.0) / binHz));
        const int hi = std::min (half - 1, static_cast<int> (fc * std::sqrt (2.0) / binHz));
        int counted = 0;
        for (int k = lo; k <= hi; ++k)
            if (taken[static_cast<std::size_t> (k)] == 0)
                powers[static_cast<std::size_t> (counted++)] = std::norm (bin (k));
        if (counted < 2)
        {
            residual[static_cast<std::size_t> (b)] = floorDb;
            continue;
        }
        const auto quiet = static_cast<std::size_t> (counted / 2);
        std::nth_element (powers.begin(), powers.begin() + static_cast<std::ptrdiff_t> (quiet), powers.begin() + counted);
        double sum = 0.0;
        for (std::size_t k = 0; k < quiet; ++k)
            sum += powers[k];
        const double meanPower = sum / static_cast<double> (std::max<std::size_t> (1, quiet)) / 0.307;
        const double power = 2.0 * meanPower * (hi - lo + 1) / (n * windowPower);
        float bandHarmonics = floorDb;
        for (int h = 0; h < maxPartials; ++h)
            if (const double f = (h + 1) * f0; f >= fc / std::sqrt (2.0) && f < fc * std::sqrt (2.0))
                bandHarmonics = std::max (bandHarmonics, partials[static_cast<std::size_t> (h)]);
        const float db = powerToDb (power);
        residual[static_cast<std::size_t> (b)] = db < std::max (bandHarmonics, strongest - 20.0f) - 45.0f ? floorDb : db;
    }
}

void LiveMorph::analyse() noexcept
{
    const int order = windowOrder (std::min (settings.carrierHz, settings.sourceHz), hostRate);
    const int n = 1 << order;
    const auto w = static_cast<std::size_t> (order - smallOrder);
    const auto& window = shared->windows[w];
    const double windowPower = shared->windowPower[w];
    auto& z = const_cast<std::vector<std::complex<double>>&> (shared->scratch);   // (one voice at a time)
    double px = 0.0, py = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const auto at = static_cast<std::size_t> ((write - n + i) & ringMask);
        const double wi = window[static_cast<std::size_t> (i)];
        const double x = ringX[at] * wi, y = ringY[at] * wi;
        px += x * x;
        py += y * y;
        z[static_cast<std::size_t> (i)] = { x, y };
    }
    xLevelDb = powerToDb (px / windowPower);
    yLevelDb = powerToDb (py / windowPower);
    shared->transforms[w].forward (z.data());
    measure (z, order, false, settings.carrierHz, xDb, xNoiseDb);
    measure (z, order, true, settings.sourceHz, yDb, yNoiseDb);
    if (settings.method == MorphMethod::imprint)
    {
        makeEnvelope (xDb, settings.carrierHz, xEnvelope);
        makeEnvelope (yDb, settings.sourceHz, yEnvelope);
    }
    ++analyses;
}

void LiveMorph::renderHost() noexcept
{
    corePrev = coreNow;
    float sum = 0.0f;
    for (int h = 0; h < partialCount; ++h)
    {
        const auto i = static_cast<std::size_t> (h);
        amp[i] += smoothing * (ampTarget[i] - amp[i]);
        const float r = re[i] * stepRe[i] - im[i] * stepIm[i];
        im[i] = re[i] * stepIm[i] + im[i] * stepRe[i];
        re[i] = r;
        sum += amp[i] * im[i];
    }
    if (--renormCountdown <= 0)
    {
        // Keep the rotators on the unit circle (float rounding drifts them).
        renormCountdown = 1024;
        for (int h = 0; h < maxPartials; ++h)
        {
            const auto i = static_cast<std::size_t> (h);
            const float m = std::sqrt (re[i] * re[i] + im[i] * im[i]);
            if (m > 0.0f)
            {
                re[i] /= m;
                im[i] /= m;
            }
        }
    }
    const auto white = static_cast<float> (noise.bipolar() * 1.7320508);   // (unit variance)
    for (int b = 0; b < noiseBands; ++b)
    {
        const auto i = static_cast<std::size_t> (b);
        noiseGain[i] += smoothing * (noiseTarget[i] - noiseGain[i]);
        if (noiseGain[i] > 1.0e-6f)
            sum += noiseGain[i] * noiseBank[i].band (white);
    }
    coreNow = sum;
}

float LiveMorph::process (float carrier, float source) noexcept
{
    if (! std::isfinite (carrier))
        carrier = 0.0f;
    if (! std::isfinite (source))
        source = 0.0f;
    // Each side into its ring at the host rate; an analysis every hop.
    accX += carrier;
    accY += source;
    if (phaseInHost == factor - 1)
    {
        const float scale = 1.0f / static_cast<float> (factor);
        ringX[static_cast<std::size_t> (write)] = accX * scale;
        ringY[static_cast<std::size_t> (write)] = accY * scale;
        write = (write + 1) & ringMask;
        accX = accY = 0.0f;
        ++age;
        if (age >= attackSamples)
            warm = std::min (1.0f, warm + warmStep);
        if (++sinceAnalysis >= std::max (hop, (1 << windowOrder (std::min (settings.carrierHz, settings.sourceHz), hostRate)) / 4))
        {
            sinceAnalysis = 0;
            analyse();
            updateTargets();
        }
    }
    if (phaseInHost == 0)
        renderHost();
    const float frac = static_cast<float> (phaseInHost + 1) / static_cast<float> (factor);
    phaseInHost = (phaseInHost + 1) % factor;

    // The endpoint law, its weights following the warm-up and the position (a few ms). (A card
    // that is silent outright, muted or mixed out, never reaches the morph: the synth plays
    // the other as it is.)
    {
        const auto p = static_cast<float> (settings.position);
        const float b = std::sqrt (std::max (0.0f, 4.0f * p * (1.0f - p)));
        const float bw = analyses > 0 ? b * warm : 0.0f;
        tX = (1.0f - p) * (1.0f - bw);
        tCore = bw;
        tY = p * (1.0f - bw);
        // Equal power (the three are not in phase: the morph's partials are its own): the
        // loudness holds across the knob; the ends (one weight 1) are untouched.
        const float norm = std::sqrt (tX * tX + tCore * tCore + tY * tY);
        if (norm > 1.0e-6f)
        {
            tX /= norm;
            tCore /= norm;
            tY /= norm;
        }
    }
    const float k = 0.0015f;
    glide (wX, tX, k);
    glide (wCore, tCore, k);
    glide (wY, tY, k);
    glide (matchGain, matchTarget, k);
    levelX += levelCoef * (levelXTarget - levelX);
    levelY += levelCoef * (levelYTarget - levelY);
    const float core = corePrev + frac * (coreNow - corePrev);
    coreOut = wCore * matchGain * core;
    return wX * levelX * carrier + coreOut + wY * levelY * source;
}

} // namespace andor::analog::morph
