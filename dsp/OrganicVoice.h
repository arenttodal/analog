#pragma once

#include "model/InstrumentModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace andor::analog
{

/**
    ORGANIC in one voice: a recording (ANDOR OSP's prepared model, built by its loader on the
    shell's layer B) played at the note's pitch, mono, into the voice's mixer beside the
    oscillators. Held notes on a recording that sustains cycle through ANDOR OSP's own
    continuation analysis (its best loop, crossfaded where it measured the two windows
    alike); REVERSE plays it backwards from START.

    Real-time safe: no allocation after prepare(); reads only the model it was started with,
    which the synth guarantees alive (the shell's setSource contract; a swap fades the old
    sound from a short copy: captureTail()).
*/
class OrganicVoice
{
public:
    /** Message thread: the internal rate, and room for a swap's fade (seconds). */
    void prepare (double internalRate, double tailSeconds = 0.006)
    {
        rate = internalRate;
        tail.assign (static_cast<std::size_t> (std::max (1.0, std::ceil (tailSeconds * internalRate))), 0.0f);
        tailLength = tailAt = 0;
    }

    /** A note starts on `m` (null: silent) at `startFraction` of the recording. */
    void start (const osp::InstrumentModel* m, double startFraction, bool reverseOn) noexcept
    {
        model = m != nullptr && m->isValid() ? m : nullptr;
        crossfading = false;
        finishedFlag = model == nullptr;
        if (model == nullptr)
            return;
        const auto& src = *model->original.source;
        const double frames = static_cast<double> (src.numFrames());
        reverse = reverseOn;
        const double from = std::clamp (startFraction, 0.0, 0.999) * frames;
        position = reverse ? frames - 1.0 - from : std::max (src.startFrame(), from);
        // The loop: one of ANDOR OSP's continuation jumps that goes back (its jumps are a
        // walk, some forward): its best loop if that lies ahead of the start, else the latest
        // one ahead (the most of the body played before it cycles). A note started past every
        // loop plays out. Forward playing only.
        jump = nullptr;
        const auto& c = model->original.continuation;
        if (! reverse && c.hasJumps())
        {
            auto usable = [this] (const osp::ContinuationJump& j) { return j.toFrame < j.fromFrame && j.fromFrame > position; };
            if (c.bestLoop >= 0 && c.bestLoop < static_cast<int> (c.jumps.size()) && usable (c.jumps[static_cast<std::size_t> (c.bestLoop)]))
                jump = &c.jumps[static_cast<std::size_t> (c.bestLoop)];
            else
                for (const auto& j : c.jumps)
                    if (usable (j) && (jump == nullptr || j.fromFrame > jump->fromFrame))
                        jump = &j;
        }
    }

    /** The source frames per internal sample for a pitch (MIDI note): from the root. */
    double stepFor (double notePitch, double rootOverride) const noexcept
    {
        if (model == nullptr)
            return 0.0;
        const auto& src = *model->original.source;
        const double root = rootOverride >= 0.0 ? rootOverride : src.rootMidi();
        return src.sampleRate() / rate * std::exp2 ((notePitch - root) / 12.0);
    }

    /** One sample (mono: the recording's channels summed at equal weight). `holding`: the key
        (or pedal) holds the note and LOOP is on: a sustaining recording keeps cycling. */
    float next (double step, bool holding) noexcept
    {
        float out = 0.0f;
        if (tailAt < tailLength)
            out += tail[static_cast<std::size_t> (tailAt++)];
        if (model == nullptr || finishedFlag)
            return out;
        const auto& src = *model->original.source;
        const double frames = static_cast<double> (src.numFrames());
        float value = read (src, position);
        if (crossfading)
        {
            // Into the loop's destination over its measured crossfade, the law set by how alike
            // ANDOR OSP measured the two windows: equal gain when they match (no swell), equal
            // power when they do not (no dip).
            const float x = static_cast<float> (std::clamp (fadeDone / fadeLength, 0.0, 1.0));
            const float r = std::clamp (jump->correlation, 0.0f, 1.0f);
            const float a = r * (1.0f - x) + (1.0f - r) * std::cos (0.5f * 3.14159265f * x);
            const float b = r * x + (1.0f - r) * std::sin (0.5f * 3.14159265f * x);
            value = a * value + b * read (src, fadePosition);
            fadePosition += step;
            fadeDone += step;
            if (fadeDone >= fadeLength)
            {
                crossfading = false;
                position = fadePosition;
                value = read (src, position);
            }
        }
        out += value * static_cast<float> (src.playbackGain());
        position += reverse ? -step : step;
        if (! crossfading && jump != nullptr && holding && position >= jump->fromFrame)
        {
            crossfading = true;
            fadePosition = jump->toFrame + (position - jump->fromFrame);
            fadeLength = std::max (step, jump->crossfadeFrames);
            fadeDone = 0.0;
        }
        if (position >= frames - 1.0 || position < 0.0)
            finishedFlag = true;
        return out;
    }

    /** The recording has played out (a released or one-shot note ends with it). */
    bool finished() const noexcept { return finishedFlag && tailAt >= tailLength; }
    bool playing() const noexcept { return model != nullptr && ! finishedFlag; }
    const osp::InstrumentModel* playingModel() const noexcept { return model; }
    /** How far it is, 0..1 of the recording (the card's playhead; MORPH's place in the profile). */
    double seconds() const noexcept
    {
        return model != nullptr ? position / std::max (1.0, model->original.source->sampleRate()) : 0.0;
    }

    /** Where the read head is, 0..1 of the recording the card shows (ANDOR OSP's playhead:
        a model's display range maps it). False when not playing. */
    bool playhead (float& where) const noexcept
    {
        if (! playing())
            return false;
        const auto frames = static_cast<double> (std::max<std::int64_t> (1, model->original.source->numFrames()));
        const double inSource = std::clamp (position / frames, 0.0, 1.0);
        where = static_cast<float> (std::clamp (model->displayStart + inSource * (model->displayEnd - model->displayStart), 0.0, 1.0));
        return true;
    }

    /** The model is about to be replaced (the old one is still readable now, not after):
        the next few milliseconds of what this voice plays are copied and faded out, then the
        voice stops reading it. */
    void captureTail (double step, bool holding) noexcept
    {
        if (model == nullptr || finishedFlag)
        {
            model = nullptr;
            return;
        }
        const int n = static_cast<int> (tail.size());
        for (int i = 0; i < n; ++i)
        {
            const float fade = 1.0f - static_cast<float> (i) / static_cast<float> (n);
            const float pending = tailAt < tailLength ? tail[static_cast<std::size_t> (tailAt++)] : 0.0f;
            tail[static_cast<std::size_t> (i)] = pending + fade * next (step, holding);
        }
        tailLength = n;
        tailAt = 0;
        model = nullptr;
        finishedFlag = true;
    }

    void stop() noexcept
    {
        model = nullptr;
        finishedFlag = true;
        tailLength = tailAt = 0;
    }

private:
    /** Hermite (4-point) between frames, the channels averaged; zero outside the recording. */
    static float read (const osp::PlaybackSource& src, double at) noexcept
    {
        const auto i = static_cast<std::int64_t> (std::floor (at));
        const auto f = static_cast<float> (at - static_cast<double> (i));
        const int pad = src.padding();
        if (i < 1 - pad || i + 2 >= src.numFrames() + pad)
            return 0.0f;
        float sum = 0.0f;
        const int channels = src.numChannels();
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* d = src.channelData (ch) + i;   // (channelData skips the padding: d[-1] .. d[2] exist inside it)
            const float xm1 = d[-1], x0 = d[0], x1 = d[1], x2 = d[2];
            const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            sum += ((c3 * f + c2) * f + c1) * f + x0;
        }
        return sum / static_cast<float> (std::max (1, channels));
    }

    double rate = 192000.0;
    const osp::InstrumentModel* model = nullptr;
    const osp::ContinuationJump* jump = nullptr;
    double position = 0.0;
    bool reverse = false, finishedFlag = true;
    bool crossfading = false;
    double fadePosition = 0.0, fadeLength = 1.0, fadeDone = 0.0;
    std::vector<float> tail;
    int tailLength = 0, tailAt = 0;
};

} // namespace andor::analog
