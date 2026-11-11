#include "SubOsc.h"

#include <algorithm>
#include <cmath>

namespace andor::analog
{

namespace
{
    constexpr double twoPi = 6.283185307179586;

    /** Two-point PolyBLEP: the step's residual at `t` (0..1) for a phase increment `dt`. */
    inline float polyBlep (double t, double dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return static_cast<float> (t + t - t * t - 1.0);
        }
        if (t > 1.0 - dt)
        {
            t = (t - 1.0) / dt;
            return static_cast<float> (t * t + t + t + 1.0);
        }
        return 0.0f;
    }

    inline float dbToGain (double db) noexcept { return static_cast<float> (std::pow (10.0, db / 20.0)); }

    // A fourth-order Butterworth low-pass as two second-order sections: k = 1 / Q.
    constexpr float sectionK1 = 1.847759f, sectionK2 = 0.765367f;
}

void SubOsc::prepare (double sampleRate) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    attackStep = static_cast<float> (1.0 / (0.012 * rate));             // a 12 ms anti-click attack
    releaseCoef = static_cast<float> (std::exp (-6.9 / (0.22 * rate)));   // -60 dB in 220 ms
    rangeCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.012 * rate)));   // range / response / level glide
    quickCoef = static_cast<float> (std::exp (-6.9 / (0.005 * rate)));     // ALL SOUND OFF: 5 ms
    const double rc = 1.0 / (twoPi * 25.0);                                // the subsonic guard at 25 Hz
    hpCoef = static_cast<float> (rc / (rc + 1.0 / rate));
    setSettings (settings);
    reset();
}

void SubOsc::reset() noexcept
{
    for (auto& v : voices)
        v = Voice {};
    applyFilterTimes();
    held.fill (0);
    sustained.fill (false);
    pedal = false;
    bendSemitones = 0.0;
    toneState = hp1x = hp1y = hp2x = hp2y = 0.0f;
}

void SubOsc::setSettings (const SubSettings& s) noexcept
{
    settings = s;
    settings.octave = s.octave <= -2 ? -2 : -1;
    settings.lowKey = std::clamp (s.lowKey, 0, 127);
    settings.highKey = std::clamp (s.highKey, settings.lowKey, 127);
    // GRIT's tone: the harmonics it adds tamed from 9 kHz (a touch) to 2.2 kHz (all of it).
    const double toneHz = 9000.0 * std::pow (2200.0 / 9000.0, std::clamp (settings.grit, 0.0, 1.0));
    toneCoef = static_cast<float> (1.0 - std::exp (-twoPi * toneHz / rate));
    reconcile();
}

float SubOsc::keyGain (int key) const noexcept
{
    if (! inRange (key))
        return 0.0f;
    float g = dbToGain (settings.levelDb);
    if (settings.taper && key > settings.taperStart)
        g *= dbToGain (-settings.taperDbPerOctave * static_cast<double> (key - settings.taperStart) / 12.0);
    return g;
}

void SubOsc::startKey (int key, float velocity) noexcept
{
    // A voice already on this key (still sounding or fading) takes it again: no new attack.
    Voice* chosen = nullptr;
    for (auto& v : voices)
        if (v.key == key)
            chosen = &v;
    if (chosen == nullptr)
        for (auto& v : voices)
            if (v.key < 0)
            {
                chosen = &v;
                break;
            }
    // None free: the oldest released voice, else the oldest held one (it glides to the new key).
    if (chosen == nullptr)
        for (auto& v : voices)
            if (v.releasing && (chosen == nullptr || v.age < chosen->age))
                chosen = &v;
    if (chosen == nullptr)
        for (auto& v : voices)
            if (chosen == nullptr || v.age < chosen->age)
                chosen = &v;
    const bool fresh = chosen->key < 0;
    chosen->key = key;
    chosen->releasing = false;
    chosen->killed = false;
    chosen->velocity = velocity;
    chosen->age = ++clock;
    chosen->gainTarget = keyGain (key) * velocityGain (velocity);
    if (fresh)
    {
        chosen->gain = chosen->gainTarget;
        chosen->env = 0.0f;
        chosen->phase = 0.0;
        chosen->s1 = chosen->s2 = chosen->s3 = chosen->s4 = 0.0f;
    }
    // Its filter envelope starts again with each key, as a card's does.
    chosen->filterEnv.noteOn();
    chosen->filterCountdown = 0;
}

void SubOsc::setFilterFollow (const SubFilterFollow& f) noexcept
{
    const bool timesChanged = f.attack != follow.attack || f.decay != follow.decay || f.sustain != follow.sustain || f.release != follow.release;
    follow = f;
    if (timesChanged)
        applyFilterTimes();
}

void SubOsc::applyFilterTimes() noexcept
{
    for (auto& v : voices)
    {
        v.filterEnv.setSampleRate (rate);
        v.filterEnv.setTimes (follow.attack, follow.decay, follow.sustain, follow.release);
    }
}

float SubOsc::voiceCutoffHz (int index) const noexcept
{
    const auto& v = voices[static_cast<std::size_t> (std::clamp (index, 0, maxVoices - 1))];
    return follow.on && v.key >= 0 ? v.cutoffHz : 0.0f;
}

void SubOsc::updateCutoff (Voice& v) noexcept
{
    // The cards' cutoff for this key: base (TONE and its modulation), tracking around C4, the
    // envelope scaled by velocity.
    const float velocityScale = 1.0f - follow.velocityToEnv + follow.velocityToEnv * v.velocity;
    const float note = follow.baseNote + follow.keyTrack * static_cast<float> (v.key - 60)
                       + v.filterEnvNow * follow.envDepth * velocityScale;
    const double hz = std::clamp (440.0 * std::exp2 ((static_cast<double> (note) - 69.0) / 12.0), 8.0, 0.45 * rate);
    v.cutoffHz = static_cast<float> (hz);
    v.g = static_cast<float> (std::tan (twoPi * 0.5 * hz / rate));
}

float SubOsc::filterSample (Voice& v, float x) noexcept
{
    // Two TPT state-variable low-passes (Zavalishin), the cutoff shared.
    auto section = [g = v.g] (float in, float k, float& ic1, float& ic2) {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1, a3 = g * a2;
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;
    };
    const float y = section (section (x, sectionK1, v.s1, v.s2), sectionK2, v.s3, v.s4);
    for (float* s : { &v.s1, &v.s2, &v.s3, &v.s4 })
        if (std::abs (*s) < 1.0e-20f)
            *s = 0.0f;   // (no denormals)
    return y;
}

void SubOsc::releaseKey (int key) noexcept
{
    for (auto& v : voices)
        if (v.key == key)
        {
            v.releasing = true;
            v.filterEnv.noteOff();
        }
}

void SubOsc::releaseAll() noexcept
{
    for (auto& v : voices)
        if (v.key >= 0)
        {
            v.releasing = true;
            v.filterEnv.noteOff();
        }
}

void SubOsc::reconcile() noexcept
{
    // Off, or a key out of the range now: its voice fades (the release). On, a held key in the
    // range without a voice: it starts (turning SUB on, or widening the range, while playing).
    for (auto& v : voices)
        if (v.key >= 0)
        {
            if (! settings.enabled || ! inRange (v.key))
            {
                v.releasing = true;
                v.filterEnv.noteOff();
            }
            else
                v.gainTarget = keyGain (v.key) * velocityGain (v.velocity);
        }
    if (! settings.enabled)
        return;
    for (int key = 0; key < 128; ++key)
    {
        const auto k = static_cast<std::size_t> (key);
        if ((held[k] == 0 && ! sustained[k]) || ! inRange (key))
            continue;
        bool playing = false;
        for (const auto& v : voices)
            playing = playing || (v.key == key && ! v.releasing);
        if (! playing)
            startKey (key, heldVelocity[k]);
    }
}

void SubOsc::handleMidi (const std::uint8_t* data, int size) noexcept
{
    if (size < 1)
        return;
    const int status = data[0] & 0xf0;
    if ((status == 0x90 || status == 0x80) && size >= 3)
    {
        const int key = data[1] & 0x7f;
        const auto k = static_cast<std::size_t> (key);
        if (status == 0x90 && data[2] > 0)
        {
            held[k] = static_cast<std::uint8_t> (std::min (255, held[k] + 1));
            sustained[k] = false;
            heldVelocity[k] = static_cast<float> (data[2]) / 127.0f;
            if (settings.enabled && inRange (key))
                startKey (key, heldVelocity[k]);
        }
        else if (held[k] > 0)
        {
            if (--held[k] == 0)
            {
                if (pedal)
                    sustained[k] = true;
                else
                    releaseKey (key);
            }
        }
        return;
    }
    if (status == 0xb0 && size >= 3)
    {
        const int controller = data[1], value = data[2];
        if (controller == 64)
        {
            pedal = value >= 64;
            if (! pedal)
                for (int key = 0; key < 128; ++key)
                    if (sustained[static_cast<std::size_t> (key)])
                    {
                        sustained[static_cast<std::size_t> (key)] = false;
                        if (held[static_cast<std::size_t> (key)] == 0)
                            releaseKey (key);
                    }
        }
        else if (controller == 120 || controller == 123)   // ALL SOUND OFF (at once) / ALL NOTES OFF
        {
            held.fill (0);
            sustained.fill (false);
            releaseAll();
            if (controller == 120)
                for (auto& v : voices)
                    if (v.key >= 0)
                        v.killed = true;   // (only these: a note after it plays as usual)
        }
        else if (controller == 121)   // RESET ALL CONTROLLERS
        {
            bendSemitones = 0.0;
            if (pedal)
            {
                const std::uint8_t off[3] { 0xb0, 64, 0 };
                handleMidi (off, 3);
            }
        }
        return;
    }
    if (status == 0xe0 && size >= 3)
    {
        const int bend = ((data[2] & 0x7f) << 7 | (data[1] & 0x7f)) - 8192;
        bendSemitones = bendRange * static_cast<double> (bend) / 8192.0;
    }
}

float SubOsc::sample (Voice& v, double increment) noexcept
{
    float x;
    switch (settings.wave)
    {
        case SubSettings::Wave::sine:
            x = static_cast<float> (std::sin (twoPi * v.phase));
            break;
        case SubSettings::Wave::triangle:
            x = static_cast<float> (4.0 * std::abs (v.phase - 0.5) - 1.0);
            break;
        case SubSettings::Wave::square:
        {
            double half = v.phase + 0.5;
            if (half >= 1.0)
                half -= 1.0;
            x = (v.phase < 0.5 ? 1.0f : -1.0f) + polyBlep (v.phase, increment) - polyBlep (half, increment);
            x *= 0.7f;   // (as loud as the sine, near enough)
            break;
        }
        case SubSettings::Wave::saw:
        default:
            x = static_cast<float> (2.0 * v.phase - 1.0) - polyBlep (v.phase, increment);
            x *= 0.8f;
            break;
    }
    v.phase += increment;
    if (v.phase >= 1.0)
        v.phase -= 1.0;
    return x;
}

bool SubOsc::sounding() const noexcept
{
    for (const auto& v : voices)
        if (v.key >= 0)
            return true;
    return std::abs (hp2y) > 1.0e-7f || std::abs (hp1y) > 1.0e-7f;
}

int SubOsc::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.key >= 0 && ! v.releasing ? 1 : 0;
    return n;
}

void SubOsc::render (float* left, float* right, int n) noexcept
{
    if (! sounding())
    {
        hp1x = hp1y = hp2x = hp2y = toneState = 0.0f;
        return;
    }
    const float level = std::max (1.0e-6f, dbToGain (settings.levelDb));
    const double grit = std::clamp (settings.grit, 0.0, 1.0);
    const float drive = static_cast<float> (1.0 + 7.0 * grit);
    std::array<double, maxVoices> increments {};
    for (std::size_t i = 0; i < voices.size(); ++i)
        if (voices[i].key >= 0)
        {
            const double note = static_cast<double> (voices[i].key) + 12.0 * settings.octave + bendSemitones;
            increments[i] = std::min (0.45, 440.0 * std::exp2 ((note - 69.0) / 12.0) / rate);
        }
    for (int s = 0; s < n; ++s)
    {
        float sum = 0.0f;
        for (std::size_t i = 0; i < voices.size(); ++i)
        {
            auto& v = voices[i];
            if (v.key < 0)
                continue;
            if (v.killed)
                v.env *= quickCoef;
            else if (v.releasing)
                v.env *= releaseCoef;
            else
                v.env = std::min (1.0f, v.env + attackStep);
            v.gain += rangeCoef * (v.gainTarget - v.gain);
            float x = sample (v, increments[i]);
            if (follow.on)
            {
                // The instrument's filter: its envelope runs per sample, the cutoff at control rate.
                v.filterEnvNow = v.filterEnv.next();
                if (--v.filterCountdown <= 0)
                {
                    v.filterCountdown = filterInterval;
                    updateCutoff (v);
                }
                x = filterSample (v, x);
            }
            sum += x * v.env * v.gain;
            if (v.releasing && v.env < 1.0e-5f)
            {
                v = Voice {};
                v.filterEnv.setSampleRate (rate);
                v.filterEnv.setTimes (follow.attack, follow.decay, follow.sustain, follow.release);
            }
        }
        if (grit > 0.0)
        {
            // GRIT: soft saturation measured against the level (a single key at full is driven
            // as hard as GRIT says, a quiet one hardly), its new harmonics tamed.
            const float u = sum / level;
            sum = level * std::tanh (drive * u) / drive * (1.0f + 0.35f * static_cast<float> (grit));
            toneState += toneCoef * (sum - toneState);
            sum = toneState;
        }
        // The subsonic guard: two one-pole high-passes at 25 Hz (and no DC).
        hp1y = hpCoef * (hp1y + sum - hp1x);
        hp1x = sum;
        hp2y = hpCoef * (hp2y + hp1y - hp2x);
        hp2x = hp1y;
        if (std::abs (hp2y) < 1.0e-20f)
            hp2y = 0.0f;   // (no denormals)
        left[s] += hp2y;
        right[s] += hp2y;
    }
}

} // namespace andor::analog
