#pragma once

#include "MorphProfile.h"
#include "../Patch.h"

#include "core/Prng.h"

#include <array>
#include <cstdint>

namespace andor::analog::morph
{

/** The analog carrier's spectrum as FUSE and WEAVE take it: relative harmonic amplitudes
    (harmonic h at index h - 1), from the slot's waves (analytic: no measurement per voice). */
using CarrierSpectrum = std::array<float, MorphProfile::partials>;

/**
    MORPH in one voice: the ORGANIC recording turned into the analog carrier (OSC 1, OSC 2 or
    the levelled pair) by one of three methods, all with the same exact ends. With b the core's
    weight,
        out = a o + b M + c C,   b = sqrt (4 p (1 - p)),   a = (1 - p)(1 - b),   c = p (1 - b),
    so at p = 0 the output is the recording o, at p = 1 the carrier C, both sample for sample,
    and in the middle the morph core M alone (a fusion, not a crossfade).

      - IMPRINT: the carrier through a constant-Q band bank whose gains carry the recording's
        spectral envelope (at its own formant positions) over the carrier's own band energy,
        measured as it plays: G = clamp (E_acoustic / E_carrier), applied as G^s (log
        interpolation), s = 1 - p; plus the recording's residual texture as shaped noise.
      - FUSE: coherent additive partials on the carrier's fundamental (phase rotators locked to
        its pitch: no FFT phases are ever interpolated), each one's magnitude interpolated in
        log between the recording's analysed trajectory (formant-corrected: the body's
        resonances stay where they are as the pitch moves) and the carrier's spectrum; the
        residual's noise beside them.
      - WEAVE: the same partials, but whole groups of them taken from one side or the other in
        a seeded order the position sweeps through (smoothed masks): the recording's own
        trajectories, vibrato and all, interleaved with the carrier's steady ones.

    The additive partials and the noise run at the host rate and are interpolated up: their
    images land in the decimators' stopband. IMPRINT's bank runs at the internal rate (it
    filters the carrier itself). Real-time safe; deterministic from the note's seed.
*/
class MorphVoice
{
public:
    static constexpr int bands = 11;          ///< IMPRINT: 2/3-octave bands, 100 Hz .. 10.2 kHz
    static constexpr int noiseBands = 8;      ///< the residual's texture, octave bands
    static constexpr int maxPartials = 48;    ///< FUSE / WEAVE (DETAIL: 8 .. 48)

    void prepare (double internalRate, int oversampling) noexcept;
    /** A note starts: its profile (null: the morph has nothing to fuse and is bypassed). */
    void start (const MorphProfile* profile, std::uint64_t seed) noexcept;

    struct Control
    {
        double position = 0.0;            ///< 0 recording .. 1 carrier
        MorphMethod method = MorphMethod::imprint;
        double texture = 0.5, attack = 0.5, formants = 0.0, detail = 0.5;
        bool levelMatch = true;
        double profileSeconds = 0.0;      ///< where the recording's playhead is (its own time)
        double fundamentalHz = 220.0;     ///< the carrier's
        double sourceRatio = 1.0;         ///< the recording's playback speed (its formants move with it)
        const CarrierSpectrum* carrier = nullptr;
    };
    /** Control rate (the synth's period): the profile frame, the gains, the partials' targets. */
    void control (const Control& c) noexcept;

    /** One internal sample: `organic` (the recording as the voice plays it) and `carrier`;
        returns the morph's output. */
    float process (float organic, float carrier) noexcept;
    /** The morph core alone, for LAYERED (b M: no dry ends). */
    float lastCore() const noexcept { return coreOut; }
    bool active() const noexcept { return profile != nullptr; }

private:
    struct Svf   // TPT state-variable band-pass (peak gain 1)
    {
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, s1 = 0.0f, s2 = 0.0f;
        void set (double hz, double q, double rate) noexcept;
        float band (float x) noexcept;
    };
    void renderHost() noexcept;   // one host-rate step of the partials and the noise

    const MorphProfile* profile = nullptr;
    double rate = 192000.0, hostRate = 48000.0;
    int factor = 4, phaseInHost = 0;
    osp::Prng noise { 1 };

    // Weights of the endpoint law (smoothed per sample towards the control rate's).
    float wOrganic = 1.0f, wCore = 0.0f, wCarrier = 0.0f;
    float tOrganic = 1.0f, tCore = 0.0f, tCarrier = 0.0f;
    float coreOut = 0.0f;
    MorphMethod method = MorphMethod::imprint;

    // IMPRINT: the bank on the carrier, its band energies, the gains (now, target).
    std::array<Svf, bands> bank {};
    std::array<float, bands> carrierEnergy {}, gain {}, gainTarget {};
    float energyCoef = 0.001f;

    // FUSE / WEAVE: rotators (cos, sin), their step, amplitudes (now, target).
    int partialCount = 24;
    std::array<float, maxPartials> re {}, im {}, stepRe {}, stepIm {}, amp {}, ampTarget {};
    // The residual's texture: octave-band noise (host rate), its gains.
    std::array<Svf, noiseBands> noiseBank {};
    std::array<float, noiseBands> noiseGain {}, noiseTarget {};
    // Host-rate outputs, linearly interpolated up: previous and current.
    float additivePrev = 0.0f, additiveNow = 0.0f, noisePrev = 0.0f, noiseNow = 0.0f;
    // Level matching: the recording's, the carrier's and the core's loudness.
    float organicLevel = 0.0f, carrierLevel = 0.0f, coreLevel = 0.0f, matchGain = 1.0f, matchTarget = 1.0f;
    float targetLevelDb = -120.0f;
    float attackKeep = 0.0f;
    bool fresh = true;   ///< the first control after start(): everything lands on its target
    std::array<float, maxPartials> weaveOrder {};   ///< WEAVE: each group's threshold (seeded)
    // The residual's bands per octave band of the noise (recomputed when FORMANTS moves).
    std::array<std::int8_t, MorphProfile::residualBands> noiseBandOf {};
    double mappedShift = -1.0;
    std::array<float, bands> bandCentre {};
};

/** The analytic spectrum of a slot's waves (saw 2 / (pi h), pulse 4 |sin (pi h w)| / (pi h),
    triangle 8 / (pi h)^2 on odd h), times the slot's level: harmonic h at index h - 1. */
void addCarrierSpectrum (CarrierSpectrum& out, bool saw, bool pulse, bool triangle, float width, float gain) noexcept;

} // namespace andor::analog::morph
