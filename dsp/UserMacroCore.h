#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace andor::analog::usermacro
{

/**
    USER MACROS V1.1 (docs/USER_MACROS_V1_1.md): the pure core - the anchored, zero-jump mapping
    of one macro onto one target, the bounded compiled table the audio thread reads, and the
    lock-free hand-over of that table. No JUCE, no allocation, no state but what is passed in.

    Every value is in the destination's normalised (0..1) parameter domain: a mapping is an
    additive offset on the target's own (live) value, never a write to it.
*/
constexpr int macroCount = 6;
constexpr int mapsPerMacro = 8;
constexpr int maxMaps = macroCount * mapsPerMacro;   ///< 48

enum class Curve : std::uint8_t { linear, ease };

/** One assignment's shape (spec §6.2). Created with low = high = anchorTarget: no influence. */
struct Mapping
{
    float anchorMacro = 0.0f;    ///< a: the macro's position when the assignment was made
    float anchorTarget = 0.0f;   ///< b0: the target's normalised value then
    float low = 0.0f;            ///< L: where the target goes at macro 0 (from b0)
    float high = 0.0f;           ///< H: where it goes at macro 1
    Curve curve = Curve::linear;
    bool enabled = true;
    bool operator== (const Mapping&) const = default;
};

/** The offset a mapping adds to its target at macro position `m` (0..1). Exactly 0 at the
    anchor, so making an assignment never jumps; LOW above HIGH inverts it. */
inline double offsetOf (const Mapping& map, double m) noexcept
{
    if (! map.enabled)
        return 0.0;
    constexpr double epsilon = 1.0e-6;
    m = std::clamp (m, 0.0, 1.0);
    const double a = std::clamp (static_cast<double> (map.anchorMacro), 0.0, 1.0);
    const double b0 = static_cast<double> (map.anchorTarget);
    auto shape = [&map] (double t) { return map.curve == Curve::ease ? t * t * (3.0 - 2.0 * t) : t; };   // (on the distance, never the value)
    if (m < a && a > epsilon)
        return (static_cast<double> (map.low) - b0) * shape ((a - m) / a);
    if (m > a && a < 1.0 - epsilon)
        return (static_cast<double> (map.high) - b0) * shape ((m - a) / (1.0 - a));
    return 0.0;
}

/** A sanitised mapping: finite, in range (a NaN or an infinity becomes the neutral value). */
inline Mapping sanitised (Mapping map) noexcept
{
    auto unit = [] (float v, float fallback) { return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : fallback; };
    map.anchorMacro = unit (map.anchorMacro, 0.0f);
    map.anchorTarget = unit (map.anchorTarget, 0.0f);
    map.low = unit (map.low, map.anchorTarget);
    map.high = unit (map.high, map.anchorTarget);
    return map;
}

/** Where a target lives (the audio thread's address for it). */
enum class TargetKind : std::uint8_t { synth, fx };

struct CompiledMap
{
    std::uint8_t macro = 0;               ///< 0..5
    TargetKind kind = TargetKind::synth;
    std::int16_t index = -1;              ///< the reader's value index (synth: params::Reader; fx: fxparams::Reader)
    Mapping map;
};

/** The table the audio thread reads: bounded, trivially copyable, active assignments only
    (an orphaned or disabled one is never compiled in). */
struct Snapshot
{
    int count = 0;
    std::array<CompiledMap, maxMaps> maps {};
    bool empty() const noexcept { return count == 0; }
};

/**
    A single-writer (message thread), single-reader (audio thread) triple buffer for a
    trivially copyable value: the writer never waits for the reader, the reader never waits
    and never allocates; the reader sees the latest complete value at its next read.
*/
template <typename T>
class TripleBuffer
{
public:
    /** Writer: hands over a new value (the previous unread one, if any, is dropped). */
    void publish (const T& value) noexcept
    {
        slots[static_cast<std::size_t> (back)] = value;
        back = middle.exchange (back | fresh, std::memory_order_acq_rel) & indexMask;
    }
    /** Reader: the latest value handed over (taking a newer one first if there is one). */
    const T& read() noexcept
    {
        if ((middle.load (std::memory_order_acquire) & fresh) != 0)
            front = middle.exchange (front, std::memory_order_acq_rel) & indexMask;
        return slots[static_cast<std::size_t> (front)];
    }

private:
    static constexpr int fresh = 4, indexMask = 3;
    std::array<T, 3> slots {};
    std::atomic<int> middle { 1 };
    int front = 0, back = 2;
};

} // namespace andor::analog::usermacro
