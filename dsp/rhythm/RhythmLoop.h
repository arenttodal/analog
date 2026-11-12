#pragma once

#include "RhythmAnalysis.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace andor::analog::rhythm
{

/**
    RHYTHMIC MORPH, gate 5: card C's recording as a loop that follows the host's tempo and bar
    grid without changing its pitch (docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md).

    The method (chosen over a phase-vocoder stretch for V1, see the document): the loop is cut at
    its onsets (each slice from 2 ms before its attack), and every slice starts exactly where the
    host's grid puts it and plays at its original speed - so its pitch and its transient are the
    recording's own, untouched. A slower host leaves a short gap after a slice (it fades out at
    the next slice's start in the source); a faster one cuts it (3 ms crossfade into the next).
    No latency, no allocation, block-size independent.

    A source without a reliable pulse (UNSURE), or SYNC off, plays FREE: continuously, at its own
    speed, round its whole length (5 ms crossfade at the turn).
*/
struct PreparedLoop
{
    std::vector<float> left, right;   ///< the whole recording (source rate)
    double rate = 48000.0;
    Analysis analysis;
    double loopStart = 0.0;           ///< frames: the downbeat
    double loopFrames = 0.0;          ///< frames: one loop (its beats at its tempo)
    std::vector<double> slices;       ///< frames from loopStart, ascending, the first 0
    bool rhythmic() const noexcept { return analysis.kind == Kind::rhythmic && analysis.beats > 0 && loopFrames > 0.0; }
};

/** Prepares a loop (a loader thread: analyses, cuts). Null for no audio. */
std::shared_ptr<const PreparedLoop> prepareLoop (const float* left, const float* right, std::size_t frames, double rate);

class LoopPlayer
{
public:
    struct Settings
    {
        bool sync = true;           ///< follow the host grid (a rhythmic loop only)
        double tempoFactor = 1.0;   ///< HALF 0.5 / DETECTED 1 / DOUBLE 2
        /** Card C's TUNE, OCT and DETUNE as a speed (2^(semitones / 12), 0.25..4): each slice
            still starts on its place in the grid and plays faster (higher, shorter: a gap
            after it) or slower (lower, longer: cut at the next slice), as a sampler's slices. */
        double pitchRatio = 1.0;
        /** Opt-in (D RHYTHM's PITCH / OCT): the level and transient outputs follow the loop as
            played un-pitched - each slice's envelope at its original timing, whatever the
            pitch - so the modulation's pulses never move or squeeze. Off: as the head reads
            (card C's RHYTHM, unchanged). */
        bool envelopeOnGrid = false;
    };

    void prepare (double hostRate) noexcept;
    void reset() noexcept;
    /** The loop to play (owned by the caller, alive while set). A new one crossfades in. */
    void setLoop (const PreparedLoop* loop) noexcept;
    const PreparedLoop* loop() const noexcept { return current; }

    /**
        Renders n samples: the loop (stereo) times `gain` added to outL / outR (either may be
        null: MOD only), and C RHYTHM's raw inputs per sample - the level (0..1, the loop's own
        envelope where it plays) and the transient strength (0..1). `ppq` is the musical position
        of the first sample and `ppqPerSample` its rate; `running` false holds everything still
        (the transport stopped and nothing sounding).
    */
    void render (float* outL, float* outR, float gain, float* level, float* transient, int n, double ppq, double ppqPerSample, bool running,
                 const Settings& settings) noexcept;

    /** Where it plays (source seconds; -1 idle): the card's playhead. */
    double playheadSeconds() const noexcept { return shownPlayhead.load (std::memory_order_relaxed); }

private:
    struct Head
    {
        double pos = 0.0;   ///< source frames (absolute)
        double origin = 0.0;   ///< where it started (envelopeOnGrid: its envelope read from here at the un-pitched speed)
        float gain = 0.0f, target = 0.0f, step = 0.0f;
        bool on = false;
        const PreparedLoop* source = nullptr;
    };
    void start (const PreparedLoop* source, double pos, float rampSeconds) noexcept;   // (the head's origin: pos)
    void fadeAll (float seconds) noexcept;
    float read (const Head& h, int channel) const noexcept;

    double hostRate = 48000.0;
    const PreparedLoop* current = nullptr;
    std::array<Head, 3> heads {};
    int main = -1;           ///< the head that plays now
    int slice = -1;          ///< its slice (sync)
    double freePos = 0.0;    ///< FREE's own position (frames from loopStart)
    bool wasRunning = false;
    double lastQ = 0.0, lastDq = 0.0;   ///< the clock a sample ago (a jump: a seek or the host's loop)
    bool haveLastQ = false;
    std::atomic<double> shownPlayhead { -1.0 };
};

} // namespace andor::analog::rhythm
