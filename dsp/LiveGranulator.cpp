#include "LiveGranulator.h"

#include "engine/FastMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace andor::analog
{

void LiveRecording::prepare (int lengthSamples)
{
    length = std::max (64, lengthSamples);
    data.assign (static_cast<std::size_t> (length + guard), 0.0f);
    restart();
}

//==============================================================================
void LiveGranulator::start (const osp::GranularParams& params, std::uint64_t seed) noexcept
{
    // The scheduler as ANDOR OSP's (GranularSource::start): its own stream from the note's seed.
    rng.reseed (osp::Prng::deriveSeed (seed, 0x6772736dull, 0));
    for (auto& g : grains)
        g.active = false;
    activeGrains = 0;
    spawned = 0;
    countdown = 0;   // the first grain starts with the note
    spawning = true;
    setParams (params);
}

void LiveGranulator::setParams (const osp::GranularParams& params) noexcept
{
    settings = params;
    // Overlapping grains add up (incoherently): ANDOR OSP's level correction.
    const double overlap = std::clamp (settings.density, 1.0, 80.0) * std::clamp (settings.sizeSeconds, 0.005, 1.0);
    norm = static_cast<float> (1.0 / std::max (1.0, 0.612 * std::sqrt (overlap)));
}

void LiveGranulator::stop() noexcept
{
    for (auto& g : grains)
        g.active = false;
    activeGrains = 0;
    spawning = false;
}

void LiveGranulator::spawn (const LiveRecording& recording, double step) noexcept
{
    Grain* slot = nullptr;
    for (auto& g : grains)
        if (! g.active)
        {
            slot = &g;
            break;
        }
    if (slot == nullptr)
        return;   // pool full: CPU stays bounded

    double ratio = std::exp2 (std::clamp (settings.tuneSemitones, -24.0, 24.0) / 12.0);
    if (remapAmount > 0.0 && rng.nextDouble() < remapAmount)
        ratio *= rng.nextDouble() < 0.6 ? 2.0 : 1.5;   // KALEIDOSCOPE: an octave or a fifth above
    const double speed = std::max (1.0e-4, step * ratio);

    // How much has been played (the grain may only read that), and how fast it reads
    // against the recording (a faster grain must start far enough back not to overtake it,
    // a slower one must finish before what it reads is overwritten). 15 % margin for wow.
    const double capacity = static_cast<double> (recording.size()) - 8.0;
    const double history = std::min (recording.history(), capacity) - 4.0;
    if (history < 8.0)
        return;
    const double ahead = std::max (0.0, speed - 1.0) * 1.15;
    const double behind = std::max (0.0, 1.0 - speed) * 1.15;
    double samples = std::clamp (settings.sizeSeconds, 0.005, 1.0) * rate;
    if (ahead > 0.0)
        samples = std::min (samples, (history - 4.0) / ahead);
    if (behind > 0.0)
        samples = std::min (samples, (capacity - 4.0) / behind);
    if (samples < 16.0)
        return;
    const double minDelay = 3.0 + ahead * samples;
    const double maxDelay = std::min (history, capacity - behind * samples);
    if (maxDelay < minDelay)
        return;

    // Where: POS of the way back through what has been played (up to `reach` of the
    // recording), scattered by SPREAD, plus a few milliseconds of jitter so SPREAD 0 never
    // combs - as ANDOR OSP's grains around POS in a file.
    const double span = std::min (history, capacity) * reach;
    const double back = std::clamp (settings.position, 0.0, 1.0) * span;
    const double offset = (std::clamp (settings.spread, 0.0, 1.0) * 0.5 * span + 0.004 * rate) * rng.bipolar();
    const double delay = std::clamp (back + offset, minDelay, maxDelay);

    const auto n = static_cast<int> (samples);
    const double w = 2.0 * std::numbers::pi / static_cast<double> (n);
    slot->position = static_cast<double> (recording.now()) - delay;
    slot->ratio = ratio;
    slot->c = 1.0;
    slot->s = 0.0;
    slot->cd = std::cos (w);
    slot->sd = std::sin (w);
    slot->remaining = n;
    // Rows by the golden ratio: evenly spread, no random draw (the sound's stream is untouched).
    const double row = 0.5 + 0.6180339887498949 * static_cast<double> (spawned++);
    slot->lane = static_cast<float> (row - std::floor (row));
    slot->active = true;
    playing[static_cast<std::size_t> (activeGrains++)] = static_cast<int> (slot - grains.data());
}

int LiveGranulator::collect (const LiveRecording& recording, GrainDot* out, int max) const noexcept
{
    const double scale = reachSamples (recording);
    if (activeGrains == 0 || scale < 1.0)
        return 0;
    const auto now = static_cast<double> (recording.now());
    int n = 0;
    for (int k = 0; k < activeGrains && n < max; ++k)
    {
        const auto& g = grains[static_cast<std::size_t> (playing[static_cast<std::size_t> (k)])];
        out[n].position = static_cast<float> (std::clamp ((now - g.position) / scale, 0.0, 1.0));
        out[n].level = static_cast<float> (0.5 - 0.5 * g.c);
        out[n].lane = g.lane;
        ++n;
    }
    return n;
}

float LiveGranulator::render (const LiveRecording& recording, double step) noexcept
{
    if (spawning && --countdown <= 0)
    {
        spawn (recording, step);
        const double interval = rate / std::clamp (settings.density, 1.0, 80.0);
        countdown = std::max (1, static_cast<int> (interval * rng.uniform (0.8, 1.2)));
    }
    if (activeGrains == 0)
        return 0.0f;

    // Every grain adds its four Hermite taps, weighted by its window (ANDOR OSP's read).
    // Only the playing grains are visited (their slots listed in `playing`).
    using osp::simd::Float4;
    const auto now = static_cast<double> (recording.now());
    const double newest = now - 3.0;
    const double oldest = now - recording.history() + 1.0;
    auto acc = Float4::zero();
    int kept = 0;
    for (int n = 0; n < activeGrains; ++n)
    {
        auto& g = grains[static_cast<std::size_t> (playing[static_cast<std::size_t> (n)])];
        const auto window = static_cast<float> (0.5 - 0.5 * g.c);
        const double pos = g.position < oldest ? oldest : (g.position > newest ? std::max (oldest, newest) : g.position);
        const auto i = static_cast<std::int64_t> (pos);   // (tape time is never negative)
        const auto wts = osp::hermiteWeights (static_cast<float> (pos - static_cast<double> (i))) * Float4::broadcast (window);
        acc = acc + wts * Float4::load (recording.taps (i - 1));
        g.position += step * g.ratio;
        const double c = g.c * g.cd - g.s * g.sd;
        g.s = g.s * g.cd + g.c * g.sd;
        g.c = c;
        if (--g.remaining <= 0)
            g.active = false;
        else
            playing[static_cast<std::size_t> (kept++)] = playing[static_cast<std::size_t> (n)];
    }
    activeGrains = kept;
    return acc.sum() * norm;
}

} // namespace andor::analog
