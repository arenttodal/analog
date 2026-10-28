#pragma once

#include <algorithm>
#include <cmath>

namespace andor::analog
{

/**
    An exponential ADSR, the capacitor-charge shape of the original instrument's envelopes.

    ATTACK charges towards an overshoot target (1.3) and turns to DECAY on reaching 1, so the
    rise is convex and reaches the top in exactly the attack time. DECAY and RELEASE fall
    exponentially; their times are the time to cover 99 % of the way (-40 dB). A note-on
    restarts the attack from wherever the envelope is (no click); RELEASE ends below -100 dB.

    Times are set at control rate (setTimes), the envelope runs per sample. Real-time safe.
*/
class Envelope
{
public:
    enum class Stage { idle, attack, decay, sustain, release };

    static constexpr float attackTarget = 1.3f;
    static constexpr float idleThreshold = 1.0e-5f;   // -100 dB

    void setSampleRate (double rate) noexcept { sampleRate = rate; }

    void setTimes (double attackSeconds, double decaySeconds, double sustainLevel, double releaseSeconds) noexcept
    {
        // ln(1.3 / 0.3): time constants that reach 1 from 0 in the attack time.
        constexpr double attackLog = 1.4663370687934272;
        constexpr double fallLog = 4.605170185988092;   // ln 100
        attackCoef = coefficient (std::max (attackSeconds, 0.0005) / attackLog);
        decayCoef = coefficient (std::max (decaySeconds, 0.001) / fallLog);
        releaseCoef = coefficient (std::max (releaseSeconds, 0.001) / fallLog);
        sustain = static_cast<float> (std::clamp (sustainLevel, 0.0, 1.0));
    }

    void noteOn() noexcept { stage = Stage::attack; }
    void noteOff() noexcept
    {
        if (stage != Stage::idle)
            stage = Stage::release;
    }
    /** Silences at once (voice reset). */
    void kill() noexcept
    {
        stage = Stage::idle;
        level = 0.0f;
    }

    float next() noexcept
    {
        switch (stage)
        {
            case Stage::idle: return 0.0f;
            case Stage::attack:
                level += attackCoef * (attackTarget - level);
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:
                level += decayCoef * (sustain - level);
                if (std::abs (level - sustain) < 1.0e-5f)
                {
                    level = sustain;
                    stage = Stage::sustain;
                }
                break;
            case Stage::sustain:
                // Follows a moving SUSTAIN smoothly (a knob turned while a note holds).
                level += decayCoef * (sustain - level);
                break;
            case Stage::release:
                level -= releaseCoef * level;
                if (level < idleThreshold)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                break;
        }
        return level;
    }

    Stage currentStage() const noexcept { return stage; }
    bool isActive() const noexcept { return stage != Stage::idle; }
    bool isReleasing() const noexcept { return stage == Stage::release; }
    float value() const noexcept { return level; }

private:
    float coefficient (double tau) const noexcept { return static_cast<float> (1.0 - std::exp (-1.0 / (tau * sampleRate))); }

    double sampleRate = 48000.0;
    float attackCoef = 1.0f, decayCoef = 1.0f, releaseCoef = 1.0f, sustain = 1.0f;
    float level = 0.0f;
    Stage stage = Stage::idle;
};

} // namespace andor::analog
