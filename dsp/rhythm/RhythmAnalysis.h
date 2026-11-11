#pragma once

#include <cstddef>
#include <vector>

namespace andor::analog::rhythm
{

/**
    RHYTHMIC MORPH, gate 4 (docs/features/SATELLITE_RHYTHM_ARCHITECTURE.md): what a recording's
    rhythm is, analysed offline (a loader thread; never the audio thread). Pure C++, deterministic.

    It measures, it does not invent:
    - onsets (spectral flux, adaptive threshold, refined to the sample on the attack);
    - tempo candidates (the onset curve's autocorrelation, every octave of the period scored,
      a gentle prior around 120 BPM) with a confidence;
    - the loop: when the recording's length is a whole number of bars at a candidate tempo
      (within 4 %), the tempo becomes exactly that (so a synced loop never drifts) and BARS
      and the meter (3 or 4 beats) follow;
    - the beat grid's phase and the downbeat (the bar position with the strongest, lowest
      onsets) - a loop may start with a pickup or silence;
    - two envelopes for C RHYTHM: the level (RMS) and the transients (the onset curve, 0..1).

    A source with no reliable pulse (rain, ambience, a single hit) is UNSURE: its envelopes are
    still there (for free-running modulation), but no tempo is claimed for it.
*/
enum class Kind
{
    silent,     ///< nothing above -80 dBFS (or too short to say)
    rhythmic,   ///< a reliable pulse: SYNC is offered
    unsure      ///< sound without a reliable pulse: FREE
};

struct Candidate
{
    double bpm = 0.0;
    double score = 0.0;   ///< relative (the best is 1)
};

struct Onset
{
    double seconds = 0.0;
    float strength = 0.0f;   ///< 0..1
};

struct Analysis
{
    static constexpr int version = 1;   ///< bump when a result for the same audio can change

    Kind kind = Kind::silent;
    double sampleRate = 48000.0;
    double durationSeconds = 0.0;

    double bpm = 0.0;          ///< the source's tempo (exact when loopFits)
    double confidence = 0.0;   ///< 0..1: how clear the pulse is
    std::vector<Candidate> candidates;   ///< best first (up to 4: half / double included)

    bool loopFits = false;     ///< the length is a whole number of bars at bpm
    int beats = 0;             ///< beats in the loop
    int beatsPerBar = 4;
    double bars = 0.0;
    double downbeatSeconds = 0.0;   ///< the first downbeat (a pickup or silence before it)

    std::vector<Onset> onsets;

    double hopSeconds = 0.005;
    std::vector<float> envelope;    ///< RMS level per hop (linear, unnormalised)
    std::vector<float> transient;   ///< onset strength per hop, 0..1
    float envelopePeak = 0.0f;      ///< the envelope's highest (for normalising)

    double beatSeconds() const noexcept { return bpm > 0.0 ? 60.0 / bpm : 0.0; }
    double loopSeconds() const noexcept { return beats > 0 ? beats * beatSeconds() : durationSeconds; }
};

/** Analyses a mono recording. Never throws; odd input (empty, NaN, silence, tiny) is reported, not fatal. */
Analysis analyse (const float* mono, std::size_t length, double sampleRate);

/** HALF / DOUBLE: the same loop at half or double the tempo (beats halved or doubled, the
    downbeat kept); a manual correction, never applied automatically. */
Analysis withTempoFactor (const Analysis& a, double factor);

} // namespace andor::analog::rhythm
