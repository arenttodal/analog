#include "ShaperDna.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

ShaperDnaSettings ShaperDnaSettings::sanitised() const noexcept
{
    ShaperDnaSettings s = *this;
    s.profiles = std::clamp (s.profiles, 2, maxProfiles);
    s.notesPerProfile = std::clamp (s.notesPerProfile, 1, 3);
    s.splitAB = std::clamp (s.splitAB, 0, 126);
    s.splitBC = std::clamp (s.splitBC, s.splitAB + 1, 127);
    for (auto& p : s.pattern)
        p = p < 0 ? follow : std::min (p, osp::RhythmicShaper::patternCount - 1);
    for (auto& r : s.rate)
        r = r < 0 ? follow : std::min (r, static_cast<int> (osp::ShaperRate::thirtySecond));
    return s;
}

namespace shaperDna
{
    int zoneOf (const ShaperDnaSettings& settings, int note) noexcept
    {
        const auto s = settings.sanitised();
        if (note <= s.splitAB)
            return 0;
        if (s.profiles == 2 || note <= s.splitBC)
            return 1;
        return 2;
    }

    int cycleOf (const ShaperDnaSettings& settings, std::uint64_t index) noexcept
    {
        const auto s = settings.sanitised();
        const auto group = index / static_cast<std::uint64_t> (s.notesPerProfile);
        return static_cast<int> (group % static_cast<std::uint64_t> (s.profiles));
    }

    Profile resolve (const ShaperDnaSettings& settings, int profile, const osp::ShaperParams& shaper) noexcept
    {
        const auto s = settings.sanitised();
        const auto i = static_cast<std::size_t> (std::clamp (profile, 0, ShaperDnaSettings::maxProfiles - 1));
        Profile p;
        p.steps = s.pattern[i] == ShaperDnaSettings::follow ? osp::RhythmicShaper::stepsOf (shaper) : osp::RhythmicShaper::patternSteps (s.pattern[i]);
        p.span = osp::RhythmicShaper::span (p.steps, shaper.smooth);
        p.rate = s.rate[i] == ShaperDnaSettings::follow ? shaper.rate : static_cast<osp::ShaperRate> (s.rate[i]);
        return p;
    }

    double cyclePhase (double quarterNotes, osp::ShaperRate rate, double phaseOffset) noexcept
    {
        const double cycle = osp::RhythmicShaper::stepQuarterNotes (rate) * osp::RhythmicShaper::steps;
        double p = quarterNotes / cycle;
        if (phaseOffset != 0.0)
            p += phaseOffset;   // (as the SHAPER: PHASE 0 adds nothing)
        return p - std::floor (p);
    }

    float shapeAt (const Profile& profile, double quarterNotes, double smooth, double phaseOffset) noexcept
    {
        return osp::RhythmicShaper::evaluate (profile.steps, cyclePhase (quarterNotes, profile.rate, phaseOffset), smooth, profile.span);
    }
} // namespace shaperDna

} // namespace andor::analog
