#pragma once

// ORGANIC / MORPH: the analysed description of one recording that ANDOR ANALOG's voices morph
// towards (and away from) their analog oscillators. Pure C++20, no JUCE.
//
// A profile is made once per recording on a worker thread (MorphAnalyzer.h), then shared
// read-only by all 16 voices. Everything a renderer needs is in the frames: per-frame f0,
// 96 harmonic amplitudes, the noise between them in 32 bands and a smoothed spectral
// (formant) envelope in 48 bands, plus a few keyframes (onset, body, a loop region).
//
// Units: seconds, Hz, linear amplitude (full scale = 1) unless a name says dB.

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace andor::analog::morph
{

struct MorphProfile
{
    static constexpr int schemaVersion = 1;
    static constexpr int analyzerVersion = 1;      ///< bump when the analysis changes meaning: invalidates caches
    static constexpr int partials = 96;
    static constexpr int residualBands = 32;       ///< log-spaced, residualBandEdgeHz(): 40 Hz .. 20 kHz
    static constexpr int envelopeBands = 48;       ///< log-spaced, envelopeBandEdgeHz(): 40 Hz .. 18 kHz

    // Band layouts are part of the schema: renderers map them back to Hz with these.
    static constexpr double residualLowHz = 40.0, residualHighHz = 20000.0;
    static constexpr double envelopeLowHz = 40.0, envelopeHighHz = 18000.0;

    /** Edge `i` (0..residualBands) of the residual bands; band b spans [edge b, edge b+1). */
    static double residualBandEdgeHz (int i) noexcept
    {
        return residualLowHz * std::pow (residualHighHz / residualLowHz, static_cast<double> (i) / residualBands);
    }
    static double residualBandCentreHz (int b) noexcept { return std::sqrt (residualBandEdgeHz (b) * residualBandEdgeHz (b + 1)); }

    /** Edge `i` (0..envelopeBands) of the envelope bands; band b spans [edge b, edge b+1). */
    static double envelopeBandEdgeHz (int i) noexcept
    {
        return envelopeLowHz * std::pow (envelopeHighHz / envelopeLowHz, static_cast<double> (i) / envelopeBands);
    }
    static double envelopeBandCentreHz (int b) noexcept { return std::sqrt (envelopeBandEdgeHz (b) * envelopeBandEdgeHz (b + 1)); }

    std::string contentKey;                        ///< FNV-1a 64 (hex) of the samples' bits + sample rate + analyzerVersion; empty when cancelled
    double sampleRate = 0.0;
    std::int64_t numFrames = 0;                    ///< samples in the recording (not analysis frames)

    enum class Kind
    {
        pitched,
        unpitched,
        silent,
        tooShort
    };
    Kind kind = Kind::silent;

    double fundamentalHz = 0.0, rootMidi = 60.0, rootConfidence = 0.0;   ///< root from the body's median voiced f0 (or the override)
    double hopSeconds = 0.0;                       ///< analysis frame hop; frame k is centred at k * hopSeconds

    struct Frame
    {
        float seconds = 0, f0Hz = 0 /* 0 when unvoiced */, voicing = 0 /* 0..1, voiced >= 0.5 */, level = 0 /* RMS, linear */;
        std::array<float, partials> amp {};             ///< linear amplitude of harmonic h+1 of this frame's f0 (0 above Nyquist or when unvoiced)
        std::array<float, residualBands> residual {};   ///< noise RMS per band (linear), with the partials removed
        std::array<float, envelopeBands> envelopeDb {}; ///< smoothed spectral envelope, dB re full scale (-120 floor): partial peaks when voiced, residual band RMS when not
    };
    std::vector<Frame> frames;

    int onsetFrame = 0, bodyFrame = 0;             ///< first audible frame; first frame after the attack settles
    bool sustains = false;
    int loopStartFrame = 0, loopEndFrame = 0;      ///< inclusive range of a stable region a held note may cycle through
    std::vector<int> transientFrames;              ///< spectral-flux peaks (ascending)
    float peak = 0;                                ///< sample peak (linear)
    float loudnessDb = -120;                       ///< RMS of the body in dBFS
    float inharmonicity = 0;                       ///< B in f_h = h f0 sqrt(1 + B h^2)
    std::string status;                            ///< human-readable: what was found or why it fell back

    /** Interpolated frame at `seconds` (clamped to the frames), for renderers: linear in time,
        log-domain for amplitudes and f0, linear for dB and voicing. A default Frame when empty. */
    Frame at (double seconds) const;
};

} // namespace andor::analog::morph
