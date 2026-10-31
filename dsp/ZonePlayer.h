#pragma once

#include "OrganicVoice.h"
#include "SampleSource.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace andor::analog
{

/**
    One zone of an imported sample source, sounding in one voice: its recording at the note's
    pitch (from the zone's root and tune), mono (its channels summed at equal weight, as
    ORGANIC's), with the zone's loop.

    The loop (Kontakt loop 1, "until end"): once reached it cycles for as long as the voice
    sounds, release included. Forward: the `crossfade` frames before its end blend (equal
    power) into the frames as far before its start, so the joint is the recording's own
    material; the crossfade is kept within the loop's start and half its length. Alternating:
    back and forth between its ends. Without a loop the note plays out and the player ends.

    Real-time safe: no allocation after prepare(); reads only the zone it was started with,
    which the synth guarantees alive (a swap copies a short tail first: captureTail()).
*/
class ZonePlayer
{
public:
    /** Message thread: the internal rate, and room for a swap's fade (seconds). */
    void prepare (double internalRate, double tailSeconds = 0.006)
    {
        rate = internalRate;
        tail.assign (static_cast<std::size_t> (std::max (1.0, std::ceil (tailSeconds * internalRate))), 0.0f);
        tailLength = tailAt = 0;
    }

    void start (const SampleZone* z) noexcept
    {
        zone = z != nullptr && z->audio != nullptr ? z : nullptr;
        finishedFlag = zone == nullptr;
        direction = 1.0;
        if (zone == nullptr)
            return;
        const auto& src = *zone->audio;
        const double frames = static_cast<double> (src.numFrames());
        position = std::clamp (zone->startFrame, 0.0, std::max (0.0, frames - 1.0));
        const auto& l = zone->loop;
        looping = l.on && l.length >= 2.0 && l.start >= 0.0 && l.start + l.length <= frames;
        loopStart = l.start;
        loopEnd = l.start + l.length;
        fade = looping && ! l.alternating ? std::clamp (l.crossfade, 0.0, std::min (l.start, 0.5 * l.length)) : 0.0;
    }

    /** Source frames per internal sample for a pitch (MIDI note). */
    double stepFor (double notePitch) const noexcept
    {
        if (zone == nullptr)
            return 0.0;
        return zone->audio->sampleRate() / rate * std::exp2 ((notePitch - zone->root + zone->tuneSemitones) / 12.0);
    }

    /** One sample. `loopOn`: the card's LOOP (off: the note plays out, past the loop). */
    float next (double step, bool loopOn = true) noexcept
    {
        float out = 0.0f;
        if (tailAt < tailLength)
            out += tail[static_cast<std::size_t> (tailAt++)];
        if (zone == nullptr || finishedFlag)
            return out;
        const auto& src = *zone->audio;
        float value = OrganicVoice::read (src, position);
        const bool cycling = looping && loopOn;
        if (cycling && fade > 0.0 && position >= loopEnd - fade)
        {
            // The loop's end into the material before its start (equal power).
            const auto x = static_cast<float> (std::clamp ((position - (loopEnd - fade)) / fade, 0.0, 1.0));
            value = std::cos (0.5f * 3.14159265f * x) * value + std::sin (0.5f * 3.14159265f * x) * OrganicVoice::read (src, position - (loopEnd - loopStart));
        }
        out += value;
        position += direction * step;
        if (cycling)
        {
            if (zone->loop.alternating)
            {
                if (direction > 0.0 && position >= loopEnd)
                {
                    position = loopEnd - (position - loopEnd);
                    direction = -1.0;
                }
                else if (direction < 0.0 && position <= loopStart)
                {
                    position = loopStart + (loopStart - position);
                    direction = 1.0;
                }
            }
            else if (position >= loopEnd)
                position -= loopEnd - loopStart;
        }
        if (position >= static_cast<double> (src.numFrames()) - 1.0 || position < 0.0)
            finishedFlag = true;
        return out;
    }

    bool playing() const noexcept { return zone != nullptr && ! finishedFlag; }
    bool finished() const noexcept { return finishedFlag && tailAt >= tailLength; }
    const SampleZone* playingZone() const noexcept { return zone; }

    /** Where the read head is, 0..1 of the recording (a card's playhead). */
    bool playhead (float& where) const noexcept
    {
        if (! playing())
            return false;
        where = static_cast<float> (std::clamp (position / std::max (1.0, static_cast<double> (zone->audio->numFrames())), 0.0, 1.0));
        return true;
    }

    /** The source is about to be replaced (still readable now, not after): the next few
        milliseconds are copied and faded, then the player stops reading it. */
    void captureTail (double step) noexcept
    {
        if (zone == nullptr || finishedFlag)
        {
            zone = nullptr;
            return;
        }
        const int n = static_cast<int> (tail.size());
        for (int i = 0; i < n; ++i)
        {
            const float f = 1.0f - static_cast<float> (i) / static_cast<float> (n);
            const float pending = tailAt < tailLength ? tail[static_cast<std::size_t> (tailAt++)] : 0.0f;
            tail[static_cast<std::size_t> (i)] = pending + f * next (step);
        }
        tailLength = n;
        tailAt = 0;
        zone = nullptr;
        finishedFlag = true;
    }

    void stop() noexcept
    {
        zone = nullptr;
        finishedFlag = true;
        tailLength = tailAt = 0;
    }

private:
    double rate = 192000.0;
    const SampleZone* zone = nullptr;
    double position = 0.0, direction = 1.0;
    bool looping = false, finishedFlag = true;
    double loopStart = 0.0, loopEnd = 0.0, fade = 0.0;
    std::vector<float> tail;
    int tailLength = 0, tailAt = 0;
};

} // namespace andor::analog
