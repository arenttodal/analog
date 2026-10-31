#pragma once

#include "model/PlaybackSource.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace andor::analog
{

/**
    An imported sample source (the Sample Source importer, MVP A1): several recordings, each
    mapped to an explicit zone of keys and velocities, as a Kontakt group maps them. It plays in
    ORGANIC's place (layer B), through the same card, A/B mix, filter and effects.

    Immutable once built (off the audio thread: audio decoded into ANDOR OSP's PlaybackSource,
    padded for the interpolator); the synth reads it through Synth::setSampleSource with the
    same lifetime contract as ORGANIC's recording.
*/
struct SampleZone
{
    std::shared_ptr<const osp::PlaybackSource> audio;
    int keyLo = 0, keyHi = 127;          ///< MIDI notes, inclusive
    int velLo = 1, velHi = 127;          ///< inclusive
    double root = 60.0;                  ///< the key at which it plays untransposed (MIDI)
    double tuneSemitones = 0.0;          ///< the zone's fine tune
    float gain = 1.0f;                   ///< the zone's level (linear)
    double startFrame = 0.0;             ///< where a note starts (source frames)
    /** Its loop (Kontakt loop 1): forward, or back and forth; `crossfade` source frames before
        its end blend into the frames before its start. Absent: the note plays out. */
    struct Loop
    {
        bool on = false, alternating = false;
        double start = 0.0, length = 0.0, crossfade = 0.0;
    } loop;
    std::string file;                    ///< the recording's name (review, relink)

    bool contains (int key, int velocity) const noexcept { return key >= keyLo && key <= keyHi && velocity >= velLo && velocity <= velHi; }
};

struct SampleSource
{
    static constexpr int schemaVersion = 1;
    /** The most zones one note sounds at once (overlapping zones play together, as in Kontakt). */
    static constexpr int maxLayers = 8;   // (a combined patch layers four groups, each with its own overlaps)

    std::string name;                    ///< e.g. the Kontakt group's name
    std::vector<SampleZone> zones;
    float gain = 1.0f;                   ///< the source's own level (linear: the group's volume)

    /** The zones a key and velocity sound (up to maxLayers, in zone order). Returns how many. */
    int select (int key, int velocity, std::array<int, maxLayers>& out) const noexcept
    {
        int n = 0;
        for (int i = 0; i < static_cast<int> (zones.size()) && n < maxLayers; ++i)
            if (zones[static_cast<std::size_t> (i)].audio != nullptr && zones[static_cast<std::size_t> (i)].contains (key, velocity))
                out[static_cast<std::size_t> (n++)] = i;
        return n;
    }
    bool isValid() const noexcept
    {
        for (const auto& z : zones)
            if (z.audio != nullptr)
                return true;
        return false;
    }
};

} // namespace andor::analog
