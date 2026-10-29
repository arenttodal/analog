#pragma once

#include "core/Prng.h"
#include "engine/GranularSource.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace andor::analog
{

/**
    A voice's own recent sound: a rolling recording the oscillators write, sample by sample,
    and grains read. Positions are counted in samples since the note started ("tape time"),
    so a read position stays valid while the recording rolls on; a sample is kept for one
    buffer length. Four guard samples after the end repeat the first four, so a four-point
    read never wraps. prepare() allocates; the rest is real-time safe.
*/
class LiveRecording
{
public:
    void prepare (int lengthSamples);
    /** Forgets what was recorded (a new note starts from silence). */
    void restart() noexcept
    {
        written = 0;
        head = 0;
    }
    void write (float x) noexcept
    {
        data[static_cast<std::size_t> (head)] = x;
        if (head < guard)
            data[static_cast<std::size_t> (length + head)] = x;
        if (++head == length)
            head = 0;
        ++written;
    }
    /** Samples written since the note started (the next sample's tape time). */
    std::int64_t now() const noexcept { return written; }
    /** How far back a read may reach now (samples; at most the buffer's length less a margin). */
    double history() const noexcept { return static_cast<double> (std::min<std::int64_t> (written, length - guard)); }
    int size() const noexcept { return length; }
    /** The four samples from tape time `first` (first..first+3, each already written and
        still kept): counted back from the write head, so no division. */
    const float* taps (std::int64_t first) const noexcept
    {
        auto i = head - static_cast<int> (written - first);
        if (i < 0)
            i += length;
        return data.data() + i;
    }

private:
    static constexpr int guard = 4;
    std::vector<float> data;
    int length = 1, head = 0;
    std::int64_t written = 0;
};

/**
    GRANULAR on a live voice: ANDOR OSP's granulator (GranularSource: Hann-windowed grains,
    cubic reads, its density, size, scatter, tuning and level correction, the same seeded
    scheduler) reading the voice's own recording instead of a file. POS is how far back in
    time the grains are taken (0: just played .. 1: about a second ago), SPREAD scatters them
    around it. A grain never reads ahead of what has been played: at a note's start, while
    little is recorded, the grains come from just behind the playing sound and reach further
    back as the note goes on.

    `remapAmount` (KALEIDOSCOPE): that share of the grains plays an octave or a fifth above.
    Deterministic (seeded per note), real-time safe (a fixed pool, no allocation).
*/
class LiveGranulator
{
public:
    /** A playing grain as the card draws it: `position` how far back it reads on POS's scale
        (0 just played .. 1 POS at 100 %), `level` its window now, `lane` its row (0..1). */
    struct GrainDot
    {
        float position = 0.0f, level = 0.0f, lane = 0.5f;
    };
    static constexpr int maxGrains = osp::GranularSource::maxGrains;
    /** POS 1 reaches this share of the recording back (the rest is room for SPREAD and the grain). */
    static constexpr double reach = 0.7;

    void prepare (double sampleRate) noexcept { rate = sampleRate; }
    void start (const osp::GranularParams& params, std::uint64_t seed) noexcept;
    void setParams (const osp::GranularParams& params) noexcept;
    void setRemap (double amount) noexcept { remapAmount = amount; }
    void stop() noexcept;
    int grainCount() const noexcept { return activeGrains; }
    /** Writes up to `max` playing grains (reading `recording`); returns how many. */
    int collect (const LiveRecording& recording, GrainDot* out, int max) const noexcept;

    /** One sample from `recording` (which already holds everything up to now). `step`:
        how fast grains read relative to the recording (1: its own pitch; REIMAGINED's wow). */
    float render (const LiveRecording& recording, double step) noexcept;

private:
    struct Grain
    {
        double position = 0.0, ratio = 1.0;   // tape time; read speed
        double c = 1.0, s = 0.0, cd = 1.0, sd = 0.0;   // Hann window by rotation
        int remaining = 0;
        float lane = 0.5f;   // its row on the card (drawn only)
        bool active = false;
    };
    void spawn (const LiveRecording& recording, double step) noexcept;
    /** How far back POS 1 reaches now (samples): the scale grains are placed on. */
    static double reachSamples (const LiveRecording& recording) noexcept
    {
        const double capacity = static_cast<double> (recording.size()) - 8.0;
        return (std::min (recording.history(), capacity) - 4.0) * reach;   // as spawn() places them
    }

    osp::GranularParams settings;
    double rate = 48000.0, remapAmount = 0.0;
    osp::Prng rng;
    std::array<Grain, maxGrains> grains {};
    std::array<int, maxGrains> playing {};   ///< the active grains' slots, in the order they started
    int activeGrains = 0, countdown = 0;
    std::uint32_t spawned = 0;   ///< grains started this note (their lanes)
    bool spawning = false;
    float norm = 1.0f;
};

} // namespace andor::analog
