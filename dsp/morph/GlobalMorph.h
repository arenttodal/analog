#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace andor::analog
{

/**
    EXPERIMENT (docs/experiments/KONTAKT_EXTERNAL_MORPH.md): GLOBAL MORPH between two stereo buses -
    the oscillators' A + B (the carrier) and card C's external instrument (Kontakt) - at bus level,
    never per voice (LiveMorph's per-note morph is untouched).

    The method: a multiband spectral IMPRINT, chosen over an FFT vocoder for this prototype
    because it adds no latency (both endpoints stay sample-aligned with no compensation) and
    keeps the carrier's own phase character and transients.
    - Analysis: 24 third-octave bands (70 Hz .. 14 kHz, two cascaded band-passes each) on both
      buses; each band's energy followed (3 ms up, 60 ms down), the carrier's linked across its
      stereo pair so its image never wanders.
    - Each band's target gain takes the carrier's band energy towards C's:
      g = (E_C / E_AB)^(d * a), clamped to -36 .. +18 dB and glided (10 ms). d is the imprint's
      depth; a is C's activity (0 while C is silent: the carrier is left alone - never shaped by
      nothing, never noise lifted).
    - The hybrid is the carrier through a 24-band graphic EQ (peaking filters at the same
      centres, Q 4.3) set to those gains. Every band at 0 dB is the identity: 0 % is the
      carrier exactly. (A first version added band-passed differences back onto the carrier; the
      band-passes do not sum to the signal, so most of it passed unshaped: 14 dB from C's
      envelope at 50 % - measured, replaced.)
    - Level: the hybrid's energy is followed and steered (80 ms, +-12 dB) to the geometric
      interpolation of the two buses' energies, E_AB^(1-d) * E_C^d: where C leaves bands empty
      the cut energy is made up, so the sweep never sags (the graphic EQ's overlap and the gain
      clamps would otherwise leave it up to 9 dB low with a mono line on C).

    POSITION (MORPH): 0 the carrier exactly; 0 .. 0.5 the imprint deepening (d = 2 x: the
    carrier taking C's spectral envelope - not a crossfade); 0.5 the full hybrid, at C's band
    energies (level-matched to C); 0.5 .. 1 an equal-power fade from the hybrid to C; 1 C exactly.
    Pure C++, deterministic, no allocation after prepare().
*/
class GlobalMorph
{
public:
    static constexpr int bandCount = 24;

    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;

    /**
        left / right: the carrier in, the result out. cLeft / cRight: C (already at its level).
        `position` 0..1 (smoothed here, 15 ms).
    */
    void process (float* left, float* right, const float* cLeft, const float* cRight, int n, double position) noexcept;

    /** C's activity now (0 silent .. 1), and the imprint's band gains (dB): tests, telemetry. */
    float activity() const noexcept { return act; }
    float bandGainDb (int band) const noexcept;
    double bandCentreHz (int band) const noexcept { return centres[static_cast<std::size_t> (band)]; }

private:
    struct Biquad
    {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    struct Band
    {
        std::array<Biquad, 2> l, r, c;   ///< two cascaded band-passes per signal (analysis)
        double energyAb = 0.0, energyC = 0.0;
        double gainDb = 0.0, targetDb = 0.0, setDb = 0.0;
        // The graphic EQ's peaking filter (direct form I: coefficients may move every 32 samples).
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double xl1 = 0, xl2 = 0, yl1 = 0, yl2 = 0, xr1 = 0, xr2 = 0, yr1 = 0, yr2 = 0;
        void setPeak (double hz, double db, double rate) noexcept;
    };

    double rate = 48000.0;
    std::array<Band, bandCount> bands {};
    std::array<double, bandCount> centres {};
    double attack = 0.0, release = 0.0, gainStep = 0.0, positionStep = 0.0, activityStep = 0.0;
    bool shaping = false;   ///< some band away from 0 dB
    double outEnergy = 0.0, abEnergy = 0.0, cEnergy = 0.0, levelSmooth = 0.0, makeUpDb = 0.0, makeUp = 1.0, makeUpTarget = 0.0;
    double positionNow = 0.0;
    float act = 0.0f;
    double activityNow = 0.0;
    int control = 0;
};

} // namespace andor::analog
