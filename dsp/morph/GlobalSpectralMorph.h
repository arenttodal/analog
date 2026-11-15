#pragma once

#include "fx/FxDsp.h"

#include <array>
#include <atomic>
#include <complex>
#include <cstdint>
#include <vector>

namespace andor::analog
{

/**
    GLOBAL MORPH 2.0 (docs/GLOBAL_MORPH_2.md): MORPH TO D's stage. A global stereo processor -
    one instance, never per voice - between the synth's bus and D's.

    Inputs, each stereo and sample-aligned: S, the whole synth (A/B/C after the FILTER, C's
    buses); K, the CARRIER - the part of S that morphs (OSC 1, OSC 2 or both, tapped from the
    voices through their own filter: S - K, the rest, never morphs and leaves towards 100 %);
    and D, after its FILTER and LEVEL.

    An STFT (sqrt-Hann analysis and synthesis, 75 % overlap; N = 1024 samples up to 50 kHz,
    2048 to 100 kHz, 4096 above: about 21 ms) analyses K and D every hop. Their power spectra
    are linked across each stereo pair (one gain per bin applied to both channels: no image
    wandering) and smoothed into envelopes over a band DETAIL sets (an octave .. a semitone).
    D's envelope is followed in time (its attack by ATTACK, a 250 ms release: a loop's
    analysis is held between its hits and decays gracefully) and moved by FORMANTS (its
    envelope read 2^(-st/12) along the frequency axis - D's pitch is never shifted).

    The methods (the morph's core M, per bin):
    - IMPRINT: the carrier with D's envelope - K x (E_D / E_K)^(d/2), d = min (1, 2p): the
      synth keeps its pitch and fine structure and takes D's spectral shape, complete from
      50 %. TEXTURE above its middle adds D's own residual (breath, bow, drum noise).
    - FUSE: both sources reshaped to one shared envelope, a power mean between them
      ((1-q) E_K^a + q E_D^a)^(1/a) with a = 0.35 (between the geometric and the arithmetic
      mean, so a bin only one source fills is not wiped out), their fine structures summed at
      equal power: neither the synth's timbre nor D's - one fused sound. Pitch structures are
      both kept as they are (no fundamental is interpolated on a polyphonic bus).
    - WEAVE: the spectrum divided into log-spaced regions (DETAIL: 6 .. 40), each owned by K
      or D: region r passes to D as the position crosses its seeded threshold (deterministic,
      a fixed seed), its boundary blended over a raised cosine in frequency and its turn over a
      short span of the position - interleaved regions of each source, each at its own level.
    TEXTURE (0.5 neutral) scales the residual - the bins that are not spectral peaks - from
    removed (0) to +12 dB (1). ATTACK sets how fast D's envelope rises (80 ms .. instant) and
    how much of D's own transient frames (spectral flux) pass into the core.

    The output (owner's decisions, 15 November 2026), with b = sqrt (4 p (1 - p)):
    - MORPH ONLY: rest x cos (pi p / 2) + K (1 - p)(1 - b) + b M + D p (1 - b).
    - LAYERED: (S cos (pi p / 2) + D sin (pi p / 2) + b M) / sqrt (1 + b^2).
    0 % is S and 100 % is D, sample for sample (delayed by the latency); LEVEL MATCH (on)
    glides the loudness between S's and D's measured levels (300 ms, dB, within +-12 dB, never
    lifting below -60 dBFS), scaled by b: nothing at the ends.

    Pure C++, deterministic; prepare() allocates, nothing else does.
*/
class GlobalSpectralMorph
{
public:
    enum class Method { imprint, fuse, weave };
    enum class Output { morphOnly, layered };
    struct Controls
    {
        Method method = Method::imprint;
        double position = 0.0;    ///< MORPH 0..1 (D's availability already applied)
        double texture = 0.5;     ///< 0..1, 0.5 neutral
        double attack = 0.5;      ///< 0..1
        double formants = 0.0;    ///< semitones, -12..12
        double detail = 0.5;      ///< 0..1
        Output output = Output::morphOnly;
        bool levelMatch = true;
    };

    static constexpr int displayBands = 32;   ///< telemetry: log bands 60 Hz .. 16 kHz
    static constexpr int maxWeaveRegions = 41;

    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    void setControls (const Controls& c) noexcept { controls = c; }
    const Controls& currentControls() const noexcept { return controls; }

    /** In place on S: sL / sR the synth in, the result out. kL / kR the carrier (a part of S),
        dL / dR D. Any n (internally hop by hop). */
    void process (float* sL, float* sR, const float* kL, const float* kR, const float* dL, const float* dR, int n) noexcept;

    /** The stage's latency (samples): every path through it, dry ones included. */
    int latencySamples() const noexcept { return size; }
    int fftSize() const noexcept { return size; }

    /** Telemetry (any thread, relaxed): the last frame's band levels (dB re full scale) of the
        carrier, of D as the morph uses it (after FORMANTS) and of the morph's result; WEAVE's
        region ownership per band (0 K .. 1 D); D's transient strength and presence. */
    float carrierDb (int band) const noexcept { return shownK[static_cast<std::size_t> (band)].load (std::memory_order_relaxed); }
    float destinationDb (int band) const noexcept { return shownD[static_cast<std::size_t> (band)].load (std::memory_order_relaxed); }
    float resultDb (int band) const noexcept { return shownM[static_cast<std::size_t> (band)].load (std::memory_order_relaxed); }
    float ownership (int band) const noexcept { return shownOwner[static_cast<std::size_t> (band)].load (std::memory_order_relaxed); }
    float transient() const noexcept { return shownTransient.load (std::memory_order_relaxed); }
    float presence() const noexcept { return shownPresence.load (std::memory_order_relaxed); }
    double displayBandHz (int band) const noexcept;
    int frames() const noexcept { return frameCount.load (std::memory_order_relaxed); }

private:
    using Complex = std::complex<float>;
    void frame() noexcept;
    void method (Method m, double p, std::vector<Complex>& outL, std::vector<Complex>& outR) noexcept;
    void smoothAcross (const std::vector<float>& power, std::vector<float>& envelope, double octaves) noexcept;
    float weaveOwnership (int bin, double p) const noexcept;

    double rate = 48000.0;
    int size = 1024, hop = 256, bins = 513;
    fx::dsp::Fft fft;
    std::vector<float> window;
    // Input frames (the last N samples), filled hop by hop.
    std::array<std::vector<float>, 4> in;   ///< kL, kR, dL, dR
    int fill = 0;
    // Overlap-add of the core M, and its finished hop.
    std::array<std::vector<float>, 2> acc, ready;
    int readyAt = 0;
    // The dry paths, delayed by the latency (S, K, D).
    std::array<std::vector<float>, 6> dry;
    int dryAt = 0;
    // Spectra and envelopes.
    std::vector<Complex> work, kSpecL, kSpecR, dSpecL, dSpecR, mL, mR, prevL, prevR;
    std::vector<float> powK, powD, envK, envD, envDShifted, fineK, fineD, tonalK, tonalD, prefix, envDFollow, envKFollow, powDPrev;
    std::array<float, maxWeaveRegions + 1> weaveThreshold {};
    std::vector<float> binOctave;   ///< log2 (bin's frequency / 60 Hz), for WEAVE's regions
    Controls controls;
    Method previousMethod = Method::imprint, blendMethod = Method::imprint;
    int methodBlend = 0;   ///< frames left of a method change's crossfade
    static constexpr int methodBlendFrames = 12;   ///< (about 64 ms at 48 kHz)
    double framePosition = 0.0, positionNow = 0.0, positionStep = 0.0;
    float presenceNow = 0.0f, holdSeconds = 0.0f, transientNow = 0.0f;
    // LEVEL MATCH (time domain, after the output law).
    double powS = 0.0, powDry = 0.0, powOut = 0.0, levelSmooth = 0.0, matchDb = 0.0, matchStep = 0.0;
    std::array<std::atomic<float>, displayBands> shownK {}, shownD {}, shownM {}, shownOwner {};
    std::atomic<float> shownTransient { 0.0f }, shownPresence { 0.0f };
    std::atomic<int> frameCount { 0 };
    std::array<double, displayBands + 1> displayEdges {};
};

} // namespace andor::analog
