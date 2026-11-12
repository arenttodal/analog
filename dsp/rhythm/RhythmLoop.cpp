#include "RhythmLoop.h"

#include <algorithm>
#include <cmath>

namespace andor::analog::rhythm
{

std::shared_ptr<const PreparedLoop> prepareLoop (const float* left, const float* right, std::size_t frames, double rate)
{
    if (left == nullptr || frames == 0 || ! (rate > 0.0))
        return nullptr;
    auto loop = std::make_shared<PreparedLoop>();
    loop->rate = rate;
    loop->left.assign (left, left + frames);
    loop->right.assign (right != nullptr ? right : left, (right != nullptr ? right : left) + frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        if (! std::isfinite (loop->left[i]))
            loop->left[i] = 0.0f;
        if (! std::isfinite (loop->right[i]))
            loop->right[i] = 0.0f;
    }
    std::vector<float> mono (frames);
    for (std::size_t i = 0; i < frames; ++i)
        mono[i] = 0.5f * (loop->left[i] + loop->right[i]);
    loop->analysis = analyse (mono.data(), frames, rate);
    const auto& a = loop->analysis;
    if (a.kind == Kind::rhythmic && a.beats > 0)
    {
        loop->loopStart = a.downbeatSeconds * rate;
        loop->loopFrames = a.loopSeconds() * rate;
        // Slices: anchored on every onset's attack (each read from 2 ms before it), measured
        // from the downbeat round the loop; the downbeat itself always anchors one. (Every
        // onset, a soft one too: a hat under a kick's tail barely raises the level, yet it must
        // land on the grid.)
        loop->slices.push_back (0.0);
        for (const auto& o : a.onsets)
        {
            double at = o.seconds * rate - loop->loopStart;
            at = std::fmod (at + 4.0 * loop->loopFrames, loop->loopFrames);
            if (at > 0.004 * rate && at < loop->loopFrames - 0.004 * rate)
                loop->slices.push_back (at);
        }
        std::sort (loop->slices.begin(), loop->slices.end());
        loop->slices.erase (std::unique (loop->slices.begin(), loop->slices.end(), [rate] (double x, double y) { return y - x < 0.03 * rate; }),
                            loop->slices.end());
    }
    else
    {
        loop->loopStart = 0.0;
        loop->loopFrames = static_cast<double> (frames);
    }
    return loop;
}

void LoopPlayer::prepare (double rate) noexcept
{
    hostRate = rate > 0.0 ? rate : 48000.0;
    reset();
}

void LoopPlayer::reset() noexcept
{
    heads = {};
    main = -1;
    slice = -1;
    freePos = 0.0;
    wasRunning = false;
    haveLastQ = false;
    shownPlayhead.store (-1.0, std::memory_order_relaxed);
}

void LoopPlayer::setLoop (const PreparedLoop* loop) noexcept
{
    if (loop == current)
        return;
    // A new recording: whatever plays fades out (it keeps reading its own until then); the
    // next sample places the new one.
    if (current != nullptr && loop != nullptr)
        fadeAll (0.01f);
    else
        heads = {};   // (no fade to make: nothing, or nothing left to read)
    current = loop;
    main = -1;
    slice = -1;
    freePos = 0.0;
}

void LoopPlayer::fadeAll (float seconds) noexcept
{
    for (auto& h : heads)
        if (h.on)
        {
            h.target = 0.0f;
            h.step = 1.0f / std::max (1.0f, seconds * static_cast<float> (hostRate));
        }
    main = -1;
}

void LoopPlayer::start (const PreparedLoop* source, double pos, float rampSeconds) noexcept
{
    // The old head fades (3 ms); the new one takes a free slot (the quietest if none is free).
    if (main >= 0)
    {
        auto& old = heads[static_cast<std::size_t> (main)];
        old.target = 0.0f;
        old.step = 1.0f / std::max (1.0f, 0.003f * static_cast<float> (hostRate));
    }
    int slot = -1;
    for (int i = 0; i < static_cast<int> (heads.size()); ++i)
        if (! heads[static_cast<std::size_t> (i)].on && i != main)
        {
            slot = i;
            break;
        }
    if (slot < 0)
    {
        slot = main == 0 ? 1 : 0;
        for (int i = 0; i < static_cast<int> (heads.size()); ++i)
            if (i != main && heads[static_cast<std::size_t> (i)].gain < heads[static_cast<std::size_t> (slot)].gain)
                slot = i;
    }
    auto& h = heads[static_cast<std::size_t> (slot)];
    h.on = true;
    h.source = source;
    h.pos = pos;
    h.gain = rampSeconds <= 0.0f ? 1.0f : 0.0f;
    h.target = 1.0f;
    h.step = rampSeconds <= 0.0f ? 1.0f : 1.0f / std::max (1.0f, rampSeconds * static_cast<float> (hostRate));
    main = slot;
}

float LoopPlayer::read (const Head& h, int channel) const noexcept
{
    const auto& data = channel == 0 ? h.source->left : h.source->right;
    const auto frames = static_cast<double> (data.size());
    if (frames < 2.0)
        return 0.0f;
    double p = std::fmod (h.pos, frames);
    if (p < 0.0)
        p += frames;
    const auto i = static_cast<std::size_t> (p);
    const auto j = i + 1 < data.size() ? i + 1 : 0;
    const float t = static_cast<float> (p - static_cast<double> (i));
    return data[i] + t * (data[j] - data[i]);
}

void LoopPlayer::render (float* outL, float* outR, float gain, float* level, float* transient, int n, double ppq, double ppqPerSample, bool running,
                         const Settings& settings) noexcept
{
    const PreparedLoop* loop = current;
    if (running && ! wasRunning)
    {
        main = -1;   // (starting: placed afresh from the clock)
        slice = -1;
        haveLastQ = false;
    }
    wasRunning = running;
    const bool sync = loop != nullptr && settings.sync && loop->rhythmic();
    const double pitch = std::clamp (settings.pitchRatio, 0.25, 4.0);
    const double step = (loop != nullptr ? loop->rate / hostRate : 1.0) * pitch;   // (source frames a host sample: its own speed, times TUNE)
    for (int i = 0; i < n; ++i)
    {
        if (loop != nullptr && running)
        {
            if (sync)
            {
                // Each slice is anchored on its attack: its anchor sits exactly on its place in
                // the host's grid, its 2 ms of pre-roll just before - whatever the tempo.
                const double factor = std::clamp (settings.tempoFactor, 0.25, 4.0);
                const double beats = static_cast<double> (loop->analysis.beats) * factor;
                const double framesPerBeat = loop->loopFrames / beats;
                const double hostSecondsPerBeat = ppqPerSample > 0.0 ? 1.0 / (ppqPerSample * hostRate) : 0.5;
                const double toSource = pitch * hostSecondsPerBeat * loop->rate / framesPerBeat;   // (loop frames on the grid -> source frames played, at TUNE's speed)
                const double preroll = 0.002 * loop->rate;                                 // (source frames)
                double b = std::fmod (ppq + static_cast<double> (i) * ppqPerSample, beats);
                if (b < 0.0)
                    b += beats;
                const double inLoop = b * framesPerBeat;
                const auto count = static_cast<int> (loop->slices.size());
                // The slice whose pre-roll has begun (the next loop's downbeat past the last).
                const double query = inLoop + preroll / toSource;
                int j = static_cast<int> (std::distance (loop->slices.begin(), std::upper_bound (loop->slices.begin(), loop->slices.end(), query))) - 1;
                double anchor = loop->slices[static_cast<std::size_t> (std::max (0, j))];
                if (query >= loop->loopFrames)
                {
                    j = 0;
                    anchor = loop->loopFrames;
                }
                const int next = j + 1;
                const double nextAnchor = (next < count ? loop->slices[static_cast<std::size_t> (next)] : loop->loopFrames) + (anchor >= loop->loopFrames ? loop->loopFrames : 0.0);
                const double elapsed = (inLoop - anchor) * toSource;   // (source frames from the attack; -preroll .. )
                const double expected = loop->loopStart + anchor + elapsed;
                // (it must be silent by the next slice's pre-roll: its 3 ms fade ends there)
                const double fadeFrames = 0.003 * loop->rate * pitch;   // (the 3 ms fade, in source frames at TUNE's speed)
                const bool inSlice = anchor + elapsed < nextAnchor - preroll - fadeFrames;
                const auto* m = main >= 0 ? &heads[static_cast<std::size_t> (main)] : nullptr;
                // A seek or the host's loop: the clock itself jumps (a tempo change never does -
                // the playing slice keeps its own speed; only the next one is placed anew).
                const double qNow = ppq + static_cast<double> (i) * ppqPerSample;
                const bool jumped = haveLastQ && std::abs (qNow - (lastQ + lastDq)) > 1.0e-3;
                lastQ = qNow;
                lastDq = ppqPerSample;
                haveLastQ = true;
                if (j != slice || m == nullptr || m->source != loop || jumped)
                {
                    slice = j;
                    if (inSlice)
                        start (loop, expected, 0.0005f);
                    else if (main >= 0)
                        fadeAll (0.003f);
                }
                else if (m->target > 0.0f)
                {
                    // Where the head really is (it plays at its own speed whatever the tempo did
                    // meanwhile): reaching the source's next slice before the grid does, it fades
                    // (3 ms, ending at that slice's pre-roll) and leaves a gap.
                    const double posInLoop = std::fmod (m->pos - loop->loopStart + 4.0 * loop->loopFrames, loop->loopFrames);
                    double rel = posInLoop - std::fmod (anchor, loop->loopFrames);
                    if (rel < -0.5 * loop->loopFrames)
                        rel += loop->loopFrames;
                    else if (rel > 0.5 * loop->loopFrames)
                        rel -= loop->loopFrames;
                    if (rel >= nextAnchor - anchor - preroll - fadeFrames)
                    {
                        auto& h = heads[static_cast<std::size_t> (main)];
                        h.target = 0.0f;
                        h.step = 1.0f / std::max (1.0f, 0.003f * static_cast<float> (hostRate));
                    }
                }
            }
            else
            {
                // FREE: round the whole recording (or the loop) at its own speed.
                if (main < 0 || heads[static_cast<std::size_t> (main)].source != loop)
                    start (loop, loop->loopStart + freePos, 0.003f);
                else if (freePos >= loop->loopFrames - 0.005 * loop->rate)
                {
                    freePos = 0.0;
                    start (loop, loop->loopStart, 0.005f);   // (the turn: 5 ms crossfade)
                }
                freePos += step;
            }
        }
        else if (main >= 0 || ! running)
            fadeAll (0.005f);

        // The heads: ramps, reads, advances.
        float l = 0.0f, r = 0.0f;
        float envLevel = 0.0f, envTransient = 0.0f;
        for (int k = 0; k < static_cast<int> (heads.size()); ++k)
        {
            auto& h = heads[static_cast<std::size_t> (k)];
            if (! h.on)
                continue;
            if (h.gain < h.target)
                h.gain = std::min (h.target, h.gain + h.step);
            else if (h.gain > h.target)
                h.gain = std::max (h.target, h.gain - h.step);
            if (h.gain <= 0.0f && h.target <= 0.0f)
            {
                h.on = false;
                if (k == main)
                    main = -1;
                continue;
            }
            l += h.gain * read (h, 0);
            r += h.gain * read (h, 1);
            if (k == main)
            {
                // C RHYTHM's inputs: the loop's own envelope where this head reads (so the
                // modulation and the sound are one: never ahead, never behind).
                const auto& a = h.source->analysis;
                if (! a.envelope.empty())
                {
                    const auto frames = static_cast<double> (h.source->left.size());
                    double p = std::fmod (h.pos, frames);
                    if (p < 0.0)
                        p += frames;
                    const double seconds = p / h.source->rate;
                    // The level: the envelope between its frames.
                    const double f = seconds / a.hopSeconds;
                    const auto i0 = std::min (a.envelope.size() - 1, static_cast<std::size_t> (f));
                    const auto i1 = std::min (a.envelope.size() - 1, i0 + 1);
                    const auto t = static_cast<float> (f - static_cast<double> (i0));
                    const float e = a.envelope[i0] + t * (a.envelope[i1] - a.envelope[i0]);
                    envLevel = h.gain * (a.envelopePeak > 0.0f ? e / a.envelopePeak : 0.0f);
                    // The transient: a pulse from the last attack at or before the head, to the
                    // sample (its level, falling over ~30 ms).
                    const auto it = std::upper_bound (a.onsets.begin(), a.onsets.end(), seconds, [] (double x, const Onset& o) { return x < o.seconds; });
                    if (it != a.onsets.begin())
                    {
                        const auto& o = *std::prev (it);
                        envTransient = h.gain * o.level * static_cast<float> (std::exp (-(seconds - o.seconds) / 0.03));
                    }
                }
            }
            h.pos += step;
        }
        if (outL != nullptr)
            outL[i] += gain * l;
        if (outR != nullptr)
            outR[i] += gain * r;
        if (level != nullptr)
            level[i] = envLevel;
        if (transient != nullptr)
            transient[i] = envTransient;
    }
    shownPlayhead.store (main >= 0 && loop != nullptr ? std::fmod (heads[static_cast<std::size_t> (main)].pos, static_cast<double> (loop->left.size())) / loop->rate : -1.0,
                         std::memory_order_relaxed);
}

} // namespace andor::analog::rhythm
