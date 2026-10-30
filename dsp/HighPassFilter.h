#pragma once

#include "AnalogMath.h"

namespace andor::analog
{

/** Which instrument's high-pass the four positions follow (see VoiceHighPass). */
enum class HpfVariant { juno6, juno60, juno106, jupiter };

/**
    The per-voice high-pass before the low-pass filter: a fixed-position, non-resonant
    6 dB/oct high-pass (or, on the Juno-106's position 0, a bass shelf), at the internal
    (oversampled) rate. Bypass is the caller's (skip the call); FLAT positions are an exact
    pass-through anyway.

    One structure serves every position: a topology-preserving (TPT) one-pole low-pass,
    prewarped (g = tan (pi fc / fs), the bilinear transform with its corner exactly at fc), and
        y = x + m * lowpass (x)
    m = -1: the one-pole high-pass x - lp (6 dB/oct below fc, -3 dB at fc);
    m = A - 1 (A > 1): a low shelf, gain A at DC and 1 at high frequencies, half its dB gain
        near fc;
    m = 0: flat (the output is x itself, bit for bit).

    Switching positions while playing: the coefficients (g and m) move along a linear ramp
    over 20 ms (`rampSeconds`), so a switch is a short, smooth sweep, not a step. The TPT
    low-pass keeps its state meaningful while g moves (its state is the capacitor's), and it
    keeps running on FLAT, so leaving FLAT starts from a settled filter. Coefficients are
    computed only when the variant or position changes (and one division per sample during a
    ramp).

    Frequencies and sources. The originals' values were not available from Roland's documents
    (owner's manuals and spec sheets list the controls, not their frequencies); the values
    below come from third-party re-creations and are approximations, to be verified by ear or
    by measuring an instrument:

    JUNO-106 (4-position switch; HPF selected by a 4052 analog switch):
      0 BOOST: low shelf, +6 dB at DC, corner 150 Hz.  APPROXIMATION. The KR-106 emulation's
               DSP notes model it as "+10 dB below 150 Hz"; SoundForce (VCF/VCA 6, a Juno-60/
               106 filter re-creation) describes it only as a "slight" bass boost. We take the
               corner from the former and a boost between the two descriptions.
      1 FLAT   (both sources agree).
      2 240 Hz one-pole high-pass (KR-106; SoundForce: 225 Hz).  APPROXIMATION.
      3 720 Hz one-pole high-pass (both sources).  Likely, not measured by us.
    JUNO-6 / JUNO-60 (HPF slider; we map our 4 positions to flat + three cutoffs):
      0 FLAT, 1 225 Hz, 2 339 Hz, 3 720 Hz: the three cut positions of SoundForce's VCF/VCA 6
      (its Juno-60-style steps).  APPROXIMATION. The Juno-6 and Juno-60 share the voice
      circuit; no source distinguishes their HPFs, so they use the same values.
    JUPITER-8 (continuous HPF slider, non-resonant 6 dB/oct):
      0 FLAT, 1 120 Hz, 2 350 Hz, 3 1 kHz.  APPROXIMATION, NOT SOURCED: no measured range was
      found (the owner's manual gives none); chosen as a roughly logarithmic spread over the
      slider's useful range, to be verified by measurement.

    Sources consulted (October 2026): Roland JUNO-106 owner's manual and technical
    specifications ("HPF cutoff frequency (0/1/2/3)"); SoundForce VCF/VCA 6 product notes
    ("position 0: the bass boost from the 106; position 1: no filtering; positions 2 to 4:
    225 Hz, 339 Hz and 720 Hz"); ultramaster KR-106 docs/DSP_ARCHITECTURE.md (position 0:
    "+10 dB below 150 Hz", 2: 240 Hz, 3: 720 Hz). No code was taken from any of them.

    Real-time safe: no allocation; prepare() before use.
*/
class VoiceHighPass
{
public:
    static constexpr double rampSeconds = 0.02;

    /** sampleRate: the internal (oversampled) rate. */
    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        rampLength = std::max (1, static_cast<int> (std::lround (rampSeconds * rate)));
        targetG = gFor (targetHz);
        jumpToTarget();
    }

    void reset() noexcept
    {
        s = 0.0f;
        jumpToTarget();
    }

    /** position 0..3 (see the class note). Recomputes the coefficients only on a change; a
        change while playing ramps over 20 ms. */
    void setMode (HpfVariant newVariant, int newPosition) noexcept
    {
        newPosition = std::clamp (newPosition, 0, 3);
        if (newVariant == variant && newPosition == position)
            return;
        variant = newVariant;
        position = newPosition;

        const Setting setting = settingFor (variant, position);
        targetMix = setting.mix;
        if (setting.hz > 0.0)   // FLAT keeps the corner where it is: nothing to sweep
            targetHz = setting.hz;
        if (rate <= 0.0)
            return;   // prepare() will take the target
        targetG = gFor (targetHz);
        remaining = rampLength;
        stepG = (targetG - g) / static_cast<float> (rampLength);
        stepMix = (targetMix - mix) / static_cast<float> (rampLength);
    }

    float process (float x) noexcept
    {
        if (remaining > 0)
        {
            if (--remaining == 0)
            {
                g = targetG;
                mix = targetMix;
            }
            else
            {
                g += stepG;
                mix += stepMix;
            }
            G = g / (1.0f + g);
        }

        const float v = G * (x - s);
        const float lp = v + s;
        const float next = lp + v;
        s = std::abs (next) < 1.0e-20f ? 0.0f : next;   // no denormals in a decaying tail
        return mix == 0.0f ? x : x + mix * lp;
    }

private:
    struct Setting
    {
        double hz;   // the low-pass corner (0: FLAT, keep the current one)
        float mix;   // m in y = x + m * lowpass (x)
    };

    static constexpr float highPass = -1.0f;
    static constexpr double junoBoostDb = 6.0;   // APPROXIMATION, see the class note
    static constexpr double junoBoostHz = 150.0;

    static Setting settingFor (HpfVariant variant, int position) noexcept
    {
        switch (variant)
        {
            case HpfVariant::juno106:
            {
                constexpr double cuts[] = { 0.0, 0.0, 240.0, 720.0 };
                if (position == 0)
                    return { junoBoostHz, math::dbToGain (junoBoostDb) - 1.0f };
                if (position == 1)
                    return { 0.0, 0.0f };
                return { cuts[position], highPass };
            }
            case HpfVariant::jupiter:
            {
                constexpr double cuts[] = { 0.0, 120.0, 350.0, 1000.0 };
                return position == 0 ? Setting { 0.0, 0.0f } : Setting { cuts[position], highPass };
            }
            case HpfVariant::juno6:
            case HpfVariant::juno60:
            default:
            {
                constexpr double cuts[] = { 0.0, 225.0, 339.0, 720.0 };
                return position == 0 ? Setting { 0.0, 0.0f } : Setting { cuts[position], highPass };
            }
        }
    }

    float gFor (double hz) const noexcept
    {
        return static_cast<float> (std::tan (math::pi * std::min (hz, 0.45 * rate) / rate));
    }

    void jumpToTarget() noexcept
    {
        remaining = 0;
        g = targetG;
        mix = targetMix;
        G = g / (1.0f + g);
    }

    double rate = 0.0;
    int rampLength = 1, remaining = 0;
    HpfVariant variant = HpfVariant::juno106;
    int position = 1;                     // FLAT
    double targetHz = 225.0;              // a corner to start from when leaving FLAT
    float targetG = 0.0f, targetMix = 0.0f;
    float g = 0.0f, G = 0.0f, mix = 0.0f, stepG = 0.0f, stepMix = 0.0f;
    float s = 0.0f;
};

} // namespace andor::analog
