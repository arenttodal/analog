#pragma once

#include "../Patch.h"

#include "core/Fft.h"
#include "core/Prng.h"

#include <array>
#include <complex>
#include <cstdint>
#include <memory>
#include <vector>

namespace andor::analog::morph
{

/**
    MORPH in one voice: two sources heard live and turned into one sound. The carrier x is
    the oscillators (OSC 1, OSC 2 or the levelled pair: cards A and B), the source y is
    whatever card C plays (a recording, OSC 3, NOISE, an instrument patch's zones). Nothing is
    analysed ahead of time: both are measured as they play, so MORPH works on any sound.

    Analysis, every quarter window (5.3 .. 21 ms) on the last 21 .. 85 ms (windowOrder): both
    sources in one complex FFT (x real, y imaginary). Each source's harmonics are read at its
    own fundamental (the note it plays: the key, card C's OCT and DETUNE), the strongest bin
    within a quarter harmonic of each, parabolically interpolated. What is left after the
    harmonics (the breath, the bow, a noise source) is kept as eight octave bands of residual.

    Synthesis, at the host rate, interpolated up:
      - partials on one fundamental, f = fx^(1 - p) fy'^p, fy' being card C's pitch moved by
        whole octaves to the nearest of the carrier's (the pitch morphs too, octave-aware:
        card C's OCT never puts the fusion between notes, only DETUNE-like offsets glide), each
        partial's level interpolated in dB between the two sources (FUSE), taken from one side
        or the other in a seeded order the position sweeps through (WEAVE), or the carrier's
        own partials with their smooth envelope (2/3 octave) traded for card C's (IMPRINT: a
        vocoder-like imprint at the carrier's pitch, complete from 50 %);
      - the residual bands' noise, interpolated in dB (TEXTURE scales it).
    FORMANTS shifts card C's envelope. LEVEL MATCH (on by default) brings both sources to the
    louder one's loudness first (a quiet card C is raised to the oscillators'), so the knob
    changes the sound and not the volume; off, each side plays as it is.

    The ends are the sources themselves (sample for sample with LEVEL MATCH off; with it, at
    the common loudness): with b = sqrt (4 p (1 - p)),
        out = (1 - p)(1 - b) x + b M + p (1 - b) y,
    so at 0 % the carrier, at 100 % card C, and at 50 % the morph M alone (a fusion, never a
    crossfade). A note begins on the sources as played (ATTACK sets how long, at least one
    analysis window) and glides into the morph.

    Real-time safe after prepare() (which allocates); deterministic (no randomness but the
    seeded noise and WEAVE's order, from the note's seed).
*/
class LiveMorph
{
public:
    static constexpr int maxPartials = 48;
    static constexpr int noiseBands = 8;
    static constexpr int hop = 256;              ///< host samples between analyses at the shortest window (a quarter of it)
    static constexpr int smallOrder = 10, largeOrder = 12;   ///< 21 .. 85 ms windows at 48 kHz
    /** The window for a note: its harmonics at least 8 bins apart (a window's leakage between
        them stays below -60 dB), so 1024 samples from 375 Hz up, 2048 from 188 Hz, else 4096
        (at 48 kHz; in proportion at other rates). */
    static int windowOrder (double lowestHz, double hostRate) noexcept;

    /** What every voice shares (one voice is analysed at a time): the transforms, their
        windows and a scratch buffer. Allocated once, by the synth. */
    struct Shared
    {
        Shared();
        std::array<osp::Fft, largeOrder - smallOrder + 1> transforms { osp::Fft (10), osp::Fft (11), osp::Fft (12) };
        std::array<std::vector<float>, largeOrder - smallOrder + 1> windows;
        std::array<float, largeOrder - smallOrder + 1> windowSum {}, windowPower {};
        std::vector<std::complex<double>> scratch;
    };

    void prepare (double internalRate, int oversampling, const Shared* shared);
    /** A note starts (its seed: the noise and WEAVE's order). */
    void start (std::uint64_t seed) noexcept;

    struct Control
    {
        double position = 0.5;            ///< 0 the carrier (A+B) .. 1 card C
        MorphMethod method = MorphMethod::fuse;
        double texture = 0.5, attack = 0.5, formants = 0.0, detail = 0.5;
        bool levelMatch = true;
        double carrierHz = 220.0, sourceHz = 220.0;   ///< the fundamentals each side plays
    };
    /** Control rate: the targets. */
    void control (const Control& c) noexcept;

    /** One internal sample: the carrier x (A+B) and the source y (card C). Returns the output. */
    float process (float carrier, float source) noexcept;
    /** The morph's core alone at its weight (b M), for LAYERED. */
    float lastCore() const noexcept { return coreOut; }

    /** Tests: each side's measured harmonics (dB, index h - 1) and residual bands (dB). */
    const std::array<float, maxPartials>& carrierPartialsDb() const noexcept { return xDb; }
    const std::array<float, maxPartials>& sourcePartialsDb() const noexcept { return yDb; }
    const std::array<float, noiseBands>& carrierResidualDb() const noexcept { return xNoiseDb; }
    const std::array<float, noiseBands>& sourceResidualDb() const noexcept { return yNoiseDb; }
    /** Tests: the morph's fundamental now (Hz) and whether its core is sounding. */
    double morphFundamental() const noexcept { return f0Morph; }
    bool coreReady() const noexcept { return analyses > 0 && warm >= 1.0f; }

    static constexpr float floorDb = -100.0f;
    static constexpr int envelopePoints = 72;   ///< the broad envelopes: 8 per octave from 50 Hz

private:
    struct Svf   // TPT state-variable band-pass (peak gain 1)
    {
        float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, s1 = 0.0f, s2 = 0.0f;
        void set (double hz, double q, double rate) noexcept;
        float band (float x) noexcept;
    };

    void analyse() noexcept;
    void measure (const std::vector<std::complex<double>>& z, int order, bool imaginary, double f0, std::array<float, maxPartials>& partials,
                  std::array<float, noiseBands>& residual) const noexcept;
    float envelopeAt (const std::array<float, maxPartials>& partials, double f0, double hz) const noexcept;
    /** A side's broad envelope (dB) at `hz`, from its table (made at each analysis). */
    float smoothEnvelopeAt (const std::array<float, envelopePoints>& table, double hz) const noexcept;
    void makeEnvelope (const std::array<float, maxPartials>& partials, double f0, std::array<float, envelopePoints>& table) const noexcept;
    void updateTargets() noexcept;
    void renderHost() noexcept;

    const Shared* shared = nullptr;
    double rate = 192000.0, hostRate = 48000.0;
    int factor = 4, phaseInHost = 0;

    // Input: each side decimated to the host rate (a box average of `factor` samples) into
    // its ring.
    std::vector<float> ringX, ringY;
    int ringMask = 0, write = 0, sinceAnalysis = 0;
    float accX = 0.0f, accY = 0.0f;
    int analyses = 0;

    // Analysis results.
    std::array<float, maxPartials> xDb {}, yDb {};
    std::array<float, noiseBands> xNoiseDb {}, yNoiseDb {};
    std::array<float, envelopePoints> xEnvelope {}, yEnvelope {};
    bool targetsDirty = true;
    double stepsF0 = -1.0;
    int stepsCount = -1;
    float xLevelDb = floorDb, yLevelDb = floorDb;

    Control settings;
    double f0Morph = 220.0;

    // Synthesis: rotators (cos, sin), their steps, amplitudes (now, target).
    int partialCount = 24;
    std::array<float, maxPartials> re {}, im {}, stepRe {}, stepIm {}, amp {}, ampTarget {};
    std::array<float, maxPartials> weaveOrder {};
    std::array<Svf, noiseBands> noiseBank {};
    std::array<float, noiseBands> noiseGain {}, noiseTarget {}, noiseNorm {};
    osp::Prng noise { 1 };
    float coreNow = 0.0f, corePrev = 0.0f, coreOut = 0.0f;
    float coreLevel = 0.0f, matchGain = 1.0f, matchTarget = 1.0f, smoothing = 0.01f;
    float levelX = 1.0f, levelY = 1.0f, levelXTarget = 1.0f, levelYTarget = 1.0f, levelCoef = 1.0e-4f;   ///< LEVEL MATCH: each side to the louder one
    int renormCountdown = 0;

    // The endpoint law's weights (smoothed per sample), and the attack's warm-up 0 .. 1.
    float wX = 0.5f, wCore = 0.0f, wY = 0.5f;
    float tX = 0.5f, tCore = 0.0f, tY = 0.5f;
    float warm = 0.0f, warmStep = 0.0f;
    int attackSamples = 0, age = 0;   ///< host samples since the note began
    bool fresh = true;
};

} // namespace andor::analog::morph
