#pragma once

#include "engine/RhythmicShaper.h"

#include <array>
#include <cstdint>

namespace andor::analog
{

/**
    SHAPER DNA (docs/SHAPER_DNA_V1.md): MOVEMENT lane A's SHAPER, per note. Up to three rhythm
    profiles (A / B / C), each a pattern and a rate of the SHAPER's own library (or FOLLOW: the
    SHAPER's own, its CUSTOM included). Each new note is given one profile when it starts and
    keeps it for its life (held, sustained, released). The profiles share the SHAPER's clock,
    PHASE, TARGET, SMOOTH and depth (the MOVEMENT macro): they differ only in pattern and rate.

    This is the pure model: settings, the assignment, the profiles resolved against the SHAPER's
    parameters, and their phase on the shared clock. No state but the assigner's counter; no
    allocation. The audio (per voice) is the Synth's.
*/
struct ShaperDnaSettings
{
    static constexpr int follow = -1;         ///< a profile's pattern or rate: the SHAPER's own
    static constexpr int maxProfiles = 3;

    enum class Assignment : int { cycle, keyZones };

    bool enabled = false;
    Assignment assignment = Assignment::cycle;
    int profiles = 3;                         ///< 2 (A / B) or 3 (A / B / C)
    int notesPerProfile = 1;                  ///< CYCLE: 1, 2 or 3 notes before the next profile
    int splitAB = 59;                         ///< KEY ZONES: A plays keys 0..splitAB (B3)
    int splitBC = 71;                         ///< B plays splitAB+1..splitBC (B4), C above
    /** Each profile's library pattern (0..11) or `follow`. */
    std::array<int, maxProfiles> pattern { follow, follow, follow };
    /** Each profile's rate (osp::ShaperRate as int, 0..5) or `follow`. */
    std::array<int, maxProfiles> rate { follow, static_cast<int> (osp::ShaperRate::eighth), static_cast<int> (osp::ShaperRate::eighthTriplet) };

    bool operator== (const ShaperDnaSettings&) const = default;

    /** In range: profiles 2..3, notes 1..3, splits ordered inside 0..127 (A and B at least one key). */
    ShaperDnaSettings sanitised() const noexcept;
};

namespace shaperDna
{
    /** The profile (0 A, 1 B, 2 C) KEY ZONES gives `note`: inclusive splits. */
    int zoneOf (const ShaperDnaSettings& settings, int note) noexcept;

    /** CYCLE's profile for the `index`-th note (0 first): A x n, B x n, (C x n), A ... */
    int cycleOf (const ShaperDnaSettings& settings, std::uint64_t index) noexcept;

    /** A profile resolved against the SHAPER's parameters: what it plays. */
    struct Profile
    {
        osp::ShaperPattern steps {};
        osp::RhythmicShaper::Span span;
        osp::ShaperRate rate = osp::ShaperRate::sixteenth;
        bool operator== (const Profile& o) const noexcept
        {
            return steps == o.steps && span.low == o.span.low && span.high == o.span.high && rate == o.rate;
        }
    };
    /** FOLLOW takes the SHAPER's pattern (its CUSTOM steps too) and rate. Bounded work (the
        span); call when something changed, never per sample. */
    Profile resolve (const ShaperDnaSettings& settings, int profile, const osp::ShaperParams& shaper) noexcept;

    /** Where a rate's 16-step cycle is (0..1) at `quarterNotes` on the shared clock, moved by
        PHASE (0..1): the SHAPER's own law, so every profile keeps to the same grid. */
    double cyclePhase (double quarterNotes, osp::ShaperRate rate, double phaseOffset) noexcept;

    /** A profile's modulation (0 closed .. 1 open) at `quarterNotes`. */
    float shapeAt (const Profile& profile, double quarterNotes, double smooth, double phaseOffset) noexcept;
} // namespace shaperDna

/** CYCLE's counter: one step per logical note-on (a chord's notes in the order they arrive). */
class ShaperDnaAssigner
{
public:
    void reset() noexcept { count = 0; }
    /** The profile for a new note (CYCLE advances; KEY ZONES reads the key). */
    int assign (const ShaperDnaSettings& settings, int note) noexcept
    {
        if (settings.assignment == ShaperDnaSettings::Assignment::keyZones)
            return shaperDna::zoneOf (settings, note);
        return shaperDna::cycleOf (settings, count++);
    }
    std::uint64_t notesAssigned() const noexcept { return count; }

private:
    std::uint64_t count = 0;
};

} // namespace andor::analog
