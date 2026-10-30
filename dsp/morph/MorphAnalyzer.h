#pragma once

// Offline analysis of one recording into a MorphProfile (MorphProfile.h). Worker thread only:
// it allocates, runs FFTs and takes tens to hundreds of milliseconds. Never call it from the
// audio thread; hand the finished, immutable profile to the voices instead.

#include "morph/MorphProfile.h"

#include <atomic>
#include <cstdint>
#include <functional>

namespace andor::analog::morph
{

struct AnalysisOptions
{
    /** < 0: detect the root. Otherwise the root (MIDI note, fractional allowed) replaces the
        detected one in fundamentalHz / rootMidi; partials are still measured at the detected
        per-frame f0, so the frames are identical with or without it (and so is contentKey:
        a cache keyed on contentKey can re-apply an override without analysing again). */
    double rootOverrideMidi = -1.0;
};

/**
    Analyses a mono recording at its own sample rate. Worker thread only.

    `cancel` may be null; when it becomes true the analysis stops at the next frame and returns
    a profile whose status starts with "cancelled" (no frames, empty contentKey so it is never
    cached). `progress` (0..1, non-decreasing) may be empty; it is called on this thread.

    Never throws on odd input: silence, noise, DC, a single click, 10 ms or 60 s of audio, NaN
    or infinite samples all return a usable profile with the right Kind and a status.

    Deterministic: the same samples and rate give a bitwise identical profile.
*/
MorphProfile analyse (const float* mono, std::int64_t numFrames, double sampleRate, const AnalysisOptions& options = {},
                      const std::atomic<bool>* cancel = nullptr, const std::function<void (float)>& progress = {});

} // namespace andor::analog::morph
